// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Derek P. Moore <derekm@hackunix.org>
 *   Bryce Harrington <bryce@bryceharrington.org>
 *   Johan Engelen <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Copyright (C) 2004-2011 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "scalar.h"

#include <gtkmm/scale.h>

#include "spinbutton.h"

namespace Inkscape::UI::Widget {

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::ustring const &icon,
               bool mnemonic)
    : Labelled(label, tooltip, new SpinButton(), icon, mnemonic),
      setProgrammatically(false)
{
}

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Labelled(label, tooltip, new SpinButton(0.0, digits), icon, mnemonic),
      setProgrammatically(false)
{
}

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::RefPtr<Gtk::Adjustment> adjust,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Labelled(label, tooltip, new SpinButton(std::move(adjust), 0.0, digits), icon, mnemonic),
      setProgrammatically(false)
{
}

unsigned Scalar::getDigits() const
{
    g_assert(_widget != nullptr);
    return dynamic_cast<SpinButton const &>(*_widget).get_digits();
}

double Scalar::getStep() const
{
    g_assert(_widget != nullptr);
    double step, page;
    dynamic_cast<SpinButton const &>(*_widget).get_increments(step, page);
    return step;
}

double Scalar::getPage() const
{
    g_assert(_widget != nullptr);
    double step, page;
    dynamic_cast<SpinButton const &>(*_widget).get_increments(step, page);
    return page;
}

double Scalar::getRangeMin() const
{
    g_assert(_widget != nullptr);
    double min, max;
    dynamic_cast<SpinButton const &>(*_widget).get_range(min, max);
    return min;
}

double Scalar::getRangeMax() const
{
    g_assert(_widget != nullptr);
    double min, max;
    dynamic_cast<SpinButton const &>(*_widget).get_range(min, max);
    return max;
}

double Scalar::getValue() const
{
    g_assert(_widget != nullptr);
    return dynamic_cast<SpinButton const &>(*_widget).get_value();
}

int Scalar::getValueAsInt() const
{
    g_assert(_widget != nullptr);
    return dynamic_cast<SpinButton const &>(*_widget).get_value_as_int();
}


void Scalar::setDigits(unsigned digits)
{
    g_assert(_widget != nullptr);
    dynamic_cast<SpinButton &>(*_widget).set_digits(digits);
}

void Scalar::setNoLeadingZeros()
{
    g_assert(_widget != nullptr);
    if (getDigits()) {
        auto &spin_button = dynamic_cast<SpinButton &>(*_widget);
        spin_button.set_numeric(false);
        spin_button.set_update_policy(Gtk::UPDATE_ALWAYS);
        spin_button.signal_output().connect(sigc::mem_fun(*this, &Scalar::setNoLeadingZerosOutput));
    }
}

bool
Scalar::setNoLeadingZerosOutput()
{
    g_assert(_widget != nullptr);
    auto &spin_button = dynamic_cast<SpinButton &>(*_widget);
    double digits = std::pow(10.0, spin_button.get_digits());
    double val = std::round(spin_button.get_value() * digits) / digits;
    spin_button.set_text(Glib::ustring::format(val));
    return true;
}

void 
Scalar::setWidthChars(gint width_chars) {
    g_assert(_widget != nullptr);
    dynamic_cast<SpinButton &>(*_widget).property_width_chars() = width_chars;
}

void Scalar::setIncrements(double step, double /*page*/)
{
    g_assert(_widget != nullptr);
    dynamic_cast<SpinButton &>(*_widget).set_increments(step, 0);
}

void Scalar::setRange(double min, double max)
{
    g_assert(_widget != nullptr);
    dynamic_cast<SpinButton &>(*_widget).set_range(min, max);
}

void Scalar::setValue(double value, bool setProg)
{
    g_assert(_widget != nullptr);
    if (setProg) {
        setProgrammatically = true; // callback is supposed to reset back, if it cares
    }
    dynamic_cast<SpinButton &>(*_widget).set_value(value);
    setProgrammatically = false;
}

void Scalar::setWidthChars(unsigned chars)
{
    g_assert(_widget != NULL);
    dynamic_cast<SpinButton &>(*_widget).set_width_chars(chars);
}

void Scalar::update()
{
    g_assert(_widget != nullptr);
    dynamic_cast<SpinButton &>(*_widget).update();
}

void Scalar::addSlider()
{
    auto const scale = Gtk::make_managed<Gtk::Scale>(dynamic_cast<SpinButton &>(*_widget).get_adjustment());
    scale->set_draw_value(false);
    pack_start(*manage (scale));
}

Glib::SignalProxy<void> Scalar::signal_value_changed()
{
    return dynamic_cast<SpinButton &>(*_widget).signal_value_changed();
}

Glib::SignalProxy<bool, GdkEventButton*> Scalar::signal_button_release_event()
{
    return dynamic_cast<SpinButton &>(*_widget).signal_button_release_event();
}

void Scalar::hide_label() {
    if (auto label = const_cast<Gtk::Label*>(getLabel())) {
        label->set_visible(false);
        label->set_no_show_all();
        label->set_hexpand(true);
    }
    if (_widget) {
        remove(*_widget);
        _widget->set_hexpand();
        this->pack_end(*_widget);
    }
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
