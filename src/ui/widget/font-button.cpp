// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "font-button.h"

#include <gtkmm/fontbutton.h>

namespace Inkscape::UI::Widget {

FontButton::FontButton(Glib::ustring const &label, Glib::ustring const &tooltip,
                       Glib::ustring const &icon, bool mnemonic)
    : Labelled{label, tooltip, new Gtk::FontButton{"Sans 10"}, icon, mnemonic}
{
}

Glib::ustring FontButton::getValue() const
{
    g_assert(_widget);
    return dynamic_cast<Gtk::FontButton const &>(*_widget).get_font_name();
}

void FontButton::setValue(Glib::ustring const &fontspec)
{
    g_assert(_widget);
    dynamic_cast<Gtk::FontButton &>(*_widget).set_font_name(fontspec);
}

Glib::SignalProxy<void> FontButton::signal_font_value_changed()
{
    g_assert(_widget);
    return dynamic_cast<Gtk::FontButton &>(*_widget).signal_font_set();
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
