// SPDX-License-Identifier: GPL-2.0-or-later
#include "global-palettes.h"

#include <array>
#include <giomm/file.h>
#include <giomm/fileinputstream.h>
#include <giomm/inputstream.h>
#include <glibmm/exception.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <iomanip>

// Using Glib::regex because
//  - std::regex is too slow in debug mode.
//  - boost::regex requires a library not present in the CI image.
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <iostream>
#include <lcms2.h>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "color/cmyk-conv.h"
#include "hsluv.h"
#include "io/resource.h"
#include "io/sys.h"

namespace Inkscape {

Glib::ustring get_extension(const Glib::ustring name) {
    auto extpos = name.rfind('.');
    if (extpos != Glib::ustring::npos) {
        auto ext = name.substr(extpos).casefold();
        return ext;
    }
    return {};
}

std::vector<uint8_t> read_data(const Glib::RefPtr<Gio::InputStream>& s, size_t len) {
    std::vector<uint8_t> buf(len, 0);
    s->read(buf.data(), len);
    return buf;
}

std::string read_string(const Glib::RefPtr<Gio::InputStream>& s, size_t len) {
    std::vector<char> buf(len, 0);
    s->read(buf.data(), len);
    return std::string(buf.data(), len);
}

template<typename T>
T read_value(const Glib::RefPtr<Gio::InputStream>& s) {
    uint8_t buf[sizeof(T)];
    s->read(buf, sizeof(T));
    T val = 0;
    for (int i = 0; i < sizeof(T); ++i) {
        val <<= 8;
        val |= buf[i];
    }
    return val;
}

float read_float(const Glib::RefPtr<Gio::InputStream>& s) {
    auto val = read_value<uint32_t>(s);
    return *reinterpret_cast<float*>(&val);
}

Glib::ustring read_pstring(const Glib::RefPtr<Gio::InputStream>& s, bool short_string = false) {
    size_t len = short_string ? read_value<uint16_t>(s) : read_value<uint32_t>(s);
    if (!len) return {};

    std::vector<uint16_t> buf(len, 0);
    s->read(buf.data(), 2 * len);
    for (int i = 0; i < len; ++i) {
        auto c = buf[i];
        c = (c & 0xff) << 8 | c >> 8; // std::byteswap()
        buf[i] = c;
    }
    // null terminated string?
    if (buf[len - 1] == 0) --len;

    auto string = g_utf16_to_utf8(buf.data(), len, nullptr, nullptr, nullptr);
    if (!string) return {};

    Glib::ustring ret(string);
    g_free(string);
    return ret;
}

void skip(const Glib::RefPtr<Gio::InputStream>& s, size_t bytes) {
    s->skip(bytes);
}

using namespace UI::Dialog;

// Load Adobe ACB color book
void load_acb_palette(PaletteFileData& palette, const char* fname) {
    auto file = Gio::File::create_for_path(fname);
    auto stream = file->read();
    auto magic = read_string(stream, 4);
    if (magic != "8BCB") throw std::runtime_error(std::string("ACB file header not recognized: ") + fname);

    auto version = read_value<uint16_t>(stream);
    if (version != 1) {
        g_message("Unknown ACB palette version");
    }

    /* id */ read_value<uint16_t>(stream);

    auto ttl = read_pstring(stream);
    auto prefix = read_pstring(stream);
    auto suffix = read_pstring(stream);
    auto desc = read_pstring(stream);
    auto extract = [](const Glib::ustring& str) {
        auto pos = str.find('=');
        if (pos != Glib::ustring::npos) {
            return str.substr(pos + 1);
        }
        return Glib::ustring();
    };
    prefix = extract(prefix);
    suffix = extract(suffix);
    ttl = extract(ttl);

    auto color_count = read_value<uint16_t>(stream);
    palette.page_size = read_value<uint16_t>(stream);
    palette.page_offset = read_value<uint16_t>(stream);
    auto cs = read_value<uint16_t>(stream);
    int components = 0;
    PaletteFileData::ColorSpace color_space;

	switch (cs) {
    case 0: // RGB
        components = 3;
        color_space = PaletteFileData::Rgb255;
        break;
    case 2: // CMYK
        components = 4;
        color_space = PaletteFileData::Cmyk100;
        break;
    case 7: // LAB
        components = 3;
        color_space = PaletteFileData::Lab100;
        break;
    case 8: // Grayscale
        components = 1;
        color_space = PaletteFileData::Rgb255; // using RGB for grayscale
        break;
    default:
        throw std::runtime_error("ACB file color space not supported");
    }

    auto ext = get_extension(ttl);
    if (ext == ".acb") {
        // extension in palette title -> junk name; use file name instead
        palette.name = Glib::path_get_basename(fname);
    }
    else {
        auto r = ttl.find("^R");
        if (r != Glib::ustring::npos) ttl.replace(r, 2, "®");
        palette.name = ttl;
    }

    palette.columns = 0;
    palette.user = true;

// g_message("pal: %s - '%s' col: %d, space: %d, page: %d offs: %d", palette.name.c_str(), ttl.c_str(), color_count, color_space, palette.page_size, palette.page_offset);

    palette.colors.reserve(color_count);
    // simple CMYK converter here; original palette colors are kept for later use
    CmykConverter convert;

    for (int index = 0; index < color_count; ++index) {

        auto name = read_pstring(stream);
        auto code = read_string(stream, 6);
        auto channels = read_data(stream, components);
        PaletteFileData::Color color;
        std::ostringstream ost;
        ost.precision(3);

        switch (color_space) {
            case PaletteFileData::Lab100: {
                auto l = std::floor(channels[0] / 2.55f + 0.5f);
                auto a = channels[1] - 128.0f;
                auto b = channels[2] - 128.0f;
                auto rgb = Hsluv::lab_to_rgb(l, a, b);
                unsigned ur = rgb[0] * 255;
                unsigned ug = rgb[1] * 255;
                unsigned ub = rgb[2] * 255;
                color = {{l, a, b, 0}, color_space, {ur, ug, ub}};
                ost << "L: " << l << " a: " << a << " b: " << b;
            }
            break;

            case PaletteFileData::Cmyk100: {
                // 0% - 100%
                auto c = std::floor((255 - channels[0]) / 2.55f + 0.5f);
                auto m = std::floor((255 - channels[1]) / 2.55f + 0.5f);
                auto y = std::floor((255 - channels[2]) / 2.55f + 0.5f);
                auto k = std::floor((255 - channels[3]) / 2.55f + 0.5f);
                auto [r, g, b] = convert.cmyk_to_rgb(c, m, y, k);
                color = {{c, m, y, k}, color_space, {r, g, b}};
                ost << "C: " << c << "% M: " << m << "% Y: " << y << "% K: " << k << '%';
            }
            break;

            case PaletteFileData::Rgb255: {
                float r = channels[0];
                float g = cs == 1 ? r : channels[1];
                float b = cs == 1 ? r : channels[2];
                unsigned ur = r;
                unsigned ug = g;
                unsigned ub = b;
                color = {{r, g, b, 0}, color_space, {ur, ug, ub}};
                if (cs == 1) {
                    // grayscale
                    ost << "R: " << ur << " G: " << ug << " B: " << ub;
                }
                else {
                    ost << "R: " << ur << " G: " << ug << " B: " << ub;
                }
            }
            break;

            default:
                throw std::runtime_error("Palette color space unexpected");
        }

        if (name.empty()) {
            color.filler = true;
        }
        else {
            color.name = prefix + name + suffix;
            color.definition = ost.str();
        }
        palette.colors.push_back(color);
    }
}

void load_ase_swatches(PaletteFileData& palette, const char* fname) {
    auto file = Gio::File::create_for_path(fname);
    auto stream = file->read();
    auto magic = read_string(stream, 4);
    if (magic != "ASEF") throw std::runtime_error("ASE file header not recognized");

    auto version_major = read_value<uint16_t>(stream);
    auto version_minor = read_value<uint16_t>(stream);

    auto block_count = read_value<uint32_t>(stream);
    CmykConverter convert;

    for (uint32_t block = 0; block < block_count; ++block) {
        auto block_type = read_value<uint16_t>(stream);
        auto block_length = read_value<uint32_t>(stream);
        std::ostringstream ost;

        if (block_type == 0xc001) { // group start
            if (!palette.name.empty()) {
                g_warning("multigroup in %s", fname);
            }
            auto name = read_pstring(stream, true);
            palette.name = name;
        }
        else if (block_type == 0x0001) { // color entry
            auto color_name = read_pstring(stream, true);
            auto mode = read_string(stream, 4);
            if (mode == "CMYK") {
                auto c = read_float(stream) * 100;
                auto m = read_float(stream) * 100;
                auto y = read_float(stream) * 100;
                auto k = read_float(stream) * 100;
                auto type = read_value<uint16_t>(stream);
                auto [r, g, b] = convert.cmyk_to_rgb(c, m, y, k);
                ost << "C: " << c << "% M: " << m << "% Y: " << y << "% K: " << k << '%';
                palette.colors.push_back(
                    PaletteFileData::Color {{c, m, y, k}, PaletteFileData::Cmyk100, {r, g, b}, color_name, ost.str()}
                );
            }
            else if (mode == "RGB ") {
                auto r = read_float(stream) * 255;
                auto g = read_float(stream) * 255;
                auto b = read_float(stream) * 255;
                auto type = read_value<uint16_t>(stream);
                palette.colors.push_back(
                    PaletteFileData::Color {{r, g, b, 0}, PaletteFileData::Rgb255, {(unsigned)r, (unsigned)g, (unsigned)b}, color_name}
                );
            }
            else if (mode == "LAB ") {
                //TODO - verify
                auto l = read_float(stream) * 100;
                auto a = read_float(stream) * 100;
                auto b = read_float(stream) * 100;
                auto rgb = Hsluv::lab_to_rgb(l, a, b);
                unsigned ur = rgb[0] * 255;
                unsigned ug = rgb[1] * 255;
                unsigned ub = rgb[2] * 255;
                ost << "L: " << l << " a: " << a << " b: " << b;
                palette.colors.push_back(
                    PaletteFileData::Color {{l, a, b, 0}, PaletteFileData::Lab100, {ur, ug, ub}, color_name, ost.str()}
                );
            }
            else if (mode == "Gray") {
                auto g = read_float(stream) * 255;
                auto type = read_value<uint16_t>(stream);
                palette.colors.push_back(
                    PaletteFileData::Color {{g, g, g, 0}, PaletteFileData::Rgb255, {(unsigned)g, (unsigned)g, (unsigned)g}, color_name}
                );
            }
            else {
                std::ostringstream ost;
                ost << "ASE color mode not recognized: '" << mode << "'";
                throw std::runtime_error(ost.str());
            }
           
        }
        else if (block_type == 0xc002) { // group end
        }
        else {
            skip(stream, block_length);
        }
    }

    if (palette.name.empty()) {
        palette.name = Glib::path_get_basename(fname);
    }
// g_message("pal: %s - %s", palette.name.c_str(), fname);
}

} // namespace

