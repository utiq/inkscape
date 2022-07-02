// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Symbols dialog.
 */
/* Authors:
 * Copyright (C) 2012 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include "symbols.h"

#include <iostream>
#include <algorithm>
#include <locale>
#include <sstream>
#include <fstream>
#include <regex>

#include <glibmm/i18n.h>
#include <glibmm/markup.h>
#include <glibmm/regex.h>
#include <glibmm/stringutils.h>

#include "document.h"
#include "inkscape.h"
#include "path-prefix.h"
#include "selection.h"

#include "display/cairo-utils.h"
#include "include/gtkmm_version.h"
#include "io/resource.h"
#include "io/sys.h"
#include "object/sp-defs.h"
#include "object/sp-root.h"
#include "object/sp-symbol.h"
#include "object/sp-use.h"
#include "ui/cache/svg_preview_cache.h"
#include "ui/clipboard.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/widget/scrollprotected.h"
#include "xml/href-attribute-helper.h"

#ifdef WITH_LIBVISIO
  #include <libvisio/libvisio.h>

  #include <librevenge-stream/librevenge-stream.h>

  using librevenge::RVNGFileStream;
  using librevenge::RVNGString;
  using librevenge::RVNGStringVector;
  using librevenge::RVNGPropertyList;
  using librevenge::RVNGSVGDrawingGenerator;
#endif


namespace Inkscape {
namespace UI {

namespace Dialog {
static std::map<std::string, std::pair<Glib::ustring, SPDocument*> > symbol_sets;
/**
 * Constructor
 */
