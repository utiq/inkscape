// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Tavmjong Bah
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream> // Logging
#include <algorithm> // Sort
#include <set> // Coarsener
#include <array>
#include <2geom/convex-hull.h>
#include <epoxy/gl.h>

#include "canvas.h"
#include "canvas-grid.h"

#include "color.h"          // Background color
#include "cms-system.h"     // Color correction
#include "desktop.h"
#include "document.h"
#include "preferences.h"

#include "display/drawing.h"
#include "display/control/canvas-item-group.h"
#include "display/control/snap-indicator.h"
#include "display/control/canvas-item-rect.h"

#include "ui/tools/tool-base.h"      // Default cursor

#include "updaters.h"         // Update strategies
#include "pixelstreamer.h"    // OpenGL
#include "framecheck.h"       // For frame profiling
#define framecheck_whole_function(D) \
    auto framecheckobj = D->prefs.debug_framecheck ? FrameCheck::Event(__func__) : FrameCheck::Event();

/*
 *   The canvas is responsible for rendering the SVG drawing with various "control"
 *   items below and on top of the drawing. Rendering is triggered by a call to one of:
 *
 *
 *   * redraw_all()     Redraws the entire canvas by calling redraw_area() with the canvas area.
 *
 *   * redraw_area()    Redraws the indicated area. Use when there is a change that doesn't affect
 *                      a CanvasItem's geometry or size.
 *
 *   * request_update() Redraws after recalculating bounds for changed CanvasItems. Use if a
 *                      CanvasItem's geometry or size has changed.
 *
 *   The first three functions add a request to the Gtk's "idle" list via
 *
 *   * add_idle()       Which causes Gtk to call when resources are available:
 *
 *   * on_idle()        Which sets up the backing stores, divides the area of the canvas that has been marked
 *                      unclean into rectangles that are small enough to render quickly, and renders them outwards
 *                      from the mouse with a call to:
 *
 *   * paint_rect_internal() Which paints the rectangle using paint_single_buffer(). It renders onto a Cairo
 *                           surface "backing_store". After a piece is rendered there is a call to:
 *
 *   * queue_draw_area() A Gtk function for marking areas of the window as needing a repaint, which when
 *                       the time is right calls:
 *
 *   * on_draw()        Which blits the Cairo surface to the screen.
 *
 *   The other responsibility of the canvas is to determine where to send GUI events. It does this
 *   by determining which CanvasItem is "picked" and then forwards the events to that item. Not all
 *   items can be picked. As a last resort, the "CatchAll" CanvasItem will be picked as it is the
 *   lowest CanvasItem in the stack (except for the "root" CanvasItem). With a small be of work, it
 *   should be possible to make the "root" CanvasItem a "CatchAll" eliminating the need for a
 *   dedicated "CatchAll" CanvasItem. There probably could be efficiency improvements as some
 *   items that are not pickable probably should be which would save having to effectively pick
 *   them "externally" (e.g. gradient CanvasItemCurves).
 */

namespace Inkscape {
namespace UI {
namespace Widget {

namespace {

/*
 * GDK event utilities
 */

// GdkEvents can only be safely copied using gdk_event_copy. Since this function allocates, we need the following smart pointer to wrap the result.
struct GdkEventFreer {void operator()(GdkEvent *ev) const {gdk_event_free(ev);}};
using GdkEventUniqPtr = std::unique_ptr<GdkEvent, GdkEventFreer>;

// Copies a GdkEvent, returning the result as a smart pointer.
auto make_unique_copy(const GdkEvent *ev) {return GdkEventUniqPtr(gdk_event_copy(ev));}

/*
 * Preferences
 */

struct Prefs
{
    // Original parameters
    Pref<int>    tile_size                = Pref<int>   ("/options/rendering/tile-size", 16, 1, 10000);
    Pref<int>    tile_multiplier          = Pref<int>   ("/options/rendering/tile-multiplier", 16, 1, 512);
    Pref<int>    x_ray_radius             = Pref<int>   ("/options/rendering/xray-radius", 100, 1, 1500);
    Pref<bool>   from_display             = Pref<bool>  ("/options/displayprofile/from_display");
    Pref<int>    grabsize                 = Pref<int>   ("/options/grabsize/value", 3, 1, 15);
    Pref<int>    outline_overlay_opacity  = Pref<int>   ("/options/rendering/outline-overlay-opacity", 50, 1, 100);

    // Things that require redraws
    Pref<void>   softproof                = Pref<void>  ("/options/softproof");
    Pref<void>   displayprofile           = Pref<void>  ("/options/displayprofile");
    Pref<bool>   imageoutlinemode         = Pref<bool>  ("/options/rendering/imageinoutlinemode");

    // New parameters
    Pref<int>    update_strategy          = Pref<int>   ("/options/rendering/update_strategy", 3, 1, 3);
    Pref<int>    render_time_limit        = Pref<int>   ("/options/rendering/render_time_limit", 1000, 100, 1000000);
    Pref<bool>   use_new_bisector         = Pref<bool>  ("/options/rendering/use_new_bisector", true);
    Pref<int>    new_bisector_size        = Pref<int>   ("/options/rendering/new_bisector_size", 500, 1, 10000);
    Pref<int>    pad                      = Pref<int>   ("/options/rendering/pad", 350, 0, 1000);
    Pref<int>    margin                   = Pref<int>   ("/options/rendering/margin", 100, 0, 1000);
    Pref<int>    preempt                  = Pref<int>   ("/options/rendering/preempt", 250, 0, 1000);
    Pref<int>    coarsener_min_size       = Pref<int>   ("/options/rendering/coarsener_min_size", 200, 0, 1000);
    Pref<int>    coarsener_glue_size      = Pref<int>   ("/options/rendering/coarsener_glue_size", 80, 0, 1000);
    Pref<double> coarsener_min_fullness   = Pref<double>("/options/rendering/coarsener_min_fullness", 0.3, 0.0, 1.0);
    Pref<bool>   request_opengl           = Pref<bool>  ("/options/rendering/request_opengl");
    Pref<int>    pixelstreamer_method     = Pref<int>   ("/options/rendering/pixelstreamer_method", 1, 1, 4);

    // Debug switches
    Pref<bool>   debug_framecheck         = Pref<bool>  ("/options/rendering/debug_framecheck");
    Pref<bool>   debug_logging            = Pref<bool>  ("/options/rendering/debug_logging");
    Pref<bool>   debug_slow_redraw        = Pref<bool>  ("/options/rendering/debug_slow_redraw");
    Pref<int>    debug_slow_redraw_time   = Pref<int>   ("/options/rendering/debug_slow_redraw_time", 50, 0, 1000000);
    Pref<bool>   debug_show_redraw        = Pref<bool>  ("/options/rendering/debug_show_redraw");
    Pref<bool>   debug_show_unclean       = Pref<bool>  ("/options/rendering/debug_show_unclean");
    Pref<bool>   debug_show_snapshot      = Pref<bool>  ("/options/rendering/debug_show_snapshot");
    Pref<bool>   debug_show_clean         = Pref<bool>  ("/options/rendering/debug_show_clean");
    Pref<bool>   debug_disable_redraw     = Pref<bool>  ("/options/rendering/debug_disable_redraw");
    Pref<bool>   debug_sticky_decoupled   = Pref<bool>  ("/options/rendering/debug_sticky_decoupled");
    Pref<bool>   debug_animate            = Pref<bool>  ("/options/rendering/debug_animate");
    Pref<bool>   debug_idle_starvation    = Pref<bool>  ("/options/rendering/debug_idle_starvation");

    // Developer mode
    Pref<bool> devmode = Pref<bool>("/options/rendering/devmode");
    void set_devmode(bool on)
    {
        tile_size.set_enabled(on);
        render_time_limit.set_enabled(on);
        use_new_bisector.set_enabled(on);
        new_bisector_size.set_enabled(on);
        pad.set_enabled(on);
        margin.set_enabled(on);
        preempt.set_enabled(on);
        coarsener_min_size.set_enabled(on);
        coarsener_glue_size.set_enabled(on);
        coarsener_min_fullness.set_enabled(on);
        pixelstreamer_method.set_enabled(on);
        debug_framecheck.set_enabled(on);
        debug_logging.set_enabled(on);
        debug_slow_redraw.set_enabled(on);
        debug_slow_redraw_time.set_enabled(on);
        debug_show_redraw.set_enabled(on);
        debug_show_unclean.set_enabled(on);
        debug_show_snapshot.set_enabled(on);
        debug_show_clean.set_enabled(on);
        debug_disable_redraw.set_enabled(on);
        debug_sticky_decoupled.set_enabled(on);
        debug_animate.set_enabled(on);
        debug_idle_starvation.set_enabled(on);
    }
};

/*
 * Conversion functions
 */

// 2Geom <-> Cairo

auto geom_to_cairo(const Geom::IntRect &rect)
{
    return Cairo::RectangleInt{rect.left(), rect.top(), rect.width(), rect.height()};
}

auto cairo_to_geom(const Cairo::RectangleInt &rect)
{
    return Geom::IntRect::from_xywh(rect.x, rect.y, rect.width, rect.height);
}

auto geom_to_cairo(const Geom::Affine &affine)
{
    return Cairo::Matrix(affine[0], affine[1], affine[2], affine[3], affine[4], affine[5]);
}

// 2Geom <-> OpenGL

void geom_to_uniform_mat(const Geom::Affine &affine, GLuint location)
{
    glUniformMatrix2fv(location, 1, GL_FALSE, std::begin({(GLfloat)affine[0], (GLfloat)affine[1], (GLfloat)affine[2], (GLfloat)affine[3]}));
}

void geom_to_uniform_trans(const Geom::Affine &affine, GLuint location)
{
    glUniform2fv(location, 1, std::begin({(GLfloat)affine[4], (GLfloat)affine[5]}));
}

void geom_to_uniform(const Geom::Affine &affine, GLuint mat_location, GLuint trans_location)
{
    geom_to_uniform_mat(affine, mat_location);
    geom_to_uniform_trans(affine, trans_location);
}

auto dimensions(const Cairo::RefPtr<Cairo::ImageSurface> &surface)
{
    return Geom::IntPoint(surface->get_width(), surface->get_height());
}

auto dimensions(const Gtk::Allocation &allocation)
{
    return Geom::IntPoint(allocation.get_width(), allocation.get_height());
}

// 2Geom additions

auto expandedBy(Geom::IntRect rect, int amount)
{
    rect.expandBy(amount);
    return rect;
}

auto distSq(const Geom::IntPoint pt, const Geom::IntRect &rect)
{
    auto v = rect.clamp(pt) - pt;
    return v.x() * v.x() + v.y() * v.y();
}

auto operator*(const Geom::IntPoint &a, int b)
{
    return Geom::IntPoint(a.x() * b, a.y() * b);
}

auto operator*(const Geom::Point &a, const Geom::IntPoint &b)
{
    return Geom::Point(a.x() * b.x(), a.y() * b.y());
}

auto operator*(const Geom::IntPoint &a, const Geom::IntPoint &b)
{
    return Geom::IntPoint(a.x() * b.x(), a.y() * b.y());
}

auto operator/(const Geom::Point &a, const Geom::IntPoint &b)
{
    return Geom::Point(a.x() / b.x(), a.y() / b.y());
}

auto operator/(double a, const Geom::Point &b)
{
    return Geom::Point(a / b.x(), a / b.y());
}

auto operator/(const Geom::IntPoint &a, const Geom::IntPoint &b)
{
    return Geom::IntPoint(a.x() / b.x(), a.y() / b.y());
}

auto absolute(const Geom::Point &a)
{
    return Geom::Point(std::abs(a.x()), std::abs(a.y()));
}

auto min(const Geom::Point &a)
{
    return std::min(a.x(), a.y());
}

// Compute the minimum-area bounding box of a collection of points.
// Returns the result in fragment format, i.e. as a rotation that should be applied to the points, followed by an axis-aligned rectangle.
auto min_bounding_box(const std::vector<Geom::Point> &pts)
{
    // Compute the convex hull.
    const auto hull = Geom::ConvexHull(pts);

    // Move the point i along until it maximises distance in the direction n.
    auto advance = [&] (int &i, const Geom::Point &n) {
        auto ih = Geom::dot(hull[i], n);
        while (true) {
            int j = (i + 1) % hull.size();
            auto jh = Geom::dot(hull[j], n);
            if (ih >= jh) break;
            i = j;
            ih = jh;
        }
    };

    double maxa = 0.0;
    std::pair<Geom::Affine, Geom::Rect> result;

    // Run rotating callipers.
    int j, k, l;
    for (int i = 0; i < hull.size(); i++) {
        // Get the current segment.
        auto &p1 = hull[i];
        auto &p2 = hull[(i + 1) % hull.size()];
        auto v = (p2 - p1).normalized();
        auto n = Geom::Point(-v.y(), v.x());

        if (i == 0) {
            // Initialise the points.
            j = 0; advance(j,  v);
            k = j; advance(k,  n);
            l = k; advance(l, -v);
        } else {
            // Advance the points.
            advance(j,  v);
            advance(k,  n);
            advance(l, -v);
        }

        // Compute the dimensions of the unconstrained rectangle.
        auto w = Geom::dot(hull[j] - hull[l], v);
        auto h = Geom::dot(hull[k] - hull[i], n);
        auto a = w * h;

        // Track the maxmimum.
        if (a > maxa) {
            maxa = a;
            result = std::make_pair(Geom::Affine(v.x(), -v.y(), v.y(), v.x(), 0.0, 0.0),
                                    Geom::Rect::from_xywh(Geom::dot(hull[l], v), Geom::dot(hull[i], n), w, h));
        }
    }

    return result;
}

// Determine whether an affine transformation is approximately a dihedral transformation of the unit square.
bool approx_dihedral(const Geom::Affine &affine_in, double eps = 0.0001)
{
    // Map the unit square to the origin-centered unit square.
    auto affine = Geom::Translate(0.5, 0.5) * affine_in * Geom::Translate(-0.5, -0.5);

    // Ensure translational part is zero.
    if (std::abs(affine[4]) > eps || std::abs(affine[5]) > eps) return false;

    // Ensure linear part has integer components.
    std::array<int, 4> arr;
    for (int i = 0; i < 4; i++) {
        arr[i] = std::round(affine[i]);
        if (std::abs(affine[i] - arr[i]) > eps) return false;
        arr[i] = std::abs(arr[i]);
    }

    // Ensure rounded linear part is correct.
    return arr == std::array{1, 0, 0, 1} || arr == std::array{0, 1, 1, 0};
}

// Regularisation operator for Geom::OptIntRect. Turns zero-area rectangles into empty optionals.
auto regularised(const Geom::OptIntRect &r)
{
    return r && !r->hasZeroArea() ? r : Geom::OptIntRect();
}

// STL additions

// Just like std::clamp, except it doesn't deliberately crash if lo > hi due to rounding errors, so is safe to use with floating-point types.
template <typename T>
auto safeclamp(T val, T lo, T hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// Cairo additions

void region_to_path(const Cairo::RefPtr<Cairo::Context> &cr, const Cairo::RefPtr<Cairo::Region> &reg)
{
    for (int i = 0; i < reg->get_num_rectangles(); i++) {
        auto rect = reg->get_rectangle(i);
        cr->rectangle(rect.x, rect.y, rect.width, rect.height);
    }
}

// Shrink a region by d/2 in all directions, while also translating it by (d/2 + t, d/2 + t).
auto shrink_region(const Cairo::RefPtr<Cairo::Region> &reg, int d, int t = 0)
{
    // Find the bounding rect, expanded by 1 in all directions.
    auto rect = geom_to_cairo(expandedBy(cairo_to_geom(reg->get_extents()), 1));

    // Take the complement of the region within the rect.
    auto reg2 = Cairo::Region::create(rect);
    reg2->subtract(reg);

    // Increase the width and height of every rectangle by d.
    auto reg3 = Cairo::Region::create();
    for (int i = 0; i < reg2->get_num_rectangles(); i++) {
        auto rect = reg2->get_rectangle(i);
        rect.x += t;
        rect.y += t;
        rect.width += d;
        rect.height += d;
        reg3->do_union(rect);
    }

    // Take the complement of the region within the rect.
    reg2 = Cairo::Region::create(rect);
    reg2->subtract(reg3);

    return reg2;
}

// Apply an affine transformation to a region, then return a strictly smaller region approximating it, made from chunks of size roughly d. To reduce computation, only the intersection of the result with bounds will be valid.
auto region_affine_approxinwards(const Cairo::RefPtr<Cairo::Region> &reg, const Geom::Affine &affine, const Geom::IntRect &bounds, int d = 200)
{
    // Trivial empty case.
    if (reg->empty()) return Cairo::Region::create();

    // Trivial identity case.
    if (affine.isIdentity(0.001)) return reg->copy();

    // Fast-path for rectilinear transformations.
    if (affine.withoutTranslation().isScale(0.001)) {
        auto regdst = Cairo::Region::create();

        auto transform = [&] (const Geom::IntPoint &p) {
            return (Geom::Point(p) * affine).round();
        };

        for (int i = 0; i < reg->get_num_rectangles(); i++)
        {
            auto rect = cairo_to_geom(reg->get_rectangle(i));
            regdst->do_union(geom_to_cairo(Geom::IntRect(transform(rect.min()), transform(rect.max()))));
        }

        return regdst;
    }

    // General case.
    auto ext = cairo_to_geom(reg->get_extents());
    auto rectdst = regularised((Geom::Parallelogram(ext) * affine).bounds().roundOutwards() & bounds);
    if (!rectdst) return Cairo::Region::create();
    auto rectsrc = (Geom::Parallelogram(*rectdst) * affine.inverse()).bounds().roundOutwards();

    auto regdst = Cairo::Region::create(geom_to_cairo(*rectdst));
    auto regsrc = Cairo::Region::create(geom_to_cairo(rectsrc));
    regsrc->subtract(reg);

    double fx = min(absolute(Geom::Point(1.0, 0.0) * affine.withoutTranslation()));
    double fy = min(absolute(Geom::Point(0.0, 1.0) * affine.withoutTranslation()));

    for (int i = 0; i < regsrc->get_num_rectangles(); i++)
    {
        auto rect = cairo_to_geom(regsrc->get_rectangle(i));
        int nx = std::ceil(rect.width()  * fx / d);
        int ny = std::ceil(rect.height() * fy / d);
        auto pt = [&] (int x, int y) {
            return rect.min() + (rect.dimensions() * Geom::IntPoint(x, y)) / Geom::IntPoint(nx, ny);
        };
        for (int x = 0; x < nx; x++) {
            for (int y = 0; y < ny; y++) {
                auto r = Geom::IntRect(pt(x, y), pt(x + 1, y + 1));
                auto r2 = (Geom::Parallelogram(r) * affine).bounds().roundOutwards();
                regdst->subtract(geom_to_cairo(r2));
            }
        }
    }

    return regdst;
}

auto unioned(Cairo::RefPtr<Cairo::Region> a, const Cairo::RefPtr<Cairo::Region> &b)
{
    a->do_union(b);
    return std::move(a); // Just to be safe; not sure if NVRO will apply.
}

// Colour operations

auto rgb_to_array(uint32_t rgb)
{
    return std::array{SP_RGBA32_R_U(rgb) / 255.0f, SP_RGBA32_G_U(rgb) / 255.0f, SP_RGBA32_B_U(rgb) / 255.0f};
}

auto rgba_to_array(uint32_t rgba)
{
    return std::array{SP_RGBA32_R_U(rgba) / 255.0f, SP_RGBA32_G_U(rgba) / 255.0f, SP_RGBA32_B_U(rgba) / 255.0f, SP_RGBA32_A_U(rgba) / 255.0f};
}

auto premultiplied(std::array<GLfloat, 4> arr)
{
    arr[0] *= arr[3];
    arr[1] *= arr[3];
    arr[2] *= arr[3];
    return arr;
}

auto checkerboard_darken(const std::array<float, 3> &rgb, float amount = 1.0f)
{
    std::array<float, 3> hsl;
    SPColor::rgb_to_hsl_floatv(&hsl[0], rgb[0], rgb[1], rgb[2]);
    hsl[2] += (hsl[2] < 0.08 ? 0.08 : -0.08) * amount;

    std::array<float, 3> rgb2;
    SPColor::hsl_to_rgb_floatv(&rgb2[0], hsl[0], hsl[1], hsl[2]);

    return rgb2;
}

auto checkerboard_darken(uint32_t rgba)
{
    return checkerboard_darken(rgb_to_array(rgba), 1.0f - SP_RGBA32_A_U(rgba) / 255.0f);
}

// Preference integers <-> enums

auto pref_to_updater(int index)
{
    constexpr auto arr = std::array{Updater::Strategy::Responsive,
                                    Updater::Strategy::FullRedraw,
                                    Updater::Strategy::Multiscale};
    assert(1 <= index && index <= arr.size());
    return arr[index - 1];
}

auto pref_to_pixelstreamer(int index)
{
    constexpr auto arr = std::array{PixelStreamer::Method::Auto,
                                    PixelStreamer::Method::Persistent,
                                    PixelStreamer::Method::Asynchronous,
                                    PixelStreamer::Method::Synchronous};
    assert(1 <= index && index <= arr.size());
    return arr[index - 1];
}

/*
 * OpenGL utilities
 */

template <GLuint type>
struct Shader : boost::noncopyable
{
    GLuint id;
    Shader(const char *src) {id = glCreateShader(type); glShaderSource(id, 1, &src, nullptr); glCompileShader(id);}
    ~Shader() { glDeleteShader(id); }
};
using GShader = Shader<GL_GEOMETRY_SHADER>;
using VShader = Shader<GL_VERTEX_SHADER>;
using FShader = Shader<GL_FRAGMENT_SHADER>;

struct Program : boost::noncopyable
{
    GLuint id = 0;
    void create(const VShader &v,                   const FShader &f) {id = glCreateProgram(); glAttachShader(id, v.id);                           glAttachShader(id, f.id); glLinkProgram(id);}
    void create(const VShader &v, const GShader &g, const FShader &f) {id = glCreateProgram(); glAttachShader(id, v.id); glAttachShader(id, g.id); glAttachShader(id, f.id); glLinkProgram(id);}
    auto loc(const char *str) const {return glGetUniformLocation(id, str);}
    ~Program() {glDeleteProgram(id);}
};

struct VAO : boost::noncopyable
{
    GLuint vao, vbuf;
    VAO() : vao(0) {}
    VAO(GLuint vao, GLuint vbuf) : vao(vao), vbuf(vbuf) {}
    VAO(VAO &&other) noexcept : vao(0) {*this = std::move(other);}
    VAO &operator=(VAO &&other) noexcept {this->~VAO(); new (this) VAO(other.vao, other.vbuf); other.vao = 0; return *this;}
    ~VAO() {if (vao) {glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbuf);}}
};

/*
 * Fragments
 */

// A "fragment" is a rectangle of drawn content at a specfic place. This is a lightweight POD class that stores only the geometry...
struct Fragment
{
    // The affine the geometry was imbued with when the content was drawn.
    Geom::Affine affine;

