// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * XML editor.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   David Turner
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2006 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */

#include "xml-tree.h"

#include <glibmm/i18n.h>
#include <gtkmm/button.h>
#include <gtkmm/enums.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <memory>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "layer-manager.h"
#include "message-context.h"
#include "message-stack.h"

#include "object/sp-root.h"
#include "object/sp-string.h"

#include "ui/builder-utils.h"
#include "ui/dialog-events.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/tools/tool-base.h"

#include "widgets/sp-xmlview-tree.h"

namespace {
/**
 * Set the orientation of `paned` to vertical or horizontal, and make the first child resizable
 * if vertical, and the second child resizable if horizontal.
 * @pre `paned` has two children
 */
void paned_set_vertical(Gtk::Paned &paned, bool vertical)
{
    auto& first = *paned.get_child1();
    auto& second = *paned.get_child2();
    const int space = 1;
    paned.child_property_resize(first) = vertical;
    first.set_margin_bottom(vertical ? space : 0);
    first.set_margin_end(vertical ? 0 : space);
    second.set_margin_top(vertical ? space : 0);
    second.set_margin_start(vertical ? 0 : space);
    assert(paned.child_property_resize(second));
    paned.set_orientation(vertical ? Gtk::ORIENTATION_VERTICAL : Gtk::ORIENTATION_HORIZONTAL);
}
} // namespace

namespace Inkscape {
namespace UI {
namespace Dialog {

XmlTree::XmlTree()
    : DialogBase("/dialogs/xml/", "XMLEditor"),
    _builder(create_builder("dialog-xml.glade")),
    _paned(get_widget<Gtk::Paned>(_builder, "pane")),
    xml_element_new_button(get_widget<Gtk::Button>(_builder, "new-elem")),
    xml_text_new_button(get_widget<Gtk::Button>(_builder, "new-text")),
    xml_node_delete_button(get_widget<Gtk::Button>(_builder, "del")),
    xml_node_duplicate_button(get_widget<Gtk::Button>(_builder, "dup")),
    unindent_node_button(get_widget<Gtk::Button>(_builder, "unindent")),
    indent_node_button(get_widget<Gtk::Button>(_builder, "indent")),
    lower_node_button(get_widget<Gtk::Button>(_builder, "lower")),
    raise_node_button(get_widget<Gtk::Button>(_builder, "raise"))
{
    /* tree view */
    tree = SP_XMLVIEW_TREE(sp_xmlview_tree_new(nullptr, nullptr, nullptr));
    gtk_widget_set_tooltip_text( GTK_WIDGET(tree), _("Drag to reorder nodes") );

    Gtk::ScrolledWindow& tree_scroller = get_widget<Gtk::ScrolledWindow>(_builder, "tree-wnd");
    tree_scroller.add(*Gtk::manage(Glib::wrap(GTK_WIDGET(tree))));
    fix_inner_scroll(&tree_scroller);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool dir = prefs->getBool("/dialogs/xml/vertical", true);

    /* attributes */
    attributes = new AttrDialog();
    attributes->set_margin_top(0);
    attributes->set_margin_bottom(0);
    attributes->set_margin_start(0);
    attributes->set_margin_end(0);
    attributes->_scrolledWindow.set_shadow_type(Gtk::SHADOW_IN);
    attributes->show();
    attributes->status_box.hide();
    attributes->status_box.set_no_show_all();
    _paned.pack2(*attributes, true, false);
    paned_set_vertical(_paned, dir);

    /* Signal handlers */
    GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(tree));
    _selection_changed = g_signal_connect (G_OBJECT(selection), "changed", G_CALLBACK (on_tree_select_row), this);
    _tree_move = g_signal_connect_after( G_OBJECT(tree), "tree_move", G_CALLBACK(after_tree_move), this);

    xml_element_new_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_new_element_node));
    xml_text_new_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_new_text_node));
    xml_node_duplicate_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_duplicate_node));
    xml_node_delete_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_delete_node));
    unindent_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_unindent_node));
    indent_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_indent_node));
    raise_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_raise_node));
    lower_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_lower_node));

    set_name("XMLAndAttributesDialog");
    set_spacing(0);
    show_all();

    int panedpos = prefs->getInt("/dialogs/xml/panedpos", 200);
    _paned.property_position() = panedpos;
    _paned.property_position().signal_changed().connect(sigc::mem_fun(*this, &XmlTree::_resized));

    pack_start(get_widget<Gtk::Box>(_builder, "main"), true, true);

    int min_width = 0, dummy;
    get_preferred_width(min_width, dummy);

    signal_size_allocate().connect([=] (Gtk::Allocation const &alloc) {
        // skip bogus sizes
        if (alloc.get_width() < 10 || alloc.get_height() < 10) return;

        // minimal width times fudge factor to arrive at "narrow" dialog with automatic vertical layout:
        const bool narrow = alloc.get_width() < min_width * 1.5;
        paned_set_vertical(_paned, narrow);
    });
}

