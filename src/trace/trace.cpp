// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A generic interface for plugging different
 *  autotracers into Inkscape.
 *
 * Authors:
 *   Bob Jamison <rjamison@earthlink.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2006 Bob Jamison
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <limits>
#include <boost/range/adaptor/reversed.hpp>
#include <glibmm/i18n.h>
#include <gtkmm/main.h>

#include <2geom/transforms.h>

#include "trace.h"
#include "siox.h"

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape.h"
#include "message-stack.h"
#include "selection.h"

#include "display/cairo-utils.h"
#include "display/drawing.h"
#include "display/drawing-shape.h"
#include "display/drawing-context.h"

#include "object/sp-item.h"
#include "object/sp-shape.h"
#include "object/sp-image.h"

#include "ui/icon-names.h"

#include "xml/repr.h"
#include "xml/attribute-record.h"

/**
 * Given an SPImage, get the transform from pixbuf coordinates to the document.
 */
static Geom::Affine getImageTransform(SPImage const *img)
{
    double x = img->x.computed;
    double y = img->y.computed;
    double w = img->width.computed;
    double h = img->height.computed;

    int iw = img->pixbuf->width();
    int ih = img->pixbuf->height();

    double wscale = w / iw;
    double hscale = h / ih;

    return Geom::Scale(wscale, hscale) * Geom::Translate(x, y) * img->transform;
}

namespace Inkscape {
namespace Trace {

SPImage *Tracer::getSelectedSPImage()
{
    auto desktop = SP_ACTIVE_DESKTOP;
    if (!desktop) {
        g_warning("Trace: No active desktop");
        return nullptr;
    }

    auto msgStack = desktop->getMessageStack();

    auto sel = desktop->getSelection();
    if (!sel) {
        msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace"));
        return nullptr;
    }

    if (sioxEnabled) {
        SPImage *img = nullptr;
        sioxItems.clear();

        for (auto item : sel->items()) {
            if (auto itemimg = SP_IMAGE(item)) {
                if (img) { // we want only one
                    msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select only one <b>image</b> to trace"));
                    return nullptr;
                }
                img = itemimg;
            } else if (img) { // Items are processed back-to-front, so this means "above the image".
                sioxItems.emplace_back(item);
            }
        }

        if (!img || sioxItems.size() < 1) {
            msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select one image and one or more shapes above it"));
            return nullptr;
        }
        return img;
    } else {
        // SIOX not enabled. We want exactly one image selected
        auto item = sel->singleItem();
        if (!item) {
            msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace")); // same as above
            return nullptr;
        }

        auto img = SP_IMAGE(item);
        if (!img) {
            msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace"));
            return nullptr;
        }

        return img;
    }
}

class TraceSioxObserver
    : public SioxObserver
{
public:
    bool progress(float percentCompleted) override
    {
        // Allow the GUI to update.
        Gtk::Main::iteration(false); // at least once, non-blocking
        while (Gtk::Main::events_pending()) {
            Gtk::Main::iteration();
        }
        return true;
    }
};

Glib::RefPtr<Gdk::Pixbuf> Tracer::sioxProcessImage(SPImage *img, Glib::RefPtr<Gdk::Pixbuf> const &origPixbuf)
{
    if (!sioxEnabled) {
        return origPixbuf;
    }

    auto desktop = SP_ACTIVE_DESKTOP;
    if (!desktop) {
        g_warning("%s", _("Trace: No active desktop"));
        return {};
    }

    auto msgStack = desktop->getMessageStack();

    auto sel = desktop->getSelection();
    if (!sel) {
        msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace"));
        return {};
    }

    SioxImage simage(origPixbuf);
    int iwidth = simage.getWidth();
    int iheight = simage.getHeight();

    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, iwidth, iheight);
    auto dc = Inkscape::DrawingContext(surface->cobj(), {});
    auto tf = getImageTransform(img);
    dc.transform(tf.inverse());

    auto dkey = SPItem::display_key_new(1);
    Inkscape::Drawing drawing;

    for (auto item : sioxItems) {
        auto ai = item->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY);
        drawing.setRoot(ai);
        auto rect = Geom::IntRect(0, 0, iwidth, iheight);
        drawing.update(rect);
        drawing.render(dc, rect);
        item->invoke_hide(dkey);
    }

    for (int y = 0; y < iheight; y++) {
        for (int x = 0; x < iwidth; x++) {
            auto p = surface->get_data() + y * surface->get_stride() + 4 * x;
            float a = p[3] / 255.0f;
            float cm = Siox::CERTAIN_BACKGROUND_CONFIDENCE + (Siox::UNKNOWN_REGION_CONFIDENCE - Siox::CERTAIN_BACKGROUND_CONFIDENCE) * a;
            simage.setConfidence(x, y, cm);
        }
    }

    /*auto tmp = simage;
    for (int i = 0; i < iwidth * iheight; i++) {
        tmp.getImageData()[i] = 255 * tmp.getConfidenceData()[i];
    }
    tmp.writePPM("/tmp/x1.ppm");*/

