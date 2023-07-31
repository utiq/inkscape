// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Desktop widget implementation
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   John Bintz <jcoswell@coswellproductions.org>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2007 Johan Engelen
 * Copyright (C) 2006 John Bintz
 * Copyright (C) 2004 MenTaLguY
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "desktop-widget.h"

#include <algorithm>
#include <string>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/toolbar.h>
#include <2geom/rect.h>

#include "conn-avoid-ref.h"
#include "desktop.h"
#include "document-undo.h"
#include "enums.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "display/control/canvas-item-drawing.h"
#include "object/sp-image.h"
#include "object/sp-namedview.h"
#include "ui/dialog/swatches.h"
#include "ui/dialog-run.h"
#include "ui/monitor.h"   // Monitor aspect ratio
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-multipaned.h"
#include "ui/dialog/dialog-window.h"
#include "ui/toolbar/toolbar-constants.h"
#include "ui/toolbar/command-toolbar.h"
#include "ui/toolbar/snap-toolbar.h"
#include "ui/toolbar/tool-toolbar.h"
#include "ui/toolbar/toolbars.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/canvas-grid.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/ink-ruler.h"
#include "ui/widget/layer-selector.h"
#include "ui/widget/page-selector.h"
#include "ui/widget/selected-style.h"
#include "ui/widget/spin-button-tool-item.h"
#include "ui/widget/status-bar.h"
#include "ui/widget/unit-tracker.h"
#include "ui/themes.h"
#include "util/units.h"
// We're in the "widgets" directory, so no need to explicitly prefix these:
#include "widget-sizes.h"

using Inkscape::DocumentUndo;
using Inkscape::UI::Dialog::DialogContainer;
using Inkscape::UI::Dialog::DialogMultipaned;
using Inkscape::UI::Dialog::DialogWindow;
using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::unit_table;

//---------------------------------------------------------------------
/* SPDesktopWidget */

