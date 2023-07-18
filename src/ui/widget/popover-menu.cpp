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

#include <gtkmm/grid.h>
#include <gtkmm/stylecontext.h>
#include "ui/util.h"
#include "ui/widget/popover-menu.h"
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

PopoverMenu::PopoverMenu()
    : Glib::ObjectBase{"PopoverMenu"}
    , Gtk::Popover{}
    , _grid{*Gtk::make_managed<PopoverMenuGrid>()}
{
    auto const style_context = get_style_context();
    style_context->add_class("popover-menu");
    style_context->add_class("menu");
    set_position(Gtk::POS_BOTTOM);
    add(_grid);

    // FIXME: Initially focused item is sometimes wrong on first popup. GTK bug?
}

void PopoverMenu::attach(Gtk::Widget &child,
                         int const left_attach, int const right_attach,
                         int const top_attach, int const bottom_attach)
{
    auto const width = right_attach - left_attach;
    auto const height = bottom_attach - top_attach;
    _grid.attach(child, left_attach, top_attach, width, height);
}

void PopoverMenu::append(Gtk::Widget &child)
{
    _grid.attach_next_to(child, Gtk::POS_BOTTOM);
}

void PopoverMenu::remove(Gtk::Widget &child)
{
    _grid.remove(child);
}

void PopoverMenu::popup_at(Gtk::Widget &relative_to)
{
    set_relative_to(relative_to);
    popup();
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
