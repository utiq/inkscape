// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Factory for SPObject tree
 *
 * Authors:
 *   Markus Engel
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "sp-factory.h"

// primary
#include "box3d.h"
#include "box3d-side.h"
#include "color-profile.h"
#include "persp3d.h"
#include "sp-anchor.h"
#include "sp-clippath.h"
#include "sp-defs.h"
#include "sp-desc.h"
#include "sp-ellipse.h"
#include "sp-filter.h"
#include "sp-flowdiv.h"
#include "sp-flowregion.h"
#include "sp-flowtext.h"
#include "sp-font.h"
#include "sp-font-face.h"
#include "sp-glyph.h"
#include "sp-glyph-kerning.h"
#include "sp-guide.h"
#include "sp-hatch.h"
#include "sp-hatch-path.h"
#include "sp-image.h"
#include "sp-line.h"
#include "sp-linear-gradient.h"
#include "sp-marker.h"
#include "sp-mask.h"
#include "sp-mesh-gradient.h"
#include "sp-mesh-patch.h"
#include "sp-mesh-row.h"
#include "sp-metadata.h"
#include "sp-missing-glyph.h"
#include "sp-namedview.h"
#include "sp-offset.h"
#include "sp-page.h"
#include "sp-path.h"
#include "sp-pattern.h"
#include "sp-polyline.h"
#include "sp-radial-gradient.h"
#include "sp-rect.h"
#include "sp-root.h"
#include "sp-script.h"
#include "sp-solid-color.h"
#include "sp-spiral.h"
#include "sp-star.h"
#include "sp-stop.h"
#include "sp-string.h"
#include "sp-style-elem.h"
#include "sp-switch.h"
#include "sp-symbol.h"
#include "sp-tag.h"
#include "sp-tag-use.h"
#include "sp-text.h"
#include "sp-textpath.h"
#include "sp-title.h"
#include "sp-tref.h"
#include "sp-tspan.h"
#include "sp-use.h"
#include "live_effects/lpeobject.h"

// filters
#include "filters/blend.h"
#include "filters/colormatrix.h"
#include "filters/componenttransfer.h"
#include "filters/componenttransfer-funcnode.h"
#include "filters/composite.h"
#include "filters/convolvematrix.h"
#include "filters/diffuselighting.h"
#include "filters/displacementmap.h"
#include "filters/distantlight.h"
#include "filters/flood.h"
#include "filters/gaussian-blur.h"
#include "filters/image.h"
#include "filters/merge.h"
#include "filters/mergenode.h"
#include "filters/morphology.h"
#include "filters/offset.h"
#include "filters/pointlight.h"
#include "filters/specularlighting.h"
#include "filters/spotlight.h"
#include "filters/tile.h"
#include "filters/turbulence.h"

#include <functional>
#include <unordered_map>

