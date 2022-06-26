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
#include "inkscape-depixelize.h"

#include <iomanip>
#include <thread>
#include <glibmm/i18n.h>
#include <gtkmm/main.h>
#include <gtkmm.h>

#include "desktop.h"
#include "message-stack.h"
#include "helper/geom.h"
#include "object/sp-path.h"
#include "display/cairo-templates.h"

#include <svg/path-string.h>
#include <svg/svg.h>
#include <svg/svg-color.h>
#include "svg/css-ostringstream.h"

namespace Inkscape {
namespace Trace {
namespace Depixelize {

DepixelizeTracingEngine::DepixelizeTracingEngine()
    : traceType(TRACE_VORONOI)
{
}

DepixelizeTracingEngine::DepixelizeTracingEngine(TraceType traceType, double curves, int islands, int sparsePixels, double sparseMultiplier, bool optimize)
    : traceType(traceType)
{
    params.curvesMultiplier = curves;
    params.islandsWeight = islands;
    params.sparsePixelsRadius = sparsePixels;
    params.sparsePixelsMultiplier = sparseMultiplier;
    params.optimize = optimize;
    params.nthreads = Inkscape::Preferences::get()->getIntLimited("/options/threading/numthreads", std::thread::hardware_concurrency(), 1, 256);
}

std::vector<TracingEngineResult> DepixelizeTracingEngine::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    std::vector<TracingEngineResult> res;

    if (pixbuf->get_width() > 256 || pixbuf->get_height() > 256) {
        char *msg = _("Image looks too big. Process may take a while and it is"
                      " wise to save your document before continuing."
                      "\n\nContinue the procedure (without saving)?");
        Gtk::MessageDialog dialog(msg, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);

        if (dialog.run() != Gtk::RESPONSE_OK) {
            return res;
        }
    }

    ::Tracer::Splines splines;

    if (traceType == TRACE_VORONOI) {
        splines = ::Tracer::Kopf2011::to_voronoi(pixbuf, params);
    } else {
        splines = ::Tracer::Kopf2011::to_splines(pixbuf, params);
    }

    for (auto const &it : splines) {
        char b[64];
        sp_svg_write_color(b, sizeof(b),
                           SP_RGBA32_U_COMPOSE(unsigned(it.rgba[0]),
                                               unsigned(it.rgba[1]),
                                               unsigned(it.rgba[2]),
                                               unsigned(it.rgba[3])));
        Inkscape::CSSOStringStream osalpha;
        osalpha << it.rgba[3] / 255.0f;
        char *style = g_strdup_printf("fill:%s;fill-opacity:%s;", b, osalpha.str().c_str());
        printf("%s\n", style);
        res.emplace_back(style, sp_svg_write_path(it.pathVector), count_pathvector_nodes(it.pathVector));
        g_free(style);
    }

    return res;
}

void DepixelizeTracingEngine::abort()
{
    // Unimplemented, as this operation is not supported by libdepixelize.
}

Glib::RefPtr<Gdk::Pixbuf> DepixelizeTracingEngine::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    return pixbuf;
}

} // namespace Depixelize
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
