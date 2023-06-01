// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class to provide access to system/user ICC color profiles.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include "cms-system.h"

#include <glibmm.h>

#include "cms-util.h"
#include "color-profile-cms-fns.h"
#include "document.h"
#include "preferences.h"

#include "io/resource.h"
#include "object/color-profile.h"

#ifdef _WIN32
#include <windows.h>  // IS THIS NECESSARY?
#include <icm.h>
#endif

using Inkscape::ColorProfile;

namespace Inkscape {

class ProfileInfo
{
public:
    ProfileInfo( cmsHPROFILE prof, Glib::ustring  path );

    Glib::ustring const& getName() {return _name;}
    Glib::ustring const& getPath() {return _path;}
    cmsColorSpaceSignature getSpace() {return _profileSpace;}
    cmsProfileClassSignature getClass() {return _profileClass;}

private:
    Glib::ustring _path;
    Glib::ustring _name;
    cmsColorSpaceSignature _profileSpace;
    cmsProfileClassSignature _profileClass;
};

ProfileInfo::ProfileInfo(cmsHPROFILE prof, Glib::ustring path)
    : _path(std::move(path))
    , _name(get_name_from_color_profile(prof))
    , _profileSpace(cmsGetColorSpace(prof))
    , _profileClass(cmsGetDeviceClass(prof))
{
}

CMSSystem::CMSSystem() {
    // Read in profiles (move to refresh()?).
    load_profiles();

    // Create generic sRGB profile.
    sRGBProf = cmsCreate_sRGBProfile();
}

// Common operation... we track last transform created so we can delete it later.
void
CMSSystem::clear_transform() {
    if (current_transform) {
        cmsDeleteTransform(current_transform);
        current_transform = nullptr;
    }
}

// Search for system ICC profile files and add them to list.
void
CMSSystem::load_profiles() {

    system_profile_infos.clear(); // Allows us to refresh list if necessary.

    // Get list of all possible file directories, with flag if they are "home" directories or not.
    auto directory_paths = get_directory_paths();

    // Look for icc files in specified directories.
    for (auto directory_path : directory_paths) {

        using Inkscape::IO::Resource::get_filenames;
        for (auto &filename : get_filenames(directory_path.first, {".icc", ".icm"})) {

            // Check if files are ICC files and extract out basic information, add to list.
            if (is_icc_file(filename)) {

                cmsHPROFILE profile = cmsOpenProfileFromFile(filename.c_str(), "r");
                if (profile) {
                    ICCProfileInfo info(profile, filename, directory_path.second);
                    cmsCloseProfile(profile);
                    profile = nullptr;

                    bool same_name = false;
                    for (auto &profile_info : system_profile_infos) {
                        if (profile_info.get_name() == info.get_name() ) {
                            same_name = true;
                            break;
                        }
                    }

                    if ( !same_name ) {
                        system_profile_infos.emplace_back(info);
                    }
                } else {
                    std::cerr << "CMSSystem::load_profiles: failed to load " << filename << std::endl;
                }
            } else {
                std::cerr << "CMSSystem::load_profiles: " << filename << " is not an ICC file!" << std::endl;
            }
        }
    }
}

/* Create list of all directories where ICC profiles are expected to be found. */
std::vector<std::pair<std::string, bool>>
CMSSystem::get_directory_paths() {

    std::vector<std::pair<std::string, bool>> paths;

    // First try user's local directory.
    std::string path = Glib::build_filename(Glib::get_user_data_dir(), "color", "icc");
    paths.push_back(std::pair(path, true));

    // See https://github.com/hughsie/colord/blob/fe10f76536bb27614ced04e0ff944dc6fb4625c0/lib/colord/cd-icc-store.c#L590

    // User store
    path = Glib::build_filename(Glib::get_user_data_dir(), "icc");
    paths.push_back(std::pair(path, true));

    path = Glib::build_filename(Glib::get_home_dir(), ".color", "icc");
    paths.push_back(std::pair(path, true));

    // System store
    paths.push_back(std::pair("/var/lib/color/icc", false));
    paths.push_back(std::pair("/var/lib/colord/icc", false));

    auto data_directories = Glib::get_system_data_dirs();
    for (auto data_directory : data_directories) {
        path = Glib::build_filename(data_directory, "color", "icc");
        paths.push_back(std::pair(path, false));
    }

#ifdef __APPLE__
    path.push_back(std::pair("/System/Library/ColorSync/Profiles", false));
    path.push_back(std::pair("/Library/ColorSync/Profiles", false));

    path = Glib::build_filename(Glib::get_user_home_dir(), "Library", "ColorSync", "Profiles");
    paths.push_back(std::pair(path, true));
#endif // __APPLE__

#ifdef _WIN32
    wchar_t pathBuf[MAX_PATH + 1];
    pathBuf[0] = 0;
    DWORD pathSize = sizeof(pathBuf);
    g_assert(sizeof(wchar_t) == sizeof(gunichar2));
    if ( GetColorDirectoryW( NULL, pathBuf, &pathSize ) ) {
        gchar * utf8Path = g_utf16_to_utf8( (gunichar2*)(&pathBuf[0]), -1, NULL, NULL, NULL );
        if ( !g_utf8_validate(utf8Path, -1, NULL) ) {
            g_warning( "GetColorDirectoryW() resulted in invalid UTF-8" );
        } else {
            paths.push_back(std::pair(utf8Path, false));
        }
        g_free( utf8Path );
    }
#endif // _WIN32

    return paths;
}


cmsHPROFILE CMSSystem::get_system_profile()
{
    static cmsHPROFILE theOne = nullptr;
    static Glib::ustring lastURI;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring uri = prefs->getString("/options/displayprofile/uri");

    if ( !uri.empty() ) {
        if ( uri != lastURI ) {
            lastURI.clear();
            if ( theOne ) {
                cmsCloseProfile( theOne );
            }
            clear_transform();
            theOne = cmsOpenProfileFromFile( uri.data(), "r" );
            if ( theOne ) {
                // a display profile must have the proper stuff
                cmsColorSpaceSignature space = cmsGetColorSpace(theOne);
                cmsProfileClassSignature profClass = cmsGetDeviceClass(theOne);

                if ( profClass != cmsSigDisplayClass ) {
                    g_warning("CMSSystem::get_system_profile: Not a display profile: %s\n", lastURI.c_str());
                    cmsCloseProfile( theOne );
                    theOne = nullptr;
                } else if ( space != cmsSigRgbData ) {
                    g_warning("CMSSystem::get_system_profile: Not an RGB profile: %s\n", lastURI.c_str());
                    cmsCloseProfile( theOne );
                    theOne = nullptr;
                } else {
                    lastURI = uri;
                }
            }
        }
    } else if ( theOne ) {
        cmsCloseProfile( theOne );
        theOne = nullptr;
        lastURI.clear();
        clear_transform();
    }

    return theOne;
}


cmsHPROFILE CMSSystem::get_proof_profile()
{
    static cmsHPROFILE theOne = nullptr;
    static Glib::ustring lastURI;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool which = prefs->getBool( "/options/softproof/enable");
    Glib::ustring uri = prefs->getString("/options/softproof/uri");

    if ( which && !uri.empty() ) {
        if ( lastURI != uri ) {
            lastURI.clear();
            if ( theOne ) {
                cmsCloseProfile( theOne );
            }
            clear_transform();
            theOne = cmsOpenProfileFromFile( uri.data(), "r" );
            if ( theOne ) {
                // a display profile must have the proper stuff
                cmsColorSpaceSignature space = cmsGetColorSpace(theOne);
                cmsProfileClassSignature profClass = cmsGetDeviceClass(theOne);

                (void)space;
                (void)profClass;
/*
                if ( profClass != cmsSigDisplayClass ) {
                    g_warning("Not a display profile");
                    cmsCloseProfile( theOne );
                    theOne = 0;
                } else if ( space != cmsSigRgbData ) {
                    g_warning("Not an RGB profile");
                    cmsCloseProfile( theOne );
                    theOne = 0;
                } else {
*/
                    lastURI = uri;
/*
                }
*/
            }
        }
    } else if ( theOne ) {
        cmsCloseProfile( theOne );
        theOne = nullptr;
        lastURI.clear();
        clear_transform();
    }

    return theOne;
}


// Get a color profile handle corresponding to "name" from the document.
cmsHPROFILE Inkscape::CMSSystem::get_document_profile(SPDocument* document, guint* intent, gchar const* name)
{
    cmsHPROFILE profile_handle = nullptr;

    // Search through <ColorProfile> elements for one with matching name.
    ColorProfile* color_profile = nullptr;
    std::vector<SPObject *> color_profiles = document->getResourceList("iccprofile");
    for (auto *object : color_profiles) {
        if (auto color_profile_test = cast<ColorProfile>(object)) {
            if ( color_profile_test->name && (strcmp(color_profile_test->name, name) == 0) ) {
                color_profile = color_profile_test;
            }
        }
    }

    // If found, set profile_handle pointer.
    if ( color_profile ) {
        profile_handle = color_profile->getHandle();
    }

    // If requested, fill "RENDERING_INTENT" value.
    if (intent) {
        *intent = color_profile ? color_profile->rendering_intent : (guint)RENDERING_INTENT_UNKNOWN;
    }

    return profile_handle;
}

std::vector<Glib::ustring> Inkscape::CMSSystem::get_display_names()
{
    std::vector<Glib::ustring> result;

    for (auto & profile_info : system_profile_infos) {
        if (profile_info.get_profileclass() == cmsSigDisplayClass &&
            profile_info.get_colorspace() == cmsSigRgbData ) {
            result.push_back( profile_info.get_name() );
        }
    }
    std::sort(result.begin(), result.end());

    return result;
}

std::vector<Glib::ustring> Inkscape::CMSSystem::get_softproof_names()
{
    std::vector<Glib::ustring> result;

    for (auto & profile_info : system_profile_infos) {
        if (profile_info.get_profileclass() == cmsSigOutputClass) {
            result.push_back(profile_info.get_name());
        }
    }
    std::sort(result.begin(), result.end());

    return result;
}

std::string Inkscape::CMSSystem::get_path_for_profile(Glib::ustring const& name)
{
    std::string result;

    for (auto & profile_info : system_profile_infos) {
        if (name == profile_info.get_name()) {
            result = profile_info.get_path();
            break;
        }
    }

    return result;
}

// Static, doesn't rely on class
void Inkscape::CMSSystem::do_transform(cmsHTRANSFORM transform, void *inBuf, void *outBuf, unsigned int size)
{
    cmsDoTransform(transform, inBuf, outBuf, size);
}

// Only reason this is part of class is to access transform variable.
cmsHTRANSFORM Inkscape::CMSSystem::get_display_transform_system()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool fromDisplay = prefs->getBool( "/options/displayprofile/from_display");
    if (fromDisplay) {
        clear_transform();
        return nullptr;
    }