    // The rectangle of world space where the fragment was drawn.
    Geom::IntRect rect;
};

// ...while this is an abstract class for fragments backed by real content. It has subclasses for each graphics backend.
struct FragmentBase : Fragment
{
    virtual ~FragmentBase() {}

    virtual bool has_content()         const = 0;
    virtual bool has_outline_content() const = 0;

    virtual void clear_content()         = 0;
    virtual void clear_outline_content() = 0;
};

/*
 * Graphics state
 */

struct GraphicsState
{
    virtual ~GraphicsState() {}

    virtual FragmentBase *get_store()    = 0;
    virtual FragmentBase *get_snapshot() = 0;

    virtual void swap_stores() = 0;
};

// OpenGL graphics state

struct GLFragment : FragmentBase
{
    Texture texture;
    Texture outline_texture;

    bool has_content()         const override {return (bool)texture;}
    bool has_outline_content() const override {return (bool)outline_texture;}

    void clear_content()         override {texture.clear();}
    void clear_outline_content() override {outline_texture.clear();}
};

struct GLState : GraphicsState
{
    // Drawn content.
    GLFragment store, snapshot;

    // OpenGL objects.
    VAO rect; // Rectangle vertex data.
    Program checker, shadow, texcopy, texcopydouble, outlineoverlay, xray, outlineoverlayxray; // Shaders
    GLuint fbo; // Framebuffer object for rendering to the main fragment.

    // Pixel streamer for uploading pixel data to GPU.
    std::unique_ptr<PixelStreamer> pixelstreamer;

    // For preventing unnecessary pipeline recreation.
    enum class State {None, PaintWidget, OnIdle, PaintRect};
    State state;

    // For caching frequently-used uniforms.
    GLuint mat_loc, trans_loc, tex_loc, texoutline_loc;

    GLState(PixelStreamer::Method method)
    {
        // Create rectangle geometry.
        constexpr GLfloat verts[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        glGenBuffers(1, &rect.vbuf);
        glBindBuffer(GL_ARRAY_BUFFER, rect.vbuf);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glGenVertexArrays(1, &rect.vao);
        glBindVertexArray(rect.vao);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, 0);

        // Create shader programs.
        auto vs = VShader(R"(
            #version 330 core

            uniform mat2 mat;
            uniform vec2 trans;
            layout(location = 0) in vec2 pos;
            smooth out vec2 uv;

            void main()
            {
                uv = pos;
                vec2 pos2 = mat * pos + trans;
                gl_Position = vec4(pos2.x, pos2.y, 0.0, 1.0);
            }
        )");

        auto texcopy_fs = FShader(R"(
            #version 330 core

            uniform sampler2D tex;
            smooth in vec2 uv;
            out vec4 outColour;

            void main()
            {
                outColour = texture(tex, uv);
            }
        )");

        auto texcopydouble_fs = FShader(R"(
            #version 330 core

            uniform sampler2D tex;
            uniform sampler2D tex_outline;
            smooth in vec2 uv;
            layout(location = 0) out vec4 outColour;
            layout(location = 1) out vec4 outColour_outline;

            void main()
            {
                outColour = texture(tex, uv);
                outColour_outline = texture(tex_outline, uv);
            }
        )");

        auto outlineoverlay_fs = FShader(R"(
            #version 330 core

            uniform sampler2D tex;
            uniform sampler2D tex_outline;
            uniform float opacity;
            smooth in vec2 uv;
            out vec4 outColour;

            void main()
            {
                vec4 c1 = texture(tex, uv);
                vec4 c2 = texture(tex_outline, uv);
                vec4 c1w = vec4(mix(c1.rgb, vec3(1.0, 1.0, 1.0) * c1.a, opacity), c1.a);
                outColour = c1w * (1.0 - c2.a) + c2;
            }
        )");

        auto xray_fs = FShader(R"(
            #version 330 core

            uniform sampler2D tex;
            uniform sampler2D tex_outline;
            uniform vec2 pos;
            uniform float radius;
            smooth in vec2 uv;
            out vec4 outColour;

            void main()
            {
                vec4 c1 = texture(tex, uv);
                vec4 c2 = texture(tex_outline, uv);

                float r = length(gl_FragCoord.xy - pos);
                r = clamp((radius - r) / 2.0, 0.0, 1.0);

                outColour = mix(c1, c2, r);
            }
        )");

        auto outlineoverlayxray_fs = FShader(R"(
            #version 330 core

            uniform sampler2D tex;
            uniform sampler2D tex_outline;
            uniform float opacity;
            uniform vec2 pos;
            uniform float radius;
            smooth in vec2 uv;
            out vec4 outColour;

            void main()
            {
                vec4 c1 = texture(tex, uv);
                vec4 c2 = texture(tex_outline, uv);
                vec4 c1w = vec4(mix(c1.rgb, vec3(1.0, 1.0, 1.0) * c1.a, opacity), c1.a);
                outColour = c1w * (1.0 - c2.a) + c2;

                float r = length(gl_FragCoord.xy - pos);
                r = clamp((radius - r) / 2.0, 0.0, 1.0);

                outColour = mix(outColour, c2, r);
            }
        )");

        auto checker_fs = FShader(R"(
            #version 330 core

            uniform float size;
            uniform vec3 col1, col2;
            out vec4 outColour;

            void main()
            {
                vec2 a = floor(fract(gl_FragCoord.xy / size) * 2.0);
                float b = abs(a.x - a.y);
                outColour = vec4((1.0 - b) * col1 + b * col2, 1.0);
            }
        )");

        auto shadow_gs = GShader(R"(
            #version 330 core

            layout(triangles) in;
            layout(triangle_strip, max_vertices = 10) out;

            uniform vec2 wh;
            uniform float size;
            uniform vec2 dir;

            smooth out vec2 uv;
            flat out vec2 maxuv;

            void f(vec4 p, vec4 v0, mat2 m)
            {
                gl_Position = p;
                uv = m * (p.xy - v0.xy);
                EmitVertex();
            }

            float push(float x)
            {
                return 0.15 * (1.0 + clamp(x / 0.707, -1.0, 1.0));
            }

            void main()
            {
                vec4 v0 = gl_in[0].gl_Position;
                vec4 v1 = gl_in[1].gl_Position;
                vec4 v2 = gl_in[2].gl_Position;
                vec4 v3 = gl_in[2].gl_Position - gl_in[1].gl_Position + gl_in[0].gl_Position;

                vec2 a = normalize((v1 - v0).xy * wh);
                vec2 b = normalize((v3 - v0).xy * wh);
                vec2 c = size / abs(a.x * b.y - a.y * b.x) / wh;
                vec4 d = vec4(a * c, 0.0, 0.0);
                vec4 e = vec4(b * c, 0.0, 0.0);
                mat2 m = mat2(a.y, -b.y, -a.x, b.x) * mat2(wh.x, 0.0, 0.0, wh.y) / size;

                float ap = dot(vec2(a.y, -a.x), dir);
                float bp = dot(vec2(-b.y, b.x), dir);
                v0.xy += (b *  push( ap) + a *  push( bp)) * size / wh;
                v1.xy += (b *  push( ap) + a * -push(-bp)) * size / wh;
                v2.xy += (b * -push(-ap) + a * -push(-bp)) * size / wh;
                v3.xy += (b * -push(-ap) + a *  push( bp)) * size / wh;

                maxuv = m * (v2.xy - v0.xy);
                f(v0, v0, m);
                f(v0 - d - e, v0, m);
                f(v1, v0, m);
                f(v1 + d - e, v0, m);
                f(v2, v0, m);
                f(v2 + d + e, v0, m);
                f(v3, v0, m);
                f(v3 - d + e, v0, m);
                f(v0, v0, m);
                f(v0 - d - e, v0, m);
                EndPrimitive();
            }
        )");

        auto shadow_fs = FShader(R"(
            #version 330 core

            uniform vec4 shadow_col;

            smooth in vec2 uv;
            flat in vec2 maxuv;

            out vec4 outColour;

            void main()
            {
                float x = max(uv.x - maxuv.x, 0.0) - max(-uv.x, 0.0);
                float y = max(uv.y - maxuv.y, 0.0) - max(-uv.y, 0.0);
                float s = min(length(vec2(x, y)), 1.0);

                float A = 4.0; // This coefficient changes how steep the curve is and controls shadow drop-off.
                s = (exp(A * (1.0 - s)) - 1.0) / (exp(A) - 1.0); // Exponential decay for drop shadow - long tail.

                outColour = shadow_col * s;
            }
        )");

        texcopy.create(vs, texcopy_fs);
        texcopydouble.create(vs, texcopydouble_fs);
        outlineoverlay.create(vs, outlineoverlay_fs);
        xray.create(vs, xray_fs);
        outlineoverlayxray.create(vs, outlineoverlayxray_fs);
        checker.create(vs, checker_fs);
        shadow.create(vs, shadow_gs, shadow_fs);

        // Create the framebuffer object for rendering to off-screen fragments.
        glGenFramebuffers(1, &fbo);

        // Create the PixelStreamer.
        pixelstreamer = PixelStreamer::create(method);

        // Set the last known state as unspecified, forcing a pipeline recreation whatever the next operation is.
        state = State::None;
    }

    ~GLState() override { glDeleteFramebuffers(1, &fbo); }

    FragmentBase *get_store()    override { return &store; }
    FragmentBase *get_snapshot() override { return &snapshot; }
    void swap_stores() override { std::swap(store, snapshot); }

    // Get the affine transformation required to paste fragment A onto fragment B, assuming
    // coordinates such that A is a texture (0 to 1) and B is a framebuffer (-1 to 1).
    static auto calc_paste_transform(const Fragment &a, const Fragment &b)
    {
        Geom::Affine result = Geom::Scale(a.rect.dimensions());

        if (a.affine == b.affine) {
            result *= Geom::Translate(a.rect.min() - b.rect.min());
        } else {
            result *= Geom::Translate(a.rect.min()) * a.affine.inverse() * b.affine * Geom::Translate(-b.rect.min());
        }

        return result * Geom::Scale(2.0 / b.rect.dimensions()) * Geom::Translate(-1.0, -1.0);
    }

    // Given a region, shrink it by 0.5px, and convert the result to a VAO of triangles.
    static auto clean_region_shrink_vao(const Cairo::RefPtr<Cairo::Region> &reg, const Geom::IntRect &rel)
    {
        // Shrink the region by 0.5 (translating it by (0.5, 0.5) in the process).
        auto reg2 = shrink_region(reg, 1);

        // Preallocate the vertex buffer.
        int nrects = reg2->get_num_rectangles();
        std::vector<GLfloat> verts;
        verts.reserve(nrects * 12);

        // Add a vertex to the buffer, transformed to a coordinate system in which the enclosing rectangle 'rel' goes from 0 to 1.
        // Also shift them up/left by 0.5px; combined with the width/height increase from earlier, this shrinks the region by 0.5px.
        auto emit_vertex = [&] (const Geom::IntPoint &pt) {
            verts.emplace_back((pt.x() - 0.5f - rel.left()) / rel.width());
            verts.emplace_back((pt.y() - 0.5f - rel.top() ) / rel.height());
        };

        // Todo: Use a better triangulation algorithm here that results in 1) less triangles, and 2) no seaming.
        for (int i = 0; i < nrects; i++) {
            auto rect = cairo_to_geom(reg2->get_rectangle(i));
            for (int j = 0; j < 6; j++) {
                constexpr int indices[] = {0, 1, 2, 0, 2, 3};
                emit_vertex(rect.corner(indices[j]));
            }
        }

        // Package the data in a VAO.
        VAO result;
        glGenBuffers(1, &result.vbuf);
        glBindBuffer(GL_ARRAY_BUFFER, result.vbuf);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GLfloat), verts.data(), GL_STREAM_DRAW);
        glGenVertexArrays(1, &result.vao);
        glBindVertexArray(result.vao);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, 0);

        // Return the VAO and the number of rectangles.
        return std::make_pair(std::move(result), nrects);
    }
};

// Cairo graphics state

struct CairoFragment : FragmentBase
{
    Cairo::RefPtr<Cairo::ImageSurface> surface;
    Cairo::RefPtr<Cairo::ImageSurface> outline_surface;

    bool has_content()         const override { return (bool)surface; }
    bool has_outline_content() const override { return (bool)outline_surface; }

    void clear_content()         override { surface.clear(); }
    void clear_outline_content() override { outline_surface.clear(); }
};

struct CairoState : GraphicsState
{
    // Drawn content.
    CairoFragment store, snapshot;

    // Whether the solid-colour optimisation is in use.
    bool solid_colour;

    FragmentBase *get_store()    override { return &store; }
    FragmentBase *get_snapshot() override { return &snapshot; };
    void swap_stores() override { std::swap(store, snapshot); }

    void set_colours(uint32_t page, uint32_t background)
    {
        // Enable solid colour optimisation if both page and desk are solid (as opposed to checkerboard).
        solid_colour = SP_RGBA32_A_U(page) == 255 && SP_RGBA32_A_U(background) == 255;
    }

    // Same as the above, but additionally returns whether the content must be redrawn.
    bool update_colours(uint32_t page, uint32_t background)
    {
        bool prev_solid = solid_colour;
        set_colours(page, background);
        return prev_solid || solid_colour;
    }

    // Convert an rgba into a pattern, turning transparency into checkerboard-ness.
    static Cairo::RefPtr<Cairo::Pattern> rgba_to_pattern(uint32_t rgba)
    {
        if (SP_RGBA32_A_U(rgba) == 255) {
            return Cairo::SolidPattern::create_rgb(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba));
        } else {
            constexpr int w = 6;
            constexpr int h = 6;

            auto dark = checkerboard_darken(rgba);

            auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 2 * w, 2 * h);

            auto cr = Cairo::Context::create(surface);
            cr->set_operator(Cairo::OPERATOR_SOURCE);
            cr->set_source_rgb(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba));
            cr->paint();
            cr->set_source_rgb(dark[0], dark[1], dark[2]);
            cr->rectangle(0, 0, w, h);
            cr->rectangle(w, h, w, h);
            cr->fill();

            auto pattern = Cairo::SurfacePattern::create(surface);
            pattern->set_extend(Cairo::EXTEND_REPEAT);
            pattern->set_filter(Cairo::FILTER_NEAREST);

            return pattern;
        }
    }
};

} // anonymous namespace

/*
 * Implementation class
 */

class CanvasPrivate
{
public:
    friend class Canvas;
    Canvas *q;
    CanvasPrivate(Canvas *q) : q(q) {}

    // Lifecycle
    bool active = false;
    void activate();
    void deactivate();

    // Preferences
    Prefs prefs;

    // Update strategy; tracks the unclean region and decides how to redraw it.
    std::unique_ptr<Updater> updater;

    // Graphics state; holds all the graphics resources, including the drawn content.
    std::unique_ptr<GraphicsState> graphics;
    auto glstate() const { return static_cast<GLState*>   (graphics.get()); }
    auto crstate() const { return static_cast<CairoState*>(graphics.get()); }
    void activate_graphics();
    void deactivate_graphics();