void XmlTree::_resized()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt("/dialogs/xml/panedpos", _paned.property_position());
}

void XmlTree::_toggleDirection(Gtk::RadioButton *vertical)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool dir = vertical->get_active();
    prefs->setBool("/dialogs/xml/vertical", dir);
    paned_set_vertical(_paned, dir);
    prefs->setInt("/dialogs/xml/panedpos", _paned.property_position());
}

void XmlTree::on_unrealize() {
    // disconnect signals, they can fire after 'tree' gets deleted
    GtkTreeSelection* selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(tree));
    g_signal_handler_disconnect(G_OBJECT(selection), _selection_changed);
    g_signal_handler_disconnect(G_OBJECT(tree), _tree_move);

    unsetDocument();

    DialogBase::on_unrealize();
}

XmlTree::~XmlTree () { }

void XmlTree::unsetDocument()
{
    document_uri_set_connection.disconnect();
    if (deferred_on_tree_select_row_id != 0) {
        g_source_destroy(g_main_context_find_source_by_id(nullptr, deferred_on_tree_select_row_id));
        deferred_on_tree_select_row_id = 0;
    }
}

void XmlTree::documentReplaced()
{
    unsetDocument();
    if (auto document = getDocument()) {
        // TODO: Why is this a document property?
        document->setXMLDialogSelectedObject(nullptr);

        document_uri_set_connection =
            document->connectFilenameSet(sigc::bind(sigc::ptr_fun(&on_document_uri_set), document));
        on_document_uri_set(document->getDocumentFilename(), document);
        set_tree_repr(document->getReprRoot());
    } else {
        set_tree_repr(nullptr);
    }
}

void XmlTree::selectionChanged(Selection *selection)
{
    if (!blocked++) {
        Inkscape::XML::Node *node = get_dt_select();
        set_tree_select(node);
    }
    blocked--;
}

void XmlTree::set_tree_repr(Inkscape::XML::Node *repr)
{
    if (repr == selected_repr) {
        return;
    }

    sp_xmlview_tree_set_repr(tree, repr);
    if (repr) {
        set_tree_select(get_dt_select());
    } else {
        set_tree_select(nullptr);
    }

    propagate_tree_select(selected_repr);
}

/**
 * Expand all parent nodes of `repr`
 */
static void expand_parents(SPXMLViewTree *tree, Inkscape::XML::Node *repr)
{
    auto parentrepr = repr->parent();
    if (!parentrepr) {
        return;
    }

    expand_parents(tree, parentrepr);

    GtkTreeIter node;
    if (sp_xmlview_tree_get_repr_node(tree, parentrepr, &node)) {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store), &node);
        if (path) {
            gtk_tree_view_expand_row(GTK_TREE_VIEW(tree), path, false);
        }
    }
}

void XmlTree::set_tree_select(Inkscape::XML::Node *repr)
{
    if (selected_repr) {
        Inkscape::GC::release(selected_repr);
    }
    selected_repr = repr;
    if (selected_repr) {
        Inkscape::GC::anchor(selected_repr);
    }
    if (auto document = getDocument()) {
        document->setXMLDialogSelectedObject(nullptr);
    }
    if (repr) {
        GtkTreeIter node;

        Inkscape::GC::anchor(selected_repr);

        expand_parents(tree, repr);

        if (sp_xmlview_tree_get_repr_node(SP_XMLVIEW_TREE(tree), repr, &node)) {

            GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
            gtk_tree_selection_unselect_all (selection);

            GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store), &node);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), path, nullptr, TRUE, 0.66, 0.0);
            gtk_tree_selection_select_iter(selection, &node);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, NULL, false);
            gtk_tree_path_free(path);

        } else {
            g_message("XmlTree::set_tree_select : Couldn't find repr node");
        }
    } else {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        gtk_tree_selection_unselect_all (selection);

        on_tree_unselect_row_disable();
    }
    propagate_tree_select(repr);
}



void XmlTree::propagate_tree_select(Inkscape::XML::Node *repr)
{
    if (repr &&
       (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE ||
        repr->type() == Inkscape::XML::NodeType::TEXT_NODE ||
        repr->type() == Inkscape::XML::NodeType::COMMENT_NODE))
    {
        attributes->setRepr(repr);
    } else {
        attributes->setRepr(nullptr);
    }
}


