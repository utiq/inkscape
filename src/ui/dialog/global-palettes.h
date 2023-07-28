// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Global color palette information.
 */
/* Authors: PBS <pbs3141@gmail.com>
 * Copyright (C) 2022 PBS
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H
#define INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H

#include <array>
#include <vector>
#include <glibmm/ustring.h>

namespace Inkscape {
namespace UI {
namespace Dialog {

/**
 * The data loaded from a palette file.
 */
struct PaletteFileData
{
    /// Name of the palette, either specified in the file or taken from the filename.
    Glib::ustring name;

    /// The preferred number of columns (unused).
    int columns;

    /// Whether this is a user or system palette.
    bool user;

    /// Color space of all colors in this palette. Original definitions are kept in "Color.channels"
    /// for use with ICC profiles. Preview sRGB colors are inside "Color.rgb"
    enum ColorSpace {
        Undefined, // not a valid color definition
        Rgb255, // RGB 0..255
        Lab100, // Cie*Lab, L 0..100, a, b -128..127
        Cmyk100 // CMYK 0%..100%
    };

    struct Color
    {
        /// Original color definition (Lab, Cmyk, Rgb); unused channels 0.0
        std::array<float, 4> channels;

        /// Color space of this color.
        ColorSpace space = Undefined;

        /// RGB color.
        std::array<unsigned, 3> rgb;

        /// Name of the color, either specified in the file or generated from the rgb.
        Glib::ustring name;

        /// Color as defined in a palette, for informational purposes.
        Glib::ustring definition;

        /// if true, this color definition is blank, and it acts as a spacer to align other colors
        bool filler = false;

#ifdef false // not currently used
        bool operator < (const Color& c) const {
            for (int i = 0; i < rgb.size(); ++i) {
                if (rgb[i] < c.rgb[i]) return true;
                if (rgb[i] > c.rgb[i]) return false;
            }
            for (int i = 0; i < channels.size(); ++i) {
                if (channels[i] < c.channels[i]) return true;
                if (channels[i] > c.channels[i]) return false;
            }
            return name < c.name;
        }
#endif
    };

    /// The list of colors in the palette.
    std::vector<Color> colors;

    /// Certain color palettes are organized into blocks, typically 7 or 8 colors long.
    /// Page size tells us how big the block are, if any.
    /// We can use this info to organize colors in columns in multiples of the page size.
    unsigned int page_size = 0;
    /// Index to a representative color of the color block; starts from 0 for each block.
    unsigned int page_offset = 0;

    /// Load from the given file, throwing std::runtime_error on fail.
    PaletteFileData(Glib::ustring const &path);

    /// Empty palette
    PaletteFileData() {}
};

/**
 * Singleton class that manages the static list of global palettes.
 */
class GlobalPalettes
{
    GlobalPalettes();
public:
    static GlobalPalettes const &get();
    std::vector<PaletteFileData> palettes;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_GLOBAL_PALETTES_H
