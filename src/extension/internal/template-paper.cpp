// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Contain internal sizes of paper which can be used in various
 * functions to make and size pages.
 *
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "template-paper.h"

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

/**
 * Return the width and height of the new page with the orientation.
 */
Geom::Point TemplatePaper::get_template_size(Inkscape::Extension::Template *tmod) const
{
    std::string orient = tmod->get_param_optiongroup("orientation", "port");
    double min = tmod->get_param_float("min");
    double max = tmod->get_param_float("max");
    if (orient == "port") {
        return Geom::Point(min, max);
    } else if (orient == "land") {
        return Geom::Point(max, min);
    }
    g_warning("Unknown orientation for paper! '%s'", orient.c_str());
    return Geom::Point(100, 100);
}

void TemplatePaper::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.paper</id>"
            "<name>" N_("Paper Sizes") "</name>"
            "<description>" N_("General sizes for paper") "</description>"
            "<category>" N_("Print") "</category>"

            "<param name='unit' gui-text='" N_("Unit") "' type='string'>mm</param>"
            "<param name='min' gui-text='" N_("Shortest Side") "' type='float' min='1.0' max='100000.0'>210.0</param>"
            "<param name='max' gui-text='" N_("Longest Side") "' type='float' min='1.0' max='100000.0'>297.0</param>"
            "<param name='orientation' gui-text='" N_("Orientation") "' type='optiongroup' appearance='radio'>"
              "<option value='port'>" N_("Portrait") "</option>"
              "<option value='land'>" N_("Landscape") "</option>"
            "</param>"

            "<template unit='mm' icon='print_portrait' priority='-100'>"
"<preset name='A4 (Portrait)' label='210mm x 297mm' min='210' max='297' orientation='port' selectable='true' priority='-110'/>"
"<preset name='A4 (Landscape)' label='297mm x 210mm' min='210' max='297' orientation='land' selectable='true' icon='print_landscape' priority='-109'/>"
"<preset name='US Letter (Portrait)' label='8.5in x 11in' min='8.5' max='11' unit='in' orientation='port' selectable='true' icon='print_US_portrait' priority='-108'/>"
"<preset name='US Letter (Landscape)' label='11in x 8.5in' min='8.5' max='11' unit='in' orientation='land' selectable='true' icon='print_US_landscape' priority='-107'/>"
"<preset name='A0' label='841mm x 1189mm' min='841' max='1189' selectable='true'/>"
"<preset name='A1' label='594mm x 841mm' min='594' max='841' selectable='true'/>"
"<preset name='A2' label='420mm x 594mm' min='420' max='594' selectable='true'/>"
"<preset name='A3' label='297mm x 420mm' min='297' max='420' selectable='true'/>"
"<preset name='A5' label='148mm x 210mm' min='148' max='210' selectable='true'/>"
"<preset name='A6' label='105mm x 148mm' min='105' max='148' />"
"<preset name='A7' label='74mm x 105mm' min='74' max='105' />"
"<preset name='A8' label='52mm x 74mm' min='52' max='74' />"
"<preset name='A9' label='37mm x 52mm' min='37' max='52' />"
"<preset name='A10' label='26mm x 37mm' min='26' max='37' />"
"<preset name='B0' label='1000mm x 1414mm' min='1000' max='1414' />"
"<preset name='B1' label='707mm x 1000mm' min='707' max='1000' />"
"<preset name='B2' label='500mm x 707mm' min='500' max='707' />"
"<preset name='B3' label='353mm x 500mm' min='353' max='500' />"
"<preset name='B4' label='250mm x 353mm' min='250' max='353' />"
"<preset name='B5' label='176mm x 250mm' min='176' max='250' />"
"<preset name='B6' label='125mm x 176mm' min='125' max='176' />"
"<preset name='B7' label='88mm x 125mm' min='88' max='125' />"
"<preset name='B8' label='62mm x 88mm' min='62' max='88' />"
"<preset name='B9' label='44mm x 62mm' min='44' max='62' />"
"<preset name='B10' label='31mm x 44mm' min='31' max='44' />"
"<preset name='C0' label='917mm x 1297mm' min='917' max='1297' />"
"<preset name='C1' label='648mm x 917mm' min='648' max='917' />"
"<preset name='C2' label='458mm x 648mm' min='458' max='648' />"
"<preset name='C3' label='324mm x 458mm' min='324' max='458' />"
"<preset name='C4' label='229mm x 324mm' min='229' max='324' />"
"<preset name='C5' label='162mm x 229mm' min='162' max='229' />"
"<preset name='C6' label='114mm x 162mm' min='114' max='162' />"
"<preset name='C7' label='81mm x 114mm' min='81' max='114' />"
"<preset name='C8' label='57mm x 81mm' min='57' max='81' />"
"<preset name='C9' label='40mm x 57mm' min='40' max='57' />"
"<preset name='C10' label='28mm x 40mm' min='28' max='40' />"
"<preset name='D1' label='545mm x 771mm' min='545' max='771' />"
"<preset name='D2' label='385mm x 545mm' min='385' max='545' />"
"<preset name='D3' label='272mm x 385mm' min='272' max='385' />"
"<preset name='D4' label='192mm x 272mm' min='192' max='272' />"
"<preset name='D5' label='136mm x 192mm' min='136' max='192' />"
"<preset name='D6' label='96mm x 136mm' min='96' max='136' />"
"<preset name='D7' label='68mm x 96mm' min='68' max='96' />"
"<preset name='E3' label='400mm x 560mm' min='400' max='560' />"
"<preset name='E4' label='280mm x 400mm' min='280' max='400' />"
"<preset name='E5' label='200mm x 280mm' min='200' max='280' />"
"<preset name='E6' label='140mm x 200mm' min='140' max='200' />"
"<preset name='Ledger/Tabloid' label='11in x 17in' min='11' max='17' unit='in' selectable='true'/>"
"<preset name='US Executive' label='7.25in x 10.5in' min='7.25' max='10.5' unit='in' selectable='true' icon='print_US_portrait'/>"
"<preset name='US Legal' label='8.5in x 14in' min='8.5' max='14' unit='in' selectable='true' icon='print_US_portrait'/>"
"<preset name='DL Envelope' label='220mm x 110mm' min='110' max='220' orientation='land' selectable='true' icon='envelope_landscape'/>"
"<preset name='US #10 Envelope' label='9.5in x 4.125in' min='4.125' max='9.5' unit='in' orientation='land' selectable='true' icon='envelope_landscape'/>"
"<preset name='Arch A' label='9in x 12in' min='9' max='12' unit='in' />"
"<preset name='Arch B' label='12in x 18in' min='12' max='18' unit='in' />"
"<preset name='Arch C' label='18in x 24in' min='18' max='24' unit='in' />"
"<preset name='Arch D' label='24in x 36in' min='24' max='36' unit='in' />"
"<preset name='Arch E' label='36in x 48in' min='36' max='48' unit='in' />"
"<preset name='Arch E1' label='30in x 42in' min='30' max='42' unit='in' />"
            "</template>"
        "</inkscape-extension>",
        new TemplatePaper());
    // clang-format on
}

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

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
