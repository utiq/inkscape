// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains recently used font list related logic. The functions to manage the
 * recently used fonts are defined in this file.
 *
 * Authors:
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "recently-used-fonts.h"
#include "font-collections.h"
#include "libnrtype/font-lister.h"
#include "preferences.h"

#include <fstream>
// #include <iostream>

#ifdef _WIN32
#include<direct.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

using namespace Inkscape::IO::Resource;

namespace Inkscape {

// get_instance method for the singleton class.
RecentlyUsedFonts* RecentlyUsedFonts::get()
{
    static RecentlyUsedFonts* s_instance = new Inkscape::RecentlyUsedFonts();
    return s_instance;
}

RecentlyUsedFonts::RecentlyUsedFonts()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _max_size = prefs->getInt("/tools/text/recently_used_fonts_size", 10);

    init();
}

void RecentlyUsedFonts::init()
{
    // Clear the previous collections(we may be re-reading).
    clear();

    // Step 1: Fetch the collections directory from the system directory.
    Glib::ustring system_directory = get_path_string(SYSTEM, FONTCOLLECTIONS, "");

    // Generate the name of the file.
    Glib::ustring file_name = "recently_used_fonts.txt";
    std::string file_path = get_path_string(SYSTEM, FONTCOLLECTIONS, file_name.c_str());
    std::string file_dir = get_path_string(SYSTEM, FONTCOLLECTIONS, nullptr);

    static bool create_dir = true;

    if(create_dir) {
#ifdef _WIN32
        mkdir(file_dir.c_str());
#else
        mkdir(file_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
        create_dir = false;
    }

    // Read the file.
    read(file_path);
}

void RecentlyUsedFonts::clear()
{
    _recent_list.clear();
}

/*
void RecentlyUsedFonts::print_recently_used_fonts()
{
    std::cout << std::endl << "********************" << std::endl;

    for(auto const& font: _recent_list) {
        std::cout << font << std::endl;
    }

    std::cout << std::endl << "********************" << std::endl;
}
*/

// Read fonts stored in a collection file.
void RecentlyUsedFonts::read(const Glib::ustring& file_path)
{
    // Filestream object to read data from the file.
    std::ifstream input_file(file_path);

    // Now read all the fonts stored in this file.
    std::string line;
    FontCollections *font_collections = Inkscape::FontCollections::get();
    Inkscape::FontLister *font_lister= Inkscape::FontLister::get_instance();

    while(getline(input_file, line)) {
        // Get rid of unwanted characters from the left and right.
        line = font_collections->trim_left_and_right(line);
        Glib::ustring font = line;

        // Now check if the font is installed on the system because it is possible
        // that a previously installed font may not be available now.
        if(font_lister->font_installed_on_system(font)) {
            _recent_list.push_front(font);
        }
    }
}

// Function to write the recently used fonts to a file.
void RecentlyUsedFonts::write_recently_used_fonts()
{
    // Step 1: Fetch the collections directory from the system directory.

    // Generate the name of the file.
    Glib::ustring file_name = "recently_used_fonts.txt";
    std::string file_path = get_path_string(SYSTEM, FONTCOLLECTIONS, file_name.c_str());

    std::fstream output_file;
    output_file.open(file_path, std::fstream::out);

    for(auto it = _recent_list.rbegin(); it !=  _recent_list.rend(); ++it) {
        // std::cout << ". Writing font:" << (*it) << std::endl;
        output_file << (*it) << '\n';
    }

    // Very Important: Close the file after use.
    output_file.close();
    init();
}

void RecentlyUsedFonts::change_max_list_size(const int& max_size)
{
    // std::cout << "Changing size from: " << _max_size << " to: " << max_size << "\n";

    if(max_size < 0) {
        std::cerr << "Can not set negative size" << std::endl;
        return;
    }

    _max_size = max_size;

    // If the font that are currently stored in the recent list are more than the
    // new max size of the list, then we need to remove the least recent fonts
    // to make sure that the list if not longer than the max size.
    int difference = _recent_list.size() - _max_size;

    if(difference > 0) {
        while(difference--) {
            _recent_list.pop_back();
        }
    }

    update_signal.emit();
}

// This function is called whenever the user clicks the Apply button in
// the text and font dialog. It inserts the selected family into the
// recently used list and it's correct position.
void RecentlyUsedFonts::prepend_to_list(const Glib::ustring& font_name) {
    /*
     * Look-Up step
     */
    auto it = std::find(_recent_list.begin(), _recent_list.end(), font_name);

    /*
     * If the element is already present
     * Delete it, because we'll re-insert it at the top
     */
     if(it != _recent_list.end()) {
         _recent_list.erase(it);
     }
     else {
         /*
          * Insert the element in the list.
          */
         _recent_list.push_front(font_name);
     }

     /*
      * Check if the current size exceeds the max size
      * If yes, then delete an element from the end
      */
     if(_recent_list.size() > _max_size) {
         _recent_list.pop_back();
     }

     write_recently_used_fonts();
     update_signal.emit();
}

/*
bool RecentlyUsedFonts::is_empty()
{
    return _max_size == 0;
}
*/

int RecentlyUsedFonts::get_count()
{
    return _recent_list.size();
}

// Returns the recently used fonts.
const std::list <Glib::ustring> RecentlyUsedFonts::get_fonts()
{
    return _recent_list;
}

} // Namespace

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
