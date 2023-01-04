// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A quick hack to use the Cairo renderer to write out a file.  This
 * then makes 'save as...' PDF.
 *
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *   Ulf Erikson <ulferikson@users.sf.net>
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2010 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cairo.h>
#ifdef CAIRO_HAS_PDF_SURFACE

#include "cairo-renderer-pdf-out.h"
#include "cairo-render-context.h"
#include "cairo-renderer.h"
#include "latex-text-renderer.h"
#include "path-chemistry.h"
#include <print.h>
#include "extension/system.h"
#include "extension/print.h"
#include "extension/db.h"
#include "extension/output.h"

#include "display/drawing.h"
#include "display/curve.h"

#include "object/sp-item.h"
#include "object/sp-root.h"
#include "object/sp-page.h"

#include <2geom/affine.h>
#include "page-manager.h"
#include "document.h"

#include "util/units.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

bool CairoRendererPdfOutput::check(Inkscape::Extension::Extension * /*module*/)
{
    bool result = true;

    if (nullptr == Inkscape::Extension::db.get("org.inkscape.output.pdf.cairorenderer")) {
        result = false;
    }

    return result;
}

// TODO: Make this function more generic so that it can do both PostScript and PDF; expose in the headers
static bool
pdf_render_document_to_file(SPDocument *doc, gchar const *filename, unsigned int level, PDFOptions flags,
                            int resolution, const gchar * const exportId, double bleedmargin_px)
{
    if (flags.text_to_path) {
        assert(!flags.text_to_latex);
        // Cairo's text-to-path method has numerical precision and font matching
        // issues (https://gitlab.com/inkscape/inkscape/-/issues/1979).
        // We get better results by using Inkscape's Object-to-Path method.
        Inkscape::convert_text_to_curves(doc);
    }

    doc->ensureUpToDate();

    SPRoot *root = doc->getRoot();
    SPItem *base = nullptr;

    bool pageBoundingBox = !flags.drawing_only;
    if (exportId && strcmp(exportId, "")) {
        // we want to export the given item only
        base = cast<SPItem>(doc->getObjectById(exportId));
        if (!base) {
            throw Inkscape::Extension::Output::export_id_not_found(exportId);
        }
        root->cropToObject(base); // TODO: This is inconsistent in CLI (should only happen for --export-id-only)
    } else {
        // we want to export the entire document from root
        base = root;
    }

    if (!base) {
        return false;
    }
    
    /* Create new drawing */
    Inkscape::Drawing drawing;
    unsigned dkey = SPItem::display_key_new(1);
    drawing.setRoot(root->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY));
    drawing.setExact();

    /* Create renderer and context */
    CairoRenderer *renderer = new CairoRenderer();
    CairoRenderContext *ctx = renderer->createContext();
    ctx->setPDFLevel(level);
    ctx->setTextToPath(flags.text_to_path);
    ctx->setOmitText(flags.text_to_latex);
    ctx->setFilterToBitmap(flags.rasterize_filters);
    ctx->setBitmapResolution(resolution);

    bool ret = ctx->setPdfTarget (filename);
    if(ret) {
        /* Render document */
        ret = renderer->setupDocument(ctx, doc, pageBoundingBox, bleedmargin_px, base);
        if (ret) {
            /* Render multiple pages */
            ret = renderer->renderPages(ctx, doc, flags.stretch_to_fit);
            ctx->finish();
        }
    }

    root->invoke_hide(dkey);

    renderer->destroyContext(ctx);
    delete renderer;

    return ret;
}

