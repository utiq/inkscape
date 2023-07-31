// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Jon A. Cruz
 *   John Bintz
 *   Abhishek Sharma
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2005 Jon A. Cruz
 * Copyright (C) 2008 John Bintz
 * Copyright (C) 2022 PBS
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "swatches.h"

#include <algorithm>
#include <giomm/file.h>
#include <giomm/inputstream.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <glibmm/utility.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/object.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/togglebutton.h>
#include <pangomm/layout.h>
#include <string>
#include <vector>

#include "document.h"
#include "helper/choose-file.h"
#include "object/sp-defs.h"
#include "style.h"
#include "desktop-style.h"
#include "object/sp-gradient-reference.h"

#include "inkscape-preferences.h"
#include "ui/builder-utils.h"
#include "widgets/paintdef.h"
#include "ui/widget/color-palette.h"
#include "ui/dialog/global-palettes.h"
#include "ui/dialog/color-item.h"

namespace Inkscape::UI::Dialog {

struct PaletteSetColumns : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> translated_title;
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<bool> loaded; // true for a palette loaded by the user
    Gtk::TreeModelColumn<Cairo::RefPtr<Cairo::Surface>> set_image;

    PaletteSetColumns() {
        add(translated_title);
        add(id);
        add(loaded);
        add(set_image);
    }
} const g_set_columns;

constexpr const char auto_id[] = "Auto";

/*
 * Lifecycle
 */