    // Event processor. Events that interact with the Canvas are buffered here until the start of the next frame. They are processed by a separate object so that deleting the Canvas mid-event can be done safely.
    struct EventProcessor
    {
        std::vector<GdkEventUniqPtr> events;
        int pos;
        GdkEvent *ignore = nullptr;
        CanvasPrivate *canvasprivate; // Nulled on destruction.
        bool in_processing = false; // For handling recursion due to nested GTK main loops.
        bool compression = true; // Whether event compression is enabled.
        void process();
        void compress();
        int gobble_key_events(guint keyval, guint mask);
        void gobble_motion_events(guint mask);
    };
    std::shared_ptr<EventProcessor> eventprocessor; // Usually held by CanvasPrivate, but temporarily also held by itself while processing so that it is not deleted mid-event.
    bool add_to_bucket(const GdkEvent*);
    bool process_bucketed_event(const GdkEvent*);
    bool pick_current_item(const GdkEvent*);
    bool emit_event(const GdkEvent*);
    Inkscape::CanvasItem *pre_scroll_grabbed_item;

    // State for determining when to run event processor.
    bool pending_draw = false;
    sigc::connection bucket_emptier;
    std::optional<guint> bucket_emptier_tick_callback;
    void schedule_bucket_emptier();
    void disconnect_bucket_emptier_tick_callback();

    // Idle system. The high priority idle ensures at least one idle cycle between add_idle and on_draw.
    void add_idle();
    sigc::connection hipri_idle;
    sigc::connection lopri_idle;
    bool on_hipri_idle();
    bool on_lopri_idle();
    bool idle_running = false;

    // Widget drawing
    std::pair<Geom::IntRect, Geom::IntRect> calc_splitview_cliprects() const;
    void draw_splitview_controller(const Cairo::RefPtr<Cairo::Context> &cr) const;
    void paint_background(const Fragment &fragment, const Cairo::RefPtr<Cairo::Context> &cr) const;

    // Content drawing
    bool on_idle();
    void paint_rect(Geom::IntRect const &rect);
    void paint_single_buffer(const Cairo::RefPtr<Cairo::ImageSurface> &surface, const Geom::IntRect &rect, bool need_background);
    std::optional<Geom::Dim2> old_bisector(const Geom::IntRect &rect);
    std::optional<Geom::Dim2> new_bisector(const Geom::IntRect &rect);
    bool need_outline_store() const {return q->_split_mode != Inkscape::SplitMode::NORMAL || q->_render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY;}

    uint32_t desk   = 0xffffffff; // The background colour, with the alpha channel used to control checkerboard.
    uint32_t border = 0x000000ff; // The border colour, used only to control shadow colour.
    uint32_t page   = 0xffffffff; // The page colour, also with alpha channel used to control checkerboard.

    int device_scale; // The device scale the stores are drawn at.
    Geom::Affine geom_affine; // The affine the geometry was last imbued with.
    bool decoupled_mode = false;

    // Edge flicker prevention.
    Cairo::RefPtr<Cairo::Region> snapshot_cleanregion; // Approximate clean region of the snapshot store in store space, valid only in decoupled mode.
    bool processed_edge;

    // Trivial overload of GtkWidget function.
    void queue_draw_area(const Geom::IntRect &rect);

    // For tracking the last known mouse position. (The function Gdk::Window::get_device_position cannot be used because of slow X11 round-trips. Remove this workaround when X11 dies.)
    std::optional<Geom::Point> last_mouse;

    // Idle time starvation counter.
    gint64 sample_begin = 0;
    gint64 wait_begin = 0;
    gint64 wait_accumulated = 0;
};

/*
 * Lifecycle
 */

Canvas::Canvas()
    : d(std::make_unique<CanvasPrivate>(this))
{
    set_name("InkscapeCanvas");

    // Events
    add_events(Gdk::BUTTON_PRESS_MASK   |
               Gdk::BUTTON_RELEASE_MASK |
               Gdk::ENTER_NOTIFY_MASK   |
               Gdk::LEAVE_NOTIFY_MASK   |
               Gdk::FOCUS_CHANGE_MASK   |
               Gdk::KEY_PRESS_MASK      |
               Gdk::KEY_RELEASE_MASK    |
               Gdk::POINTER_MOTION_MASK |
               Gdk::SCROLL_MASK         |
               Gdk::SMOOTH_SCROLL_MASK  );

    // Set up EventProcessor
    d->eventprocessor = std::make_shared<CanvasPrivate::EventProcessor>();
    d->eventprocessor->canvasprivate = d.get();

    // Developer mode master switch
    d->prefs.devmode.action = [=] { d->prefs.set_devmode(d->prefs.devmode); };
    d->prefs.devmode.action();

    // Updater
    d->updater = Updater::create(pref_to_updater(d->prefs.update_strategy));
    d->updater->reset();

    // Preferences
    d->prefs.grabsize.action = [=] { _canvas_item_root->update_canvas_item_ctrl_sizes(d->prefs.grabsize); };
    d->prefs.debug_show_unclean.action = [=] { queue_draw(); };
    d->prefs.debug_show_clean.action = [=] { queue_draw(); };
    d->prefs.debug_disable_redraw.action = [=] { d->add_idle(); };
    d->prefs.debug_sticky_decoupled.action = [=] { d->add_idle(); };
    d->prefs.debug_animate.action = [=] { queue_draw(); };
    d->prefs.update_strategy.action = [=] {
        auto new_updater = Updater::create(pref_to_updater(d->prefs.update_strategy));
        new_updater->clean_region = std::move(d->updater->clean_region);
        d->updater = std::move(new_updater);
    };
    d->prefs.outline_overlay_opacity.action = [=] { queue_draw(); };
    d->prefs.softproof.action = [=] { redraw_all(); };
    d->prefs.displayprofile.action = [=] { redraw_all(); };
    d->prefs.imageoutlinemode.action = [=] { redraw_all(); };
    d->prefs.request_opengl.action = [=] {
        if (get_realized()) {
            d->deactivate_graphics();
            set_opengl_enabled(d->prefs.request_opengl);
            d->activate_graphics();
            d->updater->reset();
            d->add_idle();
        }
    };
    d->prefs.pixelstreamer_method.action = [=] {
        if (get_realized() && get_opengl_enabled()) {
            d->deactivate_graphics();
            d->activate_graphics();
        }
    };
    d->prefs.debug_idle_starvation.action = [=] { d->sample_begin = d->wait_begin = d->wait_accumulated = 0; };

    // Cavas item root
    _canvas_item_root = new Inkscape::CanvasItemGroup(nullptr);
    _canvas_item_root->set_name("CanvasItemGroup:Root");
    _canvas_item_root->set_canvas(this);

    // Split view.
    _split_direction = Inkscape::SplitDirection::EAST;
    _split_frac = {0.5, 0.5};

    // Recreate stores on HiDPI change.
    property_scale_factor().signal_changed().connect([this] { d->add_idle(); });

    // OpenGL switch.
    set_opengl_enabled(d->prefs.request_opengl);
}

// Graphics becomes active when the widget is realized.
void CanvasPrivate::activate_graphics()
{
    if (q->get_opengl_enabled()) {
        q->make_current();
        graphics = std::make_unique<GLState>(pref_to_pixelstreamer(prefs.pixelstreamer_method));
    } else {
        graphics = std::make_unique<CairoState>();
        crstate()->set_colours(page, desk);
    }
}

// After graphics becomes active, the canvas becomes active when additionally a drawing is set.
void CanvasPrivate::activate()
{
    // Event handling/item picking
    q->_pick_event.type = GDK_LEAVE_NOTIFY;
    q->_pick_event.crossing.x = 0;
    q->_pick_event.crossing.y = 0;

    q->_in_repick         = false;
    q->_left_grabbed_item = false;
    q->_all_enter_events  = false;
    q->_is_dragging       = false;
    q->_state             = 0;

    q->_current_canvas_item     = nullptr;
    q->_current_canvas_item_new = nullptr;
    q->_grabbed_canvas_item     = nullptr;
    q->_grabbed_event_mask = (Gdk::EventMask)0;
    pre_scroll_grabbed_item = nullptr;

    // Drawing
    q->_drawing_disabled = false;
    q->_need_update = true;

    // Split view
    q->_split_dragging = false;

    // Ensure GTK event compression is disabled.
    q->get_window()->set_event_compression(false);

    active = true;

    add_idle();
}

void CanvasPrivate::deactivate()
{
    active = false;

    // Disconnect signals and timeouts. (Note: They will never be rescheduled while inactive.)
    hipri_idle.disconnect();
    lopri_idle.disconnect();
    bucket_emptier.disconnect();
    disconnect_bucket_emptier_tick_callback();
}

void CanvasPrivate::deactivate_graphics()
{
    if (q->get_opengl_enabled()) q->make_current();
    graphics.reset();
}

Canvas::~Canvas()
{
    // Disconnect from EventProcessor.
    d->eventprocessor->canvasprivate = nullptr;

    // Remove entire CanvasItem tree.
    delete _canvas_item_root;
}

void Canvas::set_drawing(Drawing *drawing)
{
    if (d->active && !drawing) d->deactivate();
    _drawing = drawing;
    if (!d->active && get_realized() && drawing) d->activate();
}

void Canvas::on_realize()
{
    parent_type::on_realize();
    d->activate_graphics();
    if (_drawing) d->activate();
}

void Canvas::on_unrealize()
{
    if (_drawing) d->deactivate();
    d->deactivate_graphics();
    parent_type::on_unrealize();
}

/*
 * Events system
 */

// The following protected functions of Canvas are where all incoming events initially arrive.
// Those that do not interact with the Canvas are processed instantaneously, while the rest are
// delayed by placing them into the bucket.

bool Canvas::on_scroll_event(GdkEventScroll *scroll_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(scroll_event));
}

bool Canvas::on_button_press_event(GdkEventButton *button_event)
{
    return on_button_event(button_event);
}

bool Canvas::on_button_release_event(GdkEventButton *button_event)
{
    return on_button_event(button_event);
}

// Unified handler for press and release events.
bool Canvas::on_button_event(GdkEventButton *button_event)
{
    // Sanity-check event type.
    switch (button_event->type) {
        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            break; // Good
        default:
            std::cerr << "Canvas::on_button_event: illegal event type!" << std::endl;
            return false;
    }

    // Drag the split view controller.
    if (_split_mode == Inkscape::SplitMode::SPLIT) {
        auto cursor_position = Geom::IntPoint(button_event->x, button_event->y);
        switch (button_event->type) {
            case GDK_BUTTON_PRESS:
                if (_hover_direction != Inkscape::SplitDirection::NONE) {
                    _split_dragging = true;
                    _split_drag_start = cursor_position;
                    return true;
                }
                break;
            case GDK_2BUTTON_PRESS:
                if (_hover_direction != Inkscape::SplitDirection::NONE) {
                    _split_direction = _hover_direction;
                    _split_dragging = false;
                    queue_draw();
                    return true;
                }
                break;
            case GDK_BUTTON_RELEASE:
                _split_dragging = false;

                // Check if we are near the edge. If so, revert to normal mode.
                if (cursor_position.x() < 5                                  ||
                    cursor_position.y() < 5                                  ||
                    cursor_position.x() - get_allocation().get_width()  > -5 ||
                    cursor_position.y() - get_allocation().get_height() > -5 ) {

                    // Reset everything.
                    _split_mode = Inkscape::SplitMode::NORMAL;
                    _split_frac = {0.5, 0.5};
                    set_cursor();
                    queue_draw();

                    // Update action (turn into utility function?).
                    auto window = dynamic_cast<Gtk::ApplicationWindow*>(get_toplevel());
                    if (!window) {
                        std::cerr << "Canvas::on_motion_notify_event: window missing!" << std::endl;
                        return true;
                    }

                    auto action = window->lookup_action("canvas-split-mode");
                    if (!action) {
                        std::cerr << "Canvas::on_motion_notify_event: action 'canvas-split-mode' missing!" << std::endl;
                        return true;
                    }

                    auto saction = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(action);
                    if (!saction) {
                        std::cerr << "Canvas::on_motion_notify_event: action 'canvas-split-mode' not SimpleAction!" << std::endl;
                        return true;
                    }

                    saction->change_state((int)Inkscape::SplitMode::NORMAL);
                }

                break;
        }
    }

    // Otherwise, handle as a delayed event.
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(button_event));
}

bool Canvas::on_enter_notify_event(GdkEventCrossing *crossing_event)
{
    if (crossing_event->window != get_window()->gobj()) {
        std::cout << "  WHOOPS... this does really happen" << std::endl;
        return false;
    }
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(crossing_event));
}

bool Canvas::on_leave_notify_event(GdkEventCrossing *crossing_event)
{
    if (crossing_event->window != get_window()->gobj()) {
        std::cout << "  WHOOPS... this does really happen" << std::endl;
        return false;
    }
    d->last_mouse = {};
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(crossing_event));
}

bool Canvas::on_focus_in_event(GdkEventFocus *focus_event)
{
    grab_focus();
    return false;
}

bool Canvas::on_key_press_event(GdkEventKey *key_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(key_event));
}

bool Canvas::on_key_release_event(GdkEventKey *key_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(key_event));
}

bool Canvas::on_motion_notify_event(GdkEventMotion *motion_event)
{
    // Record the last mouse position.
    d->last_mouse = Geom::IntPoint(motion_event->x, motion_event->y);

    // Handle interactions with the split view controller.
    if (_split_mode == Inkscape::SplitMode::XRAY) {
        queue_draw();
    } else if (_split_mode == Inkscape::SplitMode::SPLIT) {
        auto cursor_position = Geom::IntPoint(motion_event->x, motion_event->y);

        // Move controller.
        if (_split_dragging) {
            auto delta = cursor_position - _split_drag_start;
            if (_hover_direction == Inkscape::SplitDirection::HORIZONTAL) {
                delta.x() = 0;
            } else if (_hover_direction == Inkscape::SplitDirection::VERTICAL) {
                delta.y() = 0;
            }
            _split_frac += Geom::Point(delta) / get_dimensions();
            _split_drag_start = cursor_position;
            queue_draw();
            return true;
        }

        auto split_position = (_split_frac * get_dimensions()).round();
        auto diff = cursor_position - split_position;
        auto hover_direction = Inkscape::SplitDirection::NONE;
        if (Geom::Point(diff).length() < 20.0) {
            // We're hovering over circle, figure out which direction we are in.
            if (diff.y() - diff.x() > 0) {
                if (diff.y() + diff.x() > 0) {
                    hover_direction = Inkscape::SplitDirection::SOUTH;
                } else {
                    hover_direction = Inkscape::SplitDirection::WEST;
                }
            } else {
                if (diff.y() + diff.x() > 0) {
                    hover_direction = Inkscape::SplitDirection::EAST;
                } else {
                    hover_direction = Inkscape::SplitDirection::NORTH;
                }
            }
        } else if (_split_direction == Inkscape::SplitDirection::NORTH ||
                   _split_direction == Inkscape::SplitDirection::SOUTH) {
            if (std::abs(diff.y()) < 3) {
                // We're hovering over horizontal line
                hover_direction = Inkscape::SplitDirection::HORIZONTAL;
            }
        } else {
            if (std::abs(diff.x()) < 3) {
                // We're hovering over vertical line
                hover_direction = Inkscape::SplitDirection::VERTICAL;
            }
        }

        if (_hover_direction != hover_direction) {
            _hover_direction = hover_direction;
            set_cursor();
            queue_draw();
        }

        if (_hover_direction != Inkscape::SplitDirection::NONE) {
            // We're hovering, don't pick or emit event.
            return true;
        }
    }

    // Otherwise, handle as a delayed event.
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(motion_event));
}

// Most events end up here. We store them in the bucket, and process them as soon as possible after
// the next 'on_draw'. If 'on_draw' isn't pending, we use the 'tick_callback' signal to process them
// when 'on_draw' would have run anyway. If 'on_draw' later becomes pending, we remove this signal.

// Add an event to the bucket and ensure it will be emptied in the near future.
bool CanvasPrivate::add_to_bucket(const GdkEvent *event)
{
    framecheck_whole_function(this)

    if (!active) {
        std::cerr << "Canvas::add_to_bucket: Called while not active!" << std::endl;
        return false;
    }

    // Prevent re-fired events from going through again.
    if (event == eventprocessor->ignore) {
        return false;
    }

    // If this is the first event, ensure event processing will run on the main loop as soon as possible after the next frame has started.
    if (eventprocessor->events.empty() && !pending_draw) {
        assert(!bucket_emptier_tick_callback); // Guaranteed since cleared when the event queue is emptied and not set until non-empty again.
        bucket_emptier_tick_callback = q->add_tick_callback([this] (const Glib::RefPtr<Gdk::FrameClock>&) {
            assert(active); // Guaranteed since disconnected upon becoming inactive and not scheduled until active again.
            bucket_emptier_tick_callback.reset();
            schedule_bucket_emptier();
            return false;
        });
    }

    // Add a copy to the queue.
    eventprocessor->events.emplace_back(gdk_event_copy(event));

    // Tell GTK the event was handled.
    return true;
}

void CanvasPrivate::schedule_bucket_emptier()
{
    if (!active) {
        std::cerr << "Canvas::schedule_bucket_emptier: Called while not active!" << std::endl;
        return;
    }

    if (!bucket_emptier.connected()) {
        bucket_emptier = Glib::signal_idle().connect([this] {
            assert(active);
            eventprocessor->process();
            return false;
        }, G_PRIORITY_HIGH_IDLE + 14); // before hipri_idle
    }
}

void CanvasPrivate::disconnect_bucket_emptier_tick_callback()
{
    if (bucket_emptier_tick_callback) {
        q->remove_tick_callback(*bucket_emptier_tick_callback);
        bucket_emptier_tick_callback.reset();
    }
}

// The following functions run at the start of the next frame on the GTK main loop.
// (Note: It is crucial that it runs on the main loop and not in any frame clock tick callbacks. GTK does not allow widgets to be deleted in the latter; only the former.)

// Process bucketed events.
void CanvasPrivate::EventProcessor::process()
{
    framecheck_whole_function(canvasprivate)

    // Ensure the EventProcessor continues to live even if the Canvas is destroyed during event processing.
    auto self = canvasprivate->eventprocessor;

    // Check if toplevel or recursive. (Recursive calls happen if processing an event starts its own nested GTK main loop.)
    bool toplevel = !in_processing;
    in_processing = true;

    // If toplevel, run compression, and initialise the iteration index. It may be incremented externally by gobblers or recursive calls.
    if (toplevel) {
        if (compression) compress();
        pos = 0;
    }

    while (pos < events.size()) {
        // Extract next event.
        auto event = std::move(events[pos]);
        pos++;

        // Fire the event at the CanvasItems and see if it was handled.
        bool handled = canvasprivate->process_bucketed_event(event.get());

        if (!handled) {
            // Re-fire the event at the window, and ignore it when it comes back here again.
            ignore = event.get();
            canvasprivate->q->get_toplevel()->event(event.get());
            ignore = nullptr;
        }

        // If the Canvas was destroyed or deactivated during event processing, exit now.
        if (!canvasprivate || !canvasprivate->active) return;
    }

    // Otherwise, clear the list of events that was just processed.
    events.clear();

    // Disconnect the bucket emptier tick callback, as no longer anything to empty.
    canvasprivate->disconnect_bucket_emptier_tick_callback();

    // Reset the variable to track recursive calls.
    if (toplevel) {
        in_processing = false;
    }
}

