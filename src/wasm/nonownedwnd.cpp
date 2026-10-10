/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/nonownedwnd.cpp
// Purpose:     wxNonOwnedWindow implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/nonownedwnd.h"
#include "wx/wasm/private.h"
#include "wx/wasm/private/display.h"
#include "wx/wasm/pageframes.h"     // KICLOUD: S4.8 (wxWasmWindowSlot)

#include <emscripten.h>

void wxNonOwnedWindow::Init()
{
    m_cssId = wxID_NONE;
}

wxNonOwnedWindow::~wxNonOwnedWindow()
{
    if (m_cssId != wxID_NONE)
    {
        EM_ASM({
            destroyWindow($0);
        }, m_cssId);
    }
}

bool wxNonOwnedWindow::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxString& name)
{
    wxString classList = GetCSSClassList();

    m_cssId = EM_ASM_INT({
        return createWindow(-1, true, $0, UTF8ToString($1));
    }, m_isShown, static_cast<const char*>(classList.utf8_str()));

    int x = pos.x;
    int y = pos.y;

    if (x == wxDefaultCoord)
    {
        x = 0;
    }
    if (y == wxDefaultCoord)
    {
        y = 0;
    }

    int width = WidthDefault(size.x);
    int height = HeightDefault(size.y);

    if (!wxNonOwnedWindowBase::Create(parent, id, wxPoint(x, y), wxSize(width, height), style, name))
    {
        wxFAIL_MSG(wxT("wxTopLevelWindowWasm creation failed"));
        return false;
    }

    wxTopLevelWindows.Append(this);

    // KICLOUD: P3-K a top-level window has no minimum size of its own. wxWindowBase::CreateBase
    // makes the creation size the minimum of every window not yet in wxTopLevelWindows, and this
    // port appends the window only after that: every dialog got the display size as its minimum,
    // so Fit() could not shrink it to its content (Add Design Variant filled the page).
    SetMinSize(wxDefaultSize);

    // The first TLW is the browser main window — the page itself — which
    // has no hidden state. Apps need not ever call Show(true) on it
    // (KiCad doesn't), so the visibility sync must not treat it as a
    // hidden root: children created before an explicit Show() would be
    // stuck at display:none.
    if (IsMainFrame())
        m_isShown = true;

    //printf("CreateWindow: %d\n", m_cssId);

    return true;
}

void wxNonOwnedWindow::SetSizer(wxSizer *sizer, bool deleteOld)
{
    wxWindow::SetSizer(sizer, deleteOld);
    Layout();
}

void wxNonOwnedWindow::DoSetSize(int x, int y,
                                 int width, int height,
                                 int sizeFlags)
{
    //printf("DoSetSize: %d, %d, %d, %d, %d\n", GetCSSId(), x, y, width, height);

    // KICLOUD: P3-K a top-level window keeps its minimum and maximum size, as on every native
    // port (the window manager enforces them). KiCad's DIALOG_SHIM::Show re-applies the dialog's
    // initial size (Preferences: 980 x 560) after PAGED_DIALOG::onPageChanged had grown the dialog
    // to its page's minimum; without the clamp the page was laid out too short (Preferences >
    // Common overlapped its controls and clipped the last rows).
    if (!IsMainFrame())
    {
        if (width != wxDefaultCoord)
        {
            if (GetMinWidth() != wxDefaultCoord && width < GetMinWidth()) width = GetMinWidth();
            if (GetMaxWidth() != wxDefaultCoord && width > GetMaxWidth()) width = GetMaxWidth();
        }
        if (height != wxDefaultCoord)
        {
            if (GetMinHeight() != wxDefaultCoord && height < GetMinHeight()) height = GetMinHeight();
            if (GetMaxHeight() != wxDefaultCoord && height > GetMaxHeight()) height = GetMaxHeight();
        }
    }

    wxRect oldRect = GetScreenRect();
    wxNonOwnedWindowBase::DoSetSize(x, y, width, height, sizeFlags);
    wxRect newRect = GetScreenRect();

    if (newRect != oldRect)
    {
        // KICLOUD: S4.8 (docs/patches.md, wx/wasm/pageframes.h): with the slot (attached
        // window) the window is drawn in; wx.js moves its element into that slot's document
        EM_ASM({
            return setWindowRect($0, $1, $2, $3, $4, $5);
        }, GetCSSId(), newRect.x, newRect.y, newRect.width, newRect.height,
           wxWasmWindowSlot(this));
    }
}

bool wxNonOwnedWindow::Show(bool show)
{
    bool ret = wxNonOwnedWindowBase::Show(show);
    if (ret && !IsMainFrame())
    {
        EM_ASM({
            setWindowVisibility($0, $1);
        }, GetCSSId(), show);

        // When showing a popup window, raise it to ensure it appears above
        // other windows including GL canvases
        if (show)
        {
            Raise();
        }
    }

    return ret;
}

void wxNonOwnedWindow::Raise()
{
    EM_ASM({
        raiseWindow($0);
    }, GetCSSId());

    for (wxWindowList::iterator windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxWindow *window = *windowIter;
        if (window->GetParent() != NULL &&
            window->GetParent()->GetTopLevelWindow() == this)
        {
            window->Raise();
        }
    }
}

void wxNonOwnedWindow::Lower()
{
    for (wxWindowList::iterator windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxWindow *window = *windowIter;
        if (window->GetParent() != NULL &&
            window->GetParent()->GetTopLevelWindow() == this)
        {
            window->Lower();
        }
    }

    EM_ASM({
        lowerWindow($0);
    }, GetCSSId());
}

void wxNonOwnedWindow::HandlePaintRequests()
{
    if (NeedsPaint())
    {
        DoPaint(false);
    }
}

bool wxNonOwnedWindow::IsMainFrame() const
{
    return !wxTopLevelWindows.IsEmpty() && this == wxTopLevelWindows[0];
}
