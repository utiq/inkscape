// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_PATTERN_EDITOR_H
#define SEEN_PATTERN_EDITOR_H

#include <gtkmm/box.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/grid.h>
#include <gtkmm/button.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/builder.h>
#include <optional>
#include <2geom/transforms.h>
#include "color.h"
#include "object/sp-pattern.h"
#include "spin-scale.h"
#include "ui/operation-blocker.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/pattern-store.h"

class SPDocument;
class ColorPicker;

namespace Inkscape {
namespace UI {
namespace Widget {

class PatternEditor : public Gtk::Box {
public:
    PatternEditor(const char* prefs);
    ~PatternEditor() noexcept override;

    // pass document with stock patterns
    void set_stock_patterns(SPDocument* patterns);
    // pass current document to extract patterns
    void set_document(SPDocument* document);
    // set selected pattern
    void set_selected(SPPattern* pattern);
    // selected pattern ID if any plus stock pattern flag
    std::pair<std::string, bool> get_selected();
    // and its color
    std::optional<unsigned int> get_selected_color();
    // return combined scale and rotation
    Geom::Affine get_selected_transform();
    // return pattern offset
    Geom::Point get_selected_offset();
    // is scale uniform?
    bool is_selected_scale_uniform();
    // return gap size for pattern tiles
    Geom::Scale get_selected_gap();

private:
    sigc::signal<void/*, SPPattern*/> _signal_changed;
    sigc::signal<void, unsigned int> _signal_color_changed;
    sigc::signal<void> _signal_edit;

public:
    decltype(_signal_changed) signal_changed() const { return _signal_changed; }
    decltype(_signal_color_changed) signal_color_changed() const { return _signal_color_changed; }
    decltype(_signal_edit) signal_edit() const { return _signal_edit; }

private:
    void bind_store(Gtk::FlowBox& list, PatternStore& store);
    void update_store(const std::vector<Glib::RefPtr<PatternItem>>& list, Gtk::FlowBox& gallery, PatternStore& store);
    Glib::RefPtr<PatternItem> get_active(Gtk::FlowBox& gallery, PatternStore& pat);
    std::pair<Glib::RefPtr<PatternItem>, bool> get_active();
    void set_active(Gtk::FlowBox& gallery, PatternStore& pat, Glib::RefPtr<PatternItem> item);
    void update_widgets_from_pattern(Glib::RefPtr<PatternItem>& pattern);
    void update_scale_link();
    void update_ui(Glib::RefPtr<PatternItem> pattern);
    std::vector<Glib::RefPtr<PatternItem>> set_document_patterns(SPDocument* document);

    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::Box& _main_grid;
    Gtk::Grid& _input_grid;
    Gtk::SpinButton& _offset_x;
    Gtk::SpinButton& _offset_y;
    Gtk::SpinButton& _scale_x;
    Gtk::SpinButton& _scale_y;
    Gtk::SpinButton& _angle_btn;
    Gtk::Scale& _orient_slider;
    Gtk::Scale& _gap_x_slider;
    Gtk::Scale& _gap_y_slider;
    Gtk::Button& _edit_btn;
    Gtk::Label& _color_label;
    Gtk::Button& _color_btn;
    Gtk::Button& _link_scale;
    Gtk::Label& _id_label;
    Gtk::Image& _preview_img;
    Gtk::Viewport& _preview;
    Gtk::FlowBox& _doc_gallery;
    Gtk::FlowBox& _stock_gallery;
    bool _scale_linked = true;
    Glib::ustring _prefs;
    PatternStore _doc_pattern_store;
    PatternStore _stock_pattern_store;
    std::shared_ptr<SPDocument> _preview_doc;
    std::shared_ptr<SPDocument> _big_preview_doc;
    std::unique_ptr<ColorPicker> _color_picker;
    OperationBlocker _update;
};


} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif
