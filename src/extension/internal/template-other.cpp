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

#include "template-other.h"

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

/**
 * Return the width and height of the new page, the default is a fixed orientation.
 */
Geom::Point TemplateOther::get_template_size(Inkscape::Extension::Template *tmod) const
{
    auto size = tmod->get_param_float("size");
    return Geom::Point(size, size);
}

void TemplateOther::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">"
            "<id>org.inkscape.template.other</id>"
            "<name>" N_("Other Sizes") "</name>"
            "<description>" N_("General sizes for miscellaneous") "</description>"
            "<category>" N_("Other") "</category>"

            "<param name='unit' gui-text='" N_("Unit") "' type='string'>px</param>"
            "<param name='size' gui-text='" N_("Size") "' type='float' min='1.0' max='100000.0'>32.0</param>"

            "<template icon='icon_square' unit='px' priority='-10'>"

"<preset name='Icon 16x16' label='16px x 16px' size='16' selectable='true'/>"
"<preset name='Icon 32x32' label='32px x 32px' size='32' selectable='true'/>"
"<preset name='Icon 48x48' label='48px x 48px' size='48' selectable='true'/>"
"<preset name='Icon 120x120' label='120px x 120px' size='120' selectable='true'/>"
"<preset name='Icon 180x180' label='180px x 180px' size='180' selectable='true'/>"
"<preset name='Icon 512x512' label='512px x 512px' size='512' selectable='true'/>"

            "</template>"
        "</inkscape-extension>"


        ,
        new TemplateOther());
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
