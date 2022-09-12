// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Rewrite of GridCanvasItem.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <2geom/line.h>

#include "canvas-item-grid.h"
#include "color.h"
#include "ui/widget/canvas.h"

enum Dim3 { X, Y, Z };

static int calculate_scaling_factor(double length, int major)
{
    int multiply = 1;
    int step = std::max(major, 1);
    int watchdog = 0;

    while (length * multiply < 8.0 && watchdog < 100) {
        multiply *= step;
        // First pass, go up to the major line spacing, then keep increasing by two.
        step = 2;
        watchdog++;
    }

    return multiply;
}

namespace Inkscape {

/**
 * Create a null control grid.
 */
CanvasItemGrid::CanvasItemGrid(CanvasItemGroup *group)
    : CanvasItem(group)
    , _origin(0, 0)
    , _spacing(1, 1)
    , _minor_color(GRID_DEFAULT_MINOR_COLOR)
    , _major_color(GRID_DEFAULT_MAJOR_COLOR)
    , _major_line_interval(5)
    , _dotted(false)
{
    _bounds = Geom::Rect(-Geom::infinity(), -Geom::infinity(), Geom::infinity(), Geom::infinity());

    request_update(); // Update affine
}

/**
 * Returns true if point p (in canvas units) is within tolerance (canvas units) distance of grid.
 */
bool CanvasItemGrid::contains(Geom::Point const &p, double tolerance)
{
    return false; // We're not pickable!
}

// Find the signed distance of a point to a line. The distance is negative if
// the point lies to the left of the line considering the line's versor.
static double signed_distance(Geom::Point const &point, Geom::Line const &line)
{
    return Geom::cross(point - line.initialPoint(), line.versor());
}

// Find intersections of line with rectangle. There should be zero or two.
// If line is degenerate with rectangle side, two corner points are returned.
static std::vector<Geom::Point> intersect_line_rectangle(Geom::Line const &line, Geom::Rect const &rect)
{
    std::vector<Geom::Point> intersections;
    for (unsigned i = 0; i < 4; ++i) {
        Geom::LineSegment side(rect.corner(i), rect.corner((i + 1) % 4));
        try {
            if (auto oc = Geom::intersection(line, side)) {
                intersections.emplace_back(line.pointAt(oc->ta));
            }
        } catch (Geom::InfiniteSolutions const &) {
            return { side.pointAt(0), side.pointAt(1) };
        }
    }
    return intersections;
}

void CanvasItemGrid::set_origin(Geom::Point const &point)
{
    _origin = point;
    request_update();
}

void CanvasItemGrid::set_major_color(guint32 color)
{
    _major_color = color;
    request_update();
}

void CanvasItemGrid::set_minor_color(guint32 color)
{
    _minor_color = color;
    request_update();
}

void CanvasItemGrid::set_dotted(bool b)
{
    _dotted = b;
    request_update();
}

void CanvasItemGrid::set_spacing(Geom::Point const &point)
{
    _spacing = point;
    request_update();
}

void CanvasItemGrid::set_major_line_interval(guint n)
{
    if (n < 1) return;

    _major_line_interval = n;
    request_update();
}

/** ====== Rectangular Grid  ====== **/

CanvasItemGridXY::CanvasItemGridXY(Inkscape::CanvasItemGroup *group)
    : CanvasItemGrid(group)
{}

void CanvasItemGridXY::update(Geom::Affine const &affine)
{
    if (_affine == affine && !_need_update) {
        // Nothing to do.
        return;
    }
    _affine = affine;
    _need_update = false;

    // Queue redraw of grid area
    ow = _origin * affine;
    sw[0] = Geom::Point(_spacing[0], 0) * affine.withoutTranslation();
    sw[1] = Geom::Point(0, _spacing[1]) * affine.withoutTranslation();

    // Find suitable grid spacing for display
    for (int dim = 0; dim < 2; dim++) {
        int const scaling_factor = calculate_scaling_factor(sw[dim].length(), _major_line_interval);
        sw[dim] *= scaling_factor;
        scaled[dim] = scaling_factor > 1;
    }

    request_redraw();
}

void CanvasItemGridXY::render(Inkscape::CanvasItemBuffer *buf)
{
    if (!buf) {
        std::cerr << "CanvasItemGridXY::render: No buffer!" << std::endl;
        return;
    }

    if (!_visible)
        return;

    // no_emphasize_when_zoomedout determines color (minor or major) when only major grid lines/dots shown.
    guint32 _empcolor = ( (scaled[Geom::X] || scaled[Geom::Y]) && _no_emp_when_zoomed_out ) ? _minor_color : _major_color;
    guint32 _color = _minor_color;

    buf->cr->save();
    buf->cr->translate(-buf->rect.left(), -buf->rect.top());
    buf->cr->set_line_width(1.0);
    buf->cr->set_line_cap(Cairo::LINE_CAP_SQUARE);

    // Adding a 2 px margin to the buffer rectangle to avoid missing intersections (in case of rounding errors, and due to adding 0.5 below)
    Geom::IntRect buf_rect_with_margin = buf->rect;
    buf_rect_with_margin.expandBy(2);

    for (unsigned dim = 0; dim < 2; ++dim) {

        // Construct an axis line through origin with direction normal to grid spacing.
        Geom::Line axis = Geom::Line::from_origin_and_vector( ow, sw[dim]       );
        Geom::Line orth = Geom::Line::from_origin_and_vector( ow, sw[(dim+1)%2] );

        double spacing = sw[(dim+1)%2].length();  // Spacing between grid lines.
        double dash    = sw[dim].length();        // Total length of dash pattern.

        // Find the minimum and maximum distances of the buffer corners from axis.
        double min =  Geom::infinity();
        double max = -Geom::infinity();
        for (unsigned c = 0; c < 4; ++c) {

            // We need signed distance... lib2geom offers only positive distance.
            double distance = signed_distance( buf_rect_with_margin.corner(c), axis );

            // Correct it for coordinate flips (inverts handedness).
            if (Geom::cross( axis.vector(), orth.vector() ) > 0 ) {
                distance = -distance;
            }

            if (distance < min)
                min = distance;
            if (distance > max)
                max = distance;
        }
        int start = floor( min/spacing );
        int stop  = floor( max/spacing );

        // Loop over grid lines that intersected buf rectangle.
        for (int j = start+1; j <= stop; ++j) {

            Geom::Line grid_line = Geom::make_parallel_line( ow + j * sw[(dim+1)%2], axis );

            std::vector<Geom::Point> x = intersect_line_rectangle( grid_line, buf_rect_with_margin );

            // If we have two intersections, grid line intersects buffer rectangle.
            if (x.size() == 2) {
                // Make sure lines are always drawn in the same direction (or dashes misplaced).
                Geom::Line vector(x[0], x[1]);
                if (Geom::dot( vector.vector(), axis.vector() ) < 0.0) {
                    std::swap(x[0], x[1]);
                }

                // Set up line. Need to use floor()+0.5 such that Cairo will draw us lines with a width of a single pixel, without any aliasing.
                // For this we need to position the lines at exactly half pixels, see https://www.cairographics.org/FAQ/#sharp_lines
                // Must be consistent with the pixel alignment of the guide lines, see CanvasItemGridXY::render(), and the drawing of the rulers
                buf->cr->move_to(floor(x[0][Geom::X]) + 0.5, floor(x[0][Geom::Y]) + 0.5);
                buf->cr->line_to(floor(x[1][Geom::X]) + 0.5, floor(x[1][Geom::Y]) + 0.5);

                // Set dash pattern and color.
                if (_dotted) {
                    // alpha needs to be larger than in the line case to maintain a similar
                    // visual impact but setting it to the maximal value makes the dots
                    // dominant in some cases. Solution, increase the alpha by a factor of
                    // 4. This then allows some user adjustment.
                    guint32 _empdot = (_empcolor & 0xff) << 2;
                    if (_empdot > 0xff)
                        _empdot = 0xff;
                    _empdot += (_empcolor & 0xffffff00);

                    guint32 _colordot = (_color & 0xff) << 2;
                    if (_colordot > 0xff)
                        _colordot = 0xff;
                    _colordot += (_color & 0xffffff00);

                    // Dash pattern must use spacing from orthogonal direction.
                    // Offset is to center dash on orthogonal lines.
                    double offset = fmod( signed_distance( x[0], orth ), sw[dim].length());
                    if (Geom::cross( axis.vector(), orth.vector() ) > 0 ) {
                        offset = -offset;
                    }

                    std::vector<double> dashes;
                    if (!scaled[dim] && (abs(j) % _major_line_interval) != 0) {
                        // Minor lines
                        dashes.push_back(1.0);
                        dashes.push_back(dash - 1.0);
                        offset -= 0.5;
                        buf->cr->set_source_rgba(SP_RGBA32_R_F(_colordot), SP_RGBA32_G_F(_colordot),
                                                 SP_RGBA32_B_F(_colordot), SP_RGBA32_A_F(_colordot));
                    } else {
                        // Major lines
                        dashes.push_back(3.0);
                        dashes.push_back(dash - 3.0);
                        offset -= 1.5; // Center dash on intersection.
                        buf->cr->set_source_rgba(SP_RGBA32_R_F(_empdot), SP_RGBA32_G_F(_empdot),
                                                 SP_RGBA32_B_F(_empdot), SP_RGBA32_A_F(_empdot));
                    }

                    buf->cr->set_line_cap(Cairo::LINE_CAP_BUTT);
                    buf->cr->set_dash(dashes, -offset);

                } else {

                    // Solid lines

                    // Set color
                    if (!scaled[dim] && (abs(j) % _major_line_interval) != 0) {
                        buf->cr->set_source_rgba(SP_RGBA32_R_F(_color), SP_RGBA32_G_F(_color),
                                                 SP_RGBA32_B_F(_color), SP_RGBA32_A_F(_color));
                    } else {
                        buf->cr->set_source_rgba(SP_RGBA32_R_F(_empcolor), SP_RGBA32_G_F(_empcolor),
                                                 SP_RGBA32_B_F(_empcolor), SP_RGBA32_A_F(_empcolor));
                    }
                }

                buf->cr->stroke();

            } else {
                std::cerr << "CanvasItemGridXY::render: Grid line doesn't intersect!" << std::endl;
            }
        }
    }

    buf->cr->restore();
}

/** ========= Axonometric Grids ======== */

/*
 * Current limits are: one axis (y-axis) is always vertical. The other two
 * axes are bound to a certain range of angles. The z-axis always has an angle
 * smaller than 90 degrees (measured from horizontal, 0 degrees being a line extending
 * to the right). The x-axis will always have an angle between 0 and 90 degrees.
 */
CanvasItemGridAxonom::CanvasItemGridAxonom(Inkscape::CanvasItemGroup *group)
    : CanvasItemGrid(group)
{
    angle_deg[X] = 30.0;
    angle_deg[Y] = 30.0;
    angle_deg[Z] = 0.0;

    angle_rad[X] = Geom::rad_from_deg(angle_deg[X]);
    angle_rad[Y] = Geom::rad_from_deg(angle_deg[Y]);
    angle_rad[Z] = Geom::rad_from_deg(angle_deg[Z]);

    tan_angle[X] = tan(angle_rad[X]);
    tan_angle[Y] = tan(angle_rad[Y]);
    tan_angle[Z] = tan(angle_rad[Z]);
}

void CanvasItemGridAxonom::update(Geom::Affine const &affine)
{
    if (_affine == affine && !_need_update) {
        // Nothing to do.
        return;
    }
    _affine = affine;
    _need_update = false;

    ow = _origin * affine;
    lyw = _spacing.y() * affine.descrim();

    int const scaling_factor = calculate_scaling_factor(lyw, _major_line_interval);
    lyw *= scaling_factor;
    scaled = scaling_factor > 1;

    spacing_ylines = lyw / (tan_angle[X] + tan_angle[Z]);
    lxw_x          = Geom::are_near(tan_angle[X], 0) ? Geom::infinity() : lyw / tan_angle[X];
    lxw_z          = Geom::are_near(tan_angle[Z], 0) ? Geom::infinity() : lyw / tan_angle[Z];

    if (_major_line_interval == 0) {
        scaled = true;
    }

    request_redraw();
}

// expects value given to be in degrees
void CanvasItemGridAxonom::set_angle_x(double deg)
{
    angle_deg[X] = std::clamp(deg, 0.0, 89.0); // setting to 90 and values close cause extreme slowdowns
    angle_rad[X] = Geom::rad_from_deg(angle_deg[X]);
    tan_angle[X] = tan(angle_rad[X]);

    request_update();
}

// expects value given to be in degrees
void CanvasItemGridAxonom::set_angle_z(double deg)
{
    angle_deg[Z] = std::clamp(deg, 0.0, 89.0); // setting to 90 and values close cause extreme slowdowns
    angle_rad[Z] = Geom::rad_from_deg(angle_deg[Z]);
    tan_angle[Z] = tan(angle_rad[Z]);

    request_update();
}

static void drawline(Inkscape::CanvasItemBuffer *buf, gint x0, gint y0, gint x1, gint y1, guint32 rgba)
{
    buf->cr->move_to(0.5 + x0, 0.5 + y0);
    buf->cr->line_to(0.5 + x1, 0.5 + y1);
    buf->cr->set_source_rgba(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba),
                             SP_RGBA32_B_F(rgba), SP_RGBA32_A_F(rgba));
    buf->cr->stroke();
}

