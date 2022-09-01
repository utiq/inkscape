// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_HELPER_GEOM_H
#define INKSCAPE_HELPER_GEOM_H

/**
 * @file
 * Specific geometry functions for Inkscape, not provided my lib2geom.
 */
/*
 * Author:
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *
 * Copyright (C) 2008 Johan Engelen
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <vector>
#include <2geom/forward.h>
#include <2geom/rect.h>
#include <2geom/affine.h>
#include "mathfns.h"

Geom::OptRect bounds_fast_transformed(Geom::PathVector const & pv, Geom::Affine const & t);
Geom::OptRect bounds_exact_transformed(Geom::PathVector const & pv, Geom::Affine const & t);

void pathv_matrix_point_bbox_wind_distance ( Geom::PathVector const & pathv, Geom::Affine const &m, Geom::Point const &pt,
                                             Geom::Rect *bbox, int *wind, Geom::Coord *dist,
                                             Geom::Coord tolerance, Geom::Rect const *viewbox);

bool is_intersecting(Geom::PathVector const&a, Geom::PathVector const&b);
bool pathvs_have_nonempty_overlap(Geom::PathVector const &a, Geom::PathVector const &b);

size_t count_pathvector_nodes(Geom::PathVector const &pathv );
size_t count_path_nodes(Geom::Path const &path);
bool pointInTriangle(Geom::Point const &p, Geom::Point const &p1, Geom::Point const &p2, Geom::Point const &p3);
Geom::PathVector pathv_to_linear_and_cubic_beziers( Geom::PathVector const &pathv );
Geom::PathVector pathv_to_linear( Geom::PathVector const &pathv, double maxdisp );
Geom::PathVector pathv_to_cubicbezier( Geom::PathVector const &pathv);
bool pathv_similar(Geom::PathVector const &apv, Geom::PathVector const &bpv, double precission = 0.001);
void recursive_bezier4(const double x1, const double y1, const double x2, const double y2, 
                       const double x3, const double y3, const double x4, const double y4,
                       std::vector<Geom::Point> &pointlist,
                       int level);
bool approx_dihedral(Geom::Affine const &affine, double eps = 0.0001);
std::pair<Geom::Affine, Geom::Rect> min_bounding_box(std::vector<Geom::Point> const &pts);

/// Returns signed area of triangle given by points; may be negative.
inline Geom::Coord triangle_area(Geom::Point const &p1, Geom::Point const &p2, Geom::Point const &p3)
{
    using Geom::X;
    using Geom::Y;
    return p1[X] * p2[Y] + p1[Y] * p3[X] + p2[X] * p3[Y] - p2[Y] * p3[X] - p1[Y] * p2[X] - p1[X] * p3[Y];
}

inline auto rounddown(Geom::IntPoint const &a, Geom::IntPoint const &b)
{
    using namespace Inkscape::Util;
    return Geom::IntPoint(rounddown(a.x(), b.x()), rounddown(a.y(), b.y()));
}

inline auto expandedBy(Geom::IntRect rect, int amount)
{
    rect.expandBy(amount);
    return rect;
}

inline auto distSq(Geom::IntPoint const &pt, Geom::IntRect const &rect)
{
    auto v = rect.clamp(pt) - pt;
    return v.x() * v.x() + v.y() * v.y();
}

inline Geom::IntPoint operator*(Geom::IntPoint const &a, int b)
{
    return Geom::IntPoint(a.x() * b, a.y() * b);
}

inline Geom::Point operator*(Geom::Point const &a, Geom::IntPoint const &b)
{
    return Geom::Point(a.x() * b.x(), a.y() * b.y());
}

inline auto operator*(Geom::IntPoint const &a, Geom::IntPoint const &b)
{
    return Geom::IntPoint(a.x() * b.x(), a.y() * b.y());
}

inline auto operator/(Geom::Point const &a, Geom::IntPoint const &b)
{
    return Geom::Point(a.x() / b.x(), a.y() / b.y());
}

inline auto operator/(Geom::IntPoint const &a, Geom::IntPoint const &b)
{
    return Geom::IntPoint(a.x() / b.x(), a.y() / b.y());
}

inline auto operator/(Geom::Point const &a, Geom::Point const &b)
{
    return Geom::Point(a.x() / b.x(), a.y() / b.y());
}

inline auto operator/(double a, Geom::Point const &b)
{
    return Geom::Point(a / b.x(), a / b.y());
}

inline auto absolute(Geom::Point const &a)
{
    return Geom::Point(std::abs(a.x()), std::abs(a.y()));
}

inline auto min(Geom::IntPoint const &a)
{
    return std::min(a.x(), a.y());
}

inline auto min(Geom::Point const &a)
{
    return std::min(a.x(), a.y());
}

inline auto max(Geom::IntPoint const &a)
{
    return std::max(a.x(), a.y());
}

inline auto max(Geom::Point const &a)
{
    return std::max(a.x(), a.y());
}

/// Regularisation operator for Geom::OptIntRect. Turns zero-area rectangles into empty optionals.
inline auto regularised(Geom::OptIntRect const &r)
{
    return r && !r->hasZeroArea() ? r : Geom::OptIntRect();
}

#endif // INKSCAPE_HELPER_GEOM_H

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
