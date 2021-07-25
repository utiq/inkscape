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

#include "builder-toolbar.h"
#include "ui/tools//builder-tool.h"

#include <glibmm/i18n.h>

#include <gtkmm/adjustment.h>
#include <gtkmm/separatortoolitem.h>

#include <2geom/rect.h>

#include "desktop.h"
#include "document-undo.h"
#include "selection.h"
#include "message-stack.h"
#include "selection-chemistry.h"
#include "verbs.h"


#include "object/sp-namedview.h"

#include "ui/icon-names.h"
#include "ui/widget/canvas.h" // Focus widget
#include "ui/widget/unit-tracker.h"

#include "widgets/widget-sizes.h"

namespace Inkscape {
namespace UI {
namespace Toolbar {

BuilderToolbar::BuilderToolbar(SPDesktop *desktop) :
    Toolbar(desktop)
{
    init();
}

void BuilderToolbar::init()
{
    add_label("Mode: ");
    mode_buttons_init();
    add_separator(_operation_widgets);
    _operation_widgets.push_back(add_label("Operations: "));
    operation_buttons_init();
    add_separator(_command_widgets);
    _command_widgets.push_back(add_label("Commands: "));
    boolop_buttons_init();
    add_separator(_command_widgets);
    compound_operations_buttons_init();
    add_separator(_interactive_mode_widgets);
    interactive_mode_buttons_init();

    show_all();
}

void BuilderToolbar::normal_mode_setup()
{
//    std::cout << "In normal_mode_setup.\n";
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);

    hide_interactive_mode_buttons();
    show_normal_mode_buttons();
    operation_buttons_init_set_active_button();

    if (builder_tool) {
        if (!builder_tool->in_interactive_mode() && notify_back) {
//            std::cout << "In normal mode. Returning.\n";
            return;
        }

        if (notify_back) {
            interactive_mode_apply();
        }
    }

}

void BuilderToolbar::set_mode_normal()
{
    mode_changed_called = false;
    auto normal_mode_button = _mode_buttons[1];
    normal_mode_button->set_active(true);
    if (!mode_changed_called) {
        normal_mode_setup();
    }
}

void BuilderToolbar::interactive_mode_setup()
{
//    std::cout << "In interactive_mode_setup.\n";
    hide_normal_mode_buttons();
    show_interactive_mode_buttons();
    operation_buttons_init_set_active_button();
//    std::cout << "Set the buttons to interactive mode buttons.\n";

    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool) {
        if (builder_tool->in_interactive_mode() && notify_back) {
//            std::cout << "In builder mode. Returning.\n";
            return;
        }

        if (notify_back) {
//            std::cout << "Calling BuilderTool::start_interactive_mode\n";
            builder_tool->start_interactive_mode();
        }

        if (!builder_tool->in_interactive_mode()) {
//            std::cout << "Couldn't start interactive mode.\n";
//            std::cout << "Setting the active button to normal.\n";
            set_mode_normal();
        }
    }

}

void BuilderToolbar::set_mode_interactive()
{
    mode_changed_called = false;
    auto normal_mode_button = _mode_buttons[0];
    normal_mode_button->set_active(true);
    if (!mode_changed_called) {
        interactive_mode_setup();
    }
}

void set_widgets_visibility(const std::vector<Gtk::Widget*> widgets, bool visibility)
{
    for (auto widget : widgets) {
        widget->set_visible(visibility);
    }
}

void BuilderToolbar::show_normal_mode_buttons()
{
    set_widgets_visibility(_operation_widgets, true);
    set_widgets_visibility(_command_widgets, true);
}

void BuilderToolbar::hide_normal_mode_buttons()
{
    _operation_buttons[Tools::BuilderTool::SELECT_AND_INTERSECT]->set_visible(false);
    _operation_buttons[Tools::BuilderTool::JUST_SELECT]->set_visible(false);
    set_widgets_visibility(_command_widgets, false);
}

void BuilderToolbar::show_interactive_mode_buttons()
{
    set_widgets_visibility(_interactive_mode_widgets, true);
}

void BuilderToolbar::hide_interactive_mode_buttons()
{
    set_widgets_visibility(_interactive_mode_widgets, false);
}

void BuilderToolbar::mode_buttons_init()
{
    // TODO change the icons and tooltips text.
    const static std::vector<ButtonDescriptor> mode_buttons_descriptors = {
        {
            .label = _("Interactive"),
            .tooltip_text = _("Merge and Delete shapes interactively"),
            .icon_name = "interactive-builder",
            .handler = &BuilderToolbar::interactive_mode_setup,
        },
        {
            .label = _("Normal"),
            .tooltip_text = _("Perform boolean operations"),
            .icon_name = "path-union",
            .handler = &BuilderToolbar::normal_mode_setup,
        },
    };

    mode_buttons_init_create_buttons(mode_buttons_descriptors);
    mode_buttons_init_add_buttons();
}

void BuilderToolbar::mode_buttons_init_create_buttons(const std::vector<ButtonDescriptor>& descriptors)
{
    Gtk::RadioToolButton::Group mode_group;

    for (auto& mode : descriptors)
    {
        auto button = Gtk::manage(new Gtk::RadioToolButton((mode.label)));
        button->set_tooltip_text((mode.tooltip_text));
        button->set_icon_name(INKSCAPE_ICON(mode.icon_name));
        button->set_group(mode_group);
        _mode_buttons.push_back(button);
        _mode_handlers.push_back(mode.handler);
    }
}

void BuilderToolbar::mode_buttons_init_add_buttons()
{
    int button_index = 0;
    for (auto button : _mode_buttons)
    {
        button->set_sensitive();
        button->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &BuilderToolbar::mode_changed), button_index++));
        _mode_widgets.push_back(button);
        add(*button);
    }
}

