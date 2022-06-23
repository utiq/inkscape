// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KnotHolderEntity definition.
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2001 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2001 Mitsuru Oka
 * Copyright (C) 2004 Monash University
 * Copyright (C) 2008 Maximilian Albert
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "knot-holder-entity.h"

#include "desktop.h"
#include "display/control/canvas-item-ctrl.h"
#include "display/control/canvas-item-quad.h"
#include "inkscape.h"
#include "knot-holder.h"
#include "live_effects/effect.h"
#include "object/sp-hatch.h"
#include "object/sp-item.h"
#include "object/sp-marker.h"
#include "object/sp-namedview.h"
#include "object/sp-pattern.h"
#include "preferences.h"
#include "snap.h"
#include "style.h"

void KnotHolderEntity::create(SPDesktop *desktop, SPItem *item, KnotHolder *parent,
                              Inkscape::CanvasItemCtrlType type,
                              Glib::ustring const & name,
                              const gchar *tip, guint32 color)
{
    if (!desktop) {
        desktop = parent->getDesktop();
    }

    g_assert(item == parent->getItem());
    g_assert(desktop && desktop == parent->getDesktop());
    g_assert(knot == nullptr);

    parent_holder = parent;
    this->item = item; // TODO: remove the item either from here or from knotholder.cpp
    this->desktop = desktop;

    my_counter = KnotHolderEntity::counter++;

    knot = new SPKnot(desktop, tip, type, name);
    knot->fill [SP_KNOT_STATE_NORMAL] = color;
    knot->ctrl->set_fill(color);
    on_created();
    update_knot();
    knot->show();

    _mousedown_connection = knot->mousedown_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_mousedown_handler));
    _moved_connection = knot->moved_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_moved_handler));
    _click_connection = knot->click_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_clicked_handler));
    _ungrabbed_connection = knot->ungrabbed_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_ungrabbed_handler));
}
                              
KnotHolderEntity::~KnotHolderEntity()
{
    _mousedown_connection.disconnect();
    _moved_connection.disconnect();
    _click_connection.disconnect();
    _ungrabbed_connection.disconnect();

    /* unref should call destroy */
    if (knot) {
        //g_object_unref(knot);
        knot_unref(knot);
    } else {
        // FIXME: This shouldn't occur. Perhaps it is caused by LPE PointParams being knotholder entities, too
        //        If so, it will likely be fixed with upcoming refactoring efforts.
        g_return_if_fail(knot);
    }
}

void
KnotHolderEntity::update_knot()
{
    Geom::Point knot_pos(knot_get());
    if (knot_pos.isFinite()) {
        Geom::Point dp(knot_pos * parent_holder->getEditTransform() * item->i2dt_affine());

        _moved_connection.block();
        knot->setPosition(dp, SP_KNOT_STATE_NORMAL);
        _moved_connection.unblock();
    } else {
        // knot coords are non-finite, hide knot
        knot->hide();
    }
}

Geom::Point
KnotHolderEntity::snap_knot_position(Geom::Point const &p, guint state)
{
    if (state & GDK_SHIFT_MASK) { // Don't snap when shift-key is held
        return p;
    }

    Geom::Affine const i2dt (parent_holder->getEditTransform() * item->i2dt_affine());
    Geom::Point s = p * i2dt;

    if (!desktop) std::cout << "No desktop" << std::endl;
    if (!desktop->namedview) std::cout << "No named view" << std::endl;
    SnapManager &m = desktop->namedview->snap_manager;
    m.setup(desktop, true, item);
    m.freeSnapReturnByRef(s, Inkscape::SNAPSOURCE_NODE_HANDLE);
    m.unSetup();

    return s * i2dt.inverse();
}

Geom::Point
KnotHolderEntity::snap_knot_position_constrained(Geom::Point const &p, Inkscape::Snapper::SnapConstraint const &constraint, guint state)
{
    if (state & GDK_SHIFT_MASK) { // Don't snap when shift-key is held
        return p;
    }

    Geom::Affine const i2d (parent_holder->getEditTransform() * item->i2dt_affine());
    Geom::Point s = p * i2d;

    SnapManager &m = desktop->namedview->snap_manager;
    m.setup(desktop, true, item);

    // constrainedSnap() will first project the point p onto the constraint line and then try to snap along that line.
    // This way the constraint is already enforced, no need to worry about that later on
    Inkscape::Snapper::SnapConstraint transformed_constraint = Inkscape::Snapper::SnapConstraint(constraint.getPoint() * i2d, (constraint.getPoint() + constraint.getDirection()) * i2d - constraint.getPoint() * i2d);
    m.constrainedSnapReturnByRef(s, Inkscape::SNAPSOURCE_NODE_HANDLE, transformed_constraint);
    m.unSetup();

    return s * i2d.inverse();
}

void
LPEKnotHolderEntity::knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    Inkscape::LivePathEffect::Effect *effect = _effect;
    if (effect) {
        effect->refresh_widgets = true;
        effect->writeParamsToSVG();
    }
}

