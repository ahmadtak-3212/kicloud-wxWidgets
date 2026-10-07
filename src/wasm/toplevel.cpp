/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/toplevel.cpp
// Purpose:     wxTopLevelWindowWasm implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/dcclient.h"
#include "wx/frame.h"
#include "wx/settings.h"
#include "wx/toplevel.h"

#include "wx/wasm/private.h"
#include "wx/wasm/private/display.h"
#include "wx/wasm/pageframes.h"     // KICLOUD: B1.6d
#include "wx/dialog.h"
#include "wx/weakref.h"

#include <emscripten.h>
#include <emscripten/html5.h>

static const wxCoord TITLE_BAR_HEIGHT = 22;
static const wxColour TITLE_BAR_BACKGROUND_COLOUR(200, 200, 200);
static const wxColour TITLE_BAR_FOREGROUND_COLOUR(40, 40, 40);

static const wxCoord MINIMIZE_BUTTON_SIZE = 16;
static const wxCoord MINIMIZE_BUTTON_PADDING = 3;

// ----------------------------------------------------------------------------
// wxTopLevelWindowWasm
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxTopLevelWindowWasm, wxTopLevelWindowBase)
    EVT_NC_PAINT(wxTopLevelWindowWasm::OnNcPaint)
    EVT_LEFT_DOWN(wxTopLevelWindowWasm::OnMouseDown)
    EVT_LEFT_UP(wxTopLevelWindowWasm::OnMouseUp)
    EVT_MOTION(wxTopLevelWindowWasm::OnMotion)
wxEND_EVENT_TABLE()

// KICLOUD: page frames (wx/wasm/pageframes.h, docs/patches.md B1.6d)
static wxString gs_nextPageFrame;
static int gs_pageFramesEnabled = -1;

bool wxWasmPageFramesEnabled()
{
    if (gs_pageFramesEnabled < 0)
    {
        gs_pageFramesEnabled = EM_ASM_INT({
            return (typeof Module !== 'undefined' && Module.wxPageFrames) ? 1 : 0;
        });
    }

    return gs_pageFramesEnabled == 1;
}

void wxWasmSetNextPageFrame(const char* key)
{
    gs_nextPageFrame = (key && *key && wxWasmPageFramesEnabled()) ? wxString::FromUTF8(key)
                                                                   : wxString();
}

// ----------------------------------------------------------------------------
// KICLOUD: A14 (docs/patches.md): dialogs and floating windows stay on the page
// ----------------------------------------------------------------------------
//
// On a desktop, the window manager keeps a newly shown or moved window on the screen and never
// makes it narrower than its title. In the browser the page is the screen and this port is the
// window manager, so it does the same here. Without it, KiCad put dialogs back where they were
// last closed even when they had grown since (the PCB Print dialog shares its saved place with the
// shorter schematic one and opened with its Print button below a 900 px page), opened the drawing
// sheet editor's Design Inspector with its lower half below the page, and showed the Gerber
// viewer's D Codes dialog narrower than its own title ("D...").
//
// Every change goes through SetSize(), so wx's own idea of the window's place (used to hit-test
// canvas-drawn controls) and the DOM element (setWindowRect) always agree.

// The size of the page in wx screen coordinates: the main window's size, which is the area every
// top-level window is placed in. (0, 0) while the page has no layout (a hidden editor iframe).
static wxSize wxWasmPageSize()
{
    if (wxTheApp && wxTheApp->GetDisplay())
        return wxTheApp->GetDisplay()->GetScreenSize();

    return wxSize(0, 0);
}

// The width, in px, the window `cssId` needs so its DOM title bar shows its whole title: the
// title text's own width and padding, plus the rest of the bar (the close button and its margin),
// plus the window's own border. The text is measured as laid out (a Range round its contents), not
// as its box: the box stretches with the window, and the element may still have the size of the
// geometry being replaced, so its width would forbid making a window narrower. 0 when the window
// has no DOM title bar or is not on screen (display: none has no layout to measure). Reads the DOM
// only; changes nothing.
static int wxWasmTitleBarWidth(int cssId)
{
    return EM_ASM_INT({
        var win = document.getElementById('window-' + $0);
        var bar = win ? win.querySelector(':scope > .window-titlebar') : null;
        var text = bar ? bar.querySelector(':scope > .window-titlebar-text') : null;
        if (!text || !text.getClientRects().length) {
            return 0;
        }
        var px = function (v) { return parseFloat(v || '0') || 0; };
        var range = document.createRange();
        range.selectNodeContents(text);
        var ts = getComputedStyle(text);
        var need = range.getBoundingClientRect().width + px(ts.paddingLeft) + px(ts.paddingRight);
        for (var i = 0; i < bar.children.length; i++) {
            var child = bar.children[i];
            if (child !== text) {
                var cs = getComputedStyle(child);
                need += child.offsetWidth + px(cs.marginLeft) + px(cs.marginRight);
            }
        }
        // + 1: a title a fraction of a pixel wider than its box is already drawn with an ellipsis
        return Math.ceil(need + (win.offsetWidth - bar.clientWidth) + 1);
    }, cssId);
}

