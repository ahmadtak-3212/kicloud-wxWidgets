/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/dialog.cpp
// Purpose:     wxDialog implementation for WASM using JSPI for modal dialogs
// Author:      Robert Roebling, Vaclav Slavik (original univ)
//              Adam Hilss (WASM port), extended for suspending modals
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
//              (c) 2022 Adam Hilss
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:src/wasm/dialog.cpp (W3.0P; kicloud/docs/provenance.md)

// This file provides the complete wxDialog implementation for WASM builds.
// It replaces src/univ/dialog.cpp entirely for WASM because the standard
// wxWidgets event loop approach doesn't work in WASM (JavaScript is
// single-threaded and cannot truly block).
//
// ShowModal() registers a "modal" wait with the scheduler shim and
// JSPI-suspends the C++ stack on it; EndModal() resolves the innermost
// registered wait and the stack resumes (docs/features/async/17 S4). The
// top-level tick is the sole event dispatcher while the modal is open.
// KICLOUD: W3.0P (kicloud/TODO.md E2.4; W3.0P lens 2 of the K.11 retry): EndModal()
// resolves the dialog's OWN wait (a wxWasmNestedWait, wx/wasm/private/yieldwait.h), not
// the innermost one, and ShowModal() returns once every blocking call begun after it (a
// modal or a nested loop) has returned.

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#include "wx/dialog.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
    #include "wx/app.h"
#endif

#include "wx/evtloop.h"
#include "wx/modalhook.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <cstdio>

//-----------------------------------------------------------------------------
// wxDialog
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxDialog,wxDialogBase)
    EVT_BUTTON  (wxID_OK,       wxDialog::OnOK)
    EVT_BUTTON  (wxID_CANCEL,   wxDialog::OnCancel)
    EVT_BUTTON  (wxID_APPLY,    wxDialog::OnApply)
    EVT_CLOSE   (wxDialog::OnCloseWindow)
wxEND_EVENT_TABLE()

void wxDialog::Init()
{
    m_returnCode = 0;
    m_windowDisabler = NULL;
    m_eventLoop = NULL;
    m_isShowingModal = false;
    m_modalCallback = NULL;
    m_modalWait = NULL;   // KICLOUD: W3.0P (E2.4)
}

wxDialog::~wxDialog()
{
    // if the dialog is modal, this will end its event loop
    Show(false);

    delete m_eventLoop;
}

bool wxDialog::Create(wxWindow *parent,
                      wxWindowID id, const wxString &title,
                      const wxPoint &pos, const wxSize &size,
                      long style, const wxString &name)
{
    SetExtraStyle(GetExtraStyle() | wxTOPLEVEL_EX_DIALOG);

    // all dialogs should have tab traversal enabled
    style |= wxTAB_TRAVERSAL;

    return wxTopLevelWindow::Create(parent, id, title, pos, size, style, name);
}

void wxDialog::OnApply(wxCommandEvent &WXUNUSED(event))
{
    if ( Validate() )
        TransferDataFromWindow();
}

void wxDialog::OnCancel(wxCommandEvent &WXUNUSED(event))
{
    if ( IsModal() )
    {
        EndModal(wxID_CANCEL);
    }
    else
    {
        SetReturnCode(wxID_CANCEL);
        Show(false);
    }
}

void wxDialog::OnOK(wxCommandEvent &WXUNUSED(event))
{
    if ( Validate() && TransferDataFromWindow() )
    {
        if ( IsModal() )
        {
            EndModal(wxID_OK);
        }
        else
        {
            SetReturnCode(wxID_OK);
            Show(false);
        }
    }
}

void wxDialog::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
    // We'll send a Cancel message by default,
    // which may close the dialog.
    // Check for looping if the Cancel event handler calls Close().

    // Note that if a cancel button and handler aren't present in the dialog,
    // nothing will happen when you close the dialog via the window manager, or
    // via Close().
    // We wouldn't want to destroy the dialog by default, since the dialog may have been
    // created on the stack.
    // However, this does mean that calling dialog->Close() won't delete the dialog
    // unless the handler for wxID_CANCEL does so. So use Destroy() if you want to be
    // sure to destroy the dialog.
    // The default OnCancel (above) simply ends a modal dialog, and hides a modeless dialog.

    static wxList s_closing;

    if (s_closing.Member(this))
        return;   // no loops

    s_closing.Append(this);

    wxCommandEvent cancelEvent(wxEVT_BUTTON, wxID_CANCEL);
    cancelEvent.SetEventObject(this);
    GetEventHandler()->ProcessEvent(cancelEvent);
    s_closing.DeleteObject(this);
}

