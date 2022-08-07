// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_UI_DIALOG_RUN_H
#define INKSCAPE_UI_DIALOG_RUN_H

#include <gtkmm/dialog.h>

namespace Inkscape::UI {

/**
 * This is a GTK4 porting aid meant to replace the removal of the Gtk::Dialog synchronous API.
 *
 * It is intended as a temporary measure, although experience suggests it will be anything but.
 *
 * Todo: Attempt to port code that uses this function to the asynchronous API.
 */
int dialog_run(Gtk::Dialog &dialog);

} // namespace Inkscape::UI

#endif // INKSCAPE_UI_DIALOG_RUN_H
