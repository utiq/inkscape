// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Toolbar for Tools.
 */
/* Authors:
 *   Mike Kowalski (Popovers)
 *   Matrin Owens (Tool button catagories)
 *   Jonathon Neuhauser (Open tool preferences)
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <utility>
#include <sigc++/adaptors/bind.h>
#include <glibmm/i18n.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/menu.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include "tool-toolbar.h"
#include "actions/actions-tools.h" // Function to open tool preferences.
#include "inkscape-window.h"
#include "ui/builder-utils.h"
#include "widgets/spw-utilities.h" // Find action target

namespace Inkscape::UI::Toolbar {

ToolToolbar::ToolToolbar(InkscapeWindow *window)
    : Gtk::Box()
{
    set_name("ToolToolbar");
    set_homogeneous(false);

    Gtk::ScrolledWindow* tool_toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-tool.ui");
    builder->get_widget("tool-toolbar", tool_toolbar);
    if (!tool_toolbar) {
        std::cerr << "ToolToolbar: Failed to load tool toolbar!" << std::endl;
        return;
    }

    attachHandlers(builder, window);

    pack_start(*tool_toolbar, true, true);

    // Hide/show buttons based on preferences.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    buttons_pref_observer = prefs->createObserver(tools_button_path, [=]() { set_visible_buttons(); });
    set_visible_buttons(); // Must come after pack_start()
}

ToolToolbar::~ToolToolbar() = default;

void ToolToolbar::set_visible_buttons()
{
    int buttons_before_separator = 0;
    Gtk::Widget* last_sep = nullptr;
    Gtk::FlowBox* last_box = nullptr;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    sp_traverse_widget_tree(this, [&](Gtk::Widget* widget) {
        if (auto flowbox = dynamic_cast<Gtk::FlowBox*>(widget)) {
            flowbox->show();
            flowbox->set_no_show_all();
            flowbox->set_max_children_per_line(1);
            last_box = flowbox;
        }
        else if (auto btn = dynamic_cast<Gtk::Button*>(widget)) {
            auto name = sp_get_action_target(widget);
            auto show = prefs->getBool(get_tool_visible_button_path(name), true);
            auto parent = btn->get_parent();
            if (show) {
                parent->show();
                ++buttons_before_separator;
                // keep the max_children up to date improves display.
                last_box->set_max_children_per_line(buttons_before_separator);
                last_sep = nullptr;
            }
            else {
                parent->hide();
            }
        }
        else if (auto sep = dynamic_cast<Gtk::Separator*>(widget)) {
            if (buttons_before_separator <= 0) {
                sep->hide();
            }
            else {
                sep->show();
                buttons_before_separator = 0;
                last_sep = sep;
            }
        }
        return false;
    });
    if (last_sep) {
        // hide trailing separator
        last_sep->hide();
    }
}

// We should avoid passing in the window in Gtk4 by turning "tool_preferences()" into an action.
/**
 * @brief Create a context menu for a tool button.
 * @param tool_name The tool name (parameter to the tool-switch action)
 * @param win The Inkscape window which will display the preferences dialog.
 */
Gtk::Menu *ToolToolbar::getContextMenu(Glib::ustring const &tool_name, InkscapeWindow *window)
{
    auto menu = Gtk::make_managed<Gtk::Menu>();
    auto gio_menu = Gio::Menu::create();
    auto action_group = Gio::SimpleActionGroup::create();
    menu->insert_action_group("ctx", action_group);
    action_group->add_action("open-tool-preferences",
                             sigc::bind(&tool_preferences, tool_name, window));

    auto menu_item = Gio::MenuItem::create(_("Open tool preferences"), "ctx.open-tool-preferences");

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getInt("/theme/menuIcons", true)) {
        auto _icon = Gio::Icon::create("preferences-system");
        menu_item->set_icon(_icon);
    }

    gio_menu->append_item(menu_item);
    menu->bind_model(gio_menu, true);
    menu->show();
    return menu;
}

/**
 * @brief Attach handlers to all tool buttons, so that double-clicking on a tool
 *        in the toolbar opens up that tool's preferences, and a right click opens a
 *        context menu with the same functionality.
 * @param builder The builder that contains a loaded UI structure containing RadioButton's.
 * @param window The Inkscape window which will display the preferences dialog.
 */
void ToolToolbar::attachHandlers(Glib::RefPtr<Gtk::Builder> builder, InkscapeWindow *window)
{
    for (auto &object : builder->get_objects()) {
        auto const radio = dynamic_cast<Gtk::RadioButton *>(object.get());
        if (!radio) {
            continue;
        }

        Glib::VariantBase action_target;
        radio->get_property("action-target", action_target);
        if (!action_target.is_of_type(Glib::VARIANT_TYPE_STRING)) {
            continue;
        }

        auto tool_name = Glib::ustring((gchar const *)action_target.get_data());

        auto menu = getContextMenu(tool_name, window);
        menu->attach_to_widget(*radio);

        auto on_click_pressed = [=, tool_name = std::move(tool_name)]
                                (GdkEventButton const * const ev)
        {
            // Open tool preferences upon double click
            if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
                tool_preferences(tool_name, window);
                return true;
            }
            if (ev->button == 3) {
                menu->popup_at_pointer(reinterpret_cast<GdkEvent const *>(ev));
                return true;
            }
            return false;
        };
        radio->signal_button_press_event().connect(std::move(on_click_pressed));
    }
}

Glib::ustring ToolToolbar::get_tool_visible_button_path(const Glib::ustring& button_action_name) {
    return Glib::ustring(tools_button_path) + "/show" + button_action_name;
}

} // namespace Inkscape::UI::Toolbar

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
