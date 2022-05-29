// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Tavmjong Bah
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream> // Logging
#include <algorithm> // Sort
#include <set> // Coarsener
#include <array>
#include <2geom/convex-hull.h>
#include <epoxy/gl.h>

#include "canvas.h"
#include "canvas-grid.h"

#include "color.h"          // Background color
#include "cms-system.h"     // Color correction
#include "desktop.h"
#include "document.h"
#include "preferences.h"
#include "ui/util.h"
#include "helper/geom.h"

#include "display/drawing.h"
#include "display/cairo-utils.h"
#include "display/control/canvas-item-drawing.h"
#include "display/control/canvas-item-group.h"
#include "display/control/canvas-item-rect.h"
#include "display/control/snap-indicator.h"

#include "ui/tools/tool-base.h"      // Default cursor

#include "canvas/prefs.h"
#include "canvas/stores.h"
#include "canvas/updaters.h"
#include "canvas/graphics.h"
#include "canvas/util.h"
#include "canvas/framecheck.h"
#define framecheck_whole_function(D) \
    auto framecheckobj = D->prefs.debug_framecheck ? FrameCheck::Event(__func__) : FrameCheck::Event();

/*
 *   The canvas is responsible for rendering the SVG drawing with various "control"
 *   items below and on top of the drawing. Rendering is triggered by a call to one of:
 *
 *
 *   * redraw_all()     Redraws the entire canvas by calling redraw_area() with the canvas area.
 *
 *   * redraw_area()    Redraws the indicated area. Use when there is a change that doesn't affect
 *                      a CanvasItem's geometry or size.
 *
 *   * request_update() Redraws after recalculating bounds for changed CanvasItems. Use if a
 *                      CanvasItem's geometry or size has changed.
 *
 *   The first three functions add a request to the Gtk's "idle" list via
 *
 *   * add_idle()       Which causes Gtk to call when resources are available:
 *
 *   * on_idle()        Which sets up the backing stores, divides the area of the canvas that has been marked
 *                      unclean into rectangles that are small enough to render quickly, and renders them outwards
 *                      from the mouse with a call to:
 *
 *   * paint_rect_internal() Which paints the rectangle using paint_single_buffer(). It renders onto a Cairo
 *                           surface "backing_store". After a piece is rendered there is a call to:
 *
 *   * queue_draw_area() A Gtk function for marking areas of the window as needing a repaint, which when
 *                       the time is right calls:
 *
 *   * on_draw()        Which blits the Cairo surface to the screen.
 *
 *   The other responsibility of the canvas is to determine where to send GUI events. It does this
 *   by determining which CanvasItem is "picked" and then forwards the events to that item. Not all
 *   items can be picked. As a last resort, the "CatchAll" CanvasItem will be picked as it is the
 *   lowest CanvasItem in the stack (except for the "root" CanvasItem). With a small be of work, it
 *   should be possible to make the "root" CanvasItem a "CatchAll" eliminating the need for a
 *   dedicated "CatchAll" CanvasItem. There probably could be efficiency improvements as some
 *   items that are not pickable probably should be which would save having to effectively pick
 *   them "externally" (e.g. gradient CanvasItemCurves).
 */

namespace Inkscape {
namespace UI {
namespace Widget {
namespace {

/*
 * Utilities
 */

// GdkEvents can only be safely copied using gdk_event_copy. Since this function allocates, we need the following smart pointer to wrap the result.
struct GdkEventFreer {void operator()(GdkEvent *ev) const {gdk_event_free(ev);}};
using GdkEventUniqPtr = std::unique_ptr<GdkEvent, GdkEventFreer>;

// Copies a GdkEvent, returning the result as a smart pointer.
auto make_unique_copy(const GdkEvent *ev) {return GdkEventUniqPtr(gdk_event_copy(ev));}

auto pref_to_updater(int index)
{
    constexpr auto arr = std::array{Updater::Strategy::Responsive,
                                    Updater::Strategy::FullRedraw,
                                    Updater::Strategy::Multiscale};
    assert(1 <= index && index <= arr.size());
    return arr[index - 1];
}

} // namespace

/*
 * Implementation class
 */

class CanvasPrivate
{
public:
    friend class Canvas;
    Canvas *q;
    CanvasPrivate(Canvas *q)
        : q(q)
        , stores(prefs) {}

    // Lifecycle
    bool active = false;
    void activate();
    void deactivate();

    // Preferences
    Prefs prefs;

    // Stores
    Stores stores;
    void handle_stores_action(Stores::Action action);

    // Update strategy; tracks the unclean region and decides how to redraw it.
    std::unique_ptr<Updater> updater;

    // Graphics state; holds all the graphics resources, including the drawn content.
    std::unique_ptr<Graphics> graphics;
    void activate_graphics();
    void deactivate_graphics();

    // Event processor. Events that interact with the Canvas are buffered here until the start of the next frame. They are processed by a separate object so that deleting the Canvas mid-event can be done safely.
    struct EventProcessor
    {
        std::vector<GdkEventUniqPtr> events;
        int pos;
        GdkEvent *ignore = nullptr;
        CanvasPrivate *canvasprivate; // Nulled on destruction.
        bool in_processing = false; // For handling recursion due to nested GTK main loops.
        bool compression = true; // Whether event compression is enabled.
        void process();
        void compress();
        int gobble_key_events(guint keyval, guint mask);
        void gobble_motion_events(guint mask);
    };
    std::shared_ptr<EventProcessor> eventprocessor; // Usually held by CanvasPrivate, but temporarily also held by itself while processing so that it is not deleted mid-event.
    bool add_to_bucket(const GdkEvent*);
    bool process_bucketed_event(const GdkEvent*);
    bool pick_current_item(const GdkEvent*);
    bool emit_event(const GdkEvent*);
    Inkscape::CanvasItem *pre_scroll_grabbed_item;

    // State for determining when to run event processor.
    bool pending_draw = false;
    sigc::connection bucket_emptier;
    std::optional<guint> bucket_emptier_tick_callback;
    void schedule_bucket_emptier();
    void disconnect_bucket_emptier_tick_callback();

    // Idle system. The high priority idle ensures at least one idle cycle between add_idle and on_draw.
    void add_idle();
    sigc::connection hipri_idle;
    sigc::connection lopri_idle;
    bool on_hipri_idle();
    bool on_lopri_idle();
    bool idle_running = false;

    // Content drawing
    bool on_idle();
    void paint_rect(Geom::IntRect const &rect);
    void paint_single_buffer(const Cairo::RefPtr<Cairo::ImageSurface> &surface, const Geom::IntRect &rect, bool need_background, bool outline_pass);
    std::optional<Geom::Dim2> old_bisector(const Geom::IntRect &rect);
    std::optional<Geom::Dim2> new_bisector(const Geom::IntRect &rect);
    bool outlines_required() const { return q->_split_mode != Inkscape::SplitMode::NORMAL || q->_render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY; }

    uint32_t desk   = 0xffffffff; // The background colour, with the alpha channel used to control checkerboard.
    uint32_t border = 0x000000ff; // The border colour, used only to control shadow colour.
    uint32_t page   = 0xffffffff; // The page colour, also with alpha channel used to control checkerboard.
    bool clip_to_page = false; // Whether to enable clip-to-page mode.

    bool outlines_enabled = false;
    int scale_factor = 1; // The device scale the stores are drawn at.
    Geom::Affine geom_affine; // The affine the geometry was last imbued with.
    PageInfo pi;

    bool background_in_stores = false;
    bool require_background_in_stores() const { return !q->get_opengl_enabled() && SP_RGBA32_A_U(page) == 255 && SP_RGBA32_A_U(desk) == 255; } // Enable solid colour optimisation if both page and desk are solid (as opposed to checkerboard).

    // Trivial overload of GtkWidget function.
    void queue_draw_area(const Geom::IntRect &rect);

    // For tracking the last known mouse position. (The function Gdk::Window::get_device_position cannot be used because of slow X11 round-trips. Remove this workaround when X11 dies.)
    std::optional<Geom::IntPoint> last_mouse;

