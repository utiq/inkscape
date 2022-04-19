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

#include "template-screen.h"

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

void TemplateScreen::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.digital</id>"
            "<name>" N_("Screen Sizes") "</name>"
            "<description>" N_("General sizes for screens") "</description>"
            "<category>" N_("Screen") "</category>"

            "<param name='unit' gui-text='" N_("Unit") "' type='string'>px</param>"
            "<param name='width' gui-text='" N_("Width") "' type='float' min='1.0' max='100000.0'>100.0</param>"
            "<param name='height' gui-text='" N_("Height") "' type='float' min='1.0' max='100000.0'>100.0</param>"

            "<template icon='desktop_hd_landscape' unit='px' priority='-20'>"

"<preset name='Desktop 1080p' label='1920px x 1080px' height='1080' width='1920' selectable='true'/>"
"<preset name='Desktop 2K' label='2560px x 1440px' height='1440' width='2560' selectable='true'/>"
"<preset name='Desktop 4K' label='3840px x 2160px' height='2160' width='3840' selectable='true'/>"
"<preset name='Desktop 720p' label='1366px x 768px' height='768' width='1366' selectable='true'/>"
"<preset name='Desktop SD' label='1024px x 768px' height='768' width='1024' selectable='true' icon='desktop_landscape'/>"
"<preset name='iPhone 5' label='640px x 1136px' height='1136' width='640' selectable='true' icon='mobile_portrait'/>"
"<preset name='iPhone X' label='1125px x 2436px' height='2436' width='1125' selectable='true' icon='mobile_portrait'/>"
"<preset name='Mobile-smallest' label='360px x 640px' height='640' width='360' selectable='true' icon='mobile_portrait'/>"
"<preset name='iPad Pro' label='2388px x 1668px' height='1668' width='2388' selectable='true' icon='tablet_landscape'/>"
"<preset name='Tablet-smallest' label='1024px x 768px' height='768' width='1024' selectable='true' icon='tablet_landscape'/>"

            "</template>"
        "</inkscape-extension>",
        new TemplateScreen());
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
