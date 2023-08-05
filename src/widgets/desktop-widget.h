// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SP_DESKTOP_WIDGET_H
#define SEEN_SP_DESKTOP_WIDGET_H

/** \file
 * SPDesktopWidget: handling Gtk events on a desktop.
 *
 * Authors:
 *      Jon A. Cruz <jon@joncruz.org> (c) 2010
 *      John Bintz <jcoswell@coswellproductions.org> (c) 2006
 *      Ralf Stephan <ralf@ark.in-berlin.de> (c) 2005
 *      Abhishek Sharma
 *      ? -2004
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstddef>
#include <2geom/point.h>
#include <sigc++/connection.h>
#include <gtkmm.h>

#include "message.h"
#include "preferences.h"

class InkscapeWindow;
class SPDocument;
class SPDesktop;
class SPObject;

namespace Inkscape {
namespace UI {

namespace Dialog {
class DialogContainer;
class DialogMultipaned;
class SwatchesPanel;
} // namespace Dialog

namespace Toolbar {
  class  Toolbars;
  class  CommandToolbar;
  class  SnapToolbar;
} // namespace Toolbars

namespace Widget {
  class Button;
  class Canvas;
  class CanvasGrid;
  class LayerSelector;
  class PageSelector;
  class SelectedStyle;
  class SpinButton;
} // namespace Widget

} // namespace UI
} // namespace Inkscape

/// A GtkEventBox on an SPDesktop.
class SPDesktopWidget : public Gtk::EventBox
{
    using parent_type = Gtk::EventBox;

    SPDesktopWidget(InkscapeWindow *inkscape_window);

public:
    SPDesktopWidget(InkscapeWindow *inkscape_window, SPDocument *document);
    ~SPDesktopWidget() override;

    Inkscape::UI::Widget::CanvasGrid *get_canvas_grid() { return _canvas_grid; }  // Temp, I hope!
    Inkscape::UI::Widget::Canvas     *get_canvas()      { return _canvas; }

    Gio::ActionMap* get_action_map();

    void on_realize() override;
    void on_unrealize() override;

    sigc::connection modified_connection;

    SPDesktop *desktop = nullptr;

    InkscapeWindow *window = nullptr;
    Gtk::MenuBar *_menubar;

private:
    // The root vbox of the window layout.
    Gtk::Box *_vbox;

    Gtk::Paned *_tbbox = nullptr;
    Gtk::Box *_hbox = nullptr;
    Inkscape::UI::Dialog::DialogContainer *_container = nullptr;
    Inkscape::UI::Dialog::DialogMultipaned *_columns = nullptr;
    Gtk::Grid* _top_toolbars = nullptr;

    Gtk::Box     *_statusbar = nullptr;

    Inkscape::UI::Dialog::SwatchesPanel *_panels;

    Glib::RefPtr<Gtk::Adjustment> _hadj;
    Glib::RefPtr<Gtk::Adjustment> _vadj;

    Gtk::Grid *_coord_status;

    Gtk::Label *_select_status;
    Gtk::Label *_coord_status_x;
    Gtk::Label *_coord_status_y;

    Gtk::Box* _zoom_status_box;
    Inkscape::UI::Widget::SpinButton *_zoom_status;
    sigc::connection _zoom_status_input_connection;
    sigc::connection _zoom_status_output_connection;
    sigc::connection _zoom_status_value_changed_connection;
    sigc::connection _zoom_status_populate_popup_connection;

    Gtk::Box* _rotation_status_box;
    Inkscape::UI::Widget::SpinButton *_rotation_status = nullptr;
    sigc::connection _rotation_status_input_connection;
    sigc::connection _rotation_status_output_connection;
    sigc::connection _rotation_status_value_changed_connection;
    sigc::connection _rotation_status_populate_popup_connection;

    Inkscape::UI::Widget::SelectedStyle *_selected_style;

    /** A grid to display the canvas, rulers, and scrollbars. */
    Inkscape::UI::Widget::CanvasGrid *_canvas_grid;

    unsigned int _interaction_disabled_counter = 0;

public:
    double _dt2r;

private:
    Inkscape::UI::Widget::Canvas *_canvas = nullptr;
    std::vector<sigc::connection> _connections;
    Inkscape::PrefObserver _statusbar_preferences_observer;
    Inkscape::UI::Widget::LayerSelector* _layer_selector;
    Inkscape::UI::Widget::PageSelector* _page_selector;

public:
    void setMessage(Inkscape::MessageType type, gchar const *message);
    void viewSetPosition (Geom::Point p);
    void letZoomGrabFocus();
    void getWindowGeometry (gint &x, gint &y, gint &w, gint &h);
    void setWindowPosition (Geom::Point p);
    void setWindowSize (gint w, gint h);
    void setWindowTransient (void *p, int transient_policy);
    void presentWindow();
    bool showInfoDialog( Glib::ustring const &message );
    bool warnDialog (Glib::ustring const &text);
    Gtk::Toolbar* get_toolbar_by_name(const Glib::ustring& name);
    void setToolboxFocusTo (gchar const *);
    void setToolboxAdjustmentValue (gchar const * id, double value);
    bool isToolboxButtonActive (gchar const *id);
    void setCoordinateStatus(Geom::Point p);
    void enableInteraction();
    void disableInteraction();
    void updateTitle(gchar const *uri);
    bool onFocusInEvent(GdkEventFocus *);
    Inkscape::UI::Dialog::DialogContainer *getDialogContainer();
    void showNotice(Glib::ustring const &msg, unsigned timeout = 0);

    Gtk::MenuBar *menubar() { return _menubar; }

    void updateNamedview();
    void update_guides_lock();

    // Canvas Grid Widget
    void update_zoom();
    void update_rotation();
    void repack_snaptoolbar();

    void iconify();
    void maximize();
    void fullscreen();

    void layoutWidgets();
    void toggle_scrollbars();
    void toggle_command_palette();
    void toggle_rulers();
    void sticky_zoom_toggled();
    void sticky_zoom_updated();

    Gtk::Widget *get_tool_toolbox() const { return tool_toolbox; }
    Gtk::Widget *get_hbox() const { return _hbox; }

private:
    Gtk::Widget *tool_toolbox;
    Inkscape::UI::Toolbar::Toolbars *tool_toolbars;
    Inkscape::UI::Toolbar::CommandToolbar *command_toolbar;
    Inkscape::UI::Toolbar::SnapToolbar *snap_toolbar;
    Inkscape::PrefObserver _tb_snap_pos;
    Inkscape::PrefObserver _tb_icon_sizes1;
    Inkscape::PrefObserver _tb_icon_sizes2;
    Inkscape::PrefObserver _tb_visible_buttons;
    Inkscape::PrefObserver _ds_sticky_zoom;

    void namedviewModified(SPObject *obj, unsigned flags);
    int zoom_input(double *new_val);
    bool zoom_output();
    void zoom_value_changed();
    void zoom_menu_handler(double factor);
    void zoom_populate_popup(Gtk::Menu *menu);
    bool rotation_output();
    void rotation_value_changed();
    void rotation_populate_popup(Gtk::Menu *menu);
    void update_statusbar_visibility();
    void apply_ctrlbar_settings();
};

#endif /* !SEEN_SP_DESKTOP_WIDGET_H */

/*
   Local Variables:
mode:c++
c-file-style:"stroustrup"
c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
indent-tabs-mode:nil
fill-column:99
End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
