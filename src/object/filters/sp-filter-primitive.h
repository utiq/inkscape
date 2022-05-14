// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SP_FILTER_PRIMITIVE_H
#define SEEN_SP_FILTER_PRIMITIVE_H

/** \file
 * Document level base class for all SVG filter primitives.
 */
/*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Niko Kiirala <niko@kiirala.com>
 *
 * Copyright (C) 2006,2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <memory>
#include "2geom/rect.h"
#include "object/sp-object.h"
#include "object/sp-dimensions.h"

namespace Inkscape {
class Drawing;
class DrawingItem;
namespace Filters {
class Filter;
class FilterPrimitive;
} // namespace Filters
} // namespace Inkscape

class SPFilterPrimitive
    : public SPObject
    , public SPDimensions
{
public:
	SPFilterPrimitive();
	~SPFilterPrimitive() override;

    int image_in, image_out;

protected:
    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
	void release() override;

    void set(SPAttr key, char const *value) override;

    void update(SPCtx *ctx, unsigned flags) override;

    Inkscape::XML::Node *write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, unsigned flags) override;

public:
    virtual void show(Inkscape::DrawingItem *item) {}
    virtual void hide(Inkscape::DrawingItem *item) {}

    virtual std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer(Inkscape::DrawingItem *item) const = 0;

    /* Calculate the filter's effect on the region */
    virtual Geom::Rect calculate_region(Geom::Rect const &region) const;

    /* Return true if the object should be allowed to use this filter */
    virtual bool valid_for(SPObject const *obj) const
    {
        // This is used by feImage to stop infinite loops.
        return true;
    };

	/* Common initialization for filter primitives */
    void build_renderer_common(Inkscape::Filters::FilterPrimitive *primitive) const;

	int name_previous_out();
	int read_in(char const *name);
	int read_result(char const *name);
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FILTER_PRIMITIVE, SPFilterPrimitive)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FILTER_PRIMITIVE, SPFilterPrimitive)

#endif // SEEN_SP_FILTER_PRIMITIVE_H

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