/* Pattern manipulation */

/*  TODO: this pattern manipulation is not able to handle general transformation matrices. Only matrices that are the result of a pure scale times a pure rotation. */

void PatternKnotHolderEntity::on_created()
{
    // Setup an initial pattern transformation in the center
    if (auto rect = item->documentGeometricBounds()) {
        set_offset(rect->midpoint());
    }
}

/**
 * Returns the position based on the pattern's origin, shifted by the percent x/y of it's size.
 */
Geom::Point PatternKnotHolderEntity::_get_pos(gdouble x, gdouble y) const
{
    auto pat = _pattern();
    auto scale = Geom::Scale(pat->width(), pat->height());
    return Geom::Point(_cell[Geom::X] + x, _cell[Geom::Y] + y) * scale * pat->getTransform();
}

bool PatternKnotHolderEntity::set_item_clickpos(Geom::Point loc)
{
    set_offset(loc);
    update_knot();
    return true;
}

void PatternKnotHolderEntity::set_offset(Geom::Point loc)
{
    auto pat = _pattern();

    // 1. Turn the location into the pattern grid coordinate
    auto scale = Geom::Scale(pat->width(), pat->height());
    auto d2i = item->i2doc_affine().inverse();
    auto i2p = pat->getTransform().inverse();

    // Get grid index of nearest pattern repetition.
    _cell = (loc * d2i * i2p * scale.inverse()).floor();
}

SPPattern *PatternKnotHolderEntity::_pattern() const
{
    return _fill ? SP_PATTERN(item->style->getFillPaintServer()) : SP_PATTERN(item->style->getStrokePaintServer());
}

bool PatternKnotHolderEntity::knot_missing() const
{
    return (_pattern() == nullptr);
}

/* Pattern X/Y knot */

PatternKnotHolderEntityXY::~PatternKnotHolderEntityXY()
{
    delete _quad;
}

void PatternKnotHolderEntityXY::on_created()
{
    PatternKnotHolderEntity::on_created();
    // TODO: Move to constructor when desktop is generally available
    _quad = new Inkscape::CanvasItemQuad(desktop->getCanvasControls());
    _quad->set_fill(0x00000000);
    _quad->set_stroke(0x808080ff);
    _quad->set_inverted(true);
    _quad->hide();
}

void PatternKnotHolderEntityXY::update_knot()
{
    KnotHolderEntity::update_knot();
    auto tr = item->i2doc_affine();
    _quad->set_coords(_get_pos(0, 0) * tr, _get_pos(0, 1) * tr,
                      _get_pos(1, 1) * tr, _get_pos(1, 0) * tr);
    _quad->show();
}

Geom::Point PatternKnotHolderEntityXY::knot_get() const
{
    return _get_pos(0, 0);
}

void
PatternKnotHolderEntityXY::knot_set(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    if ( state & GDK_CONTROL_MASK ) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state)  {
        Geom::Point const q = p_snapped - knot_get();
        item->adjust_pattern(Geom::Translate(q), false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Pattern Angle knot */

Geom::Point PatternKnotHolderEntityAngle::knot_get() const
{
    return _get_pos(1.0, 0);
}

void
PatternKnotHolderEntityAngle::knot_set(Geom::Point const &p, Geom::Point const &/*origin*/, guint state)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);

    // get the angle from pattern 0,0 to the cursor pos
    Geom::Point transform_origin = _get_pos(0, 0);
    gdouble theta = atan2(p - transform_origin);
    gdouble theta_old = atan2(knot_get() - transform_origin);

    if ( state & GDK_CONTROL_MASK ) {
        /* Snap theta */
        double snaps_radian = M_PI/snaps;
        theta = std::round(theta/snaps_radian) * snaps_radian;
    }

    Geom::Affine rot = Geom::Translate(-transform_origin)
                     * Geom::Rotate(theta - theta_old)
                     * Geom::Translate(transform_origin);
    item->adjust_pattern(rot, false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Pattern scale knot */

Geom::Point PatternKnotHolderEntityScale::knot_get() const
{
    return _get_pos(1.0, 1.0);
}

void
PatternKnotHolderEntityScale::knot_set(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    Geom::Point p_origin = _get_pos(0.0, 0.0);
    Geom::Point p_position = _get_pos(1.0, 1.0);
    Geom::Point size = p_position - p_origin;
    Geom::Point p_offset = p_snapped - p_origin;

    // 1. Calculate absolute scale factor first
    auto scale = Geom::Scale(p_offset[Geom::X] / size[Geom::X], p_offset[Geom::Y] / size[Geom::Y]);

    if ( state & GDK_CONTROL_MASK ) {
        // if ctrl is pressed: use 1:1 scaling
        auto i = Geom::Interval(scale.vector()[Geom::X], scale.vector()[Geom::Y]);
        scale = Geom::Scale(i.middle());
    }

    // 2. Calculate offset to keep pattern origin aligned
    Geom::Affine offset = Geom::Translate(p_origin - (p_origin * scale));

    SPPattern *pat = _pattern();
    Geom::Affine rot = pat->getTransform() * scale * offset;
    item->adjust_pattern(rot, true, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Hatch manipulation */
bool HatchKnotHolderEntity::knot_missing() const
{
    SPHatch *hatch = _hatch();
    return (hatch == nullptr);
}

SPHatch *HatchKnotHolderEntity::_hatch() const
{
    return _fill ? SP_HATCH(item->style->getFillPaintServer()) : SP_HATCH(item->style->getStrokePaintServer());
}

static Geom::Point sp_hatch_knot_get(SPHatch const *hatch, gdouble x, gdouble y)
{
    return Geom::Point(x, y) * hatch->hatchTransform();
}

Geom::Point HatchKnotHolderEntityXY::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, 0, 0);
}

Geom::Point HatchKnotHolderEntityAngle::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, hatch->pitch(), 0);
}

