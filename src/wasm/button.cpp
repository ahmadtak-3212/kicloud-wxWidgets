/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/button.cpp
// Purpose:     wxButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BUTTON

#include "wx/button.h"

#include "wx/dcscreen.h"
#include "wx/stockitem.h"
#include "wx/utils.h"         // KICLOUD: LOOK.6, wxMax
#include "wx/wasm/private/dom.h"

// RTTI for wxButton comes from wxIMPLEMENT_DYNAMIC_CLASS_XTI in
// src/common/btncmn.cpp (shared by all ports).

// KICLOUD: LOOK.6: defined in textctrl.cpp (a dialog's form control gets the roomy size)
extern bool wxWasmIsDialogFormControl(const wxWindow* win);

// KICLOUD: LOOK.6: the dashboard's form control height and a text button's side padding, px
static const int FORM_CONTROL_HEIGHT = 30;
static const int BUTTON_SIDE_PADDING = 12;

wxButton::wxButton()
{
}

wxButton::wxButton(wxWindow *parent, wxWindowID id,
                   const wxString& label,
                   const wxPoint& pos,
                   const wxSize& size, long style,
                   const wxValidator& validator,
                   const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxButton::Create(wxWindow *parent, wxWindowID id,
                      const wxString& label,
                      const wxPoint& pos,
                      const wxSize& size, long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    WasmCreateDomNode("button");

    // KICLOUD: LOOK.6: a dialog's button gets the roomy form size (wx-dom.js wxDomSetRoomy)
    if ( WasmGetDomId() && wxWasmIsDialogFormControl(this) )
        EM_ASM({ if (typeof wxDomSetRoomy === 'function') wxDomSetRoomy($0); }, WasmGetDomId());

    // Buttons created with a stock id and no label use the stock label
    // ("OK", "Cancel", ...), like every other port.
    wxString lbl = label;
    if (lbl.empty() && wxIsStockID(id))
        lbl = wxGetStockLabel(id);

    SetLabel(lbl);

    return true;
}

void wxButton::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Strip the mnemonic marker; browser buttons have no accelerators yet.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
}

void wxButton::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CLICK)
    {
        wxCommandEvent event(wxEVT_BUTTON, GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

wxWindow *wxButton::SetDefault()
{
    wxWindow *oldDefault = wxButtonBase::SetDefault();

    // KICLOUD: the default button is marked on its DOM element, which the page styles (B1.20).
    if (wxButton* old = wxDynamicCast(oldDefault, wxButton))
    {
        if (old != this && old->WasmGetDomId())
            wxDomSetDefault(old->WasmGetDomId(), false);
    }
    if (WasmGetDomId())
        wxDomSetDefault(WasmGetDomId(), true);

    return oldDefault;
}

/* static */
wxSize wxButtonBase::GetDefaultSize(wxWindow* win)
{
    // Standard dialog buttons should never collapse below a comfortable
    // click target; derived from the default GUI font like other ports.
    // KICLOUD: LOOK.6: with the dashboard's 12 px side padding and at least its 30 px form
    // control height (was 9 px and the text height + 12). Computed once, for the first window
    // asked about: wx caches it the same way on other ports.
    static wxSize size = wxDefaultSize;
    if (size == wxDefaultSize)
    {
        wxScreenDC dc;
        if (win)
            dc.SetFont(win->GetFont());
        const wxSize ext = dc.GetTextExtent(wxT("OK Cancel"));
        size.x = ext.x + 2 * BUTTON_SIDE_PADDING + 4;   // label margins + border
        size.y = wxMax(ext.y + 2 * 4 + 4, FORM_CONTROL_HEIGHT);
    }
    return size;
}

#endif // wxUSE_BUTTON