// Called before event processing starts to perform event compression.
void CanvasPrivate::EventProcessor::compress()
{
    int in = 0, out = 0;

    while (in < events.size()) {
        // Compress motion events belonging to the same device.
        if (events[in]->type == GDK_MOTION_NOTIFY) {
            auto begin = in, end = in + 1;
            while (end < events.size() && events[end]->type == GDK_MOTION_NOTIFY && events[end]->motion.device == events[begin]->motion.device) end++;
            // Check if there is more than one event to compress.
            if (end != begin + 1) {
                // Keep only the last event.
                events[out] = std::move(events[end - 1]);
                in = end;
                out++;
                continue;
            }
        }

        // Todo: Could consider compressing other events too (e.g. scrolls) if it helps.

        // Otherwise, leave the event untouched.
        if (in != out) events[out] = std::move(events[in]);
        in++;
        out++;
    }

    events.resize(out);
}

void Canvas::set_event_compression(bool enabled)
{
    d->eventprocessor->compression = enabled;
}

// Called during event processing by some tools to batch backlogs of key events that may have built up after a freeze.
int Canvas::gobble_key_events(guint keyval, guint mask)
{
    return d->eventprocessor->gobble_key_events(keyval, mask);
}

int CanvasPrivate::EventProcessor::gobble_key_events(guint keyval, guint mask)
{
    int count = 0;

    while (pos < events.size()) {
        auto &event = events[pos];
        if ((event->type == GDK_KEY_PRESS || event->type == GDK_KEY_RELEASE) && event->key.keyval == keyval && (!mask || (event->key.state & mask))) {
            // Discard event and continue.
            if (event->type == GDK_KEY_PRESS) count++;
            pos++;
        } else {
            // Stop discarding.
            break;
        }
    }

    if (count > 0 && canvasprivate->prefs.debug_logging) std::cout << "Gobbled " << count << " key press(es)" << std::endl;

    return count;
}

// Called during event processing by some tools to ignore backlogs of motion events that may have built up after a freeze.
// Todo: Largely obviated since the introduction of event compression. May be possible to remove.
void Canvas::gobble_motion_events(guint mask)
{
    d->eventprocessor->gobble_motion_events(mask);
}

void CanvasPrivate::EventProcessor::gobble_motion_events(guint mask)
{
    int count = 0;

    while (pos < events.size()) {
        auto &event = events[pos];
        if (event->type == GDK_MOTION_NOTIFY && (event->motion.state & mask)) {
            // Discard event and continue.
            count++;
            pos++;
        } else {
            // Stop discarding.
            break;
        }
    }

    if (count > 0 && canvasprivate->prefs.debug_logging) std::cout << "Gobbled " << count << " motion event(s)" << std::endl;
}

// From now on Inkscape's regular event processing logic takes place. The only thing to remember is that
// all of this happens at a slight delay after the original GTK events. Therefore, it's important to make
// sure that stateful variables like '_current_canvas_item' and friends are ONLY read/written within these
// functions, not during the earlier GTK event handlers. Otherwise state confusion will ensue.

bool CanvasPrivate::process_bucketed_event(const GdkEvent *event)
{
    auto calc_button_mask = [&] () -> int {
        switch (event->button.button) {
            case 1:  return GDK_BUTTON1_MASK; break;
            case 2:  return GDK_BUTTON2_MASK; break;
            case 3:  return GDK_BUTTON3_MASK; break;
            case 4:  return GDK_BUTTON4_MASK; break;
            case 5:  return GDK_BUTTON5_MASK; break;
            default: return 0; // Buttons can range at least to 9 but mask defined only to 5.
        }
    };

    // Do event-specific processing.
    switch (event->type) {
        case GDK_SCROLL:
        {
            // Save the current event-receiving item just before scrolling starts. It will continue to receive scroll events until the mouse is moved.
            if (!pre_scroll_grabbed_item) {
                pre_scroll_grabbed_item = q->_current_canvas_item;
                if (q->_grabbed_canvas_item && !q->_current_canvas_item->is_descendant_of(q->_grabbed_canvas_item)) {
                    pre_scroll_grabbed_item = q->_grabbed_canvas_item;
                }
            }

            // Process the scroll event...
            bool retval = emit_event(event);

            // ...then repick.
            q->_state = event->scroll.state;
            pick_current_item(event);

            return retval;
        }

        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        {
            pre_scroll_grabbed_item = nullptr;

            // Pick the current item as if the button were not pressed...
            q->_state = event->button.state;
            pick_current_item(event);

            // ...then process the event.
            q->_state ^= calc_button_mask();
            bool retval = emit_event(event);

            return retval;
        }

        case GDK_BUTTON_RELEASE:
        {
            pre_scroll_grabbed_item = nullptr;

            // Process the event as if the button were pressed...
            q->_state = event->button.state;
            bool retval = emit_event(event);

            // ...then repick after the button has been released.
            auto event_copy = make_unique_copy(event);
            event_copy->button.state ^= calc_button_mask();
            q->_state = event_copy->button.state;
            pick_current_item(event_copy.get());

            return retval;
        }

        case GDK_ENTER_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->crossing.state;
            return pick_current_item(event);

        case GDK_LEAVE_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->crossing.state;
            // This is needed to remove alignment or distribution snap indicators.
            if (q->_desktop) {
                q->_desktop->snapindicator->remove_snaptarget();
            }
            return pick_current_item(event);

        case GDK_KEY_PRESS:
        case GDK_KEY_RELEASE:
            return emit_event(event);

        case GDK_MOTION_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->motion.state;
            pick_current_item(event);
            return emit_event(event);

        default:
            return false;
    }
}

// This function is called by 'process_bucketed_event' to manipulate the state variables relating
// to the current object under the mouse, for example, to generate enter and leave events.
// (A more detailed explanation by Tavmjong follows.)
// --------
// This routine reacts to events from the canvas. It's main purpose is to find the canvas item
// closest to the cursor where the event occurred and then send the event (sometimes modified) to
// that item. The event then bubbles up the canvas item tree until an object handles it. If the
// widget is redrawn, this routine may be called again for the same event.
//
// Canvas items register their interest by connecting to the "event" signal.
// Example in desktop.cpp:
//   canvas_catchall->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), this));
bool CanvasPrivate::pick_current_item(const GdkEvent *event)
{
    // Ensure requested geometry updates are performed first.
    if (q->_need_update) {
        q->_canvas_item_root->update(geom_affine);
        q->_need_update = false;
    }

    int button_down = 0;
    if (!q->_all_enter_events) {
        // Only set true in connector-tool.cpp.

        // If a button is down, we'll perform enter and leave events on the
        // current item, but not enter on any other item.  This is more or
        // less like X pointer grabbing for canvas items.
        button_down = q->_state & (GDK_BUTTON1_MASK |
                                   GDK_BUTTON2_MASK |
                                   GDK_BUTTON3_MASK |
                                   GDK_BUTTON4_MASK |
                                   GDK_BUTTON5_MASK);
        if (!button_down) q->_left_grabbed_item = false;
    }

    // Save the event in the canvas.  This is used to synthesize enter and
    // leave events in case the current item changes.  It is also used to
    // re-pick the current item if the current one gets deleted.  Also,
    // synthesize an enter event.
    if (event != &q->_pick_event) {
        if (event->type == GDK_MOTION_NOTIFY || event->type == GDK_SCROLL || event->type == GDK_BUTTON_RELEASE) {
            // Convert to GDK_ENTER_NOTIFY

            // These fields have the same offsets in all types of events.
            q->_pick_event.crossing.type       = GDK_ENTER_NOTIFY;
            q->_pick_event.crossing.window     = event->motion.window;
            q->_pick_event.crossing.send_event = event->motion.send_event;
            q->_pick_event.crossing.subwindow  = nullptr;
            q->_pick_event.crossing.x          = event->motion.x;
            q->_pick_event.crossing.y          = event->motion.y;
            q->_pick_event.crossing.mode       = GDK_CROSSING_NORMAL;
            q->_pick_event.crossing.detail     = GDK_NOTIFY_NONLINEAR;
            q->_pick_event.crossing.focus      = false;

            // These fields don't have the same offsets in all types of events.
            switch (event->type)
            {
                case GDK_MOTION_NOTIFY:
                    q->_pick_event.crossing.state  = event->motion.state;
                    q->_pick_event.crossing.x_root = event->motion.x_root;
                    q->_pick_event.crossing.y_root = event->motion.y_root;
                    break;
                case GDK_SCROLL:
                    q->_pick_event.crossing.state  = event->scroll.state;
                    q->_pick_event.crossing.x_root = event->scroll.x_root;
                    q->_pick_event.crossing.y_root = event->scroll.y_root;
                    break;
                case GDK_BUTTON_RELEASE:
                    q->_pick_event.crossing.state  = event->button.state;
                    q->_pick_event.crossing.x_root = event->button.x_root;
                    q->_pick_event.crossing.y_root = event->button.y_root;
                    break;
                default:
                    assert(false);
            }

        } else {
            q->_pick_event = *event;
        }
    }

    if (q->_in_repick) {
        // Don't do anything else if this is a recursive call.
        return false;
    }

    // Find new item
    q->_current_canvas_item_new = nullptr;

    if (q->_pick_event.type != GDK_LEAVE_NOTIFY && q->_canvas_item_root->is_visible()) {
        // Leave notify means there is no current item.
        // Find closest item.
        double x = 0.0;
        double y = 0.0;

        if (q->_pick_event.type == GDK_ENTER_NOTIFY) {
            x = q->_pick_event.crossing.x;
            y = q->_pick_event.crossing.y;
        } else {
            x = q->_pick_event.motion.x;
            y = q->_pick_event.motion.y;
        }

        // If in split mode, look at where cursor is to see if one should pick with outline mode.
        auto split_position = q->_split_frac * q->get_dimensions();
        if (q->_split_mode == Inkscape::SplitMode::SPLIT && q->_render_mode != Inkscape::RenderMode::OUTLINE_OVERLAY) {
            if ((q->_split_direction == Inkscape::SplitDirection::NORTH && y > split_position.y()) ||
                (q->_split_direction == Inkscape::SplitDirection::SOUTH && y < split_position.y()) ||
                (q->_split_direction == Inkscape::SplitDirection::WEST  && x > split_position.x()) ||
                (q->_split_direction == Inkscape::SplitDirection::EAST  && x < split_position.x()) ) {
                q->_drawing->setRenderMode(Inkscape::RenderMode::OUTLINE);
            }
        }
        // Convert to world coordinates.
        auto p = Geom::Point(x, y) + q->_pos;
        if (decoupled_mode) {
            p *= geom_affine * q->_affine.inverse();
        }

        q->_current_canvas_item_new = q->_canvas_item_root->pick_item(p);
        // if (q->_current_canvas_item_new) {
        //     std::cout << "  PICKING: FOUND ITEM: " << q->_current_canvas_item_new->get_name() << std::endl;
        // } else {
        //     std::cout << "  PICKING: DID NOT FIND ITEM" << std::endl;
        // }
        
        // Reset the drawing back to the requested render mode.
        q->_drawing->setRenderMode(q->_render_mode);
    }

    if (q->_current_canvas_item_new == q->_current_canvas_item && !q->_left_grabbed_item) {
        // Current item did not change!
        return false;
    }

    // Synthesize events for old and new current items.
    bool retval = false;
    if (q->_current_canvas_item_new != q->_current_canvas_item &&
        q->_current_canvas_item != nullptr                     &&
        !q->_left_grabbed_item                                 ) {

        GdkEvent new_event;
        new_event = q->_pick_event;
        new_event.type = GDK_LEAVE_NOTIFY;
        new_event.crossing.detail = GDK_NOTIFY_ANCESTOR;
        new_event.crossing.subwindow = nullptr;
        q->_in_repick = true;
        retval = emit_event(&new_event);
        q->_in_repick = false;
    }

    if (q->_all_enter_events == false) {
        // new_current_item may have been set to nullptr during the call to emitEvent() above.
        if (q->_current_canvas_item_new != q->_current_canvas_item && button_down) {
            q->_left_grabbed_item = true;
            return retval;
        }
    }

    // Handle the rest of cases
    q->_left_grabbed_item = false;
    q->_current_canvas_item = q->_current_canvas_item_new;

    if (q->_current_canvas_item != nullptr) {
        GdkEvent new_event;
        new_event = q->_pick_event;
        new_event.type = GDK_ENTER_NOTIFY;
        new_event.crossing.detail = GDK_NOTIFY_ANCESTOR;
        new_event.crossing.subwindow = nullptr;
        retval = emit_event(&new_event);
    }

    return retval;
}

// Fires an event at the canvas, after a little pre-processing. Returns true if handled.
bool CanvasPrivate::emit_event(const GdkEvent *event)
{
    // Handle grabbed items.
    if (q->_grabbed_canvas_item) {
        auto mask = (Gdk::EventMask)0;

        switch (event->type) {
            case GDK_ENTER_NOTIFY:
                mask = Gdk::ENTER_NOTIFY_MASK;
                break;
            case GDK_LEAVE_NOTIFY:
                mask = Gdk::LEAVE_NOTIFY_MASK;
                break;
            case GDK_MOTION_NOTIFY:
                mask = Gdk::POINTER_MOTION_MASK;
                break;
            case GDK_BUTTON_PRESS:
            case GDK_2BUTTON_PRESS:
            case GDK_3BUTTON_PRESS:
                mask = Gdk::BUTTON_PRESS_MASK;
                break;
            case GDK_BUTTON_RELEASE:
                mask = Gdk::BUTTON_RELEASE_MASK;
                break;
            case GDK_KEY_PRESS:
                mask = Gdk::KEY_PRESS_MASK;
                break;
            case GDK_KEY_RELEASE:
                mask = Gdk::KEY_RELEASE_MASK;
                break;
            case GDK_SCROLL:
                mask = Gdk::SCROLL_MASK;
                mask |= Gdk::SMOOTH_SCROLL_MASK;
                break;
            default:
                break;
        }

        if (!(mask & q->_grabbed_event_mask)) {
            return false;
        }
    }

    // Convert to world coordinates. We have two different cases due to different event structures.
    auto conv = [&, this] (double &x, double &y) {
        auto p = Geom::Point(x, y) + q->_pos;
        if (decoupled_mode) {
            p *= q->_affine.inverse() * geom_affine;
        }
        x = p.x();
        y = p.y();
    };

    auto event_copy = make_unique_copy(event);

    switch (event->type) {
        case GDK_ENTER_NOTIFY:
        case GDK_LEAVE_NOTIFY:
            conv(event_copy->crossing.x, event_copy->crossing.y);
            break;
        case GDK_MOTION_NOTIFY:
        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            conv(event_copy->motion.x, event_copy->motion.y);
            break;
        default:
            break;
    }

    // Block undo/redo while anything is dragged.
    if (event->type == GDK_BUTTON_PRESS && event->button.button == 1) {
        q->_is_dragging = true;
    } else if (event->type == GDK_BUTTON_RELEASE) {
        q->_is_dragging = false;
    }

    if (q->_current_canvas_item) {
        // Choose where to send event.
        auto item = q->_current_canvas_item;

        if (q->_grabbed_canvas_item && !q->_current_canvas_item->is_descendant_of(q->_grabbed_canvas_item)) {
            item = q->_grabbed_canvas_item;
        }

        if (pre_scroll_grabbed_item && event->type == GDK_SCROLL) {
            item = pre_scroll_grabbed_item;
        }

        // Propagate the event up the canvas item hierarchy until handled.
        while (item) {
            if (item->handle_event(event_copy.get())) return true;
            item = item->get_parent();
        }
    }

    return false;
}

/*
 * Protected functions
 */

Geom::IntPoint Canvas::get_dimensions() const
{
    return dimensions(get_allocation());
}

/**
 * Is world point inside canvas area?
 */
bool Canvas::world_point_inside_canvas(Geom::Point const &world) const
{
    return get_area_world().contains(world.floor());
}

/**
 * Translate point in canvas to world coordinates.
 */
Geom::Point Canvas::canvas_to_world(Geom::Point const &point) const
{
    return point + _pos;
}

/**
 * Return the area shown in the canvas in world coordinates.
 */
Geom::IntRect Canvas::get_area_world() const
{
    return Geom::IntRect(_pos, _pos + get_dimensions());
}

/**
 * Return the last known mouse position of center if off-canvas.
 */
std::optional<Geom::Point> Canvas::get_last_mouse() const
{
    return d->last_mouse;
}

/**
 * Set the affine for the canvas.
 */
void Canvas::set_affine(Geom::Affine const &affine)
{
    if (_affine == affine) {
        return;
    }

    _affine = affine;

    d->add_idle();
    queue_draw();
}

const Geom::Affine &Canvas::get_geom_affine() const
{
    return d->geom_affine;
}

void CanvasPrivate::queue_draw_area(const Geom::IntRect &rect)
{
    if (q->get_opengl_enabled()) {
        // Note: GTK glitches out when you use queue_draw_area in OpenGL mode.
        // Also, does GTK actually obey this command, or redraw the whole window?
        q->queue_draw();
    } else {
        q->queue_draw_area(rect.left(), rect.top(), rect.width(), rect.height());
    }
}

/**
 * Invalidate drawing and redraw during idle.
 */
void Canvas::redraw_all()
{
    if (!d->active) {
        // CanvasItems redraw their area when being deleted... which happens when the Canvas is destroyed.
        // We need to ignore their requests!
        return;
    }
    d->updater->reset(); // Empty region (i.e. everything is dirty).
    d->add_idle();
    if (d->prefs.debug_show_unclean) queue_draw();
}

/**
 * Redraw the given area during idle.
 */
void Canvas::redraw_area(int x0, int y0, int x1, int y1)
{
    if (!d->active) {
        // CanvasItems redraw their area when being deleted... which happens when the Canvas is destroyed.
        // We need to ignore their requests!
        return;
    }

    // Clamp area to Cairo's technically supported max size (-2^30..+2^30-1).
    // This ensures that the rectangle dimensions don't overflow and wrap around.
    constexpr int min_coord = -(1 << 30);
    constexpr int max_coord = (1 << 30) - 1;

    x0 = std::clamp(x0, min_coord, max_coord);
    y0 = std::clamp(y0, min_coord, max_coord);
    x1 = std::clamp(x1, min_coord, max_coord);
    y1 = std::clamp(y1, min_coord, max_coord);

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    auto rect = Geom::IntRect::from_xywh(x0, y0, x1 - x0, y1 - y0);
    d->updater->mark_dirty(rect);
    d->add_idle();
    if (d->prefs.debug_show_unclean) queue_draw();
}

