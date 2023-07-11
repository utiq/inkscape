// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Inkscape toolbar definitions and general utility functions.
 * Each tool should have its own xxx-toolbar implementation file
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Jabiertxo Arraiza <jabier.arraiza@marker.es>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2015 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbox.h"

#include <gtkmm.h>
#include <glibmm/i18n.h>

#include "actions/actions-canvas-snapping.h"
#include "actions/actions-tools.h"
#include "io/resource.h"
#include "ui/util.h"
#include "ui/builder-utils.h"
#include "ui/widget/style-swatch.h"
#include "widgets/widget-sizes.h"


#include "ui/tools/tool-base.h"

//#define DEBUG_TEXT

using Inkscape::UI::ToolboxFactory;

using Inkscape::IO::Resource::get_filename;
using Inkscape::IO::Resource::UIS;

// This is the box that contains icons for the different tools.
Gtk::Widget *ToolboxFactory::createToolToolbox(InkscapeWindow *window)
{
    Gtk::Widget* toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-tool.ui");
    builder->get_widget("tool-toolbar", toolbar);
    if (!toolbar) {
        std::cerr << "InkscapeWindow: Failed to load tool toolbar!" << std::endl;
    }

    _attachHandlers(builder, window);

    toolbar->reference(); // Or it will be deleted when builder goes out of scope.
    return toolbar;
}

/**
 * @brief Create a context menu for a tool button.
 * @param tool_name The tool name (parameter to the tool-switch action)
 * @param win The Inkscape window which will display the preferences dialog.
 */
Gtk::Menu *ToolboxFactory::_getContextMenu(Glib::ustring tool_name, InkscapeWindow *win)
{
    auto menu = new Gtk::Menu();
    auto gio_menu = Gio::Menu::create();
    auto action_group = Gio::SimpleActionGroup::create();
    menu->insert_action_group("ctx", action_group);
    action_group->add_action("open-tool-preferences", sigc::bind(
                                                          sigc::ptr_fun(&tool_preferences), tool_name, win));

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
 * @param win The Inkscape window which will display the preferences dialog.
 */
void ToolboxFactory::_attachHandlers(Glib::RefPtr<Gtk::Builder> builder, InkscapeWindow *win)
{
    for (auto &object : builder->get_objects()) {
        if (auto radio = dynamic_cast<Gtk::RadioButton *>(object.get())) {

            Glib::VariantBase action_target;
            radio->get_property("action-target", action_target);
            if (!action_target.is_of_type(Glib::VARIANT_TYPE_STRING)) {
                continue;
            }

            auto tool_name = Glib::ustring((gchar const *)action_target.get_data());

            auto menu = _getContextMenu(tool_name, win);
            menu->attach_to_widget(*radio);

            radio->signal_button_press_event().connect([=](GdkEventButton *ev) -> bool {
                // Open tool preferences upon double click
                if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
                    tool_preferences(tool_name, win);
                    return true;
                }
                if (ev->button == 3) {
                    menu->popup_at_pointer(reinterpret_cast<GdkEvent *>(ev));
                }
                return false;
            });
        }
    }
}

//####################################
//# Commands Bar
//####################################

GtkWidget *ToolboxFactory::createCommandsToolbox()
{
    auto tb = new Gtk::Box();
    tb->set_name("CommandsToolbox");
    tb->set_orientation(Gtk::ORIENTATION_VERTICAL);
    tb->set_homogeneous(false);

    Gtk::Toolbar* toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-commands.ui");
    builder->get_widget("commands-toolbar", toolbar);
    if (!toolbar) {
        std::cerr << "ToolboxFactory: Failed to load commands toolbar!" << std::endl;
    } else {
        tb->pack_start(*toolbar, false, false);

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if ( prefs->getBool("/toolbox/icononly", true) ) {
            toolbar->set_toolbar_style( Gtk::TOOLBAR_ICONS );
        }
    }

    return tb->Gtk::Widget::gobj();
}

int show_popover(void* button) {
    auto btn = static_cast<Gtk::MenuButton*>(button);
    btn->get_popover()->show();
    return false;
}

Glib::ustring ToolboxFactory::get_tool_visible_buttons_path(const Glib::ustring& button_action_name) {
    return Glib::ustring(ToolboxFactory::tools_visible_buttons) + "/show" + button_action_name;
}

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
