// SPDX-License-Identifier: GPL-2.0-or-later

#include "spin-button-tool-item.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <2geom/math-utils.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/toolbar.h>

#include "spinbutton.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/popup-menu.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

/**
 * \brief Handler for when the button's "is-focus" property changes
 *
 * \detail This just logs the current value of the spin-button
 *         and sets the _transfer_focus flag if focused-in, or unsets the
 *         _transfer_focus flag and removes the text selection if focused-out.
 */
void
SpinButtonToolItem::on_btn_is_focus_changed()
{
    auto const is_focus = _btn->is_focus();
    if (is_focus) {
        _last_val = _btn->get_value();
    } else {
        auto const position = _btn->get_position();
        _btn->select_region(position, position);
    }
    _transfer_focus = is_focus;
}

/**
 * \brief Handler for when a key is pressed when the button has focus
 *
 * \detail If the ESC key was pressed, restore the last value and defocus.
 *         If the Enter key was pressed, just defocus.
 */
bool
SpinButtonToolItem::on_btn_key_pressed(GtkEventControllerKey const * const controller,
                                       unsigned const keyval, unsigned const keycode,
                                       GdkModifierType const state)
{
    auto display = Gdk::Display::get_default();
    auto keymap  = display->get_keymap();
    guint key = 0;
    gdk_keymap_translate_keyboard_state(keymap, keycode, state,
                                        0, &key, 0, 0, 0);
    auto val = _btn->get_value();

    switch(key) {
        case GDK_KEY_Escape:
            _transfer_focus = true;
            _btn->set_value(_last_val);
            defocus();
            return true;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            _transfer_focus = true;
            defocus();
            return true;

        case GDK_KEY_Tab:
            return process_tab(1);

        case GDK_KEY_ISO_Left_Tab:
            return process_tab(-1);

        // TODO: Enable variable step-size if this is ever used
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            _transfer_focus = false;
            _btn->set_value(val+1);
            return true;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            _transfer_focus = false;
            _btn->set_value(val-1);
            return true;

        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            _transfer_focus = false;
            _btn->set_value(val+10);
            return true;

        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            _transfer_focus = false;
            _btn->set_value(val-10);
            return true;

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (Controller::has_flag(state, GDK_CONTROL_MASK)) {
                _transfer_focus = false;
                _btn->set_value(_last_val);
                return true;
            }
    }

    return false;
}

/**
 * \brief Shift focus to a different widget
 *
 * \details This only has an effect if the _transfer_focus flag and the _focus_widget are set
 */
void
SpinButtonToolItem::defocus()
{
    if(_transfer_focus && _focus_widget) {
        _focus_widget->grab_focus();
    }
}

/**
 * \brief Move focus to another spinbutton in the toolbar
 *
 * \param increment[in] The number of places to shift within the toolbar
 */
bool
SpinButtonToolItem::process_tab(int increment)
{
    // If the increment is zero, do nothing
    if(increment == 0) return true;

    // Here, we're working through the widget hierarchy:
    // Toolbar
    // |- ToolItem (*this)
    //    |-> Box
    //       |-> SpinButton (*_btn)
    //
    // Our aim is to find the next/previous spin-button within a toolitem in our toolbar

    // We only bother doing this if the current item is actually in a toolbar!
    auto toolbar = dynamic_cast<Gtk::Toolbar *>(get_parent());
    if (toolbar) {
        // Get the index of the current item within the toolbar and the total number of items
        auto my_index = toolbar->get_item_index(*this);
        auto n_items  = toolbar->get_n_items();
        auto test_index = my_index + increment; // The index of the item we want to check

        // Loop through tool items as long as we're within the limits of the toolbar and
        // we haven't yet found our new item to focus on
        while (test_index > 0 && test_index <= n_items) {
            auto tool_item = toolbar->get_nth_item(test_index);

            if(tool_item) {
                // There are now two options that we support:
                if (auto sb_tool_item = dynamic_cast<SpinButtonToolItem *>(tool_item)) {
                    // (1) The tool item is a SpinButtonToolItem, in which case, we just pass
                    //     focus to its spin-button
                    sb_tool_item->grab_button_focus();
                    return true;
                }
                else if (auto const spin_button = dynamic_cast<Gtk::SpinButton *>
                                                              (tool_item->get_child()))
                {
                    // (2) The tool item contains a plain Gtk::SpinButton, in which case we
                    //     pass focus directly to it
                    spin_button->grab_focus();
                    return true;
                }
            }

            test_index += increment;
        }
    }

    return false;
}

/**
 * \brief Handler for toggle events on numeric menu items
 *
 * \details Sets the adjustment to the desired value
 */