SymbolsDialog::SymbolsDialog(gchar const *prefsPath)
    : DialogBase(prefsPath, "Symbols")
    , _columns{}
    , store(Gtk::ListStore::create(_columns))
    , all_docs_processed(false)
    , icon_view(nullptr)
    , preview_document(nullptr)
    , gtk_connections()
    , CURRENTDOC(_("Current document"))
    , ALLDOCS(_("All symbol sets"))
{
  /********************    Table    *************************/
  auto table = new Gtk::Grid();

  table->set_margin_start(3);
  table->set_margin_end(3);
  table->set_margin_top(4);
  // panel is a locked Gtk::VBox
  pack_start(*Gtk::manage(table), Gtk::PACK_EXPAND_WIDGET);
  guint row = 0;

  /******************** Symbol Sets *************************/
  Gtk::Label* label_set = new Gtk::Label(Glib::ustring(_("Symbol set")) + ": ");
  table->attach(*Gtk::manage(label_set),0,row,1,1);
  symbol_set = new Inkscape::UI::Widget::ScrollProtected<Gtk::ComboBoxText>();  // Fill in later
  symbol_set->append(CURRENTDOC);
  symbol_set->append(ALLDOCS);
  symbol_set->set_active_text(CURRENTDOC);
  symbol_set->set_hexpand();
  auto cb = dynamic_cast<Gtk::ComboBoxText *>(symbol_set);
  if (cb) {
      auto renderer = dynamic_cast<Gtk::CellRendererText *>(cb->get_cells()[0]); //Get the ComboBoxText only renderer
      if (renderer) {
          // not needed because container limit
          // renderer->property_width_chars() = 12; // always show min 15 chars
          renderer->property_ellipsize() = Pango::EllipsizeMode::ELLIPSIZE_END;
      }
  }
  gtk_connections.emplace_back(
      symbol_set->signal_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::rebuild)));

  table->attach(*Gtk::manage(symbol_set),1,row,1,1);
  ++row;

  /********************    Separator    *************************/


  Gtk::Separator* separator = Gtk::manage(new Gtk::Separator());  // Search
  separator->set_margin_top(10);
  separator->set_margin_bottom(10);
  table->attach(*Gtk::manage(separator),0,row,2,1);

  ++row;

  /********************    Search    *************************/

  search = Gtk::manage(new Gtk::SearchEntry());  // Search
  search->set_tooltip_text(_("Press 'Return' to start search."));
  search->signal_key_press_event().connect_notify(  sigc::mem_fun(*this, &SymbolsDialog::beforeSearch));
  search->signal_key_release_event().connect_notify(sigc::mem_fun(*this, &SymbolsDialog::unsensitive));

  search->set_margin_bottom(6);
  search->signal_search_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::clearSearch));
  table->attach(*Gtk::manage(search),0,row,2,1);
  search_str = "";

  ++row;


  /********************* Icon View **************************/
  icon_view = new Gtk::IconView(static_cast<Glib::RefPtr<Gtk::TreeModel> >(store));
  icon_view->set_tooltip_column(_columns.symbol_title.index());
  icon_view->set_pixbuf_column(_columns.symbol_image);
  // Giving the iconview a small minimum size will help users understand
  // What the dialog does.
  icon_view->set_size_request( 100, 250 );
  icon_view->set_vexpand(true);
  std::vector< Gtk::TargetEntry > targets;
  targets.emplace_back( "application/x-inkscape-paste");

  icon_view->enable_model_drag_source (targets, Gdk::BUTTON1_MASK, Gdk::ACTION_COPY);
  gtk_connections.emplace_back(
      icon_view->signal_drag_data_get().connect(sigc::mem_fun(*this, &SymbolsDialog::iconDragDataGet)));
  gtk_connections.emplace_back(
      icon_view->signal_selection_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::iconChanged)));
  gtk_connections.emplace_back(
      icon_view->signal_drag_begin().connect(
          [=](Glib::RefPtr<Gdk::DragContext> const &) { onDragStart(); }));
  gtk_connections.emplace_back(icon_view->signal_button_press_event().connect([=](GdkEventButton *ev) -> bool {
      _last_mousedown = {ev->x, ev->y - icon_view->get_vadjustment()->get_value()};
      return false;
  }, false));

  scroller = new Gtk::ScrolledWindow();
  scroller->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroller->add(*Gtk::manage(icon_view));
  scroller->set_hexpand();
  scroller->set_vexpand();
  scroller->set_overlay_scrolling(false);
  // here we fix scoller to allow pass the scroll to parent scroll when reach upper or lower limit
  // this must be added to al scrolleing window in dialogs. We dont do auto because dialogs can be recreated
  // in the dialog code so think is safer call inside
  fix_inner_scroll(scroller);
  overlay = new Gtk::Overlay();
  overlay->set_hexpand();
  overlay->set_vexpand();
  overlay->add(* scroller);
  overlay->get_style_context()->add_class("symbolsoverlay");
  scroller->set_size_request(100, -1);
  table->attach(*Gtk::manage(overlay), 0, row, 2, 1);

  /*************************Overlays******************************/
  overlay_opacity = new Gtk::Image();
  overlay_opacity->set_halign(Gtk::ALIGN_START);
  overlay_opacity->set_valign(Gtk::ALIGN_START);
  overlay_opacity->get_style_context()->add_class("rawstyle");
  overlay_opacity->set_no_show_all(true);
  // No results
  overlay_icon = sp_get_icon_image("searching", Gtk::ICON_SIZE_DIALOG);
  overlay_icon->set_pixel_size(110);
  overlay_icon->set_halign(Gtk::ALIGN_CENTER);
  overlay_icon->set_valign(Gtk::ALIGN_START);

  overlay_icon->set_margin_top(25);
  overlay_icon->set_no_show_all(true);

  overlay_title = new Gtk::Label();
  overlay_title->set_halign(Gtk::ALIGN_CENTER );
  overlay_title->set_valign(Gtk::ALIGN_START );
  overlay_title->set_justify(Gtk::JUSTIFY_CENTER);
  overlay_title->set_margin_top(135);
  overlay_title->set_no_show_all(true);

  overlay_desc = new Gtk::Label();
  overlay_desc->set_halign(Gtk::ALIGN_CENTER);
  overlay_desc->set_valign(Gtk::ALIGN_START);
  overlay_desc->set_margin_top(160);
  overlay_desc->set_justify(Gtk::JUSTIFY_CENTER);
  overlay_desc->set_no_show_all(true);

  overlay->add_overlay(*overlay_opacity);
  overlay->add_overlay(*overlay_icon);
  overlay->add_overlay(*overlay_title);
  overlay->add_overlay(*overlay_desc);

  previous_height = 0;
  previous_width = 0;
  ++row;


  ++row;

  /******************** Tools *******************************/
  tools = new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL);

  //tools->set_layout( Gtk::BUTTONBOX_END );
  scroller->set_hexpand();
  table->attach(*Gtk::manage(tools),0,row,2,1);

  auto add_symbol_image = Gtk::manage(sp_get_icon_image("symbol-add", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  add_symbol = Gtk::manage(new Gtk::Button());
  add_symbol->add(*add_symbol_image);
  add_symbol->set_tooltip_text(_("Add Symbol from the current document."));
  add_symbol->set_relief( Gtk::RELIEF_NONE );
  add_symbol->set_focus_on_click( false );
  add_symbol->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::insertSymbol));
  tools->pack_start(* add_symbol, Gtk::PACK_SHRINK);

  auto remove_symbolImage = Gtk::manage(sp_get_icon_image("symbol-remove", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  remove_symbol = Gtk::manage(new Gtk::Button());
  remove_symbol->add(*remove_symbolImage);
  remove_symbol->set_tooltip_text(_("Remove Symbol from the current document."));
  remove_symbol->set_relief( Gtk::RELIEF_NONE );
  remove_symbol->set_focus_on_click( false );
  remove_symbol->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::revertSymbol));
  tools->pack_start(* remove_symbol, Gtk::PACK_SHRINK);

  Gtk::Label* spacer = Gtk::manage(new Gtk::Label(""));
  tools->pack_start(* Gtk::manage(spacer));

  // Pack size (controls display area)
  pack_size = 2; // Default 32px

  auto packMoreImage = Gtk::manage(sp_get_icon_image("pack-more", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  more = Gtk::manage(new Gtk::Button());
  more->add(*packMoreImage);
  more->set_tooltip_text(_("Display more icons in row."));
  more->set_relief( Gtk::RELIEF_NONE );
  more->set_focus_on_click( false );
  more->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::packmore));
  tools->pack_start(* more, Gtk::PACK_SHRINK);

  auto packLessImage = Gtk::manage(sp_get_icon_image("pack-less", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  fewer = Gtk::manage(new Gtk::Button());
  fewer->add(*packLessImage);
  fewer->set_tooltip_text(_("Display fewer icons in row."));
  fewer->set_relief( Gtk::RELIEF_NONE );
  fewer->set_focus_on_click( false );
  fewer->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::packless));
  tools->pack_start(* fewer, Gtk::PACK_SHRINK);

  // Toggle scale to fit on/off
  auto fit_symbolImage = Gtk::manage(sp_get_icon_image("symbol-fit", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  fit_symbol = Gtk::manage(new Gtk::ToggleButton());
  fit_symbol->add(*fit_symbolImage);
  fit_symbol->set_tooltip_text(_("Toggle 'fit' symbols in icon space."));
  fit_symbol->set_relief( Gtk::RELIEF_NONE );
  fit_symbol->set_focus_on_click( false );
  fit_symbol->set_active( true );
  fit_symbol->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::rebuild));
  tools->pack_start(* fit_symbol, Gtk::PACK_SHRINK);

  // Render size (scales symbols within display area)
  scale_factor = 0; // Default 1:1 * pack_size/pack_size default
  auto zoom_outImage = Gtk::manage(sp_get_icon_image("symbol-smaller", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  zoom_out = Gtk::manage(new Gtk::Button());
  zoom_out->add(*zoom_outImage);
  zoom_out->set_tooltip_text(_("Make symbols smaller by zooming out."));
  zoom_out->set_relief( Gtk::RELIEF_NONE );
  zoom_out->set_focus_on_click( false );
  zoom_out->set_sensitive( false );
  zoom_out->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::zoomout));
  tools->pack_start(* zoom_out, Gtk::PACK_SHRINK);

  auto zoom_inImage = Gtk::manage(sp_get_icon_image("symbol-bigger", Gtk::ICON_SIZE_SMALL_TOOLBAR));

  zoom_in = Gtk::manage(new Gtk::Button());
  zoom_in->add(*zoom_inImage);
  zoom_in->set_tooltip_text(_("Make symbols bigger by zooming in."));
  zoom_in->set_relief( Gtk::RELIEF_NONE );
  zoom_in->set_focus_on_click( false );
  zoom_in->set_sensitive( false );
  zoom_in->signal_clicked().connect(sigc::mem_fun(*this, &SymbolsDialog::zoomin));
  tools->pack_start(* zoom_in, Gtk::PACK_SHRINK);

  ++row;

  sensitive = true;

  preview_document = symbolsPreviewDoc(); /* Template to render symbols in */
  key = SPItem::display_key_new(1);
  renderDrawing.setRoot(preview_document->getRoot()->invoke_show(renderDrawing, key, SP_ITEM_SHOW_DISPLAY ));

  getSymbolsTitle();
  icons_found = false;
}