    // Idle time starvation counter.
    gint64 sample_begin = 0;
    gint64 wait_begin = 0;
    gint64 wait_accumulated = 0;
};

/*
 * Lifecycle
 */

Canvas::Canvas()
    : d(std::make_unique<CanvasPrivate>(this))
{
    set_name("InkscapeCanvas");

    // Events
    add_events(Gdk::BUTTON_PRESS_MASK   |
               Gdk::BUTTON_RELEASE_MASK |
               Gdk::ENTER_NOTIFY_MASK   |
               Gdk::LEAVE_NOTIFY_MASK   |
               Gdk::FOCUS_CHANGE_MASK   |
               Gdk::KEY_PRESS_MASK      |
               Gdk::KEY_RELEASE_MASK    |
               Gdk::POINTER_MOTION_MASK |
               Gdk::SCROLL_MASK         |
               Gdk::SMOOTH_SCROLL_MASK  );

    // Set up EventProcessor
    d->eventprocessor = std::make_shared<CanvasPrivate::EventProcessor>();
    d->eventprocessor->canvasprivate = d.get();

    // Updater
    d->updater = Updater::create(pref_to_updater(d->prefs.update_strategy));
    d->updater->reset();

    // Preferences
    d->prefs.grabsize.action = [=] { _canvas_item_root->update_canvas_item_ctrl_sizes(d->prefs.grabsize); };
    d->prefs.debug_show_unclean.action = [=] { queue_draw(); };
    d->prefs.debug_show_clean.action = [=] { queue_draw(); };
    d->prefs.debug_disable_redraw.action = [=] { d->add_idle(); };
    d->prefs.debug_sticky_decoupled.action = [=] { d->add_idle(); };
    d->prefs.debug_animate.action = [=] { queue_draw(); };
    d->prefs.update_strategy.action = [=] {
        auto new_updater = Updater::create(pref_to_updater(d->prefs.update_strategy));
        new_updater->clean_region = std::move(d->updater->clean_region);
        d->updater = std::move(new_updater);
    };
    d->prefs.outline_overlay_opacity.action = [=] { queue_draw(); };
    d->prefs.softproof.action = [=] { redraw_all(); };
    d->prefs.displayprofile.action = [=] { redraw_all(); };
    d->prefs.request_opengl.action = [=] {
        if (get_realized()) {
            d->deactivate();
            d->deactivate_graphics();
            set_opengl_enabled(d->prefs.request_opengl);
            d->updater->reset();
            d->activate_graphics();
            d->activate();
        }
    };
    d->prefs.pixelstreamer_method.action = [=] {
        if (get_realized() && get_opengl_enabled()) {
            d->deactivate();
            d->deactivate_graphics();
            d->activate_graphics();
            d->activate();
        }
    };
    d->prefs.debug_idle_starvation.action = [=] { d->sample_begin = d->wait_begin = d->wait_accumulated = 0; };

    // Cavas item root
    _canvas_item_root = new Inkscape::CanvasItemGroup(nullptr);
    _canvas_item_root->set_name("CanvasItemGroup:Root");
    _canvas_item_root->set_canvas(this);

    // Split view.
    _split_direction = Inkscape::SplitDirection::EAST;
    _split_frac = {0.5, 0.5};

    // Recreate stores on HiDPI change.
    property_scale_factor().signal_changed().connect([this] { d->add_idle(); });

    // OpenGL switch.
    set_opengl_enabled(d->prefs.request_opengl);
}

// Graphics becomes active when the widget is realized.
void CanvasPrivate::activate_graphics()
{
    if (q->get_opengl_enabled()) {
        q->make_current();
        graphics = Graphics::create_gl(prefs, stores, pi);
    } else {
        graphics = Graphics::create_cairo(prefs, stores, pi);
    }
    stores.set_graphics(graphics.get());
    stores.reset();
}

// After graphics becomes active, the canvas becomes active when additionally a drawing is set.
void CanvasPrivate::activate()
{
    // Event handling/item picking
    q->_pick_event.type = GDK_LEAVE_NOTIFY;
    q->_pick_event.crossing.x = 0;
    q->_pick_event.crossing.y = 0;

    q->_in_repick         = false;
    q->_left_grabbed_item = false;
    q->_all_enter_events  = false;
    q->_is_dragging       = false;
    q->_state             = 0;

    q->_current_canvas_item     = nullptr;
    q->_current_canvas_item_new = nullptr;
    q->_grabbed_canvas_item     = nullptr;
    q->_grabbed_event_mask = (Gdk::EventMask)0;
    pre_scroll_grabbed_item = nullptr;

    // Drawing
    q->_drawing_disabled = false;
    q->_need_update = true;

    // Split view
    q->_split_dragging = false;

    // Todo: Disable GTK event compression again when doing so is no longer buggy.
    q->get_window()->set_event_compression(true);

    active = true;

    add_idle();
}

void CanvasPrivate::deactivate()
{
    active = false;

    // Disconnect signals and timeouts. (Note: They will never be rescheduled while inactive.)
    hipri_idle.disconnect();
    lopri_idle.disconnect();
    bucket_emptier.disconnect();
    disconnect_bucket_emptier_tick_callback();
}

void CanvasPrivate::deactivate_graphics()
{
    stores.set_graphics(nullptr);
    if (q->get_opengl_enabled()) q->make_current();
    graphics.reset();
}

Canvas::~Canvas()
{
    // Disconnect from EventProcessor.
    d->eventprocessor->canvasprivate = nullptr;

    // Remove entire CanvasItem tree.
    delete _canvas_item_root;
}

void Canvas::set_drawing(Drawing *drawing)
{
    if (d->active && !drawing) d->deactivate();
    _drawing = drawing;
    if (_drawing) {
        _drawing->setRenderMode(_render_mode == RenderMode::OUTLINE_OVERLAY ? RenderMode::NORMAL : _render_mode);
        _drawing->setColorMode(_color_mode);
        _drawing->setOutlineOverlay(d->outlines_required());
    }
    if (!d->active && get_realized() && drawing) d->activate();
}

void Canvas::on_realize()
{
    parent_type::on_realize();
    d->activate_graphics();
    if (_drawing) d->activate();
}

void Canvas::on_unrealize()
{
    if (_drawing) d->deactivate();
    d->deactivate_graphics();
    parent_type::on_unrealize();
}

/*
 * Events system
 */

// The following protected functions of Canvas are where all incoming events initially arrive.
// Those that do not interact with the Canvas are processed instantaneously, while the rest are
// delayed by placing them into the bucket.

bool Canvas::on_scroll_event(GdkEventScroll *scroll_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(scroll_event));
}

bool Canvas::on_button_press_event(GdkEventButton *button_event)
{
    return on_button_event(button_event);
}

bool Canvas::on_button_release_event(GdkEventButton *button_event)
{
    return on_button_event(button_event);
}

// Unified handler for press and release events.
bool Canvas::on_button_event(GdkEventButton *button_event)
{
    // Sanity-check event type.
    switch (button_event->type) {
        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            break; // Good
        default:
            std::cerr << "Canvas::on_button_event: illegal event type!" << std::endl;
            return false;
    }

    // Drag the split view controller.
    if (_split_mode == Inkscape::SplitMode::SPLIT) {
        auto cursor_position = Geom::IntPoint(button_event->x, button_event->y);
        switch (button_event->type) {
            case GDK_BUTTON_PRESS:
                if (_hover_direction != Inkscape::SplitDirection::NONE) {
                    _split_dragging = true;
                    _split_drag_start = cursor_position;
                    return true;
                }
                break;
            case GDK_2BUTTON_PRESS:
                if (_hover_direction != Inkscape::SplitDirection::NONE) {
                    _split_direction = _hover_direction;
                    _split_dragging = false;
                    queue_draw();
                    return true;
                }
                break;
            case GDK_BUTTON_RELEASE:
                _split_dragging = false;

                // Check if we are near the edge. If so, revert to normal mode.
                if (cursor_position.x() < 5                                  ||
                    cursor_position.y() < 5                                  ||
                    cursor_position.x() - get_allocation().get_width()  > -5 ||
                    cursor_position.y() - get_allocation().get_height() > -5 ) {

                    // Reset everything.
                    _split_frac = {0.5, 0.5};
                    set_cursor();
                    set_split_mode(Inkscape::SplitMode::NORMAL);

                    // Update action (turn into utility function?).
                    auto window = dynamic_cast<Gtk::ApplicationWindow*>(get_toplevel());
                    if (!window) {
                        std::cerr << "Canvas::on_motion_notify_event: window missing!" << std::endl;
                        return true;
                    }

                    auto action = window->lookup_action("canvas-split-mode");
                    if (!action) {
                        std::cerr << "Canvas::on_motion_notify_event: action 'canvas-split-mode' missing!" << std::endl;
                        return true;
                    }

                    auto saction = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(action);
                    if (!saction) {
                        std::cerr << "Canvas::on_motion_notify_event: action 'canvas-split-mode' not SimpleAction!" << std::endl;
                        return true;
                    }

                    saction->change_state((int)Inkscape::SplitMode::NORMAL);
                }

                break;

            default:
                break;
        }
    }

    // Otherwise, handle as a delayed event.
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(button_event));
}

bool Canvas::on_enter_notify_event(GdkEventCrossing *crossing_event)
{
    if (crossing_event->window != get_window()->gobj()) {
        return false;
    }
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(crossing_event));
}

bool Canvas::on_leave_notify_event(GdkEventCrossing *crossing_event)
{
    if (crossing_event->window != get_window()->gobj()) {
        return false;
    }
    d->last_mouse = {};
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(crossing_event));
}

bool Canvas::on_focus_in_event(GdkEventFocus *focus_event)
{
    grab_focus();
    return false;
}

bool Canvas::on_key_press_event(GdkEventKey *key_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(key_event));
}

bool Canvas::on_key_release_event(GdkEventKey *key_event)
{
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(key_event));
}

