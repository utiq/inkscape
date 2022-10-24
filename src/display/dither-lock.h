// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_DISPLAY_DITHER_LOCK_H
#define INKSCAPE_DISPLAY_DITHER_LOCK_H

#include "drawing-context.h"
#include "cairo-utils.h"

namespace Inkscape {

/// RAII object for temporarily turning dithering on.
class DitherLock
{
public:
    DitherLock(DrawingContext &dc, bool on)
        : _cr(dc.rawTarget())
        , _on(on)
    {
        if (_on) {
            ink_cairo_set_dither(_cr, true);
        }
    }

    ~DitherLock()
    {
        if (_on) {
            ink_cairo_set_dither(_cr, false);
        }
    }

private:
    cairo_surface_t *_cr;
    bool _on;
};

} // namespace Inkscape

#endif // INKSCAPE_DISPLAY_DITHER_LOCK_H
