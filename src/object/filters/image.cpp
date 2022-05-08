// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feImage> implementation.
 */
/*
 * Authors:
 *   Felipe Corrêa da Silva Sanches <juca@members.fsf.org>
 *   hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2007 Felipe Sanches
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "image.h"

#include <sigc++/bind.h>

#include "attributes.h"

#include "bad-uri-exception.h"

#include "object/sp-image.h"
#include "object/uri.h"
#include "object/uri-references.h"

#include "display/nr-filter-image.h"
#include "display/nr-filter.h"

#include "xml/repr.h"

void SPFeImage::build(SPDocument *document, Inkscape::XML::Node *repr)
{
    SPFilterPrimitive::build(document, repr);

    readAttr(SPAttr::PRESERVEASPECTRATIO);
    readAttr(SPAttr::XLINK_HREF);
}

void SPFeImage::release()
{
    _image_modified_connection.disconnect();
    _href_modified_connection.disconnect();
    SVGElemRef.reset();

    SPFilterPrimitive::release();
}

void SPFeImage::on_image_modified()
{
    parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
}

void SPFeImage::on_href_modified(SPObject *new_elem)
{
    _image_modified_connection.disconnect();

    if (new_elem) {
        SVGElem = SP_ITEM(new_elem);
        _image_modified_connection = SVGElem->connectModified([this] (SPObject*, unsigned) { on_image_modified(); });
    } else {
        SVGElem = nullptr;
    }

    parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
}

void SPFeImage::set(SPAttr key, char const *value)
{
    switch (key) {
        case SPAttr::XLINK_HREF:
            if (href) {
                g_free(href);
            }
            href = value ? g_strdup(value) : nullptr;
            if (!href) return;
            SVGElemRef.reset();
            SVGElem = nullptr;
            _image_modified_connection.disconnect();
            _href_modified_connection.disconnect();
            try {
                Inkscape::URI SVGElem_uri(href);
                SVGElemRef = std::make_unique<Inkscape::URIReference>(document);
                SVGElemRef->attach(SVGElem_uri);
                from_element = true;
                _href_modified_connection = SVGElemRef->changedSignal().connect([this] (SPObject*, SPObject *to) { on_href_modified(to); });
                if (SPObject *elemref = SVGElemRef->getObject()) {
                    SVGElem = SP_ITEM(elemref);
                    _image_modified_connection = SVGElem->connectModified([this] (SPObject*, unsigned) { on_image_modified(); });
                    requestModified(SP_OBJECT_MODIFIED_FLAG);
                    break;
                } else {
                    g_warning("SVG element URI was not found in the document while loading this: %s", value);
                }
            } catch (Inkscape::BadURIException const &e) {
                // catches either MalformedURIException or UnsupportedURIException
                from_element = false;
                /* This occurs when using external image as the source */
                // g_warning("caught Inkscape::BadURIException in sp_feImage_set");
                break;
            }
            break;

        case SPAttr::PRESERVEASPECTRATIO:
            /* Copied from sp-image.cpp */
            /* Do setup before, so we can use break to escape */
            aspect_align = SP_ASPECT_XMID_YMID; // Default
            aspect_clip = SP_ASPECT_MEET; // Default
            requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG);
            if (value) {
                int len;
                char c[256];
                char const *p, *e;
                unsigned int align, clip;
                p = value;
                while (*p && *p == 32) p += 1;
                if (!*p) break;
                e = p;
                while (*e && *e != 32) e += 1;
                len = e - p;
                if (len > 8) break;
                std::memcpy(c, value, len);
                c[len] = 0;
                /* Now the actual part */
                if (!std::strcmp(c, "none")) {
                    align = SP_ASPECT_NONE;
                } else if (!std::strcmp(c, "xMinYMin")) {
                    align = SP_ASPECT_XMIN_YMIN;
                } else if (!std::strcmp(c, "xMidYMin")) {
                    align = SP_ASPECT_XMID_YMIN;
                } else if (!std::strcmp(c, "xMaxYMin")) {
                    align = SP_ASPECT_XMAX_YMIN;
                } else if (!std::strcmp(c, "xMinYMid")) {
                    align = SP_ASPECT_XMIN_YMID;
                } else if (!std::strcmp(c, "xMidYMid")) {
                    align = SP_ASPECT_XMID_YMID;
                } else if (!std::strcmp(c, "xMaxYMid")) {
                    align = SP_ASPECT_XMAX_YMID;
                } else if (!std::strcmp(c, "xMinYMax")) {
                    align = SP_ASPECT_XMIN_YMAX;
                } else if (!std::strcmp(c, "xMidYMax")) {
                    align = SP_ASPECT_XMID_YMAX;
                } else if (!std::strcmp(c, "xMaxYMax")) {
                    align = SP_ASPECT_XMAX_YMAX;
                } else {
                    g_warning("Illegal preserveAspectRatio: %s", c);
                    break;
                }
                clip = SP_ASPECT_MEET;
                while (*e && *e == 32) e += 1;
                if (*e) {
                    if (!std::strcmp(e, "meet")) {
                        clip = SP_ASPECT_MEET;
                    } else if (!std::strcmp(e, "slice")) {
                        clip = SP_ASPECT_SLICE;
                    } else {
                        break;
                    }
                }
                aspect_align = align;
                aspect_clip = clip;
            } else {
                aspect_align = SP_ASPECT_XMID_YMID; // Default
                aspect_clip = SP_ASPECT_MEET; // Default
            }
            break;

        default:
        	SPFilterPrimitive::set(key, value);
            break;
    }
}

/*
 * Check if the object is being used in the filter's definition
 * and returns true if it is being used (to avoid infinate loops)
 */
bool SPFeImage::valid_for(SPObject const *obj) const
{
    // SVGElem could be nullptr, but this should still work.
    return obj && SP_ITEM(obj) != SVGElem;
}

std::unique_ptr<Inkscape::Filters::FilterPrimitive> SPFeImage::build_renderer() const
{
    auto image = std::make_unique<Inkscape::Filters::FilterImage>();
    build_renderer_common(image.get());

    image->from_element = from_element;
    image->SVGElem = SVGElem;
    image->set_align(aspect_align);
    image->set_clip(aspect_clip);
    image->set_href(href);
    image->set_document(document);

    return image;
}

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
