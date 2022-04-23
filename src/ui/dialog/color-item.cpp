// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Inkscape color swatch UI item.
 */
/* Authors:
 *   Jon A. Cruz
 *   Abhishek Sharma
 *
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cerrno>

#include <gtkmm/label.h>
#include <glibmm/i18n.h>

#include "color-item.h"

#include "desktop.h"

#include "desktop-style.h"
#include "display/cairo-utils.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape.h" // for SP_ACTIVE_DESKTOP
#include "message-context.h"

#include "io/resource.h"
#include "io/sys.h"
#include "svg/svg-color.h"
#include "ui/icon-names.h"
#include "ui/widget/gradient-vector-selector.h"


namespace Inkscape {
namespace UI {
namespace Dialog {

static std::vector<std::string> mimeStrings;
static std::map<std::string, guint> mimeToInt;

void
ColorItem::handleClick() {
    buttonClicked(false);
}

void
ColorItem::handleSecondaryClick(gint /*arg1*/) {
    buttonClicked(true);
}

bool
ColorItem::handleEnterNotify(GdkEventCrossing* /*event*/) {
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if ( desktop ) {
        gchar* msg = g_strdup_printf(_("Color: <b>%s</b>; <b>Click</b> to set fill, <b>Shift+click</b> to set stroke"),
                def.descr.c_str());
        desktop->tipsMessageContext()->set(Inkscape::INFORMATION_MESSAGE, msg);
        g_free(msg);
    }

    return false;
}

bool
ColorItem::handleLeaveNotify(GdkEventCrossing* /*event*/) {
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    if ( desktop ) {
        desktop->tipsMessageContext()->clear();
    }

    return false;
}

// TODO resolve this more cleanly:
extern bool colorItemHandleButtonPress(GdkEventButton* event, UI::Widget::Preview *preview, gpointer user_data);

void
ColorItem::drag_begin(const Glib::RefPtr<Gdk::DragContext> &dc)
{
    using Inkscape::IO::Resource::get_path;
    using Inkscape::IO::Resource::PIXMAPS;
    using Inkscape::IO::Resource::SYSTEM;
    int width = 32;
    int height = 24;

    if (def.getType() != ege::PaintDef::RGB){
        GError *error;
        gsize bytesRead = 0;
        gsize bytesWritten = 0;
        gchar *localFilename = g_filename_from_utf8(get_path(SYSTEM, PIXMAPS, "remove-color.png"), -1, &bytesRead,
                &bytesWritten, &error);
        auto pixbuf = Gdk::Pixbuf::create_from_file(localFilename, width, height, false);
        g_free(localFilename);
        dc->set_icon(pixbuf, 0, 0);
    } else {
        Glib::RefPtr<Gdk::Pixbuf> pixbuf;
        if (getGradient() ){
            cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
            cairo_pattern_t *gradient = getGradient()->create_preview_pattern(width);
            cairo_t *ct = cairo_create(s);
            cairo_set_source(ct, gradient);
            cairo_paint(ct);
            cairo_destroy(ct);
            cairo_pattern_destroy(gradient);
            cairo_surface_flush(s);

            pixbuf = Glib::wrap(ink_pixbuf_create_from_cairo_surface(s));
        } else {
            pixbuf = Gdk::Pixbuf::create( Gdk::COLORSPACE_RGB, false, 8, width, height );
            guint32 fillWith = (0xff000000 & (def.getR() << 24))
                | (0x00ff0000 & (def.getG() << 16))
                | (0x0000ff00 & (def.getB() <<  8));
            pixbuf->fill( fillWith );
        }
        dc->set_icon(pixbuf, 0, 0);
    }
}

SwatchPage::SwatchPage()
    : _prefWidth(0)
{
}

SwatchPage::~SwatchPage() = default;

ColorItem::ColorItem(ege::PaintDef::ColorType type)
    : def(type)
{
    def.signal_changed.connect([this] { _updatePreviews(); });
}

