// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * A class to represent a control textrilateral. Used to highlight selected text.
 */

/*
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Rewrite of SPCtrlTextr
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "canvas-item-text.h"

#include <cmath>
#include <utility> // std::move
#include <glibmm/i18n.h>

#include "color.h" // SP_RGBA_x_F

#include "ui/util.h"
#include "ui/widget/canvas.h"

namespace Inkscape {

/**
 * Create an null control text.
 */
CanvasItemText::CanvasItemText(CanvasItemGroup *group)
    : CanvasItem(group)
{
    _name = "CanvasItemText";
    _pickable = false; // Text is never pickable.
    _fill = 0x33337fff; // Override CanvasItem default.
}

/**
 * Create a control text. Point are in document coordinates.
 */
CanvasItemText::CanvasItemText(CanvasItemGroup *group, Geom::Point const &p, Glib::ustring text, bool scaled)
    : CanvasItem(group)
    , _p(p)
    , _text(std::move(text))
    , _scaled(scaled)
{
    _name = "CanvasItemText";
    _pickable = false; // Text is never pickable.
    _fill = 0x33337fff; // Override CanvasItem default.

    request_update();
}

/**
 * Set a text position. Position is in document coordinates.
 */
void CanvasItemText::set_coord(Geom::Point const &p)
{
    _p = p;

    request_update();
}

/**
 * Set a text position. Position is in document coordinates.
 */
void CanvasItemText::set_bg_radius(double const &rad)
{
    _bg_rad = rad;

    request_update();
}

/**
 * Returns distance between point in canvas units and nearest point on text.
 */
double CanvasItemText::closest_distance_to(Geom::Point const &p)
{
    double d = Geom::infinity();
    std::cerr << "CanvasItemText::closest_distance_to: Not implemented!" << std::endl;
    return d;
}

/**
 * Returns true if point p (in canvas units) is within tolerance (canvas units) distance of text.
 */
bool CanvasItemText::contains(Geom::Point const &p, double tolerance)
{
    return false; // We never select text.
}

/**
 * Update and redraw control text.
 */
void CanvasItemText::update(Geom::Affine const &affine)
{
    if (_affine == affine && !_need_update) {
        // Nothing to do.
        return;
    }

    // Queue redraw of old area (erase previous content).
    request_redraw();

    // Get new bounds
    _affine = affine;

    // Point needs to be scaled manually if not cairo scaling
    Geom::Point p = _scaled ? _p : _p * _affine;

    // Measure text size
    _text_box = load_text_extents();

    // Offset relative to requested point
    double offset_x = -(_anchor_position.x() * _text_box.width());
    double offset_y = -(_anchor_position.y() * _text_box.height());
    offset_x += p.x() + _adjust_offset.x();
    offset_y += p.y() + _adjust_offset.y();
    _text_box *= Geom::Translate(Geom::Point(int(offset_x), int(offset_y)));

    // Pixel alignment of background. Avoid aliasing artifacts on redraw.
    _text_box = _text_box.roundOutwards();

    // Don't apply affine here, to keep text at the same size in screen coords.
    _bounds = _text_box;
    if (_scaled) {
        _bounds *= _affine;
        _bounds = _bounds.roundOutwards();
    }

    // Queue redraw of new area
    request_redraw();

    _need_update = false;
}

/**
 * Render text to screen via Cairo.
 */
