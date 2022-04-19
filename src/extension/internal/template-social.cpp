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

#include "template-social.h"

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

void TemplateSocial::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.social</id>"
            "<name>" N_("Social Sizes") "</name>"
            "<description>" N_("General sizes for social media") "</description>"
            "<category>" N_("Social") "</category>"

            "<param name='unit' gui-text='" N_("Unit") "' type='string'>px</param>"
            "<param name='width' gui-text='" N_("Width") "' type='float' min='1.0' max='100000.0'>100.0</param>"
            "<param name='height' gui-text='" N_("Height") "' type='float' min='1.0' max='100000.0'>100.0</param>"

            "<template icon='social_landscape' unit='px' priority='-30'>"

"<preset name='Facebook cover photo' label='820px x 462px' height='462' width='820' selectable='true'/>"
"<preset name='Facebook event image' label='1920px x 1080px' height='1080' width='1920' selectable='true'/>"
"<preset name='Facebook image post' label='1200px x 630px' height='630' width='1200' selectable='true'/>"
"<preset name='Facebook link image' label='1200px x 630px' height='630' width='1200' selectable='true'/>"
"<preset name='Facebook profile picture' label='180px x 180px' height='180' width='180' selectable='true' icon='social_square'/>"
"<preset name='Facebook video' label='1280px x 720px' height='720' width='1280' selectable='true'/>"
"<preset name='Instagram landscape' label='1080px x 608px' height='608' width='1080' selectable='true'/>"
"<preset name='Instagram portrait' label='1080px x 1350px' height='1350' width='1080' selectable='true' icon='social_portrait'/>"
"<preset name='Instagram square' label='1080px x 1080px' height='1080' width='1080' selectable='true' icon='social_square'/>"
"<preset name='LinkedIn business banner image' label='646px x 220px' height='220' width='646' selectable='true'/>"
"<preset name='LinkedIn company logo' label='300px x 300px' height='300' width='300' selectable='true' icon='social_square'/>"
"<preset name='LinkedIn cover photo' label='1536px x 768px' height='768' width='1536' selectable='true'/>"
"<preset name='LinkedIn dynamic ad' label='100px x 100px' height='100' width='100' selectable='true' icon='social_square'/>"
"<preset name='LinkedIn hero image' label='1128px x 376px' height='376' width='1128' selectable='true'/>"
"<preset name='LinkedIn sponsored content image' label='1200px x 627px' height='627' width='1200' selectable='true'/>"
"<preset name='Snapchat advertisement' label='1080px x 1920px' height='1920' width='1080' selectable='true' icon='social_portrait'/>"
"<preset name='Twitter card image' label='1200px x 628px' height='628' width='1200' selectable='true'/>"
"<preset name='Twitter header' label='1500px x 500px' height='500' width='1500' selectable='true'/>"
"<preset name='Twitter post image' label='1024px x 512px' height='512' width='1024' selectable='true'/>"
"<preset name='Twitter profile picture' label='400px x 400px' height='400' width='400' selectable='true' icon='social_square'/>"
"<preset name='Twitter video landscape' label='1280px x 720px' height='720' width='1280' selectable='true'/>"
"<preset name='Twitter video portrait' label='720px x 1280px' height='1280' width='720' selectable='true' icon='social_portrait'/>"
"<preset name='Twitter video square' label='720px x 720px' height='720' width='720' selectable='true' icon='social_square'/>"

            "</template>"
        "</inkscape-extension>",
        new TemplateSocial());
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
