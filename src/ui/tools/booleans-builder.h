// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *//*
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLS_BOOLEANS_BUILDER_H
#define INKSCAPE_UI_TOOLS_BOOLEANS_BUILDER_H

#include <vector>
#include <optional>

#include "booleans-subitems.h"

class SPDesktop;
class SPDocument;
class SPObject;

namespace Inkscape {

class CanvasItemGroup;
class CanvasItemBpath;
class ObjectSet;

using VisualItem = std::shared_ptr<CanvasItemBpath>;
using ItemPair = std::pair<WorkItem, VisualItem>;

class BooleanBuilder
{
public:
    BooleanBuilder(ObjectSet *obj);
    ~BooleanBuilder();

    void redraw_items();
    void undo();
    void redo();

    std::vector<SPObject *> shape_commit(bool all = false);
    std::optional<ItemPair> get_item(const Geom::Point &point);
    bool task_select(const Geom::Point &point, bool add_task = true);
    bool task_add(const Geom::Point &point);
    void task_cancel();
    void task_commit();
    bool has_task() { return (bool)_work_task; }
    bool highlight(const Geom::Point &point, bool add_task = true);

private:

    ObjectSet *_set;
    std::unique_ptr<CanvasItemGroup> _group;

    std::vector<WorkItem> _work_items;
    std::vector<ItemPair> _screen_items;
    WorkItem _work_task;
    VisualItem _screen_task;
    bool _add_task;

    // Lists of _work_items which can be brought back.
    std::vector<std::vector<WorkItem>> _undo;
    std::vector<std::vector<WorkItem>> _redo;
};

} // namespace Inkscape

#endif // INKSCAPE_UI_TOOLS_BOOLEANS_BUILDER_H
