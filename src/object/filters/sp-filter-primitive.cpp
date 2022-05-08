// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Superclass for all the filter primitives
 *
 */
/*
 * Authors:
 *   Kees Cook <kees@outflux.net>
 *   Niko Kiirala <niko@kiirala.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>

#include "sp-filter-primitive.h"
#include "attributes.h"
#include "display/nr-filter-primitive.h"
#include "style.h"

SPFilterPrimitive::SPFilterPrimitive()
{
    image_in = Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
    image_out = Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;

    // We must keep track if a value is set or not, if not set then the region defaults to 0%, 0%,
    // 100%, 100% ("x", "y", "width", "height") of the -> filter <- region. If set then
    // percentages are in terms of bounding box or viewbox, depending on value of "primitiveUnits"

    // NB: SVGLength.set takes prescaled percent values: 1 means 100%
    x.unset(SVGLength::PERCENT, 0, 0);
    y.unset(SVGLength::PERCENT, 0, 0);
    width.unset(SVGLength::PERCENT, 1, 0);
    height.unset(SVGLength::PERCENT, 1, 0);
}

SPFilterPrimitive::~SPFilterPrimitive() = default;

void SPFilterPrimitive::build(SPDocument *document, Inkscape::XML::Node *repr)
{
    readAttr(SPAttr::STYLE); // struct not derived from SPItem, we need to do this ourselves.
    readAttr(SPAttr::IN_);
    readAttr(SPAttr::RESULT);
    readAttr(SPAttr::X);
    readAttr(SPAttr::Y);
    readAttr(SPAttr::WIDTH);
    readAttr(SPAttr::HEIGHT);

    SPObject::build(document, repr);
}

void SPFilterPrimitive::release()
{
    SPObject::release();
}

void SPFilterPrimitive::set(SPAttr key, char const *value)
{
    int image_nr;
    switch (key) {
        case SPAttr::IN_:
            if (value) {
                image_nr = read_in(value);
            } else {
                image_nr = Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
            }
            if (image_nr != image_in) {
                image_in = image_nr;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;
        case SPAttr::RESULT:
            if (value) {
                image_nr = read_result(value);
            } else {
                image_nr = Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
            }
            if (image_nr != image_out) {
                image_out = image_nr;
                parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            }
            break;

        /* Filter primitive sub-region */
        case SPAttr::X:
            x.readOrUnset(value);
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::Y:
            y.readOrUnset(value);
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::WIDTH:
            width.readOrUnset(value);
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::HEIGHT:
            height.readOrUnset(value);
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
    }

    /* See if any parents need this value. */
    SPObject::set(key, value);
}

void SPFilterPrimitive::update(SPCtx *ctx, unsigned flags)
{
    auto ictx = static_cast<SPItemCtx*>(ctx);

    // Do here since we know viewport (Bounding box case handled during rendering)
    auto parent_filter = static_cast<SPFilter*>(parent);

    if (parent_filter->primitiveUnits == SP_FILTER_UNITS_USERSPACEONUSE) {
        calcDimsFromParentViewport(ictx, true);
    }

    SPObject::update(ctx, flags);
}

Inkscape::XML::Node* SPFilterPrimitive::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, unsigned flags)
{
    auto parent_filter = static_cast<SPFilter*>(parent);

    if (!repr) {
        repr = getRepr()->duplicate(doc);
    }

    gchar const *in_name = parent_filter->name_for_image(image_in);
    repr->setAttribute("in", in_name);

    gchar const *out_name = parent_filter->name_for_image(image_out);
    repr->setAttribute("result", out_name);

    /* Do we need to add x, y, width, height? */
    SPObject::write(doc, repr, flags);

    return repr;
}

int SPFilterPrimitive::read_in(char const *name)
{
    if (!name) {
        return Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
    }
    // TODO: are these case sensitive or not? (assumed yes)
    switch (name[0]) {
        case 'S':
            if (std::strcmp(name, "SourceGraphic") == 0)
                return Inkscape::Filters::NR_FILTER_SOURCEGRAPHIC;
            if (std::strcmp(name, "SourceAlpha") == 0)
                return Inkscape::Filters::NR_FILTER_SOURCEALPHA;
            if (std::strcmp(name, "StrokePaint") == 0)
                return Inkscape::Filters::NR_FILTER_STROKEPAINT;
            break;
        case 'B':
            if (std::strcmp(name, "BackgroundImage") == 0)
                return Inkscape::Filters::NR_FILTER_BACKGROUNDIMAGE;
            if (std::strcmp(name, "BackgroundAlpha") == 0)
                return Inkscape::Filters::NR_FILTER_BACKGROUNDALPHA;
            break;
        case 'F':
            if (std::strcmp(name, "FillPaint") == 0)
                return Inkscape::Filters::NR_FILTER_FILLPAINT;
            break;
    }

    auto parent_filter = static_cast<SPFilter*>(parent);
    int ret = parent_filter->get_image_name(name);
    if (ret >= 0) return ret;

    return Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
}

int SPFilterPrimitive::read_result(char const *name)
{
    auto parent_filter = static_cast<SPFilter*>(parent);
    int ret = parent_filter->get_image_name(name);
    if (ret >= 0) return ret;

    ret = parent_filter->set_image_name(name);
    if (ret >= 0) return ret;

    return Inkscape::Filters::NR_FILTER_SLOT_NOT_SET;
}

/**
 * Gives name for output of previous filter. Makes things clearer when 'this'
 * is a filter with two or more inputs. Returns the slot number of result
 * of previous primitive, or NR_FILTER_SOURCEGRAPHIC if this is the first
 * primitive.
 */
int SPFilterPrimitive::name_previous_out()
{
    auto parent_filter = static_cast<SPFilter*>(parent);
    SPObject *i = parent->firstChild();
    while (i && i->getNext() != this) {
        i = i->getNext();
    }
    if (i) {
        SPFilterPrimitive *i_prim = SP_FILTER_PRIMITIVE(i);
        if (i_prim->image_out < 0) {
            Glib::ustring name = parent_filter->get_new_result_name();
            int slot = parent_filter->set_image_name(name.c_str());
            i_prim->image_out = slot;
            //XML Tree is being directly used while it shouldn't be.
            i_prim->setAttributeOrRemoveIfEmpty("result", name);
            return slot;
        } else {
            return i_prim->image_out;
        }
    }
    return Inkscape::Filters::NR_FILTER_SOURCEGRAPHIC;
}

// Common initialization for filter primitives
void SPFilterPrimitive::build_renderer_common(Inkscape::Filters::FilterPrimitive *primitive) const
{
    g_assert(primitive);
    
    primitive->set_input(image_in);
    primitive->set_output(image_out);

    /* TODO: place here code to handle input images, filter area etc. */
    // We don't know current viewport or bounding box, this is wrong approach.
    primitive->set_subregion(x, y, width, height);

    // Give renderer access to filter properties
    primitive->setStyle(style);
}

/* Calculate the region taken up by this filter, given the previous region.
 *
 * @param current_region The original shape's region or previous primitive's calculate_region output.
 */
Geom::Rect SPFilterPrimitive::calculate_region(Geom::Rect const &region) const
{
    return region; // No change.
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