void Canvas::redraw_area(Geom::Coord x0, Geom::Coord y0, Geom::Coord x1, Geom::Coord y1)
{
    // Handle overflow during conversion gracefully.
    // Round outward to make sure integral coordinates cover the entire area.
    constexpr Geom::Coord min_int = std::numeric_limits<int>::min();
    constexpr Geom::Coord max_int = std::numeric_limits<int>::max();

    redraw_area(
        (int)std::floor(std::clamp(x0, min_int, max_int)),
        (int)std::floor(std::clamp(y0, min_int, max_int)),
        (int)std::ceil (std::clamp(x1, min_int, max_int)),
        (int)std::ceil (std::clamp(y1, min_int, max_int))
    );
}

void Canvas::redraw_area(Geom::Rect &area)
{
    redraw_area(area.left(), area.top(), area.right(), area.bottom());
}

/**
 * Redraw after changing canvas item geometry.
 */
void Canvas::request_update()
{
    // Flag geometry as needing update.
    _need_update = true;

    // Trigger the idle process to perform the update.
    d->add_idle();
}

/**
 * Scroll window so drawing point 'pos' is at upper left corner of canvas.
 */
void Canvas::set_pos(Geom::IntPoint const &pos)
{
    if (pos == _pos) {
        return;
    }

    _pos = pos;

    d->add_idle();
    queue_draw();

    if (auto grid = dynamic_cast<Inkscape::UI::Widget::CanvasGrid*>(get_parent())) {
        grid->UpdateRulers();
    }
}

/**
 * Set the desk colour. Transparency is interpreted as amount of checkerboard.
 */
void Canvas::set_desk(uint32_t rgba)
{
    if (d->desk == rgba) return;
    d->desk = rgba;
    if (get_realized() && !get_opengl_enabled() && d->crstate()->update_colours(d->page, d->desk)) redraw_all();
    queue_draw();
}

/**
 * Set the page border colour. Although we don't draw the borders, this colour affects the shadows which we do draw (in OpenGL mode).
 */
void Canvas::set_border(uint32_t rgba)
{
    if (d->border == rgba) return;
    d->border = rgba;
    if (get_realized() && get_opengl_enabled()) queue_draw();
}

/**
 * Set the page colour. Like the desk colour, transparency is interpreted as checkerboard.
 */
void Canvas::set_page(uint32_t rgba)
{
    if (d->page == rgba) return;
    d->page = rgba;
    if (get_realized() && !get_opengl_enabled() && d->crstate()->update_colours(d->page, d->desk)) redraw_all();
    queue_draw();
}

uint32_t Canvas::get_effective_background() const
{
    auto arr = checkerboard_darken(rgb_to_array(d->desk), 1.0f - 0.5f * SP_RGBA32_A_U(d->desk) / 255.0f);
    return SP_RGBA32_F_COMPOSE(arr[0], arr[1], arr[2], 1.0);
}

void Canvas::set_drawing_disabled(bool disable)
{
    _drawing_disabled = disable;
    if (!disable) {
        d->add_idle();
    }
}

void Canvas::set_render_mode(Inkscape::RenderMode mode)
{
    if (_render_mode != mode) {
        _render_mode = mode;
        _drawing->setRenderMode(_render_mode);
        redraw_all();
    }
    if (_desktop) {
        _desktop->setWindowTitle(); // Mode is listed in title.
    }
}

void Canvas::set_color_mode(Inkscape::ColorMode mode)
{
    if (_color_mode != mode) {
        _color_mode = mode;
        redraw_all();
    }
    if (_desktop) {
        _desktop->setWindowTitle(); // Mode is listed in title.
    }
}

void Canvas::set_split_mode(Inkscape::SplitMode mode)
{
    if (_split_mode != mode) {
        _split_mode = mode;
        if (_split_mode == Inkscape::SplitMode::SPLIT) {
            _hover_direction = Inkscape::SplitDirection::NONE;
        }
        redraw_all();
    }
}

void Canvas::set_cms_key(std::string key)
{
    _cms_key = std::move(key);
    _cms_active = !_cms_key.empty();
    redraw_all();
}

/**
 * Clear current and grabbed items.
 */
void Canvas::canvas_item_destructed(Inkscape::CanvasItem *item)
{
    if (item == _current_canvas_item) {
        _current_canvas_item = nullptr;
    }

    if (item == _current_canvas_item_new) {
        _current_canvas_item_new = nullptr;
    }

    if (item == _grabbed_canvas_item) {
        _grabbed_canvas_item = nullptr;
        auto const display = Gdk::Display::get_default();
        auto const seat    = display->get_default_seat();
        seat->ungrab();
    }

    if (item == d->pre_scroll_grabbed_item) {
        d->pre_scroll_grabbed_item = nullptr;
    }
}

// Change cursor
void Canvas::set_cursor()
{
    if (!_desktop) {
        return;
    }

    auto display = Gdk::Display::get_default();

    switch (_hover_direction) {
        case Inkscape::SplitDirection::NONE:
            _desktop->event_context->use_tool_cursor();
            break;

        case Inkscape::SplitDirection::NORTH:
        case Inkscape::SplitDirection::EAST:
        case Inkscape::SplitDirection::SOUTH:
        case Inkscape::SplitDirection::WEST:
        {
            auto cursor = Gdk::Cursor::create(display, "pointer");
            get_window()->set_cursor(cursor);
            break;
        }

        case Inkscape::SplitDirection::HORIZONTAL:
        {
            auto cursor = Gdk::Cursor::create(display, "ns-resize");
            get_window()->set_cursor(cursor);
            break;
        }

        case Inkscape::SplitDirection::VERTICAL:
        {
            auto cursor = Gdk::Cursor::create(display, "ew-resize");
            get_window()->set_cursor(cursor);
            break;
        }

        default:
            // Shouldn't reach.
            std::cerr << "Canvas::set_cursor: Unknown hover direction!" << std::endl;
    }
}

void Canvas::get_preferred_width_vfunc(int &minimum_width, int &natural_width) const
{
    minimum_width = natural_width = 256;
}

void Canvas::get_preferred_height_vfunc(int &minimum_height, int &natural_height) const
{
    minimum_height = natural_height = 256;
}

void Canvas::on_size_allocate(Gtk::Allocation &allocation)
{
    parent_type::on_size_allocate(allocation);
    assert(allocation == get_allocation());

    // Necessary as GTK seems to somehow invalidate the current pipeline state upon resize.
    if (d->active && get_opengl_enabled()) {
        d->glstate()->state = GLState::State::None;
    }

    // Trigger the size update to be applied to the stores before the next redraw of the window.
    d->add_idle();
}

Glib::RefPtr<Gdk::GLContext> Canvas::create_context()
{
    Glib::RefPtr<Gdk::GLContext> result;

    try {
        result = get_window()->create_gl_context();
    } catch (const Gdk::GLError &e) {
        std::cerr << "Failed to create OpenGL context: " << e.what() << std::endl;
        return {};
    }

    try {
        result->realize();
    } catch (const Glib::Error &e) {
        std::cerr << "Failed to realize OpenGL context: " << e.what() << std::endl;
        return {};
    }

    return result;
}

/*
 * Drawing
 */

std::pair<Geom::IntRect, Geom::IntRect> CanvasPrivate::calc_splitview_cliprects() const
{
    auto window = Geom::IntRect({}, q->get_dimensions());

    auto content = window;
    auto outline = window;
    auto split = [&] (Geom::Dim2 dim, Geom::IntRect &lo, Geom::IntRect &hi) {
        int s = std::round(q->_split_frac[dim] * q->get_dimensions()[dim]);
        lo[dim].setMax(s);
        hi[dim].setMin(s);
    };

    switch (q->_split_direction) {
        case Inkscape::SplitDirection::NORTH: split(Geom::Y, content, outline); break;
        case Inkscape::SplitDirection::EAST:  split(Geom::X, outline, content); break;
        case Inkscape::SplitDirection::SOUTH: split(Geom::Y, outline, content); break;
        case Inkscape::SplitDirection::WEST:  split(Geom::X, content, outline); break;
        default: assert(false); break;
    }

    return std::make_pair(content, outline);
}

void CanvasPrivate::draw_splitview_controller(const Cairo::RefPtr<Cairo::Context> &cr) const
{
    auto split_position = (q->_split_frac * q->get_dimensions()).round();

    // Add dividing line.
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->set_line_width(1.0);
    if (q->_split_direction == Inkscape::SplitDirection::EAST ||
        q->_split_direction == Inkscape::SplitDirection::WEST) {
        cr->move_to(split_position.x() + 0.5, 0.0                    );
        cr->line_to(split_position.x() + 0.5, q->get_dimensions().y());
        cr->stroke();
    } else {
        cr->move_to(0.0                    , split_position.y() + 0.5);
        cr->line_to(q->get_dimensions().x(), split_position.y() + 0.5);
        cr->stroke();
    }

    // Add controller image.
    double a = q->_hover_direction == Inkscape::SplitDirection::NONE ? 0.5 : 1.0;
    cr->set_source_rgba(0.2, 0.2, 0.2, a);
    cr->arc(split_position.x(), split_position.y(), 20, 0, 2 * M_PI);
    cr->fill();

    for (int i = 0; i < 4; i++) {
        // The four direction triangles.
        cr->save();

        // Position triangle.
        cr->translate(split_position.x(), split_position.y());
        cr->rotate((i + 2) * M_PI / 2);

        // Draw triangle.
        cr->move_to(-5,  8);
        cr->line_to( 0, 18);
        cr->line_to( 5,  8);
        cr->close_path();

        double b = (int)q->_hover_direction == (i + 1) ? 0.9 : 0.7;
        cr->set_source_rgba(b, b, b, a);
        cr->fill();

        cr->restore();
    }
}

// Structure used for holding the list of pages.
struct PageInfo
{
    std::vector<Geom::Rect> pages;

    PageInfo(const CanvasItemGroup *root)
    {
        root->visit_page_rects([&] (const Geom::Rect &rect) {
            pages.push_back(rect);
        });
    }

    // Check whether a single page is occupying the whole fragment.
    bool check_single_page(const Fragment &fragment) const
    {
        auto pl = Geom::Parallelogram(fragment.rect) * fragment.affine.inverse();
        for (auto &rect : pages)
            if (Geom::Parallelogram(rect).contains(pl))
                return true;
        return false;
    }
};

// Paint the background and pages using Cairo into the given fragment.
void CanvasPrivate::paint_background(const Fragment &fragment, const Cairo::RefPtr<Cairo::Context> &cr) const
{
    cr->save();
    cr->set_operator(Cairo::OPERATOR_SOURCE);
    cr->rectangle(0, 0, fragment.rect.width(), fragment.rect.height());
    cr->clip();

    auto pi = PageInfo(q->_canvas_item_root);

    if (desk == page || pi.check_single_page(fragment)) {
        // Desk and page are the same, or a single page fills the whole screen; just clear the fragment to page.
        cr->set_source(CairoState::rgba_to_pattern(page));
        cr->paint();
    } else {
        // Paint the background to the complement of the pages. (Slightly overpaints when pages overlap.)
        cr->save();
        cr->set_source(CairoState::rgba_to_pattern(desk));
        cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
        cr->rectangle(0, 0, fragment.rect.width(), fragment.rect.height());
        cr->translate(-fragment.rect.left(), -fragment.rect.top());
        cr->transform(geom_to_cairo(fragment.affine));
        for (auto &rect : pi.pages) {
            cr->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        }
        cr->fill();
        cr->restore();

        // Paint the pages.
        cr->save();
        cr->set_source(CairoState::rgba_to_pattern(page));
        cr->translate(-fragment.rect.left(), -fragment.rect.top());
        cr->transform(geom_to_cairo(fragment.affine));
        for (auto &rect : pi.pages) {
            cr->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        }
        cr->fill();
        cr->restore();
    }

    cr->restore();
}