// Moves, and if needed resizes, the shown top-level window `win` so that it lies wholly on the
// page and is at least as wide as its title, as a desktop window manager does:
//   - at least as wide as its title (but never wider than the page);
//   - never larger than the page; a window that is larger is shrunk to it, and its sizers then lay
//     it out smaller (scrolling where KiCad's dialog scrolls). Its minimum size still wins: wx
//     keeps it (nonownedwnd.cpp), and the layout would overlap below it;
//   - its right and bottom edges on the page, then its left and top edges, so a window that is
//     still taller than the page (its minimum) keeps its title bar visible.
// Returns true when it changed the window's geometry, through SetSize(), which sends the window a
// complete size event of its own. Does nothing while the page has no size.
static bool wxWasmKeepOnPage(wxTopLevelWindowWasm* win)
{
    const wxSize page = wxWasmPageSize();
    if (page.x <= 0 || page.y <= 0)
        return false;

    const wxRect rect = win->GetRect();     // a top-level window's rect is in screen coordinates
    int width = rect.width;
    int height = rect.height;

    width = wxMax(width, wxWasmTitleBarWidth(win->GetCSSId()));
    width = wxMin(width, page.x);
    height = wxMin(height, page.y);

    // the size SetSize() will actually give the window (nonownedwnd.cpp keeps the minimum)
    const wxSize minSize = win->GetMinSize();
    if (minSize.x != wxDefaultCoord)
        width = wxMax(width, minSize.x);
    if (minSize.y != wxDefaultCoord)
        height = wxMax(height, minSize.y);

    int x = wxMax(wxMin(rect.x, page.x - width), 0);
    int y = wxMax(wxMin(rect.y, page.y - height), 0);

    if (x == rect.x && y == rect.y && width == rect.width && height == rect.height)
        return false;

    win->SetSize(x, y, width, height);
    return true;
}

bool wxTopLevelWindowWasm::Create(wxWindow *parent,
                                  wxWindowID id,
                                  const wxString& title,
                                  const wxPoint& pos,
                                  const wxSize& sizeOrig,
                                  long style,
                                  const wxString& name)
{
    // Handle default size like GTK/MSW ports do - resolve to display size
    // before passing to base class. This ensures GetClientSize() returns
    // reasonable values even before Show() is called.
    wxSize size(sizeOrig);

    // KICLOUD: the first top-level window created after wxWasmSetNextPageFrame() is that page
    // frame: it fills the page (B1.6d)
    m_pageKey = gs_nextPageFrame;
    gs_nextPageFrame.clear();
    wxPoint position(pos);

    if (IsPageFrame() && wxTheApp && wxTheApp->GetDisplay())
    {
        size = wxTheApp->GetDisplay()->GetScreenSize();
        position = wxPoint(0, 0);
    }

    if (!size.IsFullySpecified())
    {
        // Query display size directly from wxTheApp if available.
        // This is safer than calling GetDefaultSize() which goes through
        // wxDisplay and can crash if the display system isn't initialized yet.
        wxSize defaultSize(1280, 720);  // Reasonable fallback
        if (wxTheApp && wxTheApp->GetDisplay())
        {
            defaultSize = wxTheApp->GetDisplay()->GetScreenSize();
        }
        size.SetDefaults(defaultSize);
    }

    if (!wxTopLevelWindowBase::Create(parent, id, position, size, style, name))
    {
        wxFAIL_MSG(wxT("wxTopLevelWindowWasm creation failed"));
        return false;
    }

    SetTitle(title);

    // Non-main wxFrames get a real DOM title bar (drag handle + close "X")
    // instead of the canvas-painted one: a pointer-events:none canvas title bar
    // loses hit-testing to overlapping pointer-events:auto DOM controls from
    // another frame (e.g. the main editor's toolbar over the 3D viewer), so its
    // clicks never reach the #canvas mouse router. The DOM bar wins via normal
    // stacking. Created after SetTitle so the bar carries the current title.
    if (UseDomTitleBar())
    {
        EM_ASM({
            createWindowTitlebar($0, UTF8ToString($1), $2);
        }, GetCSSId(), static_cast<const char *>(title.utf8_str()), TITLE_BAR_HEIGHT);
    }

    // Resizable windows (wxRESIZE_BORDER) also get DOM edge-resize handles. Added
    // after the title bar so the side handles can start just below it (barHeight).
    if (UseDomResize())
    {
        EM_ASM({
            createWindowResizeHandles($0, $1);
        }, GetCSSId(), TITLE_BAR_HEIGHT);
    }

    // KICLOUD: A14: every move or resize of a shown dialog or floating window (KiCad restoring
    // its saved place, a drag of its title bar or edge, a Centre()) ends on the page. wxWindowWasm
    // sends a size event for a move too. Bound here, first, so the handlers that KiCad binds later
    // (DIALOG_SHIM::OnSize) run before it. When it corrects the geometry, its SetSize() has already
    // sent a complete size event for the corrected one (laying the window out at its final size),
    // so the event for the rejected geometry goes no further; otherwise it goes on as before.
    if (HasTitleBar())
    {
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
        {
            if (IsShown() && wxWasmKeepOnPage(this))
                return;

            event.Skip();
        });
    }

    return true;
}