// Load GIMP color palette
Inkscape::UI::Dialog::PaletteFileData::PaletteFileData(Glib::ustring const &path)
{
    name = Glib::path_get_basename(path);
    columns = 1;
    user = Inkscape::IO::file_is_writable(path.c_str());

    auto f = std::unique_ptr<FILE, void(*)(FILE*)>(Inkscape::IO::fopen_utf8name(path.c_str(), "r"), [] (FILE *f) {if (f) std::fclose(f);});
    if (!f) throw std::runtime_error("Failed to open file");

    char buf[1024];
    if (!std::fgets(buf, sizeof(buf), f.get())) throw std::runtime_error("File is empty");
    if (std::strncmp("GIMP Palette", buf, 12) != 0) throw std::runtime_error("First line is wrong");

    static auto const regex_rgb   = Glib::Regex::create("\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s*(?:\\s(.*\\S)\\s*)?$", Glib::REGEX_OPTIMIZE | Glib::REGEX_ANCHORED);
    static auto const regex_name  = Glib::Regex::create("\\s*Name:\\s*(.*\\S)", Glib::REGEX_OPTIMIZE | Glib::REGEX_ANCHORED);
    static auto const regex_cols  = Glib::Regex::create("\\s*Columns:\\s*(.*\\S)", Glib::REGEX_OPTIMIZE | Glib::REGEX_ANCHORED);
    static auto const regex_blank = Glib::Regex::create("\\s*(?:$|#)", Glib::REGEX_OPTIMIZE | Glib::REGEX_ANCHORED);

    while (std::fgets(buf, sizeof(buf), f.get())) {
        auto line = Glib::ustring(buf); // Unnecessary copy required until using a glibmm with support for string views. TODO: Fix when possible.
        Glib::MatchInfo match;
        if (regex_rgb->match(line, match)) { // ::regex_match(line, match, boost::regex(), boost::regex_constants::match_continuous)) {
            // RGB color, followed by an optional name.
            Color color;
            for (int i = 0; i < 3; i++) {
                color.rgb[i] = std::clamp(std::stoi(match.fetch(i + 1)), 0, 255);
                color.channels[i] = color.rgb[i];
            }
            color.channels[3] = 0;
            color.space = Rgb255;
            color.name = match.fetch(4);

            if (!color.name.empty()) {
                // Translate the name if present.
                color.name = g_dpgettext2(nullptr, "Palette", color.name.c_str());
            } else {
                // Otherwise, set the name to be the hex value.
                color.name = Glib::ustring::compose("#%1%2%3",
                                 Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), color.rgb[0]),
                                 Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), color.rgb[1]),
                                 Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), color.rgb[2])
                             ).uppercase();
            }

            colors.emplace_back(std::move(color));
        } else if (regex_name->match(line, match)) {
            // Header entry for name.
            name = match.fetch(1);
        } else if (regex_cols->match(line, match)) {
            // Header entry for columns.
            columns = std::clamp(std::stoi(match.fetch(1)), 1, 1000);
        } else if (regex_blank->match(line, match)) {
            // Comment or blank line.
        } else {
            // Unrecognised.
            throw std::runtime_error("Invalid line " + std::string(line));
        }
    }
}

