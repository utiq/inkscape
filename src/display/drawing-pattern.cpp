// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Canvas belonging to SVG pattern.
 *//*
 * Authors:
 *   Tomasz Boczkowski <penginsbacon@gmail.com>
 *
 * Copyright (C) 2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cairomm/region.h>
#include "preferences.h"
#include "display/cairo-utils.h"
#include "display/drawing-context.h"
#include "display/drawing-pattern.h"
#include "display/drawing-surface.h"

namespace {

auto operator/(Geom::Point const &a, Geom::Point const &b)
{
    return Geom::Point(a.x() / b.x(), a.y() / b.y());
}

auto operator*(Geom::IntPoint const &a, Geom::IntPoint const &b)
{
    return Geom::IntPoint(a.x() * b.x(), a.y() * b.y());
}

auto geom_to_cairo(Geom::IntRect const &rect)
{
    return Cairo::RectangleInt{rect.left(), rect.top(), rect.width(), rect.height()};
}

auto cairo_to_geom(Cairo::RectangleInt const &rect)
{
    return Geom::IntRect::from_xywh(rect.x, rect.y, rect.width, rect.height);
}

int safemod(int a, int b)
{
    a %= b;
    return a < 0 ? a + b : a;
}

int rounddown(int a, int b)
{
    return a - safemod(a, b);
}

int roundup(int a, int b)
{
    return rounddown(a - 1, b) + b;
}

auto rounddown(Geom::IntPoint const &a, Geom::IntPoint const &b)
{
    return Geom::IntPoint(rounddown(a.x(), b.x()), rounddown(a.y(), b.y()));
}

} // namespace

namespace Inkscape {

DrawingPattern::Surface::Surface(Geom::IntRect const &rect, int device_scale)
    : rect(rect)
    , surface(Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, rect.width() * device_scale, rect.height() * device_scale))
{
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    #ifdef CAIRO_HAS_DITHER
        auto prefs = Inkscape::Preferences::get();
        if (prefs->getBool("/options/dithering/value", true)) {
            cairo_image_surface_set_dither(surface->cobj(), CAIRO_DITHER_BEST);
        }
    #endif
}

DrawingPattern::DrawingPattern(Drawing &drawing)
    : DrawingGroup(drawing)
    , _overflow_steps(1)
{
}

DrawingPattern::~DrawingPattern()
{
}

void DrawingPattern::setPatternToUserTransform(Geom::Affine const &new_trans)
{
    double constexpr EPS = 1e-18;

    Geom::Affine current;
    if (_pattern_to_user) {
        current = *_pattern_to_user;
    }

    if (!Geom::are_near(current, new_trans, EPS)) {
        // mark the area where the object was for redraw.
        _markForRendering();
        if (new_trans.isIdentity(EPS)) {
            _pattern_to_user.reset();
        } else {
            _pattern_to_user = std::make_unique<Geom::Affine>(new_trans);
        }
        _markForUpdate(STATE_ALL, true);
    }
}

void DrawingPattern::setTileRect(Geom::Rect const &tile_rect)
{
    _tile_rect = tile_rect;
    _markForUpdate(STATE_ALL, true);
}

void DrawingPattern::setOverflow(Geom::Affine const &initial_transform, int steps, Geom::Affine const &step_transform)
{
    _overflow_initial_transform = initial_transform;
    _overflow_steps = steps;
    _overflow_step_transform = step_transform;
}

