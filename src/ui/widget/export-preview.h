// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_EXPORT_PREVIEW_H
#define INKSCAPE_UI_WIDGET_EXPORT_PREVIEW_H

#include <cstdint>
#include <2geom/rect.h>
#include <gtkmm.h>
#include "display/drawing.h"
#include "async/channel.h"

class SPDocument;
class SPObject;
class SPItem;

namespace Inkscape {
class Drawing;

namespace UI {
namespace Dialog {

class ExportPreview final : public Gtk::Image
{
public:
    ExportPreview() = default;
    ExportPreview(BaseObjectType *cobj, Glib::RefPtr<Gtk::Builder> const &) : Gtk::Image(cobj) {}
    ~ExportPreview() override;

    void setDocument(SPDocument *document);
    void refreshHide(std::vector<SPItem*> &&list = {});
    void setItem(SPItem *item);
    void setDbox(double x0, double x1, double y0, double y1);
    void queueRefresh();
    void resetPixels();
    void setSize(int newSize);
    void setBackgroundColor(uint32_t bg_color);

private:
    int size = 128; // size of preview image
    bool isLastHide = false;
    sigc::connection refresh_conn;
    bool _hidden_requested = false;

    SPDocument *_document = nullptr;
    SPItem *_item = nullptr;
    Geom::OptRect _dbox;

    std::shared_ptr<Drawing> drawing; // drawing implies _document
    int delay_msecs = 100;
    uint32_t _bg_color = 0;
    unsigned visionkey = 0;

    std::vector<SPItem*> _hidden_excluded;

    Async::Channel::Dest dest;

    void renderPreview();
    void performHide();
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_EXPORT_PREVIEW_H

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
