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

#include "ui/widget/popover-menu.h"

#include <glibmm/main.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/stylecontext.h>

#include "ui/menuize.h"
#include "ui/popup-menu.h"
#include "ui/util.h"
#include "ui/widget/css-name-class-init.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

// Make our Grid have CSS name `menu` to try to piggyback “real” Menusʼ theming.
// Ditto, we leave Popover as `popover` so we don't lose normal Popover theming.
class PopoverMenuGrid final
    : public CssNameClassInit
    , public Gtk::Grid
{
public:
    [[nodiscard]] PopoverMenuGrid()
        : Glib::ObjectBase{"PopoverMenuGrid"}
        , CssNameClassInit{"menu"}
        , Gtk::Grid{}
    {
        get_style_context()->add_class("menu");
        set_orientation(Gtk::ORIENTATION_VERTICAL);
    }
};

PopoverMenu::PopoverMenu(Gtk::PositionType const position)
    : Glib::ObjectBase{"PopoverMenu"}
    , Gtk::Popover{}
    , _grid{*Gtk::make_managed<PopoverMenuGrid>()}
{
    auto const style_context = get_style_context();
    style_context->add_class("popover-menu");
    style_context->add_class("menu");
    set_position(position);
    add(_grid);

    signal_show().connect([this]
    {
        // Check no one (accidentally?) removes child
        g_return_if_fail(_grid.get_parent() == this);

        // FIXME: Initially focused item is sometimes wrong on first popup. GTK bug?
        // Grabbing focus in ::show does not always work & sometimes even crashes :(
        // For now, just remove possibly wrong, visible selection until hover/keynav
        // This is also nicer for menus with only 1 item, like the ToolToolbar popup
        Glib::signal_idle().connect_once( [this]{ unset_items_focus_hover(nullptr); });
    });

    // Temporarily hide tooltip of relative-to widget to avoid it covering us up
    UI::autohide_tooltip(*this);
}

void PopoverMenu::attach(Gtk::Widget &child,
                         int const left_attach, int const right_attach,
                         int const top_attach, int const bottom_attach)
{
    // Check no one (accidentally?) removes child
    g_return_if_fail(_grid.get_parent() == this);

    auto const width = right_attach - left_attach;
    auto const height = bottom_attach - top_attach;
    _grid.attach(child, left_attach, top_attach, width, height);
}

void PopoverMenu::append(Gtk::Widget &child)
{
    _grid.attach_next_to(child, Gtk::POS_BOTTOM);
}

void PopoverMenu::prepend(Gtk::Widget &child)
{
    _grid.attach_next_to(child, Gtk::POS_TOP);
}

void PopoverMenu::remove(Gtk::Widget &child)
{
    _grid.remove(child);
}

void PopoverMenu::append_section_label(Glib::ustring const &markup)
{
    auto const label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    auto const item = Gtk::make_managed<PopoverMenuItem>();
    item->add(*label);
    item->set_sensitive(false);
    append(*item);
}

void PopoverMenu::append_separator()
{
    append(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL));
}

void PopoverMenu::popup_at(Gtk::Widget &relative_to,
                           int const x_offset, int const y_offset)
{
    ::Inkscape::UI::popup_at(*this, relative_to, x_offset, y_offset);
}

void PopoverMenu::popup_at_center(Gtk::Widget &relative_to)
{
    ::Inkscape::UI::popup_at_center(*this, relative_to);
}

std::vector<Gtk::Widget *> PopoverMenu::get_items()
{
    return _grid.get_children();
}

void PopoverMenu::unset_items_focus_hover(Gtk::Widget * const except_active)
{
    for_each_child(_grid, [=](Gtk::Widget &item)
    {
        if (&item != except_active) {
            item.unset_state_flags(Gtk::STATE_FLAG_FOCUSED | Gtk::STATE_FLAG_PRELIGHT);
        }
        return ForEachResult::_continue;
    });
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