bool Canvas::on_motion_notify_event(GdkEventMotion *motion_event)
{
    // Record the last mouse position.
    d->last_mouse = Geom::IntPoint(motion_event->x, motion_event->y);

    // Handle interactions with the split view controller.
    if (_split_mode == Inkscape::SplitMode::XRAY) {
        queue_draw();
    } else if (_split_mode == Inkscape::SplitMode::SPLIT) {
        auto cursor_position = Geom::IntPoint(motion_event->x, motion_event->y);

        // Move controller.
        if (_split_dragging) {
            auto delta = cursor_position - _split_drag_start;
            if (_hover_direction == Inkscape::SplitDirection::HORIZONTAL) {
                delta.x() = 0;
            } else if (_hover_direction == Inkscape::SplitDirection::VERTICAL) {
                delta.y() = 0;
            }
            _split_frac += Geom::Point(delta) / get_dimensions();
            _split_drag_start = cursor_position;
            queue_draw();
            return true;
        }

        auto split_position = (_split_frac * get_dimensions()).round();
        auto diff = cursor_position - split_position;
        auto hover_direction = Inkscape::SplitDirection::NONE;
        if (Geom::Point(diff).length() < 20.0) {
            // We're hovering over circle, figure out which direction we are in.
            if (diff.y() - diff.x() > 0) {
                if (diff.y() + diff.x() > 0) {
                    hover_direction = Inkscape::SplitDirection::SOUTH;
                } else {
                    hover_direction = Inkscape::SplitDirection::WEST;
                }
            } else {
                if (diff.y() + diff.x() > 0) {
                    hover_direction = Inkscape::SplitDirection::EAST;
                } else {
                    hover_direction = Inkscape::SplitDirection::NORTH;
                }
            }
        } else if (_split_direction == Inkscape::SplitDirection::NORTH ||
                   _split_direction == Inkscape::SplitDirection::SOUTH) {
            if (std::abs(diff.y()) < 3) {
                // We're hovering over horizontal line
                hover_direction = Inkscape::SplitDirection::HORIZONTAL;
            }
        } else {
            if (std::abs(diff.x()) < 3) {
                // We're hovering over vertical line
                hover_direction = Inkscape::SplitDirection::VERTICAL;
            }
        }

        if (_hover_direction != hover_direction) {
            _hover_direction = hover_direction;
            set_cursor();
            queue_draw();
        }

        if (_hover_direction != Inkscape::SplitDirection::NONE) {
            // We're hovering, don't pick or emit event.
            return true;
        }
    }

    // Otherwise, handle as a delayed event.
    return d->add_to_bucket(reinterpret_cast<GdkEvent*>(motion_event));
}

// Most events end up here. We store them in the bucket, and process them as soon as possible after
// the next 'on_draw'. If 'on_draw' isn't pending, we use the 'tick_callback' signal to process them
// when 'on_draw' would have run anyway. If 'on_draw' later becomes pending, we remove this signal.

// Add an event to the bucket and ensure it will be emptied in the near future.
bool CanvasPrivate::add_to_bucket(const GdkEvent *event)
{
    framecheck_whole_function(this)

    if (!active) {
        std::cerr << "Canvas::add_to_bucket: Called while not active!" << std::endl;
        return false;
    }

    // Prevent re-fired events from going through again.
    if (event == eventprocessor->ignore) {
        return false;
    }

    // If this is the first event, ensure event processing will run on the main loop as soon as possible after the next frame has started.
    if (eventprocessor->events.empty() && !pending_draw) {
        assert(!bucket_emptier_tick_callback); // Guaranteed since cleared when the event queue is emptied and not set until non-empty again.
        bucket_emptier_tick_callback = q->add_tick_callback([this] (const Glib::RefPtr<Gdk::FrameClock>&) {
            assert(active); // Guaranteed since disconnected upon becoming inactive and not scheduled until active again.
            bucket_emptier_tick_callback.reset();
            schedule_bucket_emptier();
            return false;
        });
    }

    // Add a copy to the queue.
    eventprocessor->events.emplace_back(gdk_event_copy(event));

    // Tell GTK the event was handled.
    return true;
}

void CanvasPrivate::schedule_bucket_emptier()
{
    if (!active) {
        std::cerr << "Canvas::schedule_bucket_emptier: Called while not active!" << std::endl;
        return;
    }

    if (!bucket_emptier.connected()) {
        bucket_emptier = Glib::signal_idle().connect([this] {
            assert(active);
            eventprocessor->process();
            return false;
        }, G_PRIORITY_HIGH_IDLE + 14); // before hipri_idle
    }
}

void CanvasPrivate::disconnect_bucket_emptier_tick_callback()
{
    if (bucket_emptier_tick_callback) {
        q->remove_tick_callback(*bucket_emptier_tick_callback);
        bucket_emptier_tick_callback.reset();
    }
}

// The following functions run at the start of the next frame on the GTK main loop.
// (Note: It is crucial that it runs on the main loop and not in any frame clock tick callbacks. GTK does not allow widgets to be deleted in the latter; only the former.)

// Process bucketed events.
void CanvasPrivate::EventProcessor::process()
{
    framecheck_whole_function(canvasprivate)

    // Ensure the EventProcessor continues to live even if the Canvas is destroyed during event processing.
    auto self = canvasprivate->eventprocessor;

    // Check if toplevel or recursive. (Recursive calls happen if processing an event starts its own nested GTK main loop.)
    bool toplevel = !in_processing;
    in_processing = true;

    // If toplevel, run compression, and initialise the iteration index. It may be incremented externally by gobblers or recursive calls.
    if (toplevel) {
        if (compression) compress();
        pos = 0;
    }

    while (pos < events.size()) {
        // Extract next event.
        auto event = std::move(events[pos]);
        pos++;

        // Fire the event at the CanvasItems and see if it was handled.
        bool handled = canvasprivate->process_bucketed_event(event.get());

        if (!handled) {
            // Re-fire the event at the window, and ignore it when it comes back here again.
            ignore = event.get();
            canvasprivate->q->get_toplevel()->event(event.get());
            ignore = nullptr;
        }

        // If the Canvas was destroyed or deactivated during event processing, exit now.
        if (!canvasprivate || !canvasprivate->active) return;
    }

    // Otherwise, clear the list of events that was just processed.
    events.clear();

    // Disconnect the bucket emptier tick callback, as no longer anything to empty.
    canvasprivate->disconnect_bucket_emptier_tick_callback();

    // Reset the variable to track recursive calls.
    if (toplevel) {
        in_processing = false;
    }
}

// Called before event processing starts to perform event compression.
void CanvasPrivate::EventProcessor::compress()
{
    int in = 0, out = 0;

    while (in < events.size()) {
        // Compress motion events belonging to the same device.
        if (events[in]->type == GDK_MOTION_NOTIFY) {
            auto begin = in, end = in + 1;
            while (end < events.size() && events[end]->type == GDK_MOTION_NOTIFY && events[end]->motion.device == events[begin]->motion.device) end++;
            // Check if there is more than one event to compress.
            if (end != begin + 1) {
                // Keep only the last event.
                events[out] = std::move(events[end - 1]);
                in = end;
                out++;
                continue;
            }
        }

        // Todo: Could consider compressing other events too (e.g. scrolls) if it helps.

        // Otherwise, leave the event untouched.
        if (in != out) events[out] = std::move(events[in]);
        in++;
        out++;
    }

    events.resize(out);
}

void Canvas::set_event_compression(bool enabled)
{
    d->eventprocessor->compression = enabled;
}

// Called during event processing by some tools to batch backlogs of key events that may have built up after a freeze.
int Canvas::gobble_key_events(guint keyval, guint mask)
{
    return d->eventprocessor->gobble_key_events(keyval, mask);
}

int CanvasPrivate::EventProcessor::gobble_key_events(guint keyval, guint mask)
{
    int count = 0;

    while (pos < events.size()) {
        auto &event = events[pos];
        if ((event->type == GDK_KEY_PRESS || event->type == GDK_KEY_RELEASE) && event->key.keyval == keyval && (!mask || (event->key.state & mask))) {
            // Discard event and continue.
            if (event->type == GDK_KEY_PRESS) count++;
            pos++;
        } else {
            // Stop discarding.
            break;
        }
    }

    if (count > 0 && canvasprivate->prefs.debug_logging) std::cout << "Gobbled " << count << " key press(es)" << std::endl;

    return count;
}

// Called during event processing by some tools to ignore backlogs of motion events that may have built up after a freeze.
// Todo: Largely obviated since the introduction of event compression. May be possible to remove.
void Canvas::gobble_motion_events(guint mask)
{
    d->eventprocessor->gobble_motion_events(mask);
}

void CanvasPrivate::EventProcessor::gobble_motion_events(guint mask)
{
    int count = 0;

    while (pos < events.size()) {
        auto &event = events[pos];
        if (event->type == GDK_MOTION_NOTIFY && (event->motion.state & mask)) {
            // Discard event and continue.
            count++;
            pos++;
        } else {
            // Stop discarding.
            break;
        }
    }

    if (count > 0 && canvasprivate->prefs.debug_logging) std::cout << "Gobbled " << count << " motion event(s)" << std::endl;
}

// From now on Inkscape's regular event processing logic takes place. The only thing to remember is that
// all of this happens at a slight delay after the original GTK events. Therefore, it's important to make
// sure that stateful variables like '_current_canvas_item' and friends are ONLY read/written within these
// functions, not during the earlier GTK event handlers. Otherwise state confusion will ensue.