    // Fetch these now, as they might clear the transform as a side effect.
    cmsHPROFILE system_profile = get_system_profile();

    if (!current_transform) {
        current_transform = set_transform(system_profile, current_transform);
    }

    return current_transform;
}

// This function takes a profile and transform, returning a new transform based on preference settings.
// "transform" is either "current_transform" if called by get_diplay_profile_system() or a
// transform attached to a monitor if called by get_display_profile_monitor().
cmsHTRANSFORM Inkscape::CMSSystem::set_transform(cmsHPROFILE profile, cmsHTRANSFORM transform)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool warn = prefs->getBool( "/options/softproof/gamutwarn");
    int intent = prefs->getIntLimited( "/options/displayprofile/intent", 0, 0, 3 );
    int proofIntent = prefs->getIntLimited( "/options/softproof/intent", 0, 0, 3 );
    bool bpc = prefs->getBool( "/options/softproof/bpc");
    Glib::ustring colorStr = prefs->getString("/options/softproof/gamutcolor");
    Gdk::RGBA gamutColor( colorStr.empty() ? "#808080" : colorStr );

    if ( (gamutWarn       != warn)        ||
         (lastIntent      != intent)      ||
         (lastProofIntent != proofIntent) ||
         (lastBPC         != bpc)         ||
         (lastGamutColor  != gamutColor)  ) {
        gamutWarn       = warn;
        lastIntent      = intent;
        lastProofIntent = proofIntent;
        lastBPC         = bpc;
        lastGamutColor  = gamutColor;
        free_transforms();
    }

    // Fetch this now, as they might clear the transform as a side effect.
    cmsHPROFILE proof_profile = profile ? get_proof_profile() : nullptr;

    if (!transform) { // May be nullptr if freed above or never set.

        if (profile && proof_profile) {
            cmsUInt32Number dwFlags = cmsFLAGS_SOFTPROOFING;

            if (gamutWarn) {
                dwFlags |= cmsFLAGS_GAMUTCHECK;

                auto gamutColor_r = gamutColor.get_red_u();
                auto gamutColor_g = gamutColor.get_green_u();
                auto gamutColor_b = gamutColor.get_blue_u();

                cmsUInt16Number newAlarmCodes[cmsMAXCHANNELS] = {0};
                newAlarmCodes[0] = gamutColor_r;
                newAlarmCodes[1] = gamutColor_g;
                newAlarmCodes[2] = gamutColor_b;
                newAlarmCodes[3] = ~0;
                cmsSetAlarmCodes(newAlarmCodes);
            }

            if ( bpc ) {
                dwFlags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
            }

            transform = cmsCreateProofingTransform( get_sRGB_profile(), TYPE_BGRA_8, profile, TYPE_BGRA_8,
                                                    proof_profile, intent, proofIntent, dwFlags );

        } else if (profile) {
            transform = cmsCreateTransform( get_sRGB_profile(), TYPE_BGRA_8, profile, TYPE_BGRA_8, intent, 0 );
        }
    }

    return transform;
}