void BuilderToolbar::mode_changed(int mode)
{
//    std::cout << "Mode changed to " << mode << '\n';
    mode_changed_called = true;
    auto handler = _mode_handlers[mode];
    (this->*handler)();
}

void BuilderToolbar::operation_buttons_init()
{
    // TODO change the icons.
    // if you're going to edit this, remember to edit the BuilderTool::Mode enum and
    // BuilderTool::operation_cursor_filenames as well. remember that they have to be in the same order.
    const static std::vector<ButtonDescriptor> operation_buttons_descriptors = {
        {
            .label = _("Union"),
            .tooltip_text = _("Union whatever the mouse moves over"),
            .icon_name = "path-union",
            .handler = &BuilderToolbar::set_operation_union,
        },
        {
            .label = _("Delete"),
            .tooltip_text = _("Delete whatever the mouse moves over"),
            .icon_name = "path-difference",
            .handler = &BuilderToolbar::set_operation_delete,
        },
        {
            .label = _("Intersection"),
            .tooltip_text = _("Intersect whatever the mouse moves over"),
            .icon_name = "path-intersection",
            .handler = &BuilderToolbar::set_operation_intersection,
        },
        {
            .label = _("Just Select"),
            .tooltip_text = _("Just select whatever the mouse moves over"),
            .icon_name = "tool-pointer",
            .handler = &BuilderToolbar::set_operation_just_select,
        },
    };

    operation_buttons_init_create_buttons(operation_buttons_descriptors);
    operation_buttons_init_set_active_button();
    operation_buttons_init_add_buttons();
}

void BuilderToolbar::operation_buttons_init_create_buttons(const std::vector<ButtonDescriptor>& descriptors)
{
    Gtk::RadioToolButton::Group operation_group;

    for (auto& operation : descriptors)
    {
        auto button = Gtk::manage(new Gtk::RadioToolButton((operation.label)));
        button->set_tooltip_text((operation.tooltip_text));
        button->set_icon_name(INKSCAPE_ICON(operation.icon_name));
        button->set_group(operation_group);
        _operation_buttons.push_back(button);
        _operation_handlers.push_back(operation.handler);
    }
}

void BuilderToolbar::operation_buttons_init_set_active_button()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    gint type;
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool && builder_tool->in_interactive_mode()) {
        type = prefs->getInt("/tools/builder/interactive_operation", 0);
    } else {
        type = prefs->getInt("/tools/builder/normal_operation", 0);
    }

    _operation_buttons[type]->set_active();
}

void BuilderToolbar::operation_buttons_init_add_buttons()
{
    int button_index = 0;
    for (auto button : _operation_buttons)
    {
        button->set_sensitive();
        button->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &BuilderToolbar::operation_changed), button_index++));
        _operation_widgets.push_back(button);
        add(*button);
    }
}

void BuilderToolbar::operation_changed(int operation)
{
    // each operation has its own handler so that it's
    // easier to attach more logic in the future.
    auto handler = _operation_handlers[operation];
    (this->*handler)();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool && builder_tool->in_interactive_mode()) {
        prefs->setInt("/tools/builder/interactive_operation", operation);
    } else {
        prefs->setInt("/tools/builder/normal_operation", operation);
    }
}

void BuilderToolbar::set_current_operation(int operation)
{
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool) {
        builder_tool->set_current_operation(operation);
    }
}

void BuilderToolbar::set_operation_union()
{
    set_current_operation(Tools::BuilderTool::SELECT_AND_UNION);
}

void BuilderToolbar::set_operation_delete()
{
    set_current_operation(Tools::BuilderTool::SELECT_AND_DELETE);
}

void BuilderToolbar::set_operation_intersection()
{
    set_current_operation(Tools::BuilderTool::SELECT_AND_INTERSECT);
}

void BuilderToolbar::set_operation_just_select()
{
    set_current_operation(Tools::BuilderTool::JUST_SELECT);
}


void BuilderToolbar::boolop_buttons_init()
{
    boolop_buttons_init_verbs();
    boolop_buttons_init_actions();
}

