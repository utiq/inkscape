// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_TOOLBAR_TOOL_H
#define SEEN_TOOLBAR_TOOL_H

/*
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm.h>

#include "preferences.h"

class InkscapeWindow;
class SPDesktop;

namespace Inkscape::UI::Toolbar {

class ToolToolbar : public Gtk::Box {
public:
    ToolToolbar(InkscapeWindow *window);
    ~ToolToolbar() override = default;

    void set_visible_buttons();
    static Glib::ustring get_tool_visible_button_path(const Glib::ustring& button_action_name);

private:
    Gtk::Menu* getContextMenu(Glib::ustring tool_name, InkscapeWindow *window);
    void attachHandlers(Glib::RefPtr<Gtk::Builder> builder, InkscapeWindow *window);

    static constexpr const char* tools_button_path = "/toolbox/tools/buttons";

    Inkscape::PrefObserver buttons_pref_observer;
};

} // namespace Inkscape::UI::Toolbar


#endif /* SEEN_TOOLBAR_TOOL_H */

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
