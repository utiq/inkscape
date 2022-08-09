// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Utility functions for UI
 *
 * Authors:
 *   Tavmjong Bah
 *   John Smith
 *
 * Copyright (C) 2004, 2013, 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "util.h"

#include <gtkmm.h>


/*
 * Ellipse text if longer than maxlen, "50% start text + ... + ~50% end text"
 * Text should be > length 8 or just return the original text
 */
Glib::ustring ink_ellipsize_text(Glib::ustring const &src, size_t maxlen)
{
    if (src.length() > maxlen && maxlen > 8) {
        size_t p1 = (size_t) maxlen / 2;
        size_t p2 = (size_t) src.length() - (maxlen - p1 - 1);
        return src.substr(0, p1) + "…" + src.substr(p2);
    }
    return src;
}

/**
 * Show widget, if the widget has a Gtk::Reveal parent, reveal instead.
 *
 * @param widget - The child widget to show.
 */
void reveal_widget(Gtk::Widget *widget, bool show)
{
    auto revealer = dynamic_cast<Gtk::Revealer *>(widget->get_parent());
    if (revealer) {
        revealer->set_reveal_child(show);
    }
    if (show) {
        widget->show();
    } else if (!revealer) {
        widget->hide();
    }
}


bool is_widget_effectively_visible(Gtk::Widget* widget) {
    if (!widget) return false;

    // TODO: what's the right way to determine if widget is visible on the screen?
    return widget->get_child_visible();
}

namespace Inkscape {
namespace UI {
void resize_widget_children(Gtk::Widget *widget) {
    if(widget) {
        Gtk::Allocation allocation;
        int             baseline;
        widget->get_allocated_size(allocation, baseline);
        widget->size_allocate(allocation, baseline);
    }
}
}
}


Gdk::RGBA get_background_color(const Glib::RefPtr<Gtk::StyleContext> &context,
                               Gtk::StateFlags                  state) {
    GdkRGBA *c;

    gtk_style_context_get(context->gobj(),
                          static_cast<GtkStateFlags>(state),
                          GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &c,
                          nullptr);
    auto bg_color = Glib::wrap(c);

    return bg_color;
}

// 2Geom <-> Cairo

Cairo::RectangleInt geom_to_cairo(const Geom::IntRect &rect)
{
    return Cairo::RectangleInt{rect.left(), rect.top(), rect.width(), rect.height()};
}

Geom::IntRect cairo_to_geom(const Cairo::RectangleInt &rect)
{
    return Geom::IntRect::from_xywh(rect.x, rect.y, rect.width, rect.height);
}

Cairo::Matrix geom_to_cairo(const Geom::Affine &affine)
{
    return Cairo::Matrix(affine[0], affine[1], affine[2], affine[3], affine[4], affine[5]);
}

Geom::IntPoint dimensions(const Cairo::RefPtr<Cairo::ImageSurface> &surface)
{
    return Geom::IntPoint(surface->get_width(), surface->get_height());
}

Geom::IntPoint dimensions(const Gdk::Rectangle &allocation)
{
    return Geom::IntPoint(allocation.get_width(), allocation.get_height());
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
