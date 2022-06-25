// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * SVG <hatchPath> implementation
 */
/*
 * Author:
 *   Tomasz Boczkowski <penginsbacon@gmail.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2014 Tomasz Boczkowski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_HATCH_PATH_H
#define SEEN_SP_HATCH_PATH_H

#include <list>
#include <cstddef>
#include <optional>
#include <glibmm/ustring.h>
#include <sigc++/connection.h>
#include <2geom/generic-interval.h>
#include <2geom/pathvector.h>

#include "svg/svg-length.h"
#include "object/sp-object.h"
#include "display/curve.h"

namespace Inkscape {

class Drawing;
class DrawingShape;
class DrawingItem;

} // namespace Inkscape

class SPHatchPath : public SPObject
{
public:
    SPHatchPath();
    ~SPHatchPath() override;

    SVGLength offset;

    bool isValid() const;

    Inkscape::DrawingItem *show(Inkscape::Drawing &drawing, unsigned int key, Geom::OptInterval extents);
    void hide(unsigned int key);

    void setStripExtents(unsigned int key, Geom::OptInterval const &extents);
    Geom::Interval bounds() const;

    SPCurve calculateRenderCurve(unsigned key) const;

protected:
    void build(SPDocument* doc, Inkscape::XML::Node* repr) override;
    void release() override;
    void set(SPAttr key, const gchar* value) override;
    void update(SPCtx* ctx, unsigned int flags) override;

private:
    struct View
    {
        Inkscape::DrawingShape *arenaitem;
        Geom::OptInterval extents;
        unsigned key;
    };

    using ViewIterator = std::list<SPHatchPath::View>::iterator;
    using ConstViewIterator = std::list<SPHatchPath::View>::const_iterator;
    std::list<View> _display;

    gdouble _repeatLength() const;
    void _updateView(View &view);
    SPCurve _calculateRenderCurve(View const &view) const;

    void _readHatchPathVector(char const *str, Geom::PathVector &pathv, bool &continous_join);

    std::optional<SPCurve> _curve;
    bool _continuous;
};

#endif // SEEN_SP_HATCH_PATH_H

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
