// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Gtk builder utilities
 */
/* Authors:
 *   Michael Kowalski
 *
 * Copyright (C) 2021 Michael Kowalski
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_BUILDER_UTILS_H
#define SEEN_BUILDER_UTILS_H

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>

namespace Inkscape {
namespace UI {

// get widget from builder or throw
template<class W> W& get_widget(const Glib::RefPtr<Gtk::Builder>& builder, const char* id) {
    W* widget;
    builder->get_widget(id, widget);
    if (!widget) {
        throw std::runtime_error("Missing widget in a glade resource file");
    }
    return *widget;
}

template<class Ob> Glib::RefPtr<Ob> get_object(Glib::RefPtr<Gtk::Builder>& builder, const char* id) {
    auto object = Glib::RefPtr<Ob>::cast_dynamic(builder->get_object(id));
    if (!object) {
        throw std::runtime_error("Missing object in a glade resource file");
    }
    return object;
}

// load glade file from share/ui folder and return builder; throws on errors
Glib::RefPtr<Gtk::Builder> create_builder(const char* filename);

} } // namespace

#endif // SEEN_BUILDER_UTILS_H
