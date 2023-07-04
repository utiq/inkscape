// SPDX-License-Identifier: GPL-2.0-or-later

#include "extensions-gallery.h"

#include <cairo.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <fstream>
#include <gtkmm/liststore.h>
#include <gtkmm/paned.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treeview.h>
#include <iterator>
#include <sstream>
#include <string>

#include "display/cairo-utils.h"
#include "extension/db.h"
#include "extension/effect.h"
#include "io/resource.h"
#include "io/sys.h"
#include "point.h"
#include "rect.h"
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-base.h"
#include "ui/svg-renderer.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

struct EffectColumns : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> tooltip;
    Gtk::TreeModelColumn<Glib::ustring> order;
    Gtk::TreeModelColumn<Glib::ustring> category;
    // Gtk::TreeModelColumn<bool> takes_input;
    Gtk::TreeModelColumn<Inkscape::Extension::Effect*> effect;
    Gtk::TreeModelColumn<Cairo::RefPtr<Cairo::Surface>> image;

    EffectColumns() {
        add(id);
        add(name);
        add(tooltip);
        add(order);
        add(category);
        add(effect);
        add(image);
    }
} g_effect_columns;

struct CategoriesColumns : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> name;

    CategoriesColumns() {
        add(id);
        add(name);
    }
} g_categories_columns;

// populating store with extension previews is expensive, so keep it around
Glib::RefPtr<Gtk::ListStore> g_store;

Cairo::RefPtr<Cairo::Surface> add_shadow(Geom::Point image_size, Cairo::RefPtr<Cairo::Surface> image, int device_scale) {
    auto w = image_size.x();
    auto h = image_size.y();
    auto margin = 6;
    auto width =  w + 2 * margin;
    auto height = h + 2 * margin;
    auto rect = Geom::Rect::from_xywh(margin, margin, w, h);

    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);

    // transparent background
    ctx->rectangle(0, 0, width, height);
    ctx->set_source_rgba(1, 1, 1, 0);
    ctx->fill();

    // white image background
    ctx->rectangle(margin, margin, w, h);
    ctx->set_source_rgba(1, 1, 1, 1);
    ctx->fill();

    // image (centered)
    auto imgw = cairo_image_surface_get_width(image->cobj()) / device_scale;
    auto imgh = cairo_image_surface_get_height(image->cobj()) / device_scale;
    auto cx = floor(margin + (w - imgw) / 2.0);
    auto cy = floor(margin + (h - imgh) / 2.0);
    ctx->set_source(image, cx, cy);
    ctx->paint();

    // drop shadow
    auto black = 0x000000;
    ink_cairo_draw_drop_shadow(ctx, rect, margin, black, 0.30);

    return surface;
}

const std::vector<Inkscape::Extension::Effect*> prepare_effects(const std::vector<Inkscape::Extension::Effect*>& effects) {
    std::vector<Inkscape::Extension::Effect*> out;

    std::copy_if(effects.begin(), effects.end(), std::back_inserter(out), [](auto effect) {
        return effect->is_filter_effect() || effect->hidden_from_menu() ? false : true;
    });

    return out;
}

void add_effects(Glib::RefPtr<Gtk::ListStore>& item_store, const std::vector<Inkscape::Extension::Effect*>& effects, int device_scale) {
    for (auto& effect : effects) {
        const auto id = effect->get_sanitized_id();

        auto row = *item_store->append();

        std::string name = effect->get_name();
        // remove ellipsis and mnemonics
        auto pos = name.find("...", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 3);
        }
        pos = name.find("…", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 1);
        }
        pos = name.find("_", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 1);
        }

        std::ostringstream order;
        std::ostringstream tooltip;
        auto menu = effect->get_menu_list();
        for (auto& part : menu) {
            order << part.raw() << '\n'; // effect sorting order
            tooltip << part.raw() << " \u25b8 "; // right pointing triangle; what about translations and RTL languages?
        }
        tooltip << name;
        order << name;

        row[g_effect_columns.id] = id;
        row[g_effect_columns.name] = name;
        row[g_effect_columns.tooltip] = tooltip.str();
        row[g_effect_columns.order] = order.str();
        row[g_effect_columns.category] = menu.empty() ? "" : menu.front();
        row[g_effect_columns.effect] = effect;

        auto image = Cairo::RefPtr<Cairo::Surface>();
        std::string dir(Inkscape::IO::Resource::get_path(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS));
        auto icon = effect->find_icon_file(dir);

        if (icon.empty()) {
            // fallback image
            icon = Inkscape::IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS, "resources", "missing-icon.svg");
        }
        if (Inkscape::IO::file_test(icon.c_str(), G_FILE_TEST_EXISTS)) {
            // render icon
            try {
                svg_renderer r(icon.c_str());
                auto icon_size = Geom::Point(70, 60);
                auto w = r.get_width_px();
                auto h = r.get_height_px();
                if (w > icon_size.x() || h > icon_size.y()) {
                    auto scale = std::max(w / icon_size.x(), h / icon_size.y());
                    r.set_scale(1 / scale);
                }
                image = add_shadow(icon_size, r.render_surface(device_scale), device_scale);
            }
            catch (...) {
                g_warning("Cannot render icon for effect %s", id.c_str());
            }
        }
        if (image) {
            row[g_effect_columns.image] = image;
        }
    }
}

