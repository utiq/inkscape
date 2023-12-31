// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *
 * Copyright (C) 2005,2007-2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_EXTENSION_DIALOG_H__
#define INKSCAPE_EXTENSION_DIALOG_H__

#include <memory>
#include <gtkmm/dialog.h>
#include <glibmm/value.h>
#include <glibmm/ustring.h>

namespace Gtk {
class CheckButton;
class Widget;
} // namespace Gtk;

namespace Inkscape::Extension {

class Effect;
class ExecutionEnv;
class InxParameter;

/** \brief  A class to represent the preferences for an extension */
class PrefDialog : public Gtk::Dialog {
    /** \brief  Name of the extension */
    Glib::ustring _name;

    /** \brief  A pointer to the OK button */
    Gtk::Button *_button_ok;
    /** \brief  A pointer to the CANCEL button */
    Gtk::Button *_button_cancel;

    /** \brief  Button to control live preview */
    Gtk::Widget *_button_preview;
    /** \brief  Checkbox for the preview */
    Gtk::CheckButton *_checkbox_preview;

    /** \brief  Parameter to control live preview */
    std::unique_ptr<InxParameter> _param_preview;

    /** \brief  XML to define the live effects parameter on the dialog */
    static const char * live_param_xml;

    /** \brief Signal that the user is changing the live effect state */
    sigc::signal<void ()> _signal_preview;
    /** \brief Signal that one of the parameters change */
    sigc::signal<void ()> _signal_param_change;

    /** \brief  If this is the preferences for an effect, the effect
                that we're working with. */
    Effect *_effect;

    /** \brief  If we're executing in preview mode here is the execution
                environment for the effect. */
    std::unique_ptr<ExecutionEnv> _exEnv;

    /** \brief  The timer used to make it so that parameters don't respond
                directly and allows for changes. */
    sigc::connection _timersig;

    void preview_toggle();
    void param_change();
    bool param_timer_expire();
    void on_response (int signal) override;

public:
    PrefDialog (Glib::ustring name,
                Gtk::Widget * controls = nullptr,
                Effect * effect = nullptr);
    ~PrefDialog () override;
};


} // namespace Inkscape::Extension

#endif /* INKSCAPE_EXTENSION_DIALOG_H__ */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