SPDesktopWidget::SPDesktopWidget(InkscapeWindow *inkscape_window, SPDocument *document)
    : window (inkscape_window)
{
    set_name("SPDesktopWidget");

    auto *const dtw = this;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    /* Main table */
    dtw->_vbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
    dtw->_vbox->set_name("DesktopMainTable");
    dtw->add(*dtw->_vbox);

    /* Status bar */
    dtw->_statusbar = Gtk::make_managed<Inkscape::UI::Widget::StatusBar>();
    dtw->_vbox->pack_end(*dtw->_statusbar, false, true);

    /* Swatch Bar */
    dtw->_panels = Gtk::make_managed<Inkscape::UI::Dialog::SwatchesPanel>(true, "/embedded/swatches");
    dtw->_panels->set_vexpand(false);
    dtw->_vbox->pack_end(*dtw->_panels, false, true);

    /* DesktopHBox (Vertical toolboxes, canvas) */
    dtw->_hbox = Gtk::make_managed<Gtk::Box>();
    dtw->_hbox->set_name("DesktopHbox");

    dtw->_tbbox = Gtk::make_managed<Gtk::Paned>(Gtk::ORIENTATION_HORIZONTAL);
    dtw->_tbbox->set_name("ToolboxCanvasPaned");
    dtw->_hbox->pack_start(*dtw->_tbbox, true, true);

    dtw->_vbox->pack_end(*dtw->_hbox, true, true);

    dtw->_top_toolbars = Gtk::make_managed<Gtk::Grid>();
    dtw->_top_toolbars->set_name("TopToolbars");
    dtw->_vbox->pack_end(*dtw->_top_toolbars, false, true);

    /* Toolboxes */
    dtw->command_toolbar = Gtk::make_managed<Inkscape::UI::Toolbar::CommandToolbar>();
    dtw->_top_toolbars->attach(*command_toolbar, 0, 0);

    dtw->tool_toolbars = Gtk::make_managed<Inkscape::UI::Toolbar::Toolbars>();
    dtw->_top_toolbars->attach(*dtw->tool_toolbars, 0, 1);

    dtw->tool_toolbox = Gtk::make_managed<Inkscape::UI::Toolbar::ToolToolbar>(inkscape_window);
    dtw->_tbbox->pack1(*dtw->tool_toolbox, false, false);
    auto adjust_pos = [=](){
        int minimum_width, natural_width;
        dtw->tool_toolbox->get_preferred_width(minimum_width, natural_width);
        if (minimum_width > 0) {
            int pos = dtw->_tbbox->get_position();
            int new_pos = pos + minimum_width / 2;
            const auto max = 5; // max buttons in a row
            new_pos = std::min(new_pos - new_pos % minimum_width, max * minimum_width);
            if (pos != new_pos) dtw->_tbbox->set_position(new_pos);
        }
    };
    dtw->_tbbox->property_position().signal_changed().connect([=](){ adjust_pos(); });

    dtw->snap_toolbar = Gtk::make_managed<Inkscape::UI::Toolbar::SnapToolbar>();
    dtw->_hbox->pack_end(*dtw->snap_toolbar, false, true); // May moved later.

    _tb_snap_pos = prefs->createObserver("/toolbox/simplesnap", sigc::mem_fun(*this, &SPDesktopWidget::repack_snaptoolbar));
    repack_snaptoolbar();

    auto tbox_width = prefs->getEntry("/toolbox/tools/width");
    if (tbox_width.isValid()) {
        _tbbox->set_position(tbox_width.getIntLimited(32, 8, 500));
    }

    auto set_toolbar_prefs = [=]() {
        int min = Inkscape::UI::Toolbar::min_pixel_size;
        int max = Inkscape::UI::Toolbar::max_pixel_size;
        int s = prefs->getIntLimited(Inkscape::UI::Toolbar::tools_icon_size, min, min, max);
        Inkscape::UI::set_icon_sizes(tool_toolbox->gobj(), s);
        adjust_pos();
    };

    // watch for changes
    _tb_icon_sizes1 = prefs->createObserver(Inkscape::UI::Toolbar::tools_icon_size,    [=]() { set_toolbar_prefs(); });
    _tb_icon_sizes2 = prefs->createObserver(Inkscape::UI::Toolbar::ctrlbars_icon_size, [=]() { apply_ctrlbar_settings(); });

    // restore preferences
    set_toolbar_prefs();
    apply_ctrlbar_settings();

    /* Canvas Grid (canvas, rulers, scrollbars, etc.) */
    // desktop widgets owns it
    _canvas_grid = new Inkscape::UI::Widget::CanvasGrid(this);

    /* Canvas */
    dtw->_canvas = _canvas_grid->GetCanvas();

    _ds_sticky_zoom = prefs->createObserver("/options/stickyzoom/value", [=]() { sticky_zoom_updated(); });
    sticky_zoom_updated();

    /* Dialog Container */
    _container = Gtk::make_managed<DialogContainer>(inkscape_window);
    _columns = _container->get_columns();
    _columns->set_dropzone_sizes(2, -1);
    dtw->_tbbox->pack2(*_container, true, true);

    _canvas_grid->set_hexpand(true);
    _canvas_grid->set_vexpand(true);
    _columns->append(_canvas_grid);

    // ------------------ Finish Up -------------------- //
    dtw->_vbox->show_all();
    dtw->_canvas_grid->ShowCommandPalette(false);

    dtw->_canvas->grab_focus();

    dtw->snap_toolbar->mode_update(); // Hide/show parts.

    SPNamedView *namedview = document->getNamedView();
    dtw->_dt2r = 1. / namedview->display_units->factor;

    // ---------- Desktop Dependent Setup -------------- //
    // This section seems backwards!
    dtw->desktop = new SPDesktop(); // An SPDesktop is a View::View
    dtw->desktop->init (namedview, dtw->_canvas, this);
    dtw->_canvas->set_desktop(desktop);
    INKSCAPE.add_desktop (dtw->desktop);

    // Add the shape geometry to libavoid for autorouting connectors.
    // This needs desktop set for its spacing preferences.
    init_avoided_shape_geometry(dtw->desktop);

    dtw->_statusbar->set_desktop(dtw->desktop);

    /* Once desktop is set, we can update rulers */
    dtw->_canvas_grid->updateRulers();

    /* Listen on namedview modification */
    dtw->modified_connection = namedview->connectModified(sigc::mem_fun(*dtw, &SPDesktopWidget::namedviewModified));

    // tool_toolbars is an empty Gtk::Box at this point, fill it.
    dtw->tool_toolbars->create_toolbars(dtw->desktop);

    dtw->layoutWidgets();

    dtw->_panels->setDesktop(dtw->desktop);
}

