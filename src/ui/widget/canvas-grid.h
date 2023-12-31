// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_CANVASGRID_H
#define INKSCAPE_UI_WIDGET_CANVASGRID_H
/*
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/builder.h>
#include <2geom/point.h>
#include <2geom/int-point.h>

#include "display/control/canvas-item-ptr.h"

class SPPage;
class SPDocument;
class SPDesktopWidget;

namespace Inkscape {
class MotionEvent;
class CanvasItemGuideLine;

namespace UI {

namespace Dialog {
class CommandPalette;
}

namespace Widget {

class Canvas;
class CanvasNotice;
class Ruler;

/**
 * A Gtk::Grid widget that contains rulers, scrollbars, buttons, and, of course, the canvas.
 * Canvas has an overlay to let us put stuff on the canvas.
 */
class CanvasGrid : public Gtk::Grid
{
    using parent_type = Gtk::Grid;
public:
    CanvasGrid(SPDesktopWidget *dtw);
    ~CanvasGrid() override;

    void ShowScrollbars(bool state = true);
    void ToggleScrollbars();

    void ShowRulers(bool state = true);
    void ToggleRulers();
    void updateRulers();

    void ShowCommandPalette(bool state = true);
    void ToggleCommandPalette();

    void showNotice(Glib::ustring const &msg, unsigned timeout = 0);

    Inkscape::UI::Widget::Canvas *GetCanvas() { return _canvas.get(); };

    // Hopefully temp.
    Inkscape::UI::Widget::Ruler *GetHRuler() { return _vruler.get(); };
    Inkscape::UI::Widget::Ruler *GetVRuler() { return _hruler.get(); };
    Gtk::Adjustment *GetHAdj() { return _hadj.get(); };
    Gtk::Adjustment *GetVAdj() { return _vadj.get(); };
    Gtk::ToggleButton *GetGuideLock()  { return &_guide_lock; }
    Gtk::ToggleButton *GetCmsAdjust()  { return &_cms_adjust; }
    Gtk::ToggleButton *GetStickyZoom();
    Dialog::CommandPalette *getCommandPalette() { return _command_palette.get(); }

    // Motion event handler, and delayed snap event callback.
    void rulerMotion(MotionEvent const &event, bool horiz);

    // Scroll handling.
    void updateScrollbars(double scale);

private:
    // Signal callbacks
    void on_size_allocate(Gtk::Allocation& allocation) override;
    void on_realize() override;

    // The widgets
    std::unique_ptr<Inkscape::UI::Widget::Canvas> _canvas;
    std::unique_ptr<Dialog::CommandPalette> _command_palette;
    CanvasNotice *_notice;
    Gtk::Overlay _canvas_overlay;
    Gtk::Grid _subgrid;

    Glib::RefPtr<Gtk::Adjustment> _hadj;
    Glib::RefPtr<Gtk::Adjustment> _vadj;
    Gtk::Scrollbar _hscrollbar;
    Gtk::Scrollbar _vscrollbar;

    std::unique_ptr<Inkscape::UI::Widget::Ruler> _hruler;
    std::unique_ptr<Inkscape::UI::Widget::Ruler> _vruler;

    Gtk::ToggleButton _guide_lock;
    Gtk::ToggleButton _cms_adjust;
    Gtk::MenuButton _quick_actions;
    Glib::RefPtr<Gtk::Builder> _display_popup;

    // To be replaced by stateful Gio::Actions
    bool _show_scrollbars = true;
    bool _show_rulers = true;

    // Hopefully temp
    SPDesktopWidget *_dtw;
    SPDocument *_document = nullptr;

    // Store allocation so we don't redraw too often.
    Gtk::Allocation _allocation;

    // Connections for page and selection tracking
    sigc::connection _page_selected_connection;
    sigc::connection _page_modified_connection;
    sigc::connection _sel_changed_connection;
    sigc::connection _sel_modified_connection;

    // Ruler event handling.
    bool _ruler_clicked = false; ///< True if the ruler has been clicked
    bool _ruler_dragged = false; ///< True if a drag on the ruler is occurring
    bool _ruler_ctrl_clicked = false; ///< Whether ctrl was held when the ruler was clicked.
    Geom::IntPoint _ruler_drag_origin; ///< Position of start of drag
    Geom::Point _normal; ///< Normal to the guide currently being handled during ruler event
    CanvasItemPtr<CanvasItemGuideLine> _active_guide; ///< The guide being handled during a ruler event
    Geom::IntPoint _rulerToCanvas(bool horiz) const;
    void _createGuideItem(Geom::Point const &pos, bool horiz);
    void _createGuide(Geom::Point origin, Geom::Point normal);
    Gtk::EventSequenceState _rulerButtonPress(Gtk::GestureMultiPress &gesture, int n_press, double x, double y, bool horiz);
    Gtk::EventSequenceState _rulerButtonRelease(Gtk::GestureMultiPress &gesture, int n_press, double x, double y, bool horiz);
    void _rulerMotion(GtkEventControllerMotion const *controller, double x, double y, bool horiz);

    // Temporarily required due to use of C callbacks.
    template <bool horiz>
    void _rulerMotion(GtkEventControllerMotion const *controller, double x, double y)
    {
        _rulerMotion(controller, x, y, horiz);
    }

    // Scroll handling.
    bool _updating = false;
    void _adjustmentChanged();
};

} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_CANVASGRID_H

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