void Canvas::paint_widget(const Cairo::RefPtr<Cairo::Context> &cr)
{
    framecheck_whole_function(d)

    if (d->prefs.debug_idle_starvation && d->idle_running) d->wait_accumulated += g_get_monotonic_time() - d->wait_begin;

    if (!d->active) {
        std::cerr << "Canvas::paint_widget: Called while not active!" << std::endl;
        return;
    }

    // sp_canvas_item_recursive_print_tree(0, _root);
    // canvas_item_print_tree(_canvas_item_root);

    // Although hipri_idle is scheduled at a priority higher than draw, and should therefore always be called first if
    // asked, there are times when GTK simply decides to call on_draw anyway. Here we ensure that that call has taken
    // place. This is problematic because if hipri_idle does rendering, enlarging the damage rect, then our drawing will
    // still be clipped to the old damage rect. It was precisely this problem that lead to the introduction of
    // hipri_idle. Fortunately, the following failsafe only seems to execute once during initialisation, and once on
    // further resize events. Both these events seem to trigger a full damage, hence we are ok.
    if (d->hipri_idle.connected()) {
        d->hipri_idle.disconnect();
        d->on_hipri_idle();
    }

    // Calculate the fragment corresponding to the screen.
    Fragment screen;
    screen.affine = _affine;
    screen.rect = get_area_world();

    if (get_opengl_enabled())
    {
        auto gl = d->glstate();

        // Must be done after the above idle rendering, in case it binds a different framebuffer.
        bind_framebuffer();

        // If in decoupled mode, create the vertex data describing the clean region.
        VAO clean_vao;
        int clean_numrects;
        if (d->decoupled_mode) {
            std::tie(clean_vao, clean_numrects) = GLState::clean_region_shrink_vao(d->updater->clean_region, gl->store.rect);
        }

        // Set up the base pipeline.
        gl->state = GLState::State::PaintWidget;
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, get_allocation().get_width() * d->device_scale, get_allocation().get_height() * d->device_scale);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NOTEQUAL, 1, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl->store.texture.get_id());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl->snapshot.texture.get_id());
        if (d->need_outline_store()) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gl->store.outline_texture.get_id());
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gl->snapshot.outline_texture.get_id());
        }
        glBindVertexArray(gl->rect.vao);

        // Clear the buffers. Since we have to pick a clear colour, we choose the page colour, enabling the single-page optimisation later.
        glClearColor(SP_RGBA32_R_U(d->page) / 255.0f, SP_RGBA32_G_U(d->page) / 255.0f, SP_RGBA32_B_U(d->page) / 255.0f, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        auto pi = PageInfo(_canvas_item_root);

        if (pi.check_single_page(screen)) {
            // A single page occupies the whole screen.
            if (SP_RGBA32_A_U(d->page) == 255) {
                // Page is solid - nothing to do, since already cleared to this colour.
            } else {
                // Page is checkerboard - fill screen with page pattern.
                glDisable(GL_BLEND);
                glUseProgram(gl->checker.id);
                glUniform1f(gl->checker.loc("size"), 12.0 * d->device_scale);
                glUniform3fv(gl->checker.loc("col1"), 1, std::begin(rgb_to_array(d->page)));
                glUniform3fv(gl->checker.loc("col2"), 1, std::begin(checkerboard_darken(d->page)));
                geom_to_uniform(Geom::Scale(2.0, -2.0) * Geom::Translate(-1.0, 1.0), gl->checker.loc("mat"), gl->checker.loc("trans"));
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);

            auto set_page_transform = [&] (const Geom::Rect &rect, const Program &prog) {
                geom_to_uniform(Geom::Scale(rect.dimensions()) * Geom::Translate(rect.min()) * GLState::calc_paste_transform({{}, Geom::IntRect::from_xywh(0, 0, 1, 1)}, screen) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
            };

            // Pages
            glUseProgram(gl->checker.id);
            glUniform1f(gl->checker.loc("size"), 12.0 * d->device_scale);
            glUniform3fv(gl->checker.loc("col1"), 1, std::begin(rgb_to_array(d->page)));
            glUniform3fv(gl->checker.loc("col2"), 1, std::begin(checkerboard_darken(d->page)));
            for (auto &rect : pi.pages) {
                set_page_transform(rect, gl->checker);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }

            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

            // Desk
            glUniform3fv(gl->checker.loc("col1"), 1, std::begin(rgb_to_array(d->desk)));
            glUniform3fv(gl->checker.loc("col2"), 1, std::begin(checkerboard_darken(d->desk)));
            geom_to_uniform(Geom::Scale(2.0, -2.0) * Geom::Translate(-1.0, 1.0), gl->checker.loc("mat"), gl->checker.loc("trans"));
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

            // Shadows
            if (SP_RGBA32_A_U(d->border) != 0) {
                auto dir = (Geom::Point(1.0, 1.0) * _affine * Geom::Scale(1.0, -1.0)).normalized(); // Shadow direction rotates with view.
                glUseProgram(gl->shadow.id);
                glUniform2fv(gl->shadow.loc("wh"), 1, std::begin({(GLfloat)get_allocation().get_width(), (GLfloat)get_allocation().get_height()}));
                glUniform1f(gl->shadow.loc("size"), 40.0 * std::pow(std::abs(_affine.det()), 0.25));
                glUniform2fv(gl->shadow.loc("dir"), 1, std::begin({(GLfloat)dir.x(), (GLfloat)dir.y()}));
                glUniform4fv(gl->shadow.loc("shadow_col"), 1, std::begin(premultiplied(rgba_to_array(d->border))));
                for (auto &rect : pi.pages) {
                    set_page_transform(rect, gl->shadow);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }
            }

            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }

        glStencilFunc(GL_NOTEQUAL, 2, 2);

        enum class DrawMode
        {
            Store,
            Outline,
            Combine
        };

        auto draw_store = [&, this] (const Program &prog, DrawMode drawmode) {
            glUseProgram(prog.id);
            glUniform1i(prog.loc("tex"), drawmode == DrawMode::Outline ? 2 : 0);
            if (drawmode == DrawMode::Combine) {
                glUniform1i(prog.loc("tex_outline"), 2);
                glUniform1f(prog.loc("opacity"), d->prefs.outline_overlay_opacity / 100.0);
            }

            if (!d->decoupled_mode) {
                // Backing store fragment.
                geom_to_uniform(GLState::calc_paste_transform(gl->store, screen) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            } else {
                // Backing store fragment, clipped to its clean region.
                geom_to_uniform(GLState::calc_paste_transform(gl->store, screen) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
                glBindVertexArray(clean_vao.vao);
                glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);

                // Snapshot fragment.
                glUniform1i(prog.loc("tex"), drawmode == DrawMode::Outline ? 3 : 1);
                if (drawmode == DrawMode::Combine) glUniform1i(prog.loc("tex_outline"), 3);
                geom_to_uniform(GLState::calc_paste_transform(gl->snapshot, screen) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
                glBindVertexArray(gl->rect.vao);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        };

        if (_split_mode == Inkscape::SplitMode::NORMAL || (_split_mode == Inkscape::SplitMode::XRAY && !d->last_mouse))
        {
            // Drawing the backing store over the whole screen.
            _render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY
                          ? draw_store(gl->outlineoverlay, DrawMode::Combine)
                          : draw_store(gl->texcopy, DrawMode::Store);
        }
        else if (_split_mode == Inkscape::SplitMode::SPLIT)
        {
            // Calculate the clipping rectangles for split view.
            auto [store_clip, outline_clip] = d->calc_splitview_cliprects();

            glEnable(GL_SCISSOR_TEST);

            // Draw the backing store.
            glScissor(store_clip.left() * d->device_scale, (get_dimensions().y() - store_clip.bottom()) * d->device_scale, store_clip.width() * d->device_scale, store_clip.height() * d->device_scale);
            _render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY
                          ? draw_store(gl->outlineoverlay, DrawMode::Combine)
                          : draw_store(gl->texcopy, DrawMode::Store);

            // Draw the outline store.
            glScissor(outline_clip.left() * d->device_scale, (get_dimensions().y() - outline_clip.bottom()) * d->device_scale, outline_clip.width() * d->device_scale, outline_clip.height() * d->device_scale);
            draw_store(gl->texcopy, DrawMode::Outline);

            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);

            // Calculate the bounding rectangle of the split view controller.
            auto rect = Geom::IntRect({}, get_dimensions());
            auto dim = _split_direction == Inkscape::SplitDirection::EAST || _split_direction == Inkscape::SplitDirection::WEST ? Geom::X : Geom::Y;
            rect[dim] = Geom::IntInterval(-21, 21) + std::round(_split_frac[dim] * get_dimensions()[dim]);

            // Lease out a PixelStreamer mapping to draw on.
            auto surface = gl->pixelstreamer->request(rect.dimensions() * d->device_scale);
            cairo_surface_set_device_scale(surface->cobj(), d->device_scale, d->device_scale);

            // Actually draw the content with Cairo.
            auto cr = Cairo::Context::create(surface);
            cr->set_operator(Cairo::OPERATOR_SOURCE);
            cr->set_source_rgba(0.0, 0.0, 0.0, 0.0);
            cr->paint();
            cr->translate(-rect.left(), -rect.top());
            d->draw_splitview_controller(cr);

            // Convert the surface to a texture.
            auto texture = gl->pixelstreamer->finish(std::move(surface));

            // Paint the texture onto the screen.
            glUseProgram(gl->texcopy.id);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture.get_id());
            glUniform1i(gl->texcopy.loc("tex"), 0);
            geom_to_uniform(Geom::Scale(rect.dimensions()) * Geom::Translate(rect.min()) * Geom::Scale(2.0 / get_dimensions().x(), -2.0 / get_dimensions().y()) * Geom::Translate(-1.0, 1.0), gl->texcopy.loc("mat"), gl->texcopy.loc("trans"));
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
        else // if (_split_mode == Inkscape::SplitMode::XRAY && d->last_mouse)
        {
            // Draw the backing store over the whole screen.
            const auto &shader = _render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY ? gl->outlineoverlayxray : gl->xray;
            glUseProgram(shader.id);
            glUniform1f(shader.loc("radius"), d->prefs.x_ray_radius * d->device_scale);
            glUniform2fv(shader.loc("pos"), 1, std::begin({(GLfloat)(d->last_mouse->x() * d->device_scale), (GLfloat)((get_dimensions().y() - d->last_mouse->y()) * d->device_scale)}));
            draw_store(shader, DrawMode::Combine);
        }
    }
    else // if (!get_opengl_enabled())
    {
        auto cs = d->crstate();

        auto f = FrameCheck::Event();

        // Turn off anti-aliasing while compositing the widget for large performance gains. (We can usually
        // get away with it without any negative visual impact; when we can't, we turn it back on.)
        cr->set_antialias(Cairo::ANTIALIAS_NONE);

        // Due to a Cairo bug, Cairo sometimes draws outside of its clip region. This results in flickering as Canvas content is drawn
        // over the bottom scrollbar. This cannot be fixed by setting the correct clip region, as Cairo detects that and turns it into
        // a no-op. Hence the following workaround, which recreates the clip region from scratch, is required.
        auto rlist = cairo_copy_clip_rectangle_list(cr->cobj());
        cr->reset_clip();
        for (int i = 0; i < rlist->num_rectangles; i++) {
            cr->rectangle(rlist->rectangles[i].x, rlist->rectangles[i].y, rlist->rectangles[i].width, rlist->rectangles[i].height);
        }
        cr->clip();
        cairo_rectangle_list_destroy(rlist);

        // Draw background if solid colour optimisation is not enabled. (If enabled, it is baked into the stores.)
        if (!cs->solid_colour) {
            if (d->prefs.debug_framecheck) f = FrameCheck::Event("background");
            d->paint_background(screen, cr);
        }

        // Even if in solid colour mode, draw the part of background not obscured by snapshot if in decoupled mode.
        if (cs->solid_colour && d->decoupled_mode) {
            if (d->prefs.debug_framecheck) f = FrameCheck::Event("composite", 2);
            cr->save();
            cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
            cr->rectangle(0, 0, get_allocation().get_width(), get_allocation().get_height());
            cr->translate(-_pos.x(), -_pos.y());
            cr->transform(geom_to_cairo(_affine * cs->snapshot.affine.inverse()));
            cr->rectangle(cs->snapshot.rect.left(), cs->snapshot.rect.top(), cs->snapshot.rect.width(), cs->snapshot.rect.height());
            cr->clip();
            cr->transform(geom_to_cairo(cs->snapshot.affine * _affine.inverse()));
            cr->translate(_pos.x(), _pos.y());
            d->paint_background(screen, cr);
            cr->restore();
        }

        auto draw_store = [&, this] (const Cairo::RefPtr<Cairo::ImageSurface> &store, const Cairo::RefPtr<Cairo::ImageSurface> &snapshot_store) {
            if (!d->decoupled_mode) {
                // Blit store to screen.
                if (d->prefs.debug_framecheck) f = FrameCheck::Event("draw");
                cr->save();
                cr->set_source(store, cs->store.rect.left() - _pos.x(), cs->store.rect.top() - _pos.y());
                cr->paint();
                cr->restore();
            } else {
                // Draw transformed snapshot, clipped to the complement of the store's clean region.
                if (d->prefs.debug_framecheck) f = FrameCheck::Event("composite", 1);
                cr->save();
                cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
                cr->rectangle(0, 0, get_allocation().get_width(), get_allocation().get_height());
                cr->translate(-_pos.x(), -_pos.y());
                cr->transform(geom_to_cairo(_affine * cs->store.affine.inverse()));
                region_to_path(cr, d->updater->clean_region);
                cr->clip();
                cr->transform(geom_to_cairo(cs->store.affine * cs->snapshot.affine.inverse()));
                cr->rectangle(cs->snapshot.rect.left(), cs->snapshot.rect.top(), cs->snapshot.rect.width(), cs->snapshot.rect.height());
                cr->clip();
                cr->set_source(snapshot_store, cs->snapshot.rect.left(), cs->snapshot.rect.top());
                Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::FILTER_FAST);
                cr->paint();
                if (d->prefs.debug_show_snapshot) {
                    cr->set_source_rgba(0, 0, 1, 0.2);
                    cr->set_operator(Cairo::OPERATOR_OVER);
                    cr->paint();
                }
                cr->restore();

                // Draw transformed store, clipped to clean region.
                if (d->prefs.debug_framecheck) f = FrameCheck::Event("composite", 0);
                cr->save();
                cr->translate(-_pos.x(), -_pos.y());
                cr->transform(geom_to_cairo(_affine * cs->store.affine.inverse()));
                cr->set_source(store, cs->store.rect.left(), cs->store.rect.top());
                Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::FILTER_FAST);
                region_to_path(cr, d->updater->clean_region);
                cr->fill();
                cr->restore();
            }
        };

        auto draw_overlay = [&, this] {
            // Get whitewash opacity.
            double outline_overlay_opacity = 1.0 - d->prefs.outline_overlay_opacity / 100.0;

            // Partially obscure drawing by painting semi-transparent white, then paint outline content.
            // Note: Unfortunately this also paints over the background, but this is unavoidable.
            cr->save();
            cr->set_operator(Cairo::OPERATOR_OVER);
            cr->set_source_rgb(1.0, 1.0, 1.0);
            cr->paint_with_alpha(outline_overlay_opacity);
            draw_store(cs->store.outline_surface, cs->snapshot.outline_surface);
            cr->restore();
        };

        if (_split_mode == Inkscape::SplitMode::SPLIT)
        {
            // Calculate the clipping rectangles for split view.
            auto [store_clip, outline_clip] = d->calc_splitview_cliprects();

            // Draw normal content.
            cr->save();
            cr->rectangle(store_clip.left(), store_clip.top(), store_clip.width(), store_clip.height());
            cr->clip();
            cr->set_operator(cs->solid_colour ? Cairo::OPERATOR_SOURCE : Cairo::OPERATOR_OVER);
            draw_store(cs->store.surface, cs->snapshot.surface);
            if (_render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) draw_overlay();
            cr->restore();

            // Draw outline.
            if (cs->solid_colour) {
                cr->save();
                cr->translate(outline_clip.left(), outline_clip.top());
                d->paint_background(Fragment{_affine, _pos + outline_clip}, cr);
                cr->restore();
            }
            cr->save();
            cr->rectangle(outline_clip.left(), outline_clip.top(), outline_clip.width(), outline_clip.height());
            cr->clip();
            cr->set_operator(Cairo::OPERATOR_OVER);
            draw_store(cs->store.outline_surface, cs->snapshot.outline_surface);
            cr->restore();
        }
        else
        {
            // Draw the normal content over the whole screen.
            cr->set_operator(cs->solid_colour ? Cairo::OPERATOR_SOURCE : Cairo::OPERATOR_OVER);
            draw_store(cs->store.surface, cs->snapshot.surface);
            if (_render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) draw_overlay();

            // Draw outline if in X-ray mode.
            if (_split_mode == Inkscape::SplitMode::XRAY && d->last_mouse) {
                // Clip to circle
                cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
                cr->arc(d->last_mouse->x(), d->last_mouse->y(), d->prefs.x_ray_radius, 0, 2 * M_PI);
                cr->clip();
                cr->set_antialias(Cairo::ANTIALIAS_NONE);
                // Draw background.
                d->paint_background(screen, cr);
                // Draw outline.
                cr->set_operator(Cairo::OPERATOR_OVER);
                draw_store(cs->store.outline_surface, cs->snapshot.outline_surface);
            }
        }

        // The rest can be done with antialiasing.
        cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);

        // Paint unclean regions in red.
        if (d->prefs.debug_show_unclean) {
            if (d->prefs.debug_framecheck) f = FrameCheck::Event("paint_unclean");
            cr->set_operator(Cairo::OPERATOR_OVER);
            auto reg = Cairo::Region::create(geom_to_cairo(cs->store.rect));
            reg->subtract(d->updater->clean_region);
            cr->save();
            cr->translate(-_pos.x(), -_pos.y());
            if (d->decoupled_mode) {
                cr->transform(geom_to_cairo(_affine * cs->store.affine.inverse()));
            }
            cr->set_source_rgba(1, 0, 0, 0.2);
            region_to_path(cr, reg);
            cr->fill();
            cr->restore();
        }

        // Paint internal edges of clean region in green.
        if (d->prefs.debug_show_clean) {
            if (d->prefs.debug_framecheck) f = FrameCheck::Event("paint_clean");
            cr->save();
            cr->translate(-_pos.x(), -_pos.y());
            if (d->decoupled_mode) {
                cr->transform(geom_to_cairo(_affine * cs->store.affine.inverse()));
            }
            cr->set_source_rgba(0, 0.7, 0, 0.4);
            region_to_path(cr, d->updater->clean_region);
            cr->stroke();
            cr->restore();
        }

        if (_split_mode == Inkscape::SplitMode::SPLIT) {
            d->draw_splitview_controller(cr);
        }
    }

    // Process bucketed events as soon as possible after draw. We cannot process them now, because we have
    // a frame to get out as soon as possible, and processing events may take a while. Instead, we schedule
    // it with a signal callback on the main loop that runs as soon as this function is completed.
    if (!d->eventprocessor->events.empty()) d->schedule_bucket_emptier();

    // Record the fact that a draw is no longer pending.
    d->pending_draw = false;

    // Notify the update strategy that another frame has passed.
    d->updater->frame();

    // If asked, print idle time utilisation stats.
    if (d->prefs.debug_idle_starvation && d->sample_begin != 0) {
        auto elapsed = g_get_monotonic_time() - d->sample_begin;
        auto overhead = 100 * d->wait_accumulated / elapsed;
        auto col = overhead < 5 ? "\033[1;32m" : overhead < 20 ? "\033[1;33m" : "\033[1;31m";
        std::cout << "Overhead: " << col << overhead << "%" << "\033[0m" << (d->idle_running ? " [still busy]" : "") << std::endl;
        d->sample_begin = d->wait_begin = g_get_monotonic_time();
        d->wait_accumulated = 0;
    }

    // If asked, run an animation loop.
    if (d->prefs.debug_animate) {
        auto t = g_get_monotonic_time() / 1700000.0;
        auto affine = Geom::Rotate(t * 5) * Geom::Scale(1.0 + 0.6 * cos(t * 2));
        set_affine(affine);
        auto dim = _desktop && _desktop->doc() ? _desktop->doc()->getDimensions() : Geom::Point();
        set_pos(Geom::Point((0.5 + 0.3 * cos(t * 2)) * dim.x(), (0.5 + 0.3 * sin(t * 3)) * dim.y()) * affine - Geom::Point(get_dimensions()) * 0.5);
    }
}

void CanvasPrivate::add_idle()
{
    framecheck_whole_function(this)

    if (!active) {
        // We can safely discard events until active, because we will run add_idle on activation later in initialisation.
        return;
    }

    if (prefs.debug_idle_starvation && !idle_running) {
        auto time = g_get_monotonic_time();
        if (sample_begin == 0) {
            sample_begin = time;
            wait_accumulated = 0;
        }
        wait_begin = time;
    }

    if (!hipri_idle.connected()) {
        hipri_idle = Glib::signal_idle().connect(sigc::mem_fun(this, &CanvasPrivate::on_hipri_idle), G_PRIORITY_HIGH_IDLE + 15); // after resize, before draw
    }

    if (!lopri_idle.connected()) {
        lopri_idle = Glib::signal_idle().connect(sigc::mem_fun(this, &CanvasPrivate::on_lopri_idle), G_PRIORITY_DEFAULT_IDLE);
    }

    idle_running = true;
}

// Replace a region with a larger region consisting of fewer, larger rectangles. (Allowed to slightly overlap.)
auto coarsen(const Cairo::RefPtr<Cairo::Region> &region, int min_size, int glue_size, double min_fullness)
{
    // Sort the rects by minExtent.
    struct Compare
    {
        bool operator()(const Geom::IntRect &a, const Geom::IntRect &b) const {
            return a.minExtent() < b.minExtent();
        }
    };
    std::multiset<Geom::IntRect, Compare> rects;
    int nrects = region->get_num_rectangles();
    for (int i = 0; i < nrects; i++) {
        rects.emplace(cairo_to_geom(region->get_rectangle(i)));
    }

    // List of processed rectangles.
    std::vector<Geom::IntRect> processed;
    processed.reserve(nrects);

    // Removal lists.
    std::vector<decltype(rects)::iterator> remove_rects;
    std::vector<int> remove_processed;

    // Repeatedly expand small rectangles by absorbing their nearby small rectangles.
    while (!rects.empty() && rects.begin()->minExtent() < min_size) {
        // Extract the smallest unprocessed rectangle.
        auto rect = *rects.begin();
        rects.erase(rects.begin());

        // Initialise the effective glue size.
        int effective_glue_size = glue_size;

        while (true) {
            // Find the glue zone.
            auto glue_zone = rect;
            glue_zone.expandBy(effective_glue_size);

            // Absorb rectangles in the glue zone. We could do better algorithmically speaking, but in real life it's already plenty fast.
            auto newrect = rect;
            int absorbed_area = 0;

            remove_rects.clear();
            for (auto it = rects.begin(); it != rects.end(); ++it) {
                if (glue_zone.contains(*it)) {
                    newrect.unionWith(*it);
                    absorbed_area += it->area();
                    remove_rects.emplace_back(it);
                }
            }

            remove_processed.clear();
            for (int i = 0; i < processed.size(); i++) {
                auto &r = processed[i];
                if (glue_zone.contains(r)) {
                    newrect.unionWith(r);
                    absorbed_area += r.area();
                    remove_processed.emplace_back(i);
                }
            }

            // If the result was too empty, try again with a smaller glue size.
            double fullness = (double)(rect.area() + absorbed_area) / newrect.area();
            if (fullness < min_fullness) {
                effective_glue_size /= 2;
                continue;
            }

            // Commit the change.
            rect = newrect;

            for (auto &it : remove_rects) {
                rects.erase(it);
            }

            for (int j = (int)remove_processed.size() - 1; j >= 0; j--) {
                int i = remove_processed[j];
                processed[i] = processed.back();
                processed.pop_back();
            }

            // Stop growing if not changed or now big enough.
            bool finished = absorbed_area == 0 || rect.minExtent() >= min_size;
            if (finished) {
                break;
            }

            // Otherwise, continue normally.
            effective_glue_size = glue_size;
        }

        // Put the finished rectangle in processed.
        processed.emplace_back(rect);
    }

    // Put any remaining rectangles in processed.
    for (auto &rect : rects) {
        processed.emplace_back(rect);
    }

    return processed;
}

std::optional<Geom::Dim2> CanvasPrivate::old_bisector(const Geom::IntRect &rect)
{
    int bw = rect.width();
    int bh = rect.height();

    /*
     * Determine redraw strategy:
     *
     * bw < bh (strips mode): Draw horizontal strips starting from cursor position.
     *                        Seems to be faster for drawing many smaller objects zoomed out.
     *
     * bw > hb (chunks mode): Splits across the larger dimension of the rectangle, painting
     *                        in almost square chunks (from the cursor.
     *                        Seems to be faster for drawing a few blurred objects across the entire screen.
     *                        Seems to be somewhat psychologically faster.
     *
     * Default is for strips mode.
     */

    int max_pixels;
    if (q->_render_mode != Inkscape::RenderMode::OUTLINE) {
        // Can't be too small or large gradient will be rerendered too many times!
        max_pixels = 65536 * prefs.tile_multiplier;
    } else {
        // Paths only. 1M is catched buffer and we need four channels.
        max_pixels = 262144;
    }

    if (bw * bh > max_pixels) {
        if (bw < bh || bh < 2 * prefs.tile_size) {
            return Geom::X;
        } else {
            return Geom::Y;
        }
    }

    return {};
}

std::optional<Geom::Dim2> CanvasPrivate::new_bisector(const Geom::IntRect &rect)
{
    int bw = rect.width();
    int bh = rect.height();

    // Chop in half along the bigger dimension if the bigger dimension is too big.
    if (bw > bh) {
        if (bw > prefs.new_bisector_size) {
            return Geom::X;
        }
    } else {
        if (bh > prefs.new_bisector_size) {
            return Geom::Y;
        }
    }

    return {};
}