void SPDesktopWidget::apply_ctrlbar_settings() {
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    int min = Inkscape::UI::Toolbar::min_pixel_size;
    int max = Inkscape::UI::Toolbar::max_pixel_size;
    int size = prefs->getIntLimited(Inkscape::UI::Toolbar::ctrlbars_icon_size, min, min, max);
    Inkscape::UI::set_icon_sizes(snap_toolbar, size);
    Inkscape::UI::set_icon_sizes(command_toolbar, size);
    Inkscape::UI::set_icon_sizes(tool_toolbars, size);
}

void
SPDesktopWidget::setMessage (Inkscape::MessageType type, const gchar *message)
{
    _statusbar->set_message (type, message);
}

/**
 * Called before SPDesktopWidget destruction.
 * (Might be called more than once)
 */
void
SPDesktopWidget::on_unrealize()
{
    auto dtw = this;

    if (_tbbox) {
        Inkscape::Preferences::get()->setInt("/toolbox/tools/width", _tbbox->get_position());
    }

    if (dtw->desktop) {
        for (auto &conn : dtw->_connections) {
            conn.disconnect();
        }

        // Canvas
        dtw->_canvas->set_drawing(nullptr); // Ensures deactivation
        dtw->_canvas->set_desktop(nullptr); // Todo: Remove desktop dependency.

        dtw->_panels->setDesktop(nullptr);

        delete _container; // will unrealize dtw->_canvas

        INKSCAPE.remove_desktop(dtw->desktop); // clears selection and event_context
        dtw->modified_connection.disconnect();
        dtw->desktop->destroy();
        delete dtw->desktop;
        dtw->desktop = nullptr;
    }

    parent_type::on_unrealize();
}

SPDesktopWidget::~SPDesktopWidget() {
    delete _canvas_grid;
}

/**
 * Set the title in the desktop-window (if desktop has an own window).
 *
 * The title has form file name: desktop number - Inkscape.
 * The desktop number is only shown if it's 2 or higher,
 */
void
SPDesktopWidget::updateTitle(gchar const* uri)
{
    if (window) {

        SPDocument *doc = this->desktop->doc();
        auto namedview = doc->getNamedView();

        std::string Name;
        if (doc->isModifiedSinceSave()) {
            Name += "*";
        }

        Name += uri;

        if (namedview->viewcount > 1) {
            Name += ": ";
            Name += std::to_string(namedview->viewcount);
        }
        Name += " (";

        auto render_mode = desktop->getCanvas()->get_render_mode();
        auto color_mode  = desktop->getCanvas()->get_color_mode();

        if (render_mode == Inkscape::RenderMode::OUTLINE) {
            Name += N_("outline");
        } else if (render_mode == Inkscape::RenderMode::NO_FILTERS) {
            Name += N_("no filters");
        } else if (render_mode == Inkscape::RenderMode::VISIBLE_HAIRLINES) {
            Name += N_("enhance thin lines");
        } else if (render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) {
            Name += N_("outline overlay");
        }

        if (color_mode != Inkscape::ColorMode::NORMAL &&
            render_mode != Inkscape::RenderMode::NORMAL) {
                Name += ", ";
        }

        if (color_mode == Inkscape::ColorMode::GRAYSCALE) {
            Name += N_("grayscale");
        } else if (color_mode == Inkscape::ColorMode::PRINT_COLORS_PREVIEW) {
            Name += N_("print colors preview");
        }

        if (*Name.rbegin() == '(') {  // Can not use C++11 .back() or .pop_back() with ustring!
            Name.erase(Name.size() - 2);
        } else {
            Name += ")";
        }

        Name += " - Inkscape";

        // Name += " (";
        // Name += Inkscape::version_string;
        // Name += ")";

        window->set_title (Name);
    }
}

DialogContainer *SPDesktopWidget::getDialogContainer()
{
    return _container;
}

void SPDesktopWidget::showNotice(Glib::ustring const &msg, unsigned timeout)
{
    _canvas_grid->showNotice(msg, timeout);
}

/**
 * Callback to realize desktop widget.
 */
