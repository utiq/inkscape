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
#include <boost/compute/detail/lru_cache.hpp>
#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"

namespace Inkscape::UI::Dialog {

class ExtensionsGallery : public DialogBase
{
public:
    enum Type { Filters, Effects };
    ExtensionsGallery(Type type);

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
    int _thumb_size_index = 0;
    Type _type;
    boost::compute::detail::lru_cache<std::string, Cairo::RefPtr<Cairo::Surface>> _image_cache;
    Cairo::RefPtr<Cairo::ImageSurface> _blank_image;

    Gtk::TreeModel::Row selected_item();
    void update_name();
    void show_category(const Glib::ustring& id);
    void refilter();
    void rebuild();
    void get_cell_data_func(Gtk::CellRenderer* cell_renderer, Gtk::TreeModel::Row row, bool visible);
};

} // namespace

#endif // INKSCAPE_UI_DIALOG_EXTENSIONS_H