SymbolsDialog::~SymbolsDialog()
{
  for (auto &connection : gtk_connections) {
      connection.disconnect();
  }
  gtk_connections.clear();
  idleconn.disconnect();

  Inkscape::GC::release(preview_document);
  assert(preview_document->_anchored_refcount() == 0);
  delete preview_document;
}

bool findString(const Glib::ustring & haystack, const Glib::ustring & needle)
{
  return strstr(haystack.c_str(), needle.c_str()) != nullptr;
}

void SymbolsDialog::packless() {
  if(pack_size < 4) {
      pack_size++;
      rebuild();
  }
}

void SymbolsDialog::packmore() {
  if(pack_size > 0) {
      pack_size--;
      rebuild();
  }
}

void SymbolsDialog::zoomin() {
  if(scale_factor < 4) {
      scale_factor++;
      rebuild();
  }
}

void SymbolsDialog::zoomout() {
  if(scale_factor > -8) {
      scale_factor--;
      rebuild();
  }
}

void SymbolsDialog::rebuild() {

  if (!sensitive) {
    return;
  }

  if( fit_symbol->get_active() ) {
    zoom_in->set_sensitive( false );
    zoom_out->set_sensitive( false );
  } else {
    zoom_in->set_sensitive( true);
    zoom_out->set_sensitive( true );
  }
  store->clear();
  SPDocument* symbol_document = selectedSymbols();
  icons_found = false;
  //We are not in search all docs
  if (search->get_text() != _("Searching...") && search->get_text() != _("Loading all symbols...")) {
      Glib::ustring current = Glib::Markup::escape_text(get_active_base_text());
      if (current == ALLDOCS && search->get_text() != "") {
          searchsymbols();
          return;
      }
  }
  if (symbol_document) {
    addSymbolsInDoc(symbol_document);
  } else {
    showOverlay();
  }
}
void SymbolsDialog::showOverlay() {
  Glib::ustring current = Glib::Markup::escape_text(get_active_base_text());
  if (current == ALLDOCS && !l.size())
  {
    overlay_icon->hide();
    if (!all_docs_processed ) {
        overlay_icon->show();
        overlay_title->set_markup(Glib::ustring("<span size=\"large\">") +
                                  Glib::ustring(_("Search in all symbol sets...")) + Glib::ustring("</span>"));
        overlay_desc->set_markup(Glib::ustring("<span size=\"small\">") +
                                 Glib::ustring(_("The first search can be slow.")) + Glib::ustring("</span>"));
    } else if (!icons_found && !search_str.empty()) {
        overlay_title->set_markup(Glib::ustring("<span size=\"large\">") + Glib::ustring(_("No symbols found.")) +
                                  Glib::ustring("</span>"));
        overlay_desc->set_markup(Glib::ustring("<span size=\"small\">") +
                                 Glib::ustring(_("Try a different search term.")) + Glib::ustring("</span>"));
    } else {
        overlay_icon->show();
        overlay_title->set_markup(Glib::ustring("<spansize=\"large\">") +
                                  Glib::ustring(_("Search in all symbol sets...")) + Glib::ustring("</span>"));
        overlay_desc->set_markup(Glib::ustring("<span size=\"small\">") + Glib::ustring("</span>"));
    }
  } else if (!number_symbols && (current != CURRENTDOC || !search_str.empty())) {
      overlay_title->set_markup(Glib::ustring("<span size=\"large\">") + Glib::ustring(_("No symbols found.")) +
                                Glib::ustring("</span>"));
      overlay_desc->set_markup(Glib::ustring("<span size=\"small\">") +
                               Glib::ustring(_("Try a different search term,\nor switch to a different symbol set.")) +
                               Glib::ustring("</span>"));
  } else if (!number_symbols && current == CURRENTDOC) {
      overlay_title->set_markup(Glib::ustring("<span size=\"large\">") + Glib::ustring(_("No symbols found.")) +
                                Glib::ustring("</span>"));
      overlay_desc->set_markup(
          Glib::ustring("<span size=\"small\">") +
          Glib::ustring(_("No symbols in current document.\nChoose a different symbol set\nor add a new symbol.")) +
          Glib::ustring("</span>"));
  } else if (!icons_found && !search_str.empty()) {
      overlay_title->set_markup(Glib::ustring("<span size=\"large\">") + Glib::ustring(_("No symbols found.")) +
                                Glib::ustring("</span>"));
      overlay_desc->set_markup(Glib::ustring("<span size=\"small\">") +
                               Glib::ustring(_("Try a different search term,\nor switch to a different symbol set.")) +
                               Glib::ustring("</span>"));
  }
  gint width = scroller->get_allocated_width();
  gint height = scroller->get_allocated_height();
  if (previous_height != height || previous_width != width) {
      previous_height = height;
      previous_width = width;
      overlay_opacity->set_size_request(width, height);
      overlay_opacity->set(getOverlay(width, height));
  }
  overlay_opacity->hide();
  overlay_icon->show();
  overlay_title->show();
  overlay_desc->show();
  if (l.size()) {
    overlay_opacity->show();
    overlay_icon->hide();
    overlay_title->hide();
    overlay_desc->hide();
  }
}

void SymbolsDialog::hideOverlay() {
    overlay_opacity->hide();
    overlay_icon->hide();
    overlay_title->hide();
    overlay_desc->hide();
}

void SymbolsDialog::insertSymbol() {
    getDesktop()->getSelection()->toSymbol();
}

void SymbolsDialog::revertSymbol() {
    if (auto document = getDocument()) {
        if (auto symbol = dynamic_cast<SPSymbol *>(document->getObjectById(getSymbolId(getSelected())))) {
            symbol->unSymbol();
        }
        Inkscape::DocumentUndo::done(document, _("Group from symbol"), "");
    }
}

void SymbolsDialog::iconDragDataGet(const Glib::RefPtr<Gdk::DragContext>& /*context*/, Gtk::SelectionData& data, guint /*info*/, guint /*time*/)
{
    auto selected = getSelected();
    if (!selected) {
        return;
    }
    auto row = store->get_iter(*selected);
    Glib::ustring symbol_id = (*row)[_columns.symbol_id];
    GdkAtom dataAtom = gdk_atom_intern("application/x-inkscape-paste", false);
    gtk_selection_data_set(data.gobj(), dataAtom, 9, (guchar*)symbol_id.c_str(), symbol_id.length());
}