void SPDesktopWidget::on_realize()
{
    SPDesktopWidget *dtw = this;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    parent_type::on_realize();

    Geom::Rect d = Geom::Rect::from_xywh(Geom::Point(0,0), (dtw->desktop->doc())->getDimensions());

    if (d.width() < 1.0 || d.height() < 1.0) return;

    dtw->desktop->set_display_area (d, 10);

    dtw->updateNamedview();
    auto const window = dynamic_cast<Gtk::Window *>(get_toplevel());
    if (window) {
        auto const dark = INKSCAPE.themecontext->isCurrentThemeDark(window);
        prefs->setBool("/theme/darkTheme", dark);
        INKSCAPE.themecontext->getChangeThemeSignal().emit();
        INKSCAPE.themecontext->add_gtk_css(true);
    }
}

/* This is just to provide access to common functionality from sp_desktop_widget_realize() above
   as well as from SPDesktop::change_document() */
void SPDesktopWidget::updateNamedview()
{
    // Listen on namedview modification
    // originally (prior to the sigc++ conversion) the signal was simply
    // connected twice rather than disconnecting the first connection
    modified_connection.disconnect();

    modified_connection = desktop->namedview->connectModified(sigc::mem_fun(*this, &SPDesktopWidget::namedviewModified));
    namedviewModified(desktop->namedview, SP_OBJECT_MODIFIED_FLAG);

    updateTitle( desktop->doc()->getDocumentName() );
}

void
SPDesktopWidget::update_guides_lock()
{
    bool down = _canvas_grid->GetGuideLock()->get_active();
    auto nv   = desktop->getNamedView();
    bool lock = nv->getLockGuides();

    if (down != lock) {
        nv->toggleLockGuides();
        setMessage(Inkscape::NORMAL_MESSAGE, down ? _("Locked all guides") : _("Unlocked all guides"));
    }
}

void
SPDesktopWidget::enableInteraction()
{
  g_return_if_fail(_interaction_disabled_counter > 0);

  _interaction_disabled_counter--;

  if (_interaction_disabled_counter == 0) {
    this->set_sensitive();
  }
}

void
SPDesktopWidget::disableInteraction()
{
  if (_interaction_disabled_counter == 0) {
    this->set_sensitive(false);
  }

  _interaction_disabled_counter++;
}

void
SPDesktopWidget::setCoordinateStatus(Geom::Point p)
{
    _statusbar->set_coordinate(_dt2r * p);
}

void
SPDesktopWidget::letRotateGrabFocus()
{
    _statusbar->rotate_grab_focus();
}

void
SPDesktopWidget::letZoomGrabFocus()
{
    _statusbar->zoom_grab_focus();
}

void
SPDesktopWidget::getWindowGeometry (gint &x, gint &y, gint &w, gint &h)
{
    if (window) {
        window->get_size (w, h);
        window->get_position (x, y);
        // The get_positon is very unreliable (see Gtk docs) and will often return zero.
        if (!x && !y) {
            if (Glib::RefPtr<Gdk::Window> w = window->get_window()) {
                Gdk::Rectangle rect;
                w->get_frame_extents(rect);
                x = rect.get_x();
                y = rect.get_y();
            }
        }
    }
}

void
SPDesktopWidget::setWindowPosition (Geom::Point p)
{
    if (window)
    {
        window->move (gint(round(p[Geom::X])), gint(round(p[Geom::Y])));
    }
}

void
SPDesktopWidget::setWindowSize (gint w, gint h)
{
    if (window)
    {
        window->set_default_size (w, h);
        window->resize (w, h);
    }
}

/**
 * \note transientizing does not work on windows; when you minimize a document
 * and then open it back, only its transient emerges and you cannot access
 * the document window. The document window must be restored by rightclicking
 * the taskbar button and pressing "Restore"
 */
void
SPDesktopWidget::setWindowTransient (void *p, int transient_policy)
{
    if (window)
    {
        GtkWindow *w = GTK_WINDOW(window->gobj());
        gtk_window_set_transient_for (GTK_WINDOW(p), w);

        /*
         * This enables "aggressive" transientization,
         * i.e. dialogs always emerging on top when you switch documents. Note
         * however that this breaks "click to raise" policy of a window
         * manager because the switched-to document will be raised at once
         * (so that its transients also could raise)
         */
        if (transient_policy == PREFS_DIALOGS_WINDOWS_AGGRESSIVE)
            // without this, a transient window not always emerges on top
            gtk_window_present (w);
    }
}

void
SPDesktopWidget::presentWindow()
{
    if (window)
        window->present();
}

