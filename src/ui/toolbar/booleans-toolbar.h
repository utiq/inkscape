// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A toolbar for the Builder tool.
 *
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2022 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLBAR_BOOLEANS_TOOLBAR_H
#define INKSCAPE_UI_TOOLBAR_BOOLEANS_TOOLBAR_H

#include <gtkmm.h>

class SPDesktop;

namespace Inkscape {
namespace UI {
namespace Toolbar {

class BooleansToolbar : public Gtk::Toolbar
{
public:
    BooleansToolbar(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &builder, SPDesktop *desktop);
    ~BooleansToolbar() override {};

    void on_parent_changed(Gtk::Widget *) override;

    static GtkWidget *create(SPDesktop *desktop);

private:
    bool was_referenced;

    Gtk::ToolButton &_btn_confirm;
    Gtk::ToolButton &_btn_cancel;
};

} // namespace Toolbar
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_TOOLBAR_BOOLEANS_TOOLBAR_H