SwatchesPanel::SwatchesPanel(bool compact, char const *prefsPath)
    : DialogBase(prefsPath, "Swatches"),
    _builder(create_builder("dialog-swatches.glade")),
    _list_btn(get_widget<Gtk::RadioButton>(_builder, "list")),
    _grid_btn(get_widget<Gtk::RadioButton>(_builder, "grid")),
    _selector(get_widget<Gtk::ComboBoxText>(_builder, "selector")),
    _new_btn(get_widget<Gtk::Button>(_builder, "new")),
    _edit_btn(get_widget<Gtk::Button>(_builder, "edit")),
    _delete_btn(get_widget<Gtk::Button>(_builder, "delete"))
{
    // hide edit buttons - this functionality is not implemented
    _new_btn.set_visible(false);
    _edit_btn.set_visible(false);
    _delete_btn.set_visible(false);

    _palette = Gtk::make_managed<Inkscape::UI::Widget::ColorPalette>();
    _palette->set_visible();
    if (compact) {
        pack_start(*_palette);
    }
    else {
        get_widget<Gtk::Box>(_builder, "content").pack_start(*_palette);
        _palette->set_settings_visibility(false);

        get_widget<Gtk::MenuButton>(_builder, "settings").set_popover(_palette->get_settings_popover());

        _palette->set_filter([=](const Dialog::ColorItem& color){
            return filter_callback(color);
        });
        auto& search = get_widget<Gtk::SearchEntry>(_builder, "search");
        search.signal_search_changed().connect([=, &search](){
            if (search.get_text_length() == 0) {
                clear_filter();
            }
            else {
                filter_colors(search.get_text());
            }
        });
    }

    auto prefs = Inkscape::Preferences::get();
    _current_palette_id = prefs->getString(_prefs_path + "/palette");
    auto path = prefs->getString(_prefs_path + "/palette-path");
    auto loaded = load_swatches(path);

    update_palettes(compact);

    if (!compact) {
        if (loaded) {
            update_store_entry();
        }
        _selector.set_wrap_width(2);
        auto cr = dynamic_cast<Gtk::CellRendererText*>(_selector.get_first_cell());
        cr->property_ellipsize() = Pango::ELLIPSIZE_MIDDLE;
        _selector.set_model(_palette_store);
        _selector.set_id_column(g_set_columns.id.index());
        if (!get_palette(_current_palette_id)) _current_palette_id = auto_id;
        _selector.set_active_id(_current_palette_id);
        _selector.signal_changed().connect([=](){
            auto it = _selector.get_active();
            if (it) {
                auto row = *it;
                Glib::ustring id = row[g_set_columns.id];
                set_palette(id);
            }
        });
    }

    bool embedded = compact;
    _palette->set_compact(embedded);

    // restore palette settings
    _palette->set_tile_size(prefs->getInt(_prefs_path + "/tile_size", 16));
    _palette->set_aspect(prefs->getDoubleLimited(_prefs_path + "/tile_aspect", 0.0, -2, 2));
    _palette->set_tile_border(prefs->getInt(_prefs_path + "/tile_border", 1));
    _palette->set_rows(prefs->getInt(_prefs_path + "/rows", 1));
    _palette->enable_stretch(prefs->getBool(_prefs_path + "/tile_stretch", false));
    _palette->set_large_pinned_panel(embedded && prefs->getBool(_prefs_path + "/enlarge_pinned", true));
    _palette->enable_labels(!embedded && prefs->getBool(_prefs_path + "/show_labels", true));

    // save settings when they change
    _palette->get_settings_changed_signal().connect([=] {
        prefs->setInt(_prefs_path + "/tile_size", _palette->get_tile_size());
        prefs->setDouble(_prefs_path + "/tile_aspect", _palette->get_aspect());
        prefs->setInt(_prefs_path + "/tile_border", _palette->get_tile_border());
        prefs->setInt(_prefs_path + "/rows", _palette->get_rows());
        prefs->setBool(_prefs_path + "/tile_stretch", _palette->is_stretch_enabled());
        prefs->setBool(_prefs_path + "/enlarge_pinned", _palette->is_pinned_panel_large());
        prefs->setBool(_prefs_path + "/show_labels", !embedded && _palette->are_labels_enabled());
    });

    _list_btn.signal_clicked().connect([=](){
        _palette->enable_labels(true);
    });
    _grid_btn.signal_clicked().connect([=](){
        _palette->enable_labels(false);
    });
    (_palette->are_labels_enabled() ? _list_btn : _grid_btn).set_active();

    // Watch for pinned palette options.
    _pinned_observer = prefs->createObserver(_prefs_path + "/pinned/", [this]() {
        rebuild();
    });

    rebuild();

    if (compact) {
        // Respond to requests from the palette widget to change palettes.
        _palette->get_palette_selected_signal().connect([this] (Glib::ustring name) {
            set_palette(name);
        });
    }
    else {
        pack_start(get_widget<Gtk::Box>(_builder, "main"));

        get_widget<Gtk::Button>(_builder, "open").signal_clicked().connect([=](){
            // load a color palette file selected by the user
            if (load_swatches()) {
                update_store_entry();
                _selector.set_active_id(_loaded_palette.id);
            }
        });;
    }
}

SwatchesPanel::~SwatchesPanel()
{
    untrack_gradients();
}

/*
 * Activation
 */

// Note: The "Auto" palette shows the list of gradients that are swatches. When this palette is
// shown (and we have a document), we therefore need to track both addition/removal of gradients
// and changes to the isSwatch() status to keep the palette up-to-date.

void SwatchesPanel::documentReplaced()
{
    if (getDocument()) {
        if (_current_palette_id == auto_id) {
            track_gradients();
        }
    } else {
        untrack_gradients();
    }

    if (_current_palette_id == auto_id) {
        rebuild();
    }
}

void SwatchesPanel::desktopReplaced()
{
    documentReplaced();
}

void SwatchesPanel::set_palette(const Glib::ustring& id) {
    auto prefs = Preferences::get();
    prefs->setString(_prefs_path + "/palette", id);
    select_palette(id);
}

const PaletteFileData* SwatchesPanel::get_palette(const Glib::ustring& id) {
    if (auto p = GlobalPalettes::get().find_palette(id)) return p;

    if (_loaded_palette.id == id) return &_loaded_palette;

    return nullptr;
}