void SymbolsDialog::defsModified(SPObject * /*object*/, guint /*flags*/)
{
  Glib::ustring doc_title = get_active_base_text();
  if (doc_title != ALLDOCS && !symbol_sets[doc_title].second ) {
    rebuild();
  }
}

void SymbolsDialog::selectionChanged(Inkscape::Selection *selection) {
  auto selected = getSelected();
  Glib::ustring symbol_id = getSymbolId(selected);
  Glib::ustring doc_title = get_active_base_text(getSymbolDocTitle(selected));
  if (!doc_title.empty()) {
    SPDocument* symbol_document = symbol_sets[doc_title].second;
    if (!symbol_document) {
      //we are in global search so get the original symbol document by title
      symbol_document = selectedSymbols();
    }
    if (symbol_document) {
      SPObject* symbol = symbol_document->getObjectById(symbol_id);
      if(symbol && !selection->includes(symbol)) {
          icon_view->unselect_all();
      }
    }
  }
}

void SymbolsDialog::documentReplaced()
{
    defs_modified.disconnect();
    if (auto document = getDocument()) {
        defs_modified = document->getDefs()->connectModified(sigc::mem_fun(*this, &SymbolsDialog::defsModified));
        if (!symbol_sets[get_active_base_text()].second) {
            // Symbol set is from Current document, need to rebuild
            rebuild();
        }
    }
}

SPDocument* SymbolsDialog::selectedSymbols() {
  /* OK, we know symbol name... now we need to copy it to clipboard, bon chance! */
  Glib::ustring doc_title = get_active_base_text();
  if (doc_title == ALLDOCS) {
    return nullptr;
  }
  SPDocument* symbol_document = symbol_sets[doc_title].second;
  if( !symbol_document ) {
    symbol_document = getSymbolsSet(doc_title).second;
    // Symbol must be from Current Document (this method of checking should be language independent).
    if( !symbol_document ) {
      // Symbol must be from Current Document (this method of
      // checking should be language independent).
      symbol_document = getDocument();
      add_symbol->set_sensitive( true );
      remove_symbol->set_sensitive( true );
    } else {
      add_symbol->set_sensitive( false );
      remove_symbol->set_sensitive( false );
    }
  }
  return symbol_document;
}

/** Return the path to the selected symbol, or an empty optional if nothing is selected. */
std::optional<Gtk::TreeModel::Path> SymbolsDialog::getSelected() const
{
    auto selected = icon_view->get_selected_items();
    if (selected.empty()) {
        return std::nullopt;
    }
    return selected.front();
}

/** Return the dimensions of the symbol at the given path, in document units. */
Geom::Point SymbolsDialog::getSymbolDimensions(std::optional<Gtk::TreeModel::Path> const &path) const
{
    if (!path) {
        return Geom::Point();
    }
    auto row = store->get_iter(*path);
    return (*row)[_columns.doc_dimensions];
}

/** Return the ID of the symbol at the given path, with empty string fallback. */
Glib::ustring SymbolsDialog::getSymbolId(std::optional<Gtk::TreeModel::Path> const &path) const
{
    if (!path) {
        return "";
    }
    auto row = store->get_iter(*path);
    return (*row)[_columns.symbol_id];
}

/** Return the title of the document from which the symbol at the given path comes,
 *  with empty string fallback. */
Glib::ustring SymbolsDialog::getSymbolDocTitle(std::optional<Gtk::TreeModel::Path> const &path) const
{
    if (!path) {
        return "";
    }
    auto row = store->get_iter(*path);
    return (*row)[_columns.symbol_doc_title];
}

Glib::ustring SymbolsDialog::documentTitle(SPDocument* symbol_doc) {
  if (symbol_doc) {
    SPRoot * root = symbol_doc->getRoot();
    gchar * title = root->title();
    if (title) {
      return ellipsize(Glib::ustring(title), 33);
    }
    g_free(title);
  }
  Glib::ustring current = get_active_base_text();
  if (current == CURRENTDOC) {
    return current;
  }
  return _("Untitled document");
}

/** Store the symbol in the clipboard for further manipulation/insertion into document.
 *
 * @param symbol_path The path to the symbol in the tree model.
 * @param bbox The bounding box to set on the clipboard document's clipnode.
 */
void SymbolsDialog::sendToClipboard(Gtk::TreeModel::Path const &symbol_path, Geom::Rect const &bbox)
{
    Glib::ustring symbol_id = getSymbolId(symbol_path);
    SPDocument* symbol_document = selectedSymbols();
    if (!symbol_document) {
        //we are in global search so get the original symbol document by title
        Glib::ustring doc_title = get_active_base_text(getSymbolDocTitle(symbol_path));
        if (!doc_title.empty()) {
            symbol_document = symbol_sets[doc_title].second;
        }
    }
    if (!symbol_document) {
        return;
    }
    if (SPObject* symbol = symbol_document->getObjectById(symbol_id)) {
        // Find style for use in <use>
        // First look for default style stored in <symbol>
        gchar const* style = symbol->getAttribute("inkscape:symbol-style");
        if (!style) {
            // If no default style in <symbol>, look in documents.
            if (symbol_document == getDocument()) {
                style = styleFromUse(symbol_id.c_str(), symbol_document);
            } else {
                style = symbol_document->getReprRoot()->attribute("style");
            }
        }
        auto const dims = getSymbolDimensions(symbol_path);
        ClipboardManager *cm = ClipboardManager::get();
        cm->copySymbol(symbol->getRepr(), style, symbol_document, bbox);
    }
}

void SymbolsDialog::iconChanged()
{
    if (auto selected = getSelected()) {
        auto const dims = getSymbolDimensions(selected);
        sendToClipboard(*selected, Geom::Rect(-0.5 * dims, 0.5 * dims));
    }
}

