// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A toolbar for the Builder tool.
 *
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2021 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#pragma once

#include <gtkmm.h>

#include <utility>

#include "toolbar.h"

class SPDesktop;

namespace Inkscape {
class Selection;

namespace UI {

namespace Widget {
class UnitTracker;
}

namespace Toolbar {

class InteractiveBooleansToolbar;

// A method that belongs to InteractiveBooleansToolbar that returns void and accepts noting.
typedef void (InteractiveBooleansToolbar::*InteractiveBooleansToolbarVoidMethod)();

struct ButtonDescriptor
{
    std::string label;
    std::string tooltip_text;
    std::string icon_name;
    InteractiveBooleansToolbarVoidMethod handler;
};

class InteractiveBooleansToolbar : public Toolbar {
    using parent_type = Toolbar;

private:
    std::vector<Gtk::RadioToolButton *> _operation_buttons;
    std::vector<InteractiveBooleansToolbarVoidMethod> _operation_handlers;

    std::vector<Gtk::RadioToolButton *> _mode_buttons;
    std::vector<InteractiveBooleansToolbarVoidMethod> _mode_handlers;

    std::vector<Gtk::Widget*> _mode_widgets;
    std::vector<Gtk::Widget*> _operation_widgets;
    std::vector<Gtk::Widget*> _command_widgets;
    std::vector<Gtk::Widget*> _interactive_mode_widgets;

    bool mode_changed_called = false;

    void init();

    void mode_buttons_init();
    void mode_buttons_init_create_buttons(const std::vector<ButtonDescriptor>& descriptors);
    void mode_buttons_init_add_buttons();
    void mode_changed(int mode);

    void normal_mode_setup();
    void interactive_mode_setup();

    void show_normal_mode_buttons();
    void hide_normal_mode_buttons();
    void show_interactive_mode_buttons();
    void hide_interactive_mode_buttons();

//  operation related methods {
    void operation_buttons_init();
    void operation_buttons_init_create_buttons(const std::vector<ButtonDescriptor>& descriptors);
    void operation_buttons_init_set_active_button();
    void operation_buttons_init_add_buttons();

    void operation_changed(int operation);
    void set_current_operation(int operation);

    // handlers that gets called when the operation is changed:
    void set_operation_union();
    void set_operation_delete();
    void set_operation_intersection();
    void set_operation_just_select();
//  }

    void boolop_buttons_init();
    void boolop_buttons_init_actions();
    void boolop_buttons_init_actions_add_buttons(const std::vector<ButtonDescriptor>& descriptors);
    void boolop_buttons_init_verbs();
    void perform_fracture();
    void perform_flatten();

    void compound_operations_buttons_init();
    void compound_operations_buttons_init_actions();
    void compound_operations_buttons_init_verbs();
    void perform_split_non_intersecting();

    void interactive_mode_buttons_init();
    void interactive_mode_buttons_init_add_buttons(const std::vector<ButtonDescriptor> &descriptors);
    void interactive_mode_apply();
    void interactive_mode_reset();
    void interactive_mode_discard();

    void add_separator(std::vector<Gtk::Widget*> &group);

protected:
    InteractiveBooleansToolbar(SPDesktop *desktop);

public:
    void set_mode_normal();
    void set_mode_interactive();
    static GtkWidget * create(SPDesktop *desktop);

    bool notify_back = true;
};

}
}
}
