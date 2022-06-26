// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Bob Jamison <rjamison@titan.com>
 *
 * Copyright (C) 2004-2006 Bob Jamison
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef INKSCAPE_TRACE_H
#define INKSCAPE_TRACE_H

#include <vector>
#include <utility>
#include <cstring>
#include <gdkmm/pixbuf.h>

class SPImage;
class SPItem;
class SPShape;

namespace Inkscape {
namespace Trace {

struct TracingEngineResult
{
    TracingEngineResult(std::string style_, std::string pathData_, long nodeCount_)
        : style(std::move(style_))
        , pathData(std::move(pathData_))
        , nodeCount(nodeCount_) {}

    std::string style;
    std::string pathData;
    long nodeCount;
};

/**
 * A generic interface for plugging different autotracers into Inkscape.
 */
class TracingEngine
{
public:
    TracingEngine() = default;
    virtual ~TracingEngine() = default;

    /**
     * This is the working method of this interface, and all
     * implementing classes. Take a GdkPixbuf, trace it, and
     * return a style attribute and the path data that is
     * compatible with the d="" attribute
     * of an SVG <path> element.
     */
    virtual std::vector<TracingEngineResult> trace(Glib::RefPtr<Gdk::Pixbuf> const &) = 0;

    virtual Glib::RefPtr<Gdk::Pixbuf> preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) = 0;

    /**
     * Abort the thread that is executing getPathDataFromPixbuf().
     */
    virtual void abort() = 0;
};

/**
 * This simple class allows a generic wrapper around a given
 * TracingEngine object. Its purpose is to provide a gateway
 * to a variety of tracing engines, while maintaining a
 * consistent interface.
 */
class Tracer
{
public:
    Tracer()
    {
        engine = nullptr;
        sioxEnabled = false;
    }

    /**
     * A convenience method to allow other software to 'see' the
     * same image that this class sees.
     */
    Glib::RefPtr<Gdk::Pixbuf> getSelectedImage();

    /**
     * This is the main working method. Trace the selected image, if
     * any, and create a <path> element from it, inserting it into
     * the current document.
     */
    void trace(TracingEngine *engine);

    /**
     * Abort the thread that is executing convertImageToPath()
     */
    void abort();

    /**
     * Whether we want to enable SIOX subimage selection.
     */
    void enableSiox(bool enable);

private:
    /**
     * This is the single path code that is called by its counterpart above.
     * Threaded method that does single bitmap -> path conversion.
     */
    void traceThread();

    /**
     * This is true during execution. Setting it to false (like abort()
     * does) should inform the threaded code that it needs to stop
     */
    bool keepGoing;

    /**
     * During tracing, this is non-null, and refers to the
     * engine that is currently doing the tracing.
     */
    TracingEngine *engine;

    /**
     * Get the selected image. Also check for any SPItems over it, in
     * case the user wants SIOX pre-processing.
     */
    SPImage *getSelectedSPImage();

    std::vector<SPItem*> sioxItems;

    bool sioxEnabled;

    /**
     * Process a GdkPixbuf, according to which areas have been
     * obscured in the GUI.
     */
    Glib::RefPtr<Gdk::Pixbuf> sioxProcessImage(SPImage *img, Glib::RefPtr<Gdk::Pixbuf> const &origPixbuf);

    unsigned lastSioxHash = 0;
    Glib::RefPtr<Gdk::Pixbuf> lastSioxPixbuf;
};

} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_H
