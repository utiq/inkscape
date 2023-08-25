// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *
 * Copyright (C) 2004 Carl Hetherington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "text.h"

#include <gtkmm/entry.h>

namespace Inkscape::UI::Widget {

Text::Text(Glib::ustring const &label, Glib::ustring const &tooltip,
           Glib::ustring const &icon,
           bool mnemonic)
    : Labelled{label, tooltip, new Gtk::Entry{}, icon, mnemonic},
      setProgrammatically(false)
{
}

Glib::ustring const Text::getText() const
{
    g_assert(_widget);
    return dynamic_cast<Gtk::Entry const &>(*_widget).get_text();
}

void Text::setText(Glib::ustring const text)
{
    g_assert(_widget);
    setProgrammatically = true; // callback is supposed to reset back, if it cares
    dynamic_cast<Gtk::Entry &>(*_widget).set_text(text); // FIXME: set correctly
}

Glib::SignalProxy<void> Text::signal_activate()
{
    g_assert(_widget);
    return dynamic_cast<Gtk::Entry &>(*_widget).signal_activate();
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