bool wxDialog::Show(bool show)
{
    if ( !show )
    {
        // if we had disabled other app windows, reenable them back now because
        // if they stay disabled Windows will activate another window (one
        // which is enabled, anyhow) and we will lose activation
        wxDELETE(m_windowDisabler);

        if ( IsModal() )
            EndModal(wxID_CANCEL);
    }

    if (show && CanDoLayoutAdaptation())
        DoLayoutAdaptation();

    // Native ports centre dialogs created at wxDefaultPosition (the window
    // manager / CW_USEDEFAULT does it for them); this port maps
    // wxDefaultPosition to a literal (0, 0) (nonownedwnd.cpp) with no platform
    // placement, dropping every unpositioned dialog in the top-left corner.
    // That was masked for years by DIALOG_SHIM::Show unconditionally
    // re-centring while wxDisplay::GetFromWindow() returned wxNOT_FOUND here;
    // with the display lookup fixed, provide the native default ourselves:
    // centre (on the display, matching the old visual behavior) any dialog
    // still at the default spot when first shown. A dialog whose position was
    // restored or set explicitly is not at (0, 0) and is left alone.
    if ( show && !IsShown() && GetPosition() == wxPoint(0, 0) )
        Centre();

    bool ret = wxDialogBase::Show(show);

    if ( show )
        InitDialog();

    return ret;
}

bool wxDialog::IsModal() const
{
    return m_isShowingModal;
}

// ----------------------------------------------------------------------------
// WASM-specific modal implementation using JSPI
// ----------------------------------------------------------------------------

int wxDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    if ( IsModal() )
    {
        wxFAIL_MSG( wxT("wxDialog:ShowModal called twice") );
        return GetReturnCode();
    }

    // Use the app's top level window as parent if none given unless explicitly
    // forbidden
    wxWindow * const parent = GetParentForModalDialog();
    if ( parent && parent != this )
    {
        m_parent = parent;
    }

    // The modal is a registered WAIT begun BEFORE Show(true) — an EndModal
    // running synchronously inside Show() resolves the wait early and
    // wxWasmYieldUntil returns immediately (doc 17 S4). No modal pump exists:
    // the top-level tick is the sole dispatcher (dialogs on the DOM port are
    // real HTML, so they render without a paint loop even pre-main-loop).
    // KICLOUD: W3.0P (E2.4; W3.0P lens 2): the wait is this call's wxWasmNestedWait, which
    // EndModal() ends; it returns once every blocking call begun after it has returned.
    wxWasmNestedWait wait("modal");

    // Token 0 = the scheduler refused the wait (dead or terminal instance):
    // never show a modal that no EndModal can ever dismiss. Cancel matches
    // the containment convention (the shim's error containment resolves
    // modals with wxID_CANCEL).
    if ( !wait.IsOk() )
    {
        SetReturnCode(wxID_CANCEL);
        return wxID_CANCEL;
    }

    m_modalWait = &wait;
    m_isShowingModal = true;
    Show(true);

    // Suspend the C++ stack until EndModal() ends the wait and every blocking
    // call begun after this one has returned.
    //
    // The opener's dispatch chain suspends here for the modal's whole
    // lifetime; the legitimate dispatcher keeps running meanwhile, so zero
    // the dispatch interlock for that whole span (manual save/restore:
    // wxWasmDispatchRestore centralizes the erased-guard reporting).
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    const int result = wait.Wait();
    wxWasmDispatchRestore(savedDispatchDepth, "ShowModal");

    // KICLOUD: W3.0P (E2.4): EndModal() forgets the wait before it ends it, and a dialog
    // destroyed while modal ends it too (~wxDialog -> Show(false) -> EndModal()), so after
    // an EndModal() the dialog may be gone and is not touched here. A wait that only the
    // shim's error containment ended leaves the dialog alive, still modal (as before) and
    // pointing at this wait, which goes out of scope now: forget it.
    if ( !wait.IsEndedByOwner() )
        m_modalWait = NULL;

    return result;
}

void wxDialog::ShowModal(std::function<void (int)> callback)
{
    ShowModal();

    m_modalCallback = callback;
}

void wxDialog::EndModal(int retCode)
{
    wxLogDebug(wxT("EndModal: %d"), retCode);

    SetReturnCode(retCode);

    if ( !IsModal() )
    {
        wxFAIL_MSG( wxT("wxDialog:EndModal called twice") );
        return;
    }

    m_isShowingModal = false;

    // KICLOUD: W3.0P (kicloud/TODO.md E2.4 "EndModal(): Resolve(token)"; W3.0P lens 2):
    // end THIS dialog's wait; PCBJam resolved the innermost registered modal wait, which
    // ended another dialog's ShowModal() when this one was not the innermost. ShowModal()
    // returns once every blocking call begun after it has returned (LIFO). A resolve
    // racing ahead of ShowModal's park pre-resolves the promise.
    if ( m_modalWait )
    {
        wxWasmNestedWait * const wait = m_modalWait;
        m_modalWait = NULL;
        wait->End(retCode);
    }

    Show(false);

    if (m_modalCallback)
    {
        auto callback = m_modalCallback;
        m_modalCallback = NULL;
        callback(retCode);
    }
}
