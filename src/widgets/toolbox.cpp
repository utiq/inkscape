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
using Inkscape::UI::Tools::ToolBase;

using Inkscape::IO::Resource::get_filename;
using Inkscape::IO::Resource::UIS;

#define HANDLE_POS_MARK "x-inkscape-pos"

int ToolboxFactory::prefToPixelSize(Glib::ustring const& path) {
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int size = prefs->getIntLimited(path, 16, 16, 48);
    return size;
}

Gtk::IconSize ToolboxFactory::prefToSize_mm(Glib::ustring const &path, int base)
{
    static Gtk::IconSize sizeChoices[] = { Gtk::ICON_SIZE_LARGE_TOOLBAR, Gtk::ICON_SIZE_SMALL_TOOLBAR,
                                           Gtk::ICON_SIZE_DND, Gtk::ICON_SIZE_DIALOG };
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int index = prefs->getIntLimited(path, base, 0, G_N_ELEMENTS(sizeChoices));
    return sizeChoices[index];
}

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

class SnapBar : public Gtk::Box {
public:
    SnapBar() = default;
    ~SnapBar() override = default;

    Inkscape::PrefObserver _observer;
};

GtkWidget *ToolboxFactory::createSnapToolbox()
{
    auto tb = new SnapBar();
    tb->set_name("SnapToolbox");
    tb->set_homogeneous(false);

    bool simple_snap = true;
    Gtk::Toolbar* toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-snap.ui");
    builder->get_widget("snap-toolbar", toolbar);
    if (!toolbar) {
        std::cerr << "InkscapeWindow: Failed to load snap toolbar!" << std::endl;
    } else {
        tb->pack_start(*toolbar, false, false);

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if ( prefs->getBool("/toolbox/icononly", true) ) {
            toolbar->set_toolbar_style( Gtk::TOOLBAR_ICONS );
        }
        simple_snap = prefs->getBool("/toolbox/simplesnap", simple_snap);
    }

    Gtk::ToolItem* item_simple = nullptr;
    Gtk::ToolItem* item_advanced = nullptr;
    Gtk::MenuButton* btn_simple = nullptr;
    Gtk::MenuButton* btn_advanced = nullptr;
    Gtk::LinkButton* simple = nullptr;
    Gtk::LinkButton* advanced = nullptr;
    builder->get_widget("simple-link", simple);
    builder->get_widget("advanced-link", advanced);
    builder->get_widget("tool-item-advanced", item_advanced);
    builder->get_widget("tool-item-simple", item_simple);
    builder->get_widget("btn-simple", btn_simple);
    builder->get_widget("btn-advanced", btn_advanced);
    if (simple && advanced && item_simple && item_advanced && btn_simple && btn_advanced) {
        // keep only one popup button visible
        if (simple_snap) {
            item_simple->show();
            item_advanced->hide();
        }
        else {
            item_advanced->show();
            item_simple->hide();
        }

        // Watch snap bar preferences;
        Inkscape::Preferences* prefs = Inkscape::Preferences::get();
        tb->_observer = prefs->createObserver(ToolboxFactory::snap_bar_simple, [=](const Preferences::Entry& entry) {
            if (entry.getBool(true)) {
                item_advanced->hide();
                item_simple->show();
                // adjust snapping options when transitioning to simple scheme, since most are hidden
                transition_to_simple_snapping();
            }
            else {
                item_simple->hide();
                item_advanced->show();
            }
        });

        // switch to simple mode
        simple->signal_activate_link().connect([=](){
            g_timeout_add(250, &show_popover, btn_simple);
            Inkscape::Preferences::get()->setBool(ToolboxFactory::snap_bar_simple, true);
            return true;
        }, false);

        // switch to advanced mode
        advanced->signal_activate_link().connect([=](){
            g_timeout_add(250, &show_popover, btn_advanced);
            Inkscape::Preferences::get()->setBool(ToolboxFactory::snap_bar_simple, false);
            return true;
        }, false);
    }

    return GTK_WIDGET(tb->gobj());
}

#define noDUMP_DETAILS 1

// This is only used by the snap bar to hide/unhide the "permanent" snapbar section.
void ToolboxFactory::setOrientation(GtkWidget* toolbox, GtkOrientation orientation)
{
    if (GTK_IS_BOX(toolbox)) {
        auto box = Glib::wrap(GTK_BOX(toolbox));
        auto children = box->get_children();
        for (auto child : children) {
            if (auto toolbar = dynamic_cast<Gtk::Toolbar*>(child)) {
                gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar->gobj()), orientation);
                  //  toolbar->set_orientation(orientation);  No C++ API
            } else {
                std::cerr << "ToolboxFactory::setOrientation: toolbar not found!" << std::endl;
            }
        }
    } else {
        std::cerr << "ToolboxFactory::setOrientation: wrapper is not a box!" << std::endl;
    }
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
