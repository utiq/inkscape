// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Pattern editor widget for "Fill and Stroke" dialog
 *
 * Copyright (C) 2022 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "pattern-editor.h"

#include <optional>
#include <gtkmm/builder.h>
#include <gtkmm/grid.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/button.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/treeview.h>
#include <gtkmm/treemodelcolumn.h>
#include <glibmm/i18n.h>
#include <cairo.h>
#include <iomanip>

#include "object/sp-defs.h"
#include "object/sp-root.h"
#include "style.h"
#include "ui/builder-utils.h"
#include "ui/svg-renderer.h"
#include "io/resource.h"
#include "manipulation/copy-resource.h"
#include "pattern-manipulation.h"
#include "util/units.h"

namespace Inkscape {
namespace UI {
namespace Widget {

using namespace Inkscape::IO;

// size of pattern image in a list
static const int ITEM_WIDTH = 45;
static const int ITEM_HEIGHT = 45;

// get slider position 'index' (linear) and transform that into gap percentage (non-linear)
static double slider_to_gap(double index, double upper) {
    auto v = std::tan(index / (upper + 1) * M_PI / 2.0) * 500;
    return std::round(v / 20) * 20;
}

// transform gap percentage value into slider position
static double gap_to_slider(double gap, double upper) {
    return std::atan(gap / 500) * (upper + 1) / M_PI * 2;
}

std::shared_ptr<SPDocument> get_preview_document() {
char const* buffer = R"A(
<svg width="40" height="40" viewBox="0 0 40 40"
   xmlns:xlink="http://www.w3.org/1999/xlink"
   xmlns="http://www.w3.org/2000/svg">
  <defs id="defs">
  </defs>
  <g id="layer1">
    <rect
       style="fill:#f0f0f0;fill-opacity:1;stroke:none"
       id="rect2620"
       width="100%" height="100%" x="0" y="0" />
    <rect
       style="fill:url(#sample);fill-opacity:1;stroke:black;stroke-opacity:0.3;stroke-width:1px"
       id="rect236"
       width="100%" height="100%" x="0" y="0" />
  </g>
</svg>
)A";
    return std::shared_ptr<SPDocument>(SPDocument::createNewDocFromMem(buffer, strlen(buffer), false));
}

// pattern preview document without background
std::shared_ptr<SPDocument> get_big_preview_document() {
char const* buffer = R"A(
<svg width="100" height="100"
   xmlns:xlink="http://www.w3.org/1999/xlink"
   xmlns="http://www.w3.org/2000/svg">
  <defs id="defs">
  </defs>
  <g id="layer1">
    <rect
       style="fill:url(#sample);fill-opacity:1;stroke:none"
       width="100%" height="100%" x="0" y="0" />
  </g>
</svg>
)A";
    return std::shared_ptr<SPDocument>(SPDocument::createNewDocFromMem(buffer, strlen(buffer), false));
}

Glib::ustring get_attrib(SPPattern* pattern, const char* attrib) {
    auto value = pattern->getAttribute(attrib);
    return value ? value : "";
}

double get_attrib_num(SPPattern* pattern, const char* attrib) {
    auto val = get_attrib(pattern, attrib);
    return strtod(val.c_str(), nullptr);
}

const double ANGLE_STEP = 15.0;

PatternEditor::PatternEditor(const char* prefs) :
    _builder(create_builder("pattern-edit.glade")),
    _offset_x(get_widget<Gtk::SpinButton>(_builder, "offset-x")),
    _offset_y(get_widget<Gtk::SpinButton>(_builder, "offset-y")),
    _scale_x(get_widget<Gtk::SpinButton>(_builder, "scale-x")),
    _scale_y(get_widget<Gtk::SpinButton>(_builder, "scale-y")),
    _angle_btn(get_widget<Gtk::SpinButton>(_builder, "angle")),
    _orient_slider(get_widget<Gtk::Scale>(_builder, "orient")),
    _gap_x_slider(get_widget<Gtk::Scale>(_builder, "gap-x")),
    _gap_y_slider(get_widget<Gtk::Scale>(_builder, "gap-y")),
    _edit_btn(get_widget<Gtk::Button>(_builder, "edit-pattern")),
    _id_label(get_widget<Gtk::Label>(_builder, "pattern-id")),
    _preview_img(get_widget<Gtk::Image>(_builder, "preview")),
    _preview(get_widget<Gtk::Viewport>(_builder, "preview-box")),
    _color_btn(get_widget<Gtk::Button>(_builder, "color-btn")),
    _color_label(get_widget<Gtk::Label>(_builder, "color-label")),
    _main_grid(get_widget<Gtk::Box>(_builder, "main-box")),
    _input_grid(get_widget<Gtk::Grid>(_builder, "input-grid")),
    _stock_gallery(get_widget<Gtk::FlowBox>(_builder, "flowbox")),
    _doc_gallery(get_widget<Gtk::FlowBox>(_builder, "doc-flowbox")),
    _link_scale(get_widget<Gtk::Button>(_builder, "link-scale")),
    _prefs(prefs)
{
    _color_picker = std::make_unique<ColorPicker>(
        _("Pattern color"), "", 0x7f7f7f00, true,
        &get_widget<Gtk::Button>(_builder, "color-btn"));
    _color_picker->use_transparency(false);
    _color_picker->connectChanged([=](guint color){
        if (_update.pending()) return;
        _signal_color_changed.emit(color);
    });

    const auto max = 180.0 / ANGLE_STEP;
    _orient_slider.set_range(-max, max);
    _orient_slider.set_increments(1, 1);
    _orient_slider.set_digits(0);
    _orient_slider.set_value(0);
    _orient_slider.signal_change_value().connect([=](Gtk::ScrollType st, double value){
        if (_update.pending()) return false;
        auto scoped(_update.block());
        // slider works with 15deg discrete steps
        _angle_btn.set_value(round(CLAMP(value, -max, max)) * ANGLE_STEP);
        _signal_changed.emit();
        return true;
    });

    for (auto slider : {&_gap_x_slider, &_gap_y_slider}) {
        slider->set_increments(1, 1);
        slider->set_digits(0);
        slider->set_value(0);
        slider->signal_format_value().connect([=](double val){
            auto upper = slider->get_adjustment()->get_upper();
            return Glib::ustring::format(std::fixed, std::setprecision(0), slider_to_gap(val, upper)) + "%";
        });
        slider->signal_change_value().connect([=](Gtk::ScrollType st, double value){
            if (_update.pending()) return false;
            _signal_changed.emit();
            return true;
        });
    }

    _angle_btn.signal_value_changed().connect([=]() {
        if (_update.pending() || !_angle_btn.is_sensitive()) return;
        auto scoped(_update.block());
        auto angle = _angle_btn.get_value();
        _orient_slider.set_value(round(angle / ANGLE_STEP));
        _signal_changed.emit();
    });

    _link_scale.signal_clicked().connect([=](){
        if (_update.pending()) return;
        auto scoped(_update.block());
        _scale_linked = !_scale_linked;
        if (_scale_linked) {
            // this is simplistic
            _scale_x.set_value(_scale_y.get_value());
        }
        update_scale_link();
        _signal_changed.emit();
    });

    for (auto el : {&_scale_x, &_scale_y, &_offset_x, &_offset_y}) {
        el->signal_value_changed().connect([=]() {
            if (_update.pending()) return;
            if (_scale_linked && (el == &_scale_x || el == &_scale_y)) {
                auto scoped(_update.block());
                // enforce uniform scaling
                (el == &_scale_x) ? _scale_y.set_value(el->get_value()) : _scale_x.set_value(el->get_value());
            }
            _signal_changed.emit();
        });
    }
    _preview_doc = get_preview_document();
    if (!_preview_doc.get() || !_preview_doc->getReprDoc()) {
        throw std::runtime_error("Pattern embedded preview document cannot be loaded");
    }
    _preview_doc->setWidth(Inkscape::Util::Quantity(ITEM_WIDTH, "px"));
    _preview_doc->setHeight(Inkscape::Util::Quantity(ITEM_HEIGHT, "px"));

    _big_preview_doc = get_big_preview_document();

    bind_store(_doc_gallery, _doc_pattern_store);
    bind_store(_stock_gallery, _stock_pattern_store);

    _stock_gallery.signal_child_activated().connect([=](Gtk::FlowBoxChild* box){
        if (_update.pending()) return;
        auto scoped(_update.block());
        auto pat = _stock_pattern_store.widgets_to_pattern[box];
        update_ui(pat);
        _doc_gallery.unselect_all();
        _signal_changed.emit();
    });

    _doc_gallery.signal_child_activated().connect([=](Gtk::FlowBoxChild* box){
        if (_update.pending()) return;
        auto scoped(_update.block());
        auto pat = _doc_pattern_store.widgets_to_pattern[box];
        update_ui(pat);
        _stock_gallery.unselect_all();
        _signal_changed.emit();
    });

    _edit_btn.signal_clicked().connect([=](){
        _signal_edit.emit();
    });

    update_scale_link();
    pack_start(_main_grid);
}

PatternEditor::~PatternEditor() noexcept {}

void PatternEditor::bind_store(Gtk::FlowBox& list, PatternStore& pat) {
    list.bind_list_store(pat.store, [=, &pat](const Glib::RefPtr<PatternItem>& item){
        auto image = Gtk::make_managed<Gtk::Image>(item->pix);
        image->show();
        auto box = Gtk::make_managed<Gtk::FlowBoxChild>();
        box->add(*image);
        box->get_style_context()->add_class("pattern-item-box");
        pat.widgets_to_pattern[box] = item;
        box->set_size_request(ITEM_WIDTH, ITEM_HEIGHT);
        return box;
    });
}

void PatternEditor::update_scale_link() {
    _link_scale.remove();
    _link_scale.add(get_widget<Gtk::Image>(_builder, _scale_linked ? "image-linked" : "image-unlinked"));
}

std::string get_pattern_label(SPPattern* pattern) {
    if (!pattern) return std::string();

    Inkscape::XML::Node* repr = pattern->getRepr();
    const char* stock_id = _(repr->attribute("inkscape:stockid"));
    const char* pat_id = stock_id ? stock_id : _(repr->attribute("id"));
    return std::string(pat_id ? pat_id : "");
}

void PatternEditor::update_widgets_from_pattern(Glib::RefPtr<PatternItem>& pattern) {
    _input_grid.set_sensitive(!!pattern);

    PatternItem empty;
    const auto& item = pattern ? *pattern.get() : empty;

    _id_label.set_text(item.label.c_str());

    _scale_x.set_value(item.transform.xAxis().length());
    _scale_y.set_value(item.transform.yAxis().length());

    // TODO if needed
    // auto units = get_attrib(pattern, "patternUnits");

    _scale_linked = item.uniform_scale;
    update_scale_link();

    _offset_x.set_value(item.offset.x());
    _offset_y.set_value(item.offset.y());

    auto degrees = 180.0 / M_PI * Geom::atan2(item.transform.xAxis());
    _orient_slider.set_value(round(degrees / ANGLE_STEP));
    _angle_btn.set_value(degrees);

    double x_index = gap_to_slider(item.gap[Geom::X], _gap_x_slider.get_adjustment()->get_upper());
    _gap_x_slider.set_value(x_index);
    double y_index = gap_to_slider(item.gap[Geom::Y], _gap_y_slider.get_adjustment()->get_upper());
    _gap_y_slider.set_value(y_index);

    if (item.color.has_value()) {
        _color_picker->setRgba32(item.color->toRGBA32(1.0));
        _color_btn.set_sensitive();
        _color_label.set_sensitive();
    }
    else {
        _color_picker->setRgba32(0);
        _color_btn.set_sensitive(false);
        _color_label.set_sensitive(false);
        _color_picker->closeWindow();
    }

    std::ostringstream ost;
    ost << "<small>" << item.label << "</small>";
    _id_label.set_markup(ost.str().c_str());
}

void PatternEditor::update_ui(Glib::RefPtr<PatternItem> pattern) {
    update_widgets_from_pattern(pattern);
}

// sort patterns in-place by name/id
void sort_patterns(std::vector<Glib::RefPtr<PatternItem>>& list) {
    std::sort(list.begin(), list.end(), [](Glib::RefPtr<PatternItem>& a, Glib::RefPtr<PatternItem>& b) {
        if (a->label == b->label) {
            return a->id < b->id;
        }
        return a->label < b->label;
    });
}

Cairo::RefPtr<Cairo::Surface> create_pattern_image(std::shared_ptr<SPDocument>& sandbox,
    char const* name, SPDocument* source, double scale,
    std::optional<unsigned int> checkerboard = std::optional<unsigned int>()) {

    // Retrieve the pattern named 'name' from the source SVG document
    SPObject const* pattern = source->getObjectById(name);
    if (pattern == nullptr) {
        g_warning("bad name: %s", name);
        return Cairo::RefPtr<Cairo::Surface>();
    }

    auto list = sandbox->getDefs()->childList(true);
    for (auto obj : list) {
        obj->deleteObject();
        sp_object_unref(obj);
    }
    // SPObject* oldpattern = sandbox->getObjectById("sample");
    // if (oldpattern) {
    //     oldpattern->deleteObject(false);
    // }
    SPDocument::install_reference_document scoped(sandbox.get(), source);

    // Create a copy of the pattern, name it "sample"
    auto copy = sp_copy_resource(pattern, sandbox.get());
    copy->getRepr()->setAttribute("id", "sample");

    sandbox->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    sandbox->ensureUpToDate();

    svg_renderer renderer(sandbox);
    if (checkerboard.has_value()) {
        renderer.set_checkerboard_color(*checkerboard);
    }
    auto surface = renderer.render_surface(scale);
    if (surface) {
        cairo_surface_set_device_scale(surface->cobj(), scale, scale);
    }
    return surface;
}

// given a pattern, create a PatternItem instance that describes it;
// input pattern can be a link or a root pattern
Glib::RefPtr<PatternItem> create_pattern_item(std::shared_ptr<SPDocument>& sandbox, SPPattern* pattern, bool stock_pattern, double scale) {
    if (!pattern) return Glib::RefPtr<PatternItem>();

    auto item = Glib::RefPtr<PatternItem>(new PatternItem);

    //  this is a link:       this is a root:
    // <pattern href="abc"/> <pattern id="abc"/>
    // if pattern is a root one to begin with, then both pointers will be the same:
    auto link_pattern = pattern;
    auto root_pattern = pattern->rootPattern();

    // get label and ID from root pattern
    Inkscape::XML::Node* repr = root_pattern->getRepr();
    if (auto id = repr->attribute("id")) {
        item->id = id;
    }
    item->label = get_pattern_label(root_pattern);
    item->stock = stock_pattern;
    // read transformation from a link pattern
    item->transform = link_pattern->get_this_transform();
    item->offset = Geom::Point(link_pattern->x(), link_pattern->y());

    // reading color style form "root" pattern; linked one won't have any effect, as it's not a parrent
    if (root_pattern->style && root_pattern->style->isSet(SPAttr::FILL) && root_pattern->style->fill.isColor()) {
        item->color.emplace(SPColor(root_pattern->style->fill.value.color));
    }
    // uniform scaling?
    if (link_pattern->aspect_set) {
        auto preserve = link_pattern->getAttribute("preserveAspectRatio");
        item->uniform_scale = preserve && strcmp(preserve, "none") != 0;
    }
    // pattern tile gap
    item->gap = sp_pattern_get_gap(link_pattern);

    if (sandbox) {
        // generate preview
        item->pix = create_pattern_image(sandbox, link_pattern->getId(), link_pattern->document, scale);
    }

    return item;
}

// update editor UI
void PatternEditor::set_selected(SPPattern* pattern) {
    auto scoped(_update.block());

    _stock_gallery.unselect_all();

    // current pattern (should be a link)
    auto link_pattern = pattern;
    if (pattern) pattern = pattern->rootPattern();

    const double device_scale = get_scale_factor();
    auto item = create_pattern_item(_preview_doc, link_pattern, false, device_scale);

    if (item) {
        if (auto id = link_pattern->getRepr()->attribute("id")) {
            item->link_id = id;
        }
    }
    update_widgets_from_pattern(item);

    auto patterns = set_document_patterns(pattern ? pattern->document : nullptr);

    // patch up PatternItem corresponding to current 'pattern'; we need link_id and transform
    // because what's on the list cames from root pattern, and current one should be a link pattern
    if (item) {
        for (auto p : patterns) {
            if (p->id == item->id) {
                p->link_id = item->link_id;
                p->transform = item->transform;
                break;
            }
        }
    }

    set_active(_doc_gallery, _doc_pattern_store, item);

    // generate large preview of selected pattern
    Cairo::RefPtr<Cairo::Surface> surface;
    if (pattern && _big_preview_doc) {
        const double device_scale = get_scale_factor();
        auto size = _preview.get_allocation();
        const int m = 1;
        if (size.get_width() <= m || size.get_height() <= m) {
            // widgets not resized yet, choose arbitrary size, so preview is not missing when widget is shown
            size.set_width(200);
            size.set_height(200);
        }
        _big_preview_doc->setWidth(Inkscape::Util::Quantity(size.get_width() - m, "px"));
        _big_preview_doc->setHeight(Inkscape::Util::Quantity(size.get_height() - m, "px"));
        // use white for checkerboard since most stock patterns are black
        unsigned int background = 0xffffffff;
        surface = create_pattern_image(_big_preview_doc, link_pattern->getId(), link_pattern->document, device_scale, background);
    }
    _preview_img.set(surface);
}

// generate preview images for patterns
std::vector<Glib::RefPtr<PatternItem>> create_pattern_items(const std::vector<SPPattern*>& list, bool stock, double device_scale, std::shared_ptr<SPDocument>& preview) {
    std::vector<Glib::RefPtr<PatternItem>> output;
    output.reserve(list.size());

    for (auto pat : list) {
        output.push_back(create_pattern_item(preview, pat, stock, device_scale));
    }

    return output;
}

void PatternEditor::set_document(SPDocument* document) {
    set_document_patterns(document);
}

// populate store with document patterns
std::vector<Glib::RefPtr<PatternItem>> PatternEditor::set_document_patterns(SPDocument* document) {
    auto list = sp_get_pattern_list(document);
    const double device_scale = get_scale_factor();
    auto patterns = create_pattern_items(list, false, device_scale, _preview_doc);
    update_store(patterns, _doc_gallery, _doc_pattern_store);
    return patterns;
}

// populate store with stock patterns
void PatternEditor::set_stock_patterns(SPDocument* patterns_doc) {
    auto list = sp_get_pattern_list(patterns_doc);
    const double device_scale = get_scale_factor();
    auto patterns = create_pattern_items(list, true, device_scale, _preview_doc);
    sort_patterns(patterns);
    update_store(patterns, _stock_gallery, _stock_pattern_store);

    // now we need to preselect one pattern, so there's something available initially
    // when users switch to pattern fill in F&S dialog; otherwise nothing gets assigned
    if (!patterns.empty()) {
        auto scoped(_update.block());
        auto pat = patterns.front();
        update_ui(pat);
        if (auto first = _stock_gallery.get_child_at_index(0)) {
            _stock_gallery.select_child(*first);
        }
    }
}

void PatternEditor::update_store(const std::vector<Glib::RefPtr<PatternItem>>& list, Gtk::FlowBox& gallery, PatternStore& pat) {
    pat.store->freeze_notify();

    auto selected = get_active(gallery, pat);

    pat.store->remove_all();
    pat.widgets_to_pattern.clear();

    for (auto&& pattern : list) {
        pat.store->append(pattern);
    }

    pat.store->thaw_notify();

    // reselect current
    set_active(gallery, pat, selected);
}

Glib::RefPtr<PatternItem> PatternEditor::get_active(Gtk::FlowBox& gallery, PatternStore& pat) {
    auto empty = Glib::RefPtr<PatternItem>();

    auto sel = gallery.get_selected_children();
    if (sel.size() == 1) {
        return pat.widgets_to_pattern[sel.front()];
    }
    else {
        return empty;
    }
}

std::pair<Glib::RefPtr<PatternItem>, bool> PatternEditor::get_active() {
    bool stock = false;
    auto sel = get_active(_doc_gallery, _doc_pattern_store);
    if (!sel) {
        stock = true;
        sel = get_active(_stock_gallery, _stock_pattern_store);
    }
    return std::make_pair(sel, stock);
}

void PatternEditor::set_active(Gtk::FlowBox& gallery, PatternStore& pat, Glib::RefPtr<PatternItem> item) {
    bool selected = false;
    if (item) {
        gallery.foreach([=,&selected,&pat,&gallery](Gtk::Widget& widget){
            if (auto box = dynamic_cast<Gtk::FlowBoxChild*>(&widget)) {
                if (auto pattern = pat.widgets_to_pattern[box]) {
                    if (pattern->id == item->id) {
                        // pattern->link_id = item->link_id;
                        // pattern->transform = item->transform;
                        gallery.select_child(*box);
                        selected = true;
                    }
                }
            }
        });
    }

    if (!selected) {
        gallery.unselect_all();
    }
}

std::pair<std::string, bool> PatternEditor::get_selected() {
    // document patterns first
    auto active = get_active();
    auto sel = active.first;
    auto stock = active.second;
    std::string id;
    if (sel && !active.second) {
        // document pattern; maybe link pattern, not root
        id = sel->link_id;
    }
    if (sel) {
        if (id.empty()) id = sel->id;

        return std::make_pair(id, stock);
    }

    // none
    return std::make_pair("", false);
}

std::optional<unsigned int> PatternEditor::get_selected_color() {
    auto pat = get_active();
    if (pat.first && pat.first->color.has_value()) {
        return _color_picker->get_current_color();
    }
    return std::optional<unsigned int>(); // color not supported
}

Geom::Point PatternEditor::get_selected_offset() {
    return Geom::Point(_offset_x.get_value(), _offset_y.get_value());
}

Geom::Affine PatternEditor::get_selected_transform() {
    Geom::Affine matrix;

    matrix *= Geom::Scale(_scale_x.get_value(), _scale_y.get_value());
    matrix *= Geom::Rotate(_angle_btn.get_value() / 180.0 * M_PI);
    auto pat = get_active();
    if (pat.first) {
        //TODO: this is imperfect; calculate better offset, if possible
        // this translation is kept so there's no sudden jump when editing pattern attributes
        matrix.setTranslation(pat.first->transform.translation());
    }
    return matrix;
}

bool PatternEditor::is_selected_scale_uniform() {
    return _scale_linked;
}

Geom::Scale PatternEditor::get_selected_gap() {
    auto vx = _gap_x_slider.get_value();
    auto gap_x = slider_to_gap(vx, _gap_x_slider.get_adjustment()->get_upper());

    auto vy = _gap_y_slider.get_value();
    auto gap_y = slider_to_gap(vy, _gap_y_slider.get_adjustment()->get_upper());

    return Geom::Scale(gap_x, gap_y);
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape
