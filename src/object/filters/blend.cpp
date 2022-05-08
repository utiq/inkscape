// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feBlend> implementation.
 */
/*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Niko Kiirala <niko@kiirala.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006,2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>

#include "blend.h"
#include "attributes.h"
#include "display/nr-filter.h"
#include "object/sp-filter.h"
#include "xml/repr.h"

void SPFeBlend::build(SPDocument *document, Inkscape::XML::Node *repr)
{
    SPFilterPrimitive::build(document, repr);

    readAttr(SPAttr::MODE);
    readAttr(SPAttr::IN2);
}

static SPBlendMode read_mode(char const *value)
{
    if (!value) {
    	return SP_CSS_BLEND_NORMAL;
    }

    switch (value[0]) {
        case 'n':
            if (std::strcmp(value, "normal") == 0)
                return SP_CSS_BLEND_NORMAL;
            break;
        case 'm':
            if (std::strcmp(value, "multiply") == 0)
                return SP_CSS_BLEND_MULTIPLY;
            break;
        case 's':
            if (std::strcmp(value, "screen") == 0)
                return SP_CSS_BLEND_SCREEN;
            if (std::strcmp(value, "saturation") == 0)
                return SP_CSS_BLEND_SATURATION;
            break;
        case 'd':
            if (std::strcmp(value, "darken") == 0)
                return SP_CSS_BLEND_DARKEN;
            if (std::strcmp(value, "difference") == 0)
                return SP_CSS_BLEND_DIFFERENCE;
            break;
        case 'l':
            if (std::strcmp(value, "lighten") == 0)
                return SP_CSS_BLEND_LIGHTEN;
            if (std::strcmp(value, "luminosity") == 0)
                return SP_CSS_BLEND_LUMINOSITY;
            break;
        case 'o':
            if (std::strcmp(value, "overlay") == 0)
                return SP_CSS_BLEND_OVERLAY;
            break;
        case 'c':
            if (std::strcmp(value, "color-dodge") == 0)
                return SP_CSS_BLEND_COLORDODGE;
            if (std::strcmp(value, "color-burn") == 0)
                return SP_CSS_BLEND_COLORBURN;
            if (std::strcmp(value, "color") == 0)
                return SP_CSS_BLEND_COLOR;
            break;
        case 'h':
            if (std::strcmp(value, "hard-light") == 0)
                return SP_CSS_BLEND_HARDLIGHT;
            if (std::strcmp(value, "hue") == 0)
                return SP_CSS_BLEND_HUE;
            break;
        case 'e':
            if (std::strcmp(value, "exclusion") == 0)
                return SP_CSS_BLEND_EXCLUSION;
        default:
            std::cout << "SPBlendMode: Unimplemented mode: " << value << std::endl;
            // do nothing by default
            break;
    }

    return SP_CSS_BLEND_NORMAL;
}

void SPFeBlend::set(SPAttr key, char const *value)
{
    switch (key) {
        case SPAttr::MODE: {
            auto mode = ::read_mode(value);
            if (mode != blend_mode) {
                blend_mode = mode;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }
        case SPAttr::IN2: {
            int input = read_in(value);
            if (input != in2) {
                in2 = input;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            // Unlike normal in, in2 is a required attribute. Make sure we can call it by some name.
            // This may not be true.... see issue at http://www.w3.org/TR/filter-effects/#feBlendElement (but it doesn't hurt).
            if (in2 == Inkscape::Filters::NR_FILTER_SLOT_NOT_SET || in2 == Inkscape::Filters::NR_FILTER_UNNAMED_SLOT) {
                auto filter_parent = SP_FILTER(parent);
                in2 = name_previous_out();
                //XML Tree being used directly here while it shouldn't be.
                setAttribute("in2", filter_parent->name_for_image(in2));
            }
            break;
        }
        default:
        	SPFilterPrimitive::set(key, value);
            break;
    }
}

Inkscape::XML::Node *SPFeBlend::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, unsigned flags)
{
    if ((flags & SP_OBJECT_WRITE_BUILD) && !repr) {
        repr = doc->createElement("svg:feBlend");
    }

    auto filter_parent = SP_FILTER(parent);

    char const *in2_name = filter_parent->name_for_image(in2);

    if (!in2_name) {
        // This code is very similar to name_previous_out()
        SPObject *i = parent->firstChild();

        // Find previous filter primitive
        while (i && i->getNext() != this) {
        	i = i->getNext();
        }

        if (i) {
            auto i_prim = SP_FILTER_PRIMITIVE(i);
            in2_name = filter_parent->name_for_image(i_prim->image_out);
        }
    }

    if (in2_name) {
        repr->setAttribute("in2", in2_name);
    } else {
        g_warning("Unable to set in2 for feBlend");
    }

    char const *mode;
    switch (blend_mode) {
        case SP_CSS_BLEND_NORMAL:
            mode = "normal";      break;
        case SP_CSS_BLEND_MULTIPLY:
            mode = "multiply";    break;
        case SP_CSS_BLEND_SCREEN:
            mode = "screen";      break;
        case SP_CSS_BLEND_DARKEN:
            mode = "darken";      break;
        case SP_CSS_BLEND_LIGHTEN:
            mode = "lighten";     break;
        // New
        case SP_CSS_BLEND_OVERLAY:
            mode = "overlay";     break;
        case SP_CSS_BLEND_COLORDODGE:
            mode = "color-dodge"; break;
        case SP_CSS_BLEND_COLORBURN:
            mode = "color-burn";  break;
        case SP_CSS_BLEND_HARDLIGHT:
            mode = "hard-light";  break;
        case SP_CSS_BLEND_SOFTLIGHT:
            mode = "soft-light";  break;
        case SP_CSS_BLEND_DIFFERENCE:
            mode = "difference";  break;
        case SP_CSS_BLEND_EXCLUSION:
            mode = "exclusion";   break;
        case SP_CSS_BLEND_HUE:
            mode = "hue";         break;
        case SP_CSS_BLEND_SATURATION:
            mode = "saturation";  break;
        case SP_CSS_BLEND_COLOR:
            mode = "color";       break;
        case SP_CSS_BLEND_LUMINOSITY:
            mode = "luminosity";  break;
        default:
            mode = nullptr;
    }

    repr->setAttribute("mode", mode);

    return SPFilterPrimitive::write(doc, repr, flags);
}

std::unique_ptr<Inkscape::Filters::FilterPrimitive> SPFeBlend::build_renderer() const
{
    auto blend = std::make_unique<Inkscape::Filters::FilterBlend>();
    build_renderer_common(blend.get());

    blend->set_mode(blend_mode);
    blend->set_input(1, in2);

    return blend;
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