void wxTopLevelWindowWasm::Init()
{
    m_isActive = false;
    m_minimizeButtonRect = wxRect(0, 0, MINIMIZE_BUTTON_SIZE, MINIMIZE_BUTTON_SIZE);
    m_isDragging = false;
}

wxTopLevelWindowWasm::~wxTopLevelWindowWasm()
{
    // Notify the host page when the application's main window is destroyed
    // (File->Quit or last close). A vetoed close (e.g. a cancelled
    // unsaved-changes prompt) never reaches destruction, so this only fires
    // for a real quit. Must run before ~wxTopLevelWindowBase, which clears
    // wxTheApp's top-window pointer. Child frames and dialogs are never the
    // app top window and don't notify.
    // IsMainFrame() (== wxTopLevelWindows[0], the first TLW ever created) rather
    // than GetTopWindow(): wx re-points the top window at whatever TLW is left,
    // so a transient frame dying mid-session used to look exactly like an app
    // quit — and the host acts on that by navigating the user out of the editor.
    // Observed for real 2026-08-03: a frame torn down during a heavy board load
    // silently ejected the user (docs/features/async/16 round 6).
    if (wxTheApp && IsMainFrame() && wxTheApp->GetTopWindow() == this)
    {
        EM_ASM({
            if (typeof window !== 'undefined'
                    && typeof window.wxAppTopWindowClosed === 'function')
            {
                window.wxAppTopWindowClosed();
            }
        });
    }

    // KICLOUD: a page frame closed: the page closes its tab (B1.6d)
    if (IsPageFrame())
        NotifyPage("closed");
}

// KICLOUD: a GL canvas is a DOM element of its own (not inside its window's element), shown
// by wxGLCanvas::Show from IsShownOnScreen(). Re-sync every GL canvas in `win`'s tree after its
// top-level window was shown or hidden. wxGLCanvas is found by name, as in
// wxWasmWindowHostsGLCanvas, so core does not link against the GL library (B1.6d).
static void wxWasmSyncGLCanvases(wxWindow* win, const wxClassInfo* cls)
{
    if (!win || !cls)
        return;

    if (win->IsKindOf(cls))
        win->Show(win->IsShown());

    for (wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst(); node;
         node = node->GetNext())
    {
        wxWasmSyncGLCanvases(node->GetData(), cls);
    }
}

// KICLOUD: tell the page about a page frame (wx/wasm/pageframes.h, B1.6d)
void wxTopLevelWindowWasm::NotifyPage(const char* event) const
{
    EM_ASM({
        if (typeof window !== 'undefined' && typeof window.wxWasmPageFrame === 'function')
            window.wxWasmPageFrame(UTF8ToString($0), UTF8ToString($1), UTF8ToString($2));
    }, event, static_cast<const char *>(m_pageKey.utf8_str()),
       static_cast<const char *>(m_title.utf8_str()));
}