/** Handle the start of a drag on a symbol preview icon. */
void SymbolsDialog::onDragStart()
{
    auto selected = getSelected();
    if (!selected) {
        return;
    }

    // Get the rectangle of the cell where the drag started.
    Gdk::Rectangle temprect;
    icon_view->get_cell_rect(*selected, temprect);
    auto cell_rect = Geom::IntRect::from_xywh({temprect.get_x(), temprect.get_y()},
                                              {temprect.get_width(), temprect.get_height()});

    // Find the rectangle of the actual symbol preview
    // (not the same as the cell rectangle, due to fitting and padding).
    auto const dims = getSymbolDimensions(selected);
    unsigned preview_size = SYMBOL_ICON_SIZES[pack_size];
    Geom::Dim2 larger_dim = dims[Geom::X] > dims[Geom::Y] ? Geom::X : Geom::Y;
    Geom::Dim2 smaller_dim = (Geom::Dim2)(!larger_dim);
    Geom::Rect preview_rect; ///< The actual rectangle taken up by the bbox of the rendered preview.

    Geom::Interval larger_interval = cell_rect[larger_dim];
    larger_interval.expandBy(0.5 * (preview_size - larger_interval.extent())); // Trim off the padding.
    preview_rect[larger_dim] = larger_interval;

    double const proportionally_scaled_smaller = preview_size * dims[smaller_dim] / dims[larger_dim];
    double const smaller_trim = 0.5 * (cell_rect[smaller_dim].extent() - proportionally_scaled_smaller);
    Geom::Interval smaller_interval = cell_rect[smaller_dim];
    smaller_interval.expandBy(-smaller_trim); // Trim off padding and the "letterboxes" for non-square bbox.
    preview_rect[smaller_dim] = smaller_interval;

    // Map the last mousedown position to [0, 1] x [0, 1] coordinates in the preview rectangle.
    Geom::Point normalized_position = _last_mousedown - preview_rect.min();
    normalized_position.x() = std::clamp(normalized_position.x() / preview_rect.width(), 0.0, 1.0);
    normalized_position.y() /= preview_rect.height();
    if (auto desktop = getDesktop(); desktop && !desktop->is_yaxisdown()) {
        normalized_position.y() = 1.0 - normalized_position.y();
    }
    normalized_position.y() = std::clamp(normalized_position.y(), 0.0, 1.0);

    // Push the symbol into the private clipboard with the correct bounding box. This box has dimensions
    // `dims` but is positioned in such a way that the origin point (0, 0) lies at `normalized_position`.
    auto const box_position = -Geom::Point(normalized_position.x() * dims.x(), normalized_position.y() * dims.y());
    sendToClipboard(*selected, Geom::Rect::from_xywh(box_position, dims));
}

#ifdef WITH_LIBVISIO

// Extend libvisio's native RVNGSVGDrawingGenerator with support for extracting stencil names (to be used as ID/title)
class REVENGE_API RVNGSVGDrawingGenerator_WithTitle : public RVNGSVGDrawingGenerator {
  public:
    RVNGSVGDrawingGenerator_WithTitle(RVNGStringVector &output, RVNGStringVector &titles, const RVNGString &nmSpace)
      : RVNGSVGDrawingGenerator(output, nmSpace)
      , _titles(titles)
    {}

    void startPage(const RVNGPropertyList &propList) override
    {
      RVNGSVGDrawingGenerator::startPage(propList);
      if (propList["draw:name"]) {
          _titles.append(propList["draw:name"]->getStr());
      } else {
          _titles.append("");
      }
    }

  private:
    RVNGStringVector &_titles;
};

// Read Visio stencil files
SPDocument* read_vss(std::string filename, std::string name, std::string search_str ) {
  gchar *fullname;
  #ifdef _WIN32
    // RVNGFileStream uses fopen() internally which unfortunately only uses ANSI encoding on Windows
    // therefore attempt to convert uri to the system codepage
    // even if this is not possible the alternate short (8.3) file name will be used if available
    fullname = g_win32_locale_filename_from_utf8(filename.c_str());
  #else
    fullname = strdup(filename.c_str());
  #endif

  RVNGFileStream input(fullname);
  g_free(fullname);

  if (!libvisio::VisioDocument::isSupported(&input)) {
    return nullptr;
  }
  RVNGStringVector output;
  RVNGStringVector titles;
  RVNGSVGDrawingGenerator_WithTitle generator(output, titles, "svg");

  if (!libvisio::VisioDocument::parseStencils(&input, &generator)) {
    return nullptr;
  }
  if (output.empty()) {
    return nullptr;
  }

  // prepare a valid title for the symbol file
  Glib::ustring title = Glib::Markup::escape_text(name);
  // prepare a valid id prefix for symbols libvisio doesn't give us a name for
  Glib::RefPtr<Glib::Regex> regex1 = Glib::Regex::create("[^a-zA-Z0-9_-]");
  Glib::ustring id = regex1->replace(name, 0, "_", Glib::REGEX_MATCH_PARTIAL);

  Glib::ustring tmpSVGOutput;
  tmpSVGOutput += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
  tmpSVGOutput += "<svg\n";
  tmpSVGOutput += "  xmlns=\"http://www.w3.org/2000/svg\"\n";
  tmpSVGOutput += "  xmlns:svg=\"http://www.w3.org/2000/svg\"\n";
  tmpSVGOutput += "  xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n";
  tmpSVGOutput += "  version=\"1.1\"\n";
  tmpSVGOutput += "  style=\"fill:none;stroke:#000000;stroke-width:2\">\n";
  tmpSVGOutput += "  <title>";
  tmpSVGOutput += title;
  tmpSVGOutput += "</title>\n";
  tmpSVGOutput += "  <defs>\n";

  // Each "symbol" is in its own SVG file, we wrap with <symbol> and merge into one file.
  bool load = false;
  for (unsigned i=0; i<output.size(); ++i) {

    std::stringstream ss;
    if (titles.size() == output.size() && titles[i] != "") {
      // TODO: Do we need to check for duplicated titles?
      ss << regex1->replace(titles[i].cstr(), 0, "_", Glib::REGEX_MATCH_PARTIAL);
    } else {
      ss << id << "_" << i;
    }

    tmpSVGOutput += "    <symbol id=\"" + ss.str() + "\">\n";

    if (titles.size() == output.size() && titles[i] != "") {
      tmpSVGOutput += "      <title>" + Glib::ustring(RVNGString::escapeXML(titles[i].cstr()).cstr()) + "</title>\n";
      try {
        Glib::ustring haystack = RVNGString::escapeXML(titles[i].cstr()).cstr();
        Glib::ustring needle = search_str;
        if (findString(haystack.lowercase(), needle)) {
            load = true;
        }
      } catch (...) {
          g_warning("A error happends reading the symbols file, probably encoding");
          load = true; //we try to load symbols in this error case
      }
    }

    std::istringstream iss( output[i].cstr() );
    std::string line;
    while( std::getline( iss, line ) ) {
      if( line.find( "svg:svg" ) == std::string::npos ) {
        tmpSVGOutput += "      " + line + "\n";
      }
    }

    tmpSVGOutput += "    </symbol>\n";
  }

  tmpSVGOutput += "  </defs>\n";
  tmpSVGOutput += "</svg>\n";
  if (load) {
    return SPDocument::createNewDocFromMem( tmpSVGOutput.c_str(), strlen( tmpSVGOutput.c_str()), false );
  } else {
    return nullptr;
  }
}
#endif