namespace {

class Factory
{
    std::unordered_map<std::string, std::function<SPObject*()>> map;

public:
    Factory()
    {
        // primary
        map.emplace("inkscape:box3d", [] { return new SPBox3D; });
        map.emplace("inkscape:box3dside", [] { return new Box3DSide; });
        map.emplace("svg:color-profile", [] { return new Inkscape::ColorProfile; });
        map.emplace("inkscape:persp3d", [] { return new Persp3D; });
        map.emplace("svg:a", [] { return new SPAnchor; });
        map.emplace("svg:clipPath", [] { return new SPClipPath; });
        map.emplace("svg:defs", [] { return new SPDefs; });
        map.emplace("svg:desc", [] { return new SPDesc; });
        map.emplace("svg:ellipse", [] {
            auto e = new SPGenericEllipse;
            e->type = SP_GENERIC_ELLIPSE_ELLIPSE;
            return e;
        });
        map.emplace("svg:circle", [] {
            auto c = new SPGenericEllipse;
            c->type = SP_GENERIC_ELLIPSE_CIRCLE;
            return c;
        });
        map.emplace("arc", [] {
            auto a = new SPGenericEllipse;
            a->type = SP_GENERIC_ELLIPSE_ARC;
            return a;
        });
        map.emplace("svg:filter", [] { return new SPFilter; });
        map.emplace("svg:flowDiv", [] { return new SPFlowdiv; });
        map.emplace("svg:flowSpan", [] { return new SPFlowtspan; });
        map.emplace("svg:flowPara", [] { return new SPFlowpara; });
        map.emplace("svg:flowLine", [] { return new SPFlowline; });
        map.emplace("svg:flowRegionBreak", [] { return new SPFlowregionbreak; });
        map.emplace("svg:flowRegion", [] { return new SPFlowregion; });
        map.emplace("svg:flowRegionExclude", [] { return new SPFlowregionExclude; });
        map.emplace("svg:flowRoot", [] { return new SPFlowtext; });
        map.emplace("svg:font", [] { return new SPFont; });
        map.emplace("svg:font-face", [] { return new SPFontFace; });
        map.emplace("svg:glyph", [] { return new SPGlyph; });
        map.emplace("svg:hkern", [] { return new SPHkern; });
        map.emplace("svg:vkern", [] { return new SPVkern; });
        map.emplace("sodipodi:guide", [] { return new SPGuide; });
        map.emplace("inkscape:page", [] { return new SPPage; });
        map.emplace("svg:hatch", [] { return new SPHatch; });
        map.emplace("svg:hatchpath", [] { return new SPHatchPath; });
        map.emplace("svg:hatchPath", [] {
            std::cerr << "Warning: <hatchPath> has been renamed <hatchpath>" << std::endl;
            return new SPHatchPath;
        });
        map.emplace("svg:image", [] { return new SPImage; });
        map.emplace("svg:g", [] { return new SPGroup; });
        map.emplace("svg:line", [] { return new SPLine; });
        map.emplace("svg:linearGradient", [] { return new SPLinearGradient; });
        map.emplace("svg:marker", [] { return new SPMarker; });
        map.emplace("svg:mask", [] { return new SPMask; });
        map.emplace("svg:mesh", [] { // SVG 2 old
             std::cerr << "Warning: <mesh> has been renamed <meshgradient>." << std::endl;
             std::cerr << "Warning: <mesh> has been repurposed as a shape that tightly wraps a <meshgradient>." << std::endl;
             return new SPMeshGradient;
        });
        map.emplace("svg:meshGradient", [] { // SVG 2 old
             std::cerr << "Warning: <meshGradient> has been renamed <meshgradient>" << std::endl;
             return new SPMeshGradient;
        });
        map.emplace("svg:meshgradient", [] { // SVG 2
             return new SPMeshGradient;
        });
        map.emplace("svg:meshPatch", [] {
             std::cerr << "Warning: <meshPatch> and <meshRow> have been renamed <meshpatch> and <meshrow>" << std::endl;
             return new SPMeshpatch;
        });
        map.emplace("svg:meshpatch", [] { return new SPMeshpatch; });
        map.emplace("svg:meshRow", [] { return new SPMeshrow; });
        map.emplace("svg:meshrow", [] { return new SPMeshrow; });
        map.emplace("svg:metadata", [] { return new SPMetadata; });
        map.emplace("svg:missing-glyph", [] { return new SPMissingGlyph; });
        map.emplace("sodipodi:namedview", [] { return new SPNamedView; });
        map.emplace("inkscape:offset", [] { return new SPOffset; });
        map.emplace("svg:path", [] { return new SPPath; });
        map.emplace("svg:pattern", [] { return new SPPattern; });
        map.emplace("svg:polygon", [] { return new SPPolygon; });
        map.emplace("svg:polyline", [] { return new SPPolyLine; });
        map.emplace("svg:radialGradient", [] { return new SPRadialGradient; });
        map.emplace("svg:rect", [] { return new SPRect; });
        map.emplace("rect", [] { return new SPRect; } ); // LPE rect;
        map.emplace("svg:svg", [] { return new SPRoot; });
        map.emplace("svg:script", [] { return new SPScript; });
        map.emplace("svg:solidColor", [] {
            std::cerr << "Warning: <solidColor> has been renamed <solidcolor>" << std::endl;
            return new SPSolidColor;
        });
        map.emplace("svg:solidColor", [] {
            std::cerr << "Warning: <solidColor> has been renamed <solidcolor>" << std::endl;
            return new SPSolidColor;
        });
        map.emplace("svg:solidcolor", [] { return new SPSolidColor; });
        map.emplace("spiral", [] { return new SPSpiral; });
        map.emplace("star", [] { return new SPStar; });
        map.emplace("svg:stop", [] { return new SPStop; });
        map.emplace("string", [] { return new SPString; });
        map.emplace("svg:style", [] { return new SPStyleElem; });
        map.emplace("svg:switch", [] { return new SPSwitch; });
        map.emplace("svg:symbol", [] { return new SPSymbol; });
        map.emplace("inkscape:tag", [] { return new SPTag; });
        map.emplace("inkscape:tagref", [] { return new SPTagUse; });
        map.emplace("svg:text", [] { return new SPText; });
        map.emplace("svg:title", [] { return new SPTitle; });
        map.emplace("svg:tref", [] { return new SPTRef; });
        map.emplace("svg:tspan", [] { return new SPTSpan; });
        map.emplace("svg:textPath", [] { return new SPTextPath; });
        map.emplace("svg:use", [] { return new SPUse; });
        map.emplace("inkscape:path-effect", [] { return new LivePathEffectObject; });

        // filters
        map.emplace("svg:feBlend", [] { return new SPFeBlend; });
        map.emplace("svg:feColorMatrix", [] { return new SPFeColorMatrix; });
        map.emplace("svg:feComponentTransfer", [] { return new SPFeComponentTransfer; });
        map.emplace("svg:feFuncR", [] { return new SPFeFuncNode(SPFeFuncNode::R); });
        map.emplace("svg:feFuncG", [] { return new SPFeFuncNode(SPFeFuncNode::G); });
        map.emplace("svg:feFuncB", [] { return new SPFeFuncNode(SPFeFuncNode::B); });
        map.emplace("svg:feFuncA", [] { return new SPFeFuncNode(SPFeFuncNode::A); });
        map.emplace("svg:feComposite", [] { return new SPFeComposite; });
        map.emplace("svg:feConvolveMatrix", [] { return new SPFeConvolveMatrix; });
        map.emplace("svg:feDiffuseLighting", [] { return new SPFeDiffuseLighting; });
        map.emplace("svg:feDisplacementMap", [] { return new SPFeDisplacementMap; });
        map.emplace("svg:feDistantLight", [] { return new SPFeDistantLight; });
        map.emplace("svg:feFlood", [] { return new SPFeFlood; });
        map.emplace("svg:feGaussianBlur", [] { return new SPGaussianBlur; });
        map.emplace("svg:feImage", [] { return new SPFeImage; });
        map.emplace("svg:feMerge", [] { return new SPFeMerge; });
        map.emplace("svg:feMergeNode", [] { return new SPFeMergeNode; });
        map.emplace("svg:feMorphology", [] { return new SPFeMorphology; });
        map.emplace("svg:feOffset", [] { return new SPFeOffset; });
        map.emplace("svg:fePointLight", [] { return new SPFePointLight; });
        map.emplace("svg:feSpecularLighting", [] { return new SPFeSpecularLighting; });
        map.emplace("svg:feSpotLight", [] { return new SPFeSpotLight; });
        map.emplace("svg:feTile", [] { return new SPFeTile; });
        map.emplace("svg:feTurbulence", [] { return new SPFeTurbulence; });
        map.emplace("inkscape:grid", [] { return new SPObject; }); // TODO wtf

        // ignore
        map.emplace("rdf:RDF", [] { return nullptr; } ); // no SP node yet
        map.emplace("inkscape:clipboard", [] { return nullptr; } ); // SP node not necessary
        map.emplace("inkscape:templateinfo", [] { return nullptr; } ); // metadata for templates
        map.emplace("inkscape:_templateinfo", [] { return nullptr; } ); // metadata for templates
        map.emplace("", [] { return nullptr; } ); // comments
    }

    SPObject *create(std::string const &id)
    {
        auto it = map.find(id);

        if (it == map.end()) {
            std::cerr << "WARNING: unknown type: " << id << std::endl;
            return nullptr;
        }

        return it->second();
    }
};

} // namespace

SPObject *SPFactory::createObject(std::string const &id)
{
    static Factory factory;
    return factory.create(id);
}

std::string NodeTraits::get_type_string(Inkscape::XML::Node const &node)
{
    std::string name;

    switch (node.type()) {
    case Inkscape::XML::NodeType::TEXT_NODE:
        name = "string";
        break;

    case Inkscape::XML::NodeType::ELEMENT_NODE: {
        auto sptype = node.attribute("sodipodi:type");
        name = sptype ? sptype : node.name();
        break;
    }
    default:
        name = "";
        break;
    }

    return name;
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
