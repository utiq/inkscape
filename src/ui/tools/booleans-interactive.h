// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Interactive Booleans Builder.
 */
/*
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLS_BOOLEANS_INTERACTIVE
#define INKSCAPE_UI_TOOLS_BOOLEANS_INTERACTIVE

#include <vector>
#include <map>
#include <set>
#include <stack>

#include "booleans-nonintersecting.h"

class SPDesktop;
class SPDocument;
class SPItem;

namespace Inkscape {

class ObjectSet;
namespace XML { class Node; }

// FIXME: Find a way to keep references to items on the canvas. Right now
// the program crashes if the items being used here are removed (or
// replaced) by any other operation other than the ones this tool supports.
class InteractiveBooleanBuilder
{
public:
    bool is_started() const;
    void start(ObjectSet *set);
    void reset();
    void discard();
    void commit();

    void set_union(ObjectSet *set);
    void set_delete(ObjectSet *set);

    void undo();
    void redo();

    ~InteractiveBooleanBuilder() { commit(); }

private:
    struct UnionCommand
    {
        int result;
        std::vector<int> operands;
        bool draw_result;
    };

    SPDesktop *desktop;
    SPDocument *document;

    std::vector<SPItem*> selected_items;
    std::vector<SPItem*> not_selected_items;

    std::set<int> enabled;
    std::set<int> disabled;
    std::map<int, std::string> original_styles;

    std::stack<UnionCommand> undo_stack;
    std::stack<UnionCommand> redo_stack;

    std::map<int, XML::Node*> id_to_node;
    std::map<XML::Node*, int> node_to_id;
    std::map<int, SubItem> id_to_subitem;

    int last_id = 0;
    bool started = false;
    bool is_virgin = true;

    XML::Node* get_node_from_id(int id);
    int get_id_from_node(XML::Node *node);
    SubItem& get_subitem_from_id(int id);

    void renew_node_id(XML::Node *node, int id);

    int add_disabled_item(XML::Node *node, int id);
    int add_disabled_item(XML::Node *node, const SubItem &subitem);
    void remove_disabled_item(int id);

    int add_enabled_item(XML::Node *node, int id);
    int add_enabled_item(XML::Node *node, const SubItem &subitem);
    void remove_enabled_item(int id);

    void set_style_disabled(int id);
    void restore_original_style(int id);

    void hide_items(const std::vector<SPItem*> &items);
    void show_items(const std::vector<SPItem*> &items);

    void reset_internals();

    XML::Node* draw_and_set_visible(const SubItem &subitem);
    void perform_union(ObjectSet *set, bool draw_result);

    std::vector<int> get_subitems(const std::vector<SPItem*> &items);
    SubItem get_union_subitem(const std::vector<int> &subitems);
    void remove_items(const std::vector<SPItem*> &items);

    void push_undo_command(const UnionCommand& command);
};

} // namespace Inkscape

#endif // INKSCAPE_UI_TOOLS_BOOLEANS_INTERACTIVE
