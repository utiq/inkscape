// SPDX-License-Identifier: GPL-2.0-or-later

#include "extensions-gallery.h"
#include <gtkmm/box.h>
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-base.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

ExtensionsGallery::ExtensionsGallery() :
    DialogBase("/dialogs/extensions-gallery", "ExtensionsGallery"),
    _builder(create_builder("dialog-extensions.glade"))
{

    add(get_widget<Gtk::Box>(_builder, "main"));
}

} } } // namespaces
