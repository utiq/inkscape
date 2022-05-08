// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feComposite> implementation.
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

#include "composite.h"
#include "attributes.h"
#include "display/nr-filter.h"
#include "display/nr-filter-composite.h"
#include "object/sp-filter.h"
#include "svg/svg.h"
#include "util/numeric/converters.h"
#include "xml/repr.h"

void SPFeComposite::build(SPDocument *document, Inkscape::XML::Node *repr)
{
	SPFilterPrimitive::build(document, repr);

    readAttr(SPAttr::OPERATOR);
    readAttr(SPAttr::K1);
    readAttr(SPAttr::K2);
    readAttr(SPAttr::K3);
    readAttr(SPAttr::K4);
    readAttr(SPAttr::IN2);
}

static FeCompositeOperator read_operator(char const *value)
{
    if (!value) {
    	return COMPOSITE_DEFAULT;
    }

    if (std::strcmp(value, "over") == 0) {
    	return COMPOSITE_OVER;
    } else if (std::strcmp(value, "in") == 0) {
    	return COMPOSITE_IN;
    } else if (std::strcmp(value, "out") == 0) {
    	return COMPOSITE_OUT;
    } else if (std::strcmp(value, "atop") == 0) {
    	return COMPOSITE_ATOP;
    } else if (std::strcmp(value, "xor") == 0) {
    	return COMPOSITE_XOR;
    } else if (std::strcmp(value, "arithmetic") == 0) {
    	return COMPOSITE_ARITHMETIC;
    } else if (std::strcmp(value, "lighter") == 0) {
    	return COMPOSITE_LIGHTER;
    }

    std::cout << "Inkscape::Filters::FilterCompositeOperator: Unimplemented operator: " << value << std::endl;
    return COMPOSITE_DEFAULT;
}

void SPFeComposite::set(SPAttr key, char const *value)
{
    switch (key) {
        case SPAttr::OPERATOR: {
            auto n_op = ::read_operator(value);
            if (n_op != composite_operator) {
                composite_operator = n_op;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }

        case SPAttr::K1: {
            double n_k = value ? Inkscape::Util::read_number(value) : 0.0;
            if (n_k != k1) {
                k1 = n_k;
                if (composite_operator == COMPOSITE_ARITHMETIC)
                    parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }

        case SPAttr::K2: {
            double n_k = value ? Inkscape::Util::read_number(value) : 0.0;
            if (n_k != k2) {
                k2 = n_k;
                if (composite_operator == COMPOSITE_ARITHMETIC)
                    parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }

        case SPAttr::K3: {
            double n_k = value ? Inkscape::Util::read_number(value) : 0.0;
            if (n_k != k3) {
                k3 = n_k;
                if (composite_operator == COMPOSITE_ARITHMETIC)
                    parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        }

        case SPAttr::K4: {
            double n_k = value ? Inkscape::Util::read_number(value) : 0.0;
            if (n_k != k4) {
                k4 = n_k;
                if (composite_operator == COMPOSITE_ARITHMETIC)
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

Inkscape::XML::Node* SPFeComposite::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, guint flags)
{
    auto filter_parent = SP_FILTER(parent);

    if (!repr) {
        repr = doc->createElement("svg:feComposite");
    }

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
        g_warning("Unable to set in2 for feComposite");
    }

    char const *comp_op;

    switch (this->composite_operator) {
        case COMPOSITE_OVER:
            comp_op = "over"; break;
        case COMPOSITE_IN:
            comp_op = "in"; break;
        case COMPOSITE_OUT:
            comp_op = "out"; break;
        case COMPOSITE_ATOP:
            comp_op = "atop"; break;
        case COMPOSITE_XOR:
            comp_op = "xor"; break;
        case COMPOSITE_ARITHMETIC:
            comp_op = "arithmetic"; break;
        case COMPOSITE_LIGHTER:
            comp_op = "lighter"; break;
        default:
            comp_op = nullptr;
    }

    repr->setAttribute("operator", comp_op);

    if (composite_operator == COMPOSITE_ARITHMETIC) {
        repr->setAttributeSvgDouble("k1", k1);
        repr->setAttributeSvgDouble("k2", k2);
        repr->setAttributeSvgDouble("k3", k3);
        repr->setAttributeSvgDouble("k4", k4);
    } else {
        repr->removeAttribute("k1");
        repr->removeAttribute("k2");
        repr->removeAttribute("k3");
        repr->removeAttribute("k4");
    }

    SPFilterPrimitive::write(doc, repr, flags);

    return repr;
}

std::unique_ptr<Inkscape::Filters::FilterPrimitive> SPFeComposite::build_renderer() const
{
    auto composite = std::make_unique<Inkscape::Filters::FilterComposite>();
    build_renderer_common(composite.get());

    composite->set_operator(composite_operator);
    composite->set_input(1, in2);

    if (composite_operator == COMPOSITE_ARITHMETIC) {
        composite->set_arithmetic(k1, k2, k3, k4);
    }

    return composite;
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