    auto hash = simage.hash();
    if (hash != lastSioxHash) {
        lastSioxHash = hash;

        auto result = Siox().extractForeground(simage, 0xffffff);
        if (!result) {
            g_warning("%s", _("Invalid SIOX result"));
            return {};
        }

        // result->writePPM("siox2.ppm");

        lastSioxPixbuf = result->getGdkPixbuf();
    }

    return lastSioxPixbuf;
}

Glib::RefPtr<Gdk::Pixbuf> Tracer::getSelectedImage()
{
    auto img = getSelectedSPImage();
    if (!img) {
        return {};
    }

    if (!img->pixbuf) {
        return {};
    }

    auto copy = Pixbuf(*img->pixbuf);
    auto pb = Glib::wrap(copy.getPixbufRaw(), true);

    auto sioxPixbuf = sioxProcessImage(img, pb);
    return sioxPixbuf ? sioxPixbuf : pb;
}

//#########################################################################
//#  T R A C E
//#########################################################################

void Tracer::enableSiox(bool enable)
{
    sioxEnabled = enable;
}

void Tracer::traceThread()
{
    // Remember. NEVER leave this method without setting
    // engine back to NULL

    // Prepare our kill flag. We will watch this later to
    // see if the main thread wants us to stop.
    keepGoing = true;

    auto desktop = SP_ACTIVE_DESKTOP;
    if (!desktop) {
        g_warning("Trace: No active desktop\n");
        return;
    }

    auto msgStack = desktop->getMessageStack();

    auto selection = desktop->getSelection();

    if (!SP_ACTIVE_DOCUMENT) {
        msgStack->flash(Inkscape::ERROR_MESSAGE, _("Trace: No active document"));
        engine = nullptr;
        return;
    }
    auto doc = SP_ACTIVE_DOCUMENT;
    doc->ensureUpToDate();

    auto img = getSelectedSPImage();
    if (!img) {
        engine = nullptr;
        return;
    }

    auto copy = Pixbuf(*img->pixbuf);
    auto pixbuf = Glib::wrap(copy.getPixbufRaw(), true);

    pixbuf = sioxProcessImage(img, pixbuf);

    if (!pixbuf) {
        msgStack->flash(Inkscape::ERROR_MESSAGE, _("Trace: Image has no bitmap data"));
        engine = nullptr;
        return;
    }

    msgStack->flash(Inkscape::NORMAL_MESSAGE, _("Trace: Starting trace..."));

    std::vector<TracingEngineResult> results = engine->trace(pixbuf);
    // printf("nrPaths:%d\n", results.size());
    int nrPaths = results.size();

    // Check if we should stop
    if (!keepGoing || nrPaths < 1) {
        engine = nullptr;
        return;
    }

    // Get pointers to the <image> and its parent
    // XML Tree being used directly here while it shouldn't be
    Inkscape::XML::Node *imgRepr = img->getRepr();
    Inkscape::XML::Node *par     = imgRepr->parent();

    // Get some information for the new transform
    auto tf = getImageTransform(img);

    // OK. Now let's start making new nodes

    Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
    Inkscape::XML::Node *groupRepr = nullptr;

    //# if more than 1, make a <g>roup of <path>s
    if (nrPaths > 1) {
        groupRepr = xml_doc->createElement("svg:g");
        par->addChild(groupRepr, imgRepr);
    }

    long totalNodeCount = 0;

    for (auto const &result : results) {
        totalNodeCount += result.nodeCount;

        Inkscape::XML::Node *pathRepr = xml_doc->createElement("svg:path");
        pathRepr->setAttributeOrRemoveIfEmpty("style", result.style);
        pathRepr->setAttributeOrRemoveIfEmpty("d",     result.pathData);

        if (nrPaths > 1) {
            groupRepr->addChild(pathRepr, nullptr);
        } else {
            par->addChild(pathRepr, imgRepr);
        }

        // Apply the transform from the image to the new shape
        SPObject *reprobj = doc->getObjectByRepr(pathRepr);
        if (reprobj) {
            SPItem *newItem = SP_ITEM(reprobj);
            newItem->doWriteTransform(tf);
        }

        if (nrPaths == 1) {
            selection->clear();
            selection->add(pathRepr);
        }

        Inkscape::GC::release(pathRepr);
    }

    // If we have a group, then focus on, then forget it
    if (nrPaths > 1) {
        selection->clear();
        selection->add(groupRepr);
        Inkscape::GC::release(groupRepr);
    }

    // Inform the document, so we can undo
    DocumentUndo::done(doc, _("Trace bitmap"), INKSCAPE_ICON("bitmap-trace"));

    engine = nullptr;

    char *msg = g_strdup_printf(_("Trace: Done. %ld nodes created"), totalNodeCount);
    msgStack->flash(Inkscape::NORMAL_MESSAGE, msg);
    g_free(msg);
}

void Tracer::trace(TracingEngine *theEngine)
{
    // Check if we are already running
    if (engine) {
        return;
    }

    engine = theEngine;

    traceThread();
}

void Tracer::abort()
{
    // Inform Trace's working thread
    keepGoing = false;

    if (engine) {
        engine->abort();
    }
}

} // namespace Trace
} // namespace Inkscape
