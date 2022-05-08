// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <filter> implementation.
 */
/*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Niko Kiirala <niko@kiirala.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006,2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "sp-filter.h"

#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include <2geom/transforms.h>
#include <glibmm.h>

#include "attributes.h"
#include "bad-uri-exception.h"
#include "display/nr-filter.h"
#include "document.h"
#include "filters/sp-filter-primitive.h"
#include "sp-filter-reference.h"
#include "uri.h"
#include "xml/repr.h"

SPFilter::SPFilter()
    : filterUnits(SP_FILTER_UNITS_OBJECTBOUNDINGBOX)
    , filterUnits_set(FALSE)
    , primitiveUnits(SP_FILTER_UNITS_USERSPACEONUSE)
    , primitiveUnits_set(FALSE)
    , _refcount(0)
    , _image_number_next(0)
{
    href = std::make_unique<SPFilterReference>(this);

    // Gets called when the filter is (re)attached to another filter.
    href->changedSignal().connect([this] (SPObject *old_ref, SPObject *ref) {
        if (old_ref) {
            modified_connection.disconnect();
        }

        if (SP_IS_FILTER(ref) && ref != this) {
            modified_connection = ref->connectModified([this] (SPObject*, unsigned) {
                requestModified(SP_OBJECT_MODIFIED_FLAG);
            });
        }

        requestModified(SP_OBJECT_MODIFIED_FLAG);
    });

    x = 0;
    y = 0;
    width = 0;
    height = 0;
    auto_region = true;
}

SPFilter::~SPFilter() = default;

void SPFilter::build(SPDocument *document, Inkscape::XML::Node *repr)
{
    // Read values of key attributes from XML nodes into object.
    readAttr(SPAttr::STYLE); // struct not derived from SPItem, we need to do this ourselves.
    readAttr(SPAttr::FILTERUNITS);
    readAttr(SPAttr::PRIMITIVEUNITS);
    readAttr(SPAttr::X);
    readAttr(SPAttr::Y);
    readAttr(SPAttr::WIDTH);
    readAttr(SPAttr::HEIGHT);
    readAttr(SPAttr::AUTO_REGION);
    readAttr(SPAttr::FILTERRES);
    readAttr(SPAttr::XLINK_HREF);
    _refcount = 0;

    SPObject::build(document, repr);

    document->addResource("filter", this);
}

void SPFilter::release()
{
    document->removeResource("filter", this);

    // release href
    if (href) {
        modified_connection.disconnect();
        href->detach();
        href.reset();
    }

    _image_name.clear();

    SPObject::release();
}

void SPFilter::set(SPAttr key, gchar const *value)
{
    switch (key) {
        case SPAttr::FILTERUNITS:
            if (value) {
                if (!std::strcmp(value, "userSpaceOnUse")) {
                    filterUnits = SP_FILTER_UNITS_USERSPACEONUSE;
                } else {
                    filterUnits = SP_FILTER_UNITS_OBJECTBOUNDINGBOX;
                }
                filterUnits_set = TRUE;
            } else {
                filterUnits = SP_FILTER_UNITS_OBJECTBOUNDINGBOX;
                filterUnits_set = FALSE;
            }
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::PRIMITIVEUNITS:
            if (value) {
                if (!std::strcmp(value, "objectBoundingBox")) {
                    primitiveUnits = SP_FILTER_UNITS_OBJECTBOUNDINGBOX;
                } else {
                    primitiveUnits = SP_FILTER_UNITS_USERSPACEONUSE;
                }
                primitiveUnits_set = TRUE;
            } else {
                primitiveUnits = SP_FILTER_UNITS_USERSPACEONUSE;
                primitiveUnits_set = FALSE;
            }
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
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
        case SPAttr::AUTO_REGION:
            auto_region = !value || std::strcmp(value, "false");
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::FILTERRES:
            filterRes.set(value);
            requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SPAttr::XLINK_HREF:
            if (value) {
                try {
                    href->attach(Inkscape::URI(value));
                } catch (Inkscape::BadURIException const &e) {
                    g_warning("%s", e.what());
                    href->detach();
                }
            } else {
                href->detach();
            }
            break;
        default:
            // See if any parents need this value.
            SPObject::set(key, value);
            break;
    }
}

/**
 * Returns the number of references to the filter.
 */
