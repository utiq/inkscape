// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_CANVAS_ITEM_RECT_H
#define SEEN_CANVAS_ITEM_RECT_H

/**
 * A class to represent a control rectangle. Used for rubberband selector, page outline, etc.
 */

/*
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Rewrite of CtrlRect
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <memory>
#include <2geom/path.h>

#include "canvas-item.h"

namespace Inkscape {

class CanvasItemGroup; // A canvas control that contains other canvas controls.

class CanvasItemRect : public CanvasItem {

public:
    CanvasItemRect(CanvasItemGroup *group);
    CanvasItemRect(CanvasItemGroup *group, Geom::Rect const &rect);

    // Geometry
    void set_rect(Geom::Rect const &rect);
    Geom::Rect get_rect() const {return _rect;}
    void visit_page_rects(std::function<void(const Geom::Rect&)>) const override;

    void update(Geom::Affine const &affine) override;
    double closest_distance_to(Geom::Point const &p); // Maybe not needed

    // Selection
    bool contains(Geom::Point const &p, double tolerance = 0) override;

    // Display
    void render(Inkscape::CanvasItemBuffer *buf) override;

    // Properties
    void set_is_page(bool is_page);
    void set_fill(guint32 color) override;
    void set_dashed(bool dash = true);
    void set_inverted(bool inverted = false);
    void set_shadow(uint32_t color, int width);
 
protected:
    double get_shadow_size() const;

    Geom::Rect _rect;
    bool _is_page = false;
    bool _dashed = false;
    bool _inverted = false;
    int _shadow_width = 0;
    uint32_t _shadow_color = 0x0;
};

} // namespace Inkscape

#endif // SEEN_CANVAS_ITEM_RECT_H

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
