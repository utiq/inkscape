// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "export-preview.h"

#include "document.h"
#include "display/cairo-utils.h"
#include "object/sp-item.h"
#include "object/sp-root.h"
#include "util/preview.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

void ExportPreview::resetPixels()
{
    clear();
    show();
}

void ExportPreview::setSize(int newSize)
{
    size = newSize;
    resetPixels();
}

ExportPreview::~ExportPreview()
{
    refresh_conn.disconnect();
    if (drawing) {
        _document->getRoot()->invoke_hide(visionkey);
    }
}

void ExportPreview::setItem(SPItem *item)
{
    _item = item;
    _dbox = {};
}

void ExportPreview::setDbox(double x0, double x1, double y0, double y1)
{
    if (!_document) {
        return;
    }
    if (x1 == x0 || y1 == y0) {
        return;
    }
    _item = nullptr;
    _dbox = Geom::Rect(x0, y0, x1, y1) * _document->dt2doc();
}

void ExportPreview::setDocument(SPDocument *document)
{
    if (drawing) {
        _document->getRoot()->invoke_hide(visionkey);
        drawing.reset();
        _item = nullptr;
    }
    _document = document;
    if (_document) {
        drawing = std::make_shared<Inkscape::Drawing>();
        visionkey = SPItem::display_key_new(1);
        if (auto di = _document->getRoot()->invoke_show(*drawing, visionkey, SP_ITEM_SHOW_DISPLAY)) {
            drawing->setRoot(di);
        } else {
            drawing.reset();
        }
    }
}

void ExportPreview::refreshHide(std::vector<SPItem*> &&list)
{
    _hidden_excluded = std::move(list);
    _hidden_requested = true;
}

void ExportPreview::performHide()
{
    if (_document) {
        if (isLastHide) {
            if (drawing) {
                _document->getRoot()->invoke_hide(visionkey);
            }
            drawing = std::make_shared<Inkscape::Drawing>();
            visionkey = SPItem::display_key_new(1);
            if (auto di = _document->getRoot()->invoke_show(*drawing, visionkey, SP_ITEM_SHOW_DISPLAY)) {
                drawing->setRoot(di);
            } else {
                drawing.reset();
            }
            isLastHide = false;
        }
        if (!_hidden_excluded.empty()) {
            _document->getRoot()->invoke_hide_except(visionkey, _hidden_excluded);
            isLastHide = true;
        }
    }
}

static bool debug_busyloop()
{
    static bool enabled = std::getenv("INKSCAPE_DEBUG_EXPORTDIALOG_BUSYLOOP");
    return enabled;
}

void ExportPreview::queueRefresh()
{
    if (!drawing || refresh_conn.connected() || dest) {
        return;
    }

    refresh_conn = Glib::signal_timeout().connect([this] { renderPreview(); return false; }, debug_busyloop() ? 1 : delay_msecs);
}

/*
 * This is the main function which finally renders the preview. Call this after setting document, item and dbox.
 * If dbox is given it will use it.
 * if item is given and not dbox then item is used.
 * If both are not given then we simply do nothing.
 */
void ExportPreview::renderPreview()
{
    if (!drawing || dest) {
        return;
    }

    if (_hidden_requested) {
        performHide();
        _hidden_requested = false;
    }

    dest = UI::Preview::render_preview(_document, drawing, _bg_color, _item, size, size, _dbox ? &_dbox : nullptr, [this] (Cairo::RefPtr<Cairo::ImageSurface> surface, int elapsed_msecs) {
        if (surface) {
            set(Gdk::Pixbuf::create(surface, 0, 0, surface->get_width(), surface->get_height()));
            show();
        }
        delay_msecs = std::max(100, elapsed_msecs * 3);
        dest.close();

        if (debug_busyloop()) {
            renderPreview();
        }
    });
}

void ExportPreview::setBackgroundColor(uint32_t bg_color)
{
    _bg_color = bg_color;
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
