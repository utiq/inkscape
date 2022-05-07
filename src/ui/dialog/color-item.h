// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Inkscape color swatch UI item.
 */
/* Authors:
 *   Jon A. Cruz
 *
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_DIALOGS_COLOR_ITEM_H
#define SEEN_DIALOGS_COLOR_ITEM_H

#include <boost/ptr_container/ptr_vector.hpp>
#include <gtkmm.h>

#include "widgets/ege-paint-def.h"
#include "ui/widget/preview.h"

class SPGradient;

namespace Inkscape {
namespace UI {
namespace Dialog {

class ColorItem;

class SwatchPage
{
public:
    SwatchPage();
    ~SwatchPage();

    Glib::ustring _name;
    int _prefWidth;
    boost::ptr_vector<ColorItem> _colors;
};


/**
 * The color swatch you see on screen as a clickable box.
 */
class ColorItem final : public sigc::trackable
{
    friend void _loadPaletteFile( gchar const *filename );

public:
    ColorItem();
    ColorItem(ege::PaintDef::ColorType type);
    ColorItem(unsigned r, unsigned g, unsigned b, Glib::ustring &name);
    ~ColorItem();
    virtual ColorItem &operator=(ColorItem const &other);

    void buttonClicked(bool secondary = false);

    void setGradient(SPGradient *grad);
    SPGradient * getGradient() const { return _grad; }
    void setPattern(cairo_pattern_t *pattern);

    void setState( bool fill, bool stroke );
    bool isFill() { return _isFill; }
    bool isStroke() { return _isStroke; }

    ege::PaintDef def;

    Gtk::Widget* createWidget();

    void onPreviewDestroyed(UI::Widget::Preview *preview);

private:
    void _dragGetColorData(const Glib::RefPtr<Gdk::DragContext> &drag_context,
                           Gtk::SelectionData                   &data,
                           guint                                 info,
                           guint                                 time);

    void _updatePreviews();
    void _regenPreview(UI::Widget::Preview * preview);

    void drag_begin(const Glib::RefPtr<Gdk::DragContext> &dc);
    void handleClick();
    void handleSecondaryClick(gint arg1);
    bool handleEnterNotify(GdkEventCrossing* event);
    bool handleLeaveNotify(GdkEventCrossing* event);

    std::vector<UI::Widget::Preview*> _previews;

    bool _isFill = false;
    bool _isStroke = false;
    SPGradient *_grad = nullptr;
    cairo_pattern_t *_pattern = nullptr;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // SEEN_DIALOGS_COLOR_ITEM_H

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
