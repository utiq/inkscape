// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Builder class that construct non-overlapping paths given an ObjectSet.
 *
 *
 *//*
 * Authors:
 * Osama Ahmad
 *
 * Copyright (C) 2021 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "booleans-nonintersecting.h"

#include <livarot/LivarotDefs.h>
#include <path/path-boolop.h>
#include <svg/svg.h>
#include <ui/icon-names.h>
#include <utility>

#include "helper/geom-pathstroke.h"

#include "object/object-set.h"

#include "ui/widget/canvas.h"

namespace Inkscape {

// TODO this is a duplicate code from selection-chemistry.cpp. refactor it.
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

void NonIntersectingPathsBuilder::prepare_input()
{
    // FIXME this causes a crash if the function ObjectSet::move is
    //  called with a dx or dy equals 0. this is because of an assertion
    //  in the function maybeDone. fix it later.
    // FIXME enable this and investigate why the program crashes when undoing.
    // DocumentUndo::ScopedInsensitive scopedInsensitive(set->document());

    // Ideally shouldn't be converting to paths?
    set->toCurves(true);
    set->ungroup_all();

    // TODO get rid of this line and use affines.
    //set->the_temporary_fix_for_the_transform_bug();

    set_parameters();
}

void NonIntersectingPathsBuilder::show_output(bool delete_original)
{
    draw_subitems(result_subitems);

    if (delete_original) {
        sp_selection_delete_impl(items);
    }
}

void NonIntersectingPathsBuilder::add_result_to_set()
{
    for (auto node : result_nodes) {
        set->add(node);
    }
}

void NonIntersectingPathsBuilder::perform_operation(SubItemOperation operation)
{
    if (set->isEmpty()) {
        return;
    }

    prepare_input();
    items_intersect = false;
    result_subitems = get_operation_result(operation);
}

void NonIntersectingPathsBuilder::perform_fracture()
{
    auto operation = [](SubItem & a, SubItem & b) { return a.fracture(b); };
    perform_operation(operation);
}

const std::vector<SubItem>& NonIntersectingPathsBuilder::get_result_subitems() const
{
    return result_subitems;
}

const std::vector<XML::Node*>& NonIntersectingPathsBuilder::get_result_nodes() const
{
    return result_nodes;
}

void NonIntersectingPathsBuilder::fracture(bool skip_undo)
{
    perform_fracture();
    show_output();
    add_result_to_set();

    if (!skip_undo && set->document()) {
        DocumentUndo::done(set->document(), "Fracture", INKSCAPE_ICON("path-fracture"));
    }
}

void NonIntersectingPathsBuilder::set_parameters()
{
    auto _items = set->items();
    // items will be placed in place
    //  of the first item in the selection.
    after = _items.front()->getRepr();
    parent = after->parent();

    items = std::vector<SPItem*>(_items.begin(), _items.end());
}

SPDesktop *NonIntersectingPathsBuilder::desktop()
{
    return set->desktop();
}

SPItem* SubItem::get_common_item(const SubItem &other_subitem) const
{
    for (auto item : other_subitem.items) {
        if (is_subitem_of(item)) {
            return item;
        }
    }
    return nullptr;
}

SPItem* SubItem::get_top_item() const
{
    SPItem *result = *items.begin();
    for (SPItem *item : items) {
        if (sp_item_repr_compare_position_bool(result, item)) {
            result = item;
        }
    }
    return result;
}

bool SubItem::is_virgin() const
{
    return items.size() == 1;
}

bool SubItem::operator<(const SubItem &other) const
{
    return sp_object_compare_position_bool(top_item, other.top_item);
}

std::vector<SubItem> SubItem::fracture(const SubItem &other_subitem)
{
    auto intersection_paths = sp_pathvector_boolop(paths, other_subitem.paths, bool_op_inters, fill_nonZero, fill_nonZero);
    auto intersection_top_item = other_subitem < *this ? this->top_item : other_subitem.top_item;
    SubItem intersection(intersection_paths, items, other_subitem.items, intersection_top_item);

    if (intersection.paths.empty()) {
        return {};
    }

    auto diff1_paths = sp_pathvector_boolop(paths, other_subitem.paths, bool_op_diff, fill_nonZero, fill_nonZero);
    SubItem diff1(diff1_paths, other_subitem.items, other_subitem.top_item);

    auto diff2_paths = sp_pathvector_boolop(other_subitem.paths, paths, bool_op_diff, fill_nonZero, fill_nonZero);
    SubItem diff2(diff2_paths, items, top_item);

    return {intersection, diff1, diff2};
}

std::vector<SubItem> split_non_intersecting_paths(std::vector<SubItem> &subitems)
{
    std::vector<SubItem> result;
    for (auto &path : subitems) {
        auto split = split_non_intersecting_paths(path.paths);
        for (auto &split_path : split) {
            result.emplace_back(split_path, path.items, path.top_item);
        }
    }
    return result;
}

std::vector<SubItem> NonIntersectingPathsBuilder::get_operation_result(SubItemOperation operation)
{
    std::vector<SubItem> result;
    int n = items.size();
    result.resize(n);

    for (int i = 0; i < n; i++) {
        result[i] = SubItem(items[i]->combined_pathvector(), {items[i]}, items[i]);
    }

    // TODO This should not be there.
    int max_operations_count = 5000;

    // result will grow as items are pushed
    // into it, so does result.size().
    for (int i = 0; i < result.size(); i++) {
        if (!max_operations_count) { break; }
        for (int j = 0; j < result.size(); j++) {
            if (!max_operations_count) { break; }

            if (i == j) { continue; }

            max_operations_count--;

            // if 2 subitems share at least one item, then
            //  they don't intersect by definition (since operations
            //  in this class yields non-intersecting paths). continue.
            SPItem *common_item = result[i].get_common_item(result[j]);
            if (common_item) { continue; }

            auto broken = operation(result[i], result[j]);
            remove_empty_subitems(broken);
            if (broken.empty()) { continue; } // don't intersect. continue.

            items_intersect = true;

            // the bigger index should be erased first.
            int bigger_index = (i > j) ? i : j;
            int smaller_index = (i > j) ? j : i;

            result.erase(result.begin() + bigger_index);
            result.erase(result.begin() + smaller_index);

            result.insert(result.end(), broken.begin(), broken.end());

            i--; // to cancel the next incrementation.

            break;
        }
    }

    return split_non_intersecting_paths(result);
}

void NonIntersectingPathsBuilder::draw_subitems(const std::vector<SubItem> &subitems)
{
    int n = subitems.size();
    result_nodes.resize(n);

    for (int i = 0; i < n; i++) {
        result_nodes[i] = write_path_xml(subitems[i].paths, subitems[i].top_item);
    }
}

void NonIntersectingPathsBuilder::remove_empty_subitems(std::vector<SubItem> &subitems)
{
    for (int i = 0; i < subitems.size(); i++) {
        if (subitems[i].paths.empty()) {
            subitems.erase(subitems.begin() + i);
            i--;
        }
    }
}

XML::Node *write_path_xml(const Geom::PathVector &path, const SPItem *to_copy_from, XML::Node *parent, XML::Node *after)
{
    Inkscape::XML::Node *repr = parent->document()->createElement("svg:path");
    repr->setAttribute("d", sp_svg_write_path(path));

    if (to_copy_from) {
        gchar *style = g_strdup(to_copy_from->getRepr()->attribute("style"));
        repr->setAttribute("style", style);
    }

    parent->addChild(repr, after);

    return repr;
}

XML::Node *write_path_xml(const Geom::PathVector &path, SPItem *to_copy_from)
{
    XML::Node *after = to_copy_from->getRepr();
    XML::Node *parent = after->parent();
    return write_path_xml(path, to_copy_from, parent, after);
}


};