Inkscape::XML::Node *XmlTree::get_dt_select()
{
    if (auto selection = getSelection()) {
        return selection->singleRepr();
    }
    return nullptr;
}


/**
 * Like SPDesktop::isLayer(), but ignores SPGroup::effectiveLayerMode().
 */
static bool isRealLayer(SPObject const *object)
{
    auto group = cast<SPGroup>(object);
    return group && group->layerMode() == SPGroup::LAYER;
}

void XmlTree::set_dt_select(Inkscape::XML::Node *repr)
{
    auto document = getDocument();
    if (!document)
        return;

    SPObject *object;
    if (repr) {
        while ( ( repr->type() != Inkscape::XML::NodeType::ELEMENT_NODE )
                && repr->parent() )
        {
            repr = repr->parent();
        } // end of while loop

        object = document->getObjectByRepr(repr);
    } else {
        object = nullptr;
    }

    blocked++;

    if (!object || !in_dt_coordsys(*object)) {
        // object not on canvas
    } else if (isRealLayer(object)) {
        getDesktop()->layerManager().setCurrentLayer(object);
    } else {
        if (is<SPGroup>(object->parent)) {
            getDesktop()->layerManager().setCurrentLayer(object->parent);
        }

        getSelection()->set(cast<SPItem>(object));
    }

    document->setXMLDialogSelectedObject(object);
    blocked--;
}


void XmlTree::on_tree_select_row(GtkTreeSelection *selection, gpointer data)
{
    XmlTree *self = static_cast<XmlTree *>(data);

    if (self->blocked || !self->getDesktop()) {
        return;
    }

    // Defer the update after all events have been processed. Allows skipping
    // of invalid intermediate selection states, like the automatic next row
    // selection after `gtk_tree_store_remove`.
    if (self->deferred_on_tree_select_row_id == 0) {
        self->deferred_on_tree_select_row_id = //
            g_idle_add(XmlTree::deferred_on_tree_select_row, data);
    }
}

gboolean XmlTree::deferred_on_tree_select_row(gpointer data)
{
    XmlTree *self = static_cast<XmlTree *>(data);

    self->deferred_on_tree_select_row_id = 0;

    GtkTreeIter   iter;
    GtkTreeModel *model;

    if (self->selected_repr) {
        Inkscape::GC::release(self->selected_repr);
        self->selected_repr = nullptr;
    }

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree));

    if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
        // Nothing selected, update widgets
        self->propagate_tree_select(nullptr);
        self->set_dt_select(nullptr);
        self->on_tree_unselect_row_disable();
        return FALSE;
    }

    Inkscape::XML::Node *repr = sp_xmlview_tree_node_get_repr(model, &iter);
    g_assert(repr != nullptr);


    self->selected_repr = repr;
    Inkscape::GC::anchor(self->selected_repr);

    self->propagate_tree_select(self->selected_repr);

    self->set_dt_select(self->selected_repr);

    self->on_tree_select_row_enable(&iter);

    return FALSE;
}


void XmlTree::after_tree_move(SPXMLViewTree * /*tree*/, gpointer value, gpointer data)
{
    XmlTree *self = static_cast<XmlTree *>(data);
    guint val = GPOINTER_TO_UINT(value);

    if (val) {
        DocumentUndo::done(self->getDocument(), Q_("Undo History / XML dialog|Drag XML subtree"), INKSCAPE_ICON("dialog-xml-editor"));
    } else {
        DocumentUndo::cancel(self->getDocument());
    }
}

void XmlTree::_set_status_message(Inkscape::MessageType /*type*/, const gchar *message, GtkWidget *widget)
{
    if (widget) {
        gtk_label_set_markup(GTK_LABEL(widget), message ? message : "");
    }
}

