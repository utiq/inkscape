// SPDX-License-Identifier: GPL-2.0-or-later

#include "completion-popup.h"
#include <cassert>
#include <glibmm/ustring.h>
#include <gtkmm/entrycompletion.h>
#include <gtkmm/searchentry.h>
#include "ui/builder-utils.h"

namespace Inkscape {
namespace UI {
namespace Widget {

enum Columns {
    ColID = 0,
    ColName,
    ColIcon,
    ColSearch
};

CompletionPopup::CompletionPopup() :
    _builder(create_builder("completion-box.glade")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")),
    _completion(get_object<Gtk::EntryCompletion>(_builder, "completion")),
    _popup(get_widget<Gtk::Menu>(_builder, "popup"))
{
    _list = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(_builder->get_object("list"));
    assert(_list);

    add(get_widget<Gtk::Box>(_builder, "main-box"));

    _completion->set_match_func([=](const Glib::ustring& text, const Gtk::TreeModel::const_iterator& it){
        Glib::ustring str;
        it->get_value(ColSearch, str);
        if (str.empty()) {
            return false;
        }
        return str.lowercase().find(text.lowercase()) != Glib::ustring::npos;
    });

    // clear search box without triggering completion popup menu
    auto clear = [=]() { _search.get_buffer()->set_text(Glib::ustring()); };

    _completion->signal_match_selected().connect([=](const Gtk::TreeModel::iterator& it){
        int id;
        it->get_value(ColID, id);
        _match_selected.emit(id);
        clear();
        return true;
    }, false);

    _search.signal_focus_in_event().connect([=](GdkEventFocus*){
        clear();
        return false;
    });

    _search.signal_focus_out_event().connect([=](GdkEventFocus*){
        clear();
        return false;
    });

    _search.signal_stop_search().connect([=](){
        clear();
    });

    show();
}

void CompletionPopup::clear_completion_list() {
    _list->clear();
}

void CompletionPopup::add_to_completion_list(int id, Glib::ustring name, Glib::ustring icon_name, Glib::ustring search_text) {
    auto row = *_list->append();
    row.set_value(ColID, id);
    row.set_value(ColName, name);
    row.set_value(ColIcon, icon_name);
    row.set_value(ColSearch, search_text.empty() ? name : search_text);
}

Gtk::Menu& CompletionPopup::get_menu() {
    return _popup;
}

Gtk::SearchEntry& CompletionPopup::get_entry() {
    return _search;
}

sigc::signal<void (int)>& CompletionPopup::on_match_selected() {
    return _match_selected;
}

}}} // namespaces
