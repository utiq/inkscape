// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * A class to render the SVG drawing.
 */

/*
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Rewrite of _SPCavasArena.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "canvas-item-drawing.h"

#include "desktop.h"

#include "display/drawing.h"
#include "display/drawing-context.h"
#include "display/drawing-item.h"
#include "display/drawing-group.h"

#include "ui/widget/canvas.h"
#include "ui/modifiers.h"

namespace Inkscape {

/**
 * Create the drawing. One per window!
 */
CanvasItemDrawing::CanvasItemDrawing(CanvasItemGroup *group)
    : CanvasItem(group)
{
    _name = "CanvasItemDrawing";
    _pickable = true;

    _drawing = std::make_unique<Drawing>(this);
    auto root = new DrawingGroup(*_drawing);
    root->setPickChildren(true);
    _drawing->setRoot(root);
}

/**
 * Returns distance between point in canvas units and nearest point on drawing.
 */
double CanvasItemDrawing::closest_distance_to(Geom::Point const &p)
{
    double d = Geom::infinity();
    std::cerr << "CanvasItemDrawing::closest_distance_to: Not implemented!" << std::endl;
    return d;
}

/**
 * Returns true if point p (in canvas units) is inside some object in drawing.
 */
bool CanvasItemDrawing::contains(Geom::Point const &p, double tolerance)
{
    if (tolerance != 0) {
        std::cerr << "CanvasItemDrawing::contains: Non-zero tolerance not implemented!" << std::endl;
    }

    _drawing->update(Geom::IntRect::infinite(), _ctx.ctm, DrawingItem::STATE_PICK | DrawingItem::STATE_BBOX);
    _picked_item = _drawing->pick(p, _drawing->cursorTolerance(), _sticky * DrawingItem::PICK_STICKY | _pick_outline * DrawingItem::PICK_OUTLINE);

    if (_picked_item) {
        // This will trigger a signal that is handled by our event handler. Seems a bit of a
        // round-a-bout way of doing things but it matches what other pickable canvas-item classes
        // do.
        return true;
    }

    return false;
}

/**
 * Update and redraw drawing.
 */
void CanvasItemDrawing::update(Geom::Affine const &affine)
{
    auto new_affine = affine;

    // Correct for y-axis. This should not be here!!!!
    if (auto *desktop = _canvas->get_desktop()) {
        new_affine = desktop->doc2dt() * new_affine;
    }

    // if (_affine == new_affine && !_need_update) {
    //     // Nothing to do.
    //     return;
    // }

    _ctx.ctm = new_affine;  // TODO Remove _ctx.ctm... it's exactly the same as _affine!

    unsigned reset = (_affine != new_affine ? DrawingItem::STATE_ALL : 0);

    _affine = new_affine;

    _drawing->update(Geom::IntRect::infinite(), _ctx.ctm, DrawingItem::STATE_ALL, reset);

    Geom::OptIntRect bbox = _drawing->root()->drawbox();
    if (bbox) {
        _bounds = *bbox;
        _bounds.expandBy(1); // Avoid aliasing artifacts.
    }

    // Todo: This should be managed elsewhere.
    if (_cursor) {
        /* Mess with enter/leave notifiers */
        DrawingItem *new_drawing_item = _drawing->pick(_c, _delta, _sticky * DrawingItem::PICK_STICKY | _pick_outline * DrawingItem::PICK_OUTLINE);
        if (_active_item != new_drawing_item) {

            GdkEventCrossing ec;
            ec.window = _canvas->get_window()->gobj();
            ec.send_event = true;
            ec.subwindow = ec.window;
            ec.time = GDK_CURRENT_TIME;
            ec.x = _c.x();
            ec.y = _c.y();

            /* fixme: Why? */
            if (_active_item) {
                ec.type = GDK_LEAVE_NOTIFY;
                _drawing_event_signal.emit((GdkEvent *) &ec, _active_item);
            }

            _active_item = new_drawing_item;

            if (_active_item) {
                ec.type = GDK_ENTER_NOTIFY;
                _drawing_event_signal.emit((GdkEvent *) &ec, _active_item);
            }
        }
    }

    _need_update = false;
}

