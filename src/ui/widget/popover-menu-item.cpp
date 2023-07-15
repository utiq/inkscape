// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm/label.h>
#include <gtkmm/stylecontext.h>
#include "ui/util.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

PopoverMenuItem::PopoverMenuItem(Glib::ustring const &caption,
                                 bool const popdown_on_activate)
    : Glib::ObjectBase{"PopoverMenuItem"}
    , CssNameClassInit{"menuitem"}
    , Gtk::Button{}
{
    get_style_context()->add_class("menuitem");
    set_relief(Gtk::RELIEF_NONE);

    if (!caption.empty()) {
        auto &label = *Gtk::make_managed<Gtk::Label>(caption, false);
        label.set_xalign(0.0);
        add(label);
    }

    if (popdown_on_activate) {
        signal_activate().connect([this]
        {
            if (auto const menu = get_menu()) {
                menu->popdown();
            }
        });
    }

    // Make items behave like GtkMenu: focus if hovered & style focus+hover same
    // If hovered naturally or below, key-focus self & clear focus+hover on rest
    add_events(Gdk::POINTER_MOTION_MASK);
    signal_motion_notify_event().connect([this](GdkEventMotion const *)
    {
        if (is_focus()) return false;
        if (auto const menu = get_menu()) {
            menu->unset_items_focus_hover(this);
            grab_focus(); // Weʼll then run the below handler @ notify::is-focus
        }
        return false;
    });
    // If key-focused in/out, ‘fake’ correspondingly appearing as hovered or not
    property_is_focus().signal_changed().connect([this]
    {
        if (is_focus()) {
            set_state_flags(Gtk::STATE_FLAG_PRELIGHT, false);
        } else {
            unset_state_flags(Gtk::STATE_FLAG_PRELIGHT);
        }
    });
}

Glib::SignalProxy<void> PopoverMenuItem::signal_activate()
{
    return signal_clicked();
}

PopoverMenu *PopoverMenuItem::get_menu()
{
    PopoverMenu *result = nullptr;
    for_each_parent(*this, [&](Gtk::Widget &parent)
    {
        if (auto const menu = dynamic_cast<PopoverMenu *>(&parent)) {
            result = menu;
            return ForEachResult::_break;
        }
        return ForEachResult::_continue;
    });
    return result;
}

} // namespace Inkscape::UI::Widget

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
