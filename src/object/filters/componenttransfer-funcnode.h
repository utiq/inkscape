// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SP_FECOMPONENTTRANSFER_FUNCNODE_H_SEEN
#define SP_FECOMPONENTTRANSFER_FUNCNODE_H_SEEN

/** \file
 * SVG <filter> implementation, see sp-filter.cpp.
 */
/*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Niko Kiirala <niko@kiirala.com>
 *   Felipe Corrêa da Silva Sanches <juca@members.fsf.org>
 *
 * Copyright (C) 2006,2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "object/sp-object.h"
#include "display/nr-filter-component-transfer.h"

class SPFeFuncNode
    : public SPObject
{
public:
    enum Channel
    {
        R, G, B, A
    };

    SPFeFuncNode(Channel channel)
        : channel(channel) {}

    Inkscape::Filters::FilterComponentTransferType type = Inkscape::Filters::COMPONENTTRANSFER_TYPE_IDENTITY;
    std::vector<double> tableValues;
    double slope = 1;
    double intercept = 0;
    double amplitude = 1;
    double exponent = 1;
    double offset = 0;
    Channel channel;

protected:
    void build(SPDocument *doc, Inkscape::XML::Node *repr) override;
	void release() override;
    void set(SPAttr key, char const *value) override;
};

MAKE_SP_OBJECT_DOWNCAST_FUNCTIONS(SP_FEFUNCNODE, SPFeFuncNode)
MAKE_SP_OBJECT_TYPECHECK_FUNCTIONS(SP_IS_FEFUNCNODE, SPFeFuncNode)

#endif // SP_FECOMPONENTTRANSFER_FUNCNODE_H_SEEN

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
