// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Utility functions for generating export previews.
 */
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *   Martin Owens <doctormo@gmail.com>
 *
 * Copyright (C) 2021 Anshudhar Kumar Singh
 *               2021 Martin Owens
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "preview.h"

#include "document.h"
#include "display/cairo-utils.h"
#include "display/drawing-context.h"
#include "object/sp-namedview.h"
#include "object/sp-root.h"
#include "async/async.h"

namespace Inkscape {
namespace UI {
namespace Preview {

Async::Channel::Dest render_preview(SPDocument *doc, std::shared_ptr<Inkscape::Drawing> drawing, uint32_t bg, SPItem *item,
                                    unsigned width_in, unsigned height_in, Geom::OptRect const *dboxIn, std::function<void(Cairo::RefPtr<Cairo::ImageSurface>, int)> &&onfinished)
{
    if (!drawing->root())
        return {};

    if (auto name = item ? item->getId() : nullptr) {
        // Get item even if it's in another document.
        if (item->document != doc) {
            item = cast<SPItem>(doc->getObjectById(name));
        }
    }

    Geom::OptRect dbox;
    if (dboxIn) {
        dbox = *dboxIn;
    } else if (item) {
        if (item->parent) {
            dbox = item->documentVisualBounds();
        } else {
            dbox = doc->preferredBounds();
        }
    } else if (doc->getRoot()) {
        // If we still don't have a dbox we will use document coordinates.
        dbox = doc->getRoot()->documentVisualBounds();
    }

    // If we still dont have anything to render then return.
    if (!dbox) return {};

    // Calculate a scaling factor for the requested bounding box.
    double sf = 1.0;
    Geom::IntRect ibox = dbox->roundOutwards();
    if (ibox.width() != width_in || ibox.height() != height_in) {
        sf = std::min((double)width_in / dbox->width(),
                      (double)height_in / dbox->height());
        auto scaled_box = *dbox * Geom::Scale(sf);
        ibox = scaled_box.roundOutwards();
    }

    // Resize the contents to the available space with a scale factor.
    drawing->root()->setTransform(Geom::Scale(sf));
    drawing->update();

    auto pdim = Geom::IntPoint(width_in, height_in);
    // The unsigned width/height can wrap around when negative.
    int dx = ((int)width_in - ibox.width()) / 2;
    int dy = ((int)height_in - ibox.height()) / 2;
    auto area = Geom::IntRect::from_xywh(ibox.min() - Geom::IntPoint(dx, dy), pdim);

    /* Actual renderable area */
    Geom::IntRect ua = *Geom::intersect(ibox, area);

    auto [src, dst] = Async::Channel::create();
    drawing->snapshot();

    Async::fire_and_forget([ua, bg, drawing = std::move(drawing), onfinished = std::move(onfinished), src = std::move(src)] {

        auto start_time = g_get_monotonic_time();

        auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, ua.width(), ua.height());

        {
            auto cr = Cairo::Context::create(surface);
            cr->rectangle(0, 0, ua.width(), ua.height());

            // We always use checkerboard to indicate transparency.
            if (SP_RGBA32_A_F(bg) < 1.0) {
                auto pattern = ink_cairo_pattern_create_checkerboard(bg, false);
                auto background = Cairo::RefPtr<Cairo::Pattern>(new Cairo::Pattern(pattern, true));
                cr->set_source(background);
                cr->fill();
            }

            // We always draw the background on top to indicate partial backgrounds.
            cr->set_source_rgba(SP_RGBA32_R_F(bg), SP_RGBA32_G_F(bg), SP_RGBA32_B_F(bg), SP_RGBA32_A_F(bg));
            cr->fill();
        }

        {
            // Render drawing.
            auto dc = Inkscape::DrawingContext(surface->cobj(), ua.min());
            drawing->render(dc, ua);
        }

        surface->flush();

        int const elapsed_msecs = (g_get_monotonic_time() - start_time) / 1000;

        src.run([drawing = std::move(drawing), onfinished = std::move(onfinished), surface = std::move(surface), elapsed_msecs] {
            drawing->unsnapshot();
            onfinished(std::move(surface), elapsed_msecs);
        });
    });

    return std::move(dst);
}

} // namespace Preview
} // namespace UI
} // namespace Inkscape