bool CanvasPrivate::process_bucketed_event(const GdkEvent *event)
{
    auto calc_button_mask = [&] () -> int {
        switch (event->button.button) {
            case 1:  return GDK_BUTTON1_MASK; break;
            case 2:  return GDK_BUTTON2_MASK; break;
            case 3:  return GDK_BUTTON3_MASK; break;
            case 4:  return GDK_BUTTON4_MASK; break;
            case 5:  return GDK_BUTTON5_MASK; break;
            default: return 0; // Buttons can range at least to 9 but mask defined only to 5.
        }
    };

    // Do event-specific processing.
    switch (event->type) {
        case GDK_SCROLL:
        {
            // Save the current event-receiving item just before scrolling starts. It will continue to receive scroll events until the mouse is moved.
            if (!pre_scroll_grabbed_item) {
                pre_scroll_grabbed_item = q->_current_canvas_item;
                if (q->_grabbed_canvas_item && !q->_current_canvas_item->is_descendant_of(q->_grabbed_canvas_item)) {
                    pre_scroll_grabbed_item = q->_grabbed_canvas_item;
                }
            }

            // Process the scroll event...
            bool retval = emit_event(event);

            // ...then repick.
            q->_state = event->scroll.state;
            pick_current_item(event);

            return retval;
        }

        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        {
            pre_scroll_grabbed_item = nullptr;

            // Pick the current item as if the button were not pressed...
            q->_state = event->button.state;
            pick_current_item(event);

            // ...then process the event.
            q->_state ^= calc_button_mask();
            bool retval = emit_event(event);

            return retval;
        }

        case GDK_BUTTON_RELEASE:
        {
            pre_scroll_grabbed_item = nullptr;

            // Process the event as if the button were pressed...
            q->_state = event->button.state;
            bool retval = emit_event(event);

            // ...then repick after the button has been released.
            auto event_copy = make_unique_copy(event);
            event_copy->button.state ^= calc_button_mask();
            q->_state = event_copy->button.state;
            pick_current_item(event_copy.get());

            return retval;
        }

        case GDK_ENTER_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->crossing.state;
            return pick_current_item(event);

        case GDK_LEAVE_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->crossing.state;
            // This is needed to remove alignment or distribution snap indicators.
            if (q->_desktop) {
                q->_desktop->snapindicator->remove_snaptarget();
            }
            return pick_current_item(event);

        case GDK_KEY_PRESS:
        case GDK_KEY_RELEASE:
            return emit_event(event);

        case GDK_MOTION_NOTIFY:
            pre_scroll_grabbed_item = nullptr;
            q->_state = event->motion.state;
            pick_current_item(event);
            return emit_event(event);

        default:
            return false;
    }
}

// This function is called by 'process_bucketed_event' to manipulate the state variables relating
// to the current object under the mouse, for example, to generate enter and leave events.
// (A more detailed explanation by Tavmjong follows.)
// --------
// This routine reacts to events from the canvas. It's main purpose is to find the canvas item
// closest to the cursor where the event occurred and then send the event (sometimes modified) to
// that item. The event then bubbles up the canvas item tree until an object handles it. If the
// widget is redrawn, this routine may be called again for the same event.
//
// Canvas items register their interest by connecting to the "event" signal.
// Example in desktop.cpp:
//   canvas_catchall->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), this));
bool CanvasPrivate::pick_current_item(const GdkEvent *event)
{
    // Ensure requested geometry updates are performed first.
    if (q->_need_update) {
        q->_canvas_item_root->update(geom_affine);
        q->_need_update = false;
    }

    int button_down = 0;
    if (!q->_all_enter_events) {
        // Only set true in connector-tool.cpp.

        // If a button is down, we'll perform enter and leave events on the
        // current item, but not enter on any other item.  This is more or
        // less like X pointer grabbing for canvas items.
        button_down = q->_state & (GDK_BUTTON1_MASK |
                                   GDK_BUTTON2_MASK |
                                   GDK_BUTTON3_MASK |
                                   GDK_BUTTON4_MASK |
                                   GDK_BUTTON5_MASK);
        if (!button_down) q->_left_grabbed_item = false;
    }

    // Save the event in the canvas.  This is used to synthesize enter and
    // leave events in case the current item changes.  It is also used to
    // re-pick the current item if the current one gets deleted.  Also,
    // synthesize an enter event.
    if (event != &q->_pick_event) {
        if (event->type == GDK_MOTION_NOTIFY || event->type == GDK_SCROLL || event->type == GDK_BUTTON_RELEASE) {
            // Convert to GDK_ENTER_NOTIFY

            // These fields have the same offsets in all types of events.
            q->_pick_event.crossing.type       = GDK_ENTER_NOTIFY;
            q->_pick_event.crossing.window     = event->motion.window;
            q->_pick_event.crossing.send_event = event->motion.send_event;
            q->_pick_event.crossing.subwindow  = nullptr;
            q->_pick_event.crossing.x          = event->motion.x;
            q->_pick_event.crossing.y          = event->motion.y;
            q->_pick_event.crossing.mode       = GDK_CROSSING_NORMAL;
            q->_pick_event.crossing.detail     = GDK_NOTIFY_NONLINEAR;
            q->_pick_event.crossing.focus      = false;

            // These fields don't have the same offsets in all types of events.
            switch (event->type)
            {
                case GDK_MOTION_NOTIFY:
                    q->_pick_event.crossing.state  = event->motion.state;
                    q->_pick_event.crossing.x_root = event->motion.x_root;
                    q->_pick_event.crossing.y_root = event->motion.y_root;
                    break;
                case GDK_SCROLL:
                    q->_pick_event.crossing.state  = event->scroll.state;
                    q->_pick_event.crossing.x_root = event->scroll.x_root;
                    q->_pick_event.crossing.y_root = event->scroll.y_root;
                    break;
                case GDK_BUTTON_RELEASE:
                    q->_pick_event.crossing.state  = event->button.state;
                    q->_pick_event.crossing.x_root = event->button.x_root;
                    q->_pick_event.crossing.y_root = event->button.y_root;
                    break;
                default:
                    assert(false);
            }

        } else {
            q->_pick_event = *event;
        }
    }

    if (q->_in_repick) {
        // Don't do anything else if this is a recursive call.
        return false;
    }

    // Find new item
    q->_current_canvas_item_new = nullptr;

    if (q->_pick_event.type != GDK_LEAVE_NOTIFY && q->_canvas_item_root->is_visible()) {
        // Leave notify means there is no current item.
        // Find closest item.
        double x = 0.0;
        double y = 0.0;

        if (q->_pick_event.type == GDK_ENTER_NOTIFY) {
            x = q->_pick_event.crossing.x;
            y = q->_pick_event.crossing.y;
        } else {
            x = q->_pick_event.motion.x;
            y = q->_pick_event.motion.y;
        }

        // If in split mode, look at where cursor is to see if one should pick with outline mode.
        bool outline;
        if (q->_render_mode == Inkscape::RenderMode::OUTLINE || q->_render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) {
            outline = true;
        } else if (q->_split_mode == Inkscape::SplitMode::SPLIT) {
            auto split_position = q->_split_frac * q->get_dimensions();
            outline = (q->_split_direction == Inkscape::SplitDirection::NORTH && y > split_position.y())
                   || (q->_split_direction == Inkscape::SplitDirection::SOUTH && y < split_position.y())
                   || (q->_split_direction == Inkscape::SplitDirection::WEST  && x > split_position.x())
                   || (q->_split_direction == Inkscape::SplitDirection::EAST  && x < split_position.x());
        } else {
            outline = false;
        }

        // Convert to world coordinates.
        auto p = Geom::Point(x, y) + q->_pos;
        if (stores.mode() == Stores::Mode::Decoupled) {
            p *= q->_affine.inverse() * geom_affine;
        }

        q->_drawing->getCanvasItemDrawing()->set_pick_outline(outline);
        q->_current_canvas_item_new = q->_canvas_item_root->pick_item(p);
        // if (q->_current_canvas_item_new) {
        //     std::cout << "  PICKING: FOUND ITEM: " << q->_current_canvas_item_new->get_name() << std::endl;
        // } else {
        //     std::cout << "  PICKING: DID NOT FIND ITEM" << std::endl;
        // }
    }

    if (q->_current_canvas_item_new == q->_current_canvas_item && !q->_left_grabbed_item) {
        // Current item did not change!
        return false;
    }

    // Synthesize events for old and new current items.
    bool retval = false;
    if (q->_current_canvas_item_new != q->_current_canvas_item &&
        q->_current_canvas_item != nullptr                     &&
        !q->_left_grabbed_item                                 ) {

        GdkEvent new_event;
        new_event = q->_pick_event;
        new_event.type = GDK_LEAVE_NOTIFY;
        new_event.crossing.detail = GDK_NOTIFY_ANCESTOR;
        new_event.crossing.subwindow = nullptr;
        q->_in_repick = true;
        retval = emit_event(&new_event);
        q->_in_repick = false;
    }

    if (q->_all_enter_events == false) {
        // new_current_item may have been set to nullptr during the call to emitEvent() above.
        if (q->_current_canvas_item_new != q->_current_canvas_item && button_down) {
            q->_left_grabbed_item = true;
            return retval;
        }
    }

    // Handle the rest of cases
    q->_left_grabbed_item = false;
    q->_current_canvas_item = q->_current_canvas_item_new;

    if (q->_current_canvas_item != nullptr) {
        GdkEvent new_event;
        new_event = q->_pick_event;
        new_event.type = GDK_ENTER_NOTIFY;
        new_event.crossing.detail = GDK_NOTIFY_ANCESTOR;
        new_event.crossing.subwindow = nullptr;
        retval = emit_event(&new_event);
    }

    return retval;
}