void
SpinButtonToolItem::on_numeric_menu_item_activate(double const value)
{
    auto adj = _btn->get_adjustment();
    adj->set_value(value);
}

UI::Widget::PopoverMenuItem *
SpinButtonToolItem::create_numeric_menu_item(Gtk::RadioButtonGroup &group,
                                             double                 value,
                                             const Glib::ustring   &label,
                                             bool enable)
{
    auto const item_label = !label.empty() ? Glib::ustring::compose("%1: %2", value, label)
                                           : Glib::ustring::format(value);
    auto const radio_button = Gtk::make_managed<Gtk::RadioButton>(group, item_label);
    radio_button->set_active(enable);

    auto const menu_item = Gtk::make_managed<UI::Widget::PopoverMenuItem>();
    menu_item->add(*radio_button);
    menu_item->signal_activate().connect(sigc::bind(
        sigc::mem_fun(*this, &SpinButtonToolItem::on_numeric_menu_item_activate), value));
    return menu_item;
}

/**
 * \brief Create a menu containing fixed numeric options for the adjustment
 *
 * \details Each of these values represents a snap-point for the adjustment's value
 *          The menu will shared_ptr.reset() on close so if it had the last reference we destroy it
 */
std::shared_ptr<UI::Widget::PopoverMenu> SpinButtonToolItem::create_numeric_menu()
{
    auto numeric_menu = std::make_shared<UI::Widget::PopoverMenu>(Gtk::POS_BOTTOM);

    // Get values for the adjustment
    auto adj = _btn->get_adjustment();
    auto adj_value = round_to_precision(adj->get_value());
    auto lower = round_to_precision(adj->get_lower());
    auto upper = round_to_precision(adj->get_upper());
    auto page = adj->get_page_increment();

    // Start by setting some fixed values based on the adjustment's
    // parameters.
    NumericMenuData values;

    // first add all custom items (necessary)
    for (auto const &custom_data : _custom_menu_data) {
        if (custom_data.first >= lower && custom_data.first <= upper) {
            values.emplace(custom_data);
        }
    }

    values.emplace(adj_value, "");

    // for quick page changes using mouse, step can changes can be done with +/- buttons on
    // SpinButton
    values.emplace(std::fmin(adj_value + page, upper), "");
    values.emplace(std::fmax(adj_value - page, lower), "");

    // add upper/lower limits to options
    if (_show_upper_limit) {
        values.emplace(upper, "");
    }
    if (_show_lower_limit) {
        values.emplace(lower, "");
    }

    Gtk::RadioButton::Group group;
    auto const add_item = [=, &group](ValueLabel const &value) {
        bool enable = (adj_value == value.first);
        auto const numeric_menu_item = create_numeric_menu_item(group, value.first, value.second, enable);
        numeric_menu->append(*numeric_menu_item);
    };

    if (_sort_decreasing) {
        std::for_each(values.crbegin(), values.crend(), add_item);
    } else {
        std::for_each(values.cbegin(), values.cend(), add_item);
    }

    return numeric_menu;
}

/**
 * \brief Create a new SpinButtonToolItem
 *
 * \param[in] name       A unique ID for this tool-item (not translatable)
 * \param[in] label_text The text to display in the toolbar
 * \param[in] adjustment The Gtk::Adjustment to attach to the spinbutton
 * \param[in] climb_rate The climb rate for the spin button (default = 0)
 * \param[in] digits     Number of decimal places to display
 */
SpinButtonToolItem::SpinButtonToolItem(const Glib::ustring            name,
                                       const Glib::ustring&           label_text,
                                       Glib::RefPtr<Gtk::Adjustment>& adjustment,
                                       double                         climb_rate,
                                       int                            digits)
    : _btn(Gtk::make_managed<SpinButton>(adjustment, climb_rate, digits)),
      _name(std::move(name)),
      _label_text(label_text),
      _digits(digits)
{
    set_margin_start(3);
    set_margin_end(3);
    set_name(_name);

    UI::on_popup_menu(*_btn, sigc::mem_fun(*this, &SpinButtonToolItem::on_popup_menu));

    _btn->property_is_focus().signal_changed().connect(
            sigc::mem_fun(*this, &SpinButtonToolItem::on_btn_is_focus_changed));

    Controller::add_key<&SpinButtonToolItem::on_btn_key_pressed>(*_btn, *this);

    _label = Gtk::make_managed<Gtk::Label>(label_text);

    _hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 3);
    _hbox->add(*_label);
    _hbox->add(*_btn);
    add(*_hbox);
    show_all();
}

