// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Group belonging to an SVG drawing element.
 *//*
 * Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *
 * Copyright (C) 2011 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_DISPLAY_DRAWING_SHAPE_H
#define SEEN_INKSCAPE_DISPLAY_DRAWING_SHAPE_H

#include "display/drawing-item.h"
#include "display/nr-style.h"

#include <memory>

class SPStyle;
class SPCurve;

namespace Inkscape {

class DrawingShape
    : public DrawingItem
{
public:
    DrawingShape(Drawing &drawing);
    ~DrawingShape() override;

    void setPath(std::shared_ptr<SPCurve const> curve);
    void setStyle(SPStyle const *style, SPStyle const *context_style = nullptr) override;
    void setChildrenStyle(SPStyle const *context_style) override;

protected:
    unsigned _updateItem(Geom::IntRect const &area, UpdateContext const &ctx, unsigned flags, unsigned reset) override;
    unsigned _renderItem(DrawingContext &dc, Geom::IntRect const &area, unsigned flags, DrawingItem *stop_at) override;
    void _clipItem(DrawingContext &dc, Geom::IntRect const &area) override;
    DrawingItem *_pickItem(Geom::Point const &p, double delta, unsigned flags) override;
    bool _canClip() override;

    void _renderFill(DrawingContext &dc, Geom::IntRect const &area);
    void _renderStroke(DrawingContext &dc, Geom::IntRect const &area);
    void _renderMarkers(DrawingContext &dc, Geom::IntRect const &area, unsigned flags, DrawingItem *stop_at);

    bool style_vector_effect_stroke : 1;
    bool style_stroke_extensions_hairline : 1;
    SPWindRule style_clip_rule;
    SPWindRule style_fill_rule;
    unsigned style_opacity : 24;

    std::shared_ptr<SPCurve const> _curve;
    NRStyle _nrstyle;

    DrawingItem *_last_pick;
    unsigned _repick_after;
};

} // end namespace Inkscape

#endif // !SEEN_INKSCAPE_DISPLAY_DRAWING_ITEM_H

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
