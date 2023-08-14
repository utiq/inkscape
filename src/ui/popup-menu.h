// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helpers to connect signals to events that popup a menu in both GTK3 and GTK4.
 * Plus miscellaneous helpers primarily useful with widgets used as popop menus.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_POPUP_MENU_H
#define SEEN_UI_POPUP_MENU_H

#include <memory>
#include <sigc++/connection.h>
#include <sigc++/slot.h>

namespace Gtk {
class Widget;
} // namespace Gtk

namespace Inkscape::UI {

/// Return whether a popup was activated.
using PopupMenuSlot = sigc::slot<bool()>;

/// Connect slot to a widgetʼs key and button events that traditionally trigger a popup menu, i.e.:
/// * The keys used by GTK3ʼs signal Widget::popup-menu: the Menu key, or the Shift+F10 combination
/// * The right mouse button or other platform convention, as per gtk_event_triggers_context_menu()
/// The slot is passed to manage() which moves it to a new address & we return a reference to that.
PopupMenuSlot &on_popup_menu(Gtk::Widget &widget, PopupMenuSlot &&slot);

/// Connects ::hide of widget to reset() the shared_ptr i.e. to ‘self-destruct’.
/// @returns A connection that can be used to disconnect & prevent self-destruct
sigc::connection on_hide_reset(std::shared_ptr<Gtk::Widget> const &widget);

} // namespace Inkscape::UI

#endif // SEEN_UI_POPUP_MENU_H

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
