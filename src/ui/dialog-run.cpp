// SPDX-License-Identifier: GPL-2.0-or-later
#include <optional>

#include <glibmm/main.h>

#include "dialog-run.h"

namespace Inkscape::UI {

int dialog_run(Gtk::Dialog &dialog)
{
    std::optional<int> result;

    auto response_conn = dialog.signal_response().connect([&] (int response) {
        result = response;
    });

    auto hide_conn = dialog.signal_hide().connect([&] {
        result = Gtk::RESPONSE_NONE;
    });

    dialog.set_modal();
    dialog.show();

    auto main_context = Glib::MainContext::get_default();
    while (!result) {
        main_context->iteration(true);
    }

    response_conn.disconnect();
    hide_conn.disconnect();

    dialog.hide();

    return *result;
}

} // namespace Inkscape::UI