bool SPDesktopWidget::showInfoDialog( Glib::ustring const &message )
{
    bool result = false;
    if (window)
    {
        Gtk::MessageDialog dialog(*window, message, false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK);
        dialog.property_destroy_with_parent() = true;
        dialog.set_name("InfoDialog");
        dialog.set_title(_("Note:")); // probably want to take this as a parameter.
        Inkscape::UI::dialog_run(dialog);
    }
    return result;
}

bool SPDesktopWidget::warnDialog (Glib::ustring const &text)
{
    Gtk::MessageDialog dialog (*window, text, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL);
    gint response = Inkscape::UI::dialog_run(dialog);
    return response == Gtk::RESPONSE_OK;
}

void
SPDesktopWidget::iconify()
{
    GtkWindow *topw = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(_canvas->gobj())));
    if (GTK_IS_WINDOW(topw)) {
        if (desktop->is_iconified()) {
            gtk_window_deiconify(topw);
        } else {
            gtk_window_iconify(topw);
        }
    }
}

void
SPDesktopWidget::maximize()
{
    GtkWindow *topw = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(_canvas->gobj())));
    if (GTK_IS_WINDOW(topw)) {
        if (desktop->is_maximized()) {
            gtk_window_unmaximize(topw);
        } else {
            gtk_window_maximize(topw);
        }
    }
}

void
SPDesktopWidget::fullscreen()
{
    GtkWindow *topw = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(_canvas->gobj())));
    if (GTK_IS_WINDOW(topw)) {
        if (desktop->is_fullscreen()) {
            gtk_window_unfullscreen(topw);
            // widget layout is triggered by the resulting window_state_event
        } else {
            gtk_window_fullscreen(topw);
            // widget layout is triggered by the resulting window_state_event
        }
    }
}

/**
 * Hide whatever the user does not want to see in the window.
 * Also move command toolbar to top or side as required.
 */
void SPDesktopWidget::layoutWidgets()
{
    SPDesktopWidget *dtw = this;
    Glib::ustring pref_root;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (desktop && desktop->is_focusMode()) {
        pref_root = "/focus/";
    } else if (desktop && desktop->is_fullscreen()) {
        pref_root = "/fullscreen/";
    } else {
        pref_root = "/window/";
    }

    if (!prefs->getBool(pref_root + "commands/state", true)) {
        dtw->command_toolbar->set_visible(false);
    } else {
        dtw->command_toolbar->show_all();
    }

    if (!prefs->getBool(pref_root + "snaptoolbox/state", true)) {
        dtw->snap_toolbar->set_visible(false);
    } else {
        dtw->snap_toolbar->set_visible(true); // Not show_all()!
    }

    if (!prefs->getBool(pref_root + "toppanel/state", true)) {
        dtw->tool_toolbars->set_visible(false);
    } else {
        dtw->tool_toolbars->set_visible(true); // Not show_all()!
    }

    if (!prefs->getBool(pref_root + "toolbox/state", true)) {
        dtw->tool_toolbox->set_visible(false);
    } else {
        dtw->tool_toolbox->show_all();
    }

    if (!prefs->getBool(pref_root + "statusbar/state", true)) {
        dtw->_statusbar->set_visible(false);
    } else {
        dtw->_statusbar->show_all();
    }
    dtw->_statusbar->update_visibility(); // Individual items in bar

    if (!prefs->getBool(pref_root + "panels/state", true)) {
        dtw->_panels->set_visible(false);
    } else {
        dtw->_panels->show_all();
    }

    _canvas_grid->ShowScrollbars(prefs->getBool(pref_root + "scrollbars/state", true));
    _canvas_grid->ShowRulers(    prefs->getBool(pref_root + "rulers/state",     true));

    // Move command toolbar as required.

    // If interface_mode unset, use screen aspect ratio. Needs to be synced with "canvas-interface-mode" action.
    Gdk::Rectangle monitor_geometry = Inkscape::UI::get_monitor_geometry_primary();
    double const width  = monitor_geometry.get_width();
    double const height = monitor_geometry.get_height();
    bool widescreen = (height > 0 && width/height > 1.65);
    widescreen = prefs->getBool(pref_root + "interface_mode", widescreen);

    // Unlink command toolbar.
    command_toolbar->reference(); // So toolbox is not deleted.
    auto parent = command_toolbar->get_parent();
    parent->remove(*command_toolbar);

    // Link command toolbar back.
    auto orientation_c = GTK_ORIENTATION_HORIZONTAL;
    if (!widescreen) {
        _top_toolbars->attach(*command_toolbar, 0, 0); // Always first in Grid
        command_toolbar->set_hexpand(true);
        orientation_c = GTK_ORIENTATION_HORIZONTAL;
    } else {
        _hbox->add(*command_toolbar);
        orientation_c = GTK_ORIENTATION_VERTICAL;
        command_toolbar->set_hexpand(false);
    }
    // Toolbar is actually child:
    command_toolbar->foreach([=](Gtk::Widget& widget) {
        if (auto toolbar = dynamic_cast<Gtk::Toolbar *>(&widget)) {
            gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar->gobj()), orientation_c); // Missing in C++interface!
        }
    });
    command_toolbar->unreference();

    // Temporary for Gtk3: Gtk toolbar resets icon sizes, so reapply them.
    // TODO: remove this call in Gtk4 after Gtk::Toolbar is eliminated.
    apply_ctrlbar_settings();

    repack_snaptoolbar();

    Inkscape::UI::resize_widget_children(_top_toolbars);
}

