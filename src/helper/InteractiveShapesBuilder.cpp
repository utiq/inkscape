// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Interactive Shapes Builder.
 *
 *
 *//*
 * Authors:
 * Osama Ahmad
 *
 * Copyright (C) 2021 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "InteractiveShapesBuilder.h"
#include "NonIntersectingPathsBuilder.h"
#include <ui/icon-names.h>
#include "display/drawing-item.h"
#include "path/path-boolop.h"
#include "style.h"
#include "document.h"
#include "object/sp-item.h"
#include "useful-functions.h"
#include "selection.h"

namespace Inkscape {

// TODO this function is copied from selection-chemistry.cpp. Refactor later.
/*
 * Return a list of SPItems that are the children of 'list'
 *
 * list - source list of items to search in
 * desktop - desktop associated with the source list
 * exclude - list of items to exclude from result
 * onlyvisible - TRUE includes only items visible on canvas
 * onlysensitive - TRUE includes only non-locked items
 * ingroups - TRUE to recursively get grouped items children
 */
std::vector<SPItem*> &get_all_items(std::vector<SPItem*> &list, SPObject *from, SPDesktop *desktop, bool onlyvisible, bool onlysensitive, bool ingroups, std::vector<SPItem*> const &exclude)
{
    for (auto& child: from->children) {
        SPItem *item = dynamic_cast<SPItem *>(&child);
        if (item &&
            !desktop->isLayer(item) &&
            (!onlysensitive || !item->isLocked()) &&
            (!onlyvisible || !desktop->itemIsHidden(item)) &&
            (exclude.empty() || exclude.end() == std::find(exclude.begin(), exclude.end(), &child))
            )
        {
            list.insert(list.begin(),item);
        }

        if (ingroups || (item && desktop->isLayer(item))) {
            list = get_all_items(list, &child, desktop, onlyvisible, onlysensitive, ingroups, exclude);
        }
    }

    return list;
}

void delete_object(SPObject* item)
{
    sp_object_ref(item, nullptr);
    item->deleteObject(true, true);
    sp_object_unref(item, nullptr);
}

bool InteractiveShapesBuilder::is_started() const
{
    return started;
}

void Inkscape::InteractiveShapesBuilder::start(Inkscape::ObjectSet *set)
{
    desktop = set->desktop();
    document = set->document();

    if (is_started()) {
        std::cerr << "InteractiveShapesBuilder: already started. Resetting before starting again.\n";
        commit();
    }

    ungroup_all(set);

    NonIntersectingPathsBuilder builder(set);

    builder.perform_fracture();
    if (!builder.items_intersected()) {
        return;
    }

    auto &subitems = builder.get_result_subitems();

    selected_items = std::vector<SPItem*>(set->items().begin(), set->items().end());
    set->clear();

    get_all_items(not_selected_items, desktop->currentRoot(), desktop, true, true, false, selected_items);
    hide_items(not_selected_items);

    builder.show_output(false);
    hide_items(selected_items);

    auto &nodes = builder.get_result_nodes();
    int n = nodes.size();

    for (int i = 0; i < n; i++) {
        add_disabled_item(nodes[i], subitems[i]);
    }

    started = true;
    is_virgin = true;
}

std::vector<int> InteractiveShapesBuilder::get_subitems(const std::vector<SPItem*> &items)
{
    std::vector<int> result;
    for (auto item : items) {

        auto repr = item->getRepr();
        int item_id = get_id_from_node(repr);

        auto subitem = enabled.find(item_id);
        if (subitem == enabled.end()) {
            subitem = disabled.find(item_id);
            if (subitem == disabled.end()) {
                std::cerr << "InteractiveShapesBuilder::get_subitems: Item that is not in either the enabled or disabled sets is involved?...\n";
                continue;
            }
        }

        result.emplace_back(*subitem);

    }

    return result;
}

SubItem InteractiveShapesBuilder::get_union_subitem(const std::vector<int> &subitems)
{
    SubItem res_subitem = get_subitem_from_id(subitems.back());

    for (int i = 0; i < subitems.size() - 1; i++)
    {
        int subitem_id = subitems[i];
        auto &subitem = get_subitem_from_id(subitem_id);
        res_subitem.paths = sp_pathvector_boolop(res_subitem.paths, subitem.paths, bool_op_union, fill_nonZero, fill_nonZero);
        res_subitem.items.insert(subitem.items.begin(), subitem.items.end());
    }

    return res_subitem;
}

void InteractiveShapesBuilder::remove_items(const std::vector<SPItem*> &items)
{
    for (auto item : items) {

        auto repr = item->getRepr();
        int id = get_id_from_node(repr);

        remove_enabled_item(id);
        remove_disabled_item(id);

        delete_object(item);
    }
}

void InteractiveShapesBuilder::perform_union(ObjectSet *set, bool draw_result)
{
    if (set->isEmpty()) {
        return;
    }

    std::vector<SPItem*> items(set->items().begin(), set->items().end());

    auto subitems = get_subitems(items);
    SubItem subitem = get_union_subitem(subitems);

    XML::Node *node;

    if (draw_result) {
        node = draw_and_set_visible(subitem);
    } else {
        // TODO quick hack. change it later.
        static XML::Node *place_holder = 0x0;
        node = place_holder++;
    }

    int id = add_enabled_item(node, subitem);

    push_undo_command({id, std::move(subitems), draw_result});

    remove_items(items);
    is_virgin = false;
}

void InteractiveShapesBuilder::set_union(ObjectSet *set)
{
    perform_union(set, true);
}

void InteractiveShapesBuilder::set_delete(ObjectSet *set)
{
    perform_union(set, false);
}

void InteractiveShapesBuilder::commit()
{
    if (!is_started()) {
        return;
    }

    if (is_virgin) {
        return discard();
    }

    std::map<SPItem*, Geom::PathVector> final_paths;
    for (auto item : selected_items) {
        final_paths[item] = item->get_pathvector();
    }

    for (auto subitem_id : enabled) {
        auto &subitem = get_subitem_from_id(subitem_id);
        auto &items = subitem.items;
        for (auto item : items) {
            auto paths_it = final_paths.find(item);
            if (paths_it == final_paths.end()) {
                std::cerr << "InteractiveShapesBuilder: No Geom::PathVector is for the item " << item << ".\n";
                continue;
            }
            final_paths[item] = sp_pathvector_boolop(subitem.paths, paths_it->second, bool_op_diff, fill_nonZero, fill_nonZero);
        }
    }

    show_items(selected_items);

    for (auto item : selected_items) {
        for (auto sub_pathvec : split_non_intersecting_paths(final_paths[item])) {
            if (!sub_pathvec.empty()) {
                draw_on_canvas(sub_pathvec, item);
            }
        }
        delete_object(item);
    }

    reset_internals();

    // FIXME for some reason, this is not working properly.
    DocumentUndo::done(document, "Interactive Mode", INKSCAPE_ICON("interactive-builder"));
}

XML::Node* InteractiveShapesBuilder::get_node_from_id(int id)
{
    auto node = id_to_node.find(id);
    if (node == id_to_node.end()) {
        std::cerr << "InteractiveShapesBuilder::get_node_from_id: ID << " << id << " is not registered.\n";
    }
    return node->second;
}

int InteractiveShapesBuilder::get_id_from_node(XML::Node *node)
{
    auto id = node_to_id.find(node);
    if (id == node_to_id.end()) {
        std::cerr << "InteractiveShapesBuilder::get_node_from_id: Node << " << node << " is not registered.\n";
    }
    return id->second;
}

SubItem &InteractiveShapesBuilder::get_subitem_from_id(int id)
{
    auto subitem = id_to_subitem.find(id);
    if (subitem == id_to_subitem.end()) {
        std::cerr << "InteractiveShapesBuilder::get_node_from_id: ID << " << id << " is not registered.\n";
    }
    return subitem->second;
}

void InteractiveShapesBuilder::renew_node_id(XML::Node *node, int id)
{
    id_to_node[id] = node;
    node_to_id[node] = id;
}

int InteractiveShapesBuilder::add_disabled_item(XML::Node *node, int id)
{
    renew_node_id(node, id);
    disabled.insert(id);
    set_style_disabled(id);
    return id;
}

int InteractiveShapesBuilder::add_disabled_item(XML::Node *node, const SubItem &subitem)
{
    int id = last_id++;
    id_to_subitem[id] = subitem;
    return add_disabled_item(node, id);
}

void InteractiveShapesBuilder::remove_disabled_item(int id)
{
    auto result = disabled.find(id);
    if (result != disabled.end()) {
        restore_original_style(id);
        disabled.erase(result);
        auto node = get_node_from_id(id);
        id_to_node.erase(id);
        node_to_id.erase(node);
    }
}

int InteractiveShapesBuilder::add_enabled_item(XML::Node *node, int id)
{
    renew_node_id(node, id);
    enabled.insert(id);
    return id;
}

int InteractiveShapesBuilder::add_enabled_item(XML::Node *node, const SubItem &subitem)
{
    int id = last_id++;
    id_to_subitem[id] = subitem;
    return add_enabled_item(node, id);
}

void InteractiveShapesBuilder::remove_enabled_item(int id)
{
    auto result = enabled.find(id);
    if (result != enabled.end()) {
        enabled.erase(result);
        auto node = get_node_from_id(id);
        id_to_node.erase(id);
        node_to_id.erase(node);
    }
}

std::string get_disabled_stroke(const std::string &style)
{
    std::string fill_color = getSubAttribute(style, "fill");
    if (fill_color == "#000000") {
        return setSubAttribute(style, "stroke", "#ffffff");
    }
    return setSubAttribute(style, "stroke", "#000000");
}

void InteractiveShapesBuilder::set_style_disabled(int id)
{
    auto node = get_node_from_id(id);
    std::string original_style = node->attribute("style");
    original_styles[id] = original_style;
    std::string new_style = setSubAttribute(original_style, "opacity", "0.5");
    new_style = get_disabled_stroke(new_style);
    node->setAttribute("style", new_style);
    std::cout << "Original: " << original_style << "\nNew: " << new_style << "\n\n";
}

void InteractiveShapesBuilder::restore_original_style(int id)
{
    auto style = original_styles.find(id);
    if (style == original_styles.end()) {
//        std::cerr << "InteractiveShapeBuilder: The node " << node << " doesn't have its original style stored.\n";
        return;
    }
    auto node = get_node_from_id(id);
    node->setAttribute("style", style->second);
}

void InteractiveShapesBuilder::hide_items(const std::vector<SPItem*> &items)
{
    for (auto item : items) {
        item->setHidden(true);
    }
}

void InteractiveShapesBuilder::show_items(const std::vector<SPItem*> &items)
{
    for (auto item : items) {
        item->setHidden(false);
    }
}

void InteractiveShapesBuilder::reset_internals()
{
    for (auto node_id : disabled) {
        auto repr = get_node_from_id(node_id);
        auto object = document->getObjectByRepr(repr);
        delete_object(object);
    }

    show_items(not_selected_items);

    last_id = 0;
    started = false;
    is_virgin = true;
    enabled.clear();
    disabled.clear();
    selected_items.clear();
    not_selected_items.clear();
    original_styles.clear();
    id_to_subitem.clear();
    id_to_node.clear();
    node_to_id.clear();
    while (!undo_stack.empty()) {
        undo_stack.pop();
    }
    while (!redo_stack.empty()) {
        redo_stack.pop();
    }
}

void InteractiveShapesBuilder::reset()
{
    // TODO do this in a better way
    while (!undo_stack.empty()) {
        undo();
    }
    while (!redo_stack.empty()) {
        redo_stack.pop();
    }
}

void InteractiveShapesBuilder::discard()
{
    for (auto node_id : enabled) {
        auto repr = get_node_from_id(node_id);
        auto object = document->getObjectByRepr(repr);
        if (object) {
            delete_object(object);
        } else {
            // this is a node that is not drawn (a deleted node).
        }
    }

    show_items(selected_items);
    reset_internals();
}

XML::Node* InteractiveShapesBuilder::draw_and_set_visible(const SubItem &subitem)
{
    auto node = draw_on_canvas(subitem.paths, subitem.top_item);
    // TODO find a better way to do this.
    auto item = dynamic_cast<SPItem*>(document->getObjectByRepr(node));
    item->setHidden(false);
    return node;
}

void InteractiveShapesBuilder::push_undo_command(const UnionCommand &command)
{
    undo_stack.push(std::move(command));
    while (!redo_stack.empty()) {
        redo_stack.pop();
    }
}

void InteractiveShapesBuilder::undo()
{
    if (undo_stack.empty()) {
        return;
    }

    auto command = undo_stack.top();
    undo_stack.pop();

    int node_id = command.result;

    if (command.draw_result) {
        auto node = get_node_from_id(node_id);
        auto object = document->getObjectByRepr(node);
        if (object) {
            delete_object(object);
        } else {
            std::cerr << "InteractiveShapesBuilder::undo: Node " << node << " doesn't have an object...\n";
        }
    }

    remove_enabled_item(node_id);
    remove_disabled_item(node_id);

    for (auto &id : command.operands) {
        auto node = draw_and_set_visible(id_to_subitem[id]);

        // TODO quick hack. change it later.
        // if the node exits in original_styles, then it was disabled.
        if (original_styles.find(id) != original_styles.end()) {
            add_disabled_item(node, id);
        } else {
            add_enabled_item(node, id);
        }

    }

    redo_stack.push(command);

    if (undo_stack.empty()) {
        is_virgin = true;
    }
}

void InteractiveShapesBuilder::redo()
{
    if (redo_stack.empty()) {
        return;
    }

    auto command = redo_stack.top();
    redo_stack.pop();

    if (command.draw_result) {
        int id = command.result;
        auto &subitem = get_subitem_from_id(id);
        auto node = draw_and_set_visible(subitem);
        add_enabled_item(node, id);
    }

    for (auto &id : command.operands)
    {
        auto node = get_node_from_id(id);
        auto object = document->getObjectByRepr(node);
        if (object) {
            delete_object(object);
        } else {
            std::cerr << "InteractiveShapesBuilder::redo: Node " << node << " doesn't have an object...\n";
        }

        remove_enabled_item(id);
        remove_disabled_item(id);
    }

    undo_stack.push(command);
    is_virgin = false;
}

}