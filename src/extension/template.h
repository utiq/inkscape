// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_EXTENSION_TEMPLATE_H__
#define INKSCAPE_EXTENSION_TEMPLATE_H__

#include <exception>
#include <giomm/file.h>
#include <glibmm.h>
#include <glibmm/fileutils.h>
#include <map>
#include <string>

#include "extension.h"
#include "util/units.h"

class SPDocument;

namespace Inkscape {
namespace Extension {

class Template;
class TemplatePreset;
typedef std::map<std::string, std::string> TemplatePrefs;
typedef std::vector<std::shared_ptr<TemplatePreset>> TemplatePresets;

class TemplatePreset
{
public:
    TemplatePreset(Template *mod, const Inkscape::XML::Node *repr, TemplatePrefs prefs = {}, int priority = 0);
    ~TemplatePreset() {};

    std::string get_key() const { return _key; }
    std::string get_icon() const { return _icon; }
    std::string get_name() const { return _name; }
    std::string get_label() const { return _label; }
    int get_sort_priority() const { return _priority; }

    bool is_selectable() { return _selectable; }
    bool is_searchable() { return _searchable; }

    SPDocument *new_from_template();

    Glib::ustring get_icon_path() const;

private:
    Template *_mod;

protected:
    std::string _key;
    std::string _icon;
    std::string _name;
    std::string _label;
    int _priority;

    bool _selectable; // Does this appear in the start screen and page size dropdown
    bool _searchable; // Does this appear when searching for a named size

    // This is a set of preferences given to the extension
    TemplatePrefs _prefs;

    Glib::ustring _get_icon_path(const std::string &name) const;
};

class Template : public Extension
{
public:
    struct create_cancelled : public std::exception
    {
        ~create_cancelled() noexcept override = default;
        const char *what() const noexcept override { return "Create was cancelled"; }
    };

    Template(Inkscape::XML::Node *in_repr, Implementation::Implementation *in_imp, std::string *base_directory);
    ~Template() override = default;

    bool check() override;

    SPDocument *new_from_template();

    std::string get_icon() const { return _icon; }
    std::string get_description() const { return _desc; }
    std::string get_category() const { return _category; }

    TemplatePresets get_presets() const;
    TemplatePresets get_selectable_presets() const;
    TemplatePresets get_searchable_presets() const;

    std::shared_ptr<TemplatePreset> get_preset(std::string key);

    Glib::RefPtr<Gio::File> get_template_filename() const;
    SPDocument *get_template_document() const;

private:
    std::string _source;
    std::string _icon;
    std::string _desc;
    std::string _category;

    TemplatePresets _presets;
};

} // namespace Extension
} // namespace Inkscape
#endif /* INKSCAPE_EXTENSION_TEMPLATE_H__ */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
