// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_EXPORT_PREVIEW_H
#define SP_EXPORT_PREVIEW_H

#include <gtkmm.h>
#include <glibmm/timer.h>

#include "desktop.h"
#include "document.h"
#include "display/drawing.h"

class SPObject;
class SPItem;

namespace Inkscape {
namespace UI {
namespace Dialog {

class ExportPreview : public Gtk::Image
{
public:
    ExportPreview() = default;
    ~ExportPreview() override;
    ExportPreview(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder> &)
        : Gtk::Image(cobject)
    {
    }

private:
    int size = 128; // size of preview image
    bool isLastHide = false;
    bool pending = false;
    bool _hidden_requested = false;

    SPDocument *_document = nullptr;
    SPItem *_item = nullptr;
    Geom::OptRect _dbox;

    std::unique_ptr<Drawing> drawing;
    std::unique_ptr<Glib::Timer> timer;
    std::unique_ptr<Glib::Timer> renderTimer;
    gdouble minDelay = 0.1;
    guint32 _bg_color = 0;
    unsigned int visionkey = 0;

    std::vector<SPItem *> _hidden_excluded;
public:
    void setDocument(SPDocument *document);
    void refreshHide(std::vector<SPItem *> const &list = {});
    void setItem(SPItem *item);
    void setDbox(double x0, double x1, double y0, double y1);
    void queueRefresh();
    void resetPixels();

    void setSize(int newSize)
    {
        size = newSize;
        resetPixels();
    }

    void set_background_color(guint32 bg_color);
private:
    void refreshPreview();
    void renderPreview();
    bool refreshCB();
    void performHide();
};
} // namespace Dialog
} // namespace UI
} // namespace Inkscape
#endif

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
