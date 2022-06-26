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
#include <iomanip>
#include <glibmm/i18n.h>
#include <gtkmm/main.h>
#include <potracelib.h>

#include "inkscape-potrace.h"
#include "bitmap.h"
#include "trace/filterset.h"
#include "trace/quantize.h"
#include "trace/imagemap-gdk.h"

#include "inkscape.h"
#include "desktop.h"
#include "message-stack.h"
#include "object/sp-path.h"
#include "svg/path-string.h"

namespace {

void updateGui()
{
   // Allow the GUI to update
   Gtk::Main::iteration(false); // at least once, non-blocking
   while (Gtk::Main::events_pending()) {
       Gtk::Main::iteration();
   }
}

Glib::ustring twohex(int value)
{
    return Glib::ustring::format(std::hex, std::setfill(L'0'), std::setw(2), value);
}

} // namespace

namespace Inkscape {
namespace Trace {
namespace Potrace {

PotraceTracingEngine::PotraceTracingEngine()
{
    common_init();
}

PotraceTracingEngine::PotraceTracingEngine(TraceType traceType, bool invert, int quantizationNrColors, double brightnessThreshold, double brightnessFloor, double cannyHighThreshold, int multiScanNrColors, bool multiScanStack, bool multiScanSmooth, bool multiScanRemoveBackground)
    : traceType(traceType)
    , invert(invert)
    , quantizationNrColors(quantizationNrColors)
    , brightnessThreshold(brightnessThreshold)
    , brightnessFloor(brightnessFloor)
    , cannyHighThreshold(cannyHighThreshold)
    , multiScanNrColors(multiScanNrColors)
    , multiScanStack(multiScanStack)
    , multiScanSmooth(multiScanSmooth)
    , multiScanRemoveBackground(multiScanRemoveBackground)
{
    common_init();
}

void PotraceTracingEngine::common_init()
{
    potraceParams = potrace_param_default();
    potraceParams->progress.data = this;
    potraceParams->progress.callback = [] (double progress, void *data) { reinterpret_cast<PotraceTracingEngine*>(data)->status_callback(progress); };
}

PotraceTracingEngine::~PotraceTracingEngine()
{
    potrace_param_free(potraceParams);
}

void PotraceTracingEngine::status_callback(double progress)
{
    updateGui();

    // g_message("progress: %f\n", progress);
}

void PotraceTracingEngine::abort()
{
    // g_message("PotraceTracingEngine::abort()\n");
    keepGoing = false;
}

void PotraceTracingEngine::setOptiCurve(int opticurve)
{
    potraceParams->opticurve = opticurve;
}

void PotraceTracingEngine::setOptTolerance(double opttolerance)
{
    potraceParams->opttolerance = opttolerance;
}

void PotraceTracingEngine::setAlphaMax(double alphamax)
{
    potraceParams->alphamax = alphamax;
}

void PotraceTracingEngine::setTurdSize(int turdsize)
{
    potraceParams->turdsize = turdsize;
}

/**
 * Recursively descend the potrace_path_t node tree, writing paths in SVG
 * format into the output stream. The Point vector is used to prevent
 * redundant paths. Returns number of paths processed.
 */
long PotraceTracingEngine::writePaths(potrace_path_t *plist, SVG::PathString &data, std::vector<Geom::Point> &points) const
{
    long nodeCount = 0;

    potrace_path_t *node;
    for (node = plist; node; node = node->sibling) {
        potrace_curve_t *curve = &node->curve;
        // g_message("node->fm:%d\n", node->fm);
        if (!curve->n) {
            continue;
        }
        potrace_dpoint_t const *pt = curve->c[curve->n - 1];
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        double x2 = pt[2].x;
        double y2 = pt[2].y;
        // Have we been here already?
        if (std::find(points.begin(), points.end(), Geom::Point(x2, y2)) != points.end()) {
            // g_message("duplicate point: (%f,%f)\n", x2, y2);
            continue;
        } else {
            points.emplace_back(x2, y2);
        }
        data.moveTo(x2, y2);
        nodeCount++;

        for (int i = 0; i < curve->n; i++) {
            if (!keepGoing) {
                return 0;
            }
            pt = curve->c[i];
            x0 = pt[0].x;
            y0 = pt[0].y;
            x1 = pt[1].x;
            y1 = pt[1].y;
            x2 = pt[2].x;
            y2 = pt[2].y;
            switch (curve->tag[i]) {
                case POTRACE_CORNER:
                    data.lineTo(x1, y1).lineTo(x2, y2);
                    break;
                case POTRACE_CURVETO:
                    data.curveTo(x0, y0, x1, y1, x2, y2);
                    break;
                default:
                    break;
            }
            nodeCount++;
        }
        data.closePath();

        for (potrace_path_t *child = node->childlist; child; child=child->sibling) {
            nodeCount += writePaths(child, data, points);
        }
    }

    return nodeCount;
}

std::optional<GrayMap> PotraceTracingEngine::filter(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const
{
    std::optional<GrayMap> map;

    if (traceType == TRACE_QUANT) {

        // Color quantization -- banding
        auto rgbmap = gdkPixbufToRgbMap(pixbuf);
        // rgbMap->writePPM(rgbMap, "rgb.ppm");
        map = quantizeBand(rgbmap, quantizationNrColors);

    } else if (traceType == TRACE_BRIGHTNESS || traceType == TRACE_BRIGHTNESS_MULTI) {

        // Brightness threshold
        auto gm = gdkPixbufToGrayMap(pixbuf);
        map = GrayMap(gm.width, gm.height);

        double floor = 3.0 * brightnessFloor * 256.0;
        double cutoff = 3.0 * brightnessThreshold * 256.0;
        for (int y = 0; y < gm.height; y++) {
            for (int x = 0; x < gm.width; x++) {
                double brightness = gm.getPixel(x, y);
                bool black = brightness >= floor && brightness < cutoff;
                map->setPixel(x, y, black ? GrayMap::BLACK : GrayMap::WHITE);
            }
        }

        // map->writePPM(map, "brightness.ppm");

    } else if (traceType == TRACE_CANNY) {

        // Canny edge detection
        auto gm = gdkPixbufToGrayMap(pixbuf);
        map = grayMapCanny(gm, 0.1, cannyHighThreshold);
        // map->writePPM(map, "canny.ppm");

    }

    // Invert the image if necessary.
    if (map && invert) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                auto brightness = map->getPixel(x, y);
                brightness = GrayMap::WHITE - brightness;
                map->setPixel(x, y, brightness);
            }
        }
    }

