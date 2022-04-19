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

#include "template-video.h"

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

void TemplateVideo::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.video</id>"
            "<name>" N_("Video Sizes") "</name>"
            "<description>" N_("General sizes for videos") "</description>"
            "<category>" N_("Video") "</category>"

            "<param name='unit' gui-text='" N_("Unit") "' type='string'>px</param>"
            "<param name='width' gui-text='" N_("Width") "' type='float' min='1.0' max='100000.0'>100.0</param>"
            "<param name='height' gui-text='" N_("Height") "' type='float' min='1.0' max='100000.0'>100.0</param>"

            "<template icon='video_landscape' unit='px' priority='-50'>"

"<preset name='Video SD PAL' label='768px x 576px' width='768' height='576' selectable='true' />"
"<preset name='Video SD Widescreen / PAL' label='1024px x 576px' width='1024' height='576' selectable='true' />"
"<preset name='Video SD NTSC' label='544px x 480px' width='544' height='480' selectable='true' />"
"<preset name='Video SD Widescreen NTSC' label='872px x 486px' width='872' height='486' selectable='true' />"
"<preset name='Video HD 720p' label='1280px x 720px' width='1280' height='720' selectable='true' />"
"<preset name='Video HD 1080p' label='1920px x 1080px' width='1920' height='1080' selectable='true' />"
"<preset name='Video DCI 2k (Full Frame)' label='2048px x 1080px' width='2048' height='1080' selectable='true' />"
"<preset name='Video UHD 4k' label='3840px x 2160px' width='3840' height='2160' selectable='true' />"
"<preset name='Video DCI 4k (Full Frame)' label='4096px x 2160px' width='4096' height='2160' selectable='true' />"
"<preset name='Video UHD 8k' label='7680px x 4320px' width='7680' height='4320' selectable='true' />"

            "</template>"
        "</inkscape-extension>",
        new TemplateVideo());
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