Geom::Point HatchKnotHolderEntityScale::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, hatch->pitch(), hatch->pitch());
}

void HatchKnotHolderEntityXY::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    Geom::Point p_snapped = snap_knot_position(p, state);

    if (state & GDK_CONTROL_MASK) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state) {
        Geom::Point const q = p_snapped - knot_get();
        item->adjust_hatch(Geom::Translate(q), false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void HatchKnotHolderEntityAngle::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);

    SPHatch *hatch = _hatch();

    // get the angle from hatch 0,0 to the cursor pos
    Geom::Point transform_origin = sp_hatch_knot_get(hatch, 0, 0);
    gdouble theta = atan2(p - transform_origin);
    gdouble theta_old = atan2(knot_get() - transform_origin);

    if (state & GDK_CONTROL_MASK) {
        /* Snap theta */
        double snaps_radian = M_PI/snaps;
        theta = std::round(theta/snaps_radian) * snaps_radian;
    }

    Geom::Affine rot =
        Geom::Translate(-transform_origin) * Geom::Rotate(theta - theta_old) * Geom::Translate(transform_origin);
    item->adjust_hatch(rot, false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void HatchKnotHolderEntityScale::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    SPHatch *hatch = _hatch();

    // FIXME: this snapping should be done together with knowing whether control was pressed.
    // If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    // Get the new scale from the position of the knotholder
    Geom::Affine transform = hatch->hatchTransform();
    Geom::Affine transform_inverse = transform.inverse();
    Geom::Point d = p_snapped * transform_inverse;
    Geom::Point d_origin = origin * transform_inverse;
    Geom::Point origin_dt;
    gdouble hatch_pitch = hatch->pitch();
    if (state & GDK_CONTROL_MASK) {
        // if ctrl is pressed: use 1:1 scaling
        d = d_origin * (d.length() / d_origin.length());
    }

    Geom::Affine scale = Geom::Translate(-origin_dt) * Geom::Scale(d.x() / hatch_pitch, d.y() / hatch_pitch) *
                         Geom::Translate(origin_dt) * transform;

    item->adjust_hatch(scale, true, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Filter manipulation */
void FilterKnotHolderEntity::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    if ( state & GDK_CONTROL_MASK ) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state)  {
        SPFilter *filter = (item->style) ? item->style->getFilter() : nullptr;
        if(!filter) return;
        Geom::OptRect orig_bbox = item->visualBounds();
        std::unique_ptr<Geom::Rect> new_bbox(_topleft ? new Geom::Rect(p,orig_bbox->max()) : new Geom::Rect(orig_bbox->min(), p));

        if (!filter->width._set) {
            filter->width.set(SVGLength::PERCENT, 1.2);
        }
        if (!filter->height._set) {
            filter->height.set(SVGLength::PERCENT, 1.2);
        }
        if (!filter->x._set) {
            filter->x.set(SVGLength::PERCENT, -0.1);
        }
        if (!filter->y._set) {
            filter->y.set(SVGLength::PERCENT, -0.1);
        }

        if(_topleft) {
            float x_a = filter->width.computed;
            float y_a = filter->height.computed;
            filter->height.scale(new_bbox->height()/orig_bbox->height());
            filter->width.scale(new_bbox->width()/orig_bbox->width());
            float x_b = filter->width.computed;
            float y_b = filter->height.computed;
            filter->x.set(filter->x.unit, filter->x.computed + x_a - x_b);
            filter->y.set(filter->y.unit, filter->y.computed + y_a - y_b);
        } else {
            filter->height.scale(new_bbox->height()/orig_bbox->height());
            filter->width.scale(new_bbox->width()/orig_bbox->width());
        }
        filter->auto_region = false;
        filter->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);

    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

Geom::Point FilterKnotHolderEntity::knot_get() const
{
    SPFilter *filter = (item->style) ? item->style->getFilter() : nullptr;
    if(!filter) return Geom::Point(Geom::infinity(), Geom::infinity());
    Geom::OptRect r = item->visualBounds();
    if (_topleft) return Geom::Point(r->min());
    else return Geom::Point(r->max());
}

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
