// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief SVG tile filter effect
 *//*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_FETILE_H_SEEN
#define SP_FETILE_H_SEEN

#include "sp-filter-primitive.h"

class SPFeTile
    : public SPFilterPrimitive
{
protected:
    std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer(Inkscape::DrawingItem *item) const override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FETILE, SPFeTile)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FETILE, SPFeTile)

#endif // SP_FETILE_H_SEEN

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
