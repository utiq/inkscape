// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A tool for building shapes.
 */
/* Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include <cstring>
#include <string>

#include <gtkmm/widget.h>
#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "include/macros.h"
#include "message-stack.h"
#include "rubberband.h"
#include "selection-chemistry.h"
#include "selection-describer.h"
#include "selection.h"
#include "seltrans.h"

#include "actions/actions-tools.h" // set_active_tool()

#include "display/drawing-item.h"
#include "display/control/canvas-item-catchall.h"
#include "display/control/canvas-item-drawing.h"

#include "object/box3d.h"
#include "style.h"

#include "ui/cursor-utils.h"
#include "ui/modifiers.h"

#include "ui/tools/builder-tool.h"

#include "ui/widget/canvas.h"

#include "ui/toolbar/builder-toolbar.h"

#ifdef WITH_DBUS
#include "extension/dbus/document-interface.h"
#endif

// TODO refactor the duplication between this tool and the selector tool.
// TODO break the methods below into smaller and more descriptive methods.

using Inkscape::DocumentUndo;
using Inkscape::Modifiers::Modifier;

namespace Inkscape {
namespace UI {
namespace Tools {

using EventHandler = BuilderTool::EventHandler;

static gint rb_escaped = 0; // if non-zero, rubberband was canceled by esc, so the next button release should not deselect
static gint drag_escaped = 0; // if non-zero, drag was canceled by esc

const std::string& BuilderTool::getPrefsPath() {
    return BuilderTool::prefsPath;
}

const std::string BuilderTool::prefsPath = "/tools/builder";

const std::vector<std::string> BuilderTool::operation_cursor_filenames = {
    "cursor-union.svg",
    "cursor-delete.svg",
    "cursor-intersect.svg",
    "select.svg",
};

const std::vector<guint32> BuilderTool::operation_colors = {
    0x0000ffff,
    0x000000ff,
    0xff00ffff,
    0xff0000ff,
};

const std::map<GdkEventType, EventHandler> BuilderTool::handlers = {
    {GDK_BUTTON_PRESS, &BuilderTool::event_button_press_handler},
    {GDK_BUTTON_RELEASE, &BuilderTool::event_button_release_handler},
    {GDK_KEY_PRESS, &BuilderTool::event_key_press_handler},
    {GDK_KEY_RELEASE, &BuilderTool::event_key_release_handler},
    {GDK_MOTION_NOTIFY, &BuilderTool::event_motion_handler},
};

BuilderTool::BuilderTool()
    : ToolBase("select.svg")
    , dragging(false)
    , moved(false)
    , button_press_state(0)
    , item(nullptr)
    , _seltrans(nullptr)
    , _describer(nullptr)
{
}

BuilderTool::~BuilderTool() {
    this->enableGrDrag(false);

    if (grabbed) {
        grabbed->ungrab();
        grabbed = nullptr;
    }

    delete this->_seltrans;
    this->_seltrans = nullptr;

    delete this->_describer;
    this->_describer = nullptr;
    g_free(no_selection_msg);

    if (item) {
        sp_object_unref(item);
        item = nullptr;
    }

    forced_redraws_stop();

    end_interactive_mode();
}

void BuilderTool::setup() {
    ToolBase::setup();

    auto select_click = Modifier::get(Modifiers::Type::SELECT_ADD_TO)->get_label();
    auto select_scroll = Modifier::get(Modifiers::Type::SELECT_CYCLE)->get_label();

    no_selection_msg = g_strdup_printf(
        _("No objects selected. Click, %s+click, %s+scroll mouse on top of objects, or drag around objects to select."),
        select_click.c_str(), select_scroll.c_str());

    this->_describer = new Inkscape::SelectionDescriber(
        desktop->selection,
        desktop->messageStack(),
        _("Click selection again to toggle scale/rotation handles"),
        no_selection_msg);

    this->_seltrans = new Inkscape::SelTrans(desktop);

    sp_event_context_read(this, "show");
    sp_event_context_read(this, "transform");

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (prefs->getBool("/tools/select/gradientdrag")) {
        this->enableGrDrag();
    }

    set_current_operation();

    start_interactive_mode();
}

void BuilderTool::set(const Inkscape::Preferences::Entry& val) {
    Glib::ustring path = val.getEntryName();

    if (path == "show") {
        if (val.getString() == "outline") {
            this->_seltrans->setShow(Inkscape::SelTrans::SHOW_OUTLINE);
        } else {
            this->_seltrans->setShow(Inkscape::SelTrans::SHOW_CONTENT);
        }
    }
}

bool BuilderTool::sp_select_context_abort() {
    if (in_interactive_mode()) {
        desktop->getSelection()->deactivate();
    }
    if (Inkscape::Rubberband::get(desktop)->is_started()) {
        Inkscape::Rubberband::get(desktop)->stop();
        rb_escaped = 1;
        defaultMessageContext()->clear();
        desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Selection canceled."));
        return true;
    }
    return false;
}

static bool
key_is_a_modifier (guint key) {
    return (key == GDK_KEY_Alt_L ||
            key == GDK_KEY_Alt_R ||
            key == GDK_KEY_Control_L ||
            key == GDK_KEY_Control_R ||
            key == GDK_KEY_Shift_L ||
            key == GDK_KEY_Shift_R ||
            key == GDK_KEY_Meta_L ||  // Meta is when you press Shift+Alt (at least on my machine)
            key == GDK_KEY_Meta_R);
}

bool BuilderTool::item_handler(SPItem* item, GdkEvent* event)
{
    // TODO consider the case for when the ENTER_NOTIFY (to set a pattern).
    return root_handler(event);
}

EventHandler BuilderTool::get_event_handler(GdkEvent* event)
{
    auto handler = handlers.find(event->type);
    if (handler != handlers.end()) {
        return handler->second; // first is the key
    }
    return nullptr;
}

bool BuilderTool::root_handler(GdkEvent* event) {

    // make sure we still have valid objects to move around
    if (this->item && this->item->document == nullptr) {
        this->sp_select_context_abort();
    }

    forced_redraws_start(5);

    bool ret = false;

    auto handler = get_event_handler(event);
    if (handler) {
        ret = (this->*handler)(event);
    }

    if (!ret) {
        ret = ToolBase::root_handler(event);
    }

    return ret;
}

bool BuilderTool::event_button_press_handler(GdkEvent *event)
{
    if (event->button.button == 1) {

        // save drag origin
        xp = (gint) event->button.x;
        yp = (gint) event->button.y;
        within_tolerance = true;

        Geom::Point const button_pt(event->button.x, event->button.y);
        Geom::Point const p(desktop->w2d(button_pt));

        int current_operation = get_current_operation();
        guint32 current_color = operation_colors[current_operation];
        Inkscape::Rubberband::get(desktop)->setColor(current_color);

        Inkscape::Rubberband::get(desktop)->setMode(RUBBERBAND_MODE_TOUCHPATH);
        Inkscape::Rubberband::get(desktop)->start(desktop, p);

        if (this->grabbed) {
            grabbed->ungrab();
            this->grabbed = nullptr;
        }

        grabbed = desktop->getCanvasCatchall();
        grabbed->grab(Gdk::KEY_PRESS_MASK      |
                      Gdk::KEY_RELEASE_MASK    |
                      Gdk::BUTTON_PRESS_MASK   |
                      Gdk::BUTTON_RELEASE_MASK |
                      Gdk::POINTER_MOTION_MASK );

        // remember what modifiers were on before button press
        this->button_press_state = event->button.state;

        this->moved = false;

        rb_escaped = drag_escaped = 0;

        return true;

    } else if (event->button.button == 3) {
        // right click; do not eat it so that right-click menu can appear, but cancel dragging & rubberband
        this->sp_select_context_abort();
    }

    return false;
}

bool BuilderTool::event_button_release_handler(GdkEvent *event)
{
    xp = yp = 0;
    Inkscape::Selection *selection = desktop->getSelection();


    if ((event->button.button == 1) && (this->grabbed)) {

        Inkscape::Rubberband *r = Inkscape::Rubberband::get(desktop);

        if (r->is_started() && !within_tolerance) {
            // this was a rubberband drag
            std::vector<SPItem*> items;

            if (r->getMode() == RUBBERBAND_MODE_RECT) {
                Geom::OptRect const b = r->getRectangle();
                items = desktop->getDocument()->getItemsInBox(desktop->dkey, (*b) * desktop->dt2doc());
            } else if (r->getMode() == RUBBERBAND_MODE_TOUCHRECT) {
                Geom::OptRect const b = r->getRectangle();
                items = desktop->getDocument()->getItemsPartiallyInBox(desktop->dkey, (*b) * desktop->dt2doc());
            } else if (r->getMode() == RUBBERBAND_MODE_TOUCHPATH) {
                items = desktop->getDocument()->getItemsAtPoints(desktop->dkey, r->getPoints(), true, false);
            }

            _seltrans->resetState();
            r->stop();
            this->defaultMessageContext()->clear();

            int operation = get_current_operation();

            if(is_operation_add_to_selection(operation, event)) {
                selection->addList (items);
            } else {
                if (in_interactive_mode()) selection->activate();
                selection->setList (items);
                perform_operation(selection, operation);
                if (in_interactive_mode()) selection->deactivate();
            }

        } else { // it was just a click, or a too small rubberband
            r->stop();

            int operation = get_current_operation();

            if (operation == JUST_SELECT && !is_operation_add_to_selection(operation, event)) {
                selection->clear();
            }

            bool in_groups = Modifier::get(Modifiers::Type::SELECT_IN_GROUPS)->active(event->button.state);

            auto item = sp_event_context_find_item(desktop, Geom::Point(event->button.x, event->button.y), false, in_groups);
            if (item) {
                if (in_interactive_mode()) selection->activate();
                selection->add(item);
                perform_operation(selection, operation);
                if (in_interactive_mode()) selection->deactivate();
            } else {
                // clicked in an empty area
                selection->clear();
            }
        }
    }
    if (grabbed) {
        grabbed->ungrab();
        grabbed = nullptr;
    }

    if (event->button.button == 1) {
        Inkscape::Rubberband::get(desktop)->stop(); // might have been started in another tool!
    }

    this->button_press_state = 0;

    return true;
}

bool BuilderTool::event_motion_handler(GdkEvent *event)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

    if ((event->motion.state & GDK_BUTTON1_MASK)) {
        Geom::Point const motion_pt(event->motion.x, event->motion.y);
        Geom::Point const p(desktop->w2d(motion_pt));
        if ( within_tolerance
             && ( abs( (gint) event->motion.x - xp ) < tolerance )
             && ( abs( (gint) event->motion.y - yp ) < tolerance ) ) {
            return false; // do not drag if we're within tolerance from origin
        }
        // Once the user has moved farther than tolerance from the original location
        // (indicating they intend to move the object, not click), then always process the
        // motion notify coordinates as given (no snapping back to origin)
        within_tolerance = false;

        if (Inkscape::Rubberband::get(desktop)->is_started()) {
            Inkscape::Rubberband::get(desktop)->move(p);

            auto touch_path = Modifier::get(Modifiers::Type::SELECT_TOUCH_PATH)->get_label();
            auto operation = Inkscape::Rubberband::get(desktop)->getMode();
            if (operation == RUBBERBAND_MODE_TOUCHPATH) {
                this->defaultMessageContext()->setF(Inkscape::NORMAL_MESSAGE,
                                                    _("<b>Draw over</b> objects to select them; release <b>%s</b> to switch to rubberband selection"), touch_path.c_str());
            } else if (operation == RUBBERBAND_MODE_TOUCHRECT) {
                this->defaultMessageContext()->setF(Inkscape::NORMAL_MESSAGE,
                                                    _("<b>Drag near</b> objects to select them; press <b>%s</b> to switch to touch selection"), touch_path.c_str());
            } else {
                this->defaultMessageContext()->setF(Inkscape::NORMAL_MESSAGE,
                                                    _("<b>Drag around</b> objects to select them; press <b>%s</b> to switch to touch selection"), touch_path.c_str());
            }

            gobble_motion_events(GDK_BUTTON1_MASK);
        }
    }

    return false;
}

bool BuilderTool::event_key_press_handler(GdkEvent *event)
{
    set_current_operation(event);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Inkscape::Selection *selection = desktop->getSelection();

    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);
    auto const y_dir = desktop->yaxisdir();

    bool ret = false;
    switch (get_latin_keyval (&event->key)) {
        case GDK_KEY_Escape:
            if (!this->sp_select_context_abort()) {
                selection->clear();
            }

            ret = true;
            break;

        case GDK_KEY_a:
        case GDK_KEY_A:
            if (MOD__CTRL_ONLY(event)) {
                sp_edit_select_all(desktop);
                ret = true;
            }
            break;

        case GDK_KEY_space:
            /* stamping operation: show outline operation moving */
            /* FIXME: Is next condition ok? (lauris) */
            if (this->dragging && this->grabbed) {
                _seltrans->stamp();
                ret = true;
            }
            break;

        case GDK_KEY_bracketleft:
            if (MOD__ALT(event)) {
                gint mul = 1 + gobble_key_events(get_latin_keyval(&event->key), 0); // with any mask
                selection->rotateScreen(-mul * y_dir);
            } else if (MOD__CTRL(event)) {
                selection->rotate(-90 * y_dir);
            } else if (snaps) {
                selection->rotate(-180.0/snaps * y_dir);
            }

            ret = true;
            break;

        case GDK_KEY_bracketright:
            if (MOD__ALT(event)) {
                gint mul = 1 + gobble_key_events(get_latin_keyval(&event->key), 0); // with any mask
                selection->rotateScreen(mul * y_dir);
            } else if (MOD__CTRL(event)) {
                selection->rotate(90 * y_dir);
            } else if (snaps) {
                selection->rotate(180.0/snaps * y_dir);
            }

            ret = true;
            break;

        case GDK_KEY_s:
        case GDK_KEY_S:
            if (MOD__SHIFT_ONLY(event)) {
                if (!selection->isEmpty()) {
                    _seltrans->increaseState();
                }

                ret = true;
            }
            break;

        case GDK_KEY_g:
        case GDK_KEY_G:
            if (MOD__SHIFT_ONLY(event)) {
                desktop->selection->toGuides();
                ret = true;
            }
            break;

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (ctrl_on && in_interactive_mode()) {
                shapes_builder.undo();
                ret = true;
            }
            break;

        case GDK_KEY_y:
        case GDK_KEY_Y:
            if (ctrl_on && in_interactive_mode()) {
                shapes_builder.redo();
                ret = true;
            }
            break;

        default:
            break;
    }

    return ret;
}