void BuilderToolbar::boolop_buttons_init_actions()
{
    // TODO find a better way than this. Using verbs is easier.
    const static std::vector<ButtonDescriptor> boolop_buttons_descriptors = {
        {
            .label = _("Fracture"),
            .tooltip_text = _("Break the selected paths into non-overlapping (fractured) paths"),
            .icon_name = "path-fracture",
            .handler = &BuilderToolbar::perform_fracture,
        },
        {
            .label = _("Flatten"),
            .tooltip_text = _("Remove any hidden part part of the selection (has an item on top of it)"),
            .icon_name = "path-flatten",
            .handler = &BuilderToolbar::perform_flatten,
        },
    };

    boolop_buttons_init_actions_add_buttons(boolop_buttons_descriptors);
}

void BuilderToolbar::boolop_buttons_init_actions_add_buttons(const std::vector<ButtonDescriptor>& descriptors)
{
    for (auto& boolop : descriptors)
    {
        auto button = Gtk::manage(new Gtk::ToolButton(boolop.label));
        button->set_tooltip_text((boolop.tooltip_text));
        button->set_icon_name(INKSCAPE_ICON(boolop.icon_name));
        button->signal_clicked().connect(sigc::mem_fun(*this, boolop.handler));
        _command_widgets.push_back(button);
        add(*button);
    }
}

void BuilderToolbar::perform_fracture()
{
    auto selection = _desktop->getSelection();
    selection->fracture();
}

void BuilderToolbar::perform_flatten()
{
    auto selection = _desktop->getSelection();
    selection->flatten();
}

void BuilderToolbar::boolop_buttons_init_verbs()
{
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_UNION));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_DIFF));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_INTERSECT));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_SYMDIFF));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_CUT));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_SLICE));
}

void BuilderToolbar::compound_operations_buttons_init()
{
    compound_operations_buttons_init_verbs();
    compound_operations_buttons_init_actions();
}

void BuilderToolbar::compound_operations_buttons_init_actions()
{
    // TODO find a better way than this. Using verbs is easier.
    const static std::vector<ButtonDescriptor> boolop_buttons_descriptors = {
        {
            .label = _("Split Non-Intersecting paths"),
            .tooltip_text = _("Split the combined path into separate non-intersecting paths"),
            .icon_name = "path-split-non-intersecting",
            .handler = &BuilderToolbar::perform_split_non_intersecting,
        },
    };

    boolop_buttons_init_actions_add_buttons(boolop_buttons_descriptors);
}

void BuilderToolbar::perform_split_non_intersecting()
{
    auto selection = _desktop->getSelection();
    selection->splitNonIntersecting();
}

void BuilderToolbar::compound_operations_buttons_init_verbs()
{
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_COMBINE));
    _command_widgets.push_back(add_toolbutton_for_verb(SP_VERB_SELECTION_BREAK_APART));
}

void BuilderToolbar::interactive_mode_buttons_init()
{
    // TODO find a better way than this. Using verbs is easier.
    const static std::vector<ButtonDescriptor> interactive_mode_buttons_descriptors = {
        {
            .label = _("Apply"),
            .tooltip_text = _("Apply changes"),
            .icon_name = "interactive-mode-apply",
            .handler = &BuilderToolbar::interactive_mode_apply,
        },
        {
            .label = _("Reset"),
            .tooltip_text = _("Reset changes"),
            .icon_name = "interactive-mode-reset",
            .handler = &BuilderToolbar::interactive_mode_reset,
        },
        {
            .label = _("Discard"),
            .tooltip_text = _("Discard interactive mode"),
            .icon_name = "interactive-mode-discard",
            .handler = &BuilderToolbar::interactive_mode_discard,
        },
    };

    interactive_mode_buttons_init_add_buttons(interactive_mode_buttons_descriptors);
}

void BuilderToolbar::interactive_mode_apply()
{
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool) {
        builder_tool->apply();
    }
}

void BuilderToolbar::interactive_mode_reset()
{
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool) {
        builder_tool->reset();
    }
}

void BuilderToolbar::interactive_mode_discard()
{
    auto builder_tool = dynamic_cast<Tools::BuilderTool*>(_desktop->event_context);
    if (builder_tool) {
        builder_tool->discard();
    }
}

void BuilderToolbar::interactive_mode_buttons_init_add_buttons(const std::vector<ButtonDescriptor> &descriptors)
{
    for (auto&descriptor : descriptors)
    {
        auto button = Gtk::manage(new Gtk::ToolButton(descriptor.label));
        button->set_tooltip_text((descriptor.tooltip_text));
        button->set_icon_name(INKSCAPE_ICON(descriptor.icon_name));
        button->signal_clicked().connect(sigc::mem_fun(*this, descriptor.handler));
        _interactive_mode_widgets.push_back(button);
        add(*button);
    }
}

void BuilderToolbar::add_separator(std::vector<Gtk::Widget*> &group)
{
    auto separator = Gtk::manage(new Gtk::SeparatorToolItem());
    group.push_back(separator);
    add(*separator);
}

GtkWidget *
BuilderToolbar::create(SPDesktop *desktop)
{
    auto toolbar = new BuilderToolbar(desktop);
    return GTK_WIDGET(toolbar->gobj());
}

}
}
}
