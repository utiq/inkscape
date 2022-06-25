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

#include "display/cairo-utils.h"
#include "display/drawing-context.h"
#include "display/drawing-pattern.h"
#include "display/drawing-surface.h"

static auto operator/(Geom::Point const &a, Geom::Point const &b)
{
    return Geom::Point(a.x() / b.x(), a.y() / b.y());
}

namespace Inkscape {

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
}

void DrawingPattern::setOverflow(Geom::Affine const &initial_transform, int steps, Geom::Affine const &step_transform)
{
    _overflow_initial_transform = initial_transform;
    _overflow_steps = steps;
    _overflow_step_transform = step_transform;
}

cairo_pattern_t *DrawingPattern::renderPattern(float opacity)
{
    if (opacity < 1e-3) {
        // Invisible.
        return nullptr;
    }

    bool needs_opacity = opacity < 1.0 - 1e-3;

    if (!_tile_rect || _tile_rect->hasZeroArea()) {
        return nullptr;
    }
    Geom::Rect pattern_tile = *_tile_rect;

    //TODO: If pattern_to_user set it to identity transform

    // The DrawingSurface class handles the mapping from "logical space"
    // (coordinates in the rendering) to "physical space" (surface pixels).
    // An oversampling is done as the pattern may not pixel align with the final surface.
    // The cairo surface is created when the DrawingContext is declared.
    // Create drawing surface with size of pattern tile (in pattern space) but with number of pixels
    // based on required resolution (c).
    Inkscape::DrawingSurface pattern_surface(pattern_tile, _pattern_resolution);
    Inkscape::DrawingContext dc(pattern_surface);
    dc.transform(pattern_surface.drawingTransform().inverse());

    pattern_tile *= pattern_surface.drawingTransform();
    Geom::IntRect one_tile = pattern_tile.roundOutwards();

    if (needs_opacity) {
        dc.pushGroup(); // this group is for pattern + opacity
    }

    if (_overflow_steps == 1) {
        render(dc, one_tile);
    } else {
        //Overflow transforms need to be transformed to the new coordinate system
        //introduced by dc.transform(pattern_surface.drawingTransform().inverse());
        Geom::Affine dt = pattern_surface.drawingTransform();
        Geom::Affine idt = pattern_surface.drawingTransform().inverse();
        Geom::Affine initial_transform = idt * _overflow_initial_transform * dt;
        Geom::Affine step_transform = idt * _overflow_step_transform * dt;
        dc.transform(initial_transform);
        for (int i = 0; i < _overflow_steps; i++) {
            // render() fails to handle transforms applied here when using cache.
            render(dc, one_tile, RENDER_BYPASS_CACHE);
            dc.transform(step_transform);
            // cairo_surface_t* raw = pattern_surface.raw();
            // std::string filename = "drawing-pattern" + std::to_string(i) + ".png";
            // cairo_surface_write_to_png( pattern_surface.raw(), filename.c_str() );
        }
    }

    // Uncomment to debug
    // cairo_surface_t* raw = pattern_surface.raw();
    // std::cout << "  cairo_surface (sp-pattern): "
    //           << " width: "  << cairo_image_surface_get_width( raw )
    //           << " height: " << cairo_image_surface_get_height( raw )
    //           << std::endl;
    // std::string filename = "drawing-pattern.png";
    // cairo_surface_write_to_png( pattern_surface.raw(), filename.c_str() );

    if (needs_opacity) {
        dc.popGroupToSource(); // pop raw pattern
        dc.paint(opacity); // apply opacity
    }

    cairo_pattern_t *cp = cairo_pattern_create_for_surface(pattern_surface.raw());
    // Apply transformation to user space. Also compensate for oversampling.
    if (_pattern_to_user) {
        ink_cairo_pattern_set_matrix(cp, _pattern_to_user->inverse() * pattern_surface.drawingTransform());
    } else {
        ink_cairo_pattern_set_matrix(cp, pattern_surface.drawingTransform());
    }

    cairo_pattern_set_extend(cp, CAIRO_EXTEND_REPEAT);

    return cp;
}

// TODO investigate if area should be used.
unsigned DrawingPattern::_updateItem(Geom::IntRect const &area, UpdateContext const &ctx, unsigned flags, unsigned reset)
{
    if (!_tile_rect || _tile_rect->hasZeroArea()) {
        return STATE_NONE;
    }

    Geom::Rect pattern_tile = *_tile_rect;
    Geom::Coord det_ctm = ctx.ctm.descrim();
    Geom::Coord det_ps2user = _pattern_to_user ? _pattern_to_user->descrim() : 1.0;
    Geom::Coord det_child_transform = _child_transform ? _child_transform->descrim() : 1.0;
    const double oversampling = 2.0;
    double scale = det_ctm * det_ps2user * det_child_transform * oversampling;
    //FIXME: When scale is too big (zooming in a hatch), cairo doesn't render the pattern
    //More precisely it fails when setting pattern matrix in DrawingPattern::renderPattern
    //Fully correct solution should make use of visible area bbox and change hatch tile rect
    //accordingly
    if (scale > 25) {
        scale = 25;
    }
    auto c = pattern_tile.dimensions() * scale * oversampling;
    _pattern_resolution = c.ceil();

    return DrawingGroup::_updateItem(Geom::IntRect::infinite(), {Geom::Translate(-pattern_tile.min()) * Geom::Scale(_pattern_resolution / pattern_tile.dimensions())}, flags, reset);
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
