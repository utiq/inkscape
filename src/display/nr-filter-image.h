// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_NR_FILTER_IMAGE_H
#define SEEN_NR_FILTER_IMAGE_H

/*
 * feImage filter primitive renderer
 *
 * Authors:
 *   Niko Kiirala <niko@kiirala.com>
 *
 * Copyright (C) 2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <memory>
#include <string>
#include "display/nr-filter-primitive.h"

class SPDocument;
class SPItem;

namespace Inkscape {
class Pixbuf;

namespace Filters {
class FilterSlot;

class FilterImage : public FilterPrimitive
{
public:
    FilterImage();
    ~FilterImage() override;

    void render_cairo(FilterSlot &slot) override;
    bool can_handle_affine(Geom::Affine const &) const override;
    double complexity(Geom::Affine const &ctm) const override;

    void set_document(SPDocument *document);
    void set_href(char const *href);
    void set_align(unsigned align);
    void set_clip(unsigned clip);
    bool from_element;
    SPItem *SVGElem;

    Glib::ustring name() const override { return Glib::ustring("Image"); }

private:
    SPDocument *document;
    std::string feImageHref;
    std::unique_ptr<Inkscape::Pixbuf> image;
    unsigned aspect_align, aspect_clip;
    bool broken_ref;
};

} // namespace Filters
} // namespace Inkscape

#endif // SEEN_NR_FILTER_IMAGE_H
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