bool BuilderTool::event_key_release_handler(GdkEvent *event)
{
    set_current_operation(event);
    guint keyval = get_latin_keyval(&event->key);
    if (key_is_a_modifier (keyval)) {
        this->defaultMessageContext()->clear();
    }

    return false;
}

void BuilderTool::perform_operation(Selection *selection, int operation)
{
    int size = selection->size();

    if (shapes_builder.is_started()) {
        if (operation == SELECT_AND_UNION) {
            shapes_builder.set_union(selection);
        } else if (operation == SELECT_AND_DELETE) {
            shapes_builder.set_delete(selection);
        }
        return;
    }

    if (operation != JUST_SELECT && size > 1) {
        if (operation == SELECT_AND_UNION) {
            selection->pathUnion();
        } else if (operation == SELECT_AND_DELETE) {
            selection->pathDiff();
        } else if (operation == SELECT_AND_INTERSECT) {
            selection->pathIntersect();
        }
        selection->clear();
    }
}

void BuilderTool::perform_current_operation(Selection *selection)
{
    int operation = get_current_operation();
    return perform_operation(selection, operation);
}

void BuilderTool::set_modifiers_state(GdkEvent* event)
{
    // TODO This function is deprecated.
    GdkModifierType modifiers;
    gdk_window_get_pointer(gdk_event_get_window(event), nullptr, nullptr, &modifiers);

    alt_on = modifiers & GDK_MOD1_MASK;
    ctrl_on = modifiers & INK_GDK_PRIMARY_MASK;
    shift_on = modifiers & GDK_SHIFT_MASK;
}

int BuilderTool::get_current_operation()
{
    if (ctrl_on) {
        if (alt_on && !in_interactive_mode()) return SELECT_AND_INTERSECT;
        return SELECT_AND_UNION;
    }
    if (alt_on) return SELECT_AND_DELETE;
    if (shift_on && !in_interactive_mode()) return JUST_SELECT;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (in_interactive_mode()) {
        return prefs->getInt("/tools/builder/interactive_operation", 0);
    } else {
        return prefs->getInt("/tools/builder/normal_operation", 0);
    }
}

void BuilderTool::set_current_operation(int current_operation)
{
    if (current_operation == -1) {
        current_operation = get_current_operation();
    }

    if (current_operation == active_operation) {
        return;
    }

    if (in_interactive_mode() &&
        (current_operation == SELECT_AND_INTERSECT || current_operation == JUST_SELECT)) {
        return;
    }

    active_operation = current_operation;
    set_cursor_operation();
    set_rubberband_color();

    // TODO add a function here to change the
    //  patter of the items the cursor went over.
}

void BuilderTool::set_current_operation(GdkEvent *event)
{
    set_modifiers_state(event);
    set_current_operation();
}

void BuilderTool::set_cursor_operation()
{
    if (active_operation > operation_cursor_filenames.size()) {
        std::cerr << "BuilderTool: operation " << active_operation << " is unknown.\n";
        return;
    }

    auto &current_cursor = operation_cursor_filenames[active_operation];
    ToolBase::cursor_filename = current_cursor;
    ToolBase::sp_event_context_update_cursor();
}

