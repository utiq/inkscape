// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utilities to for working with ICC profiles. Used by CMSSystem and ColorProfile classes.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_CMS_UTIL_H
#define SEEN_CMS_UTIL_H

#include <string>

#include <lcms2.h>

namespace Inkscape {

// Helper class to store info
class ICCProfileInfo {

public:
    ICCProfileInfo(cmsHPROFILE profile, std::string path, bool isInHome);
    ~ICCProfileInfo() = default;
    bool operator<(ICCProfileInfo const &other) const;
    std::string get_path() { return _path; }
    std::string get_name() { return _name; }
    cmsColorSpaceSignature get_colorspace() { return _colorspace; }
    cmsProfileClassSignature get_profileclass() { return _profileclass; }
    bool is_in_home() { return _is_in_home; }

private:
    std::string _path;
    std::string _name;
    bool _is_in_home;
    cmsColorSpaceSignature _colorspace;
    cmsProfileClassSignature _profileclass;
};

bool is_icc_file(const std::string& filepath);
std::string get_name_from_profile(cmsHPROFILE profile); // Read as ASCII from profile.

} // namespace Inkscape

#endif // !SEEN_COLOR_PROFILE_H

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
