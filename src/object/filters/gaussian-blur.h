// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief SVG Gaussian blur filter effect
 *//*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_GAUSSIANBLUR_H_SEEN
#define SP_GAUSSIANBLUR_H_SEEN

#include "sp-filter-primitive.h"
#include "number-opt-number.h"

class SPGaussianBlur
    : public SPFilterPrimitive
{
public:
    Geom::Rect calculate_region(Geom::Rect const &region) const override;

    NumberOptNumber const &get_std_deviation() const { return stdDeviation; }

private:
    NumberOptNumber stdDeviation;

protected:
    void build(SPDocument *document, Inkscape::XML::Node *repr) override;
    void set(SPAttr key, char const *value) override;

    std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer(Inkscape::DrawingItem *item) const override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_GAUSSIANBLUR, SPGaussianBlur)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_GAUSSIANBLUR, SPGaussianBlur)

#endif // SP_GAUSSIANBLUR_H_SEEN

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
