// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helper functions to make children in GtkPopovers act like GtkMenuItem of GTK3
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "menuize.h"

#include <utility>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <giomm/menumodel.h>
#include <gtkmm/container.h>
#include <gtkmm/eventcontroller.h>
#include <gtkmm/modelbutton.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/widget.h>

#include "ui/manage.h"
#include "ui/util.h"

namespace Inkscape::UI {

static Gtk::Widget &get_widget(GtkEventControllerMotion * const motion)
{
    auto const widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
    g_assert(widget != nullptr);
    return *Glib::wrap(widget);
}

static void on_motion_grab_focus(GtkEventControllerMotion * const motion, double /*x*/, double /*y*/,
                                 void * /*user_data*/)
{
    auto &widget = get_widget(motion);
    if (widget.is_focus()) return;
    widget.grab_focus(); // Weʼll then run the below handler @ notify::is-focus
}

static void unset_state(Gtk::Widget &widget)
{
    widget.unset_state_flags(Gtk::STATE_FLAG_FOCUSED | Gtk::STATE_FLAG_PRELIGHT);
}

static void on_leave_unset_state(GtkEventControllerMotion * const motion, double /*x*/, double /*y*/,
                                 void * /*user_data*/)
{
    auto &widget = get_widget(motion);
    auto &parent = dynamic_cast<Gtk::Widget &>(*widget.get_parent());
    unset_state(widget); // This is somehow needed for GtkPopoverMenu, although not our PopoverMenu
    unset_state(parent); // Try to unset state on all other menu items, in case we left by keyboard
}

void menuize(Gtk::Widget &widget)
{
    // If hovered naturally or below, key-focus self & clear focus+hover on rest
    auto const motion = gtk_event_controller_motion_new(widget.gobj());
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
    g_signal_connect(motion, "enter" , G_CALLBACK(on_motion_grab_focus), NULL);
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion_grab_focus), NULL);
    g_signal_connect(motion, "leave" , G_CALLBACK(on_leave_unset_state), NULL);
    manage(Glib::wrap(motion), widget);

    // If key-focused in/out, ‘fake’ correspondingly appearing as hovered or not
    widget.property_is_focus().signal_changed().connect([&]
    {
        if (widget.is_focus()) {
            widget.set_state_flags(Gtk::STATE_FLAG_PRELIGHT, false);
        } else {
            widget.unset_state_flags(Gtk::STATE_FLAG_PRELIGHT);
        }
    });
}

template <typename Type>
static void menuize_all(Gtk::Container &container)
{
    for_each_descendant(container, [=](Gtk::Widget &child)
    {
        if (auto const typed = dynamic_cast<Type *>(&child)) {
            menuize(*typed);
        }
        return ForEachResult::_continue;
    });
}

void autohide_tooltip(Gtk::Popover &popover)
{
    popover.signal_show().connect([&]
    {
        if (auto const relative_to = popover.get_relative_to()) {
            relative_to->set_has_tooltip(false);
        }
    });
    popover.signal_closed().connect([&]
    {
        if (auto const relative_to = popover.get_relative_to()) {
            relative_to->set_has_tooltip(true);
        }
    });
}

std::unique_ptr<Gtk::Popover>
make_menuized_popover(Glib::RefPtr<Gio::MenuModel> model, Gtk::Widget &relative_to)
{
    // TODO: GTK4: Be more GtkMenu-like by using PopoverMenu::Flags::NESTED
    auto popover = std::make_unique<Gtk::PopoverMenu>();
    popover->get_style_context()->add_class("menuize");
    popover->bind_model(std::move(model));
    popover->set_relative_to(relative_to);
    menuize_all<Gtk::ModelButton>(*popover);
    autohide_tooltip(*popover);
    popover->show_all_children();
    return popover;
}

} // namespace Inkscape::UI

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
