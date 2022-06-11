// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_CANVAS_ITEM_CROPMARKS_H
#define SEEN_CANVAS_ITEM_CROPMARKS_H

/**
 * A class to represent crop marks
 *
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2022 the Authors.
 *
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/ustring.h>

#include <2geom/point.h>
#include <2geom/transforms.h>

#include "canvas-item.h"

namespace Inkscape {

class CanvasItemCropMarks : public CanvasItem {

public:
    CanvasItemCropMarks(CanvasItemGroup *group);
    ~CanvasItemCropMarks() override;

    void update(Geom::Affine const &affine) override;
    void set_size(Geom::Rect const &size, Geom::Rect const &bleed);
    void render(Inkscape::CanvasItemBuffer *buf) override;

private:
    Geom::Rect _size;
    Geom::Rect _bleed;

    Geom::Rect _min;
    Geom::Rect _max;
};


} // namespace Inkscape

#endif // SEEN_CANVAS_ITEM_CROPMARKS_H

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
