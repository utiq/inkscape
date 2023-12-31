// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SP_TAG_USE_H
#define SEEN_SP_TAG_USE_H

/*
 * SVG <inkscape:tagref> implementation
 *
 * Authors:
 *   Theodore Janeczko
 *
 * Copyright (C) Theodore Janeczko 2012 <flutterguy317@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glib.h>
#include <cstddef>
#include <sigc++/sigc++.h>
#include "svg/svg-length.h"
#include "sp-object.h"

class SPItem;
class SPTagUse;
class SPTagUseReference;

class SPTagUse final : public SPObject {

public:
    int tag() const override { return tag_of<decltype(*this)>; }

    // item built from the original's repr (the visible clone)
    // relative to the SPUse itself, it is treated as a child, similar to a grouped item relative to its group
    SPObject *child;
    gchar *href;
public:
    SPTagUse();
    ~SPTagUse() override;
    
    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
    void set(SPAttr key, gchar const *value) override;
    Inkscape::XML::Node* write(Inkscape::XML::Document* doc, Inkscape::XML::Node* repr, guint flags) override;
    void release() override;
    
    virtual void href_changed(SPObject* old_ref, SPObject* ref);
    
    //virtual SPItem* unlink();
    virtual SPItem* get_original();
    virtual SPItem* root();

    // the reference to the original object
    SPTagUseReference *ref;
    sigc::connection _changed_connection;
};

#endif