std::set<std::string> add_categories(Glib::RefPtr<Gtk::ListStore>& store, const std::vector<Inkscape::Extension::Effect*>& effects, int device_scale) {
    std::set<std::string> categories;

    // collect categories
    for (auto& effect : effects) {
        auto menu = effect->get_menu_list();
        if (menu.empty()) continue;

        categories.insert(menu.front());
    }

    auto row = *store->append();
    row[g_categories_columns.id] = "all";
    row[g_categories_columns.name] = _("All Effects");

    row = *store->append();
    row[g_categories_columns.id] = "-";

    for (auto cat : categories) {
        auto row = *store->append();
        row[g_categories_columns.id] = cat;
        row[g_categories_columns.name] = cat;
        // row[g_categories_columns.icon] = cat;
    }

    return categories;
}


ExtensionsGallery::ExtensionsGallery() :
    DialogBase("/dialogs/extensions-gallery", "ExtensionsGallery"),
    _builder(create_builder("dialog-extensions.glade")),
    _grid(get_widget<Gtk::IconView>(_builder, "grid")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")),
    _run(get_widget<Gtk::Button>(_builder, "run")),
    _selector(get_widget<Gtk::TreeView>(_builder, "selector"))
{
    _run_label = _run.get_label();

    auto prefs = Inkscape::Preferences::get();
    // last selected effect
    auto selected = prefs->getString("/dialogs/extensions/selected");
    // selected category
    _current_category = prefs->getString("/dialogs/extensions/category", "all");
    auto show_list = prefs->getBool("/dialogs/extensions/show-list", true);
    auto position = prefs->getIntLimited("/dialogs/extensions/position", 120, 10, 1000);

    auto paned = &get_widget<Gtk::Paned>(_builder, "paned");
    auto show_categories_list = [=](bool show){
        paned->get_child1()->set_visible(show);
    };
    paned->set_position(position);
    paned->property_position().signal_changed().connect([=](){
        if (auto w = paned->get_child1()) {
            if (w->is_visible()) prefs->setInt("/dialogs/extensions/position", paned->get_position());
        }
    });

    // show/hide categories
    auto toggle = &get_widget<Gtk::ToggleButton>(_builder, "toggle");
    toggle->set_active(show_list);
    toggle->signal_toggled().connect([=](){
        auto visible = toggle->get_active();
        show_categories_list(visible);
        if (!visible) show_category("all"); // don't leave hidden category selection filter active
    });
    show_categories_list(show_list);

    if (!g_store) {
        g_store = Gtk::ListStore::create(g_effect_columns);
    }

    _categories = get_object<Gtk::ListStore>(_builder, "categories-store");
    _selector.set_row_separator_func([=](const Glib::RefPtr<Gtk::TreeModel>&, const Gtk::TreeModel::iterator& it){
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);
        return id == "-";
    });

    _store = g_store;
    _filtered = Gtk::TreeModelFilter::create(_store);
    auto model = Gtk::TreeModelSort::create(_filtered);

    auto effects = prepare_effects(Inkscape::Extension::db.get_effect_list());
    auto device_scale = get_scale_factor();

    if (_store->children().empty()) {
        auto app = InkscapeApplication::instance();
        if (!app) {
            g_warning("ExtensionGallery: no InkscapeApplication available");
            return;
        }
        add_effects(_store, effects, device_scale);
    }
    model->set_sort_column(g_effect_columns.order.index(), Gtk::SORT_ASCENDING);

    auto categories = add_categories(_categories, effects, device_scale);
    if (!categories.count(_current_category.raw())) {
        _current_category = "all";
    }
    _selector.set_model(_categories);

    _page_selection = _selector.get_selection();
    _selection_change = _page_selection->signal_changed().connect([=](){
        if (auto it = _page_selection->get_selected()) {
            Glib::ustring id;
            it->get_value(g_categories_columns.id.index(), id);
            show_category(id);
        }
    });

    _grid.pack_start(_image_renderer);
    _grid.add_attribute(_image_renderer, "surface", g_effect_columns.image);
    _grid.set_text_column(g_effect_columns.name);
    _grid.set_tooltip_column(g_effect_columns.tooltip.index());
    _grid.set_item_width(80);
    _grid.set_column_spacing(0);
    _grid.set_row_spacing(0);
    _grid.set_model(model);
    _grid.signal_item_activated().connect([=](const Gtk::TreeModel::Path& path){
        _run.clicked();
    });

    _search.signal_search_changed().connect([=](){
        refilter();
    });

    _filtered->set_visible_func([=](const Gtk::TreeModel::const_iterator& it){
        // filter by category
        if (_current_category != "all") {
            Glib::ustring cat = (*it)[g_effect_columns.category];
            if (_current_category != cat) return false;
        }

        // filter by name
        if (_search.get_text_length() == 0) return true;
    
        auto str = _search.get_text().lowercase();
        Glib::ustring text = (*it)[g_effect_columns.tooltip];
        return text.lowercase().find(str) != Glib::ustring::npos;
    });

    // restore selection: last used extension
    if (!selected.empty()) {
        model->foreach_path([=](const Gtk::TreeModel::Path& path){
            auto it = model->get_iter(path);
            if (selected == (*it)[g_effect_columns.id]) {
                _grid.select_path(path);
                return true;
            }
            return false;
        });
    }

    update_name();

    _grid.signal_selection_changed().connect([=](){
        update_name();
    });

    _categories->foreach([=](const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& it) {
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);

        if (id == _current_category) {
            _page_selection->select(path);
            return true;
        }
        return false;
    });

    refilter();

    add(get_widget<Gtk::Box>(_builder, "main"));
}

