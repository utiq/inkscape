// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_DISPLAY_NR_FILTER_H
#define INKSCAPE_DISPLAY_NR_FILTER_H

/*
 * SVG filters rendering
 *
 * Author:
 *   Niko Kiirala <niko@kiirala.com>
 *
 * Copyright (C) 2006 Niko Kiirala
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <memory>
#include <cairo.h>
#include "display/nr-filter-primitive.h"
#include "display/nr-filter-types.h"
#include "svg/svg-length.h"
#include "object/sp-filter-units.h"

namespace Inkscape {
class DrawingContext;
class DrawingItem;
class RenderContext;

namespace Filters {

class Filter final
{
public:
    /// Update any embedded DrawingItems prior to rendering.
    void update();

    /** Given background state from @a bgdc and an intermediate rendering from the surface
     * backing @a graphic, modify the contents of the surface backing @a graphic to represent
     * the results of filter rendering. @a bgarea and @a area specify bounding boxes
     * of both surfaces in world coordinates; Cairo contexts are assumed to be in default state
     * (0,0 = surface origin, no path, OVER operator) */
    int render(Inkscape::DrawingItem const *item, DrawingContext &graphic, DrawingContext *bgdc, RenderContext &rc) const;

    /**
     * Creates a new filter primitive under this filter object.
     * New primitive is placed so that it will be executed after all filter
     * primitives defined beforehand for this filter object.
     * Should this filter not have enough space for a new primitive, the filter
     * is enlarged to accommodate the new filter element. It may be enlarged by
     * more that one element.
     * Returns a handle (non-negative integer) to the filter primitive created.
     * Returns -1 if type is not a valid filter primitive type or a filter
     * primitive of such type cannot be created.
     */
    void add_primitive(std::unique_ptr<FilterPrimitive> primitive);

    /**
     * Removes all filter primitives from this filter.
     * All pointers to filter primitives inside this filter should be
     * considered invalid after calling this function.
     */
    void clear_primitives();

    /**
     * Sets the slot number 'slot' to be used as result from this filter.
     * If output is not set, the output from last filter primitive is used as
     * output from the filter.
     * It is an error to specify a pre-defined slot as 'slot'. Such call does
     * not have any effect to the state of filter or its primitives.
     */
    void set_output(int slot);

    void set_x(SVGLength const &length);
    void set_y(SVGLength const &length);
    void set_width(SVGLength const &length);
    void set_height(SVGLength const &length);

    /**
     * Sets the filter effects region.
     * Passing an unset length (length._set == false) as any of the parameters
     * results in that parameter not being changed.
     * Filter will not hold any references to the passed SVGLength object after
     * function returns.
     * If any of these parameters does not get set, the default value for that
     * parameter as defined in the SVG standard is used instead.
     */
    void set_region(SVGLength const &x, SVGLength const &y,
                    SVGLength const &width, SVGLength const &height);

    /**
     * Resets the filter effects region to its default value as defined
     * in SVG standard.
     */
    void reset_region();

    /**
     * Sets the width of intermediate images in pixels. If not set, suitable
     * resolution is determined automatically. If x_pixels is less than zero,
     * calling this function results in no changes to filter state.
     */
    void set_resolution(double x_pixels);

    /**
     * Sets the width and height of intermediate images in pixels. If not set,
     * suitable resolution is determined automatically. If either parameter is
     * less than zero, calling this function results in no changes to filter
     * state.
     */
    void set_resolution(double x_pixels, double y_pixels);

    /**
     * Resets the filter resolution to its default value, i.e. automatically
     * determined.
     */
    void reset_resolution();

    /**
     * Set the filterUnits-property. If not set, the default value of 
     * objectBoundingBox is used. If the parameter value is not a
     * valid enumeration value from SPFilterUnits, no changes to filter state
     * are made.
     */
    void set_filter_units(SPFilterUnits unit);

    /**
     * Set the primitiveUnits-property. If not set, the default value of
     * userSpaceOnUse is used. If the parameter value is not a valid
     * enumeration value from SPFilterUnits, no changes to filter state
     * are made.
     */
    void set_primitive_units(SPFilterUnits unit);

    /** 
     * Modifies the given area to accommodate for filters needing pixels
     * outside the rendered area.
     * When this function returns, area contains the area that needs
     * to be rendered so that after filtering, the original area is
     * drawn correctly.
     */
    void area_enlarge(Geom::IntRect &area, Inkscape::DrawingItem const *item) const;

    /**
     * Returns the filter effects area in user coordinate system.
     * The given bounding box should be a bounding box as specified in
     * SVG standard and in user coordinate system.
     */
    Geom::OptRect filter_effect_area(Geom::OptRect const &bbox) const;

    // returns cache score factor
    double complexity(Geom::Affine const &ctm) const;

    // says whether the filter accesses any of the background images
    bool uses_background() const;

    /** Creates a new filter with space for one filter element */
    Filter();

    /** 
     * Creates a new filter with space for n filter elements. If number of
     * filter elements is known beforehand, it's better to use this
     * constructor.
     */
    Filter(int n);

private:
    std::vector<std::unique_ptr<FilterPrimitive>> primitives;

    /** Amount of image slots used when this filter was rendered last time */
    int _slot_count;

    /** Image slot from which filter output should be read.
     * Negative values mean 'not set' */
    int _output_slot;

    SVGLength _region_x;
    SVGLength _region_y;
    SVGLength _region_width;
    SVGLength _region_height;

    /* x- and y-resolutions for filter rendering.
     * Negative values mean 'not set'.
     * If _y_pixels is set, _x_pixels should be set too. */
    double _x_pixels;
    double _y_pixels;

    SPFilterUnits _filter_units;
    SPFilterUnits _primitive_units;

    void _common_init();
    static int _resolution_limit(FilterQuality quality);
    std::pair<double, double> _filter_resolution(Geom::Rect const &area,
                                                 Geom::Affine const &trans,
                                                 FilterQuality q) const;
};

} // namespace Filters
} // namespace Inkscape

#endif // INKSCAPE_DISPLAY_NR_FILTER_H
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