    return map; // none of the above
}

std::optional<IndexedMap> PotraceTracingEngine::filterIndexed(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const
{
    std::optional<IndexedMap> map;

    auto gm = gdkPixbufToRgbMap(pixbuf);

    if (multiScanSmooth) {
        auto gaussMap = rgbMapGaussian(gm);
        map = rgbMapQuantize(gaussMap, multiScanNrColors);
    } else {
        map = rgbMapQuantize(gm, multiScanNrColors);
     }

    if (map && (traceType == TRACE_QUANT_MONO || traceType == TRACE_BRIGHTNESS_MULTI)) {
        // Turn to grays
        for (int i = 0; i < map->nrColors; i++) {
            auto rgb = map->clut[i];
            int grayVal = (rgb.r + rgb.g + rgb.b) / 3;
            rgb.r = rgb.g = rgb.b = grayVal;
            map->clut[i] = rgb;
        }
    }

    return map;
}

Glib::RefPtr<Gdk::Pixbuf> PotraceTracingEngine::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    if (traceType == TRACE_QUANT_COLOR ||
        traceType == TRACE_QUANT_MONO  ||
        traceType == TRACE_BRIGHTNESS_MULTI) // this is a lie: multipass doesn't use filterIndexed, but it's a better preview approx than filter()
    {
        auto gm = filterIndexed(pixbuf);
        if (!gm) {
            return {};
        }

        return indexedMapToGdkPixbuf(*gm);

    } else {

        auto gm = filter(pixbuf);
        if (!gm) {
            return {};
        }

        return grayMapToGdkPixbuf(*gm);
    }
}

/**
 * This is the actual wrapper of the call to Potrace. nodeCount
 * returns the count of nodes created. May be null if ignored.
 */
std::string PotraceTracingEngine::grayMapToPath(GrayMap const &grayMap, long *nodeCount)
{
    if (!keepGoing) {
        g_warning("aborted");
        return "";
    }

    auto potraceBitmap = bm_new(grayMap.width, grayMap.height);
    if (!potraceBitmap) {
        return "";
    }

    bm_clear(potraceBitmap, 0);

    // Read the data out of the GrayMap
    for (int y = 0; y < grayMap.height; y++) {
        for (int x = 0; x < grayMap.width; x++) {
            BM_UPUT(potraceBitmap, x, y, grayMap.getPixel(x, y) ? 0 : 1);
        }
    }

    //##Debug
    /*
    FILE *f = fopen("poimage.pbm", "wb");
    bm_writepbm(f, bm);
    fclose(f);
    */

    // trace a bitmap
    potrace_state_t *potraceState = potrace_trace(potraceParams, potraceBitmap);

    // Free the Potrace bitmap
    bm_free(potraceBitmap);

    if (!keepGoing) {
        g_warning("aborted");
        potrace_state_free(potraceState);
        return "";
    }

    Inkscape::SVG::PathString data;

    //## copy the path information into our d="" attribute string
    std::vector<Geom::Point> points;
    long thisNodeCount = writePaths(potraceState->plist, data, points);

    // free a potrace items
    potrace_state_free(potraceState);

    if (!keepGoing) {
        return "";
    }

    if (nodeCount) {
        *nodeCount = thisNodeCount;
    }

    return data.string();
}

/**
 * This is called for a single scan.
 */
std::vector<TracingEngineResult> PotraceTracingEngine::traceSingle(Glib::RefPtr<Gdk::Pixbuf> const &thePixbuf)
{
    std::vector<TracingEngineResult> results;

    brightnessFloor = 0.0; // important to set this

    auto grayMap = filter(thePixbuf);
    if (!grayMap) {
        return results;
    }

    long nodeCount = 0;
    std::string d = grayMapToPath(*grayMap, &nodeCount);

    char const *style = "fill:#000000";

    // g_message("### GOT '%s' \n", d);
    results.emplace_back(style, d, nodeCount);

    return results;
}

/**
 * This allows routines that already generate GrayMaps to skip image filtering, increasing performance.
 */
std::vector<TracingEngineResult> PotraceTracingEngine::traceGrayMap(GrayMap const &grayMap)
{
    std::vector<TracingEngineResult> results;

    brightnessFloor = 0.0; //important to set this

    long nodeCount = 0;
    std::string d = grayMapToPath(grayMap, &nodeCount);

    char const *style = "fill:#000000";

    // g_message("### GOT '%s' \n", d);
    results.emplace_back(style, d, nodeCount);

    return results;
}

/**
 * Called for multiple-scanning algorithms
 */
std::vector<TracingEngineResult> PotraceTracingEngine::traceBrightnessMulti(Glib::RefPtr<Gdk::Pixbuf> const &thePixbuf)
{
    std::vector<TracingEngineResult> results;

    double low   = 0.2; // bottom of range
    double high  = 0.9; // top of range
    double delta = (high - low) / multiScanNrColors;

    brightnessFloor = 0.0; // Set bottom to black

    int traceCount = 0;

    for (brightnessThreshold = low; brightnessThreshold <= high; brightnessThreshold += delta) {
        auto grayMap = filter(thePixbuf);
        if (!grayMap) {
            continue;
        }

        long nodeCount = 0;
        std::string d = grayMapToPath(*grayMap, &nodeCount);

        if (d.empty()) {
            continue;
        }

        // get style info
        int grayVal = 256.0 * brightnessThreshold;
        auto style = Glib::ustring::compose("fill-opacity:1.0;fill:#%1%2%3", twohex(grayVal), twohex(grayVal), twohex(grayVal) );

        // g_message("### GOT '%s' \n", style.c_str());
        results.emplace_back(style.raw(), d, nodeCount);

        if (!multiScanStack) {
            brightnessFloor = brightnessThreshold;
        }

        auto desktop = SP_ACTIVE_DESKTOP;
        if (desktop) {
            auto msg = Glib::ustring::compose(_("Trace: %1.  %2 nodes"), traceCount++, nodeCount);
            desktop->getMessageStack()->flash(Inkscape::NORMAL_MESSAGE, msg);
        }
    }

    // Remove the bottom-most scan, if requested.
    if (results.size() > 1 && multiScanRemoveBackground) {
        results.erase(results.end() - 1);
    }

    return results;
}

/**
 * Quantization
 */
std::vector<TracingEngineResult> PotraceTracingEngine::traceQuant(Glib::RefPtr<Gdk::Pixbuf> const &thePixbuf)
{
    auto imap = filterIndexed(thePixbuf);
    if (!imap) {
        return {};
    }

    // Create and clear a gray map
    auto gm = GrayMap(imap->width, imap->height);
    for (int row = 0; row < gm.height; row++) {
        for (int col = 0; col < gm.width; col++) {
            gm.setPixel(col, row, GrayMap::WHITE);
        }
    }

    std::vector<TracingEngineResult> results;

    for (int colorIndex = 0; colorIndex < imap->nrColors; colorIndex++) {
        // Make a gray map for each color index
        for (int row = 0; row < imap->height; row++) {
            for (int col = 0; col < imap->width; col++) {
                int indx = imap->getPixel(col, row);
                if (indx == colorIndex) {
                    gm.setPixel(col, row, GrayMap::BLACK);
                } else if (!multiScanStack) {
                    gm.setPixel(col, row, GrayMap::WHITE);
                }
            }
        }

        // Now we have a traceable graymap
        long nodeCount = 0;
        std::string d = grayMapToPath(gm, &nodeCount);

        if (!d.empty()) {
            // get style info
            RGB rgb = imap->clut[colorIndex];
            auto style = Glib::ustring::compose("fill:#%1%2%3", twohex(rgb.r), twohex(rgb.g), twohex(rgb.b));

            // g_message("### GOT '%s' \n", style.c_str());
            results.emplace_back(style.raw(), d, nodeCount);

            auto desktop = SP_ACTIVE_DESKTOP;
            if (desktop) {
                auto msg = Glib::ustring::compose(_("Trace: %1.  %2 nodes"), colorIndex, nodeCount);
                desktop->getMessageStack()->flash(Inkscape::NORMAL_MESSAGE, msg);
            }
        }
    }

    // Remove the bottom-most scan, if requested.
    if (results.size() > 1 && multiScanRemoveBackground) {
        results.pop_back();
    }

    return results;
}

std::vector<TracingEngineResult> PotraceTracingEngine::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    // Set up for messages
    keepGoing = true;

    if (traceType == TRACE_QUANT_COLOR || traceType == TRACE_QUANT_MONO) {
        return traceQuant(pixbuf);
    } else if (traceType == TRACE_BRIGHTNESS_MULTI) {
        return traceBrightnessMulti(pixbuf);
    } else {
        return traceSingle(pixbuf);
    }
}

} // namespace Potrace
} // namespace Trace
} // namespace Inkscape

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