void ExtensionsGallery::update_name() {
    auto& label = get_widget<Gtk::Label>(_builder, "name");
    auto& info = get_widget<Gtk::Label>(_builder, "info");

    if (auto row = selected_item()) {
        label.set_label(row[g_effect_columns.tooltip]);
        label.set_tooltip_text(row[g_effect_columns.tooltip]);
        Glib::ustring id = row[g_effect_columns.id];

        // set action name
        gtk_actionable_set_action_name(GTK_ACTIONABLE(_run.gobj()), ("app." + id).c_str());
        _run.set_sensitive();
        // add ellipsis if extension takes input
        auto& effect = *row[g_effect_columns.effect];
        _run.set_label(_run_label + (effect.takes_input() ? _("...") : ""));
        // info: extension ID
        try {
            Glib::ustring id = effect.get_id();
            info.set_markup("<i>" + id + "</i>");
        }
        catch (...) {
            g_warning("Invalid effect ID: '%s'", effect.get_id());
        }

        auto prefs = Inkscape::Preferences::get();
        prefs->setString("/dialogs/extensions/selected", id);
    }
    else {
        label.set_label("");
        label.set_tooltip_text("");
        info.set_text("");
        _run.set_label(_run_label);
        _run.set_sensitive(false);
    }
}

Gtk::TreeModel::Row ExtensionsGallery::selected_item() {
    auto sel = _grid.get_selected_items();
    auto model = _grid.get_model();
    Gtk::TreeModel::Row row;
    if (sel.size() == 1 && model) {
        row = *model->get_iter(sel.front());
    }
    return row;
}

void ExtensionsGallery::show_category(const Glib::ustring& id) {
    if (_current_category == id) return;

    _current_category = id;

    auto prefs = Inkscape::Preferences::get();
    prefs->setString("/dialogs/extensions/category", id);

    refilter();
}

void ExtensionsGallery::refilter() {
g_message("refilter");
    // filter results
    _filtered->freeze_notify();
    _filtered->refilter();
    _filtered->thaw_notify();
}

} } } // namespaces