// Fires an event at the canvas, after a little pre-processing. Returns true if handled.
bool CanvasPrivate::emit_event(const GdkEvent *event)
{
    // Handle grabbed items.
    if (q->_grabbed_canvas_item) {
        auto mask = (Gdk::EventMask)0;

        switch (event->type) {
            case GDK_ENTER_NOTIFY:
                mask = Gdk::ENTER_NOTIFY_MASK;
                break;
            case GDK_LEAVE_NOTIFY:
                mask = Gdk::LEAVE_NOTIFY_MASK;
                break;
            case GDK_MOTION_NOTIFY:
                mask = Gdk::POINTER_MOTION_MASK;
                break;
            case GDK_BUTTON_PRESS:
            case GDK_2BUTTON_PRESS:
            case GDK_3BUTTON_PRESS:
                mask = Gdk::BUTTON_PRESS_MASK;
                break;
            case GDK_BUTTON_RELEASE:
                mask = Gdk::BUTTON_RELEASE_MASK;
                break;
            case GDK_KEY_PRESS:
                mask = Gdk::KEY_PRESS_MASK;
                break;
            case GDK_KEY_RELEASE:
                mask = Gdk::KEY_RELEASE_MASK;
                break;
            case GDK_SCROLL:
                mask = Gdk::SCROLL_MASK;
                mask |= Gdk::SMOOTH_SCROLL_MASK;
                break;
            default:
                break;
        }

        if (!(mask & q->_grabbed_event_mask)) {
            return false;
        }
    }

    // Convert to world coordinates. We have two different cases due to different event structures.
    auto conv = [&, this] (double &x, double &y) {
        auto p = Geom::Point(x, y) + q->_pos;
        if (stores.mode() == Stores::Mode::Decoupled) {
            p *= q->_affine.inverse() * geom_affine;
        }
        x = p.x();
        y = p.y();
    };

    auto event_copy = make_unique_copy(event);

    switch (event->type) {
        case GDK_ENTER_NOTIFY:
        case GDK_LEAVE_NOTIFY:
            conv(event_copy->crossing.x, event_copy->crossing.y);
            break;
        case GDK_MOTION_NOTIFY:
        case GDK_BUTTON_PRESS:
        case GDK_2BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            conv(event_copy->motion.x, event_copy->motion.y);
            break;
        default:
            break;
    }

    // Block undo/redo while anything is dragged.
    if (event->type == GDK_BUTTON_PRESS && event->button.button == 1) {
        q->_is_dragging = true;
    } else if (event->type == GDK_BUTTON_RELEASE) {
        q->_is_dragging = false;
    }

    if (q->_current_canvas_item) {
        // Choose where to send event.
        auto item = q->_current_canvas_item;

        if (q->_grabbed_canvas_item && !q->_current_canvas_item->is_descendant_of(q->_grabbed_canvas_item)) {
            item = q->_grabbed_canvas_item;
        }

        if (pre_scroll_grabbed_item && event->type == GDK_SCROLL) {
            item = pre_scroll_grabbed_item;
        }

        // Propagate the event up the canvas item hierarchy until handled.
        while (item) {
            if (item->handle_event(event_copy.get())) return true;
            item = item->get_parent();
        }
    }

    return false;
}

/*
 * Protected functions
 */

Geom::IntPoint Canvas::get_dimensions() const
{
    return dimensions(get_allocation());
}

/**
 * Is world point inside canvas area?
 */
bool Canvas::world_point_inside_canvas(Geom::Point const &world) const
{
    return get_area_world().contains(world.floor());
}

/**
 * Translate point in canvas to world coordinates.
 */
Geom::Point Canvas::canvas_to_world(Geom::Point const &point) const
{
    return point + _pos;
}

/**
 * Return the area shown in the canvas in world coordinates.
 */
Geom::IntRect Canvas::get_area_world() const
{
    return Geom::IntRect(_pos, _pos + get_dimensions());
}

/**
 * Return the last known mouse position of center if off-canvas.
 */
std::optional<Geom::Point> Canvas::get_last_mouse() const
{
    return d->last_mouse;
}

/**
 * Set the affine for the canvas.
 */
void Canvas::set_affine(Geom::Affine const &affine)
{
    if (_affine == affine) {
        return;
    }

    _affine = affine;

    d->add_idle();
    queue_draw();
}

const Geom::Affine &Canvas::get_geom_affine() const
{
    return d->geom_affine;
}

void CanvasPrivate::queue_draw_area(const Geom::IntRect &rect)
{
    if (q->get_opengl_enabled()) {
        // Note: GTK glitches out when you use queue_draw_area in OpenGL mode.
        // Also, does GTK actually obey this command, or redraw the whole window?
        q->queue_draw();
    } else {
        q->queue_draw_area(rect.left(), rect.top(), rect.width(), rect.height());
    }
}

/**
 * Invalidate drawing and redraw during idle.
 */
void Canvas::redraw_all()
{
    if (!d->active) {
        // CanvasItems redraw their area when being deleted... which happens when the Canvas is destroyed.
        // We need to ignore their requests!
        return;
    }
    d->updater->reset(); // Empty region (i.e. everything is dirty).
    d->add_idle();
    if (d->prefs.debug_show_unclean) queue_draw();
}

/**
 * Redraw the given area during idle.
 */
void Canvas::redraw_area(int x0, int y0, int x1, int y1)
{
    if (!d->active) {
        // CanvasItems redraw their area when being deleted... which happens when the Canvas is destroyed.
        // We need to ignore their requests!
        return;
    }

    // Clamp area to Cairo's technically supported max size (-2^30..+2^30-1).
    // This ensures that the rectangle dimensions don't overflow and wrap around.
    constexpr int min_coord = -(1 << 30);
    constexpr int max_coord = (1 << 30) - 1;

    x0 = std::clamp(x0, min_coord, max_coord);
    y0 = std::clamp(y0, min_coord, max_coord);
    x1 = std::clamp(x1, min_coord, max_coord);
    y1 = std::clamp(y1, min_coord, max_coord);

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    auto rect = Geom::IntRect::from_xywh(x0, y0, x1 - x0, y1 - y0);
    d->updater->mark_dirty(rect);
    d->add_idle();
    if (d->prefs.debug_show_unclean) queue_draw();
}

void Canvas::redraw_area(Geom::Coord x0, Geom::Coord y0, Geom::Coord x1, Geom::Coord y1)
{
    // Handle overflow during conversion gracefully.
    // Round outward to make sure integral coordinates cover the entire area.
    constexpr Geom::Coord min_int = std::numeric_limits<int>::min();
    constexpr Geom::Coord max_int = std::numeric_limits<int>::max();

    redraw_area(
        (int)std::floor(std::clamp(x0, min_int, max_int)),
        (int)std::floor(std::clamp(y0, min_int, max_int)),
        (int)std::ceil (std::clamp(x1, min_int, max_int)),
        (int)std::ceil (std::clamp(y1, min_int, max_int))
    );
}

void Canvas::redraw_area(Geom::Rect &area)
{
    redraw_area(area.left(), area.top(), area.right(), area.bottom());
}

/**
 * Redraw after changing canvas item geometry.
 */
void Canvas::request_update()
{
    // Flag geometry as needing update.
    _need_update = true;

    // Trigger the idle process to perform the update.
    d->add_idle();
}

/**
 * Scroll window so drawing point 'pos' is at upper left corner of canvas.
 */
void Canvas::set_pos(Geom::IntPoint const &pos)
{
    if (pos == _pos) {
        return;
    }

    _pos = pos;

    d->add_idle();
    queue_draw();

    if (auto grid = dynamic_cast<Inkscape::UI::Widget::CanvasGrid*>(get_parent())) {
        grid->UpdateRulers();
    }
}

/**
 * Set the desk colour. Transparency is interpreted as amount of checkerboard.
 */
void Canvas::set_desk(uint32_t rgba)
{
    if (d->desk == rgba) return;
    bool invalidated = d->background_in_stores;
    d->desk = rgba;
    invalidated |= d->background_in_stores = d->require_background_in_stores();
    if (get_realized() && invalidated) redraw_all();
    queue_draw();
}

/**
 * Set the page border colour. Although we don't draw the borders, this colour affects the shadows which we do draw (in OpenGL mode).
 */
void Canvas::set_border(uint32_t rgba)
{
    if (d->border == rgba) return;
    d->border = rgba;
    if (get_realized() && get_opengl_enabled()) queue_draw();
}

/**
 * Set the page colour. Like the desk colour, transparency is interpreted as checkerboard.
 */
void Canvas::set_page(uint32_t rgba)
{
    if (d->page == rgba) return;
    bool invalidated = d->background_in_stores;
    d->page = rgba;
    invalidated |= d->background_in_stores = d->require_background_in_stores();
    if (get_realized() && invalidated) redraw_all();
    queue_draw();
}

uint32_t Canvas::get_effective_background() const
{
    auto arr = checkerboard_darken(rgb_to_array(d->desk), 1.0f - 0.5f * SP_RGBA32_A_U(d->desk) / 255.0f);
    return SP_RGBA32_F_COMPOSE(arr[0], arr[1], arr[2], 1.0);
}