void CanvasItemText::render(Inkscape::CanvasItemBuffer *buf)
{
    if (!buf) {
        std::cerr << "CanvasItemText::Render: No buffer!" << std::endl;
         return;
    }

    if (!_visible) {
        // Hidden
        return;
    }

    buf->cr->save();

    // Screen to desktop coords.
    buf->cr->translate(-buf->rect.left(), -buf->rect.top());

    if (_scaled) {
        // Convert from canvas space to document space
        buf->cr->transform(geom_to_cairo(_affine));
    }

    double x = _text_box.min().x();
    double y = _text_box.min().y();
    double w = _text_box.width();
    double h = _text_box.height();

    // Background
    if (_use_background) {
        if (_bg_rad == 0.0) {
            buf->cr->rectangle(x, y, w, h);
        } else {
            double radius = _bg_rad * (std::min(w ,h) / 2);
            buf->cr->arc(x + w - radius, y + radius, radius, -M_PI_2, 0);
            buf->cr->arc(x + w - radius, y + h - radius, radius, 0, M_PI_2);
            buf->cr->arc(x + radius, y + h - radius, radius, M_PI_2, M_PI);
            buf->cr->arc(x + radius, y + radius, radius, M_PI, 3*M_PI_2);
        }
        buf->cr->set_line_width(2);
        buf->cr->set_source_rgba(SP_RGBA32_R_F(_background), SP_RGBA32_G_F(_background),
                                 SP_RGBA32_B_F(_background), SP_RGBA32_A_F(_background));
        buf->cr->fill();
    }

    // Center the text inside the draw background box
    auto bx = x + w / 2.0;
    auto by = y + h / 2.0 + 1;
    buf->cr->move_to(int(bx - _text_size.x_bearing - _text_size.width/2.0),
                     int(by - _text_size.y_bearing - _text_extent.height/2.0));

    buf->cr->select_font_face(_fontname, Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    buf->cr->set_font_size(_fontsize);
    buf->cr->text_path(_text);
    buf->cr->set_source_rgba(SP_RGBA32_R_F(_fill), SP_RGBA32_G_F(_fill),
                             SP_RGBA32_B_F(_fill), SP_RGBA32_A_F(_fill));
    buf->cr->fill();
    buf->cr->restore();
}

void CanvasItemText::set_text(Glib::ustring const &text)
{
    if (_text != text) {
        _text = text;
        request_update(); // Might be larger than before!
    }
}

void CanvasItemText::set_fontsize(double fontsize)
{
    if (_fontsize != fontsize) {
        _fontsize = fontsize;
        request_update(); // Might be larger than before!
    }
}

/**
 * Load the sizes of the text extent using the given font.
 */
Geom::Rect CanvasItemText::load_text_extents()
{
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 1, 1);
    auto context = Cairo::Context::create(surface);
    context->select_font_face(_fontname, Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    context->set_font_size(_fontsize);
    context->get_text_extents(_text, _text_size);

    if (_fixed_line) {
        // TRANSLATORS: This is a set of letters to test for font accender and decenders.
        context->get_text_extents(_("lg1p$"), _text_extent);
    } else {
        _text_extent = _text_size;
    }

    return Geom::Rect::from_xywh(0, 0,
                                 _text_size.x_advance + (_border * 2), 
                                 _text_extent.height + (_border * 2));
}

void CanvasItemText::set_background(guint32 background)
{
    if (_background != background) {
        _background = background;
        request_redraw();
    }
    _use_background = true;
}

/**
 * Set the anchor point, x and y between 0.0 and 1.0.
 */
void CanvasItemText::set_anchor(Geom::Point const &anchor_pt)
{
    if (_anchor_position != anchor_pt) {
        _anchor_position = anchor_pt;
        _canvas->request_update();
    }
}

void CanvasItemText::set_adjust(Geom::Point const &adjust_pt)
{
    if (_adjust_offset != adjust_pt) {
        _adjust_offset = adjust_pt;
        _canvas->request_update();
    }
}

void CanvasItemText::set_fixed_line(bool fixed_line)
{
    if (_fixed_line != fixed_line) {
        _fixed_line = fixed_line;
        _canvas->request_update();
    }
}

void CanvasItemText::set_border(double border)
{
    if (_border != border) {
        _border = border;
        _canvas->request_update();
    }
}

} // namespace Inkscape

/* FROM: http://lists.cairographics.org/archives/cairo-bugs/2009-March/003014.html
  - Glyph surfaces: In most font rendering systems, glyph surfaces
    have an origin at (0,0) and a bounding box that is typically
    represented as (x_bearing,y_bearing,width,height).  Depending on
    which way y progresses in the system, y_bearing may typically be
    negative (for systems similar to cairo, with origin at top left),
    or be positive (in systems like PDF with origin at bottom left).
    No matter which is the case, it is important to note that
    (x_bearing,y_bearing) is the coordinates of top-left of the glyph
    relative to the glyph origin.  That is, for example:

    Scaled-glyph space:

      (x_bearing,y_bearing) <-- negative numbers
         +----------------+
         |      .         |
         |      .         |
         |......(0,0) <---|-- glyph origin
         |                |
         |                |
         +----------------+
                  (width+x_bearing,height+y_bearing)

    Note the similarity of the origin to the device space.  That is
    exactly how we use the device_offset to represent scaled glyphs:
    to use the device-space origin as the glyph origin.
*/

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
