// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Matthew Jakeman <mjakeman26@outlook.co.nz>
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef INKSCAPE_UI_WIDGET_EVENTS_CANVAS_EVENT_H
#define INKSCAPE_UI_WIDGET_EVENTS_CANVAS_EVENT_H

#include <cstdint>
#include <memory>

#include <2geom/point.h>
#include <gdk/gdk.h>

#include "enums.h"

namespace Inkscape {

// Smart pointer for wrapping GdkEvents.
struct GdkEventFreer { void operator()(GdkEvent *ev) const { gdk_event_free(ev); } };
using GdkEventUniqPtr = std::unique_ptr<GdkEvent, GdkEventFreer>;

/**
 * Abstract base class for events
 */
class CanvasEvent
{
public:
    CanvasEvent(CanvasEvent const &other) : _original(gdk_event_copy(other._original.get())) {}
    virtual ~CanvasEvent() {}

    /// Construct a CanvasEvent wrapping a GdkEvent.
    CanvasEvent(GdkEventUniqPtr original) : _original(std::move(original)) {}

    /// Return the dynamic type of the CanvasEvent.
    virtual EventType type() const = 0;

    /// Return a deep copy of the CanvasEvent.
    virtual std::unique_ptr<CanvasEvent> clone() const = 0;

    /// Access the wrapped GdkEvent. Avoid if possible - we want to get rid of this!
    GdkEvent *original() const { return _original.get(); }

protected:
    GdkEventUniqPtr _original;
};

/**
 * Abstract event for mouse button (left/right/middle). May also
 * be used for touch interactions.
 */
class ButtonEvent : public CanvasEvent
{
public:
    ButtonEvent(GdkEventUniqPtr original) : CanvasEvent(std::move(original)) {}

    GdkEventButton *original() const { return reinterpret_cast<GdkEventButton*>(_original.get()); }

    double eventX() const { return _original->button.x; }
    double eventY() const { return _original->button.y; }
    Geom::Point eventPos() const { return { eventX(), eventY() }; }
    unsigned modifiers() const { return _original->button.state; }
    unsigned button() const { return _original->button.button; }
};

/**
 * A mouse button (left/right/middle) is pressed. May also
 * be used for touch interactions.
 */
class ButtonPressEvent : public ButtonEvent
{
public:
    ButtonPressEvent(GdkEventUniqPtr original, int n_press) : ButtonEvent(std::move(original)), _n_press(n_press) {}

    EventType type() const override { return EventType::BUTTON_PRESS; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<ButtonPressEvent>(*this); }

    int numPress() const { return _n_press; };

private:
    int _n_press;
};

/**
 * A mouse button (left/right/middle) is released. May also
 * be used for touch interactions.
 */
class ButtonReleaseEvent : public ButtonEvent
{
public:
    ButtonReleaseEvent(GdkEventUniqPtr original) : ButtonEvent(std::move(original)) {}

    EventType type() const override { return EventType::BUTTON_RELEASE; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<ButtonReleaseEvent>(*this); }
};

/**
 * A key has been pressed.
 */
class KeyEvent : public CanvasEvent
{
public:
    KeyEvent(GdkEventUniqPtr original) : CanvasEvent(std::move(original)) {}

    GdkEventKey *original() const { return reinterpret_cast<GdkEventKey*>(_original.get()); }

    uint8_t group() const { return _original->key.group; }
    uint16_t hardwareKeycode() const { return _original->key.hardware_keycode; }
    uint32_t keyval() const { return _original->key.keyval; }
    unsigned modifiers() const { return _original->key.state; }
    uint32_t time() const { return _original->key.time; }
};

/**
 * A key has been pressed.
 */
class KeyPressEvent : public KeyEvent
{
public:
    KeyPressEvent(GdkEventUniqPtr original) : KeyEvent(std::move(original)) {}

    EventType type() const override { return EventType::KEY_PRESS; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<KeyPressEvent>(*this); }
};

/**
 * A key has been released.
 */
class KeyReleaseEvent : public KeyEvent
{
public:
    KeyReleaseEvent(GdkEventUniqPtr original) : KeyEvent(std::move(original)) {}

    EventType type() const override { return EventType::KEY_RELEASE; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<KeyReleaseEvent>(*this); }
};

/**
 * The pointer has moved, entered, or exited a widget or item.
 */
class PointerEvent : public CanvasEvent
{
public:
    PointerEvent(GdkEventUniqPtr original, unsigned state) : CanvasEvent(std::move(original)), _state(state) {}

    unsigned modifiers() const { return _state; }

protected:
    unsigned _state;
};

/**
 * Movement of the mouse pointer. May also be used
 * for touch interactions.
 */
class MotionEvent : public PointerEvent
{
public:
    MotionEvent(GdkEventUniqPtr original, unsigned state) : PointerEvent(std::move(original), state) {}

    EventType type() const override { return EventType::MOTION; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<MotionEvent>(*this); }

    GdkEventMotion *original() const { return reinterpret_cast<GdkEventMotion*>(_original.get()); }

    double eventX() const { return _original->motion.x; }
    double eventY() const { return _original->motion.y; }
    Geom::Point eventPos() const { return { eventX(), eventY() }; }
};

/**
 * The pointer has entered a widget or item.
 */
class EnterEvent : public PointerEvent
{
public:
    EnterEvent(GdkEventUniqPtr original, unsigned state) : PointerEvent(std::move(original), state) {}

    EventType type() const override { return EventType::ENTER; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<EnterEvent>(*this); }

    GdkEventCrossing *original() const { return reinterpret_cast<GdkEventCrossing*>(_original.get()); }

    double eventX() const { return _original->crossing.x; }
    double eventY() const { return _original->crossing.y; }
    Geom::Point eventPos() const { return { eventX(), eventY() }; }
};

/**
 * The pointer has exited a widget or item.
 *
 * Note the coordinates will always be (0, 0) for this event.
 */
class LeaveEvent : public PointerEvent
{
public:
    LeaveEvent(GdkEventUniqPtr original, unsigned state) : PointerEvent(std::move(original), state) {}

    EventType type() const override { return EventType::LEAVE; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<LeaveEvent>(*this); }

    GdkEventCrossing *original() const { return reinterpret_cast<GdkEventCrossing*>(_original.get()); }
};

/**
 * Scroll the item or widget by the provided amount
 */
class ScrollEvent : public CanvasEvent
{
public:
    ScrollEvent(GdkEventUniqPtr original) : CanvasEvent(std::move(original)) {}

    EventType type() const override { return EventType::SCROLL; }
    std::unique_ptr<CanvasEvent> clone() const override { return std::make_unique<ScrollEvent>(*this); }

    GdkEventScroll *original() const { return reinterpret_cast<GdkEventScroll*>(_original.get()); }

    unsigned modifiers() const { return _original->scroll.state; }
    double deltaX() const { return _original->scroll.delta_x; }
    double deltaY() const { return _original->scroll.delta_y; }
    Geom::Point delta() const { return { deltaX(), deltaY() }; }
    GdkScrollDirection direction() const { return _original->scroll.direction; }
};

} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_EVENTS_CANVAS_EVENT_H
