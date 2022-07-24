// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_PATTERN_STORE_H
#define INKSCAPE_UI_WIDGET_PATTERN_STORE_H
/*
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <optional>
#include <2geom/transforms.h>
#include "color.h"

namespace Inkscape {
namespace UI {
namespace Widget {

struct PatternItem : Glib::Object {
    Cairo::RefPtr<Cairo::Surface> pix;
    std::string id;
    std::string label;
    bool stock = false;
    bool uniform_scale = false;
    Geom::Affine transform;
    Geom::Point offset;
    std::optional<SPColor> color;
    std::string link_id;
    Geom::Scale gap;

    bool operator == (const PatternItem& item) const {
        return
            id == item.id &&
            label == item.label &&
            stock == item.stock &&
            uniform_scale == item.uniform_scale &&
            transform == item.transform &&
            offset == item.offset &&
            color == item.color &&
            gap == item.gap;
            // skip link_id
    }
};

struct PatternStore {
    PatternStore() {
        store = Gio::ListStore<PatternItem>::create();
    }

    Glib::RefPtr<Gio::ListStore<PatternItem>> store;
    std::map<Gtk::Widget*, Glib::RefPtr<PatternItem>> widgets_to_pattern;
};

}
}
}

#endif
