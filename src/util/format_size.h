// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::Util::format_size - format a number into a byte display
 *
 * Copyright (C) 2005-2022 Inkscape Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_UTIL_FORMAT_SIZE_H
#define SEEN_INKSCAPE_UTIL_FORMAT_SIZE_H

#include <glibmm/main.h>

namespace Inkscape {
namespace Util {

inline Glib::ustring format_size(std::size_t value) {
    if (!value) {
        return Glib::ustring("0");
    }

    typedef std::vector<char> Digits;
    typedef std::vector<Digits *> Groups;

    Groups groups;

    Digits *digits;

    while (value) {
        unsigned places=3;
        digits = new Digits();
        digits->reserve(places);

        while ( value && places ) {
            digits->push_back('0' + (char)( value % 10 ));
            value /= 10;
            --places;
        }

        groups.push_back(digits);
    }

    Glib::ustring temp;

    while (true) {
        digits = groups.back();
        while (!digits->empty()) {
            temp.append(1, digits->back());
            digits->pop_back();
        }
        delete digits;

        groups.pop_back();
        if (groups.empty()) {
            break;
        }

        temp.append(",");
    }

    return temp;
}

}}
#endif

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