ColorItem::ColorItem(unsigned r, unsigned g, unsigned b, Glib::ustring &name)
    : def(r, g, b, name.raw())
{
    def.signal_changed.connect([this] { _updatePreviews(); });
}

ColorItem::~ColorItem()
{
    if (_pattern != nullptr) {
        cairo_pattern_destroy(_pattern);
    }
}

ColorItem &ColorItem::operator=(ColorItem const &other)
{
    if ( this != &other ) {
        def = other.def;
    }
    return *this;
}

void ColorItem::setState(bool fill, bool stroke)
{
    if (_isFill != fill || _isStroke != stroke) {
        _isFill = fill;
        _isStroke = stroke;

        for (auto preview : _previews) {
            preview->set_fillstroke(_isFill, _isStroke);
        }
    }
}

void ColorItem::setGradient(SPGradient *grad)
{
    if (_grad != grad) {
        _grad = grad;
        // TODO regen and push to listeners
    }
}

void ColorItem::setPattern(cairo_pattern_t *pattern)
{
    if (_pattern) {
        cairo_pattern_destroy(_pattern);
    }
    _pattern = pattern;
    if (_pattern) {
        cairo_pattern_reference(_pattern);
    }

    _updatePreviews();
}

void
ColorItem::_dragGetColorData(const Glib::RefPtr<Gdk::DragContext>& /*drag_context*/,
                             Gtk::SelectionData                     &data,
                             guint                                   info,
                             guint                                 /*time*/)
{
    std::string key;
    if ( info < mimeStrings.size() ) {
        key = mimeStrings[info];
    } else {
        g_warning("ERROR: unknown value (%d)", info);
    }

    if ( !key.empty() ) {
        char* tmp = nullptr;
        int len = 0;
        int format = 0;
        def.getMIMEData(key, tmp, len, format);
        if ( tmp ) {
            data.set(key, format, (guchar*)tmp, len );
            delete[] tmp;
        }
    }
}

void ColorItem::_updatePreviews()
{
    for (auto preview : _previews) {
        _regenPreview(preview);
        preview->queue_draw();
    }
}

void ColorItem::_regenPreview(UI::Widget::Preview *preview)
{
    if ( def.getType() != ege::PaintDef::RGB ) {
        using Inkscape::IO::Resource::get_path;
        using Inkscape::IO::Resource::PIXMAPS;
        using Inkscape::IO::Resource::SYSTEM;
        GError *error = nullptr;
        gsize bytesRead = 0;
        gsize bytesWritten = 0;
        gchar *localFilename = g_filename_from_utf8(get_path(SYSTEM, PIXMAPS, "remove-color.png"), -1, &bytesRead, &bytesWritten, &error);
        auto pixbuf = Gdk::Pixbuf::create_from_file(localFilename);
        if (!pixbuf) {
            g_warning("Null pixbuf for %p [%s]", localFilename, localFilename );
        }
        g_free(localFilename);

        preview->set_pixbuf(pixbuf);
    }
    else if ( !_pattern ){
        preview->set_color((def.getR() << 8) | def.getR(),
                           (def.getG() << 8) | def.getG(),
                           (def.getB() << 8) | def.getB() );
    } else {
        // These correspond to PREVIEW_PIXBUF_WIDTH and VBLOCK from swatches.cpp
        // TODO: the pattern to draw should be in the widget that draws the preview,
        //       so the preview can be scalable
        int w = 128;
        int h = 16;

        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *ct = cairo_create(s);
        cairo_set_source(ct, _pattern);
        cairo_paint(ct);
        cairo_destroy(ct);
        cairo_surface_flush(s);

        auto pixbuf = Glib::wrap(ink_pixbuf_create_from_cairo_surface(s));
        preview->set_pixbuf(pixbuf);
    }

    preview->set_fillstroke(_isFill, _isStroke);
}

