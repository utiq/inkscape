// SPDX-License-Identifier: GPL-2.0-or-later

#include "extensions-gallery.h"

#include <cairo.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <fstream>
#include <sstream>

#include "display/cairo-utils.h"
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
    Gtk::TreeModelColumn<Cairo::RefPtr<Cairo::Surface>> image;

    EffectColumns() {
        add(id);
        add(name);
        add(tooltip);
        add(order);
        add(image);
    }
} g_effect_columns;

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

void add_effects(Glib::RefPtr<Gtk::ListStore>& item_store, const std::vector<InkActionEffectData::datum>& effects, int device_scale) {
    for (auto& [entry_id, submenu_name_list, entry_name] : effects) {
        if (submenu_name_list.empty()) continue;

        auto type = submenu_name_list.front();
        if (type != "Effects") continue;

        auto row = *item_store->append();

        std::string name = entry_name;
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

        std::ostringstream ost;
        std::ostringstream tooltip;
        for (auto& part : submenu_name_list) {
            ost << part << '|';
            tooltip << part << " \u25b8 "; // right pointing triangle; what about translations and RTL languages?
        }
        tooltip << name;

        row[g_effect_columns.id] = entry_id;
        row[g_effect_columns.name] = name;
        row[g_effect_columns.tooltip] = tooltip.str();
        row[g_effect_columns.order] = ost.str();

        auto image = Cairo::RefPtr<Cairo::Surface>();
        auto fpath = Inkscape::IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS, "effect-icons", (entry_id + ".svg").c_str());
        if (!Inkscape::IO::file_test(fpath.c_str(), G_FILE_TEST_EXISTS)) {
            // check in user's extensions folder too
            fpath = Inkscape::IO::Resource::get_path_string(IO::Resource::USER, IO::Resource::EXTENSIONS, (entry_id + ".svg").c_str());
        }
        if (!Inkscape::IO::file_test(fpath.c_str(), G_FILE_TEST_EXISTS)) {
            // fallback image
            fpath = Inkscape::IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS, "effect-icons", "missing-icon.svg");
        }
        if (Inkscape::IO::file_test(fpath.c_str(), G_FILE_TEST_EXISTS)) {
            // render icon
            try {
                svg_renderer r(fpath.c_str());
                auto icon_size = Geom::Point(70, 60);
                image = add_shadow(icon_size, r.render_surface(device_scale), device_scale);
            }
            catch (...) {
                g_warning("Cannot render icon for effect %s", entry_id.c_str());
            }
        }
        if (image) {
            row[g_effect_columns.image] = image;
        }
    }
}

ExtensionsGallery::ExtensionsGallery() :
    DialogBase("/dialogs/extensions-gallery", "ExtensionsGallery"),
    _builder(create_builder("dialog-extensions.glade")),
    _grid(get_widget<Gtk::IconView>(_builder, "grid")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")),
    _run(get_widget<Gtk::Button>(_builder, "run"))
{
    auto prefs = Inkscape::Preferences::get();
    auto selected = prefs->getString("/dialogs/extensions/selected");

    if (!g_store) {
        g_store = Gtk::ListStore::create(g_effect_columns);
    }

    _store = g_store;
    auto filtered= Gtk::TreeModelFilter::create(_store);
    auto model = Gtk::TreeModelSort::create(filtered);

    if (_store->children().empty()) {
        auto device_scale = get_scale_factor();
        auto app = InkscapeApplication::instance();
        if (!app) {
            g_warning("ExtensionGallery: no InkscapeApplication available");
            return;
        }
        add_effects(_store, app->get_action_effect_data().give_all_data(), device_scale);
    }
    model->set_sort_column(g_effect_columns.order.index(), Gtk::SORT_ASCENDING);

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
        // filter results
        filtered->freeze_notify();
        filtered->refilter();
        filtered->thaw_notify();
    });

    filtered->set_visible_func([=](const Gtk::TreeModel::const_iterator& it){
        if (_search.get_text_length() == 0) return true;
    
        auto str = _search.get_text().lowercase();
        Glib::ustring name = (*it)[g_effect_columns.name];
        return name.lowercase().find(str) != Glib::ustring::npos;
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

    add(get_widget<Gtk::Box>(_builder, "main"));
}

void ExtensionsGallery::update_name() {
    auto& label = get_widget<Gtk::Label>(_builder, "name");

    if (auto row = selected_item()) {
        label.set_label(row[g_effect_columns.name]);
        label.set_tooltip_text(row[g_effect_columns.tooltip]);
        Glib::ustring id = row[g_effect_columns.id];

        gtk_actionable_set_action_name(GTK_ACTIONABLE(_run.gobj()), ("app." + id).c_str());
        _run.set_sensitive();
        get_widget<Gtk::Label>(_builder, "info").set_markup("<i>" + row[g_effect_columns.id] + "</i>");

        auto prefs = Inkscape::Preferences::get();
        prefs->setString("/dialogs/extensions/selected", id);
    }
    else {
        label.set_label("");
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

} } } // namespaces