void BuilderTool::set_rubberband_color()
{
    if (active_operation > operation_colors.size()) {
        std::cerr << "BuilderTool: operation " << active_operation << " is unknown.\n";
        return;
    }

    auto instance = Rubberband::get(desktop);
    instance->setColor(operation_colors[active_operation]);
}

bool BuilderTool::is_operation_add_to_selection(int operation, GdkEvent *event)
{
    return operation == JUST_SELECT && Modifier::get(Modifiers::Type::SELECT_ADD_TO)->active(event->button.state);
}

void BuilderTool::start_interactive_mode()
{
//    std::cout << "started start_interactive_mode.\n";

    Inkscape::Selection *selection = desktop->getSelection();

    auto toolbar = desktop->get_toolbar_by_name("BuilderToolbar");
    auto builder_toolbar = dynamic_cast<Toolbar::BuilderToolbar*>(toolbar);

    shapes_builder.start(selection);

    builder_toolbar->notify_back = false;
    if (in_interactive_mode()) {
        desktop->getSelection()->deactivate();
//        std::cout << "Calling BuilderToolbar::set_mode_interactive\n";
        builder_toolbar->set_mode_interactive();
    } else {
//        std::cout << "Calling BuilderToolbar::set_mode_normal\n";
        builder_toolbar->set_mode_normal();
    }
    builder_toolbar->notify_back = true;

//    std::cout << "finished start_interactive_mode.\n";
}

