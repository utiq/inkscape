// SPDX-License-Identifier: GPL-2.0-or-later
#include "pixelstreamer.h"
#include <cassert>
#include <cmath>
#include <vector>

namespace {

constexpr auto roundup(int x, int m)
{
    return ((x - 1) / m + 1) * m;
}

constexpr cairo_user_data_key_t key{};

} // namespace

namespace Inkscape {

class PersistentPixelStreamer : public PixelStreamer
{
    static constexpr int bufsize = 0x1000000; // 16 MiB

    struct Buffer
    {
        GLuint pbo;          // Pixel buffer object.
        unsigned char *data; // The pointer to the mapped region.
        int off;             // Offset of the unused region, in bytes. Always a multiple of 64.
        int refs;            // How many mappings are currently using this buffer.
        GLsync sync;         // Sync object for telling us when the GPU has finished reading from this buffer.

        void create()
        {
            glGenBuffers(1, &pbo);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glBufferStorage(GL_PIXEL_UNPACK_BUFFER, bufsize, nullptr, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
            data = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bufsize, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
            off = 0;
            refs = 0;
        }

        void destroy()
        {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            glDeleteBuffers(1, &pbo);
        }
    };
    std::vector<Buffer> buffers;

    int current_buffer;

    struct Mapping
    {
        bool used;                 // Whether the mapping is in use, or on the freelist.
        int buf;                   // The buffer the mapping is using.
        int off;                   // Offset of the mapped region.
        int size;                  // Size of the mapped region.
        int width, height, stride; // Image properties.
    };
    std::vector<Mapping> mappings;

    /*
     * A Buffer can be in any one of three states:
     *
     *     1. Current                 -->  We are currently filling this buffer up with allocations.
     *     2. Not current, refs > 0   -->  Finished the above, but may still be writing into it and issuing GL commands from it.
     *     3. Not current, refs == 0  -->  Finished the above, but GL may be reading from it.
     *
     * Only one Buffer is Current at any given time, and is marked by the current_buffer variable.
     *
     * When a Buffer enters the last state, a fence sync object is created. We only recycle the Buffer as the current
     * buffer once this sync object has been signalled. When the Buffer leaves this state, the sync object is deleted.
     */

public:
    PersistentPixelStreamer()
    {
        // Create a single initial buffer and make it the current buffer.
        buffers.emplace_back();
        buffers.back().create();
        current_buffer = 0;
    }

    Method get_method() const override { return Method::Persistent; }

    Cairo::RefPtr<Cairo::ImageSurface> request(const Geom::IntPoint &dimensions) override
    {
        // Calculate image properties required by cairo.
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dimensions.x());
        int size = stride * dimensions.y();
        int sizeup = roundup(size, 64);
        assert(sizeup < bufsize);

        // Continue using the current buffer if possible.
        if (buffers[current_buffer].off + sizeup <= bufsize) {
            goto chosen_buffer;
        }
        // Otherwise, the current buffer has filled up. After this point, the current buffer will change.
        // Therefore, handle the state change of the current buffer out of the Current state. That means
        // creating the sync object for it if necessary. (Handle the transition 1 --> 3.)
        if (buffers[current_buffer].refs == 0) {
            buffers[current_buffer].sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
        // Attempt to re-use an old buffer.
        for (int i = 0; i < buffers.size(); i++) {
            // Automatically skip the previous current buffer. (In a limbo state at the moment, but will move to 2 or 3 shortly.)
            if (i == current_buffer) continue;
            // Skip buffers that we are still writing into. (In state 2.)
            if (buffers[i].refs > 0) continue;
            // Skip buffers that we've finished with, but GL is still reading from. (In state 3, but not ready to leave.)
            auto ret = glClientWaitSync(buffers[i].sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
            if (!(ret == GL_CONDITION_SATISFIED || ret == GL_ALREADY_SIGNALED)) continue;
            // Found an unused buffer. Re-use it. (Move to state 1.)
            glDeleteSync(buffers[i].sync);
            buffers[i].off = 0;
            current_buffer = i;
            goto chosen_buffer;
        }
        // Otherwise, there are no available buffers. Create and use a new one.
        buffers.emplace_back();
        buffers.back().create();
        current_buffer = buffers.size() - 1;
    chosen_buffer:
        // Finished changing the current buffer.
        auto &b = buffers[current_buffer];

        // Choose/create the mapping to use.
        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++) {
                if (!mappings[i].used) {
                    // Found unused mapping.
                    return i;
                }
            }
            // No free mapping; create one.
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        // Set up the mapping bookkeeping.
        m = {true, current_buffer, b.off, size, dimensions.x(), dimensions.y(), stride};
        b.off += sizeup;
        b.refs++;

        // Create the image surface.
        auto surface = Cairo::ImageSurface::create(b.data + m.off, Cairo::FORMAT_ARGB32, dimensions.x(), dimensions.y(), stride);

        // Attach the mapping handle as user data.
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);

        return surface;
    }

    Texture finish(Cairo::RefPtr<Cairo::ImageSurface> surface) override
    {
        // Extract the mapping handle from the surface's user data.
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);

        // Flush all changes from the image surface to the buffer, and delete it.
        surface.clear();

        auto &m = mappings[mapping];
        auto &b = buffers[m.buf];

        // Flush the mapped subregion.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, b.pbo);
        glFlushMappedBufferRange(GL_PIXEL_UNPACK_BUFFER, m.off, m.size);

        // Tear down the mapping bookkeeping. (if this causes transition 2 --> 3, it is handled below.)
        m.used = false;
        b.refs--;

