// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Osama Ahmad
 *
 * Copyright (C) 2022 Authors

 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"  // only include where actually required!
#endif

#include <cstring>
#include <string>
#include <array>

#include "ui/tools/booleans-tool.h"

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

#include "display/control/canvas-item-catchall.h"

#include "style.h"

#include "ui/cursor-utils.h"
#include "ui/icon-names.h"
#include "ui/modifiers.h"

#include "livarot/LivarotDefs.h"
#include "path/path-boolop.h"
#include "helper/geom.h"
#include "helper/geom-pathstroke.h"

// TODO refactor the duplication between this tool and the selector tool.
// TODO break the methods below into smaller and more descriptive methods.

using Inkscape::DocumentUndo;
using Inkscape::Modifiers::Modifier;

namespace Inkscape {
namespace UI {
namespace Tools {

using EventHandler = InteractiveBooleansTool::EventHandler;

static gint rb_escaped = 0; // if non-zero, rubberband was canceled by esc, so the next button release should not deselect
static gint drag_escaped = 0; // if non-zero, drag was canceled by esc

static constexpr std::array<char const*, 4> operation_cursor_filenames = {
    "cursor-union.svg",
    "cursor-delete.svg",
    "cursor-intersect.svg",
    "select.svg",
};

static constexpr std::array<uint32_t, 4> operation_colors = {
    0x0000ffff,
    0x000000ff,
    0xff00ffff,
    0xff0000ff,
};

InteractiveBooleansTool::InteractiveBooleansTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/booleans", "select.svg")
    , dragging(false)
    , moved(false)
    , button_press_state(0)
    , item(nullptr)
    , _seltrans(nullptr)
    , _describer(nullptr)
{
    auto select_click = Modifier::get(Modifiers::Type::SELECT_ADD_TO)->get_label();
    auto select_scroll = Modifier::get(Modifiers::Type::SELECT_CYCLE)->get_label();

    no_selection_msg = g_strdup_printf(
        _("No objects selected. Click, %s+click, %s+scroll mouse on top of objects, or drag around objects to select."),
        select_click.c_str(), select_scroll.c_str());

    this->_describer = new Inkscape::SelectionDescriber(
        _desktop->getSelection(),
        _desktop->messageStack(),
        _("Click selection again to toggle scale/rotation handles"),
        no_selection_msg);

    this->_seltrans = new Inkscape::SelTrans(_desktop);

    sp_event_context_read(this, "show");
    sp_event_context_read(this, "transform");

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (prefs->getBool("/tools/select/gradientdrag")) {
        this->enableGrDrag();
    }

    set_current_operation();
    start_interactive_mode();
}

void InteractiveBooleansTool::set(const Inkscape::Preferences::Entry& val)
{
    Glib::ustring path = val.getEntryName();

    if (path == "show") {
        if (val.getString() == "outline") {
            this->_seltrans->setShow(Inkscape::SelTrans::SHOW_OUTLINE);
        } else {
            this->_seltrans->setShow(Inkscape::SelTrans::SHOW_CONTENT);
        }
    }
}

InteractiveBooleansTool::~InteractiveBooleansTool()
{
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

    end_interactive_mode();
}

bool InteractiveBooleansTool::sp_select_context_abort()
{
    if (Inkscape::Rubberband::get(_desktop)->is_started()) {
        Inkscape::Rubberband::get(_desktop)->stop();
        rb_escaped = 1;
        defaultMessageContext()->clear();
        _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Selection canceled."));
        return true;
    }
    return false;
}

static bool key_is_a_modifier (guint key)
{
    return key == GDK_KEY_Alt_L ||
           key == GDK_KEY_Alt_R ||
           key == GDK_KEY_Control_L ||
           key == GDK_KEY_Control_R ||
           key == GDK_KEY_Shift_L ||
           key == GDK_KEY_Shift_R ||
           key == GDK_KEY_Meta_L ||  // Meta is when you press Shift+Alt (at least on my machine)
           key == GDK_KEY_Meta_R;
}

bool InteractiveBooleansTool::item_handler(SPItem *item, GdkEvent *event)
{
    // TODO consider the case for when the ENTER_NOTIFY (to set a pattern).
    return root_handler(event);
}

EventHandler InteractiveBooleansTool::get_event_handler(GdkEvent *event)
{
    switch (event->type) {
        case GDK_BUTTON_PRESS:   return &InteractiveBooleansTool::event_button_press_handler;
        case GDK_BUTTON_RELEASE: return &InteractiveBooleansTool::event_button_release_handler;
        case GDK_KEY_PRESS:      return &InteractiveBooleansTool::event_key_press_handler;
        case GDK_KEY_RELEASE:    return &InteractiveBooleansTool::event_key_release_handler;
        case GDK_MOTION_NOTIFY:  return &InteractiveBooleansTool::event_motion_handler;
        default:                 return nullptr;
    }
}

bool InteractiveBooleansTool::root_handler(GdkEvent *event)
{
    // make sure we still have valid objects to move around
    if (this->item && this->item->document == nullptr) {
        this->sp_select_context_abort();
    }

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

bool InteractiveBooleansTool::event_button_press_handler(GdkEvent *event)
{
    if (event->button.button == 1) {
        // save drag origin
        xp = (gint) event->button.x;
        yp = (gint) event->button.y;
        within_tolerance = true;

        Geom::Point const button_pt(event->button.x, event->button.y);
        Geom::Point const p(_desktop->w2d(button_pt));

        int current_operation = get_current_operation();
        guint32 current_color = operation_colors[current_operation];
        Inkscape::Rubberband::get(_desktop)->setColor(current_color);

        Inkscape::Rubberband::get(_desktop)->setMode(RUBBERBAND_MODE_TOUCHPATH);
        Inkscape::Rubberband::get(_desktop)->start(_desktop, p);

        if (this->grabbed) {
            grabbed->ungrab();
            this->grabbed = nullptr;
        }

        grabbed = _desktop->getCanvasCatchall();
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

bool InteractiveBooleansTool::event_button_release_handler(GdkEvent *event)
{
    xp = yp = 0;
    Inkscape::Selection *selection = _desktop->getSelection();

    if (event->button.button == 1 && grabbed) {

        Inkscape::Rubberband *r = Inkscape::Rubberband::get(_desktop);

        if (r->is_started() && !within_tolerance) {
            // this was a rubberband drag
            std::vector<SPItem*> items;

            if (r->getMode() == RUBBERBAND_MODE_RECT) {
                Geom::OptRect const b = r->getRectangle();
                items = _desktop->getDocument()->getItemsInBox(_desktop->dkey, (*b) * _desktop->dt2doc());
            } else if (r->getMode() == RUBBERBAND_MODE_TOUCHRECT) {
                Geom::OptRect const b = r->getRectangle();
                items = _desktop->getDocument()->getItemsPartiallyInBox(_desktop->dkey, (*b) * _desktop->dt2doc());
            } else if (r->getMode() == RUBBERBAND_MODE_TOUCHPATH) {
                items = _desktop->getDocument()->getItemsAtPoints(_desktop->dkey, r->getPoints(), true, false);
            }

            _seltrans->resetState();
            r->stop();
            this->defaultMessageContext()->clear();

            int operation = get_current_operation();

            if(is_operation_add_to_selection(operation, event)) {
                selection->addList (items);
            } else {
                selection->setList(items);
                perform_operation(operation);
            }

        } else { // it was just a click, or a too small rubberband
            r->stop();

            int operation = get_current_operation();

            if (operation == JUST_SELECT && !is_operation_add_to_selection(operation, event)) {
                selection->clear();
            }

            bool in_groups = Modifier::get(Modifiers::Type::SELECT_IN_GROUPS)->active(event->button.state);

            auto item = sp_event_context_find_item(_desktop, Geom::Point(event->button.x, event->button.y), false, in_groups);
            if (item) {
                selection->add(item);
                perform_operation(operation);
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
        Inkscape::Rubberband::get(_desktop)->stop(); // might have been started in another tool!
    }

    button_press_state = 0;

    return true;
}

bool InteractiveBooleansTool::event_motion_handler(GdkEvent *event)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

    if ((event->motion.state & GDK_BUTTON1_MASK)) {
        Geom::Point const motion_pt(event->motion.x, event->motion.y);
        Geom::Point const p(_desktop->w2d(motion_pt));
        if ( within_tolerance
             && ( abs( (gint) event->motion.x - xp ) < tolerance )
             && ( abs( (gint) event->motion.y - yp ) < tolerance ) ) {
            return false; // do not drag if we're within tolerance from origin
        }
        // Once the user has moved farther than tolerance from the original location
        // (indicating they intend to move the object, not click), then always process the
        // motion notify coordinates as given (no snapping back to origin)
        within_tolerance = false;

        if (Inkscape::Rubberband::get(_desktop)->is_started()) {
            Inkscape::Rubberband::get(_desktop)->move(p);

            auto touch_path = Modifier::get(Modifiers::Type::SELECT_TOUCH_PATH)->get_label();
            auto operation = Inkscape::Rubberband::get(_desktop)->getMode();
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

bool InteractiveBooleansTool::event_key_press_handler(GdkEvent *event)
{
    set_current_operation(event);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Inkscape::Selection *selection = _desktop->getSelection();

    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);
    auto const y_dir = _desktop->yaxisdir();

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
                sp_edit_select_all(_desktop);
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

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (ctrl_on && in_interactive_mode()) {
                boolean_builder.undo();
                ret = true;
            }
            break;

        case GDK_KEY_y:
        case GDK_KEY_Y:
            if (ctrl_on && in_interactive_mode()) {
                boolean_builder.redo();
                ret = true;
            }
            break;

        default:
            break;
    }

    return ret;
}

bool InteractiveBooleansTool::event_key_release_handler(GdkEvent *event)
{
    set_current_operation(event);
    guint keyval = get_latin_keyval(&event->key);
    if (key_is_a_modifier (keyval)) {
        this->defaultMessageContext()->clear();
    }

    return false;
}

void InteractiveBooleansTool::perform_operation(int operation)
{
    auto selection = _desktop->getSelection();
    int size = selection->size();

    if (boolean_builder.is_started()) {
        if (operation == SELECT_AND_UNION) {
            boolean_builder.set_union(selection);
        } else if (operation == SELECT_AND_DELETE) {
            boolean_builder.set_delete(selection);
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

void InteractiveBooleansTool::perform_current_operation()
{
    return perform_operation(get_current_operation());
}

void InteractiveBooleansTool::set_modifiers_state(GdkEvent *event)
{
    // TODO This function is deprecated.
    GdkModifierType modifiers;
    gdk_window_get_pointer(gdk_event_get_window(event), nullptr, nullptr, &modifiers);

    alt_on = modifiers & GDK_MOD1_MASK;
    ctrl_on = modifiers & INK_GDK_PRIMARY_MASK;
    shift_on = modifiers & GDK_SHIFT_MASK;
}

int InteractiveBooleansTool::get_current_operation()
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

void InteractiveBooleansTool::set_current_operation(int current_operation)
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

    // TODO: Add a function here to change the
    // pattern of the items the cursor went over.
}

void InteractiveBooleansTool::set_current_operation(GdkEvent *event)
{
    set_modifiers_state(event);
    set_current_operation();
}

void InteractiveBooleansTool::set_cursor_operation()
{
    if (active_operation < 0 || active_operation >= operation_cursor_filenames.size()) {
        std::cerr << "InteractiveBooleansTool: operation " << active_operation << " is unknown.\n";
        return;
    }

    set_cursor(operation_cursor_filenames[active_operation]);
}

void InteractiveBooleansTool::set_rubberband_color()
{
    if (active_operation > operation_colors.size()) {
        std::cerr << "InteractiveBooleansTool: operation " << active_operation << " is unknown.\n";
        return;
    }

    auto instance = Rubberband::get(_desktop);
    instance->setColor(operation_colors[active_operation]);
}

bool InteractiveBooleansTool::is_operation_add_to_selection(int operation, GdkEvent *event)
{
    return operation == JUST_SELECT && Modifier::get(Modifiers::Type::SELECT_ADD_TO)->active(event->button.state);
}

void InteractiveBooleansTool::start_interactive_mode()
{
    Inkscape::Selection *selection = _desktop->getSelection();
    boolean_builder.start(selection);

    //auto toolbar = _desktop->get_toolbar_by_name("InteractiveBooleansToolbar");
    //auto builder_toolbar = dynamic_cast<Toolbar::InteractiveBooleansToolbar*>(toolbar);


    /*builder_toolbar->notify_back = false;
    if (in_interactive_mode()) {
        builder_toolbar->set_mode_interactive();
    } else {
        builder_toolbar->set_mode_normal();
    }
    builder_toolbar->notify_back = true;*/

//    std::cout << "finished start_interactive_mode.\n";
}

void InteractiveBooleansTool::end_interactive_mode()
{
    boolean_builder.commit();
    /*auto toolbar = _desktop->get_toolbar_by_name("InteractiveBooleansToolbar");
    auto builder_toolbar = dynamic_cast<Toolbar::InteractiveBooleansToolbar*>(toolbar);
    if (builder_toolbar) {
        builder_toolbar->notify_back = false;
        builder_toolbar->set_mode_normal();
        builder_toolbar->notify_back = true;
    }*/
}

bool InteractiveBooleansTool::in_interactive_mode() const
{
    return boolean_builder.is_started();
}

void InteractiveBooleansTool::apply()
{
    if (in_interactive_mode()) {
        end_interactive_mode();
    } else {
        std::cerr << "Applying while not in interactive mode?...\n";
    }
}

void InteractiveBooleansTool::reset()
{
    if (in_interactive_mode()) {
        boolean_builder.reset();
    } else {
        std::cerr << "Resetting while not in interactive mode?...\n";
    }
}

void InteractiveBooleansTool::discard()
{
    if (in_interactive_mode()) {
        boolean_builder.discard();
        /*auto toolbar = _desktop->get_toolbar_by_name("InteractiveBooleansToolbar");
        auto builder_toolbar = dynamic_cast<Toolbar::InteractiveBooleansToolbar*>(toolbar);
        if (builder_toolbar) {
            builder_toolbar->notify_back = false;
            builder_toolbar->set_mode_normal();
        }*/
    } else {
        std::cerr << "Discarding while not in interactive mode?...\n";
    }
}

void InteractiveBooleansTool::fracture(bool skip_undo)
{
    NonIntersectingPathsBuilder builder(_desktop->getSelection());
    builder.fracture(skip_undo);
}

void InteractiveBooleansTool::flatten(bool skip_undo)
{
    auto sel = _desktop->getSelection();
    sel->toCurves(true);
    sel->ungroup(true);

    struct SubItem
    {
        Geom::PathVector paths;
        SPItem *item;

        bool operator<(const SubItem& other) {
            return sp_item_repr_compare_position_bool(item, other.item);
        }
    };

    int n = sel->size();
    std::vector<SubItem> paths;
    paths.reserve(n);

    for (auto item : sel->items()) {
        paths.push_back({item->combined_pathvector(), item});
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            SubItem *top = &paths[i];
            SubItem *bottom = &paths[j];

            if (*top < *bottom) {
                std::swap(top, bottom);
            }

            auto diff = sp_pathvector_boolop(top->paths, bottom->paths, bool_op_diff, fill_nonZero, fill_nonZero, true);
            if (!diff.empty()) {
                bottom->paths = diff;
            }
        }
    }

    std::vector<XML::Node*> nodes;
    for (int i = 0; i < n; i++) {
        auto split = Inkscape::split_non_intersecting_paths(Geom::PathVector(paths[i].paths)); // todo: eliminate copy
        for (auto pathvec : split) {
            if (!pathvec.empty()) {
                nodes.push_back(write_path_xml(pathvec, paths[i].item)->getRepr());
            }
        }
    }

    sel->deleteItems(true);

    for (int i = 0; i < n; i++) {
        sel->add(nodes[i]);
    }

    if (!skip_undo) {
        if (auto document = _desktop->getDocument()) {
            DocumentUndo::done(document, "Flatten", INKSCAPE_ICON("path-flatten"));
        }
    }
}

void InteractiveBooleansTool::splitNonIntersecting(bool skip_undo)
{
    auto sel = _desktop->getSelection();

    if (sel->isEmpty()) {
        return;
    }

    sel->ungroup();

    std::vector<SPItem*> items_vec(sel->items().begin(), sel->items().end());

    int n = items_vec.size();
    std::vector<XML::Node*> result(n);

    for (int i = 0; i < n; i++) {
        auto pathvec = items_vec[i]->combined_pathvector();
        auto broken = Inkscape::split_non_intersecting_paths(std::move(pathvec));
        for (auto paths : broken) {
            result.push_back(write_path_xml(paths, items_vec[i])->getRepr());
        }
    }

    sel->deleteItems(true);

    for (auto &node : result) {
        sel->add(node);
    }

    if (!skip_undo) {
        if (auto document = _desktop->getDocument()) {
            DocumentUndo::done(document, "Split Non-Intersecting Paths", INKSCAPE_ICON("path-split-non-intersecting"));
        }
    }
}

} // namespace Tools
} // namespace UI
} // namespace Inkscape
