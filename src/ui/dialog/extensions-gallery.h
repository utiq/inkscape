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

};


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_EXTENSIONS_H
