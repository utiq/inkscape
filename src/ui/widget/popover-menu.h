// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::Menu, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_POPOVER_MENU_H
#define SEEN_UI_WIDGET_POPOVER_MENU_H

#include <vector>
#include <gtkmm/popover.h>

namespace Glib {
class ustring;
} // namespace Glib

namespace Inkscape::UI::Widget {

// TODO: GTK4: Can we use Gtk::GridView, Gio::ListModel, etc.?

/// Gtk::Grid subclass provides CSS name `menu`
class PopoverMenuGrid;

class PopoverMenuItem;

/// A replacement for GTK3ʼs Gtk::Menu, as removed in GTK4.
/// Aim is to be a minimal but mostly “drop-in” replacement
/// for Menus, including grid and activation functionality.
class PopoverMenu final : public Gtk::Popover {
public:
    /// Create popover with CSS classes `.menu` & `.popover-menu`,
    /// positioned as requested vs. relative-to/popup_at() widget.
    [[nodiscard]] PopoverMenu(Gtk::PositionType const position);

    /// Add child at pos as per Gtk::Menu::attach()
    void attach(Gtk::Widget &child,
                int left_attach, int right_attach,
                int top_attach, int bottom_attach);
    /// Add new row containing child, at start/top
    void append(Gtk::Widget &child);
    /// Add new row containing child, at end/bottom
    void prepend(Gtk::Widget &child);
    /// Remove/unparent added child.
    void remove(Gtk::Widget &child);

    /// Append label, w/ markup & the .dim-label style class.
    void append_section_label(Glib::ustring const &markup);
    /// Append a horizontal separator.
    void append_separator();

    /// Replace Gtk::Menu::popup_at_pointer. If x or y
    /// offsets != 0, :pointing-to is set to {x,y,1,1}
    void popup_at(Gtk::Widget &relative_to,
                  int x_offset = 0, int y_offset = 0);
    /// As popup_at() but point to center of widget
    void popup_at_center(Gtk::Widget &relative_to);

    /// Get the list of menu items (children of our grid)
    [[nodiscard]] std::vector<Gtk::Widget *> get_items();

    /// This would give not the items, rather an internal Grid. Use get_items().
    void get_children() const = delete;
    /// @copydoc get_children() const
    void get_children()       = delete;

private:
    PopoverMenuGrid &_grid;

    // Let PopoverMenuItem call this without making it public API
    friend class PopoverMenuItem;
    void unset_items_focus_hover(Gtk::Widget *except_active);
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_POPOVER_MENU_H

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