Gtk::Toolbar *
SPDesktopWidget::get_toolbar_by_name(const Glib::ustring& name)
{
    // The name is actually attached to the GtkGrid that contains
    // the toolbar, so we need to get the grid first
    auto widget = Inkscape::UI::find_widget_by_name(*tool_toolbars, name);
    auto grid = dynamic_cast<Gtk::Grid*>(widget);

    if (!grid) return nullptr;

    auto child = grid->get_child_at(0,0);
    auto tb = dynamic_cast<Gtk::Toolbar*>(child);

    return tb;
}

void
SPDesktopWidget::setToolboxFocusTo (const gchar* label)
{
    // Look for a named widget
    auto hb = Inkscape::UI::find_widget_by_name(*tool_toolbars, label);
    if (hb) {
        hb->grab_focus();
    }
}

void
SPDesktopWidget::setToolboxAdjustmentValue (gchar const *id, double value)
{
    // Look for a named widget
    auto hb = Inkscape::UI::find_widget_by_name(*tool_toolbars, id);
    if (hb) {
        auto sb = dynamic_cast<Inkscape::UI::Widget::SpinButtonToolItem *>(hb);
        auto a = sb->get_adjustment();

        if(a) a->set_value(value);
    } else {
        g_warning ("Could not find GtkAdjustment for %s\n", id);
    }
}

bool
SPDesktopWidget::isToolboxButtonActive (const gchar* id)
{
    bool isActive = false;
    auto thing = Inkscape::UI::find_widget_by_name(*tool_toolbars, id);

    // The toolbutton could be a few different types so try casting to
    // each of them.
    // TODO: This will be simpler in Gtk+ 4 when ToolItems have gone
    auto toggle_button      = dynamic_cast<Gtk::ToggleButton *>(thing);
    auto toggle_tool_button = dynamic_cast<Gtk::ToggleToolButton *>(thing);

    if ( !thing ) {
        //g_message( "Unable to locate item for {%s}", id );
    } else if (toggle_button) {
        isActive = toggle_button->get_active();
    } else if (toggle_tool_button) {
        isActive = toggle_tool_button->get_active();
    } else {
        //g_message( "Item for {%s} is of an unsupported type", id );
    }

    return isActive;
}

/**
 * Choose where to pack the snap toolbar.
 * Hiding/unhiding is done in the SnapToolbar widget.
 */
void SPDesktopWidget::repack_snaptoolbar()
{
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    bool is_perm = prefs->getInt("/toolbox/simplesnap", 1) == 2;
    auto& aux = *tool_toolbars;
    auto& snap = *snap_toolbar;

    // Only remove from the parent if the status has changed
    auto parent = snap.get_parent();
    if (parent && ((is_perm && parent != _hbox) || (!is_perm && parent != _top_toolbars))) {
        parent->remove(snap);
    }

    // Only repack if there's no parent widget now.
    if (!snap.get_parent()) {
        if (is_perm) {
            _hbox->pack_end(snap, false, true);
        } else {
            _top_toolbars->attach(snap, 1, 0, 1, 2);
        }
    }

    // Always reset the various constraints, even if not repacked.
    if (is_perm) {
        snap.set_valign(Gtk::ALIGN_START);
    } else {
        // This ensures that the Snap toolbox is on the top and only takes the needed space.
        if (_top_toolbars->get_children().size() == 3 && command_toolbar->get_visible()) {
            _top_toolbars->child_property_width(aux) = 2;
            _top_toolbars->child_property_height(snap) =  1;
            snap.set_valign(Gtk::ALIGN_START);
        }
        else {
            _top_toolbars->child_property_width(aux) = 1;
            _top_toolbars->child_property_height(snap) =  2;
            snap.set_valign(Gtk::ALIGN_CENTER);
        }
    }
}

