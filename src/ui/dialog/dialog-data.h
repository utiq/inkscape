// SPDX-License-Identifier: GPL-2.0-or-later

/** @file
 * @brief Basic dialog info.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2021 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/*
 * In an ideal world, this information would be in .ui files for each
 * dialog (the .ui file would describe a dialog wrapped by a notebook
 * tab). At the moment we create each dialog notebook tab on the fly
 * so we need a place to keep this information.
 */

#include <map>
#include <string>

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>

#include "ui/icon-names.h"  // INKSCAPE_ICON macro

enum class ScrollProvider {
    PROVIDE = 0,
    NOPROVIDE
};

class DialogData {
public:
    Glib::ustring label;
    Glib::ustring icon_name;
    enum Category { Basic, Advanced, Settings, Diagnostics, Other };
    Category category;
    ScrollProvider provide_scroll;
};

// dialog categories (used to group them in a dialog submenu)
static std::map<DialogData::Category, Glib::ustring> dialog_categories = {
    { DialogData::Basic,       _("Basic") },
    { DialogData::Advanced,    _("Advanced") },
    { DialogData::Settings,    _("Settings") },
    { DialogData::Diagnostics, _("Diagnostic") },
    { DialogData::Other,       _("Other") },
};

// Note the "AttrDialog" is now part of the "XMLDialog" and the "Style" dialog is part of the "Selectors" dialog.
// Also note that the "AttrDialog" does not correspond to SP_VERB_DIALOG_ATTR!!!!! (That would be the "ObjectAttributes" dialog.)

// short-term fix for missing dialog titles; map<ustring, T> exhibits a bug where "SVGFonts" entry cannot be found
// static std::map<Glib::ustring, DialogData> dialog_data =
static std::map<std::string, DialogData> dialog_data =
{
    // clang-format off
    {"AlignDistribute",    {N_("_Align and Distribute"), INKSCAPE_ICON("dialog-align-and-distribute"), DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"CloneTiler",         {N_("Create Tiled Clones"),   INKSCAPE_ICON("dialog-tile-clones"),          DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"DocumentProperties", {N_("_Document Properties"),  INKSCAPE_ICON("document-properties"),         DialogData::Settings,       ScrollProvider::NOPROVIDE }},
    {"Export",             {N_("_Export"),               INKSCAPE_ICON("document-export"),             DialogData::Basic,          ScrollProvider::PROVIDE   }},
    {"FillStroke",         {N_("_Fill and Stroke"),      INKSCAPE_ICON("dialog-fill-and-stroke"),      DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"FilterEffects",      {N_("Filter _Editor"),        INKSCAPE_ICON("dialog-filters"),              DialogData::Advanced,       ScrollProvider::NOPROVIDE }},
    {"Find",               {N_("_Find/Replace"),         INKSCAPE_ICON("edit-find"),                   DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"Glyphs",             {N_("_Unicode Characters"),   INKSCAPE_ICON("accessories-character-map"),   DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"IconPreview",        {N_("Icon Preview"),          INKSCAPE_ICON("dialog-icon-preview"),         DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"Input",              {N_("_Input Devices"),        INKSCAPE_ICON("dialog-input-devices"),        DialogData::Settings,       ScrollProvider::NOPROVIDE }},
    {"LivePathEffect",     {N_("Path E_ffects"),         INKSCAPE_ICON("dialog-path-effects"),         DialogData::Advanced,       ScrollProvider::NOPROVIDE }},
    {"Memory",             {N_("About _Memory"),         INKSCAPE_ICON("dialog-memory"),               DialogData::Diagnostics,    ScrollProvider::NOPROVIDE }},
    {"Messages",           {N_("_Messages"),             INKSCAPE_ICON("dialog-messages"),             DialogData::Diagnostics,    ScrollProvider::NOPROVIDE }},
    {"ObjectAttributes",   {N_("_Object attributes"),    INKSCAPE_ICON("dialog-object-properties"),    DialogData::Settings,       ScrollProvider::NOPROVIDE }},
    {"ObjectProperties",   {N_("_Object Properties"),    INKSCAPE_ICON("dialog-object-properties"),    DialogData::Settings,       ScrollProvider::NOPROVIDE }},
    {"Objects",            {N_("Layers and Object_s"),   INKSCAPE_ICON("dialog-objects"),              DialogData::Basic,          ScrollProvider::PROVIDE   }},
    {"PaintServers",       {N_("_Paint Servers"),        INKSCAPE_ICON("symbols"),                     DialogData::Advanced,       ScrollProvider::PROVIDE   }},
    {"Preferences",        {N_("P_references"),          INKSCAPE_ICON("preferences-system"),          DialogData::Settings,       ScrollProvider::PROVIDE   }},
    {"Selectors",          {N_("_Selectors and CSS"),    INKSCAPE_ICON("dialog-selectors"),            DialogData::Advanced,       ScrollProvider::PROVIDE   }},
    {"SVGFonts",           {N_("SVG Font Editor"),       INKSCAPE_ICON("dialog-svg-font"),             DialogData::Advanced,       ScrollProvider::NOPROVIDE }},
    {"Swatches",           {N_("S_watches"),             INKSCAPE_ICON("swatches"),                    DialogData::Basic,          ScrollProvider::PROVIDE   }},
    {"Symbols",            {N_("S_ymbols"),              INKSCAPE_ICON("symbols"),                     DialogData::Basic,          ScrollProvider::PROVIDE   }},
    {"Text",               {N_("_Text and Font"),        INKSCAPE_ICON("dialog-text-and-font"),        DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"Trace",              {N_("_Trace Bitmap"),         INKSCAPE_ICON("bitmap-trace"),                DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"Transform",          {N_("Transfor_m"),            INKSCAPE_ICON("dialog-transform"),            DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"UndoHistory",        {N_("Undo _History"),         INKSCAPE_ICON("edit-undo-history"),           DialogData::Basic,          ScrollProvider::NOPROVIDE }},
    {"XMLEditor",          {N_("_XML Editor"),           INKSCAPE_ICON("dialog-xml-editor"),           DialogData::Advanced,       ScrollProvider::NOPROVIDE }},
#if WITH_GSPELL
    {"Spellcheck",         {N_("Check Spellin_g"),       INKSCAPE_ICON("tools-check-spelling"),        DialogData::Basic,          ScrollProvider::NOPROVIDE }},
#endif
#if DEBUG
    {"Prototype",          {N_("Prototype"),             INKSCAPE_ICON("document-properties"),         DialogData::Other,          ScrollProvider::NOPROVIDE }},
#endif
    // clang-format on
};