unsigned SPFilter::getRefCount()
{
    // NOTE: this is currently updated by sp_style_filter_ref_changed() in style.cpp
    return _refcount;
}

void SPFilter::modified(unsigned flags)
{
    // We are not an LPE, do not update filter regions on load.
    if (flags & SP_OBJECT_MODIFIED_FLAG) {
        update_filter_all_regions();
    }
}

void SPFilter::update(SPCtx *ctx, unsigned flags)
{
    if (flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG)) {
        auto ictx = static_cast<SPItemCtx*>(ctx);

        // Do here since we know viewport (Bounding box case handled during rendering)
        // Note: This only works for root viewport since this routine is not called after
        // setting a new viewport. A true fix requires a strategy like SPItemView or SPMarkerView.
        if (filterUnits == SP_FILTER_UNITS_USERSPACEONUSE) {
            calcDimsFromParentViewport(ictx, true);
        }
    }

    // Update filter primitives in order to update filter primitive area
    // (SPObject::ActionUpdate is not actually used)
    unsigned childflags = flags;

    if (flags & SP_OBJECT_MODIFIED_FLAG) {
        childflags |= SP_OBJECT_PARENT_MODIFIED_FLAG;
    }
    childflags &= SP_OBJECT_MODIFIED_CASCADE;
    auto l = childList(true, SPObject::ActionUpdate);
    for (SPObject *child : l) {
        if (SP_IS_FILTER_PRIMITIVE(child)) {
            child->updateDisplay(ctx, childflags);
        }
        sp_object_unref(child);
    }

    SPObject::update(ctx, flags);
}

Inkscape::XML::Node *SPFilter::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, unsigned flags)
{
    // Original from sp-item-group.cpp
    if (flags & SP_OBJECT_WRITE_BUILD) {
        if (!repr) {
            repr = doc->createElement("svg:filter");
        }

        std::vector<Inkscape::XML::Node *> l;
        for (auto &child : children) {
            auto crepr = child.updateRepr(doc, nullptr, flags);
            if (crepr) {
                l.push_back(crepr);
            }
        }

        for (auto i = l.rbegin(); i != l.rend(); ++i) {
            repr->addChild(*i, nullptr);
            Inkscape::GC::release(*i);
        }
    } else {
        for (auto &child : children) {
            child.updateRepr(flags);
        }
    }

    if ((flags & SP_OBJECT_WRITE_ALL) || filterUnits_set) {
        switch (filterUnits) {
            case SP_FILTER_UNITS_USERSPACEONUSE:
                repr->setAttribute("filterUnits", "userSpaceOnUse");
                break;
            default:
                repr->setAttribute("filterUnits", "objectBoundingBox");
                break;
        }
    }

    if ((flags & SP_OBJECT_WRITE_ALL) || primitiveUnits_set) {
        switch (primitiveUnits) {
            case SP_FILTER_UNITS_OBJECTBOUNDINGBOX:
                repr->setAttribute("primitiveUnits", "objectBoundingBox");
                break;
            default:
                repr->setAttribute("primitiveUnits", "userSpaceOnUse");
                break;
        }
    }

    if (x._set) {
        repr->setAttributeSvgDouble("x", x.computed);
    } else {
        repr->removeAttribute("x");
    }

    if (y._set) {
        repr->setAttributeSvgDouble("y", y.computed);
    } else {
        repr->removeAttribute("y");
    }

    if (width._set) {
        repr->setAttributeSvgDouble("width", width.computed);
    } else {
        repr->removeAttribute("width");
    }

    if (height._set) {
        repr->setAttributeSvgDouble("height", height.computed);
    } else {
        repr->removeAttribute("height");
    }

    if (filterRes.getNumber() >= 0) {
        auto tmp = filterRes.getValueString();
        repr->setAttribute("filterRes", tmp);
    } else {
        repr->removeAttribute("filterRes");
    }

    if (href->getURI()) {
        auto uri_string = href->getURI()->str();
        repr->setAttributeOrRemoveIfEmpty("xlink:href", uri_string);
    }

    SPObject::write(doc, repr, flags);

    return repr;
}

