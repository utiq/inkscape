// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_GL_TEXTURE_H
#define INKSCAPE_GL_TEXTURE_H

#include <2geom/point.h>
#include <epoxy/gl.h>

namespace Inkscape {

class Texture
{
    GLuint id;
    Geom::IntPoint size;

public:
    // Create null texture owning no resources.
    Texture() : id(0) {}

    // Allocate a blank texture of a given size.
    Texture(const Geom::IntPoint &size);

    // Wrap an existing texture.
    Texture(GLuint id, const Geom::IntPoint &size) : id(id), size(size) {}

    // Boilerplate constructors/operators
    Texture(const Texture&) = delete;
    Texture(Texture &&other) noexcept : id(0) {*this = std::move(other);}
    Texture &operator=(Texture &&other) noexcept;
    ~Texture() {if (id) glDeleteTextures(1, &id);}

    // Observers
    GLuint get_id() const {return id;}
    const Geom::IntPoint &get_size() const {return size;}
    explicit operator bool() const {return id;}

    // Methods
    void clear() {this->~Texture(); new (this) Texture();}
};

} // namespace Inkscape

#endif // INKSCAPE_GL_TEXTURE_H
