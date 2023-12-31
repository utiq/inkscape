// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Gtk <themes> helper code.
 */
/*
 * Authors:
 *   Jabiertxof
 *   Martin Owens
 *
 * Copyright (C) 2017-2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef UI_THEMES_H_SEEN
#define UI_THEMES_H_SEEN

#include <map>
#include <memory>
#include <vector>
#include <sigc++/signal.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <pangomm/fontdescription.h>
#include "preferences.h"

namespace Gtk {
class CssProvider;
class Window;
} // namespace Gtk

namespace Inkscape::UI {

class ThemeContext
{
public:
    ThemeContext() = default;
    ~ThemeContext() = default;

    // Name of theme -> has dark theme
    typedef std::map<Glib::ustring, bool> gtkThemeList;
    void inkscape_fill_gtk(const gchar *path, gtkThemeList &themes);

    std::map<Glib::ustring, bool> get_available_themes();
    void add_gtk_css(bool only_providers, bool cached = false);
    void add_icon_theme();
    Glib::ustring get_symbolic_colors();
    Glib::RefPtr<Gtk::CssProvider> getColorizeProvider() { return _colorizeprovider;}
    Glib::RefPtr<Gtk::CssProvider> getContrastThemeProvider() { return _contrastthemeprovider;}
    Glib::RefPtr<Gtk::CssProvider> getThemeProvider() { return _themeprovider;}
    Glib::RefPtr<Gtk::CssProvider> getStyleProvider() { return _styleprovider;}
    Glib::RefPtr<Gtk::CssProvider> getUserProvider() { return _userprovider;}
    sigc::signal<void ()> getChangeThemeSignal() { return _signal_change_theme;}
    void themechangecallback();
    /// Set application-wide font size adjustment by a factor, where 1 is 100% (no change)
    void adjustGlobalFontScale(double factor);
    /// Get current font scaling factor (50 - 150, percent of "normal" size)
    double getFontScale() const;
    /// Save font scaling factor in preferences
    void saveFontScale(double scale);
    static Glib::ustring get_font_scale_pref_path() { return "/theme/fontscale"; }

    /// User-selected monospaced font used by XML dialog and attribute editor
    Pango::FontDescription getMonospacedFont() const;
    void saveMonospacedFont(Pango::FontDescription desc);
    static Glib::ustring get_monospaced_font_pref_path() { return "/ui/mono-font/desc"; }

    // True if current theme (applied one) is dark
    bool isCurrentThemeDark(Gtk::Window *window);

    // Get CSS foreground colors resulting from classes ".highlight-color-[1-8]"
    static std::vector<guint32> getHighlightColors(Gtk::Window *window);

    static void initialize_source_syntax_styles();
    static void select_default_syntax_style(bool dark_theme);

private:
    // user change theme
    sigc::signal<void ()> _signal_change_theme;
    Glib::RefPtr<Gtk::CssProvider> _styleprovider;
    Glib::RefPtr<Gtk::CssProvider> _themeprovider;
    Glib::RefPtr<Gtk::CssProvider> _contrastthemeprovider;
    Glib::RefPtr<Gtk::CssProvider> _colorizeprovider;
    Glib::RefPtr<Gtk::CssProvider> _spinbuttonprovider;
    Glib::RefPtr<Gtk::CssProvider> _userprovider;
#if __APPLE__
    Glib::RefPtr<Gtk::CssProvider> _macstyleprovider;
#endif
    std::unique_ptr<Preferences::Observer> _spinbutton_observer;
    Glib::RefPtr<Gtk::CssProvider> _fontsizeprovider = Gtk::CssProvider::create();
};

} // namespace Inkscape::UI

#endif /* !UI_THEMES_H_SEEN */

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