void
SpinButtonToolItem::set_icon(const Glib::ustring& icon_name)
{
    _hbox->remove(*_label);
    _icon = Gtk::manage(sp_get_icon_image(icon_name, Gtk::ICON_SIZE_SMALL_TOOLBAR));

    if(_icon) {
        _hbox->pack_start(*_icon);
        _hbox->reorder_child(*_icon, 0);
    }

    show_all();
}

/**
 * \brief Create a popup menu
 */
bool
SpinButtonToolItem::on_popup_menu()
{
    auto const numeric_menu = create_numeric_menu();
    numeric_menu->popup_at_center(*_hbox);
    UI::on_hide_reset(numeric_menu);
    return true;
}

/**
 * \brief Transfers focus to the child spinbutton by default
 */
void
SpinButtonToolItem::on_grab_focus()
{
    grab_button_focus();
}

/**
 * \brief Set the tooltip to display on this (and all child widgets)
 *
 * \param[in] text The tooltip to display
 */
void
SpinButtonToolItem::set_all_tooltip_text(const Glib::ustring& text)
{
    _hbox->set_tooltip_text(text);
}

/**
 * \brief Set the widget that focus moves to when this one loses focus
 *
 * \param widget The widget that will gain focus
 */
void
SpinButtonToolItem::set_focus_widget(Gtk::Widget *widget)
{
    _focus_widget = widget;
}

/**
 * \brief Grab focus on the spin-button widget
 */
void
SpinButtonToolItem::grab_button_focus()
{
    _btn->grab_focus();
}

/**
 * \brief A wrapper of Geom::decimal_round to remember precision
 */
double
SpinButtonToolItem::round_to_precision(double value) {
    return Geom::decimal_round(value, _digits);
}

/**
 * \brief     [discouraged] Set numeric data option in Radio menu.
 *
 * \param[in] values  values to provide as options
 * \param[in] labels  label to show for the value at same index in values.
 *
 * \detail    Use is advised only when there are no labels.
 *            This is discouraged in favor of other overloads of the function, due to error prone
 *            usage. Using two vectors for related data, undermining encapsulation.
 */
void
SpinButtonToolItem::set_custom_numeric_menu_data(const std::vector<double>&        values,
                                                 const std::vector<Glib::ustring>& labels)
{

    if (values.size() != labels.size() && !labels.empty()) {
        g_warning("Cannot add custom menu items. Value and label arrays are different sizes");
        return;
    }

    _custom_menu_data.clear();

    if (labels.empty()) {
        for (const auto &value : values) {
            _custom_menu_data.emplace(round_to_precision(value), "");
        }
        return;
    }

    int i = 0;
    for (const auto &value : values) {
        _custom_menu_data.emplace(round_to_precision(value), labels[i++]);
    }
}

/**
 * \brief     Set numeric data options for Radio menu (densely labeled data).
 *
 * \param[in] value_labels  value and labels to provide as options
 *
 * \detail    Should be used when most of the values have an associated label (densely labeled data)
 *
 */
void
SpinButtonToolItem::set_custom_numeric_menu_data(const std::vector<ValueLabel>& value_labels) {
    _custom_menu_data.clear();
    for(const auto& value_label : value_labels) {
        _custom_menu_data.emplace(round_to_precision(value_label.first), value_label.second);
    }
}

/**
 * \brief     Set numeric data options for Radio menu (sparsely labeled data).
 *
 * \param[in] values         values without labels
 * \param[in] sparse_labels  value and labels to provide as options
 *
 * \detail    Should be used when very few values have an associated label (sparsely labeled data).
 *            Duplicate values in vector and map are acceptable but, values labels in map are
 *            preferred. Avoid using duplicate values intentionally though.
 *
 */
void
SpinButtonToolItem::set_custom_numeric_menu_data(const std::vector<double> &values,
                                                 const std::unordered_map<double, Glib::ustring> &sparse_labels)
{
    _custom_menu_data.clear();

    for(const auto& value_label : sparse_labels) {
        _custom_menu_data.emplace(round_to_precision(value_label.first), value_label.second);
    }

    for(const auto& value : values) {
        _custom_menu_data.emplace(round_to_precision(value), "");
    }

}

void SpinButtonToolItem::show_upper_limit(bool show) { _show_upper_limit = show; }

void SpinButtonToolItem::show_lower_limit(bool show) { _show_lower_limit = show; }

void SpinButtonToolItem::show_limits(bool show) { _show_upper_limit = _show_lower_limit = show; }

void SpinButtonToolItem::sort_decreasing(bool decreasing) { _sort_decreasing = decreasing; }

Glib::RefPtr<Gtk::Adjustment>
SpinButtonToolItem::get_adjustment()
{
    return _btn->get_adjustment();
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
