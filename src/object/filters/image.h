// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief SVG image filter effect
 *//*
 * Authors:
 *   Felipe Corrêa da Silva Sanches <juca@members.fsf.org>
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_FEIMAGE_H_SEEN
#define SP_FEIMAGE_H_SEEN

#include <memory>
#include "sp-filter-primitive.h"
#include "enums.h"

class SPItem;

namespace Inkscape {
class URIReference;
} // namespace Inksacpe

class SPFeImage
    : public SPFilterPrimitive
{
private:
    char *href = nullptr;

    // preserveAspectRatio
    unsigned aspect_align = SP_ASPECT_XMID_YMID;
    unsigned aspect_clip = SP_ASPECT_MEET;

    bool from_element = false;
    SPItem *SVGElem = nullptr;
    std::unique_ptr<Inkscape::URIReference> SVGElemRef;
    sigc::connection _image_modified_connection;
    sigc::connection _href_modified_connection;

    bool valid_for(SPObject const *obj) const override;

    void on_image_modified();
    void on_href_modified(SPObject *new_elem);

protected:
    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
	void release() override;
    void set(SPAttr key, char const *value) override;

    std::unique_ptr<Inkscape::Filters::FilterPrimitive> build_renderer() const override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FEIMAGE, SPFeImage)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FEIMAGE, SPFeImage)

#endif // SP_FEIMAGE_H_SEEN

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