/**
 * Render drawing to screen via Cairo.
 */
void CanvasItemDrawing::render(Inkscape::CanvasItemBuffer *buf)
{
    if (!buf) {
        std::cerr << "CanvasItemDrawing::Render: No buffer!" << std::endl;
        return;
    }

    if (buf->rect.hasZeroArea()) {
        return;
    }

    auto dc = Inkscape::DrawingContext(buf->cr->cobj(), buf->rect.min());
    _drawing->render(dc, buf->rect, buf->outline_pass * DrawingItem::RENDER_OUTLINE);
}

/**
 * Handle events directed at the drawing. We first attempt to handle them here.
 */
bool CanvasItemDrawing::handle_event(GdkEvent *event)
{
    bool retval = false;
    
    switch (event->type) {
        case GDK_ENTER_NOTIFY:
            if (!_cursor) {
                if (_active_item) {
                    std::cerr << "CanvasItemDrawing::event_handler: cursor entered drawing with an active item!" << std::endl;
                }
                _cursor = true;

                /* TODO ... event -> arena transform? */
                _c = Geom::Point(event->crossing.x, event->crossing.y);

                /* fixme: Not sure abut this, but seems the right thing (Lauris) */
                //_drawing->update(Geom::IntRect::infinite(), _ctx, DrawingItem::STATE_PICK | DrawingItem::STATE_BBOX, 0);
                _active_item = _drawing->pick(_c, _drawing->cursorTolerance(), _sticky * DrawingItem::PICK_STICKY | _pick_outline * DrawingItem::PICK_OUTLINE);
                retval = _drawing_event_signal.emit(event, _active_item);
            }
            break;

        case GDK_LEAVE_NOTIFY:
            if (_cursor) {
                retval = _drawing_event_signal.emit(event, _active_item);
                _active_item = nullptr;
                _cursor = false;
            }
            break;

        case GDK_MOTION_NOTIFY:
        {
            /* TODO ... event -> arena transform? */
            _c = Geom::Point(event->motion.x, event->motion.y);

            /* fixme: Not sure abut this, but seems the right thing (Lauris) */
            //_drawing->update(Geom::IntRect::infinite(), _ctx, DrawingItem::STATE_PICK | DrawingItem::STATE_BBOX);

            auto new_drawing_item = _drawing->pick(_c, _drawing->cursorTolerance(), _sticky * DrawingItem::PICK_STICKY | _pick_outline * DrawingItem::PICK_OUTLINE);
            if (_active_item != new_drawing_item) {

                GdkEventCrossing ec;
                ec.window = event->motion.window;
                ec.send_event = event->motion.send_event;
                ec.subwindow = event->motion.window;
                ec.time = event->motion.time;
                ec.x = event->motion.x;
                ec.y = event->motion.y;

                /* fixme: What is wrong? */
                if (_active_item) {
                    ec.type = GDK_LEAVE_NOTIFY;
                    retval = _drawing_event_signal.emit((GdkEvent *) &ec, _active_item);
                }

                _active_item = new_drawing_item;

                if (_active_item) {
                    ec.type = GDK_ENTER_NOTIFY;
                    retval = _drawing_event_signal.emit((GdkEvent *) &ec, _active_item);
                }
            }
            retval = retval || _drawing_event_signal.emit(event, _active_item);
            break;
        }

        case GDK_SCROLL:
        {
            if (Modifiers::Modifier::get(Modifiers::Type::CANVAS_ZOOM)->active(event->scroll.state)) {
                /* Zoom is emitted by the canvas as well, ignore here */
                return false;
            }
            retval = _drawing_event_signal.emit(event, _active_item);
            break;
        }

        default:
            /* Just send event */
            retval = _drawing_event_signal.emit(event, _active_item);
            break;
    }

    return retval;
}

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
