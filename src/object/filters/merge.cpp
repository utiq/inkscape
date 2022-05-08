// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feMerge> implementation.
 */
/*
 * Authors:
 *   hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "attributes.h"
#include "svg/svg.h"
#include "xml/repr.h"

#include "merge.h"
#include "mergenode.h"
#include "display/nr-filter.h"
#include "display/nr-filter-merge.h"

std::unique_ptr<Inkscape::Filters::FilterPrimitive> SPFeMerge::build_renderer() const
{
    auto merge = std::make_unique<Inkscape::Filters::FilterMerge>();
    build_renderer_common(merge.get());

    int in_nr = 0;

    for (auto const &input : children) {
        if (auto node = SP_FEMERGENODE(&input)) {
            merge->set_input(in_nr, node->input);
            in_nr++;
        }
    }

    return merge;
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
