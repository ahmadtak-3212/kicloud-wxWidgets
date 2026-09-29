/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/toplevel.h
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_TOPLEVEL_H_
#define _WX_WASM_TOPLEVEL_H_

#include "wx/vector.h"      // KICLOUD: page frames (B1.6d)
#include "wx/weakref.h"

//-----------------------------------------------------------------------------
// wxTopLevelWindowWasm
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxTopLevelWindowWasm : public wxTopLevelWindowBase
{
    typedef wxTopLevelWindowBase base_type;
public:
    // construction
    wxTopLevelWindowWasm() { Init(); }
    wxTopLevelWindowWasm(wxWindow *parent,
                         wxWindowID id,
                         const wxString& title,
                         const wxPoint& pos = wxDefaultPosition,
                         const wxSize& size = wxDefaultSize,
                         long style = wxDEFAULT_FRAME_STYLE,
                         const wxString& name = wxFrameNameStr)
    {
        Init();
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxFrameNameStr);

    virtual ~wxTopLevelWindowWasm();

    virtual wxPoint GetClientAreaOrigin() const wxOVERRIDE;

    // implement base class pure virtuals
    virtual void Maximize(bool WXUNUSED(maximize) = true) wxOVERRIDE { }
    virtual bool IsMaximized() const wxOVERRIDE { return false; }
    // KICLOUD: a page frame always fills the page, like the main frame (B1.6d)
    virtual bool IsAlwaysMaximized() const wxOVERRIDE { return IsMainFrame() || IsPageFrame(); }
    virtual void Iconize(bool WXUNUSED(iconize) = true) wxOVERRIDE { }
    virtual bool IsIconized() const wxOVERRIDE { return false; }
    virtual void Restore() wxOVERRIDE { }

    virtual void SetIcons(const wxIconBundle& icons) wxOVERRIDE;

    virtual void ShowWithoutActivating() wxOVERRIDE;
    virtual bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) wxOVERRIDE;
    virtual bool IsFullScreen() const wxOVERRIDE;

    virtual bool IsActive() wxOVERRIDE { return m_isActive; }

    virtual void SetTitle(const wxString &title) wxOVERRIDE;
    virtual wxString GetTitle() const wxOVERRIDE { return m_title; }

    virtual wxString GetCSSClassList() const wxOVERRIDE {
      return wxNonOwnedWindow::GetCSSClassList() + " toplevel" + (IsPageFrame() ? " page" : "");
    }

    // KICLOUD: page frames (wx/wasm/pageframes.h, B1.6d)
    bool IsPageFrame() const { return !m_pageKey.empty(); }
    const wxString& GetPageKey() const { return m_pageKey; }
    virtual bool Show(bool show = true) wxOVERRIDE;

protected:
    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

    virtual void DoScreenToClient(int *x, int *y) const wxOVERRIDE;
    virtual void DoClientToScreen(int *x, int *y) const wxOVERRIDE;

    virtual bool HasTitleBar() const;

    // True when this window should use a real DOM title bar (drag + close) instead
    // of the canvas-painted one — all non-main top-level windows (secondary frames
    // and dialogs); popups/tooltips (wxFRAME_NO_TASKBAR) are excluded.
    virtual bool UseDomTitleBar() const;

    // True when this window should get DOM edge-resize handles: a DOM-title-bar
    // window whose style carries wxRESIZE_BORDER (all frames + opted-in dialogs;
    // KiCad's DIALOG_SHIM sets it by default). Fixed dialogs stay non-resizable.
    virtual bool UseDomResize() const;

private:
    void Init();

    void SetActive(bool active) { m_isActive = active; }

    void DrawTitleText(wxDC& dc, const wxRect& rect);
    void DrawMinimizeButton(wxDC& dc, const wxRect& rect);

    void StartDrag(const wxPoint& pos);
    void EndDrag();
    void DragMove(const wxPoint& pos);

    void OnNcPaint(wxNcPaintEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);

    bool m_isActive;
    wxString m_title;
    // KICLOUD: the page frame key (empty: not a page frame), and the non-modal top-level
    // windows of this page frame that were hidden with it (B1.6d)
    wxString m_pageKey;
    wxVector<wxWeakRef<wxWindow> > m_hiddenWithPage;
    void NotifyPage(const char* event) const;

    wxRect m_minimizeButtonRect;

    bool m_isDragging;
    wxPoint m_dragOffset;

    friend class wxApp;

    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_WASM_TOPLEVEL_H_