/**
 * Update the filter's region based on it's detectable href links
 *
 * Automatic region only updated if auto_region is false
 * and filterUnits is not UserSpaceOnUse
 */
void SPFilter::update_filter_all_regions()
{
    if (!auto_region || filterUnits == SP_FILTER_UNITS_USERSPACEONUSE) {
        return;
    }

    // Combine all items into one region for updating.
    Geom::OptRect opt_r;
    for (auto &obj : hrefList) {
        auto item = dynamic_cast<SPItem *>(obj);
        opt_r.unionWith(get_automatic_filter_region(item));
    }
    if (opt_r) {
        Geom::Rect region = *opt_r;
        set_filter_region(region.left(), region.top(), region.width(), region.height());
    }
}

/**
 * Update the filter region based on the object's bounding box
 *
 * @param item - The item who's coords are used as the basis for the area.
 */
void SPFilter::update_filter_region(SPItem *item)
{
    if (!auto_region || filterUnits == SP_FILTER_UNITS_USERSPACEONUSE) {
        return; // No adjustment for dead box
    }

    auto region = get_automatic_filter_region(item);

    // Set the filter region into this filter object
    set_filter_region(region.left(), region.top(), region.width(), region.height());
}

/**
 * Generate a filter region based on the item and return it.
 *
 * @param item - The item who's coords are used as the basis for the area.
 */
Geom::Rect SPFilter::get_automatic_filter_region(SPItem *item)
{
    // Calling bbox instead of visualBound() avoids re-requesting filter regions
    Geom::OptRect v_box = item->bbox(Geom::identity(), SPItem::VISUAL_BBOX);
    Geom::OptRect g_box = item->bbox(Geom::identity(), SPItem::GEOMETRIC_BBOX);
    if (!v_box || !g_box) {
        return Geom::Rect(); // No adjustment for dead box
    }

    // Because the filter box is in geometric bounding box units, it must ALSO
    // take account of the visualBox, so even if the filter does NOTHING to the
    // size of an object, we must add the difference between the geometric and
    // visual boxes ourselves or find them cut off by renderers of all kinds.
    Geom::Rect inbox = *g_box;
    Geom::Rect outbox = *v_box;
    for (auto &primitive_obj : children) {
        auto primitive = dynamic_cast<SPFilterPrimitive *>(&primitive_obj);
        if (primitive) {
            // Update the region with the primitive's options
            outbox = primitive->calculate_region(outbox);
        }
    }

    // Include the original visual bounding-box in the result
    outbox.unionWith(v_box);
    // Scale outbox to width/height scale of input, this scales the geometric
    // into the visual bounding box requiring any changes to it to re-run this.
    outbox *= Geom::Translate(-inbox.left(), -inbox.top());
    outbox *= Geom::Scale(1.0 / inbox.width(), 1.0 / inbox.height());
    return outbox;
}

/**
 * Set the filter region attributes from a bounding box
 */
void SPFilter::set_filter_region(double x, double y, double width, double height)
{
    if (width != 0 && height != 0) {
        // TODO: set it in UserSpaceOnUse instead?
        auto repr = getRepr();
        repr->setAttributeSvgDouble("x", x);
        repr->setAttributeSvgDouble("y", y);
        repr->setAttributeSvgDouble("width", width);
        repr->setAttributeSvgDouble("height", height);
    }
}

/**
 * Check each filter primitive for conflicts with this object.
 */
