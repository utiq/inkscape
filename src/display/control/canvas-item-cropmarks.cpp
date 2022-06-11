// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * A class to represent print marks on canvas
 */

/*
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2022 the Authors.
 *
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "canvas-item-cropmarks.h"
#include "color.h"

#include <2geom/line.h>

namespace Inkscape {

CanvasItemCropMarks::CanvasItemCropMarks(CanvasItemGroup *group)
    : CanvasItem(group)
{
    _name = "CanvasItemCropMarks";
    _pickable = false;
}

CanvasItemCropMarks::~CanvasItemCropMarks() = default;

/**
 * Sets position of the crop marks
 */
void CanvasItemCropMarks::set_size(Geom::Rect const &size, Geom::Rect const &bleed)
{
    if (_size != size || _bleed != bleed) {
        _size = size;
        _bleed = bleed;

        // Copy page size and grow it by half the bleed size.
        _min = _size;
        _min.expandTo(Geom::middle_point(_size.corner(0), _bleed.corner(0)));
        _min.expandTo(Geom::middle_point(_size.corner(2), _bleed.corner(2)));

        // This increases the maximum area by x2 the delta, the *2 is hidden.
        _max = _min;
        _max.expandBy(_bleed.maxExtent() - _size.maxExtent());

        request_update();
    }
}

/**
 * Something has changed, so update the drawing geometry.
 */
void CanvasItemCropMarks::update(Geom::Affine const &affine)
{
    if (_affine == affine && !_need_update) {
        // Nothing to do.
        return;
    }

    // Queue redraw of old area (erase previous content).
    request_redraw();

    _affine = affine;

    // Save maximum extent so it can be cleared by base class
    _bounds = _max * _affine;
    _bounds.expandBy(2);

    request_redraw();
    _need_update = false;
}

/**
 * Render crop marks to screen via Cairo.
 */
void CanvasItemCropMarks::render(Inkscape::CanvasItemBuffer *buf)
{
    if (!buf) {
        std::cerr << "CanvasItemCropMarks::Render: No buffer!" << std::endl;
        return;
    }

    if (!_visible || !_bounds.intersects(buf->rect)) {
        // Hidden
        return;
    }

    // Build 8 lines from the three rectangles to make the crop marks
    std::vector<Geom::Line> lines;
    for (int i = 0; i < 4; i++) {
        auto a = _size.corner((i % 2) * 2) * _affine;
        auto b = _min.corner((i > 1) * 2) * _affine;
        auto c = _max.corner((i > 1) * 2) * _affine;
        lines.emplace_back(Geom::Point(a[Geom::X], b[Geom::Y]), Geom::Point(a[Geom::X], c[Geom::Y]));
        lines.emplace_back(Geom::Point(b[Geom::X], a[Geom::Y]), Geom::Point(c[Geom::X], a[Geom::Y]));
    }

    // Set up the Cairo rendering context with screen offset and stroke color
    auto ctx = buf->cr;
    ctx->save();
    ctx->translate(-buf->rect.left(), -buf->rect.top());
    ctx->set_source_rgba(SP_RGBA32_R_F(_stroke), SP_RGBA32_G_F(_stroke),
                         SP_RGBA32_B_F(_stroke), SP_RGBA32_A_F(_stroke));
    ctx->set_line_width(1);

    // Draw each of the requested lines
    for (auto line : lines) {
        auto start = line.initialPoint();
        auto end = line.finalPoint();
        // Lines are offset by half a px to align them to the screen pixels.
        ctx->move_to(floor(start.x()) + 0.5, floor(start.y()) + 0.5);
        ctx->line_to(floor(end.x()) + 0.5, floor(end.y()) + 0.5);
    }
    ctx->stroke();
    ctx->restore();
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