void SPDesktopWidget::namedviewModified(SPObject *obj, guint flags)
{
    auto nv = cast<SPNamedView>(obj);

    if (flags & SP_OBJECT_MODIFIED_FLAG) {
        _dt2r = 1. / nv->display_units->factor;

        _canvas_grid->GetVRuler()->set_unit(nv->getDisplayUnit());
        _canvas_grid->GetHRuler()->set_unit(nv->getDisplayUnit());
        _canvas_grid->GetVRuler()->set_tooltip_text(gettext(nv->display_units->name_plural.c_str()));
        _canvas_grid->GetHRuler()->set_tooltip_text(gettext(nv->display_units->name_plural.c_str()));
        _canvas_grid->updateRulers();

        /* This loops through all the grandchildren of tool toolbars,
         * and for each that it finds, it performs an Inkscape::UI::find_widget_by_name(*),
         * looking for widgets named "unit-tracker" (this is used by
         * all toolboxes to refer to the unit selector). The default document units
         * is then selected within these unit selectors.
         *
         * This should solve: https://bugs.launchpad.net/inkscape/+bug/362995
         */
        std::vector<Gtk::Widget*> ch = tool_toolbars->get_children();
        for (auto i:ch) {
            if (auto container = dynamic_cast<Gtk::Container *>(i)) {
                std::vector<Gtk::Widget*> grch = container->get_children();
                for (auto j:grch) {
                    // Don't apply to text toolbar. We want to be able to
                    // use different units for text. (Bug 1562217)
                    const Glib::ustring name = j->get_name();
                    if ( name == "TextToolbar" || name == "MeasureToolbar" || name == "CalligraphicToolbar" )
                        continue;

                    auto const tracker = dynamic_cast<Inkscape::UI::Widget::ComboToolItem*>
                                                     (Inkscape::UI::find_widget_by_name(*j, "unit-tracker"));
                    if (tracker) { // it's null when inkscape is first opened
                        if (auto ptr = static_cast<UnitTracker*>(tracker->get_data(Glib::Quark("unit-tracker")))) {
                            ptr->setActiveUnit(nv->display_units);
                        }
                    }
                } // grandchildren
            } // if child is a container
        } // children
    }
}

// We make the desktop window with focus active. Signal is connected in inkscape-window.cpp
void SPDesktopWidget::onFocus(bool const has_toplevel_focus)
{
    if (!has_toplevel_focus) return;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/options/bitmapautoreload/value", true)) {
        std::vector<SPObject *> imageList = (desktop->doc())->getResourceList("image");
        for (auto it : imageList) {
            auto image = cast<SPImage>(it);
            image->refresh_if_outdated();
        }
    }

    INKSCAPE.activate_desktop (desktop);
}

// ------------------------ Zoom ------------------------

void
SPDesktopWidget::sticky_zoom_toggled()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/options/stickyzoom/value", _canvas_grid->GetStickyZoom()->get_active());
}

void
SPDesktopWidget::sticky_zoom_updated()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _canvas_grid->GetStickyZoom()->set_active(prefs->getBool("/options/stickyzoom/value", false));
}

void
SPDesktopWidget::update_zoom()
{
    _statusbar->update_zoom();
}

// ---------------------- Rotation ------------------------

void
SPDesktopWidget::update_rotation()
{
    _statusbar->update_rotate();
}

// --------------- Rulers/Scrollbars/Etc. -----------------

void
SPDesktopWidget::toggle_command_palette() {
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleCommandPalette();
}

void
SPDesktopWidget::toggle_rulers()
{
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleRulers();
}

void
SPDesktopWidget::toggle_scrollbars()
{
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleScrollbars();
}

Gio::ActionMap* SPDesktopWidget::get_action_map() {
    return window;
}

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
