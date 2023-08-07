// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SEEN_SPIN_BUTTON_TOOL_ITEM_H
#define SEEN_SPIN_BUTTON_TOOL_ITEM_H

#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/toolitem.h>

namespace Gtk {
class Adjustment;
class Box;
class GestureMultiPress;
class RadioButtonGroup;
class Widget;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class PopoverMenu;
class PopoverMenuItem;
class SpinButton;

/**
 * \brief A spin-button with a label that can be added to a toolbar
 */
class SpinButtonToolItem : public Gtk::ToolItem
{
private:
    using ValueLabel = std::pair<double, Glib::ustring>;
    using NumericMenuData = std::map<double, Glib::ustring>;

    Glib::ustring  _name;                  ///< A unique ID for the widget (NOT translatable)
    SpinButton    *_btn;                   ///< The spin-button within the widget
    Glib::ustring  _label_text;            ///< A string to use in labels for the widget (translatable)
    double         _last_val = 0.0;        ///< The last value of the adjustment
    bool           _transfer_focus = false; ///< Whether or not to transfer focus

    Gtk::Box    *_hbox;       ///< Horizontal box, to store widgets
    Gtk::Widget *_label;      ///< A text label to describe the setting
    Gtk::Widget *_icon;       ///< An icon to describe the setting

    /** A widget that grabs focus when this one loses it */
    Gtk::Widget * _focus_widget = nullptr;

    // Custom values and labels to add to the numeric popup-menu
    NumericMenuData _custom_menu_data;

    // To show or not to show upper/lower limit of the adjustment
    bool _show_upper_limit = false;
    bool _show_lower_limit = false;

    // sort in decreasing order
    bool _sort_decreasing = false;

    // digits of adjustment
    int _digits;

    // just a wrapper for Geom::decimal_round to simplify calls
    double round_to_precision(double value);

    // Event handlers
    void on_btn_is_focus_changed();
    bool on_btn_key_pressed(GtkEventControllerKey const *controller,
                            unsigned keyval, unsigned keycode, GdkModifierType state);
    Gtk::EventSequenceState on_btn_click_pressed(Gtk::GestureMultiPress const &click,
                                                 int n_press, double x, double y);
    bool on_popup_menu();
    void do_popup_menu();

    void defocus();
    bool process_tab(int direction);

    void on_numeric_menu_item_activate(double value);

    void create_numeric_menu();
    UI::Widget::PopoverMenuItem *create_numeric_menu_item(Gtk::RadioButtonGroup &group,
                                                          double                 value,
                                                          const Glib::ustring   &label = {},
                                                          bool                   enable = false);

protected:
    void on_grab_focus() override;

public:
    SpinButtonToolItem(const Glib::ustring            name,
                       const Glib::ustring&           label_text,
                       Glib::RefPtr<Gtk::Adjustment>& adjustment,
                       double                         climb_rate = 0.1,
                       int                            digits     = 3);

    void set_all_tooltip_text(const Glib::ustring& text);
    void set_focus_widget(Gtk::Widget *widget);
    void grab_button_focus();

    void set_custom_numeric_menu_data(const std::vector<double>&        values,
                                      const std::vector<Glib::ustring>& labels = std::vector<Glib::ustring>());

    void set_custom_numeric_menu_data(const std::vector<ValueLabel> &value_labels);

    void set_custom_numeric_menu_data(const std::vector<double>&                       values,
                                      const std::unordered_map<double, Glib::ustring>& sparse_labels);

    Glib::RefPtr<Gtk::Adjustment> get_adjustment();
    void set_icon(const Glib::ustring& icon_name);

    // display limits
    void show_upper_limit(bool show = true);
    void show_lower_limit(bool show = true);
    void show_limits     (bool show = true);

    // sorting order
    void sort_decreasing(bool decreasing = true);

    SpinButton *get_spin_button() { return _btn; };
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_SPIN_BUTTON_TOOL_ITEM_H

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