// KICLOUD: page frames are shown one at a time (B1.6d). Showing one fills the page with it,
// hides the page frame that was shown (with its non-modal top-level windows), and makes it the
// application's top window, which the browser's resize, focus and file drops go to.
bool wxTopLevelWindowWasm::Show(bool show)
{
    if (!IsPageFrame() || show == IsShown())
    {
        const bool changed = base_type::Show(show);

        // KICLOUD: A14: a dialog or floating window shown where it does not fit (placed while
        // hidden, or created too narrow for its title) is brought wholly onto the page. Measured
        // after it is shown: a hidden window's title bar has no layout.
        if (show && HasTitleBar())
            wxWasmKeepOnPage(this);

        return changed;
    }

    if (show)
    {
        for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node;
             node = node->GetNext())
        {
            wxTopLevelWindow* other = wxDynamicCast(node->GetData(), wxTopLevelWindow);

            if (other && other != this && other->IsPageFrame() && other->IsShown())
                other->Show(false);
        }

        if (wxTheApp && wxTheApp->GetDisplay())
        {
            const wxSize screen = wxTheApp->GetDisplay()->GetScreenSize();
            SetSize(0, 0, screen.x, screen.y);
        }
    }
    else
    {
        m_hiddenWithPage.clear();

        for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node;
             node = node->GetNext())
        {
            wxWindow* tlw = node->GetData();
            wxDialog* dialog = wxDynamicCast(tlw, wxDialog);

            if (tlw != this && tlw->IsShown() && tlw->GetParent()
                && tlw->GetParent()->GetTopLevelWindow() == this
                && !(dialog && dialog->IsModal()))
            {
                m_hiddenWithPage.push_back(wxWeakRef<wxWindow>(tlw));
            }
        }

        for (size_t i = 0; i < m_hiddenWithPage.size(); ++i)
        {
            if (m_hiddenWithPage[i])
                m_hiddenWithPage[i]->Show(false);
        }

        wxActivateEvent deactivate(wxEVT_ACTIVATE, false, GetId());
        deactivate.SetEventObject(this);
        HandleWindowEvent(deactivate);
    }

    const bool changed = base_type::Show(show);

    // The frame's GL canvases (the editor's board or schematic view) follow it
    wxWasmSyncGLCanvases(this, wxClassInfo::FindClass(wxT("wxGLCanvas")));

    if (show)
    {
        for (size_t i = 0; i < m_hiddenWithPage.size(); ++i)
        {
            if (m_hiddenWithPage[i])
                m_hiddenWithPage[i]->Show(true);
        }

        m_hiddenWithPage.clear();

        // A modal dialog stays above whichever page frame is shown
        for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node;
             node = node->GetNext())
        {
            wxDialog* dialog = wxDynamicCast(node->GetData(), wxDialog);

            if (dialog && dialog->IsModal() && dialog->IsShown())
                dialog->Raise();
        }

        if (wxTheApp)
            wxTheApp->SetTopWindow(this);

        wxActivateEvent activate(wxEVT_ACTIVATE, true, GetId());
        activate.SetEventObject(this);
        HandleWindowEvent(activate);
        NotifyPage("shown");
    }

    return changed;
}

bool wxTopLevelWindowWasm::HasTitleBar() const
{
    // Main frame already has a native title bar.
    // KICLOUD: a page frame is a tab of the page and has none (B1.6d)
    return !IsMainFrame() && !IsPageFrame() && !(GetWindowStyle() & wxFRAME_NO_TASKBAR);
}

bool wxTopLevelWindowWasm::UseDomTitleBar() const
{
    // Every non-main top-level window with a title bar (secondary frames AND
    // dialogs) uses the real DOM title bar — consistent chrome (cursor, hover,
    // close X) and robust hit-testing over other frames' DOM controls. Popups /
    // tooltips carry wxFRAME_NO_TASKBAR, so HasTitleBar() is already false for
    // them and they get no bar.
    return HasTitleBar();
}