        // Create the texture from the mapped subregion.
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, m.width, m.height);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m.width, m.height, GL_BGRA, GL_UNSIGNED_BYTE, (void*)(uintptr_t)m.off);
        // Note: Could consider recycling textures rather than recreating them each time as above. But this is difficult because
        // our textures are all of different sizes, yet we want to do linear filtering with clamp-to-edge. Furthermore, our usage
        // pattern is few, large textures. That means the bottleneck is expected to lie in upload speed, not GPU texture storage
        // reallocation. So this optimisation is deemed unhelpful/unnecessary.

        // If the buffer is due for recycling, issue a sync command so that we can recycle it when it's ready. (Handle transition 2 --> 3.)
        if (m.buf != current_buffer && b.refs == 0) {
            b.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }

        // Return the texture.
        return Texture(tex, {m.width, m.height});
    }

    ~PersistentPixelStreamer() override
    {
        // Delete any sync objects. (For buffers in state 3.)
        for (int i = 0; i < buffers.size(); i++) {
            if (i != current_buffer && buffers[i].refs == 0) {
                glDeleteSync(buffers[i].sync);
            }
        }

        // Wait for GL to finish reading out of all the buffers.
        glFinish();

        // Deallocate the buffers on the GL side.
        for (auto &b : buffers) {
            b.destroy();
        }
    }
};

class AsynchronousPixelStreamer : public PixelStreamer
{
    static constexpr int bufsize_multiple = 0x100000; // 1 MiB

    struct Mapping
    {
        bool used;
        GLuint pbo;
        unsigned char *data;
        int size, width, height, stride;
    };
    std::vector<Mapping> mappings;

public:
    Method get_method() const override { return Method::Asynchronous; }

    Cairo::RefPtr<Cairo::ImageSurface> request(const Geom::IntPoint &dimensions) override
    {
        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++)
                if (!mappings[i].used)
                    return i;
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        m.used = true;
        m.width = dimensions.x();
        m.height = dimensions.y();
        m.stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, m.width);
        m.size = m.stride * m.height;
        int bufsize = roundup(m.size, bufsize_multiple);

        glGenBuffers(1, &m.pbo);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m.pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, bufsize, nullptr, GL_STREAM_DRAW);
        m.data = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, m.size, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);

        auto surface = Cairo::ImageSurface::create(m.data, Cairo::FORMAT_ARGB32, m.width, m.height, m.stride);
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);
        return surface;
    }

    Texture finish(Cairo::RefPtr<Cairo::ImageSurface> surface) override
    {
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);
        surface.clear();

        auto &m = mappings[mapping];

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m.pbo);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m.width, m.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

        glDeleteBuffers(1, &m.pbo);

        m.used = false;

        return Texture(tex, {m.width, m.height});
    }
};

class SynchronousPixelStreamer : public PixelStreamer
{
    struct Mapping
    {
        bool used;
        std::vector<unsigned char> data;
        int size, width, height, stride;
    };
    std::vector<Mapping> mappings;

public:
    Method get_method() const override { return Method::Synchronous; }

    Cairo::RefPtr<Cairo::ImageSurface> request(const Geom::IntPoint &dimensions) override
    {
        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++)
                if (!mappings[i].used)
                    return i;
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        m.used = true;
        m.width = dimensions.x();
        m.height = dimensions.y();
        m.stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, m.width);
        m.size = m.stride * m.height;
        m.data.resize(m.size);

        auto surface = Cairo::ImageSurface::create(&m.data[0], Cairo::FORMAT_ARGB32, m.width, m.height, m.stride);
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);
        return surface;
    }

    Texture finish(Cairo::RefPtr<Cairo::ImageSurface> surface) override
    {
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);
        surface.clear();

        auto &m = mappings[mapping];

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m.width, m.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, &m.data[0]);

        m.used = false;
        m.data.clear();

        return Texture(tex, {m.width, m.height});
    }
};

template<> std::unique_ptr<PixelStreamer> PixelStreamer::create<PixelStreamer::Method::Persistent>()   {return std::make_unique<PersistentPixelStreamer>();}
template<> std::unique_ptr<PixelStreamer> PixelStreamer::create<PixelStreamer::Method::Asynchronous>() {return std::make_unique<AsynchronousPixelStreamer>();}
template<> std::unique_ptr<PixelStreamer> PixelStreamer::create<PixelStreamer::Method::Synchronous>()  {return std::make_unique<SynchronousPixelStreamer>();}

template<>
std::unique_ptr<PixelStreamer> PixelStreamer::create<PixelStreamer::Method::Auto>()
{
    int ver = epoxy_gl_version();

    if (ver >= 30 || epoxy_has_gl_extension("GL_ARB_map_buffer_range")) {
        if (ver >= 44 || (epoxy_has_gl_extension("GL_ARB_buffer_storage") &&
                          epoxy_has_gl_extension("GL_ARB_texture_storage") &&
                          epoxy_has_gl_extension("GL_ARB_SYNC"))) {
            return create<Method::Persistent>();
        }
        return create<Method::Asynchronous>();
    }
    return create<Method::Synchronous>();
}

std::unique_ptr<PixelStreamer> PixelStreamer::create(Method method)
{
    switch (method)
    {
        case Method::Persistent:   return create<Method::Persistent>();
        case Method::Asynchronous: return create<Method::Asynchronous>();
        case Method::Synchronous:  return create<Method::Synchronous>();
        case Method::Auto:         return create<Method::Auto>();
        default: return nullptr; // Never triggered, but GCC errors out on build without.
    }
}

} // namespace Inkscape