static void vline(Inkscape::CanvasItemBuffer *buf, gint x, gint ys, gint ye, guint32 rgba)
{
    if ((x < buf->rect.left()) || (x >= buf->rect.right()))
        return;

    buf->cr->move_to(0.5 + x, 0.5 + ys);
    buf->cr->line_to(0.5 + x, 0.5 + ye);
    buf->cr->set_source_rgba(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba),
                             SP_RGBA32_B_F(rgba), SP_RGBA32_A_F(rgba));
    buf->cr->stroke();
}

/**
 * This function calls Cairo to render a line on a particular canvas buffer.
 * Coordinates are interpreted as SCREENcoordinates
 */
void CanvasItemGridAxonom::render(Inkscape::CanvasItemBuffer *buf)
{
    if (!buf) {
        std::cerr << "CanvasItemGridAxonom::render: No buffer!" << std::endl;
        return;
    }

    if (!_visible)
        return;

    //set correct coloring, depending preference (when zoomed out, always major coloring or minor coloring)
    guint32 _empcolor = (scaled && _no_emp_when_zoomed_out) ? _minor_color : _major_color;
    guint32 _color = _minor_color;

    buf->cr->save();
    buf->cr->translate(-buf->rect.left(), -buf->rect.top());
    buf->cr->set_line_width(1.0);
    buf->cr->set_line_cap(Cairo::LINE_CAP_SQUARE);

    // gc = gridcoordinates (the coordinates calculated from the grids origin 'grid->ow'.
    // sc = screencoordinates ( for example "buf->rect.left()" is in screencoordinates )
    // bc = buffer patch coordinates (x=0 on left side of page, y=0 on bottom of page)

    // tl = topleft
    auto const buf_tl_gc = buf->rect.min() - ow;

    // render the three separate line groups representing the main-axes

    // x-axis always goes from topleft to bottomright. (0,0) - (1,1)
    gdouble const xintercept_y_bc = (buf_tl_gc[Geom::X] * tan_angle[X]) - buf_tl_gc[Geom::Y] ;
    gdouble const xstart_y_sc = ( xintercept_y_bc - floor(xintercept_y_bc/lyw)*lyw ) + buf->rect.top();
    gint const xlinestart = round( (xstart_y_sc - buf_tl_gc[Geom::X]*tan_angle[X] - ow[Geom::Y]) / lyw );
    gint xlinenum = xlinestart;

    // lines starting on left side.

    for (gdouble y = xstart_y_sc; y < buf->rect.bottom(); y += lyw, xlinenum++) {
        gint const x0 = buf->rect.left();
        gint const y0 = round(y);
        gint x1 = x0 + round( (buf->rect.bottom() - y) / tan_angle[X] );
        gint y1 = buf->rect.bottom();
        if ( Geom::are_near(tan_angle[X],0.) ) {
            x1 = buf->rect.right();
            y1 = y0;
        }

        if (!scaled && (abs(xlinenum) % _major_line_interval) != 0) {
            drawline(buf, x0, y0, x1, y1, _color);
        } else {
            drawline(buf, x0, y0, x1, y1, _empcolor);
        }
    }
    // lines starting from top side
    if (!Geom::are_near(tan_angle[X],0.))
    {
        gdouble const xstart_x_sc = buf->rect.left() + (lxw_x - (xstart_y_sc - buf->rect.top()) / tan_angle[X]) ;
        xlinenum = xlinestart-1;
        for (gdouble x = xstart_x_sc; x < buf->rect.right(); x += lxw_x, xlinenum--) {
            gint const y0 = buf->rect.top();
            gint const y1 = buf->rect.bottom();
            gint const x0 = round(x);
            gint const x1 = x0 + round( (y1 - y0) / tan_angle[X] );

            if (!scaled && (abs(xlinenum) % _major_line_interval) != 0) {
                drawline(buf, x0, y0, x1, y1, _color);
            } else {
                drawline(buf, x0, y0, x1, y1, _empcolor);
            }
        }
    }

    // y-axis lines (vertical)
    gdouble const ystart_x_sc = floor (buf_tl_gc[Geom::X] / spacing_ylines) * spacing_ylines + ow[Geom::X];
    gint const  ylinestart = round((ystart_x_sc - ow[Geom::X]) / spacing_ylines);
    gint ylinenum = ylinestart;
    for (gdouble x = ystart_x_sc; x < buf->rect.right(); x += spacing_ylines, ylinenum++) {
        gint const x0 = floor(x); // sp_grid_vline will add 0.5 again, so we'll pre-emptively use floor()
        // instead of round() to avoid biasing the vertical lines to the right by half a pixel; see
        // CanvasItemGridXY::render() for more details
        if (!scaled && (abs(ylinenum) % _major_line_interval) != 0) {
            vline(buf, x0, buf->rect.top(), buf->rect.bottom() - 1, _color);
        } else {
            vline(buf, x0, buf->rect.top(), buf->rect.bottom() - 1, _empcolor);
        }
    }

    // z-axis always goes from bottomleft to topright. (0,1) - (1,0)
    gdouble const zintercept_y_bc = (buf_tl_gc[Geom::X] * -tan_angle[Z]) - buf_tl_gc[Geom::Y] ;
    gdouble const zstart_y_sc = ( zintercept_y_bc - floor(zintercept_y_bc/lyw)*lyw ) + buf->rect.top();
    gint const  zlinestart = round( (zstart_y_sc + buf_tl_gc[Geom::X]*tan_angle[Z] - ow[Geom::Y]) / lyw );
    gint zlinenum = zlinestart;
    // lines starting from left side
    gdouble next_y = zstart_y_sc;
    for (gdouble y = zstart_y_sc; y < buf->rect.bottom(); y += lyw, zlinenum++, next_y = y) {
        gint const x0 = buf->rect.left();
        gint const y0 = round(y);
        gint x1 = x0 + round( (y - buf->rect.top() ) / tan_angle[Z] );
        gint y1 = buf->rect.top();
        if ( Geom::are_near(tan_angle[Z],0.) ) {
            x1 = buf->rect.right();
            y1 = y0;
        }

        if (!scaled && (abs(zlinenum) % _major_line_interval) != 0) {
            drawline(buf, x0, y0, x1, y1, _color);
        } else {
            drawline(buf, x0, y0, x1, y1, _empcolor);
        }
    }
    // draw lines from bottom-up
    if (!Geom::are_near(tan_angle[Z],0.))
    {
        gdouble const zstart_x_sc = buf->rect.left() + (next_y - buf->rect.bottom()) / tan_angle[Z] ;
        for (gdouble x = zstart_x_sc; x < buf->rect.right(); x += lxw_z, zlinenum++) {
            gint const y0 = buf->rect.bottom();
            gint const y1 = buf->rect.top();
            gint const x0 = round(x);
            gint const x1 = x0 + round(buf->rect.height() / tan_angle[Z] );

            if (!scaled && (abs(zlinenum) % _major_line_interval) != 0) {
                drawline(buf, x0, y0, x1, y1, _color);
            } else {
                drawline(buf, x0, y0, x1, y1, _empcolor);
            }
        }
    }

    buf->cr->restore();
}

} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
