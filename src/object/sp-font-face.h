// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SP_FONTFACE_H
#define SEEN_SP_FONTFACE_H

#include <vector>

/*
 * SVG <font-face> element implementation
 *
 * Section 20.8.3 of the W3C SVG 1.1 spec
 * available at:
 * http://www.w3.org/TR/SVG/fonts.html#FontFaceElement
 *
 * Authors:
 *    Felipe C. da S. Sanches <juca@members.fsf.org>
 *
 * Copyright (C) 2008 Felipe C. da S. Sanches
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "sp-object.h"

enum FontFaceStyleType{
	SP_FONTFACE_STYLE_ALL,
	SP_FONTFACE_STYLE_NORMAL,
	SP_FONTFACE_STYLE_ITALIC,
	SP_FONTFACE_STYLE_OBLIQUE
};

enum FontFaceVariantType{
	SP_FONTFACE_VARIANT_NORMAL,
	SP_FONTFACE_VARIANT_SMALL_CAPS
};

enum FontFaceWeightType{
	SP_FONTFACE_WEIGHT_ALL,
	SP_FONTFACE_WEIGHT_NORMAL,
	SP_FONTFACE_WEIGHT_BOLD,
	SP_FONTFACE_WEIGHT_100,
	SP_FONTFACE_WEIGHT_200,
	SP_FONTFACE_WEIGHT_300,
	SP_FONTFACE_WEIGHT_400,
	SP_FONTFACE_WEIGHT_500,
	SP_FONTFACE_WEIGHT_600,
	SP_FONTFACE_WEIGHT_700,
	SP_FONTFACE_WEIGHT_800,
	SP_FONTFACE_WEIGHT_900
};

enum FontFaceStretchType{
	SP_FONTFACE_STRETCH_ALL,
	SP_FONTFACE_STRETCH_NORMAL,
	SP_FONTFACE_STRETCH_ULTRA_CONDENSED,
	SP_FONTFACE_STRETCH_EXTRA_CONDENSED,
	SP_FONTFACE_STRETCH_CONDENSED,
	SP_FONTFACE_STRETCH_SEMI_CONDENSED,
	SP_FONTFACE_STRETCH_SEMI_EXPANDED,
	SP_FONTFACE_STRETCH_EXPANDED,
	SP_FONTFACE_STRETCH_EXTRA_EXPANDED,
	SP_FONTFACE_STRETCH_ULTRA_EXPANDED
};

enum FontFaceUnicodeRangeType{
	FONTFACE_UNICODERANGE_FIXME_HERE,
};

class SPFontFace final : public SPObject {
public:
	SPFontFace();
	~SPFontFace() override;
    int tag() const override { return tag_of<decltype(*this)>; }

    char* font_family;
    std::vector<FontFaceStyleType> font_style;
    std::vector<FontFaceVariantType> font_variant;
    std::vector<FontFaceWeightType> font_weight;
    std::vector<FontFaceStretchType> font_stretch;
    char* font_size;
    std::vector<FontFaceUnicodeRangeType> unicode_range;
    double units_per_em;
    std::vector<int> panose_1;
    double stemv;
    double stemh;
    double slope;
    double cap_height;
    double x_height;
    double accent_height;
    double ascent;
    double descent;
    char* widths;
    char* bbox;
    double ideographic;
    double alphabetic;
    double mathematical;
    double hanging;
    double v_ideographic;
    double v_alphabetic;
    double v_mathematical;
    double v_hanging;
    double underline_position;
    double underline_thickness;
    double strikethrough_position;
    double strikethrough_thickness;
    double overline_position;
    double overline_thickness;

protected:
	void build(SPDocument* doc, Inkscape::XML::Node* repr) override;
	void release() override;

	void child_added(Inkscape::XML::Node* child, Inkscape::XML::Node* ref) override;
	void remove_child(Inkscape::XML::Node* child) override;

	void set(SPAttr key, const char* value) override;

	void update(SPCtx* ctx, unsigned int flags) override;

	Inkscape::XML::Node* write(Inkscape::XML::Document* doc, Inkscape::XML::Node* repr, unsigned int flags) override;
};

#endif // SEEN_SP_FONTFACE_H