/**
    \brief  This function calls the output module with the filename
    \param  mod   unused
    \param  doc   Document to be saved
    \param  filename   Filename to save to (probably will end in .pdf)

    The most interesting thing that this function does is just attach
    an '>' on the front of the filename.  This is the syntax used to
    tell the printing system to save to file.
*/
void
CairoRendererPdfOutput::save(Inkscape::Extension::Output *mod, SPDocument *doc, gchar const *filename)
{
    Inkscape::Extension::Extension * ext;
    unsigned int ret;

    ext = Inkscape::Extension::db.get("org.inkscape.output.pdf.cairorenderer");
    if (ext == nullptr)
        return;

    int level = 0;
    try {
        const gchar *new_level = mod->get_param_optiongroup("PDFversion");
        if((new_level != nullptr) && (g_ascii_strcasecmp("PDF-1.5", new_level) == 0)) {
            level = 1;
        }
    }
    catch(...) {
        g_warning("Parameter <PDFversion> might not exist");
    }

    PDFOptions flags;
    flags.text_to_path = false;
    try {
        flags.text_to_path = (strcmp(mod->get_param_optiongroup("textToPath"), "paths") == 0);
    }
    catch(...) {
        g_warning("Parameter <textToPath> might not exist");
    }

    flags.text_to_latex = false;
    try {
        flags.text_to_latex = (strcmp(mod->get_param_optiongroup("textToPath"), "LaTeX") == 0);
    }
    catch(...) {
        g_warning("Parameter <textToLaTeX> might not exist");
    }

    flags.rasterize_filters = false;
    try {
        flags.rasterize_filters = mod->get_param_bool("blurToBitmap");
    }
    catch(...) {
        g_warning("Parameter <blurToBitmap> might not exist");
    }

    int new_bitmapResolution  = 72;
    try {
        new_bitmapResolution = mod->get_param_int("resolution");
    }
    catch(...) {
        g_warning("Parameter <resolution> might not exist");
    }

    const gchar *new_exportId = nullptr;
    try {
        new_exportId = mod->get_param_string("exportId");
    }
    catch(...) {
        g_warning("Parameter <exportId> might not exist");
    }

    flags.drawing_only = false;
    try {
        flags.drawing_only = (strcmp(ext->get_param_optiongroup("area"), "page") != 0);
    } catch(...) {
        g_warning("Parameter <area> might not exist");
    }

    flags.stretch_to_fit = false;
    try {
        flags.stretch_to_fit = (strcmp(ext->get_param_optiongroup("stretch"), "relative") == 0);
    } catch(...) {
        g_warning("Parameter <stretch> might not exist");
    }

    double new_bleedmargin_px = 0.;
    try {
        new_bleedmargin_px = Inkscape::Util::Quantity::convert(mod->get_param_float("bleed"), "mm", "px");
    }
    catch(...) {
        g_warning("Parameter <bleed> might not exist");
    }

    // Create PDF file
    {
        gchar * final_name;
        final_name = g_strdup_printf("> %s", filename);
        ret = pdf_render_document_to_file(doc, final_name, level, flags, new_bitmapResolution, new_exportId,
                                          new_bleedmargin_px);
        g_free(final_name);

        if (!ret)
            throw Inkscape::Extension::Output::save_failed();
    }

    // Create LaTeX file (if requested)
    if (flags.text_to_latex) {
        ret = latex_render_document_text_to_file(doc, filename, new_exportId, flags.drawing_only,
                                                 !flags.drawing_only, new_bleedmargin_px, true);

        if (!ret)
            throw Inkscape::Extension::Output::save_failed();
    }
}

#include "clear-n_.h"

/**
    \brief   A function allocate a copy of this function.

    This is the definition of Cairo PDF out.  This function just
    calls the extension system with the memory allocated XML that
    describes the data.
*/
void
CairoRendererPdfOutput::init ()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
            "<name>Portable Document Format</name>\n"
            "<id>org.inkscape.output.pdf.cairorenderer</id>\n"
            "<param name=\"PDFversion\" gui-text=\"" N_("Restrict to PDF version:") "\" type=\"optiongroup\" appearance=\"combo\" >\n"
                "<option value='PDF-1.5'>" N_("PDF 1.5") "</option>\n"
                "<option value='PDF-1.4'>" N_("PDF 1.4") "</option>\n"
            "</param>\n"
            "<param name=\"textToPath\" gui-text=\"" N_("Text output options:") "\" type=\"optiongroup\" appearance=\"radio\">\n"
                "<option value=\"embed\">" N_("Embed fonts") "</option>\n"
                "<option value=\"paths\">" N_("Convert text to paths") "</option>\n"
                "<option value=\"LaTeX\">" N_("Omit text in PDF and create LaTeX file") "</option>\n"
            "</param>\n"
            "<param name=\"blurToBitmap\" gui-text=\"" N_("Rasterize filter effects") "\" type=\"bool\">true</param>\n"
            "<param name=\"resolution\" gui-text=\"" N_("Resolution for rasterization (dpi):") "\" type=\"int\" min=\"1\" max=\"10000\">96</param>\n"
            "<spacer size=\"10\" />"
            "<param name=\"area\" gui-text=\"" N_("Output page size:") "\" type=\"optiongroup\" appearance=\"radio\" >\n"
                "<option value=\"page\">" N_("Use document's page size") "</option>"
                "<option value=\"drawing\">" N_("Use exported object's size") "</option>"
            "</param><spacer size=\"10\" />"
            "<param name=\"stretch\" gui-text=\"" N_("Drawing size:") "\" gui-description=\""
                N_("Whether the exported objects should maintain their relative sizes (compared to the "
                   "page size) or the absolute size in real-world units.")
                "\" type=\"optiongroup\" appearance=\"radio\" >\n"
                "<option value=\"relative\">" N_("Preserve size relative to page") "</option>"
                "<option value=\"absolute\">" N_("Preserve size in absolute units") "</option>"
            "</param><spacer size=\"10\" />"
            "<param name=\"bleed\" gui-text=\"" N_("Bleed/margin (mm):") "\" type=\"float\" min=\"-10000\" max=\"10000\">0</param>\n"
            "<param name=\"exportId\" gui-text=\"" N_("Limit export to the object with ID:") "\" type=\"string\"></param>\n"
            "<output is_exported='true' priority='5'>\n"
                "<extension>.pdf</extension>\n"
                "<mimetype>application/pdf</mimetype>\n"
                "<filetypename>Portable Document Format (*.pdf)</filetypename>\n"
                "<filetypetooltip>PDF File</filetypetooltip>\n"
            "</output>\n"
        "</inkscape-extension>", new CairoRendererPdfOutput());
    // clang-format on

    return;
}

} } }  /* namespace Inkscape, Extension, Internal */

#endif /* HAVE_CAIRO_PDF */
