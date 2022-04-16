// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A class hierarchy used by the canvas for controlling what order to update invalidated regions.
 * Copyright (C) 2022 PBS <pbs3141@gmail.com>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UPDATERS_H
#define INKSCAPE_UPDATERS_H

#include <vector>
#include <memory>
#include <2geom/int-rect.h>
#include <cairomm/refptr.h>
#include <cairomm/region.h>

namespace Inkscape {

// A class for tracking invalidation events and producing redraw regions.
class Updater
{
public:
    virtual ~Updater() = default;

    // The subregion of the store with up-to-date content.
    Cairo::RefPtr<Cairo::Region> clean_region;

    enum class Strategy
    {
        Responsive, // As soon as a region is invalidated, redraw it.
        FullRedraw, // When a region is invalidated, delay redraw until after the current redraw is completed.
        Multiscale, // Updates tiles near the mouse faster. Gives the best of both.
    };

    // Create an Updater using the given strategy.
    template <Strategy strategy>
    static std::unique_ptr<Updater> create();

    // Create an Updater using a choice of strategy specified at runtime.
    static std::unique_ptr<Updater> create(Strategy strategy);

    // Return the strategy in use.
    virtual Strategy get_strategy() const = 0;

    virtual void reset() = 0;                          // Reset the clean region to empty.
    virtual void intersect (const Geom::IntRect&) = 0; // Called when the store changes position; clip everything to the new store rectangle.
    virtual void mark_dirty(const Geom::IntRect&) = 0; // Called on every invalidate event.
    virtual void mark_clean(const Geom::IntRect&) = 0; // Called on every rectangle redrawn.

    // Called by on_idle to determine what regions to consider clean for the current redraw.
    virtual Cairo::RefPtr<Cairo::Region> get_next_clean_region() = 0;

    // Called in on_idle if the redraw has finished. Returns true to indicate that further redraws are required with a different clean region.
    virtual bool report_finished() = 0;

    // Called by on_draw to notify the updater of the display of the frame.
    virtual void frame() = 0;
};

} // namespace Inkscape

#endif // INKSCAPE_UPDATERS_H
