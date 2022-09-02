// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <utility>
#include <algorithm>

#include "booleans-nonintersecting.h"

#include "livarot/LivarotDefs.h"
#include "helper/geom-pathstroke.h"
#include "object/object-set.h"
#include "path/path-boolop.h"
#include "svg/svg.h"
#include "ui/icon-names.h"

namespace Inkscape {

template <typename T>
SPObject *write_path_xml(T const &path, SPObject const *style_from, SPObject *parent, SPObject *after)
{
    auto doc = parent->document;
    auto rdoc = doc->getReprDoc();
    auto repr = rdoc->createElement("svg:path");
    repr->setAttribute("d", sp_svg_write_path(path));

    if (style_from) {
        auto style = style_from->getRepr()->attribute("style");
        repr->setAttribute("style", style);
    }

    parent->addChild(repr, after->getRepr());

    Inkscape::GC::release(repr);
    return doc->getObjectByRepr(repr);
}

template <typename T>
SPObject *write_path_xml(T const &path, SPObject *after)
{
    return write_path_xml(path, after, after->parent, after);
}

template SPObject *write_path_xml<Geom::Path>      (Geom::Path       const&, SPObject const*, SPObject*, SPObject*);
template SPObject *write_path_xml<Geom::PathVector>(Geom::PathVector const&, SPObject const*, SPObject*, SPObject*);
template SPObject *write_path_xml<Geom::Path>      (Geom::Path       const&, SPObject*);
template SPObject *write_path_xml<Geom::PathVector>(Geom::PathVector const&, SPObject*);

// TODO: This is duplicated from selection-chemistry.cpp. Make the original accessible, and use it here.
static void sp_selection_delete_impl(std::vector<SPItem*> const &items, bool propagate = true, bool propagate_descendants = true)
{
    for (auto item : items) {
        sp_object_ref(item, nullptr);
    }
    for (auto item : items) {
        item->deleteObject(propagate, propagate_descendants);
        sp_object_unref(item, nullptr);
    }
}

/*
 * Split a collection of subitems into disconnected components, and remove empty subitems.
 */
static auto split_non_intersecting(std::vector<SubItem> &&subitems)
{
    std::vector<SubItem> result;

    for (auto &subitem : subitems) {
        auto paths = split_non_intersecting_paths(std::move(subitem.paths));
        auto const size = paths.size();

        int i = 0;
        for (auto &path : paths) {
            if (path.empty()) {
                continue; // should not be necessary
            }
            if (++i == size) { // last path
                result.emplace_back(std::move(path), std::move(subitem.items));
            } else {
                result.emplace_back(std::move(path), decltype(subitem.items)(subitem.items));
            }
        }
    }

    return result;
}

/*
 * Add an SPItem to a list of SubItems, fracturing any overlapping ones even further.
 */
static auto incremental_fracture(std::vector<SubItem> &&subitems, SPItem *item)
{
    std::vector<SubItem> result;
    result.reserve(subitems.size() + 1);

    auto pathvec = item->combined_pathvector();

    for (auto &subitem : subitems) {
        auto intersection = sp_pathvector_boolop(subitem.paths, pathvec, bool_op_inters, fill_nonZero, fill_nonZero, true);
        if (intersection.empty()) {
            result.emplace_back(std::move(subitem));
            continue;
        }

        auto subitem_uniq = sp_pathvector_boolop(pathvec, subitem.paths, bool_op_diff, fill_nonZero, fill_nonZero, true);
        auto pathvec_uniq = sp_pathvector_boolop(subitem.paths, pathvec, bool_op_diff, fill_nonZero, fill_nonZero, true);

        auto intersect_items = subitem.items;
        intersect_items.emplace_back(item);

        result.emplace_back(std::move(intersection), std::move(intersect_items));
        result.emplace_back(std::move(subitem_uniq), std::move(subitem.items));
        pathvec = std::move(pathvec_uniq);
    }

    result.emplace_back(SubItem(std::move(pathvec), { item }));

    return result;
}

std::vector<SubItem> fracture(std::vector<SPItem*> items)
{
    std::vector<SubItem> result;

    for (auto item : items) {
        result = incremental_fracture(std::move(result), item);
    }

    return split_non_intersecting(std::move(result));
}

/*
 *
 */

void NonIntersectingPathsBuilder::fracture(bool skip_undo)
{
    perform_fracture();
    show_output();
    add_result_to_set();

    if (!skip_undo && set->document()) {
        DocumentUndo::done(set->document(), "Fracture", INKSCAPE_ICON("path-fracture"));
    }
}

void NonIntersectingPathsBuilder::perform_fracture()
{
    if (set->isEmpty()) {
        return;
    }

    prepare_input();

    auto itemrange = set->items();
    items = std::vector<SPItem*>(itemrange.begin(), itemrange.end());

    std::sort(items.begin(), items.end(), [] (auto a, auto b) {
        return sp_object_compare_position_bool(b, a);
    });

    result_subitems = Inkscape::fracture(items);
}

void NonIntersectingPathsBuilder::prepare_input()
{
    // FIXME: This causes a crash if the function ObjectSet::move is
    // called with a dx or dy equals 0. This is because of an assertion
    // in the function maybeDone. Enable this and investigate why
    // program crashes when undoing.
    // DocumentUndo::ScopedInsensitive scopedInsensitive(set->document());

    // Ideally shouldn't be converting to paths?
    set->toCurves(true);
    set->ungroup_all(true);
}

void NonIntersectingPathsBuilder::show_output(bool delete_original)
{
    draw_subitems(result_subitems);

    if (delete_original) {
        sp_selection_delete_impl(items);
    }
}

void NonIntersectingPathsBuilder::draw_subitems(std::vector<SubItem> const &subitems)
{
    result_nodes.clear();
    result_nodes.reserve(subitems.size());

    for (auto &subitem : subitems) {
        result_nodes.emplace_back(write_path_xml(subitem.paths, subitem.top_item()));
    }
}

void NonIntersectingPathsBuilder::add_result_to_set()
{
    for (auto node : result_nodes) {
        set->add(node);
    }
}

} // namespace Inkscape
