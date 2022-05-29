// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_UI_WIDGET_CANVAS_PREFS_H
#define INKSCAPE_UI_WIDGET_CANVAS_PREFS_H

#include "preferences.h"

namespace Inkscape {
namespace UI {
namespace Widget {

class Prefs
{
public:
    Prefs()
    {
        devmode.action = [this] { set_devmode(devmode); };
        devmode.action();
    }

    // Original parameters
    Pref<int>    tile_size                = { "/options/rendering/tile-size", 16, 1, 10000 };
    Pref<int>    tile_multiplier          = { "/options/rendering/tile-multiplier", 16, 1, 512 };
    Pref<int>    x_ray_radius             = { "/options/rendering/xray-radius", 100, 1, 1500 };
    Pref<bool>   from_display             = { "/options/displayprofile/from_display" };
    Pref<int>    grabsize                 = { "/options/grabsize/value", 3, 1, 15 };
    Pref<int>    outline_overlay_opacity  = { "/options/rendering/outline-overlay-opacity", 50, 1, 100 };

    // Things that require redraws (used by CMS system)
    Pref<void>   softproof                = { "/options/softproof" };
    Pref<void>   displayprofile           = { "/options/displayprofile" };

    // New parameters
    Pref<int>    update_strategy          = { "/options/rendering/update_strategy", 3, 1, 3 };
    Pref<int>    render_time_limit        = { "/options/rendering/render_time_limit", 1000, 100, 1000000 };
    Pref<bool>   use_new_bisector         = { "/options/rendering/use_new_bisector", true };
    Pref<int>    new_bisector_size        = { "/options/rendering/new_bisector_size", 500, 1, 10000 };
    Pref<int>    padding                  = { "/options/rendering/pad", 350, 0, 1000 };
    Pref<int>    prerender                = { "/options/rendering/margin", 100, 0, 1000 };
    Pref<int>    preempt                  = { "/options/rendering/preempt", 250, 0, 1000 };
    Pref<int>    coarsener_min_size       = { "/options/rendering/coarsener_min_size", 200, 0, 1000 };
    Pref<int>    coarsener_glue_size      = { "/options/rendering/coarsener_glue_size", 80, 0, 1000 };
    Pref<double> coarsener_min_fullness   = { "/options/rendering/coarsener_min_fullness", 0.3, 0.0, 1.0 };
    Pref<bool>   request_opengl           = { "/options/rendering/request_opengl" };
    Pref<int>    pixelstreamer_method     = { "/options/rendering/pixelstreamer_method", 1, 1, 4 };
    Pref<bool>   block_updates            = { "/options/rendering/block_updates", true };

    // Debug switches
    Pref<bool>   debug_framecheck         = { "/options/rendering/debug_framecheck" };
    Pref<bool>   debug_logging            = { "/options/rendering/debug_logging" };
    Pref<bool>   debug_slow_redraw        = { "/options/rendering/debug_slow_redraw" };
    Pref<int>    debug_slow_redraw_time   = { "/options/rendering/debug_slow_redraw_time", 50, 0, 1000000 };
    Pref<bool>   debug_show_redraw        = { "/options/rendering/debug_show_redraw" };
    Pref<bool>   debug_show_unclean       = { "/options/rendering/debug_show_unclean" };
    Pref<bool>   debug_show_snapshot      = { "/options/rendering/debug_show_snapshot" };
    Pref<bool>   debug_show_clean         = { "/options/rendering/debug_show_clean" };
    Pref<bool>   debug_disable_redraw     = { "/options/rendering/debug_disable_redraw" };
    Pref<bool>   debug_sticky_decoupled   = { "/options/rendering/debug_sticky_decoupled" };
    Pref<bool>   debug_animate            = { "/options/rendering/debug_animate" };
    Pref<bool>   debug_idle_starvation    = { "/options/rendering/debug_idle_starvation" };

private:
    // Developer mode
    Pref<bool> devmode = { "/options/rendering/devmode" };

    void set_devmode(bool on)
    {
        tile_size.set_enabled(on);
        render_time_limit.set_enabled(on);
        use_new_bisector.set_enabled(on);
        new_bisector_size.set_enabled(on);
        padding.set_enabled(on);
        prerender.set_enabled(on);
        preempt.set_enabled(on);
        coarsener_min_size.set_enabled(on);
        coarsener_glue_size.set_enabled(on);
        coarsener_min_fullness.set_enabled(on);
        pixelstreamer_method.set_enabled(on);
        debug_framecheck.set_enabled(on);
        debug_logging.set_enabled(on);
        debug_slow_redraw.set_enabled(on);
        debug_slow_redraw_time.set_enabled(on);
        debug_show_redraw.set_enabled(on);
        debug_show_unclean.set_enabled(on);
        debug_show_snapshot.set_enabled(on);
        debug_show_clean.set_enabled(on);
        debug_disable_redraw.set_enabled(on);
        debug_sticky_decoupled.set_enabled(on);
        debug_animate.set_enabled(on);
        debug_idle_starvation.set_enabled(on);
    }
};

} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_CANVAS_PREFS_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