void Canvas::set_drawing_disabled(bool disable)
{
    _drawing_disabled = disable;
    if (!disable) {
        d->add_idle();
    }
}

void Canvas::set_render_mode(Inkscape::RenderMode mode)
{
    if ((_render_mode == RenderMode::OUTLINE_OVERLAY) != (mode == RenderMode::OUTLINE_OVERLAY) && !get_opengl_enabled()) {
        queue_draw();
    }
    _render_mode = mode;
    if (_drawing) {
        _drawing->setRenderMode(_render_mode == RenderMode::OUTLINE_OVERLAY ? RenderMode::NORMAL : _render_mode);
        _drawing->setOutlineOverlay(d->outlines_required());
    }
    if (_desktop) {
        _desktop->setWindowTitle(); // Mode is listed in title.
    }
}

void Canvas::set_color_mode(Inkscape::ColorMode mode)
{
    _color_mode = mode;
    if (_drawing) {
        _drawing->setColorMode(_color_mode);
    }
    if (_desktop) {
        _desktop->setWindowTitle(); // Mode is listed in title.
    }
}

void Canvas::set_split_mode(Inkscape::SplitMode mode)
{
    if (_split_mode != mode) {
        _split_mode = mode;
        if (_split_mode == Inkscape::SplitMode::SPLIT) {
            _hover_direction = Inkscape::SplitDirection::NONE;
        }
        if (_drawing) {
            _drawing->setOutlineOverlay(d->outlines_required());
        }
        redraw_all();
    }
}

void Canvas::set_clip_to_page_mode(bool clip)
{
    if (clip != d->clip_to_page) {
        d->clip_to_page = clip;
        d->add_idle();
    }
}

void Canvas::set_cms_key(std::string key)
{
    _cms_key = std::move(key);
    _cms_active = !_cms_key.empty();
    redraw_all();
}

/**
 * Clear current and grabbed items.
 */
void Canvas::canvas_item_destructed(Inkscape::CanvasItem *item)
{
    if (item == _current_canvas_item) {
        _current_canvas_item = nullptr;
    }

    if (item == _current_canvas_item_new) {
        _current_canvas_item_new = nullptr;
    }

    if (item == _grabbed_canvas_item) {
        _grabbed_canvas_item = nullptr;
        auto const display = Gdk::Display::get_default();
        auto const seat    = display->get_default_seat();
        seat->ungrab();
    }

    if (item == d->pre_scroll_grabbed_item) {
        d->pre_scroll_grabbed_item = nullptr;
    }
}

// Change cursor
void Canvas::set_cursor()
{
    if (!_desktop) {
        return;
    }

    auto display = Gdk::Display::get_default();

    switch (_hover_direction) {
        case Inkscape::SplitDirection::NONE:
            _desktop->event_context->use_tool_cursor();
            break;

        case Inkscape::SplitDirection::NORTH:
        case Inkscape::SplitDirection::EAST:
        case Inkscape::SplitDirection::SOUTH:
        case Inkscape::SplitDirection::WEST:
        {
            auto cursor = Gdk::Cursor::create(display, "pointer");
            get_window()->set_cursor(cursor);
            break;
        }

        case Inkscape::SplitDirection::HORIZONTAL:
        {
            auto cursor = Gdk::Cursor::create(display, "ns-resize");
            get_window()->set_cursor(cursor);
            break;
        }

        case Inkscape::SplitDirection::VERTICAL:
        {
            auto cursor = Gdk::Cursor::create(display, "ew-resize");
            get_window()->set_cursor(cursor);
            break;
        }

        default:
            // Shouldn't reach.
            std::cerr << "Canvas::set_cursor: Unknown hover direction!" << std::endl;
    }
}

void Canvas::get_preferred_width_vfunc(int &minimum_width, int &natural_width) const
{
    minimum_width = natural_width = 256;
}

void Canvas::get_preferred_height_vfunc(int &minimum_height, int &natural_height) const
{
    minimum_height = natural_height = 256;
}

void Canvas::on_size_allocate(Gtk::Allocation &allocation)
{
    parent_type::on_size_allocate(allocation);
    assert(allocation == get_allocation());

    // Necessary as GTK seems to somehow invalidate the current pipeline state upon resize.
    if (d->active && get_opengl_enabled()) {
        d->graphics->invalidated_glstate();
    }

    // Trigger the size update to be applied to the stores before the next redraw of the window.
    d->add_idle();
}

Glib::RefPtr<Gdk::GLContext> Canvas::create_context()
{
    Glib::RefPtr<Gdk::GLContext> result;

    try {
        result = get_window()->create_gl_context();
    } catch (const Gdk::GLError &e) {
        std::cerr << "Failed to create OpenGL context: " << e.what().raw() << std::endl;
        return {};
    }

    try {
        result->realize();
    } catch (const Glib::Error &e) {
        std::cerr << "Failed to realize OpenGL context: " << e.what().raw() << std::endl;
        return {};
    }

    return result;
}

void Canvas::paint_widget(const Cairo::RefPtr<Cairo::Context> &cr)
{
    framecheck_whole_function(d)

    if (d->prefs.debug_idle_starvation && d->idle_running) d->wait_accumulated += g_get_monotonic_time() - d->wait_begin;

    if (!d->active) {
        std::cerr << "Canvas::paint_widget: Called while not active!" << std::endl;
        return;
    }

    // canvas_item_print_tree(_canvas_item_root);

    // Although hipri_idle is scheduled at a priority higher than draw, and should therefore always be called first if
    // asked, there are times when GTK simply decides to call on_draw anyway. Here we ensure that that call has taken
    // place. This is problematic because if hipri_idle does rendering, enlarging the damage rect, then our drawing will
    // still be clipped to the old damage rect. It was precisely this problem that lead to the introduction of
    // hipri_idle. Fortunately, the following failsafe only seems to execute once during initialisation, and once on
    // further resize events. Both these events seem to trigger a full damage, hence we are ok.
    if (d->hipri_idle.connected()) {
        d->hipri_idle.disconnect();
        d->on_hipri_idle();
    }

    if (get_opengl_enabled()) {
        // Must be done after the above idle rendering, in case it binds a different framebuffer.
        bind_framebuffer();
    }

    Graphics::PaintArgs args;
    args.mouse = d->last_mouse;
    args.render_mode = _render_mode;
    args.splitmode = _split_mode;
    args.splitfrac = _split_frac;
    args.splitdir = _split_direction;
    args.hoverdir = _hover_direction;
    args.yaxisdir = _desktop ? _desktop->yaxisdir() : 1.0;
    args.clean_region = d->updater->clean_region;

    d->graphics->paint_widget(Fragment{ _affine, get_area_world() }, args, cr);

    // Process bucketed events as soon as possible after draw. We cannot process them now, because we have
    // a frame to get out as soon as possible, and processing events may take a while. Instead, we schedule
    // it with a signal callback on the main loop that runs as soon as this function is completed.
    if (!d->eventprocessor->events.empty()) d->schedule_bucket_emptier();

    // Record the fact that a draw is no longer pending.
    d->pending_draw = false;

    // Notify the update strategy that another frame has passed.
    d->updater->next_frame();

    // If asked, print idle time utilisation stats.
    if (d->prefs.debug_idle_starvation && d->sample_begin != 0) {
        auto elapsed = g_get_monotonic_time() - d->sample_begin;
        auto overhead = 100 * d->wait_accumulated / elapsed;
        auto col = overhead < 5 ? "\033[1;32m" : overhead < 20 ? "\033[1;33m" : "\033[1;31m";
        std::cout << "Overhead: " << col << overhead << "%" << "\033[0m" << (d->idle_running ? " [still busy]" : "") << std::endl;
        d->sample_begin = d->wait_begin = g_get_monotonic_time();
        d->wait_accumulated = 0;
    }

    // If asked, run an animation loop.
    if (d->prefs.debug_animate) {
        auto t = g_get_monotonic_time() / 1700000.0;
        auto affine = Geom::Rotate(t * 5) * Geom::Scale(1.0 + 0.6 * cos(t * 2));
        set_affine(affine);
        auto dim = _desktop && _desktop->doc() ? _desktop->doc()->getDimensions() : Geom::Point();
        set_pos(Geom::Point((0.5 + 0.3 * cos(t * 2)) * dim.x(), (0.5 + 0.3 * sin(t * 3)) * dim.y()) * affine - Geom::Point(get_dimensions()) * 0.5);
    }
}

void CanvasPrivate::add_idle()
{
    framecheck_whole_function(this)

    if (!active) {
        // We can safely discard events until active, because we will run add_idle on activation later in initialisation.
        return;
    }

    if (prefs.debug_idle_starvation && !idle_running) {
        auto time = g_get_monotonic_time();
        if (sample_begin == 0) {
            sample_begin = time;
            wait_accumulated = 0;
        }
        wait_begin = time;
    }

    if (!hipri_idle.connected()) {
        hipri_idle = Glib::signal_idle().connect(sigc::mem_fun(*this, &CanvasPrivate::on_hipri_idle), G_PRIORITY_HIGH_IDLE + 15); // after resize, before draw
    }

    if (!lopri_idle.connected()) {
        lopri_idle = Glib::signal_idle().connect(sigc::mem_fun(*this, &CanvasPrivate::on_lopri_idle), G_PRIORITY_DEFAULT_IDLE);
    }

    idle_running = true;
}

