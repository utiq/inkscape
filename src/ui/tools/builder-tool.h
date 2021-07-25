// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A tool for building shapes.
 */
/* Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#pragma once

#include "ui/tools/tool-base.h"
#include "helper/InteractiveShapesBuilder.h"

#define SP_SELECT_CONTEXT(obj) (dynamic_cast<Inkscape::UI::Tools::BuilderTool*>((Inkscape::UI::Tools::ToolBase*)obj))
#define SP_IS_SELECT_CONTEXT(obj) (dynamic_cast<const Inkscape::UI::Tools::BuilderTool*>((const Inkscape::UI::Tools::ToolBase*)obj) != NULL)

namespace Inkscape {
class CanvasItem;
class SelTrans;
class SelectionDescriber;
}

namespace Inkscape {
namespace UI {
namespace Tools {

class BuilderTool : public ToolBase {
public:

    typedef bool (BuilderTool::*EventHandler)(GdkEvent*);

    enum Operation {
        SELECT_AND_UNION = 0,
        SELECT_AND_DELETE = 1,
        SELECT_AND_INTERSECT = 2,
        JUST_SELECT = 3,
    };

    BuilderTool();
    ~BuilderTool() override;

    bool dragging;
    bool moved;
    guint button_press_state;

    SPItem *item;
    Inkscape::CanvasItem *grabbed = nullptr;
    Inkscape::SelTrans *_seltrans;
    Inkscape::SelectionDescriber *_describer;
    gchar *no_selection_msg = nullptr;

    static const std::string prefsPath;

    void setup() override;
    void set(const Inkscape::Preferences::Entry& val) override;
    bool root_handler(GdkEvent* event) override;
    bool item_handler(SPItem* item, GdkEvent* event) override;

    const std::string& getPrefsPath() override;

    void set_current_operation(GdkEvent* event);
    void set_current_operation(int current_operation = -1);

    void start_interactive_mode();
    void end_interactive_mode();

    bool in_interactive_mode() const;

    void apply();
    void reset();
    void discard();

private:

    bool sp_select_context_abort();

    EventHandler get_event_handler(GdkEvent* event);
    bool event_button_press_handler(GdkEvent* event);
    bool event_button_release_handler(GdkEvent* event);
    bool event_motion_handler(GdkEvent* event);
    bool event_key_press_handler(GdkEvent* event);
    bool event_key_release_handler(GdkEvent* event);

    void perform_operation(Selection *selection, int operation);
    void perform_current_operation(Selection *selection);

    void set_modifiers_state(GdkEvent* event);
    int get_current_operation();
    bool is_operation_add_to_selection(int operation, GdkEvent *event);
    void set_cursor_operation();
    void set_rubberband_color();

    // TODO you might pre-load the cursors and store them
    //  in this vector instead of loading them each time.
    static const std::vector<std::string> operation_cursor_filenames;
    static const std::vector<guint32> operation_colors;
    static const std::map<GdkEventType, EventHandler> handlers;

    InteractiveShapesBuilder shapes_builder;

    int active_operation = JUST_SELECT; // default to the select operation since this is the default cursor.
    bool ctrl_on = false;
    bool alt_on = false;
    bool shift_on = false;
};

}
}
}
