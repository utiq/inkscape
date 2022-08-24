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

#pragma once

#include <vector>
#include <functional>
#include <set>
#include <2geom/pathvector.h>

class SPItem;
class SPDesktop;
class SPDocument;

namespace Inkscape {

class SubItem;
class ObjectSet;

namespace XML {
class Node;
}

XML::Node *write_path_xml(const Geom::PathVector &path, const SPItem *to_copy_from, XML::Node *parent, XML::Node *after = nullptr);
XML::Node *write_path_xml(const Geom::PathVector &path, SPItem *to_copy_from);

class SubItem;

class NonIntersectingPathsBuilder
{
    // TODO you might simplify this class so that it only constructs
    //  paths, and move the rest of the logic somewhere else.

private:

    XML::Node *parent;
    XML::Node *after;

    ObjectSet *set;
    std::vector<SPItem*> items;
    std::vector<SubItem> result_subitems;
    std::vector<XML::Node *> result_nodes;

    bool items_intersect = false;

public:

    NonIntersectingPathsBuilder(ObjectSet *set) : set(set) {}
    void fracture(bool skip_undo = false);
    void perform_fracture();
    void show_output(bool delete_original = true);
    void add_result_to_set();
    const std::vector<SubItem>& get_result_subitems() const;
    const std::vector<XML::Node*>& get_result_nodes() const;
    bool items_intersected() const { return items_intersect; };

private:

    using SubItemOperation = std::function<std::vector<SubItem>(SubItem &, SubItem &)>;

    void prepare_input();
    void perform_operation(SubItemOperation operation);
    std::vector<SubItem> get_operation_result(SubItemOperation operation);
    void draw_subitems(const std::vector<SubItem> &subitems);
    SPDesktop *desktop();
    void set_parameters();
    void remove_empty_subitems(std::vector<SubItem> &subitems);
};

/**
* When an item from the original ObjectSet is broken, each
* broken part is represented by the SubItem class. This
* class hold information such as the original items it originated
* from and the paths that the SubItem consists of.
**/
class SubItem
{
public:

    Geom::PathVector paths;
    std::set<SPItem*> items;
    SPItem *top_item;

    SubItem() {}

    SubItem(Geom::PathVector paths, std::set<SPItem*> items, SPItem *top_item)
        : paths(std::move(paths))
        , items(std::move(items))
        , top_item(top_item)
    {}

    SubItem(Geom::PathVector paths, const std::set<SPItem*> &items1, const std::set<SPItem*> &items2, SPItem *top_item)
        : SubItem(paths, items1, top_item)
    {
        items.insert(items2.begin(), items2.end());
    }

    bool is_subitem_of(SPItem* item) const { return items.find(item) != items.end(); }

    SPItem* get_common_item(const SubItem &other_subitem) const;
    SPItem* get_top_item() const;
    bool is_virgin() const;
    std::vector<SubItem> fracture(const SubItem &other_subitem);

    bool operator<(const SubItem &other) const;
};

}