/* Hunts preference directories for symbol files */
void SymbolsDialog::getSymbolsTitle() {

    using namespace Inkscape::IO::Resource;
    Glib::ustring title;
    number_docs = 0;
    std::regex matchtitle (".*?<title.*?>(.*?)<(/| /)");
    for(auto &filename: get_filenames(SYMBOLS, {".svg", ".vss", "vssx", "vsdx"})) {
        if(Glib::str_has_suffix(filename, ".vss") || Glib::str_has_suffix(filename, ".vssx") || Glib::str_has_suffix(filename, ".vsdx")) {
          std::size_t found = filename.find_last_of("/\\");
          title = filename.substr(found+1);
          title = title.erase(title.rfind('.'));
          if(title.empty()) {
            title = _("Unnamed Symbols");
          }
          if (!symbol_sets[filename].second)
            symbol_sets[filename]= std::make_pair(title,nullptr);
          ++number_docs;
        } else {
          std::ifstream infile(filename);
          std::string line;
          while (std::getline(infile, line)) {
              std::string title_res = std::regex_replace (line, matchtitle,"$1",std::regex_constants::format_no_copy);
              if (!title_res.empty()) {
                  title_res = g_dpgettext2(nullptr, "Symbol", title_res.c_str());
                  if (!symbol_sets[filename].second)
                    symbol_sets[filename]= std::make_pair(Glib::ustring(title_res),nullptr);
                  ++number_docs;
                  break;
              }
              std::string::size_type position_exit = line.find ("<defs");
              if (position_exit != std::string::npos) {
                  std::size_t found = filename.find_last_of("/\\");
                  title = filename.substr(found+1);
                  title = title.erase(title.rfind('.'));
                  if(title.empty()) {
                    title = _("Unnamed Symbols");
                  }
                  if (!symbol_sets[filename].second)
                    symbol_sets[filename]= std::make_pair(title,nullptr);
                  ++number_docs;
                  break;
              }
          }
        }
    }

    for(auto const &symbol_document_map : symbol_sets) {
      symbol_set->append(symbol_document_map.second.first);
    }
}

/* Hunts preference directories for symbol files */
std::pair<std::string, SPDocument*>
SymbolsDialog::getSymbolsSet(std::string filename)
{
    SPDocument* symbol_doc = nullptr;
    if (symbol_sets[filename].second) {
      return symbol_sets[filename];
    }
    using namespace Inkscape::IO::Resource;
    if(Glib::str_has_suffix(filename, ".vss") || Glib::str_has_suffix(filename, ".vssx") || Glib::str_has_suffix(filename, ".vsdx")) {
#ifdef WITH_LIBVISIO
      symbol_doc = read_vss(filename, symbol_sets[filename].first, std::string(search_str));
#endif
    } else if(Glib::str_has_suffix(filename, ".svg")) {
        std::ifstream infile(filename);
        std::string line;
        bool load = false;
        while (std::getline(infile, line)) {
          try {
            Glib::ustring haystack = Glib::ustring(line);
            Glib::ustring needle = search_str;
            if (findString(haystack.lowercase(), needle)) {
                load = true;
                break;
            }
          } catch (...) {
              g_warning("A error happends reading the symbols file, probably encoding");
              load = true; //we try to load symbols in this error case
              break;
          }
        }
        if (load) {
          symbol_doc = SPDocument::createNewDoc(filename.c_str(), FALSE);
        }
    }
    if(symbol_doc) {
      symbol_sets[filename] = std::make_pair(symbol_sets[filename].first,symbol_doc);
    }
    return symbol_sets[filename];
}

void SymbolsDialog::symbolsInDocRecursive (SPObject *r, std::map<Glib::ustring, std::pair<Glib::ustring, SPSymbol*> > &l, Glib::ustring doc_title)
{
  if(!r) return;

  // Stop multiple counting of same symbol
  if ( dynamic_cast<SPUse *>(r) ) {
    return;
  }

  if ( dynamic_cast<SPSymbol *>(r)) {
    Glib::ustring id = r->getAttribute("id");
    gchar * title = r->title();
    if(title) {
      l[doc_title + title + id] = std::make_pair(doc_title,dynamic_cast<SPSymbol *>(r));
    } else {
      l[Glib::ustring(_("notitle_")) + id] = std::make_pair(doc_title,dynamic_cast<SPSymbol *>(r));
    }
    g_free(title);
  }
  for (auto& child: r->children) {
    symbolsInDocRecursive(&child, l, doc_title);
  }
}

std::map<Glib::ustring, std::pair<Glib::ustring, SPSymbol*> >
SymbolsDialog::symbolsInDoc( SPDocument* symbol_document, Glib::ustring doc_title)
{

  std::map<Glib::ustring, std::pair<Glib::ustring, SPSymbol*> > l;
  if (symbol_document) {
    symbolsInDocRecursive (symbol_document->getRoot(), l , doc_title);
  }
  return l;
}

void SymbolsDialog::useInDoc (SPObject *r, std::vector<SPUse*> &l)
{

  if ( dynamic_cast<SPUse *>(r) ) {
    l.push_back(dynamic_cast<SPUse *>(r));
  }

  for (auto& child: r->children) {
    useInDoc( &child, l );
  }
}

std::vector<SPUse*> SymbolsDialog::useInDoc( SPDocument* useDocument) {
  std::vector<SPUse*> l;
  useInDoc (useDocument->getRoot(), l);
  return l;
}