void SwatchesPanel::select_palette(const Glib::ustring& id) {
    if (_current_palette_id == id) return;
    _current_palette_id = id;

    bool edit = false;
    if (id == auto_id) {
        if (getDocument()) {
            track_gradients();
            edit = false; /*TODO: true; when swatch editing is ready */
        }
    } else {
        untrack_gradients();
    }

    _new_btn.set_visible(edit);
    _edit_btn.set_visible(edit);
    _delete_btn.set_visible(edit);

    rebuild();
}

void SwatchesPanel::track_gradients()
{
    auto doc = getDocument();

    // Subscribe to the addition and removal of gradients.
    conn_gradients.disconnect();
    conn_gradients = doc->connectResourcesChanged("gradient", [this] {
        gradients_changed = true;
        queue_resize();
    });

    // Subscribe to child modifications of the defs section. We will use this to monitor
    // each gradient for whether its isSwatch() status changes.
    conn_defs.disconnect();
    conn_defs = doc->getDefs()->connectModified([this] (SPObject*, unsigned flags) {
        if (flags & SP_OBJECT_CHILD_MODIFIED_FLAG) {
            defs_changed = true;
            queue_resize();
        }
    });

    gradients_changed = false;
    defs_changed = false;
    rebuild_isswatch();
}

void SwatchesPanel::untrack_gradients()
{
    conn_gradients.disconnect();
    conn_defs.disconnect();
    gradients_changed = false;
    defs_changed = false;
}

/*
 * Updating
 */

void SwatchesPanel::selectionChanged(Selection*)
{
    selection_changed = true;
    queue_resize();
}

void SwatchesPanel::selectionModified(Selection*, guint flags)
{
    if (flags & SP_OBJECT_STYLE_MODIFIED_FLAG) {
        selection_changed = true;
        queue_resize();
    }
}

// Document updates are handled asynchronously by setting a flag and queuing a resize. This results in
// the following function being run at the last possible moment before the widget will be repainted.
// This ensures that multiple document updates only result in a single UI update.
void SwatchesPanel::on_size_allocate(Gtk::Allocation &alloc)
{
    if (gradients_changed) {
        assert(_current_palette_id == auto_id);
        // We are in the "Auto" palette, and a gradient was added or removed.
        // The list of widgets has therefore changed, and must be completely rebuilt.
        // We must also rebuild the tracking information for each gradient's isSwatch() status.
        rebuild_isswatch();
        rebuild();
    } else if (defs_changed) {
        assert(_current_palette_id == auto_id);
        // We are in the "Auto" palette, and a gradient's isSwatch() status was possibly modified.
        // Check if it has; if so, then the list of widgets has changed, and must be rebuilt.
        if (update_isswatch()) {
            rebuild();
        }
    }

    if (selection_changed) {
        update_fillstroke_indicators();
    }

    selection_changed = false;
    gradients_changed = false;
    defs_changed = false;

    // Necessary to perform *after* the above widget modifications, so GTK can process the new layout.
    DialogBase::on_size_allocate(alloc);
}

// TODO: The following two functions can made much nicer using C++20 ranges.

void SwatchesPanel::rebuild_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    isswatch.resize(grads.size());

    for (int i = 0; i < grads.size(); i++) {
        isswatch[i] = static_cast<SPGradient*>(grads[i])->isSwatch();
    }
}

bool SwatchesPanel::update_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    // Should be guaranteed because we catch all size changes and call rebuild_isswatch() instead.
    assert(isswatch.size() == grads.size());

    bool modified = false;

    for (int i = 0; i < grads.size(); i++) {
        if (isswatch[i] != static_cast<SPGradient*>(grads[i])->isSwatch()) {
            isswatch[i].flip();
            modified = true;
        }
    }

    return modified;
}

static auto spcolor_to_rgb(SPColor const &color)
{
    float rgbf[3];
    color.get_rgb_floatv(rgbf);

    std::array<unsigned, 3> rgb;
    for (int i = 0; i < 3; i++) {
        rgb[i] = SP_COLOR_F_TO_U(rgbf[i]);
    };

    return rgb;
}

void SwatchesPanel::update_fillstroke_indicators()
{
    auto doc = getDocument();
    auto style = SPStyle(doc);

    // Get the current fill or stroke as a ColorKey.
    auto current_color = [&, this] (bool fill) -> std::optional<ColorKey> {
        switch (sp_desktop_query_style(getDesktop(), &style, fill ? QUERY_STYLE_PROPERTY_FILL : QUERY_STYLE_PROPERTY_STROKE))
        {
            case QUERY_STYLE_SINGLE:
            case QUERY_STYLE_MULTIPLE_AVERAGED:
            case QUERY_STYLE_MULTIPLE_SAME:
                break;
            default:
                return {};
        }

        auto attr = style.getFillOrStroke(fill);
        if (!attr->set) {
            return {};
        }

        if (attr->isNone()) {
            return std::monostate{};
        } else if (attr->isColor()) {
            return spcolor_to_rgb(attr->value.color);
        } else if (attr->isPaintserver()) {
            if (auto grad = cast<SPGradient>(fill ? style.getFillPaintServer() : style.getStrokePaintServer())) {
                if (grad->isSwatch()) {
                    return grad;
                } else if (grad->ref) {
                    if (auto ref = grad->ref->getObject(); ref && ref->isSwatch()) {
                        return ref;
                    }
                }
            }
        }

        return {};
    };

    for (auto w : current_fill) w->set_fill(false);
    for (auto w : current_stroke) w->set_stroke(false);

    current_fill.clear();
    current_stroke.clear();

    if (auto fill = current_color(true)) {
        auto range = widgetmap.equal_range(*fill);
        for (auto it = range.first; it != range.second; ++it) {
            current_fill.emplace_back(it->second);
        }
    }

    if (auto stroke = current_color(false)) {
        auto range = widgetmap.equal_range(*stroke);
        for (auto it = range.first; it != range.second; ++it) {
            current_stroke.emplace_back(it->second);
        }
    }

    for (auto w : current_fill) w->set_fill(true);
    for (auto w : current_stroke) w->set_stroke(true);
}

/**
 * Process the list of available palettes and update the list in the _palette widget.
 */
void SwatchesPanel::update_palettes(bool compact) {
    std::vector<Inkscape::UI::Widget::ColorPalette::palette_t> palettes;
    palettes.reserve(1 + GlobalPalettes::get().palettes().size());

    // The first palette in the list is always the "Auto" palette. Although this
    // will contain colors when selected, the preview we show for it is empty.
    palettes.push_back({_("Document swatches"), auto_id, {}});

    // The remaining palettes in the list are the global palettes.
    for (auto &p : GlobalPalettes::get().palettes()) {
        Inkscape::UI::Widget::ColorPalette::palette_t palette;
        palette.name = p.name;
        palette.id = p.id;
        for (auto const &c : p.colors) {
            auto [r, g, b] = c.rgb;
            palette.colors.push_back({r / 255.0, g / 255.0, b / 255.0});
        }
        palettes.emplace_back(std::move(palette));
    }

    _palette->set_palettes(palettes);

    if (!compact) {
        _palette_store = Gtk::ListStore::create(g_set_columns);
        for (auto&& p : palettes) {
            auto row = _palette_store->append();
            (*row)[g_set_columns.translated_title] = p.name;
            (*row)[g_set_columns.id] = p.id;
            (*row)[g_set_columns.loaded] = false;
        }
    }
}

/**
 * Rebuild the list of color items shown by the palette.
 */
void SwatchesPanel::rebuild()
{
    std::vector<ColorItem*> palette;

    // The pointers in widgetmap are to widgets owned by the ColorPalette. It is assumed it will not
    // delete them unless we ask, via the call to set_colors() later in this function.
    widgetmap.clear();
    current_fill.clear();
    current_stroke.clear();

    // Add the "remove-color" color.
    auto const w = Gtk::make_managed<ColorItem>(PaintDef(), this);
    w->set_pinned_pref(_prefs_path);
    palette.emplace_back(w);
    widgetmap.emplace(std::monostate{}, w);
    _palette->set_page_size(0);
    if (auto pal = get_palette(_current_palette_id)) {
        _palette->set_page_size(pal->columns);
        palette.reserve(palette.size() + pal->colors.size());
        for (auto &c : pal->colors) {
            auto const w = c.filler || c.group ?
                Gtk::make_managed<ColorItem>(c.name) :
                Gtk::make_managed<ColorItem>(PaintDef(c.rgb, c.name, c.definition), this);
            w->set_pinned_pref(_prefs_path);
            palette.emplace_back(w);
            widgetmap.emplace(c.rgb, w);
        }
    } else if (_current_palette_id == auto_id && getDocument()) {
        auto grads = getDocument()->getResourceList("gradient");
        for (auto obj : grads) {
            auto grad = static_cast<SPGradient*>(obj);
            if (grad->isSwatch()) {
                auto const w = Gtk::make_managed<ColorItem>(grad, this);
                palette.emplace_back(w);
                widgetmap.emplace(grad, w);
                // Rebuild if the gradient gets pinned or unpinned
                w->signal_pinned().connect([=]() {
                    rebuild();
                });
            }
        }
    }

    if (getDocument()) {
        update_fillstroke_indicators();
    }

    _palette->set_colors(palette);
    _palette->set_selected(_current_palette_id);
}

bool SwatchesPanel::load_swatches() {
    auto window = dynamic_cast<Gtk::Window*>(get_toplevel());
    auto file = choose_palette_file(window);
    auto loaded = false;
    if (load_swatches(file)) {
        auto prefs = Preferences::get();
        prefs->setString(_prefs_path + "/palette", _loaded_palette.id);
        prefs->setString(_prefs_path + "/palette-path", file);
        select_palette(_loaded_palette.id);
        loaded = true;
    }
    return loaded;
}

bool SwatchesPanel::load_swatches(Glib::ustring path) {
    if (path.empty()) return false;

    // load colors
    auto res = load_palette(path);
    if (res.palette.has_value()) {
        // use loaded palette
        _loaded_palette = *res.palette;
        return true;
    }
    else if (auto desktop = getDesktop()) {
        desktop->showNotice(res.error_message);
    }
    return false;
}

void SwatchesPanel::update_store_entry() {
    // add or update last entry in a store to match loaded palette
    auto items = _palette_store->children();
    auto last = items.size() - 1;
    if (!items.empty() && items[last].get_value(g_set_columns.loaded)) {
        items[last].set_value(g_set_columns.translated_title, _loaded_palette.name);
        items[last].set_value(g_set_columns.id, _loaded_palette.id);
    }
    else {
        auto row = _palette_store->append();
        (*row)[g_set_columns.translated_title] = _loaded_palette.name;
        (*row)[g_set_columns.id] = _loaded_palette.id;
        (*row)[g_set_columns.loaded] = true;
    }
}

void SwatchesPanel::clear_filter() {
    if (_color_filter_text.empty()) return;

    _color_filter_text.erase();
    _palette->apply_filter();
}

void SwatchesPanel::filter_colors(const Glib::ustring& text) {
    auto search = text.lowercase();
    if (_color_filter_text == search) return;

    _color_filter_text = search;
    _palette->apply_filter();
}

bool SwatchesPanel::filter_callback(const Dialog::ColorItem& color) const {
    if (_color_filter_text.empty()) return true;

    // let's hide group headers and fillers when searching for a matching color
    if (color.is_filler() || color.is_group()) return false;

    auto text = color.get_description().lowercase();
    return text.find(_color_filter_text) != Glib::ustring::npos;
}

} // namespace

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