void XmlTree::on_tree_select_row_enable(GtkTreeIter *node)
{
    if (!node) {
        return;
    }

    Inkscape::XML::Node *repr = sp_xmlview_tree_node_get_repr(GTK_TREE_MODEL(tree->store), node);
    Inkscape::XML::Node *parent=repr->parent();

    //on_tree_select_row_enable_if_mutable
    xml_node_duplicate_button.set_sensitive(xml_tree_node_mutable(node));
    xml_node_delete_button.set_sensitive(xml_tree_node_mutable(node));

    //on_tree_select_row_enable_if_element
    if (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE) {
        xml_element_new_button.set_sensitive(true);
        xml_text_new_button.set_sensitive(true);

    } else {
        xml_element_new_button.set_sensitive(false);
        xml_text_new_button.set_sensitive(false);
    }

    //on_tree_select_row_enable_if_has_grandparent
    {
        GtkTreeIter parent;
        if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &parent, node)) {
            GtkTreeIter grandparent;
            if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &grandparent, &parent)) {
                unindent_node_button.set_sensitive(true);
            } else {
                unindent_node_button.set_sensitive(false);
            }
        } else {
            unindent_node_button.set_sensitive(false);
        }
    }
    // on_tree_select_row_enable_if_indentable
    gboolean indentable = FALSE;

    if (xml_tree_node_mutable(node)) {
        Inkscape::XML::Node *prev;

        if ( parent && repr != parent->firstChild() ) {
            g_assert(parent->firstChild());

            // skip to the child just before the current repr
            for ( prev = parent->firstChild() ;
                  prev && prev->next() != repr ;
                  prev = prev->next() ){};

            if (prev && (prev->type() == Inkscape::XML::NodeType::ELEMENT_NODE)) {
                indentable = TRUE;
            }
        }
    }

    indent_node_button.set_sensitive(indentable);

    //on_tree_select_row_enable_if_not_first_child
    {
        if ( parent && repr != parent->firstChild() ) {
            raise_node_button.set_sensitive(true);
        } else {
            raise_node_button.set_sensitive(false);
        }
    }

    //on_tree_select_row_enable_if_not_last_child
    {
        if ( parent && (parent->parent() && repr->next())) {
            lower_node_button.set_sensitive(true);
        } else {
            lower_node_button.set_sensitive(false);
        }
    }
}


gboolean XmlTree::xml_tree_node_mutable(GtkTreeIter *node)
{
    // top-level is immutable, obviously
    GtkTreeIter parent;
    if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &parent, node)) {
        return false;
    }


    // if not in base level (where namedview, defs, etc go), we're mutable
    GtkTreeIter child;
    if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &child, &parent)) {
        return true;
    }

    Inkscape::XML::Node *repr;
    repr = sp_xmlview_tree_node_get_repr(GTK_TREE_MODEL(tree->store), node);
    g_assert(repr);

    // don't let "defs" or "namedview" disappear
    if ( !strcmp(repr->name(),"svg:defs") ||
         !strcmp(repr->name(),"sodipodi:namedview") ) {
        return false;
    }

    // everyone else is okay, I guess.  :)
    return true;
}



void XmlTree::on_tree_unselect_row_disable()
{
    xml_text_new_button.set_sensitive(false);
    xml_element_new_button.set_sensitive(false);
    xml_node_delete_button.set_sensitive(false);
    xml_node_duplicate_button.set_sensitive(false);
    unindent_node_button.set_sensitive(false);
    indent_node_button.set_sensitive(false);
    raise_node_button.set_sensitive(false);
    lower_node_button.set_sensitive(false);
}

void XmlTree::onCreateNameChanged()
{
    Glib::ustring text = name_entry->get_text();
    /* TODO: need to do checking a little more rigorous than this */
    create_button->set_sensitive(!text.empty());
}

void XmlTree::on_document_uri_set(gchar const * /*uri*/, SPDocument * /*document*/)
{
/*
 * Seems to be no way to set the title on a docked dialog
*/
}

gboolean XmlTree::quit_on_esc (GtkWidget *w, GdkEventKey *event, GObject */*tbl*/)
{
    switch (Inkscape::UI::Tools::get_latin_keyval (event)) {
        case GDK_KEY_Escape: // defocus
            gtk_widget_destroy(w);
            return TRUE;
        case GDK_KEY_Return: // create
        case GDK_KEY_KP_Enter:
            gtk_widget_destroy(w);
            return TRUE;
    }
    return FALSE;
}

void XmlTree::cmd_new_element_node()
{
    auto document = getDocument();
    if (!document)
        return;

    Gtk::Dialog dialog;
    Gtk::Entry entry;

    dialog.get_content_area()->pack_start(entry);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Create", Gtk::RESPONSE_OK);
    dialog.show_all();

    int result = dialog.run();
    if (result == Gtk::RESPONSE_OK) {
        Glib::ustring new_name = entry.get_text();
        if (!new_name.empty()) {
            Inkscape::XML::Document *xml_doc = document->getReprDoc();
            Inkscape::XML::Node *new_repr;
            new_repr = xml_doc->createElement(new_name.c_str());
            Inkscape::GC::release(new_repr);
            selected_repr->appendChild(new_repr);
            set_tree_select(new_repr);
            set_dt_select(new_repr);

            DocumentUndo::done(document, Q_("Undo History / XML dialog|Create new element node"), INKSCAPE_ICON("dialog-xml-editor"));
        }
    }
} // end of cmd_new_element_node()