// Replace a region with a larger region consisting of fewer, larger rectangles. (Allowed to slightly overlap.)
auto coarsen(const Cairo::RefPtr<Cairo::Region> &region, int min_size, int glue_size, double min_fullness)
{
    // Sort the rects by minExtent.
    struct Compare
    {
        bool operator()(const Geom::IntRect &a, const Geom::IntRect &b) const {
            return a.minExtent() < b.minExtent();
        }
    };
    std::multiset<Geom::IntRect, Compare> rects;
    int nrects = region->get_num_rectangles();
    for (int i = 0; i < nrects; i++) {
        rects.emplace(cairo_to_geom(region->get_rectangle(i)));
    }

    // List of processed rectangles.
    std::vector<Geom::IntRect> processed;
    processed.reserve(nrects);

    // Removal lists.
    std::vector<decltype(rects)::iterator> remove_rects;
    std::vector<int> remove_processed;

    // Repeatedly expand small rectangles by absorbing their nearby small rectangles.
    while (!rects.empty() && rects.begin()->minExtent() < min_size) {
        // Extract the smallest unprocessed rectangle.
        auto rect = *rects.begin();
        rects.erase(rects.begin());

        // Initialise the effective glue size.
        int effective_glue_size = glue_size;

        while (true) {
            // Find the glue zone.
            auto glue_zone = rect;
            glue_zone.expandBy(effective_glue_size);

            // Absorb rectangles in the glue zone. We could do better algorithmically speaking, but in real life it's already plenty fast.
            auto newrect = rect;
            int absorbed_area = 0;

            remove_rects.clear();
            for (auto it = rects.begin(); it != rects.end(); ++it) {
                if (glue_zone.contains(*it)) {
                    newrect.unionWith(*it);
                    absorbed_area += it->area();
                    remove_rects.emplace_back(it);
                }
            }

            remove_processed.clear();
            for (int i = 0; i < processed.size(); i++) {
                auto &r = processed[i];
                if (glue_zone.contains(r)) {
                    newrect.unionWith(r);
                    absorbed_area += r.area();
                    remove_processed.emplace_back(i);
                }
            }

            // If the result was too empty, try again with a smaller glue size.
            double fullness = (double)(rect.area() + absorbed_area) / newrect.area();
            if (fullness < min_fullness) {
                effective_glue_size /= 2;
                continue;
            }

            // Commit the change.
            rect = newrect;

            for (auto &it : remove_rects) {
                rects.erase(it);
            }

            for (int j = (int)remove_processed.size() - 1; j >= 0; j--) {
                int i = remove_processed[j];
                processed[i] = processed.back();
                processed.pop_back();
            }

            // Stop growing if not changed or now big enough.
            bool finished = absorbed_area == 0 || rect.minExtent() >= min_size;
            if (finished) {
                break;
            }

            // Otherwise, continue normally.
            effective_glue_size = glue_size;
        }

        // Put the finished rectangle in processed.
        processed.emplace_back(rect);
    }

    // Put any remaining rectangles in processed.
    for (auto &rect : rects) {
        processed.emplace_back(rect);
    }

    return processed;
}

std::optional<Geom::Dim2> CanvasPrivate::old_bisector(const Geom::IntRect &rect)
{
    int bw = rect.width();
    int bh = rect.height();

    /*
     * Determine redraw strategy:
     *
     * bw < bh (strips mode): Draw horizontal strips starting from cursor position.
     *                        Seems to be faster for drawing many smaller objects zoomed out.
     *
     * bw > hb (chunks mode): Splits across the larger dimension of the rectangle, painting
     *                        in almost square chunks (from the cursor.
     *                        Seems to be faster for drawing a few blurred objects across the entire screen.
     *                        Seems to be somewhat psychologically faster.
     *
     * Default is for strips mode.
     */

    int max_pixels;
    if (q->_render_mode != Inkscape::RenderMode::OUTLINE) {
        // Can't be too small or large gradient will be rerendered too many times!
        max_pixels = 65536 * prefs.tile_multiplier;
    } else {
        // Paths only. 1M is catched buffer and we need four channels.
        max_pixels = 262144;
    }

    if (bw * bh > max_pixels) {
        if (bw < bh || bh < 2 * prefs.tile_size) {
            return Geom::X;
        } else {
            return Geom::Y;
        }
    }

    return {};
}

std::optional<Geom::Dim2> CanvasPrivate::new_bisector(const Geom::IntRect &rect)
{
    int bw = rect.width();
    int bh = rect.height();

    // Chop in half along the bigger dimension if the bigger dimension is too big.
    if (bw > bh) {
        if (bw > prefs.new_bisector_size) {
            return Geom::X;
        }
    } else {
        if (bh > prefs.new_bisector_size) {
            return Geom::Y;
        }
    }

    return {};
}

bool CanvasPrivate::on_hipri_idle()
{
    on_lopri_idle();
    return false;
}

bool CanvasPrivate::on_lopri_idle()
{
    assert(active);
    if (idle_running) {
        if (prefs.debug_idle_starvation) wait_accumulated += g_get_monotonic_time() - wait_begin;
        idle_running = on_idle();
        if (prefs.debug_idle_starvation && idle_running) wait_begin = g_get_monotonic_time();
    }
    return idle_running;
}

void CanvasPrivate::handle_stores_action(Stores::Action action)
{
   switch (action) {
       case Stores::Action::Recreated:
           // Set everything as needing redraw.
           updater->reset();

           if (prefs.debug_show_unclean) q->queue_draw();
           break;

       case Stores::Action::Shifted:
           updater->intersect(stores.store().rect);

           if (prefs.debug_show_unclean) q->queue_draw();
           break;

       default:
           break;
   }

   if (action != Stores::Action::None) {
       q->_drawing->setCacheLimit(stores.store().rect);
   }
}

