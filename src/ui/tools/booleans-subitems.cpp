// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * SubItem controls each fractured piece and links it to it's original items.
 *
 *//*
 * Authors:
 *   Martin Owens
 *   PBS
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "booleans-subitems.h"
#include "helper/geom-pathstroke.h"
#include "helper/geom.h"

#include <livarot/LivarotDefs.h>
#include <numeric>
#include <path/path-boolop.h>
#include <svg/svg.h>
#include <utility>

namespace Inkscape {

static Geom::PathVector clean_pathvector(const Geom::PathVector &pathv)
{
    Geom::PathVector ret;
    for (auto &path : pathv) {
        if (!is_path_empty(path)) {
            ret.push_back(std::move(path));
        }
    }
    return ret;
}

/**
 * Union operator, merges two subitems when requested by the user
 * The left hand side will retain priority for the resulting style
 * so you should be mindful of how you merge these shapes.
 */
SubItem &SubItem::operator+=(const SubItem &other)
{
    auto joined = sp_pathvector_boolop(_paths, other._paths, bool_op_union, fill_nonZero, fill_nonZero, true);
    // TODO: Remove clean_pathvector when boolops are fixed.
    _paths = clean_pathvector(joined);
    return *this;
}

static void add_paths(WorkItems &result, Geom::PathVector &&pathv, SPItem *item)
{
    // Imagine three rects overlapping each other. The middle rect will have two
    // corners outside of both others. These must be split apart for the fracture.
    // TODO: Remove use of path cleaning (end bool) when boolops are fixed
    for (auto &subpathv : split_non_intersecting_paths(std::move(pathv), true)) {
        // Using split_non_intersecting allows us to retain holes that a simple loop of Paths wouldn't,
        if (subpathv.size()) {
            result.emplace_back(std::make_shared<SubItem>(std::move(subpathv), item));
        }
    }
}

/**
 * Cut all the WorkItems with the given line and discard the line from the final shape.
 */
static WorkItems incremental_cut(WorkItems &&subitems, Geom::PathVector const &pathv)
{
    WorkItems result;
    result.reserve(subitems.size());

    for (auto &subitem : subitems) {
        auto pathv_cut = sp_pathvector_boolop(pathv, subitem->get_pathv(), bool_op_cut, fill_nonZero, fill_nonZero, true);
        if (pathv_cut == subitem->get_pathv()) {
            result.emplace_back(std::move(subitem));
            continue;
        }
        // Add_paths will break each part of the cut shape out
        for (auto &path : pathv_cut) {
            if (path.closed()) {
                add_paths(result, std::move(path), subitem->get_item());
            }
        }
    }
    return result;
}

/**
 * Create a fracture between two shapes such that their overlaps are their own
 * third shape added to the WorkItems collection.
 */
static WorkItems incremental_fracture(WorkItems &&subitems, SPItem *item, Geom::PathVector &&pathv)
{
    WorkItems result;
    result.reserve(subitems.size() + 1);

    for (auto &subitem : subitems) {
        auto intersection = sp_pathvector_boolop(subitem->get_pathv(), pathv, bool_op_inters, fill_nonZero, fill_nonZero, true);
        if (intersection.empty()) {
            result.emplace_back(std::move(subitem));
            continue;
        }

        auto subitem_uniq = sp_pathvector_boolop(pathv, subitem->get_pathv(), bool_op_diff, fill_nonZero, fill_nonZero, true);
        auto pathvec_uniq = sp_pathvector_boolop(subitem->get_pathv(), pathv, bool_op_diff, fill_nonZero, fill_nonZero, true);

        add_paths(result, std::move(intersection), subitem->get_item());
        add_paths(result, std::move(subitem_uniq), subitem->get_item());
        // TODO: Remove clean_pathvector when boolops are fixed.
        pathv = clean_pathvector(pathvec_uniq);
    }

    if (!pathv.empty()) {
        add_paths(result, std::move(pathv), item);
    }

    return result;
}

/**
 * Add a pathvector to the collection of items, cutting out any overlaps from the original items.
 */
static auto incremental_flatten(WorkItems &&subitems, SPItem *item, Geom::PathVector &&pathv)
{
    WorkItems result;
    result.reserve(subitems.size() + 1);

    for (auto &subitem : subitems) {
        if (!pathvs_have_nonempty_overlap(subitem->get_pathv(), pathv)) {
            result.emplace_back(std::move(subitem));
            continue;
        }

        auto subitem_uniq = sp_pathvector_boolop(pathv, subitem->get_pathv(), bool_op_diff, fill_nonZero, fill_nonZero, true);
        add_paths(result, std::move(subitem_uniq), subitem->get_item());
    }

    add_paths(result, std::move(pathv), item);

    return result;
}

/**
 * Take a list of items and fracture into a list of SubItems ready for
 * use inside the booleans interactive tool.
 */
WorkItems SubItem::build_mosaic(std::vector<SPItem*> &&items)
{
    std::vector<Geom::PathVector> lines;
    std::sort(items.begin(), items.end(), [] (auto a, auto b) {
        return sp_object_compare_position_bool(b, a);
    });

    WorkItems result;
    for (auto item : items) {
        auto pathv = item->combined_pathvector() * item->i2dt_affine();
        if (pathv.size() == 1 && !pathv[0].closed()) {
            lines.push_back(pathv);
            continue;
        }
        // Each item's path might actually be overlapping paths which must be
        // broken up so each sub-path is fractured individually.
        for (auto &path : pathv) {
            result = incremental_fracture(std::move(result), item, std::move(path));
        }
    }

    // Cut the fracture pattern by the detected lines
    for (auto line : lines) {
        result = incremental_cut(std::move(result), line);
    }

    // Currently unifiying the entire fracture, may be a better
    // way to generate holes in the future.
    auto holes = generate_holes(result);
    result.insert(result.end(), holes.begin(), holes.end());
    return result;
}

/**
 * Take a list of items and flatten into a list of SubItems.
 */
WorkItems SubItem::build_flatten(std::vector<SPItem*> &&items)
{
    std::sort(items.begin(), items.end(), [] (auto a, auto b) {
        return sp_object_compare_position_bool(a, b);
    });

    WorkItems result;

    for (auto item : items) {
        auto pathv = item->combined_pathvector() * item->i2dt_affine();
        if (pathv.size() == 1 && !pathv[0].closed()) {
            result = incremental_cut(std::move(result), pathv);
            result.emplace_back(std::make_shared<SubItem>(std::move(pathv), item));
            continue;
        }
        for (auto &path : pathv) {
            result = incremental_flatten(std::move(result), item, std::move(path));
        }
    }

    return result;
}

/**
 * Attempt to create shapes which fill-in the holes inside a fractured shape.
 * For example, the circle inside the letter 'O'. Because the shape isn't
 * generated from a source object, the subitem's _item is set to empty.
 */
WorkItems SubItem::generate_holes(const WorkItems &items)
{
    WorkItems ret;

    // 1. Generate a compete vector from the union of all items
    Geom::PathVector full_shape;

    for (auto item : items) {
        if (!full_shape.empty()) {
            full_shape = sp_pathvector_boolop(full_shape, item->_paths, bool_op_union, fill_nonZero, fill_nonZero, true);
        } else {
            full_shape = item->_paths;
        }
    }

    // 2. Create a rectangle vector path of the same size as the full shape.
    if (auto rect = full_shape.boundsExact()) {
        // 3. Remove the full_shape from the rectangle path vector (invert)
        auto rect_path = Geom::PathVector(Geom::Path(*rect));
        auto pathv = sp_pathvector_boolop(full_shape, rect_path, bool_op_diff, fill_nonZero, fill_nonZero, true);

        for (auto &new_path : pathv) {
            // This test could be done by seeing how large the gap is and using them if the gap
            // is small enough. For now we'll only use a shape if it's actually in the center.
            if (auto new_rect = new_path.boundsExact()) {
                if (   new_rect->top() != rect->top()
                    && new_rect->bottom() != rect->bottom()
                    && new_rect->left() != rect->left()
                    && new_rect->right() != rect->right()) {
                    // Shape does not touch the outer edge, so add as new SubItem
                    add_paths(ret, std::move(new_path), nullptr);
                }
            }
        }
    }

    return ret;
}

/**
 * Return true if this subitem contains the give point.
 */
bool SubItem::contains(const Geom::Point &pt) const
{
    return _paths.winding(pt) % 2 != 0;
}

} // namespace Inkscape