// Returns style from first <use> element found that references id.
// This is a last ditch effort to find a style.
gchar const* SymbolsDialog::styleFromUse( gchar const* id, SPDocument* document) {

  gchar const* style = nullptr;
  std::vector<SPUse*> l = useInDoc( document );
  for( auto use:l ) {
    if ( use ) {
      gchar const *href = Inkscape::getHrefAttribute(*use->getRepr()).second;
      if( href ) {
        Glib::ustring href2(href);
        Glib::ustring id2(id);
        id2 = "#" + id2;
        if( !href2.compare(id2) ) {
          style = use->getRepr()->attribute("style");
          break;
        }
      }
    }
  }
  return style;
}

void SymbolsDialog::clearSearch()
{
  if(search->get_text().empty() && sensitive) {
    enableWidgets(false);
    search_str = "";
    store->clear();
    SPDocument* symbol_document = selectedSymbols();
    if (symbol_document) {
      //We are not in search all docs
      icons_found = false;
      addSymbolsInDoc(symbol_document);
    } else {
      showOverlay();
      enableWidgets(true);
    }
  }
}

void SymbolsDialog::enableWidgets(bool enable)
{
  symbol_set->set_sensitive(enable);
  search->set_sensitive(enable);
  tools ->set_sensitive(enable);
}

void SymbolsDialog::beforeSearch(GdkEventKey* evt)
{
  sensitive = false;
  search_str = search->get_text().lowercase();
  if (evt->keyval != GDK_KEY_Return) {
    return;
  }
  searchsymbols();
}

void SymbolsDialog::searchsymbols()
{
    enableWidgets(false);
    SPDocument *symbol_document = selectedSymbols();
    if (symbol_document) {
        // We are not in search all docs
        search->set_text(_("Searching..."));
        store->clear();
        icons_found = false;
        addSymbolsInDoc(symbol_document);
    } else {
        idleconn.disconnect();
        idleconn = Glib::signal_idle().connect(sigc::mem_fun(*this, &SymbolsDialog::callbackAllSymbols));
        search->set_text(_("Loading all symbols..."));
    }
}

void SymbolsDialog::unsensitive(GdkEventKey* evt)
{
  sensitive = true;
}

bool SymbolsDialog::callbackSymbols(){
  icon_view->hide();
  if (l.size()) {
    showOverlay();
    for (auto symbol_data = l.begin(); symbol_data != l.end();) {
      Glib::ustring doc_title = symbol_data->second.first;
      SPSymbol * symbol = symbol_data->second.second;
      counter_symbols ++;
      gchar *symbol_title_char = symbol->title();
      gchar *symbol_desc_char = symbol->description();
      bool found = false;
      if (symbol_title_char) {
        Glib::ustring symbol_title = Glib::ustring(symbol_title_char).lowercase();
        auto pos = symbol_title.rfind(search_str);
        auto pos_translated = Glib::ustring(g_dpgettext2(nullptr, "Symbol", symbol_title_char)).lowercase().rfind(search_str);
        if ((pos != std::string::npos) || (pos_translated != std::string::npos)) {
          found = true;
        }
        if (!found && symbol_desc_char) {
          Glib::ustring symbol_desc = Glib::ustring(symbol_desc_char).lowercase();
          auto pos = symbol_desc.rfind(search_str);
          auto pos_translated = Glib::ustring(g_dpgettext2(nullptr, "Symbol", symbol_desc_char)).lowercase().rfind(search_str);
          if ((pos != std::string::npos) || (pos_translated != std::string::npos)) {
            found = true;
          }
        }
      }
      if (symbol && (search_str.empty() || found)) {
        addSymbol( symbol, doc_title);
        icons_found = true;
      }
      symbol_data = l.erase(l.begin());
      //to get more items and best performance
      g_free(symbol_title_char);
      g_free(symbol_desc_char);
    }
    if (!icons_found && !search_str.empty()) {
      showOverlay();
    } else {
      hideOverlay();
    }
    sensitive = false;
    search->set_text(search_str);
    sensitive = true;
    enableWidgets(true);
    icon_view->show();
    return false;
  }
  icon_view->show();
  return true;
}

Glib::ustring SymbolsDialog::get_active_base_text(Glib::ustring title)
{
    Glib::ustring out = title == "selectedcombo" ? symbol_set->get_active_text() : title;
    for(auto const &symbol_document_map : symbol_sets) {
      if (symbol_document_map.second.first == out) {
          out = symbol_document_map.first;
      }
    }
    return out;
}


bool SymbolsDialog::callbackAllSymbols(){
  icon_view->hide();
  Glib::ustring current = get_active_base_text();
  if (current == ALLDOCS && search->get_text() == _("Loading all symbols...")) {
    size_t counter = 0;
    std::map<std::string, std::pair<Glib::ustring, SPDocument*> > symbol_sets_tmp = symbol_sets;
    for(auto const &symbol_document_map : symbol_sets_tmp) {
      ++counter;
      Glib::ustring current = get_active_base_text();
      if (current == CURRENTDOC) {
        return true;
      }
      SPDocument* symbol_document = symbol_document_map.second.second;
      if (symbol_document) {
        continue;
      }
      symbol_document = getSymbolsSet(symbol_document_map.first).second;
      symbol_set->set_active_text(ALLDOCS);
    }
    symbol_sets_tmp.clear();
    hideOverlay();
    all_docs_processed = true;
    addSymbols();
    search->set_text(search_str);
    icon_view->show();
    return false;
  }
  icon_view->show();
  return true;
}

Glib::ustring SymbolsDialog::ellipsize(Glib::ustring data, size_t limit) {
    if (data.length() > limit) {
      data = data.substr(0, limit-3);
      return data + "...";
    }
    return data;
}

void SymbolsDialog::addSymbolsInDoc(SPDocument* symbol_document) {

  if (!symbol_document) {
    return; //Search all
  }
  Glib::ustring doc_title = documentTitle(symbol_document);
  counter_symbols = 0;
  l = symbolsInDoc(symbol_document, doc_title);
  number_symbols = l.size();
  if (!number_symbols) {
    sensitive = false;
    search->set_text(search_str);
    sensitive = true;
    enableWidgets(true);
    idleconn.disconnect();
    showOverlay();
  } else {
    idleconn.disconnect();
    idleconn = Glib::signal_idle().connect( sigc::mem_fun(*this, &SymbolsDialog::callbackSymbols));
  }
}

