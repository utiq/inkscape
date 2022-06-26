// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This is the C++ glue between Inkscape and Potrace
 *
 * Authors:
 *   Bob Jamison <rjamison@titan.com>
 *   Stéphane Gimenez <dev@gim.name>
 *
 * Copyright (C) 2004-2006 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 * Potrace, the wonderful tracer located at http://potrace.sourceforge.net,
 * is provided by the generosity of Peter Selinger, to whom we are grateful.
 *
 */
#ifndef INKSCAPE_TRACE_POTRACE_H
#define INKSCAPE_TRACE_POTRACE_H

#include <optional>
#include <2geom/point.h>
#include "trace/trace.h"
#include "trace/imagemap.h"
using potrace_param_t = struct potrace_param_s;
using potrace_path_t  = struct potrace_path_s;

namespace Inkscape {
namespace SVG {
class PathString;
} // namespace SVG
namespace Trace {
namespace Potrace {

enum TraceType
{
    TRACE_BRIGHTNESS,
    TRACE_BRIGHTNESS_MULTI,
    TRACE_CANNY,
    TRACE_QUANT,
    TRACE_QUANT_COLOR,
    TRACE_QUANT_MONO,
    // Used in tracedialog.cpp
    AUTOTRACE_SINGLE,
    AUTOTRACE_MULTI,
    AUTOTRACE_CENTERLINE
};

class PotraceTracingEngine final
    : public TracingEngine
{
public:
    PotraceTracingEngine();
    PotraceTracingEngine(TraceType traceType,
                         bool invert,
                         int quantizationNrColors,
                         double brightnessThreshold,
                         double brightnessFloor,
                         double cannyHighThreshold,
                         int multiScanNrColors,
                         bool multiScanStack,
                         bool multiScanSmooth ,
                         bool multiScanRemoveBackground);
    ~PotraceTracingEngine() override;

    std::vector<TracingEngineResult> trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) override;
    Glib::RefPtr<Gdk::Pixbuf> preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) override;
    void abort() override;

    void setOptiCurve(int);
    void setOptTolerance(double);
    void setAlphaMax(double);
    void setTurdSize(int);

    std::vector<TracingEngineResult> traceGrayMap(GrayMap const &grayMap);

private:
    potrace_param_t *potraceParams;

    TraceType traceType = TRACE_BRIGHTNESS;

    // Whether the image should be inverted at the end.
    bool invert = false;

    // Color -> b&w quantization
    int quantizationNrColors = 8;

    // Brightness items
    double brightnessThreshold = 0.45;
    double brightnessFloor = 0.0;

    // Canny items
    double cannyHighThreshold = 0.65;

    // Color -> multiscan quantization
    int multiScanNrColors = 8;
    bool multiScanStack = true; // do we tile or stack?
    bool multiScanSmooth = false; // do we use gaussian filter?
    bool multiScanRemoveBackground = false; // do we remove the bottom trace?

    bool keepGoing = true;

    void common_init();

    void status_callback(double progress);
    
    std::string grayMapToPath(GrayMap const &gm, long *nodeCount);

    std::vector<TracingEngineResult> traceBrightnessMulti(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);
    std::vector<TracingEngineResult> traceQuant          (Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);
    std::vector<TracingEngineResult> traceSingle         (Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);

    long writePaths(potrace_path_t *plist, SVG::PathString &data, std::vector<Geom::Point> &points) const;

    std::optional<GrayMap>    filter       (Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const;
    std::optional<IndexedMap> filterIndexed(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const;
};

} // namespace Potrace
} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_POTRACE_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