bool wxTopLevelWindowWasm::UseDomResize() const
{
    // Edge-resize handles only for windows wx considers resizable. wxRESIZE_BORDER
    // is the established signal: wxDEFAULT_FRAME_STYLE carries it (all frames) and
    // KiCad's DIALOG_SHIM defaults to it (all dialogs that don't opt out), so this
    // makes virtually every dialog/frame resizable while a deliberately fixed
    // dialog stays fixed.
    return UseDomTitleBar() && (GetWindowStyle() & wxRESIZE_BORDER);
}

wxPoint wxTopLevelWindowWasm::GetClientAreaOrigin() const
{
    wxPoint origin = wxTopLevelWindowBase::GetClientAreaOrigin();

    if (HasTitleBar())
    {
        origin.y += TITLE_BAR_HEIGHT;
    }

    return origin;
}

void wxTopLevelWindowWasm::DoGetClientSize(int *width, int *height) const
{
    wxTopLevelWindowBase::DoGetClientSize(width, height);

    if (height && HasTitleBar())
    {
        *height = wxMax(*height - TITLE_BAR_HEIGHT, 0);
    }
}

void wxTopLevelWindowWasm::DoSetClientSize(int width, int height)
{
    if (HasTitleBar())
    {
        height += TITLE_BAR_HEIGHT;
    }

    wxTopLevelWindowBase::DoSetClientSize(width, height);
}

void wxTopLevelWindowWasm::DoScreenToClient(int *x, int *y) const
{
    wxWindow::DoScreenToClient(x, y);
}

void wxTopLevelWindowWasm::DoClientToScreen(int *x, int *y) const
{
    wxWindow::DoClientToScreen(x, y);
}

void wxTopLevelWindowWasm::SetIcons(const wxIconBundle& icons)
{
    wxTopLevelWindowBase::SetIcons(icons);

    wxSize size = wxContentScaleFactor() >= 1.5 ? wxSize(32, 32) : wxSize(16, 16);

    wxIcon icon = icons.GetIcon(size, wxIconBundle::FALLBACK_NEAREST_LARGER);

    if (icon.IsOk())
    {
        icon.SyncToJs();

        EM_ASM({
            setIcon($0);
        }, icon.GetJavascriptId());
    }
}

void wxTopLevelWindowWasm::ShowWithoutActivating()
{
    Show(true);
}

bool wxTopLevelWindowWasm::ShowFullScreen(bool show, long WXUNUSED(style))
{
    if (show != IsFullScreen())
    {
        EM_ASM({
            showFullscreen($0);
        }, show);
    }

    return true;
}

bool wxTopLevelWindowWasm::IsFullScreen() const
{
    EmscriptenFullscreenChangeEvent fullscreenStatus;
    emscripten_get_fullscreen_status(&fullscreenStatus);
    return fullscreenStatus.isFullscreen;
}

void wxTopLevelWindowWasm::SetTitle(const wxString &title)
{
    m_title = title;

    // KICLOUD: the page shows a page frame's title on its tab (B1.6d)
    if (IsPageFrame())
        NotifyPage("title");

    if (IsMainFrame())
    {
        EM_ASM({
            document.title = UTF8ToString($0);
        }, static_cast<const char *>(title.utf8_str()));
    }
    else if (UseDomTitleBar())
    {
        // Push to the DOM title bar's text. No-op if the bar isn't built yet
        // (the Create-time SetTitle precedes createWindowTitlebar, which then
        // builds the bar with the current title).
        EM_ASM({
            setWindowTitle($0, UTF8ToString($1));
        }, GetCSSId(), static_cast<const char *>(title.utf8_str()));

        // KICLOUD: A14: a shown window whose new title is longer widens to show it whole
        if (IsShown())
            wxWasmKeepOnPage(this);
    }
}

void wxTopLevelWindowWasm::DrawTitleText(wxDC& dc, const wxRect& rect)
{
    wxCoord textWidth;
    wxCoord textHeight;
    dc.GetTextExtent(GetTitle(), &textWidth, &textHeight);

    int textX = wxMax((rect.width - textWidth) / 2, 0);
    int textY = wxMax((rect.height - textHeight) / 2, 0);

    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();

    dc.SetTextBackground(TITLE_BAR_BACKGROUND_COLOUR);
    dc.SetTextForeground(TITLE_BAR_FOREGROUND_COLOUR);
    dc.SetFont(font);

    dc.DrawText(GetTitle(), textX, textY);
}

void wxTopLevelWindowWasm::DrawMinimizeButton(wxDC& dc, const wxRect& rect)
{
    wxCoord buttonWidth = m_minimizeButtonRect.width - 2 * MINIMIZE_BUTTON_PADDING;
    wxCoord buttonHeight = m_minimizeButtonRect.height - 2 * MINIMIZE_BUTTON_PADDING;
    wxCoord buttonMargin = (rect.height - buttonHeight) / 2;

    wxCoord buttonX = wxMax(rect.x + rect.width - buttonWidth - buttonMargin, 0);
    wxCoord buttonY = rect.y + buttonMargin;

    m_minimizeButtonRect.x = buttonX - MINIMIZE_BUTTON_PADDING;
    m_minimizeButtonRect.y = buttonY - MINIMIZE_BUTTON_PADDING;

    dc.SetPen(wxPen(TITLE_BAR_FOREGROUND_COLOUR, 2));

    dc.DrawLine(buttonX, buttonY, buttonX + buttonWidth, buttonY + buttonHeight);
    dc.DrawLine(buttonX, buttonY + buttonHeight, buttonX + buttonWidth, buttonY);
}

void wxTopLevelWindowWasm::StartDrag(const wxPoint& pos)
{
    m_isDragging = true;
    m_dragOffset = pos;
    CaptureMouse();
}

void wxTopLevelWindowWasm::EndDrag()
{
    m_isDragging = false;
    ReleaseMouse();
}

void wxTopLevelWindowWasm::DragMove(const wxPoint& pos)
{
    Move(pos - m_dragOffset);
}

void wxTopLevelWindowWasm::OnNcPaint(wxNcPaintEvent& WXUNUSED(event))
{
    // Frames use a real DOM title bar (see UseDomTitleBar / createWindowTitlebar);
    // only dialogs still canvas-paint their title bar here.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxWindowDC dc(this);
        wxRect ncRect(0, 0, GetSize().x, TITLE_BAR_HEIGHT);

        dc.SetBrush(TITLE_BAR_BACKGROUND_COLOUR);
        dc.SetPen(*wxTRANSPARENT_PEN);

        dc.DrawRectangle(ncRect);

        DrawTitleText(dc, ncRect);
        DrawMinimizeButton(dc, ncRect);
    }
}

void wxTopLevelWindowWasm::OnMouseDown(wxMouseEvent& event)
{
    // Only dialogs reach the canvas title bar here; frames are driven by the DOM
    // title bar (UseDomTitleBar), whose events never propagate to #canvas.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxPoint pos = event.GetPosition() + GetClientAreaOrigin();

        if (pos.y < TITLE_BAR_HEIGHT && !m_minimizeButtonRect.Contains(pos))
        {
            StartDrag(pos);
        }
    }
}

void wxTopLevelWindowWasm::OnMouseUp(wxMouseEvent& event)
{
    if (m_isDragging)
    {
        EndDrag();
    }

    // Canvas close button is dialogs-only; frames use the DOM title bar's ×.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxPoint pos = event.GetPosition() + GetClientAreaOrigin();

        if (m_minimizeButtonRect.Contains(pos))
        {
            Close();
        }
    }
}

void wxTopLevelWindowWasm::OnMotion(wxMouseEvent& event)
{
    if (m_isDragging)
    {
        if (event.Dragging())
        {
            DragMove(ClientToScreen(event.GetPosition()));
        }
        else
        {
            EndDrag();
        }
    }
}

// ----------------------------------------------------------------------------
// JS -> C++ hooks for the DOM title bar (see createWindowTitlebar in wx.js).
// The DOM title bar drives the SAME C++ paths as the retired canvas chrome:
// drag -> Move() (one reposition source of truth), X -> Close() (-> EVT_CLOSE).
// ----------------------------------------------------------------------------

static wxTopLevelWindow* wxFindTopLevelByCSSId(int cssId)
{
    for (wxWindowList::iterator it = wxTopLevelWindows.begin();
         it != wxTopLevelWindows.end(); ++it)
    {
        wxTopLevelWindow* tlw = wxDynamicCast(*it, wxTopLevelWindow);
        if (tlw && tlw->GetCSSId() == cssId)
            return tlw;
    }
    return NULL;
}

// True if `win` is, or contains anywhere in its child tree, a window of class `cls`.
static bool wxWindowTreeHasClass(wxWindow* win, const wxClassInfo* cls)
{
    if (!win || !cls)
        return false;
    if (win->IsKindOf(cls))
        return true;
    for (wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
         node; node = node->GetNext())
    {
        if (wxWindowTreeHasClass(node->GetData(), cls))
            return true;
    }
    return false;
}

// True if `win` is, or contains, a wxGLCanvas. The 3D viewer's EDA_3D_CANVAS is a
// wxGLCanvas whose paint runs the (slow, multi-threaded) CPU raytracer — see
// wx_window_resize for why a synchronous repaint of such a window must be avoided.
// wxGLCanvas is looked up by NAME (wxClassInfo::FindClass) rather than referenced as a
// type, so this core translation unit does NOT create a link-time dependency on
// wxGLCanvas::ms_classInfo — the wxWidgets test apps link libwx_core but not the GL
// library. In an app that doesn't link a GL canvas, FindClass returns null → no match.
// Defined here (C++ linkage, NOT inside the extern "C" block below) and shared with
// wxApp::Paint() (app.cpp) to defer the raytracer on the synchronous mouse-button repaint
// path, the same reason wx_window_resize avoids a synchronous Paint() of such a window.
bool wxWasmWindowHostsGLCanvas(wxWindow* win)
{
    return wxWindowTreeHasClass(win, wxClassInfo::FindClass(wxT("wxGLCanvas")));
}

extern "C"
{

// Move a non-main top-level window to wx screen coords (x, y). Reuses Move() so
// the frame's children (GL canvas, tool/status bars) reposition through the
// normal size-event -> Layout path. Safe as a synchronous ccall (Move does not
// suspend the stack).
void EMSCRIPTEN_KEEPALIVE wx_window_move(int cssId, int x, int y)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
        win->Move(x, y);
}

// Close a non-main top-level window via wxEVT_CLOSE (-> the frame's
// OnCloseWindow). MUST be invoked as an ASYNC ccall: Close() runs the handler
// synchronously and may show a modal, which suspends — a plain ccall cannot
// suspend (SuspendError); wx_window_close is a promising export.
void EMSCRIPTEN_KEEPALIVE wx_window_close(int cssId)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
        win->Close(false);
}

// Resize a non-main top-level window to wx screen rect (x, y, width, height).
// Reuses SetSize so children reflow via the normal wxSizeEvent -> Layout path
// (incl. a frame's wxGLCanvas -> setGLCanvasRect) and the DOM syncs via
// wxNonOwnedWindow::DoSetSize -> setWindowRect. The DOM resize handles drag the
// left/bottom edges + corners, so this takes a full rect (origin + size), unlike
// the move-only wx_window_move. Safe as a synchronous ccall (SetSize does not
// suspend the stack).
void EMSCRIPTEN_KEEPALIVE wx_window_resize(int cssId, int x, int y, int width, int height)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
    {
        win->SetSize(x, y, width, height);

        // The JS resize reassigned (and thus CLEARED) the window's 2D canvas, so the
        // whole window must repaint — not just the strip SetSize invalidated. And
        // left to the per-frame tick the repaint lands a frame later (the dialog
        // flashes its black background meanwhile). Force a full, synchronous
        // repaint now — the same
        // remedy wxApp uses after a button event (HandleMouseButtonEvent -> Paint).
        // wxApp::Paint() only repaints windows whose NeedsPaint() is set, so this
        // refreshes just the resized window.
        //
        // EXCEPTION — a window hosting a wxGLCanvas (the 3D viewer, whose paint runs the
        // multi-threaded CPU raytracer). Painting it synchronously here runs the raytrace
        // NESTED inside this resize ccall (itself driven from a JS requestAnimationFrame
        // callback in wx.js). If that raytrace then has to spawn an on-demand pthread
        // Worker — the pre-warmed pool drained by earlier renders, e.g. camera moves —
        // booting the Worker needs the main thread back in the event loop, which it can't
        // reach while blocked in this synchronous Paint(): the join busy-waits for a Worker
        // that can never start → deadlock/freeze. The frame and its GL canvas have already
        // been resized (SetSize -> setGLCanvasRect); only the RE-RENDER is at stake, so let
        // Refresh() above repaint it through the normal per-frame event-loop pump instead —
        // exactly the path a camera move takes, where the Worker CAN boot.
        win->Refresh();
        if (wxTheApp && !wxWasmWindowHostsGLCanvas(win))
            wxTheApp->Paint();
    }
}

} // extern "C"