void SymbolsDialog::addSymbols() {
  store->clear();
  icons_found = false;
  for(auto const &symbol_document_map : symbol_sets) {
    SPDocument* symbol_document = symbol_document_map.second.second;
    if (!symbol_document) {
      continue;
    }
    Glib::ustring doc_title = documentTitle(symbol_document);
    std::map<Glib::ustring, std::pair<Glib::ustring, SPSymbol*> > l_tmp = symbolsInDoc(symbol_document, doc_title);
    for(auto &p : l_tmp ) {
      l[p.first] = p.second;
    }
    l_tmp.clear();
  }
  counter_symbols = 0;
  number_symbols = l.size();
  if (!number_symbols) {
    showOverlay();
    idleconn.disconnect();
    sensitive = false;
    search->set_text(search_str);
    sensitive = true;
    enableWidgets(true);
  } else {
    idleconn.disconnect();
    idleconn = Glib::signal_idle().connect( sigc::mem_fun(*this, &SymbolsDialog::callbackSymbols));
  }
}

void SymbolsDialog::addSymbol(SPSymbol *symbol, Glib::ustring doc_title)
{
  gchar const *id = symbol->getRepr()->attribute("id");

  if (doc_title.empty()) {
    doc_title = CURRENTDOC;
  } else {
    doc_title = g_dpgettext2(nullptr, "Symbol", doc_title.c_str());
  }

  Glib::ustring symbol_title;
  gchar *title = symbol->title(); // From title element
  if (title) {
    symbol_title = Glib::ustring::compose("%1 (%2)", g_dpgettext2(nullptr, "Symbol", title), doc_title.c_str());
  } else {
    symbol_title = Glib::ustring::compose("%1 %2 (%3)", _("Symbol without title"), Glib::ustring(id), doc_title);
  }
  g_free(title);

  Geom::Point dimensions{64, 64}; // Default to 64x64 px if size not available.
  if (auto rect = symbol->documentVisualBounds()) {
      dimensions = rect->dimensions();
  }

  Glib::RefPtr<Gdk::Pixbuf> pixbuf = drawSymbol( symbol );
  if( pixbuf ) {
    Gtk::ListStore::iterator row = store->append();
    (*row)[_columns.symbol_id]        = Glib::ustring(id);
    (*row)[_columns.symbol_title]     = Glib::Markup::escape_text(symbol_title);
    (*row)[_columns.symbol_doc_title] = Glib::Markup::escape_text(doc_title);
    (*row)[_columns.symbol_image]     = pixbuf;
    (*row)[_columns.doc_dimensions]   = dimensions;
  }
}

/*
 * Returns image of symbol.
 *
 * Symbols normally are not visible. They must be referenced by a
 * <use> element.  A temporary document is created with a dummy
 * <symbol> element and a <use> element that references the symbol
 * element. Each real symbol is swapped in for the dummy symbol and
 * the temporary document is rendered.
 */
Glib::RefPtr<Gdk::Pixbuf>
SymbolsDialog::drawSymbol(SPObject *symbol)
{
  // Create a copy repr of the symbol with id="the_symbol"
  Inkscape::XML::Node *repr = symbol->getRepr()->duplicate(preview_document->getReprDoc());
  repr->setAttribute("id", "the_symbol");

  // First look for default style stored in <symbol>
  gchar const* style = repr->attribute("inkscape:symbol-style");
  if(!style) {
    // If no default style in <symbol>, look in documents.
    if(symbol->document == getDocument()) {
      gchar const *id = symbol->getRepr()->attribute("id");
      style = styleFromUse( id, symbol->document );
    } else {
      style = symbol->document->getReprRoot()->attribute("style");
    }
  }

  // This is for display in Symbols dialog only
  if( style ) repr->setAttribute( "style", style );

  SPDocument::install_reference_document scoped(preview_document, getDocument());
  preview_document->getDefs()->getRepr()->appendChild(repr);
  Inkscape::GC::release(repr);

  // Uncomment this to get the preview_document documents saved (useful for debugging)
  // FILE *fp = fopen (g_strconcat(id, ".svg", NULL), "w");
  // sp_repr_save_stream(preview_document->getReprDoc(), fp);
  // fclose (fp);

  // Make sure preview_document is up-to-date.
  preview_document->ensureUpToDate();

  // Make sure we have symbol in preview_document
  SPObject *object_temp = preview_document->getObjectById( "the_use" );

  SPItem *item = dynamic_cast<SPItem *>(object_temp);
  g_assert(item != nullptr);
  unsigned psize = SYMBOL_ICON_SIZES[pack_size];

  Glib::RefPtr<Gdk::Pixbuf> pixbuf(nullptr);
  // We could use cache here, but it doesn't really work with the structure
  // of this user interface and we've already cached the pixbuf in the gtklist

  // Find object's bbox in document.
  // Note symbols can have own viewport... ignore for now.
  //Geom::OptRect dbox = item->geometricBounds();
  Geom::OptRect dbox = item->documentVisualBounds();

  if (dbox) {
    /* Scale symbols to fit */
    double scale = 1.0;
    double width  = dbox->width();
    double height = dbox->height();

    if( width == 0.0 ) width = 1.0;
    if( height == 0.0 ) height = 1.0;

    if( fit_symbol->get_active() )
      scale = psize / ceil(std::max(width, height));
    else
      scale = pow( 2.0, scale_factor/2.0 ) * psize / 32.0;

    pixbuf = Glib::wrap(render_pixbuf(renderDrawing, scale, *dbox, psize));
  }

  preview_document->getObjectByRepr(repr)->deleteObject(false);

  return pixbuf;
}

/*
 * Return empty doc to render symbols in.
 * Symbols are by default not rendered so a <use> element is
 * provided.
 */
SPDocument* SymbolsDialog::symbolsPreviewDoc()
{
  // BUG: <symbol> must be inside <defs>
  gchar const *buffer =
"<svg xmlns=\"http://www.w3.org/2000/svg\""
"     xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\""
"     xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\""
"     xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
"  <use id=\"the_use\" xlink:href=\"#the_symbol\"/>"
"</svg>";
  return SPDocument::createNewDocFromMem( buffer, strlen(buffer), FALSE );
}

/*
 * Update image widgets
 */
Glib::RefPtr<Gdk::Pixbuf>
SymbolsDialog::getOverlay(gint width, gint height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.75);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_fill (cr);
  GdkPixbuf* pixbuf = ink_pixbuf_create_from_cairo_surface(surface);
  cairo_destroy (cr);
  return Glib::wrap(pixbuf);
}

} //namespace Dialogs
} //namespace UI
} //namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-basic-offset:2
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=2:tabstop=8:softtabstop=2:fileencoding=utf-8:textwidth=99 :
