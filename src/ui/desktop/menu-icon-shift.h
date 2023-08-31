// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Shift Gtk::MenuItems with icons to align with Toggle and Radio buttons.
 */
/*
 * Authors:
 *   Tavmjong Bah       <tavmjong@free.fr>
 *   Patrick Storz      <eduard.braun2@gmx.de>
 *   Daniel Boles       <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2020-2023 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#ifndef SEEN_DESKTOP_MENU_ITEM_SHIFT_H
#define SEEN_DESKTOP_MENU_ITEM_SHIFT_H

namespace Gtk {
class MenuShell;
} // namespace Gtk

bool shift_icons(Gtk::MenuShell *menu);

#endif // SEEN_DESKTOP_MENU_ITEM_SHIFT_H

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
