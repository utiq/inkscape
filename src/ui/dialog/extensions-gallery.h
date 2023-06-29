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

#include <gtkmm/button.h>
#include <gtkmm/iconview.h>
#include <gtkmm/searchentry.h>
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
    Gtk::Button& _run;

    Gtk::CellRendererPixbuf _image_renderer;
    Glib::RefPtr<Gtk::ListStore> _store;

    Gtk::TreeModel::Row selected_item();
    void update_name();
};


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_EXTENSIONS_H