Gtk::Widget* ColorItem::createWidget()
{
    auto preview = Gtk::make_managed<UI::Widget::Preview>();
    preview->set_name("ColorItemPreview");

    _regenPreview(preview);

    preview->set_details(Inkscape::UI::Widget::VIEW_TYPE_GRID,
                         Inkscape::UI::Widget::PREVIEW_SIZE_TINY,
                         100,
                         0);

    preview->set_focus_on_click(false);
    preview->set_tooltip_text(def.descr);

    preview->signal_clicked.connect(sigc::mem_fun(*this, &ColorItem::handleClick));
    preview->signal_alt_clicked.connect(sigc::mem_fun(*this, &ColorItem::handleSecondaryClick));
    preview->signal_destroyed.connect(sigc::mem_fun(*this, &ColorItem::onPreviewDestroyed));
    preview->signal_button_press_event().connect(sigc::bind(sigc::ptr_fun(&colorItemHandleButtonPress), preview, this));

    _previews.emplace_back(preview);

    {
        auto listing = def.getMIMETypes();
        std::vector<Gtk::TargetEntry> entries;

        for (auto &str : listing) {
            auto target = str.c_str();
            guint flags = 0;
            if (mimeToInt.find(str) == mimeToInt.end()){
                // these next lines are order-dependent:
                mimeToInt[str] = mimeStrings.size();
                mimeStrings.push_back(str);
            }
            auto info = mimeToInt[target];
            Gtk::TargetEntry entry(target, (Gtk::TargetFlags)flags, info);
            entries.push_back(entry);
        }

        preview->drag_source_set(entries, Gdk::BUTTON1_MASK,
                                 Gdk::DragAction(Gdk::ACTION_MOVE | Gdk::ACTION_COPY) );
    }

    preview->signal_drag_data_get().connect(sigc::mem_fun(*this, &ColorItem::_dragGetColorData));
    preview->signal_drag_begin().connect(sigc::mem_fun(*this, &ColorItem::drag_begin));
    preview->signal_enter_notify_event().connect(sigc::mem_fun(*this, &ColorItem::handleEnterNotify));
    preview->signal_leave_notify_event().connect(sigc::mem_fun(*this, &ColorItem::handleLeaveNotify));
    preview->set_freesize(true);

    return preview;
}

void ColorItem::onPreviewDestroyed(Widget::Preview *preview)
{
    auto it = std::find(_previews.begin(), _previews.end(), preview);
    assert(it != _previews.end());
    _previews.erase(it);
}

void ColorItem::buttonClicked(bool secondary)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop) {
        char const * attrName = secondary ? "stroke" : "fill";

        SPCSSAttr *css = sp_repr_css_attr_new();
        Glib::ustring descr;
        switch (def.getType()) {
            case ege::PaintDef::CLEAR: {
                // TODO actually make this clear
                sp_repr_css_set_property( css, attrName, "none" );
                descr = secondary? _("Remove stroke color") : _("Remove fill color");
                break;
            }
            case ege::PaintDef::NONE: {
                sp_repr_css_set_property( css, attrName, "none" );
                descr = secondary? _("Set stroke color to none") : _("Set fill color to none");
                break;
            }
//mark
            case ege::PaintDef::RGB: {
                Glib::ustring colorspec;
                if ( _grad ){
                    colorspec = "url(#";
                    colorspec += _grad->getId();
                    colorspec += ")";
                } else {
                    gchar c[64];
                    guint32 rgba = (def.getR() << 24) | (def.getG() << 16) | (def.getB() << 8) | 0xff;
                    sp_svg_write_color(c, sizeof(c), rgba);
                    colorspec = c;
                }
//end mark
                sp_repr_css_set_property( css, attrName, colorspec.c_str() );
                descr = secondary? _("Set stroke color from swatch") : _("Set fill color from swatch");
                break;
            }
        }
        sp_desktop_set_style(desktop, css);
        sp_repr_css_attr_unref(css);

        DocumentUndo::done( desktop->getDocument(), descr.c_str(), INKSCAPE_ICON("swatches"));
    }
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

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