void BuilderTool::end_interactive_mode()
{
    shapes_builder.commit();
    desktop->getSelection()->activate();
    auto toolbar = desktop->get_toolbar_by_name("BuilderToolbar");
    auto builder_toolbar = dynamic_cast<Toolbar::BuilderToolbar*>(toolbar);
    if (builder_toolbar) {
        builder_toolbar->notify_back = false;
        builder_toolbar->set_mode_normal();
        builder_toolbar->notify_back = true;
    }
}

bool BuilderTool::in_interactive_mode() const
{
    return shapes_builder.is_started();
}

void BuilderTool::apply()
{
    if (in_interactive_mode()) {
        end_interactive_mode();
    } else {
        std::cerr << "Applying while not in interactive mode?...\n";
    }
}

void BuilderTool::reset()
{
    if (in_interactive_mode()) {
        shapes_builder.reset();
    } else {
        std::cerr << "Resetting while not in interactive mode?...\n";
    }
}

void BuilderTool::discard()
{
    if (in_interactive_mode()) {
        shapes_builder.discard();
        desktop->getSelection()->activate();
        auto toolbar = desktop->get_toolbar_by_name("BuilderToolbar");
        auto builder_toolbar = dynamic_cast<Toolbar::BuilderToolbar*>(toolbar);
        if (builder_toolbar) {
            builder_toolbar->notify_back = false;
            builder_toolbar->set_mode_normal();
        }
    } else {
        std::cerr << "Discarding while not in interactive mode?...\n";
    }
}

}
}
}