// Free system profile transform and all monitor profile transforms
void Inkscape::CMSSystem::free_transforms()
{
    clear_transform();

    for (auto profile : monitor_profile_infos) {
        if (profile.transform) {
            cmsDeleteTransform(profile.transform);
            profile.transform = nullptr;
        }
    }
}

std::string Inkscape::CMSSystem::get_display_id(int monitor)
{
    std::string id;

    if ( monitor >= 0 && monitor < static_cast<int>(monitor_profile_infos.size()) ) {
        MonitorProfileInfo& item = monitor_profile_infos[monitor];
        id = item.id;
    }

    return id;
}

Glib::ustring Inkscape::CMSSystem::set_display_transform_monitor(gpointer buf, guint bufLen, int monitor)
{
    while ( static_cast<int>(monitor_profile_infos.size()) <= monitor ) {
        MonitorProfileInfo tmp;
        monitor_profile_infos.push_back(tmp);
    }
    MonitorProfileInfo& item = monitor_profile_infos[monitor];

    if (item.profile) {
        cmsCloseProfile( item.profile );
        item.profile = nullptr;
    }

    Glib::ustring id;

    if (buf && bufLen) {
        gsize len = bufLen; // len is an inout parameter
        id = Glib::Checksum::compute_checksum(Glib::Checksum::CHECKSUM_MD5,
            reinterpret_cast<guchar*>(buf), len);

        // Note: if this is not a valid profile, item.hprof will be set to null.
        item.profile = cmsOpenProfileFromMem(buf, bufLen);
    }
    item.id = id;

    return id;
}

cmsHTRANSFORM CMSSystem::get_display_transform_monitor(std::string const &id)
{
    if (id.empty()) {
        return nullptr;
    }

    for (auto& info : monitor_profile_infos) {
        if ( id == info.id ) {

            // Update transform if necessary based on preferences.
            info.transform = set_transform(info.profile, info.transform);
            return info.transform;
        }
    }

    // Monitor profile not found.
    return nullptr;
}

CMSSystem *CMSSystem::_instance = nullptr;

} // namespace Inkscape


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
