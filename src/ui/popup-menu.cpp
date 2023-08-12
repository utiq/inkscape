// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helpers to connect signals to events that popup a menu in both GTK3 and GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "popup-menu.h"

#include <utility>
#include <gtkmm/gesturemultipress.h>
#include <gtkmm/widget.h>

#include "controller.h"
#include "manage.h"

namespace Inkscape::UI {

static bool on_key_pressed(GtkEventControllerKey const * /*controller*/,
                           unsigned const keyval, unsigned /*keycode*/, GdkModifierType state,
                           PopupMenuSlot const * const slot)
{
    g_return_val_if_fail(slot != nullptr, false);

    if (keyval == GDK_KEY_Menu) return (*slot)(); 

    state = static_cast<GdkModifierType>(state & gtk_accelerator_get_default_mod_mask());
    if (keyval == GDK_KEY_F10 && Controller::has_flag(state, GDK_SHIFT_MASK)) return (*slot)();

    return false;
}

static Gtk::EventSequenceState on_click_pressed(Gtk::GestureMultiPress const &click,
                                                int /*n_press*/, double /*dx*/, double /*dy*/,
                                                PopupMenuSlot const * const slot)
{
    g_return_val_if_fail(slot != nullptr, Gtk::EVENT_SEQUENCE_NONE);

    if (gdk_event_triggers_context_menu(Controller::get_last_event(click))
        && (*slot)()) return Gtk::EVENT_SEQUENCE_CLAIMED;

    return Gtk::EVENT_SEQUENCE_NONE;
}

PopupMenuSlot &on_popup_menu(Gtk::Widget &widget, PopupMenuSlot &&slot)
{
    auto &managed_slot = manage(std::move(slot), widget);
    auto const key = gtk_event_controller_key_new(widget.Gtk::Widget::gobj());
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), &managed_slot);
    Controller::add_click(widget, sigc::bind(&on_click_pressed, &managed_slot), {},
                          Controller::Button::any, Gtk::PHASE_TARGET); // ←beat Entry popup handler
    return managed_slot;
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