bool CanvasPrivate::on_idle()
{
    framecheck_whole_function(this)

    assert(active); // Guaranteed since already checked by both callers.
    assert(q->_canvas_item_root);

    // Quit idle process if not supposed to be drawing.
    if (q->_drawing_disabled) {
        return false;
    }

    // Because GTK keeps making it not current.
    if (q->get_opengl_enabled()) q->make_current();

    if ((outlines_required() && !outlines_enabled) || scale_factor != q->get_scale_factor()) {
        stores.reset();
    }

    outlines_enabled = outlines_required();
    scale_factor = q->get_scale_factor();

    pi.pages.clear();
    q->_canvas_item_root->visit_page_rects([this] (auto &rect) {
        pi.pages.emplace_back(rect);
    });

    graphics->set_outlines_enabled(outlines_enabled);
    graphics->set_scale_factor(scale_factor);
    graphics->set_colours(page, desk, border);
    graphics->set_background_in_stores(require_background_in_stores());

    auto ret = stores.update(Fragment{ q->_affine, q->get_area_world() });
    handle_stores_action(ret);

    if (clip_to_page) {
        Geom::PathVector pv;
        for (auto &rect : pi.pages) {
            pv.push_back(Geom::Path(rect));
        }
        q->_drawing->setClip(std::move(pv));
    } else {
        q->_drawing->setClip({});
    }

    // Assert that the clean region is a subregion of the store.
    #ifndef NDEBUG
    auto tmp = updater->clean_region->copy();
    tmp->subtract(geom_to_cairo(stores.store().rect));
    assert(tmp->empty());
    #endif

    // Ensure the geometry is up-to-date and in the right place.
    auto const &affine = stores.store().affine;
    if (q->_need_update || geom_affine != affine) {
        q->_canvas_item_root->update(affine);
        geom_affine = affine;
        q->_need_update = false;
    }

    // If asked to, don't paint anything and instead halt the idle process.
    if (prefs.debug_disable_redraw) {
        return false;
    }

    // Get the mouse position in screen space.
    Geom::IntPoint mouse_loc = (last_mouse ? *last_mouse : Geom::Point(q->get_dimensions()) / 2).round();

    // Map the mouse to canvas space.
    mouse_loc += q->_pos;
    if (stores.mode() == Stores::Mode::Decoupled) {
        mouse_loc = (Geom::Point(mouse_loc) * q->_affine.inverse() * stores.store().affine).round();
    }

    // Get the visible rect.
    Geom::IntRect visible = q->get_area_world();
    if (stores.mode() == Stores::Mode::Decoupled) {
        visible = (Geom::Parallelogram(visible) * q->_affine.inverse() * stores.store().affine).bounds().roundOutwards();
    }

    // Begin processing redraws.
    auto start_time = g_get_monotonic_time();

    // Paint a given subrectangle of the store given by 'bounds', but avoid painting the part of it within 'clean' if possible.
    // Some parts both outside the bounds and inside the clean region may also be painted if it helps reduce fragmentation.
    // Returns true to indicate timeout.
    auto process_redraw = [&, this] (Geom::IntRect const &bounds, Cairo::RefPtr<Cairo::Region> const &clean, bool interruptible = true, bool preemptible = true) {
        // Assert that we do not render outside of store.
        assert(stores.store().rect.contains(bounds));

        // Get the region we are asked to paint.
        auto region = Cairo::Region::create(geom_to_cairo(bounds));
        region->subtract(clean);

        // Get the list of rectangles to paint, coarsened to avoid fragmentation.
        auto rects = coarsen(region,
                             std::min<int>(prefs.coarsener_min_size, prefs.new_bisector_size / 2),
                             std::min<int>(prefs.coarsener_glue_size, prefs.new_bisector_size / 2),
                             prefs.coarsener_min_fullness);

        // Put the rectangles into a heap sorted by distance from mouse.
        auto cmp = [&] (const Geom::IntRect &a, const Geom::IntRect &b) {
            return distSq(mouse_loc, a) > distSq(mouse_loc, b);
        };
        std::make_heap(rects.begin(), rects.end(), cmp);

        // Process rectangles until none left or timed out.
        while (!rects.empty()) {
            // Extract the closest rectangle to the mouse.
            std::pop_heap(rects.begin(), rects.end(), cmp);
            auto rect = rects.back();
            rects.pop_back();

            // Cull empty rectangles.
            if (rect.hasZeroArea()) {
                continue;
            }

            // Cull rectangles that lie entirely inside the clean region.
            // (These can be generated by coarsening; they must be discarded to avoid getting stuck re-rendering the same rectangles.)
            if (clean->contains_rectangle(geom_to_cairo(rect)) == Cairo::REGION_OVERLAP_IN) {
                continue;
            }

            // Lambda to add a rectangle to the heap.
            auto add_rect = [&] (const Geom::IntRect &rect) {
                rects.emplace_back(rect);
                std::push_heap(rects.begin(), rects.end(), cmp);
            };

            // If the rectangle needs bisecting, bisect it and put it back on the heap.
            // Note: Currently we disable bisection if interruptible is false, because the only point of bisection is
            // to stay within the timeout. However in the future, with tile parallelisation, this will no longer hold.
            auto axis = prefs.use_new_bisector ? new_bisector(rect) : old_bisector(rect);
            if (axis && interruptible) {
                int mid = rect[*axis].middle();
                auto lo = rect; lo[*axis].setMax(mid); add_rect(lo);
                auto hi = rect; hi[*axis].setMin(mid); add_rect(hi);
                continue;
            }

            // Extend thin rectangles at the edge of the bounds rect to at least some minimum size, being sure to keep them within the store.
            // (This ensures we don't end up rendering one thin rectangle at the edge every frame while the view is moved continuously.)
            if (preemptible) {
                if (rect.width() < prefs.preempt) {
                    if (rect.left()  == bounds.left() ) rect.setLeft (std::max(rect.right() - prefs.preempt, stores.store().rect.left() ));
                    if (rect.right() == bounds.right()) rect.setRight(std::min(rect.left()  + prefs.preempt, stores.store().rect.right()));
                }
                if (rect.height() < prefs.preempt) {
                    if (rect.top()    == bounds.top()   ) rect.setTop   (std::max(rect.bottom() - prefs.preempt, stores.store().rect.top()   ));
                    if (rect.bottom() == bounds.bottom()) rect.setBottom(std::min(rect.top()    + prefs.preempt, stores.store().rect.bottom()));
                }
            }

            // Paint the rectangle.
            paint_rect(rect);

            // Introduce an artificial delay for each rectangle.
            if (prefs.debug_slow_redraw) g_usleep(prefs.debug_slow_redraw_time);

            // Mark the rectangle as clean.
            updater->mark_clean(rect);
            stores.mark_drawn(rect);

            // Get the rectangle of screen-space needing repaint.
            Geom::IntRect repaint_rect;
            if (stores.mode() != Stores::Mode::Decoupled) {
                // Simply translate to get back to screen space.
                repaint_rect = rect - q->_pos;
            } else {
                // Transform into screen space, take bounding box, and round outwards.
                auto pl = Geom::Parallelogram(rect);
                pl *= stores.store().affine.inverse() * q->_affine;
                pl *= Geom::Translate(-q->_pos);
                repaint_rect = pl.bounds().roundOutwards();
            }

            // Check if repaint is necessary - some rectangles could be entirely off-screen.
            auto screen_rect = Geom::IntRect({0, 0}, q->get_dimensions());
            if (regularised(repaint_rect & screen_rect)) {
                // Schedule repaint.
                queue_draw_area(repaint_rect);
                disconnect_bucket_emptier_tick_callback();
                pending_draw = true;
            }

            // Check for timeout.
            if (interruptible) {
                auto now = g_get_monotonic_time();
                auto elapsed = now - start_time;
                if (elapsed > prefs.render_time_limit) {
                    // Timed out. Temporarily return to GTK main loop, and come back here when next idle.
                    if (prefs.debug_logging) std::cout << "Timed out: " << elapsed << " us" << std::endl;
                    framecheckobj.subtype = 1;
                    return true;
                }
            }
        }

        // No timeout.
        return false;
    };

    if (auto vis_store = regularised(visible & stores.store().rect)) {
        // The highest priority to redraw is the region that is visible but not covered by either clean or snapshot content, if in decoupled mode.
        // If this is not rendered immediately, it will be perceived as edge flicker, most noticeably on zooming out, but also on rotation too.
        if (stores.mode() == Stores::Mode::Decoupled) {
            if (process_redraw(*vis_store, unioned(updater->clean_region->copy(), stores.snapshot().drawn))) return true;
        }

        // Another high priority to redraw is the grabbed canvas item, if the user has requested block updates.
        if (q->_grabbed_canvas_item && prefs.block_updates) {
            if (auto grabbed = regularised(q->_grabbed_canvas_item->get_bounds().roundOutwards() & *vis_store)) {
                process_redraw(*grabbed, updater->clean_region, false, false); // non-interruptible, non-preemptible
                // Reset timeout to leave the normal amount of time for clearing up artifacts.
                start_time = g_get_monotonic_time();
            }
        }

        // The main priority to redraw, and the bread and butter of Inkscape's painting, is the visible content that is not clean.
        // This may be done over several cycles, at the direction of the Updater, each outwards from the mouse.
        do {
            if (process_redraw(*vis_store, updater->get_next_clean_region())) return true;
        }
        while (updater->report_finished());
    }

    // The lowest priority to redraw is the prerender margin around the visible rectangle.
    // (This is in addition to any opportunistic prerendering that may have already occurred in the above steps.)
    auto prerender = expandedBy(visible, prefs.prerender);
    auto prerender_store = regularised(prerender & stores.store().rect);
    if (prerender_store) {
        if (process_redraw(*prerender_store, updater->clean_region)) return true;
    }

    // Finished drawing. Handle transitions out of decoupled mode, by checking if we need to do a final redraw at the correct affine.
    ret = stores.finished_draw(Fragment{ q->_affine, q->get_area_world() });
    handle_stores_action(ret);

    if (ret != Stores::Action::None) {
        // Continue idle process.
        return true;
    } else {
        // All done, quit the idle process.
        framecheckobj.subtype = 3;
        return false;
    }
}

void CanvasPrivate::paint_rect(const Geom::IntRect &rect)
{
    // Make sure the paint rectangle lies within the store.
    assert(stores.store().rect.contains(rect));

    auto paint = [&, this] (bool need_background, bool outline_pass) {
        auto surface = graphics->request_tile_surface(rect, outline_pass);
        paint_single_buffer(surface, rect, need_background, outline_pass);
        return surface;
    };

    Fragment fragment;
    fragment.affine = geom_affine;
    fragment.rect = rect;

    Cairo::RefPtr<Cairo::ImageSurface> surface, outline_surface;
    surface = paint(require_background_in_stores(), false);
    if (outlines_enabled) {
        outline_surface = paint(false, true);
    }

    graphics->draw_tile(fragment, surface, outline_surface);
}

void CanvasPrivate::paint_single_buffer(const Cairo::RefPtr<Cairo::ImageSurface> &surface, const Geom::IntRect &rect, bool need_background, bool outline_pass)
{
    // Create Cairo context.
    auto cr = Cairo::Context::create(surface);

    // Clear background.
    cr->save();
    if (need_background) {
        Graphics::paint_background(Fragment{ geom_affine, rect }, pi, page, desk, cr);
    } else {
        cr->set_operator(Cairo::OPERATOR_CLEAR);
        cr->paint();
    }
    cr->restore();

    // Render drawing on top of background.
    if (q->_canvas_item_root->is_visible()) {
        auto buf = Inkscape::CanvasItemBuffer{ rect, scale_factor, cr, outline_pass };
        q->_canvas_item_root->render(&buf);
    }

    // Paint over newly drawn content with a translucent random colour.
    if (prefs.debug_show_redraw) {
        cr->set_source_rgba((rand() % 256) / 255.0, (rand() % 256) / 255.0, (rand() % 256) / 255.0, 0.2);
        cr->set_operator(Cairo::OPERATOR_OVER);
        cr->paint();
    }

    if (q->_cms_active) {
        auto transf = prefs.from_display
                    ? Inkscape::CMSSystem::getDisplayPer(q->_cms_key)
                    : Inkscape::CMSSystem::getDisplayTransform();
        if (transf) {
            surface->flush();
            auto px = surface->get_data();
            int stride = surface->get_stride();
            for (int i = 0; i < rect.height(); i++) {
                auto row = px + i * stride;
                Inkscape::CMSSystem::doTransform(transf, row, row, rect.width());
            }
            surface->mark_dirty();
        }
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