cairo_pattern_t *DrawingPattern::renderPattern(Geom::IntRect const &area, float opacity, int device_scale)
{
    if (opacity < 1e-3) {
        // Invisible.
        return nullptr;
    }

    if (!_tile_rect || _tile_rect->hasZeroArea()) {
        // Empty.
        return nullptr;
    }

    // Calculate various transforms.
    auto const dt = Geom::Translate(-_tile_rect->min()) * Geom::Scale(_pattern_resolution / _tile_rect->dimensions()); // AKA user_to_tile.
    auto const idt = dt.inverse();
    auto const pattern_to_tile = _pattern_to_user ? _pattern_to_user->inverse() * dt : dt;
    auto const screen_to_tile = _ctm.inverse() * pattern_to_tile;

    // Calculate the requested area to draw within tile rasterisation space.
    auto area_tile = (Geom::Rect(area) * screen_to_tile).roundOutwards();
    auto const area_orig = area_tile;
    for (int i = 0; i < 2; i++) {
        if (area_tile.dimensions()[i] >= _pattern_resolution[i]) {
            area_tile[i] = {0, _pattern_resolution[i]};
        }
    }
    area_tile -= rounddown(area_tile.min(), _pattern_resolution);

    // Return whether the periodic tiling of a contains the periodic tiling of b.
    auto wrapped_contains = [&] (Geom::IntRect const &a, Geom::IntRect const &b) {
        auto check = [&] (int i) {
            int const period = _pattern_resolution[i];
            if (a[i].extent() >= period) return true;
            if (b[i].extent() > a[i].extent()) return false;
            return rounddown(b[i].min() - a[i].min(), period) >= b[i].max() - a[i].max();
        };
        return check(0) && check(1);
    };

    // Return whether the periodic tiling of a intersects with or touches the periodic tiling of b.
    auto wrapped_touches = [&] (Geom::IntRect const &a, Geom::IntRect const &b) {
        auto check = [&] (int i) {
            int const period = _pattern_resolution[i];
            if (a[i].extent() >= period) return true;
            if (b[i].extent() >= period) return true;
            return rounddown(b[i].max() - a[i].min(), period) >= b[i].min() - a[i].max();
        };
        return check(0) && check(1);
    };

    auto get_surface = [&, this] () -> std::pair<Surface*, Cairo::RefPtr<Cairo::Region>> {
        // If there is a rectangle containing the requested area, just use that.
        for (auto &s : surfaces) {
            if (wrapped_contains(s.rect, area_orig)) {
                return { &s, {} };
            }
        }

        // Otherwise, recursively merge the requested area with all overlapping or touching rectangles, and paint the missing part.
        std::vector<Surface> merged;
        auto expanded = area_orig;

        while (true) {
            bool modified = false;

            for (auto it = surfaces.begin(); it != surfaces.end(); ) {
                if (wrapped_touches(expanded, it->rect)) {
                    expanded.unionWith(it->rect + rounddown(expanded.max() - it->rect.min(), _pattern_resolution));
                    merged.emplace_back(std::move(*it));
                    *it = std::move(surfaces.back());
                    surfaces.pop_back();
                    modified = true;
                } else {
                    ++it;
                }
            }

            if (!modified) break;
        }

        // Canonicalise the expanded rectangle. (Stops Cairo's coordinates overflowing and the pattern disappearing.)
        for (int i = 0; i < 2; i++) {
            if (expanded.dimensions()[i] >= _pattern_resolution[i]) {
                expanded[i] = {0, _pattern_resolution[i]};
            } else {
                expanded[i] -= rounddown(expanded[i].min(), _pattern_resolution[i]);
            }
        }

        // Create a new surface covering the expanded rectangle.
        auto surface = Surface(expanded, device_scale);
        auto cr = Cairo::Context::create(surface.surface);
        cr->translate(-surface.rect.left(), -surface.rect.top());

        // Paste all the old surfaces into the new surface, tracking the remaining dirty region.
        auto dirty = Cairo::Region::create(geom_to_cairo(expanded));

        auto paint = [&, this] (Cairo::RefPtr<Cairo::ImageSurface> const &surface, Geom::IntRect const &rect) {
            dirty->subtract(geom_to_cairo(rect));
            cr->set_source(surface, rect.left(), rect.top());
            cr->paint();
        };

        for (auto &m : merged) {
            Geom::IntPoint smin, smax;
            for (int i = 0; i < 2; i++) {
                smin[i] = roundup(expanded[i].min() - m.rect[i].max() + 1, _pattern_resolution[i]);
                smax[i] = rounddown(expanded[i].max() - m.rect[i].min() - 1, _pattern_resolution[i]);
            }

            for (int x = smin.x(); x <= smax.x(); x += _pattern_resolution.x()) {
                for (int y = smin.y(); y <= smax.y(); y += _pattern_resolution.y()) {
                    paint(m.surface, m.rect - Geom::IntPoint(x, y));
                }
            }
        }

        // Emplace the surface, and return it along with the remaining dirty region.
        surfaces.emplace_back(std::move(surface));
        return std::make_pair(&surfaces.back(), std::move(dirty));
    };

    // Find an already-drawn surface containing the requested area, or create if it none exists.
    auto [surface, dirty] = get_surface();

    // Draw the pattern contents to the dirty areas of the surface, taking care of possible wrapping.
    Inkscape::DrawingContext dc(surface->surface->cobj(), surface->rect.min());

    auto paint = [&, this] (Geom::IntRect const &rect) {
        if (_overflow_steps == 1) {
            render(dc, rect);
        } else {
            // Overflow transforms need to be transformed to the old coordinate system
            // before stretching to the pattern resolution.
            auto const initial_transform = idt * _overflow_initial_transform * dt;
            auto const step_transform    = idt * _overflow_step_transform    * dt;
            dc.transform(initial_transform);
            for (int i = 0; i < _overflow_steps; i++) {
                // render() fails to handle transforms applied here when using cache.
                render(dc, rect, RENDER_BYPASS_CACHE);
                dc.transform(step_transform);
                // auto raw = pattern_surface.raw();
                // auto filename = "drawing-pattern" + std::to_string(i) + ".png";
                // cairo_surface_write_to_png(pattern_surface.raw(), filename.c_str());
            }
        }
    };

    if (dirty) {
        for (int i = 0; i < dirty->get_num_rectangles(); i++) {
            auto const rect = cairo_to_geom(dirty->get_rectangle(i));
            for (int x = 0; x <= 1; x++) {
                for (int y = 0; y <= 1; y++) {
                    auto const wrap = _pattern_resolution * Geom::IntPoint(x, y);
                    auto const rect2 = rect & Geom::IntRect(wrap, wrap + _pattern_resolution);
                    if (!rect2) continue;
                    auto save = DrawingContext::Save(dc);
                    // Clip to rectangle to be drawn.
                    dc.rectangle(*rect2);
                    dc.clip();
                    // Draw the pattern.
                    dc.translate(wrap);
                    paint(*rect2 - wrap);
                    // Apply opacity, if necessary.
                    if (opacity < 1.0 - 1e-3) {
                        dc.setOperator(CAIRO_OPERATOR_DEST_IN);
                        dc.setSource(0.0, 0.0, 0.0, opacity);
                        dc.paint();
                    }
                }
            }
        }
        dirty.clear();
    }

    // Debug: Show pattern tile.
    // surface->surface->write_to_png("/tmp/patternsurface.png");

    // Create and return pattern.
    auto cp = cairo_pattern_create_for_surface(surface->surface->cobj());
    auto const shift = surface->rect.min() + rounddown(area_orig.min() - surface->rect.min(), _pattern_resolution);
    ink_cairo_pattern_set_matrix(cp, pattern_to_tile * Geom::Translate(-shift));
    cairo_pattern_set_extend(cp, CAIRO_EXTEND_REPEAT);
    return cp;
}

