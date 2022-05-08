// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feDisplacementMap> implementation.
 */
/*
 * Authors:
 *   hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "displacementmap.h"
#include "attributes.h"
#include "display/nr-filter-displacement-map.h"
#include "display/nr-filter.h"
#include "object/sp-filter.h"
#include "svg/svg.h"
#include "util/numeric/converters.h"
#include "xml/repr.h"

void SPFeDisplacementMap::build(SPDocument *document, Inkscape::XML::Node *repr)
{
	SPFilterPrimitive::build(document, repr);

    readAttr(SPAttr::SCALE);
    readAttr(SPAttr::IN2);
    readAttr(SPAttr::XCHANNELSELECTOR);
    readAttr(SPAttr::YCHANNELSELECTOR);
}

static FilterDisplacementMapChannelSelector read_channel_selector(char const *value)
{
    if (!value) return DISPLACEMENTMAP_CHANNEL_ALPHA;
    
    switch (value[0]) {
        case 'R':
            return DISPLACEMENTMAP_CHANNEL_RED;
            break;
        case 'G':
            return DISPLACEMENTMAP_CHANNEL_GREEN;
            break;
        case 'B':
            return DISPLACEMENTMAP_CHANNEL_BLUE;
            break;
        case 'A':
            return DISPLACEMENTMAP_CHANNEL_ALPHA;
            break;
        default:
            // error
            g_warning("Invalid attribute for Channel Selector. Valid modes are 'R', 'G', 'B' or 'A'");
            break;
    }
    
    return DISPLACEMENTMAP_CHANNEL_ALPHA; //default is Alpha Channel
}

void SPFeDisplacementMap::set(SPAttr key, char const *value)
{
    switch (key) {
        case SPAttr::XCHANNELSELECTOR: {
            auto n_selector = ::read_channel_selector(value);
            if (n_selector != xChannelSelector) {
                xChannelSelector = n_selector;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }
        case SPAttr::YCHANNELSELECTOR: {
            auto n_selector = ::read_channel_selector(value);
            if (n_selector != yChannelSelector) {
                yChannelSelector = n_selector;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }
        case SPAttr::SCALE: {
            double n_num = value ? Inkscape::Util::read_number(value) : 0.0;
            if (n_num != scale) {
                scale = n_num;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }
        case SPAttr::IN2: {
            int n_in = read_in(value);
            if (n_in != in2) {
                in2 = n_in;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            // Unlike normal in, in2 is a required attribute. Make sure we can call it by some name.
            if (in2 == Inkscape::Filters::NR_FILTER_SLOT_NOT_SET || in2 == Inkscape::Filters::NR_FILTER_UNNAMED_SLOT) {
                auto filter_parent = SP_FILTER(parent);
                in2 = name_previous_out();
                setAttribute("in2", filter_parent->name_for_image(in2));
            }
            break;
        }
        default:
        	SPFilterPrimitive::set(key, value);
            break;
    }
}

void SPFeDisplacementMap::update(SPCtx *ctx, unsigned flags)
{
    // Unlike normal in, in2 is a required attribute. Make sure we can call it by some name.
    if (in2 == Inkscape::Filters::NR_FILTER_SLOT_NOT_SET || in2 == Inkscape::Filters::NR_FILTER_UNNAMED_SLOT) {
        auto filter_parent = SP_FILTER(parent);
        in2 = name_previous_out();

        //XML Tree being used directly here while it shouldn't be.
        setAttribute("in2", filter_parent->name_for_image(in2));
    }

    SPFilterPrimitive::update(ctx, flags);
}

static char const *get_channelselector_name(FilterDisplacementMapChannelSelector selector)
{
    switch(selector) {
        case DISPLACEMENTMAP_CHANNEL_RED:
            return "R";
        case DISPLACEMENTMAP_CHANNEL_GREEN:
            return "G";
        case DISPLACEMENTMAP_CHANNEL_BLUE:
            return "B";
        case DISPLACEMENTMAP_CHANNEL_ALPHA:
            return "A";
        default:
            return nullptr;
    }
}

Inkscape::XML::Node *SPFeDisplacementMap::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, unsigned flags)
{
    auto filter_parent = SP_FILTER(parent);

    if (!repr) {
        repr = doc->createElement("svg:feDisplacementMap");
    }

    char const *in2_name = filter_parent->name_for_image(in2);

    if (!in2_name) {
        // This code is very similar to name_previous_out()
        SPObject *i = filter_parent->firstChild();

        // Find previous filter primitive
        while (i && i->getNext() != this) {
        	i = i->getNext();
        }

        if (i) {
            SPFilterPrimitive *i_prim = SP_FILTER_PRIMITIVE(i);
            in2_name = filter_parent->name_for_image(i_prim->image_out);
        }
    }

    if (in2_name) {
        repr->setAttribute("in2", in2_name);
    } else {
        g_warning("Unable to set in2 for feDisplacementMap");
    }

    repr->setAttributeSvgDouble("scale", scale);
    repr->setAttribute("xChannelSelector", get_channelselector_name(xChannelSelector));
    repr->setAttribute("yChannelSelector", get_channelselector_name(yChannelSelector));

    SPFilterPrimitive::write(doc, repr, flags);

    return repr;
}

std::unique_ptr<Inkscape::Filters::FilterPrimitive> SPFeDisplacementMap::build_renderer() const
{
    auto displacement_map = std::make_unique<Inkscape::Filters::FilterDisplacementMap>();
    build_renderer_common(displacement_map.get());

    displacement_map->set_input(1, in2);
    displacement_map->set_scale(scale);
    displacement_map->set_channel_selector(0, xChannelSelector);
    displacement_map->set_channel_selector(1, yChannelSelector);

    return displacement_map;
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
