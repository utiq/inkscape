// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utility structures and functions for pdf parsing.
 *//*
 * 
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "pdf-utils.h"

#include "poppler-utils.h"

//------------------------------------------------------------------------
// ClipHistoryEntry
//------------------------------------------------------------------------

ClipHistoryEntry::ClipHistoryEntry(GfxPath *clipPathA, GfxClipType clipTypeA)
    : saved(nullptr)
    , clipPath((clipPathA) ? clipPathA->copy() : nullptr)
    , clipType(clipTypeA)
    , cleared(false)
{}

ClipHistoryEntry::~ClipHistoryEntry()
{
    if (clipPath) {
        delete clipPath;
        clipPath = nullptr;
    }
}

void ClipHistoryEntry::setClip(GfxState *state, GfxClipType clipTypeA)
{
    const GfxPath *clipPathA = state->getPath();
    // Free previous clip path
    if (clipPath) {
        delete clipPath;
    }
    cleared = false;
    if (clipPathA) {
        affine = stateToAffine(state);
        clipPath = clipPathA->copy();
        clipType = clipTypeA;
    } else {
        affine = Geom::identity();
        clipPath = nullptr;
        clipType = clipNormal;
    }
}

/**
 * Create a new clip-history, appending it to the stack.
 *
 * If keep is set to false, it will not remember the current clipping path.
 */
ClipHistoryEntry *ClipHistoryEntry::save(bool cleared)
{
    ClipHistoryEntry *newEntry = new ClipHistoryEntry(this, cleared);
    newEntry->saved = this;
    return newEntry;
}

ClipHistoryEntry *ClipHistoryEntry::restore()
{
    ClipHistoryEntry *oldEntry;

    if (saved) {
        oldEntry = saved;
        saved = nullptr;
        delete this; // TODO really should avoid deleting from inside.
    } else {
        oldEntry = this;
    }

    return oldEntry;
}

ClipHistoryEntry::ClipHistoryEntry(ClipHistoryEntry *other, bool cleared)
{
    if (other && other->clipPath) {
        this->affine = other->affine;
        this->clipPath = other->clipPath->copy();
        this->clipType = other->clipType;
        this->cleared = cleared;
    } else {
        this->affine = Geom::identity();
        this->clipPath = nullptr;
        this->clipType = clipNormal;
        this->cleared = false;
    }
    saved = nullptr;
}

Geom::Rect getRect(_POPPLER_CONST PDFRectangle *box)
{
    return Geom::Rect(box->x1, box->y1, box->x2, box->y2);
}