bool CanvasPrivate::on_hipri_idle()
{
    on_lopri_idle();
    return false;
}

bool CanvasPrivate::on_lopri_idle()
{
    assert(active);
    if (idle_running) {
        if (prefs.debug_idle_starvation) wait_accumulated += g_get_monotonic_time() - wait_begin;
        idle_running = on_idle();
        if (prefs.debug_idle_starvation && idle_running) wait_begin = g_get_monotonic_time();
    }
    return idle_running;
}

bool CanvasPrivate::on_idle()
{
    framecheck_whole_function(this)

    assert(active); // Guaranteed since already checked by both callers.
    assert(q->_canvas_item_root);

    // Quit idle process if not supposed to be drawing.
    if (q->_drawing_disabled) {
        return false;
    }

    // Because GTK keeps making it not current.
    if (q->get_opengl_enabled()) q->make_current();

    auto setup_pipeline = [this] {
        auto gl = glstate();

        if (gl->state == GLState::State::OnIdle) return;
        gl->state = GLState::State::OnIdle;

        glDisable(GL_BLEND);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl->fbo);
        constexpr GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(need_outline_store() ? 2 : 1, attachments);

        const auto &shader = need_outline_store() ? gl->texcopydouble : gl->texcopy;
        glUseProgram(shader.id);
        gl->mat_loc = shader.loc("mat");
        gl->trans_loc = shader.loc("trans");
        gl->tex_loc = shader.loc("tex");
        if (need_outline_store()) gl->texoutline_loc = shader.loc("tex_outline");
    };

    auto recreate_store = [&, this] {
        // Recreate the store fragment at the current affine so that it covers the visible region + prerender margin.
        auto store = graphics->get_store();
        store->rect = expandedBy(q->get_area_world(), prefs.margin + prefs.pad);
        store->affine = q->_affine;
        auto content_size = store->rect.dimensions() * device_scale;

        if (q->get_opengl_enabled()) {
            auto gl = glstate();

            // Setup the base pipeline.
            setup_pipeline();

            // Recreate the store textures.
            if                          (!gl->store.texture         || gl->store.texture.get_size()         != content_size)  gl->store.texture         = Texture(content_size);
            if (need_outline_store() && (!gl->store.outline_texture || gl->store.outline_texture.get_size() != content_size)) gl->store.outline_texture = Texture(content_size);

            // Bind the store to the framebuffer for writing to.
                                      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->store.texture.get_id(),         0);
            if (need_outline_store()) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl->store.outline_texture.get_id(), 0);
            glViewport(0, 0, gl->store.texture.get_size().x(), gl->store.texture.get_size().y());

            // Clear the store to transparent.
            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            auto cs = crstate();

            // Recreate the store surface.
            if (!cs->store.surface || dimensions(cs->store.surface) != content_size) {
                cs->store.surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, content_size.x(), content_size.y());
                cairo_surface_set_device_scale(cs->store.surface->cobj(), device_scale, device_scale); // No C++ API!
            }

            // Clear it to the correct contents depending on the solid colour optimisation.
            auto cr = Cairo::Context::create(cs->store.surface);
            if (cs->solid_colour) {
                paint_background(cs->store, cr);
            } else {
                cr->set_operator(Cairo::OPERATOR_CLEAR);
                cr->paint();
            }

            // Do the same for the outline store (except always clearing it to transparent).
            if (need_outline_store()) {
                if (!cs->store.outline_surface || dimensions(cs->store.outline_surface) != content_size) {
                    cs->store.outline_surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, content_size.x(), content_size.y());
                    cairo_surface_set_device_scale(cs->store.surface->cobj(), device_scale, device_scale); // No C++ API!
                }
                auto cr = Cairo::Context::create(cs->store.outline_surface);
                cr->set_operator(Cairo::OPERATOR_CLEAR);
                cr->paint();
            }
        }

        // Set everything as needing redraw.
        updater->reset();

        if (prefs.debug_show_unclean) q->queue_draw();
    };

    // Determine whether the rendering parameters have changed, and reset if so.
    if (!graphics->get_store()->has_content() || (need_outline_store() && !graphics->get_store()->has_outline_content()) || device_scale != q->get_scale_factor()) {
        device_scale = q->get_scale_factor();
        recreate_store();
        decoupled_mode = false;
        if (prefs.debug_logging) std::cout << "Full reset" << std::endl;
    }

    // Make sure to clear the outline content of the store when not in use, so we don't accidentally re-use it when it is required again.
    if (!need_outline_store()) {
        graphics->get_store()->clear_outline_content();
    }

    auto shift_store = [&, this] {
        // Create a new fragment centred on the viewport.
        auto rect = expandedBy(q->get_area_world(), prefs.margin + prefs.pad);
        auto content_size = rect.dimensions() * device_scale;

        if (q->get_opengl_enabled()) {
            auto gl = glstate();

            // Setup the base pipeline.
            setup_pipeline();

            // Create the new fragment.
            GLFragment fragment;
            fragment.rect = rect;
            fragment.affine = q->_affine;
                                      fragment.texture         = Texture(content_size);
            if (need_outline_store()) fragment.outline_texture = Texture(content_size);

            // Bind new store to the framebuffer to writing to.
                                      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fragment.texture.get_id(),         0);
            if (need_outline_store()) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fragment.outline_texture.get_id(), 0);
            glViewport(0, 0, fragment.texture.get_size().x(), fragment.texture.get_size().y());

            // Clear new store to transparent.
            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);

            // Bind the old store to texture units 0 and 1 for reading from.
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gl->store.texture.get_id());
            glUniform1i(gl->tex_loc, 0);
            if (need_outline_store()) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, gl->store.outline_texture.get_id());
                glUniform1i(gl->texoutline_loc, 1);
            }
            glBindVertexArray(gl->rect.vao);

            // Copy re-usuable contents of the old store into the new store.
            geom_to_uniform(GLState::calc_paste_transform(gl->store, fragment), gl->mat_loc, gl->trans_loc);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

            // Set the result as the new store.
            gl->store = std::move(fragment);
        } else {
            auto cs = crstate();

            auto make_surface = [&] {
                auto result = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, content_size.x(), content_size.y());
                cairo_surface_set_device_scale(result->cobj(), device_scale, device_scale); // No C++ API!
                return result;
            };

            // Create the new fragment.
            CairoFragment fragment;
            fragment.rect = rect;
            fragment.affine = q->_affine;
                                      fragment.surface =         make_surface();
            if (need_outline_store()) fragment.outline_surface = make_surface();

            // Determine the geometry of the shift.
            auto shift = fragment.rect.min() - cs->store.rect.min();
            auto reuse_rect = regularised(fragment.rect & cs->store.rect);
            assert(reuse_rect); // Should not be called if there is no overlap.

            // Paint background into region of store not covered by next operation.
            auto cr = Cairo::Context::create(fragment.surface);
            if (cs->solid_colour) {
                auto reg = Cairo::Region::create(geom_to_cairo(fragment.rect));
                reg->subtract(geom_to_cairo(*reuse_rect));
                reg->translate(-fragment.rect.left(), -fragment.rect.top());
                cr->save();
                region_to_path(cr, reg);
                cr->clip();
                paint_background(fragment, cr);
                cr->restore();
            }

            // Copy re-usuable contents of old store into new store, shifted.
            cr->rectangle(reuse_rect->left() - fragment.rect.left(), reuse_rect->top() - fragment.rect.top(), reuse_rect->width(), reuse_rect->height());
            cr->clip();
            cr->set_source(cs->store.surface, -shift.x(), -shift.y());
            cr->set_operator(Cairo::OPERATOR_SOURCE);
            cr->paint();

            // Do the same for the outline store
            if (need_outline_store()) {
                // Copy the content.
                auto cr = Cairo::Context::create(fragment.outline_surface);
                cr->rectangle(reuse_rect->left() - fragment.rect.left(), reuse_rect->top() - fragment.rect.top(), reuse_rect->width(), reuse_rect->height());
                cr->clip();
                cr->set_source(cs->store.outline_surface, -shift.x(), -shift.y());
                cr->set_operator(Cairo::OPERATOR_SOURCE);
                cr->paint();
            }

            // Set the result as the new backing store.
            cs->store = std::move(fragment);
        }

        auto store = graphics->get_store();
        assert(store->affine == q->_affine); // Should not be called if the affine has changed.

        // Mark everything as needing redraw.
        updater->intersect(store->rect);

        if (prefs.debug_show_unclean) q->queue_draw();
    };

    // Acquire some pointers for convenience.
    auto store    = graphics->get_store();
    auto snapshot = graphics->get_snapshot();

    auto take_snapshot = [&, this] {
        // Preserve the clean region for use in a moment.
        auto old_cleanregion = std::move(updater->clean_region);
        auto old_store_affine = store->affine;
        // Copy the backing store to the snapshot, leaving us temporarily in an invalid state.
        graphics->swap_stores();
        // Recreate the backing store, making the state valid again.
        recreate_store();
        // Transform the clean region into the new store.
        snapshot_cleanregion = shrink_region(region_affine_approxinwards(old_cleanregion, old_store_affine.inverse() * store->affine, store->rect), 4, -2);
        processed_edge = false;
    };

    auto snapshot_combine = [&, this] {
        // Get the list of corner points in the snapshot store and the clean region, all at the transformation q->_affine relevant for screen space.
        std::vector<Geom::Point> pts;
        auto add_rect = [&, this] (const Geom::IntRect &rect, const Geom::Affine &affine) {
            for (int i = 0; i < 4; i++) {
                pts.emplace_back(Geom::Point(rect.corner(i)) * affine.inverse() * q->_affine);
            }
        };
        add_rect(snapshot->rect, snapshot->affine);
        for (int i = 0; i < updater->clean_region->get_num_rectangles(); i++) {
            add_rect(cairo_to_geom(updater->clean_region->get_rectangle(i)), store->affine);
        }

        // Compute their minimum-area bounding box as a fragment - an (affine, rect) pair.
        auto [affine, rect] = min_bounding_box(pts);
        affine = q->_affine * affine;

        // Check if the paste transform from the snapshot store to the new fragment would be approximately a dihedral transformation.
        auto paste = Geom::Scale(snapshot->rect.dimensions()) * Geom::Translate(snapshot->rect.min()) * snapshot->affine.inverse() * affine * Geom::Translate(-rect.min()) * Geom::Scale(rect.dimensions()).inverse();
        if (approx_dihedral(paste)) {
            // If so, simply take the new fragment to be exactly the same as the snapshot store.
            rect   = snapshot->rect;
            affine = snapshot->affine;
        }

        // Compute the scale difference between the backing store and the new fragment, giving the amount of detail that would be lost by pasting.
        if ( double scale_ratio = std::sqrt(std::abs(store->affine.det() / affine.det()));
                    scale_ratio > 4.0 ) {
            // Zoom the new fragment in to increase its quality.
            double grow = scale_ratio / 2.0;
            rect   *= Geom::Scale(grow);
            affine *= Geom::Scale(grow);
        }

        // Do not allow the fragment to become more detailed than the window.
        if ( double scale_ratio = std::sqrt(std::abs(affine.det() / q->_affine.det()));
                    scale_ratio > 1.0 ) {
            // Zoom the new fragment out to reduce its quality.
            double shrink = 1.0 / scale_ratio;
            rect   *= Geom::Scale(shrink);
            affine *= Geom::Scale(shrink);
        }

        // Find the bounding rect of the visible region + prerender margin within the new fragment. We do not want to discard this content in the next clipping step.
        auto renderable = (Geom::Parallelogram(expandedBy(q->get_area_world(), prefs.margin)) * q->_affine.inverse() * affine).bounds() & rect;

        // Cap the dimensions of the new fragment to slightly larger than the maximum dimension of the window by clipping it towards the screen centre. (Lower in Cairo mode since otherwise too slow to cope.)
        double max_dimension = std::max(q->get_allocation().get_width(), q->get_allocation().get_height()) * (q->get_opengl_enabled() ? 1.7 : 0.8);
        auto dimens = rect.dimensions();
        dimens.x() = std::min(dimens.x(), max_dimension);
        dimens.y() = std::min(dimens.y(), max_dimension);
        auto center = Geom::Rect(q->get_area_world()).midpoint() * q->_affine.inverse() * affine;
        center.x() = safeclamp(center.x(), rect.left() + dimens.x() * 0.5, rect.right()  - dimens.x() * 0.5);
        center.y() = safeclamp(center.y(), rect.top()  + dimens.y() * 0.5, rect.bottom() - dimens.y() * 0.5);
        rect = Geom::Rect(center - dimens * 0.5, center + dimens * 0.5);

        // Ensure the new fragment contains the renderable rect from earlier, enlarging it and reducing resolution if necessary.
        if (!rect.contains(renderable)) {
            auto oldrect = rect;
            rect.unionWith(renderable);
            double shrink = 1.0 / std::max(rect.width() / oldrect.width(), rect.height() / oldrect.height());
            rect   *= Geom::Scale(shrink);
            affine *= Geom::Scale(shrink);
        }

        // Calculate the paste transform from the snapshot store to the new fragment (again).
        paste = Geom::Scale(snapshot->rect.dimensions()) * Geom::Translate(snapshot->rect.min()) * snapshot->affine.inverse() * affine * Geom::Translate(-rect.min()) * Geom::Scale(rect.dimensions()).inverse();

        if (prefs.debug_logging) std::cout << "New fragment dimensions " << rect.width() << ' ' << rect.height() << std::endl;

        if (paste.isIdentity(0.001) && rect.dimensions().round() == snapshot->rect.dimensions()) {
            // Fast path: simply paste the backing store onto the snapshot store.
            if (prefs.debug_logging) std::cout << "Fast snapshot combine" << std::endl;

            if (q->get_opengl_enabled()) {
                auto gl = glstate();

                // Ensure the base pipeline is correctly set up.
                setup_pipeline();

                // Compute the vertex data for the clean region.
                auto [clean_vao, clean_numrects] = GLState::clean_region_shrink_vao(updater->clean_region, store->rect);

                // Bind the snapshot to the framebuffer for writing to.
                                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->snapshot.texture.get_id(),         0);
                if (need_outline_store()) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl->snapshot.outline_texture.get_id(), 0);
                glViewport(0, 0, gl->snapshot.texture.get_size().x(), gl->snapshot.texture.get_size().y());

                // Bind the store to texture unit 0 (and its outline to 1, if necessary).
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gl->store.texture.get_id());
                glUniform1i(gl->tex_loc, 0);
                if (need_outline_store()) {
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, gl->store.outline_texture.get_id());
                    glUniform1i(gl->texoutline_loc, 1);
                }

                // Copy the clean region of the store to the snapshot.
                geom_to_uniform(GLState::calc_paste_transform(*store, *snapshot), gl->mat_loc, gl->trans_loc);
                glBindVertexArray(clean_vao.vao);
                glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);
            } else {
                auto cs = crstate();

                auto copy = [&, this] (const Cairo::RefPtr<Cairo::ImageSurface> &from,
                                       const Cairo::RefPtr<Cairo::ImageSurface> &to) {
                    auto cr = Cairo::Context::create(to);
                    cr->set_antialias(Cairo::ANTIALIAS_NONE);
                    cr->set_operator(Cairo::OPERATOR_SOURCE);
                    cr->translate(-snapshot->rect.left(), -snapshot->rect.top());
                    cr->transform(geom_to_cairo(snapshot->affine * store->affine.inverse()));
                    cr->translate(-1.0, -1.0);
                    region_to_path(cr, shrink_region(updater->clean_region, 2));
                    cr->translate(1.0, 1.0);
                    cr->clip();
                    cr->set_source(from, store->rect.left(), store->rect.top());
                    Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::FILTER_FAST);
                    cr->paint();
                };

                                          copy(cs->store.surface,         cs->snapshot.surface);
                if (need_outline_store()) copy(cs->store.outline_surface, cs->snapshot.outline_surface);
            }
        } else {
            // General path: paste the snapshot store and then the backing store onto a new fragment, then set that as the snapshot store.

            // Create the new fragment.
            auto frag_rect = rect.roundOutwards();
            auto content_size = frag_rect.dimensions() * device_scale;

            if (q->get_opengl_enabled()) {
                auto gl = glstate();

                // Ensure the base pipeline is correctly set up.
                setup_pipeline();

                // Compute the vertex data for the clean region.
                auto [clean_vao, clean_numrects] = GLState::clean_region_shrink_vao(updater->clean_region, store->rect);

                GLFragment fragment;
                fragment.rect = frag_rect;
                fragment.affine = affine;
                                          fragment.texture         = Texture(content_size);
                if (need_outline_store()) fragment.outline_texture = Texture(content_size);

                // Bind the new fragment to the framebuffer for writing to.
                                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fragment.texture.get_id(),         0);
                if (need_outline_store()) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fragment.outline_texture.get_id(), 0);

                // Clear the new fragment to transparent.
                glViewport(0, 0, fragment.texture.get_size().x(), fragment.texture.get_size().y());
                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT);

                // Bind the store and snapshot to texture units 0 and 1 (and their outlines to 2 and 3, if necessary).
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gl->snapshot.texture.get_id());
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, gl->store.texture.get_id());
                if (need_outline_store()) {
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, gl->snapshot.outline_texture.get_id());
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, gl->store.outline_texture.get_id());
                }

                // Paste the snapshot store onto the new fragment.
                glUniform1i(gl->tex_loc, 0);
                if (need_outline_store()) glUniform1i(gl->texoutline_loc, 2);
                geom_to_uniform(GLState::calc_paste_transform(*snapshot, fragment), gl->mat_loc, gl->trans_loc);
                glBindVertexArray(gl->rect.vao);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

                // Paste the backing store onto the new fragment.
                glUniform1i(gl->tex_loc, 1);
                if (need_outline_store()) glUniform1i(gl->texoutline_loc, 3);
                geom_to_uniform(GLState::calc_paste_transform(*store, fragment), gl->mat_loc, gl->trans_loc);
                glBindVertexArray(clean_vao.vao);
                glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);

                // Set the result as the new snapshot.
                gl->snapshot = std::move(fragment);
            } else {
                auto cs = crstate();

                auto make_surface = [&] {
                    auto result = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, content_size.x(), content_size.y());
                    cairo_surface_set_device_scale(result->cobj(), device_scale, device_scale); // No C++ API!
                    return result;
                };

                CairoFragment fragment;
                fragment.rect = frag_rect;
                fragment.affine = affine;
                                          fragment.surface         = make_surface();
                if (need_outline_store()) fragment.outline_surface = make_surface();

                auto copy = [&, this] (const Cairo::RefPtr<Cairo::ImageSurface> &store_from,
                                       const Cairo::RefPtr<Cairo::ImageSurface> &snapshot_from,
                                       const Cairo::RefPtr<Cairo::ImageSurface> &to, bool background) {
                    auto cr = Cairo::Context::create(to);
                    cr->set_antialias(Cairo::ANTIALIAS_NONE);
                    cr->set_operator(Cairo::OPERATOR_SOURCE);
                    if (background) paint_background(fragment, cr);
                    cr->translate(-fragment.rect.left(), -fragment.rect.top());
                    cr->transform(geom_to_cairo(fragment.affine * snapshot->affine.inverse()));
                    cr->rectangle(snapshot->rect.left(), snapshot->rect.top(), snapshot->rect.width(), snapshot->rect.height());
                    cr->set_source(snapshot_from, snapshot->rect.left(), snapshot->rect.top());
                    Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::FILTER_FAST);
                    cr->fill();
                    cr->transform(geom_to_cairo(snapshot->affine * store->affine.inverse()));
                    cr->translate(-1.0, -1.0);
                    region_to_path(cr, shrink_region(updater->clean_region, 2));
                    cr->translate(1.0, 1.0);
                    cr->clip();
                    cr->set_source(store_from, store->rect.left(), store->rect.top());
                    Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::FILTER_FAST);
                    cr->paint();
                };

                                          copy(cs->store.surface,         cs->snapshot.surface,         fragment.surface,         cs->solid_colour);
                if (need_outline_store()) copy(cs->store.outline_surface, cs->snapshot.outline_surface, fragment.outline_surface, false);

                cs->snapshot = std::move(fragment);
            }
        }
    };

    // Handle transitions and actions in response to viewport changes.
    if (!decoupled_mode) {
        // Enter decoupled mode if the affine has changed from what the backing store was drawn at.
        if (q->_affine != store->affine) {
            // Enter decoupled mode.
            if (prefs.debug_logging) std::cout << "Entering decoupled mode" << std::endl;
            decoupled_mode = true;

            // Snapshot and reset the store.
            take_snapshot();

            // Note: If redrawing is fast enough to finish during the frame, then going into decoupled mode, drawing, and leaving
            // it again performs exactly the same rendering operations as if we had not gone into it at all. Also, no extra copies
            // or blits are performed, and the drawing operations done on the screen are the same. Hence this feature comes at zero cost.
        } else {
            // Determine whether the view has moved sufficiently far that we need to shift the store.
            if (!store->rect.contains(expandedBy(q->get_area_world(), prefs.margin))) {
                // The visible region + prerender margin has reached the edge of the store.
                if (!regularised(store->rect & expandedBy(q->get_area_world(), prefs.margin + prefs.pad))) {
                    // If the store contains no reusable content at all, recreate it.
                    recreate_store();
                    if (prefs.debug_logging) std::cout << "Recreated store" << std::endl;
                } else {
                    // Otherwise shift it.
                    shift_store();
                    if (prefs.debug_logging) std::cout << "Shifted store" << std::endl;
                }
            }
            // After these operations, the store should now contain the visible region + prerender margin.
            assert(store->rect.contains(expandedBy(q->get_area_world(), prefs.margin)));
        }
    } else { // if (decoupled_mode)
        // Completely cancel the previous redraw and start again if the viewing parameters have changed too much.
        auto check_restart_redraw = [&, this] {
            // With this debug feature on, redraws should never be restarted.
            if (prefs.debug_sticky_decoupled) return false;

            // Restart if the store is no longer covering the middle 50% of the screen. (Usually triggered by rotating or zooming out.)
            auto pl = Geom::Parallelogram(q->get_area_world());
            pl *= Geom::Translate(-pl.midpoint()) * Geom::Scale(0.5) * Geom::Translate(pl.midpoint());
            pl *= q->_affine.inverse() * store->affine;
            if (!Geom::Parallelogram(store->rect).contains(pl)) {
                if (prefs.debug_logging) std::cout << "Restarting redraw (store not fully covering screen)" << std::endl;
                return true;
            }

            // Also restart if zoomed in or out too much.
            auto scale_ratio = std::abs(q->_affine.det() / store->affine.det());
            if (scale_ratio > 3.0 || scale_ratio < 0.7) {
                // Todo: Un-hard-code these thresholds.
                //  * The threshold 3.0 is for zooming in. It says that if the quality of what is being redrawn is more than 3x worse than that of the screen, restart. This is necessary to ensure acceptably high resolution is kept as you zoom in.
                //  * The threshold 0.7 is for zooming out. It says that if the quality of what is being redrawn is too high compared to the screen, restart. This prevents wasting time redrawing the screen slowly, at too high a quality that will probably not ever be seen.
                if (prefs.debug_logging) std::cout << "Restarting redraw (zoomed changed too much)" << std::endl;
                return true;
            }

            // Don't restart.
            return false;
        };

        if (check_restart_redraw()) {
            // Add the clean region to the snapshot clean region (they both exist in store space, so this is valid), and save its affine.
            snapshot_cleanregion->do_union(updater->clean_region);
            auto old_store_affine = store->affine;
            // Re-use as much content as possible from the store and the snapshot, and set as the new snapshot.
            snapshot_combine();
            // Start drawing again on a new blank store aligned to the screen.
            recreate_store();
            // Transform the snapshot clean region to the new store. (Note: Strictly, should also clip to inside the new snapshot rect, but it works well enough without this.)
            snapshot_cleanregion = shrink_region(region_affine_approxinwards(snapshot_cleanregion, old_store_affine.inverse() * store->affine, store->rect), 4, -2);
            processed_edge = false;
        }
    }

    // Assert that the clean region is a subregion of the store.
    #ifndef NDEBUG
    auto tmp = updater->clean_region->copy();
    tmp->subtract(geom_to_cairo(store->rect));
    assert(tmp->empty());
    #endif

    // Ensure the geometry is up-to-date and in the right place.
    auto affine = decoupled_mode ? store->affine : q->_affine;
    if (q->_need_update || geom_affine != affine) {
        q->_canvas_item_root->update(affine);
        geom_affine = affine;
        q->_need_update = false;
    }

    // If asked to, don't paint anything and instead halt the idle process.
    if (prefs.debug_disable_redraw) {
        return false;
    }

    // Get the mouse position in screen space.
    Geom::IntPoint mouse_loc = (last_mouse ? *last_mouse : Geom::Point(q->get_dimensions()) / 2).round();

    // Map the mouse to canvas space.
    mouse_loc += q->_pos;
    if (decoupled_mode) {
        mouse_loc = (Geom::Point(mouse_loc) * store->affine * q->_affine.inverse()).round();
    }

    // Get the visible rect.
    Geom::IntRect visible = q->get_area_world();
    if (decoupled_mode) {
        visible = (Geom::Parallelogram(visible) * q->_affine.inverse() * store->affine).bounds().roundOutwards();
    }

    // Begin processing redraws.
    auto start_time = g_get_monotonic_time();

    // Paint a given subrectangle of the store given by 'bounds', but avoid painting the part of it within 'clean' if possible.
    // Some parts both outside the bounds and inside the clean region may also be painted if it helps reduce fragmentation.
    // Returns true to indicate timeout.
    auto process_redraw = [&, this] (const Geom::IntRect &bounds, const Cairo::RefPtr<Cairo::Region> &clean) {
        // Assert that we do not render outside of store.
        assert(store->rect.contains(bounds));

        // Get the region we are asked to paint.
        auto region = Cairo::Region::create(geom_to_cairo(bounds));
        region->subtract(clean);

        // Get the list of rectangles to paint, coarsened to avoid fragmentation.
        auto rects = coarsen(region,
                             std::min<int>(prefs.coarsener_min_size, prefs.new_bisector_size / 2),
                             std::min<int>(prefs.coarsener_glue_size, prefs.new_bisector_size / 2),
                             prefs.coarsener_min_fullness);

        // Put the rectangles into a heap sorted by distance from mouse.
        auto cmp = [&] (const Geom::IntRect &a, const Geom::IntRect &b) {
            return distSq(mouse_loc, a) > distSq(mouse_loc, b);
        };
        std::make_heap(rects.begin(), rects.end(), cmp);

        // Process rectangles until none left or timed out.
        while (!rects.empty()) {
            // Extract the closest rectangle to the mouse.
            std::pop_heap(rects.begin(), rects.end(), cmp);
            auto rect = rects.back();
            rects.pop_back();

            // Cull empty rectangles.
            if (rect.width() == 0 || rect.height() == 0) {
                continue;
            }

            // Cull rectangles that lie entirely inside the clean region.
            // (These can be generated by coarsening; they must be discarded to avoid getting stuck re-rendering the same rectangles.)
            if (clean->contains_rectangle(geom_to_cairo(rect)) == Cairo::REGION_OVERLAP_IN) {
                continue;
            }

            // Lambda to add a rectangle to the heap.
            auto add_rect = [&] (const Geom::IntRect &rect) {
                rects.emplace_back(rect);
                std::push_heap(rects.begin(), rects.end(), cmp);
            };

            // If the rectangle needs bisecting, bisect it and put it back on the heap.
            auto axis = prefs.use_new_bisector ? new_bisector(rect) : old_bisector(rect);
            if (axis) {
                int mid = rect[*axis].middle();
                auto lo = rect; lo[*axis].setMax(mid); add_rect(lo);
                auto hi = rect; hi[*axis].setMin(mid); add_rect(hi);
                continue;
            }

            // Extend thin rectangles at the edge of the bounds rect to at least some minimum size, being sure to keep them within the store.
            // (This ensures we don't end up rendering one thin rectangle at the edge every frame while the view is moved continuously.)
            if (rect.width() < prefs.preempt) {
                if (rect.left()  == bounds.left() ) rect.setLeft (std::max(rect.right() - prefs.preempt, store->rect.left() ));
                if (rect.right() == bounds.right()) rect.setRight(std::min(rect.left()  + prefs.preempt, store->rect.right()));
            }
            if (rect.height() < prefs.preempt) {
                if (rect.top()    == bounds.top()   ) rect.setTop   (std::max(rect.bottom() - prefs.preempt, store->rect.top()   ));
                if (rect.bottom() == bounds.bottom()) rect.setBottom(std::min(rect.top()    + prefs.preempt, store->rect.bottom()));
            }

            // Paint the rectangle.
            paint_rect(rect);

            // Introduce an artificial delay for each rectangle.
            if (prefs.debug_slow_redraw) g_usleep(prefs.debug_slow_redraw_time);

            // Mark the rectangle as clean.
            updater->mark_clean(rect);

            // Get the rectangle of screen-space needing repaint.
            Geom::IntRect repaint_rect;
            if (!decoupled_mode) {
                // Simply translate to get back to screen space.
                repaint_rect = rect - q->_pos;
            } else {
                // Transform into screen space, take bounding box, and round outwards.
                auto pl = Geom::Parallelogram(rect);
                pl *= q->_affine * store->affine.inverse();
                pl *= Geom::Translate(-q->_pos);
                repaint_rect = pl.bounds().roundOutwards();
            }

            // Check if repaint is necessary - some rectangles could be entirely off-screen.
            auto screen_rect = Geom::IntRect({}, q->get_dimensions());
            if (regularised(repaint_rect & screen_rect)) {
                // Schedule repaint.
                queue_draw_area(repaint_rect);
                disconnect_bucket_emptier_tick_callback();
                pending_draw = true;
            }

            // Check for timeout.
            auto now = g_get_monotonic_time();
            auto elapsed = now - start_time;
            if (elapsed > prefs.render_time_limit) {
                // Timed out. Temporarily return to GTK main loop, and come back here when next idle.
                if (prefs.debug_logging) std::cout << "Timed out: " << g_get_monotonic_time() - start_time << " us" << std::endl;
                framecheckobj.subtype = 1;
                return true;
            }
        }

        // No timeout.
        return false;
    };

    if (auto vis_store = regularised(visible & store->rect)) {
        // The highest priority to redraw is the region that is visible but not covered by either clean or snapshot content, if in decoupled mode.
        // If this is not rendered immediately, it will be perceived as edge flicker, most noticeably on zooming out, but also on rotation too.
        if (decoupled_mode) {
            if (process_redraw(*vis_store, unioned(updater->clean_region->copy(), snapshot_cleanregion))) return true;
        }

        // The main priority to redraw, and the bread and butter of Inkscape's painting, is the visible content that is not clean.
        // This may be done over several cycles, at the direction of the Updater, each outwards from the mouse.
        do {
            if (process_redraw(*vis_store, updater->get_next_clean_region())) return true;
        }
        while (updater->report_finished());
    }

    // The lowest priority to redraw is the prerender margin around the visible rectangle.
    // (This is in addition to any opportunistic prerendering that may have already occurred in the above steps.)
    auto prerender = expandedBy(visible, prefs.margin);
    auto prerender_store = regularised(prerender & store->rect);
    if (prerender_store) {
        if (process_redraw(*prerender_store, updater->clean_region)) return true;
    }

    // Finished drawing. Handle transitions out of decoupled mode, by checking if we need to do a final redraw at the correct affine.
    if (decoupled_mode) {
        if (prefs.debug_sticky_decoupled) {
            // Debug feature: quit idle process, but stay in decoupled mode.
            return false;
        } else if (store->affine == q->_affine) {
            // Content is rendered at the correct affine - exit decoupled mode and quit idle process.
            if (prefs.debug_logging) std::cout << "Finished drawing - exiting decoupled mode" << std::endl;
            // Exit decoupled mode.
            decoupled_mode = false;
            // Free no-longer-needed resources.
            graphics->get_snapshot()->clear_content();
            graphics->get_snapshot()->clear_outline_content();
            // Quit idle process.
            return false;
        } else {
            // Content is rendered at the wrong affine - take a new snapshot and continue idle process to continue rendering at the new affine.
            if (prefs.debug_logging) std::cout << "Scheduling final redraw" << std::endl;
            // Snapshot and reset the backing store.
            take_snapshot();
            // Continue idle process.
            return true;
        }
    } else {
        // All done, quit the idle process.
        framecheckobj.subtype = 3;
        return false;
    }
}

