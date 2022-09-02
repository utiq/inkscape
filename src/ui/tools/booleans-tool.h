// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A tool for building shapes.
 */
/*
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLS_BOOLEANS_TOOL
#define INKSCAPE_UI_TOOLS_BOOLEANS_TOOL

#include "ui/tools/tool-base.h"
#include "booleans-interactive.h"

namespace Inkscape {
class CanvasItem;
class SelTrans;
class SelectionDescriber;

namespace UI {
namespace Tools {

class InteractiveBooleansTool
    : public ToolBase
{
public:
    using EventHandler = bool (InteractiveBooleansTool::*)(GdkEvent*);

    enum Operation
    {
        SELECT_AND_UNION = 0,
        SELECT_AND_DELETE = 1,
        SELECT_AND_INTERSECT = 2,
        JUST_SELECT = 3,
    };

    InteractiveBooleansTool(SPDesktop *desktop);
    ~InteractiveBooleansTool() override;

    bool dragging;
    bool moved;
    guint button_press_state;

    SPItem *item;
    Inkscape::CanvasItem *grabbed = nullptr;
    Inkscape::SelTrans *_seltrans;
    Inkscape::SelectionDescriber *_describer;
    gchar *no_selection_msg = nullptr;

    void set(const Inkscape::Preferences::Entry& val) override;
    bool root_handler(GdkEvent* event) override;
    bool item_handler(SPItem* item, GdkEvent* event) override;

    void set_current_operation(GdkEvent* event);
    void set_current_operation(int current_operation = -1);

    void start_interactive_mode();
    void end_interactive_mode();

    bool in_interactive_mode() const;

    void apply();
    void reset();
    void discard();

    // XXX These look like actions to me.
    void fracture(bool skip_undo = false);
    void flatten(bool skip_undo = false);
    void splitNonIntersecting(bool skip_undo = false);

private:
    bool sp_select_context_abort();

    EventHandler get_event_handler(GdkEvent* event);
    bool event_button_press_handler(GdkEvent* event);
    bool event_button_release_handler(GdkEvent* event);
    bool event_motion_handler(GdkEvent* event);
    bool event_key_press_handler(GdkEvent* event);
    bool event_key_release_handler(GdkEvent* event);

    void perform_operation(int operation);
    void perform_current_operation();

    void set_modifiers_state(GdkEvent* event);
    int get_current_operation();
    bool is_operation_add_to_selection(int operation, GdkEvent *event);
    void set_cursor_operation();
    void set_rubberband_color();

    InteractiveBooleanBuilder boolean_builder;

    int active_operation = JUST_SELECT; // default to the select operation since this is the default cursor.
    bool ctrl_on = false;
    bool alt_on = false;
    bool shift_on = false;
};

} // namespace Tools
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_TOOLS_BOOLEANS_TOOL