Inkscape::UI::Dialog::GlobalPalettes::GlobalPalettes()
{
    // Load the palettes.
    for (auto &path : Inkscape::IO::Resource::get_filenames(Inkscape::IO::Resource::PALETTES, {".gpl", ".acb", ".ase"})) {
        try {
            auto ext = get_extension(path);
            if (ext == ".acb") {
                PaletteFileData p;
                load_acb_palette(p, path.c_str());
                palettes.push_back(std::move(p));
            }
            else if (ext == ".ase") {
                PaletteFileData p;
                load_ase_swatches(p, path.c_str());
                palettes.push_back(std::move(p));
            }
            else {
                palettes.emplace_back(path);
            }
        } catch (std::runtime_error const &e) {
            g_warning("Error loading palette %s: %s", path.c_str(), e.what());
        } catch (std::logic_error const &e) {
            g_warning("Error loading palette %s: %s", path.c_str(), e.what());
        }
        catch (Glib::Exception& ex) {
            g_warning("Error loading palette %s: %s", path.c_str(), ex.what().c_str());
        }
    }

    std::sort(palettes.begin(), palettes.end(), [] (decltype(palettes)::const_reference a, decltype(palettes)::const_reference b) {
        // Sort by user/system first...
        if (a.user > b.user) return true;
        if (b.user > a.user) return false;
        // ... then by name.
        return a.name.compare(b.name) < 0;
    });
}

Inkscape::UI::Dialog::GlobalPalettes const &Inkscape::UI::Dialog::GlobalPalettes::get()
{
    static GlobalPalettes instance;
    return instance;
}
