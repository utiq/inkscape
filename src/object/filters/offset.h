// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief SVG offset filter effect
 *//*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_FEOFFSET_H_SEEN
#define SP_FEOFFSET_H_SEEN

#include "sp-filter-primitive.h"

class SPFeOffset
    : public SPFilterPrimitive
{
public:
    Geom::Rect calculate_region(Geom::Rect const &region) const override;

private:
    double dx = 0.0;
    double dy = 0.0;

    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
    void set(SPAttr key, char const *value) override;

    std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer(Inkscape::DrawingItem *item) const override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FEOFFSET, SPFeOffset)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FEOFFSET, SPFeOffset)

#endif // SP_FEOFFSET_H_SEEN

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
