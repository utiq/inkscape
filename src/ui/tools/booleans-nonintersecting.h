// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Builder class that constructs non-overlapping paths given an ObjectSet.
 */
/*
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLS_BOOLEANS_NONINTERSECTING
#define INKSCAPE_UI_TOOLS_BOOLEANS_NONINTERSECTING

#include <vector>
#include <2geom/pathvector.h>

class SPItem;
class SPObject;
class SPDesktop;
class SPDocument;

namespace Inkscape {

class ObjectSet;

/**
 * Add a path to a document as a child of \a parent.
 * \param style_from The object whose style to copy. (If null, no style is set.)
 * \tparam T The type of path, either Geom::Path or Geom::PathVector.
 * \return The newly-created item.
 */
template <typename T>
SPObject *write_path_xml(T const &path, SPObject const *style_from, SPObject *parent, SPObject *after = nullptr);

/**
 * Add a path to a document as the next sibling of \a after, also copying its style.
 * \tparam T The type of path, either Geom::Path or Geom::PathVector.
 * \return The newly-created item.
 */
template <typename T>
SPObject *write_path_xml(T const &path, SPObject *after);

/**
 * When a collection of items is fractured, each broken piece is represented by a SubItem.
 * This class holds information such as the path and the list of contributing items.
 */
class SubItem
{
public:
    Geom::PathVector paths;
    std::vector<SPItem*> items; // guaranteed non-empty and sorted top-to-bottom

    SPItem *top_item() const { return items.front(); }

    SubItem() = default;

    SubItem(Geom::PathVector &&pathvec, std::vector<SPItem*> &&items)
        : paths(std::move(pathvec))
        , items(std::move(items)) {}
};

/**
 * Split a collection of SPItems into non-overlapping disconnected pieces.
 *
 * The result is returned as a collection of SubItems, encoding the shape of the piece
 * together with the list of contributing items, in order.
 */
std::vector<SubItem> fracture(std::vector<SPItem*> items);

/**
 * A helper class for fracturing an ObjectSet and constructing the associated paths.
 */
class NonIntersectingPathsBuilder
{
public:
    NonIntersectingPathsBuilder(ObjectSet *set)
        : set(set) {}

    void fracture(bool skip_undo = false);
    void perform_fracture();
    void show_output(bool delete_original = true);

    std::vector<SubItem> const &get_result_subitems() const { return result_subitems; }
    std::vector<SPObject*> const &get_result_nodes() const { return result_nodes; }
    bool modified() const { return result_subitems.size() > items.size(); }

private:
    ObjectSet *set;
    std::vector<SPItem*> items;
    std::vector<SubItem> result_subitems;
    std::vector<SPObject*> result_nodes;

    void prepare_input();
    void draw_subitems(std::vector<SubItem> const &subitems);
    void add_result_to_set();
};

} // namespace Inkscape

#endif // INKSCAPE_UI_TOOLS_BOOLEANS_NONINTERSECTING