bool SPFilter::valid_for(SPObject const *obj) const
{
    for (auto &primitive_obj : children) {
        auto primitive = dynamic_cast<SPFilterPrimitive const *>(&primitive_obj);
        if (primitive && !primitive->valid_for(obj)) {
            return false;
        }
    }
    return true;
}

void SPFilter::child_added(Inkscape::XML::Node *child, Inkscape::XML::Node *ref)
{
    SPObject::child_added(child, ref);
    requestModified(SP_OBJECT_MODIFIED_FLAG);
}

void SPFilter::remove_child(Inkscape::XML::Node *child)
{
    SPObject::remove_child(child);
    requestModified(SP_OBJECT_MODIFIED_FLAG);
}

void SPFilter::build_renderer(Inkscape::Filters::Filter *nr_filter) const
{
    g_assert(nr_filter);

    nr_filter->set_filter_units(filterUnits);
    nr_filter->set_primitive_units(primitiveUnits);
    nr_filter->set_x(x);
    nr_filter->set_y(y);
    nr_filter->set_width(width);
    nr_filter->set_height(height);

    if (filterRes.getNumber() >= 0) {
        if (filterRes.getOptNumber() >= 0) {
            nr_filter->set_resolution(filterRes.getNumber(), filterRes.getOptNumber());
        } else {
            nr_filter->set_resolution(filterRes.getNumber());
        }
    }

    nr_filter->clear_primitives();
    for (auto &primitive_obj: children) {
        if (auto primitive = SP_FILTER_PRIMITIVE(&primitive_obj)) {
            nr_filter->add_primitive(primitive->build_renderer());
        }
    }
}

int SPFilter::primitive_count() const
{
    int count = 0;

    for (auto const &primitive_obj : children) {
        if (SP_IS_FILTER_PRIMITIVE(&primitive_obj)) {
            count++;
        }
    }

    return count;
}

int SPFilter::get_image_name(gchar const *name) const
{
    auto const result = _image_name.find(name);
    if (result == _image_name.end()) return -1;
    return result->second;
}

int SPFilter::set_image_name(gchar const *name)
{
    int value = _image_number_next++;
    auto [it, ret] = _image_name.try_emplace(name, value);
    return it->second;
}

gchar const *SPFilter::name_for_image(int image) const
{
    switch (image) {
        case Inkscape::Filters::NR_FILTER_SOURCEGRAPHIC:
            return "SourceGraphic";
            break;
        case Inkscape::Filters::NR_FILTER_SOURCEALPHA:
            return "SourceAlpha";
            break;
        case Inkscape::Filters::NR_FILTER_BACKGROUNDIMAGE:
            return "BackgroundImage";
            break;
        case Inkscape::Filters::NR_FILTER_BACKGROUNDALPHA:
            return "BackgroundAlpha";
            break;
        case Inkscape::Filters::NR_FILTER_STROKEPAINT:
            return "StrokePaint";
            break;
        case Inkscape::Filters::NR_FILTER_FILLPAINT:
            return "FillPaint";
            break;
        case Inkscape::Filters::NR_FILTER_SLOT_NOT_SET:
        case Inkscape::Filters::NR_FILTER_UNNAMED_SLOT:
            return nullptr;
            break;
        default:
            for (auto const &i : _image_name) {
                if (i.second == image) {
                    return i.first.c_str();
                }
            }
    }
    return nullptr;
}

Glib::ustring SPFilter::get_new_result_name() const
{
    int largest = 0;

    for (auto const &primitive_obj : children) {
        if (SP_IS_FILTER_PRIMITIVE(&primitive_obj)) {
            auto repr = primitive_obj.getRepr();
            auto result = repr->attribute("result");
            if (result) {
                int index;
                if (std::sscanf(result, "result%5d", &index) == 1) {
                    if (index > largest) {
                        largest = index;
                    }
                }
            }
        }
    }

    return "result" + Glib::Ascii::dtostr(largest + 1);
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