void CanvasPrivate::paint_rect(const Geom::IntRect &rect)
{
    // Make sure the paint rectangle lies within the store.
    auto store = graphics->get_store();
    assert(store->rect.contains(rect));

    if (q->get_opengl_enabled()) {
        auto gl = glstate();

        auto paint_to_texture = [&, this] {
            // Lease out a PixelStreamer mapping to draw on.
            auto surface = gl->pixelstreamer->request(rect.dimensions() * device_scale);
            cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);

            // Actually draw the content with Cairo.
            paint_single_buffer(surface, rect, false);

            // Convert the surface to a texture.
            return gl->pixelstreamer->finish(std::move(surface));
        };

        // Create and render the fragment.
        Fragment fragment;
        fragment.affine = store->affine;
        fragment.rect = rect;

        GLFragment glfragment;

        q->_drawing->setColorMode(q->_color_mode);
        glfragment.texture = paint_to_texture();

        if (need_outline_store()) {
            q->_drawing->setRenderMode(Inkscape::RenderMode::OUTLINE);
            glfragment.outline_texture = paint_to_texture();
            q->_drawing->setRenderMode(q->_render_mode); // Leave the drawing in the requested render mode.
        }

        // Set up the pipeline.
        if (gl->state != GLState::State::PaintRect) {
            gl->state = GLState::State::PaintRect;

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl->fbo);
            constexpr GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            glDrawBuffers(need_outline_store() ? 2 : 1, attachments);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->store.texture.get_id(), 0);
            if (need_outline_store()) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl->store.outline_texture.get_id(), 0);
            glViewport(0, 0, gl->store.texture.get_size().x(), gl->store.texture.get_size().y());

            const auto &shader = need_outline_store() ? gl->texcopydouble : gl->texcopy;
            glUseProgram(shader.id);
            gl->mat_loc = shader.loc("mat");
            gl->trans_loc = shader.loc("trans");
            glUniform1i(shader.loc("tex"), 0);
            if (need_outline_store()) glUniform1i(shader.loc("tex_outline"), 1);

            glBindVertexArray(gl->rect.vao);
            glDisable(GL_BLEND);
        }

        // Paste the texture onto the store.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glfragment.texture.get_id());
        if (need_outline_store()) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, glfragment.outline_texture.get_id());
        }
        geom_to_uniform(GLState::calc_paste_transform(fragment, *store), gl->mat_loc, gl->trans_loc);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    else
    {
        auto cs = crstate();

        auto paint_to_surface = [&, this] (const Cairo::RefPtr<Cairo::ImageSurface> &surface, bool normal_content) {
            // Create temporary surface that draws directly to store.
            surface->flush();
            unsigned char *data = surface->get_data();
            int stride = surface->get_stride();

            // Check we are using the correct device scale.
            double x_scale;
            double y_scale;
            cairo_surface_get_device_scale(surface->cobj(), &x_scale, &y_scale); // No C++ API!
            assert(device_scale == (int)x_scale);
            assert(device_scale == (int)y_scale);

            // Move to the correct row.
            data += stride * (rect.top() - store->rect.top()) * (int)y_scale;
            // Move to the correct column.
            data += 4 * (rect.left() - store->rect.left()) * (int)x_scale;
            auto imgs = Cairo::ImageSurface::create(data, Cairo::FORMAT_ARGB32,
                                                    rect.width()  * device_scale,
                                                    rect.height() * device_scale,
                                                    stride);

            cairo_surface_set_device_scale(imgs->cobj(), device_scale, device_scale); // No C++ API!

            paint_single_buffer(imgs, rect, normal_content);

            surface->mark_dirty();
        };

        q->_drawing->setColorMode(q->_color_mode);
        paint_to_surface(cs->store.surface, crstate()->solid_colour);

        if (need_outline_store()) {
            q->_drawing->setRenderMode(Inkscape::RenderMode::OUTLINE);
            paint_to_surface(cs->store.outline_surface, false);
            q->_drawing->setRenderMode(q->_render_mode); // Leave the drawing in the requested render mode.
        }
    }
}

void CanvasPrivate::paint_single_buffer(const Cairo::RefPtr<Cairo::ImageSurface> &surface, const Geom::IntRect &rect, bool need_background)
{
    // Create Cairo context.
    auto cr = Cairo::Context::create(surface);

    // Clear background.
    cr->save();
    if (need_background) {
        paint_background(Fragment{geom_affine, rect}, cr);
    } else {
        cr->set_operator(Cairo::OPERATOR_CLEAR);
        cr->paint();
    }
    cr->restore();

    // Render drawing on top of background.
    if (q->_canvas_item_root->is_visible()) {
        auto buf = Inkscape::CanvasItemBuffer{ rect, device_scale, cr };
        q->_canvas_item_root->render(&buf);
    }

    // Paint over newly drawn content with a translucent random colour.
    if (prefs.debug_show_redraw) {
        cr->set_source_rgba((rand() % 256) / 255.0, (rand() % 256) / 255.0, (rand() % 256) / 255.0, 0.2);
        cr->set_operator(Cairo::OPERATOR_OVER);
        cr->paint();
    }

    if (q->_cms_active) {
        auto transf = prefs.from_display
                    ? Inkscape::CMSSystem::getDisplayPer(q->_cms_key)
                    : Inkscape::CMSSystem::getDisplayTransform();
        if (transf) {
            surface->flush();
            auto px = surface->get_data();
            int stride = surface->get_stride();
            for (int i = 0; i < rect.height(); i++) {
                auto row = px + i * stride;
                Inkscape::CMSSystem::doTransform(transf, row, row, rect.width());
            }
            surface->mark_dirty();
        }
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