void XmlTree::cmd_new_text_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *text = xml_doc->createTextNode("");
    selected_repr->appendChild(text);

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Create new text node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(text);
    set_dt_select(text);
}

void XmlTree::cmd_duplicate_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();
    Inkscape::XML::Node *dup = selected_repr->duplicate(parent->document());
    parent->addChild(dup, selected_repr);

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Duplicate node"), INKSCAPE_ICON("dialog-xml-editor"));

    GtkTreeIter node;

    if (sp_xmlview_tree_get_repr_node(SP_XMLVIEW_TREE(tree), dup, &node)) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        gtk_tree_selection_select_iter(selection, &node);
    }
}

void XmlTree::cmd_delete_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    document->setXMLDialogSelectedObject(nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();

    sp_repr_unparent(selected_repr);

    if (parent) {
        auto parentobject = document->getObjectByRepr(parent);
        if (parentobject) {
            parentobject->requestDisplayUpdate(SP_OBJECT_CHILD_MODIFIED_FLAG);
        }
    }

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Delete node"), INKSCAPE_ICON("dialog-xml-editor"));
}

void XmlTree::cmd_raise_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();
    g_return_if_fail(parent != nullptr);
    g_return_if_fail(parent->firstChild() != selected_repr);

    Inkscape::XML::Node *ref = nullptr;
    Inkscape::XML::Node *before = parent->firstChild();
    while (before && (before->next() != selected_repr)) {
        ref = before;
        before = before->next();
    }

    parent->changeOrder(selected_repr, ref);

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Raise node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(selected_repr);
    set_dt_select(selected_repr);
}



void XmlTree::cmd_lower_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    g_return_if_fail(selected_repr->next() != nullptr);
    Inkscape::XML::Node *parent = selected_repr->parent();

    parent->changeOrder(selected_repr, selected_repr->next());

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Lower node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(selected_repr);
    set_dt_select(selected_repr);
}

void XmlTree::cmd_indent_node()
{
    auto document = getDocument();
    if (!document)
        return;

    Inkscape::XML::Node *repr = selected_repr;
    g_assert(repr != nullptr);

    Inkscape::XML::Node *parent = repr->parent();
    g_return_if_fail(parent != nullptr);
    g_return_if_fail(parent->firstChild() != repr);

    Inkscape::XML::Node* prev = parent->firstChild();
    while (prev && (prev->next() != repr)) {
        prev = prev->next();
    }
    g_return_if_fail(prev != nullptr);
    g_return_if_fail(prev->type() == Inkscape::XML::NodeType::ELEMENT_NODE);

    Inkscape::XML::Node* ref = nullptr;
    if (prev->firstChild()) {
        for( ref = prev->firstChild() ; ref->next() ; ref = ref->next() ){};
    }

    parent->removeChild(repr);
    prev->addChild(repr, ref);

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Indent node"), INKSCAPE_ICON("dialog-xml-editor"));
    set_tree_select(repr);
    set_dt_select(repr);

} // end of cmd_indent_node()



void XmlTree::cmd_unindent_node()
{
    auto document = getDocument();
    if (!document)
        return;

    Inkscape::XML::Node *repr = selected_repr;
    g_assert(repr != nullptr);

    Inkscape::XML::Node *parent = repr->parent();
    g_return_if_fail(parent);
    Inkscape::XML::Node *grandparent = parent->parent();
    g_return_if_fail(grandparent);

    parent->removeChild(repr);
    grandparent->addChild(repr, parent);

    DocumentUndo::done(document, Q_("Undo History / XML dialog|Unindent node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(repr);
    set_dt_select(repr);

} // end of cmd_unindent_node()

/** Returns true iff \a item is suitable to be included in the selection, in particular
    whether it has a bounding box in the desktop coordinate system for rendering resize handles.

    Descendents of <defs> nodes (markers etc.) return false, for example.
*/
bool XmlTree::in_dt_coordsys(SPObject const &item)
{
    /* Definition based on sp_item_i2doc_affine. */
    SPObject const *child = &item;
    while (is<SPItem>(child)) {
        SPObject const * const parent = child->parent;
        if (parent == nullptr) {
            g_assert(is<SPRoot>(child));
            if (child == &item) {
                // item is root
                return false;
            }
            return true;
        }
        child = parent;
    }
    g_assert(!is<SPRoot>(child));
    return false;
}

void XmlTree::desktopReplaced() {
    // subdialog does not receive desktopReplace calls, we need to propagate desktop change
    if (attributes) {
        attributes->setDesktop(getDesktop());
    }
}

}
}
}

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
