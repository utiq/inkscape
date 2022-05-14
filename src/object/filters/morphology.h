// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * @brief SVG morphology filter effect
 *//*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_FEMORPHOLOGY_H_SEEN
#define SP_FEMORPHOLOGY_H_SEEN

#include "sp-filter-primitive.h"
#include "number-opt-number.h"
#include "display/nr-filter-morphology.h"

class SPFeMorphology
    : public SPFilterPrimitive
{
private:
    Inkscape::Filters::FilterMorphologyOperator Operator = Inkscape::Filters::MORPHOLOGY_OPERATOR_ERODE;
    NumberOptNumber radius = NumberOptNumber(0);

protected:
    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
    void set(SPAttr key, char const *value) override;

    std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer(Inkscape::DrawingItem *item) const override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FEMORPHOLOGY, SPFeMorphology)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FEMORPHOLOGY, SPFeMorphology)

#endif // SP_FEMORPHOLOGY_H_SEEN

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