unsigned DrawingPattern::_updateItem(Geom::IntRect const &area, UpdateContext const &ctx, unsigned flags, unsigned reset)
{
    _dropPatternCache();

    if (!_tile_rect || _tile_rect->hasZeroArea()) {
        return STATE_NONE;
    }

    // Calculate the desired resolution of a pattern tile.
    double const det_ctm = ctx.ctm.det();
    double const det_ps2user = _pattern_to_user ? _pattern_to_user->det() : 1.0;
    double scale = std::sqrt(std::abs(det_ctm * det_ps2user));
    // Fixme: When scale is too big (zooming in a pattern), Cairo doesn't render the pattern.
    // More precisely it fails when setting pattern matrix in DrawingPattern::renderPattern.
    // Correct solution should make use of visible area and change pattern tile rect accordingly.
    auto const c = _tile_rect->dimensions() * scale;
    _pattern_resolution = c.ceil();

    // Map tile rect to the origin and stretch it to the desired resolution.
    auto const dt = Geom::Translate(-_tile_rect->min()) * Geom::Scale(_pattern_resolution / _tile_rect->dimensions());

    // Apply this transform to the actual pattern tree.
    return DrawingGroup::_updateItem(Geom::IntRect::infinite(), { dt }, flags, reset);
}

void DrawingPattern::_dropPatternCache()
{
    surfaces.clear();
}

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
