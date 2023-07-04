// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Extensions gallery
 */
/* Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_EXTENSIONS_H
#define INKSCAPE_UI_DIALOG_EXTENSIONS_H

#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/iconview.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treeview.h>
#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"

namespace Inkscape {
// class Selection;
namespace UI {
namespace Dialog {

/**
 */

class ExtensionsGallery : public DialogBase
{
public:
    ExtensionsGallery();

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::IconView& _grid;
    Gtk::SearchEntry& _search;
    Gtk::TreeView& _selector;
    Gtk::Button& _run;
    Glib::ustring _run_label;

    Gtk::CellRendererPixbuf _image_renderer;
    Glib::RefPtr<Gtk::ListStore> _store;
    Glib::RefPtr<Gtk::TreeModelFilter> _filtered;
    Glib::RefPtr<Gtk::ListStore> _categories;
    auto_connection _selection_change;
    Glib::RefPtr<Gtk::TreeSelection> _page_selection;
    Glib::ustring _current_category;

    Gtk::TreeModel::Row selected_item();
    void update_name();
    void show_category(const Glib::ustring& id);
    void refilter();
};


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_EXTENSIONS_H
