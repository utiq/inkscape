// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * This is the C++ glue between Inkscape and Autotrace
 *//*
 *
 * Authors:
 *   Marc Jeanmougin
 *
 * Copyright (C) 2018 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */
#include <iomanip>
#include <glibmm/i18n.h>
#include <gtkmm/main.h>

#include "inkscape-autotrace.h"
#include "trace/filterset.h"
#include "trace/imagemap-gdk.h"
#include "trace/quantize.h"

#include "inkscape.h"
#include "desktop.h"
#include "message-stack.h"
#include "object/sp-path.h"
#include "svg/path-string.h"

extern "C" {
#include "3rdparty/autotrace/autotrace.h"
#include "3rdparty/autotrace/output.h"
#include "3rdparty/autotrace/spline.h"
}

namespace Inkscape {
namespace Trace {
namespace Autotrace {

namespace {

/**
 * Eliminate the alpha channel by overlaying on top of white, and ensure the result is in packed RGB8 format.
 * If nothing needs to be done, the original pixbuf is returned, otherwise a new pixbuf is returned.
 */
Glib::RefPtr<Gdk::Pixbuf> to_rgb8_packed(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    int width     = pixbuf->get_width();
    int height    = pixbuf->get_height();
    int rowstride = pixbuf->get_rowstride();
    int nchannels = pixbuf->get_n_channels();
    auto data     = pixbuf->get_pixels();

    if (nchannels == 3 && rowstride == width * 3) {
        return pixbuf;
    }

    int imgsize = width * height;
    auto out = new unsigned char[3 * imgsize];
    auto q = out;

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            unsigned char alpha = nchannels == 3 ? 255 : p[3];
            unsigned char white = 255 - alpha;
            for (int c = 0; c < 3; c++) {
                *(q++) = (int)p[c] * alpha / 256 + white;
            }
            p += nchannels;
        }
    }

    return Gdk::Pixbuf::create_from_data(out, Gdk::COLORSPACE_RGB, false, 8, width, height, width * 3, [out] (auto) { delete [] out; });
}

} // namespace

AutotraceTracingEngine::AutotraceTracingEngine()
    : keepGoing(true)
{
    // Create options struct, automatically filled with defaults.
    opts = at_fitting_opts_new();
    opts->background_color = at_color_new(255, 255, 255);
    autotrace_init();
}

AutotraceTracingEngine::~AutotraceTracingEngine()
{
    at_fitting_opts_free(opts);
}

Glib::RefPtr<Gdk::Pixbuf> AutotraceTracingEngine::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    // Todo: Actually generate a meaningful preview.
    return to_rgb8_packed(pixbuf);
}

std::vector<TracingEngineResult> AutotraceTracingEngine::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    auto pb = to_rgb8_packed(pixbuf);
    
    at_bitmap bitmap;
    bitmap.height = pb->get_height();
    bitmap.width  = pb->get_width();
    bitmap.bitmap = pb->get_pixels();
    bitmap.np     = 3;
    
    auto splines = at_splines_new_full(&bitmap, opts, nullptr, nullptr, nullptr, nullptr, [] (gpointer data) -> gboolean {
        return reinterpret_cast<AutotraceTracingEngine const *>(data)->test_cancel();
    }, this);
    // at_output_write_func wfunc = at_output_get_handler_by_suffix("svg");
    // at_spline_writer *wfunc = at_output_get_handler_by_suffix("svg");

    int height = splines->height;
    at_spline_list_array_type spline = *splines;

    unsigned this_list;
    at_spline_list_type list;
    at_color last_color = { 0, 0, 0 };

    std::stringstream theStyle;
    std::stringstream thePath;
    char color[10];
    int nNodes = 0;

    std::vector<TracingEngineResult> res;

    // at_splines_write(wfunc, stdout, "", NULL, splines, NULL, NULL);

    for (this_list = 0; this_list < SPLINE_LIST_ARRAY_LENGTH(spline); this_list++) {
        unsigned this_spline;
        at_spline_type first;

        list = SPLINE_LIST_ARRAY_ELT(spline, this_list);
        first = SPLINE_LIST_ELT(list, 0);

        if (this_list == 0 || !at_color_equal(&list.color, &last_color)) {
            if (this_list > 0) {
                if (!(spline.centerline || list.open)) {
                    thePath << "z";
                    nNodes++;
                }
                res.emplace_back(theStyle.str(), thePath.str(), nNodes);
                theStyle.clear();
                thePath.clear();
                nNodes = 0;
            }
            std::sprintf(color, "#%02x%02x%02x;", list.color.r, list.color.g, list.color.b);

            theStyle << ((spline.centerline || list.open) ? "stroke:" : "fill:") << color
                     << ((spline.centerline || list.open) ? "fill:" : "stroke:") << "none";
        }
        thePath << "M" << START_POINT(first).x << " " << height - START_POINT(first).y;
        nNodes++;
        for (this_spline = 0; this_spline < SPLINE_LIST_LENGTH(list); this_spline++) {
            at_spline_type s = SPLINE_LIST_ELT(list, this_spline);

            if (SPLINE_DEGREE(s) == AT_LINEARTYPE) {
                thePath << "L" << END_POINT(s).x << " " << height - END_POINT(s).y;
                nNodes++;
            }
            else {
                thePath << "C" << CONTROL1(s).x << " " << height - CONTROL1(s).y << " " << CONTROL2(s).x << " "
                        << height - CONTROL2(s).y << " " << END_POINT(s).x << " " << height - END_POINT(s).y;
                nNodes++;
            }
            last_color = list.color;
        }
    }

    if (!(spline.centerline || list.open)) {
        thePath << "z";
    }
    nNodes++;

    if (SPLINE_LIST_ARRAY_LENGTH(spline) > 0) {
        TracingEngineResult ter(theStyle.str(), thePath.str(), nNodes);
        res.push_back(ter);
        theStyle.clear();
        thePath.clear();
        nNodes = 0;
    }

    return res;
}

void AutotraceTracingEngine::abort()
{
    // g_message("PotraceTracingEngine::abort()\n");
    keepGoing = false;
}

void AutotraceTracingEngine::setColorCount(unsigned color_count)
{
    opts->color_count = color_count;
}

void AutotraceTracingEngine::setCenterLine(bool centerline)
{
    opts->centerline = centerline;
}

void AutotraceTracingEngine::setPreserveWidth(bool preserve_width)
{
    opts->preserve_width = preserve_width;
}

void AutotraceTracingEngine::setFilterIterations(unsigned filter_iterations)
{
    opts->filter_iterations = filter_iterations;
}

void AutotraceTracingEngine::setErrorThreshold(float error_threshold)
{
    opts->error_threshold = error_threshold;
}

bool AutotraceTracingEngine::test_cancel() const
{
    return !keepGoing;
}

} // namespace Autotrace
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
