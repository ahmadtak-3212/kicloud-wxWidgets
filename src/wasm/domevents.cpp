/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/domevents.cpp
// Purpose:     Routes DOM element events (click/input/focus...) from
//              wx-dom.js into wxWindowWasm::OnDomEvent. DOM port only.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/window.h"
#include "wx/app.h"
#include "wx/hashmap.h"
#include "wx/base64.h"
#include "wx/bitmap.h"
#include "wx/image.h"
#include "wx/log.h"
#include "wx/mstream.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/dom.h"
#include "wx/wasm/private/mouse.h"
#include "wx/wasm/private/mailbox.h"   // KICLOUD: P3-I
#include "wx/weakref.h"                // KICLOUD: P3-I
#include "wx/progdlg.h"                // KICLOUD: PERF (docs/patches.md), D3
#include "wx/toplevel.h"                // KICLOUD: PERF (docs/patches.md), D3
#include "wx/button.h"                  // KICLOUD: F-P8

#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

WX_DECLARE_HASH_MAP(int, wxWindowWasm*, wxIntegerHash, wxIntegerEqual,
                    wxDomWindowMap);

static wxDomWindowMap gs_domWindows;

void wxDomRegisterWindow(int domId, wxWindowWasm *window)
{
    gs_domWindows[domId] = window;
}

void wxDomUnregisterWindow(int domId)
{
    gs_domWindows.erase(domId);
}

// KICLOUD: P3-I (docs/patches.md): the window of a DOM id, for wxWasmMenuItemsNow (menu.cpp)
wxWindowWasm *wxDomFindWindowById(int domId)
{
    wxDomWindowMap::iterator it = gs_domWindows.find(domId);
    return it == gs_domWindows.end() ? NULL : it->second;
}

// domId of the element whose event is currently being dispatched (see the
// declaration in wx/wasm/private/dom.h). Lets a window owning auxiliary DOM
// elements (built-in scrollbar gutters) discriminate which one fired.
static int gs_currentEventDomId = 0;

int wxDomCurrentEventDomId()
{
    return gs_currentEventDomId;
}

wxString wxDomBitmapToDataURL(const wxBitmap& bitmap)
{
    if ( !bitmap.IsOk() )
        return wxString();

    const wxImage image = bitmap.ConvertToImage();
    if ( !image.IsOk() )
        return wxString();

    // Apps don't necessarily call wxInitAllImageHandlers(); register the
    // PNG handler on demand and keep encode failures out of wxLog dialogs.
    if ( !wxImage::FindHandler(wxBITMAP_TYPE_PNG) )
        wxImage::AddHandler(new wxPNGHandler);

    wxLogNull noLog;

    wxMemoryOutputStream stream;
    if ( !image.SaveFile(stream, wxBITMAP_TYPE_PNG) )
        return wxString();

    const size_t len = stream.GetSize();
    wxMemoryBuffer buf;
    stream.CopyTo(buf.GetWriteBuf(len), len);
    buf.UngetWriteBuf(len);

    return wxT("data:image/png;base64,") +
           wxBase64Encode(buf.GetData(), buf.GetDataLen());
}

// KICLOUD: F-P8 (LB10): the top-level window of the DOM control event that started the dispatch
// chain now running or parked (for DIALOG_DRC's run: the DRC dialog, from its "Run DRC" button).
// Set by wx_dom_event when it starts a chain while no other chain holds the interlock, and given
// back when that chain completes (wxDomChainOrigin below); read by wxDomCancelOfParkedRun().
static wxWeakRef<wxWindow> gs_chainOrigin;

namespace
{
// KICLOUD: F-P8: records the chain's origin window for as long as the chain lives. Under JSPI the
// destructor runs only when the chain has really completed (a suspension keeps the frame alive),
// like wxWasmDispatchGuard's. Only a chain that starts while the depth is 0 records (nested
// deliveries keep the outer chain's origin).
class wxDomChainOrigin
{
public:
    explicit wxDomChainOrigin(wxWindow *window)
        : m_recorded(wxWasmDispatchDepth == 0),
          m_previous(gs_chainOrigin)
    {
        if ( m_recorded )
            gs_chainOrigin = window ? wxGetTopLevelParent(window) : NULL;
    }

    // Gives back the origin seen at construction (a chain started inside a modal dialog, where the
    // depth reads 0, must not clear the origin of the chain that opened the dialog).
    ~wxDomChainOrigin()
    {
        if ( m_recorded )
            gs_chainOrigin = m_previous.get();
    }

private:
    const bool m_recorded;
    wxWeakRef<wxWindow> m_previous;

    wxDECLARE_NO_COPY_CLASS(wxDomChainOrigin);
};

// KICLOUD: F-P8 (LB10): whether a DOM event on `window` is the Cancel button of a long operation that
// its own dialog started and that is now parked in a main-thread wait for the thread pool. KiCad's
// DRC runs inside the click handler of DIALOG_DRC's "Run DRC" button; its waits for the thread pool
// suspend (wasm/shims/main_thread_wait.c) and hold the dispatch interlock, so a click on the
// dialog's Cancel was deferred until the run had ended (41 s on the largest demo board) and
// cancelled nothing. On the desktop that click is read by the dialog's own progress updates
// (wxSafeYield) while the run goes on. Called by wx_dom_event only while a chain is parked.
// True only when all hold: the window is a wxButton with id wxID_CANCEL; its top-level window is
// the one whose control started the parked chain (gs_chainOrigin); and the scheduler counts a
// parked main-thread wait (S.mainWaitParked: the chain waits for worker threads, not for a fetch
// or a nested dialog). The handler then runs inside that state: KiCad's DIALOG_DRC::OnCancelClick
// only sets the cancel flag while a run is going, which the run checks between its steps.
bool wxDomCancelOfParkedRun(wxWindowWasm *window)
{
    if ( !gs_chainOrigin || window->GetId() != wxID_CANCEL || !wxDynamicCast(window, wxButton) )
        return false;

    if ( wxGetTopLevelParent(window) != gs_chainOrigin.get() )
        return false;

    return EM_ASM_INT({
        var S = globalThis.__wxScheduler;
        return S && S.mainWaitParked > 0 ? 1 : 0;
    }) != 0;
}
} // namespace

// KICLOUD: P3-I: a DOM event deferred behind queued input (see wx_dom_event)
namespace
{
struct wxDomDeferredEvent
{
    wxWeakRef<wxWindow> window;
    int domId;
    int kind;
};

void wxDomDeliverDeferred(void *arg)
{
    wxDomDeferredEvent *ev = static_cast<wxDomDeferredEvent *>(arg);

    if ( !ev->window )
    {
        delete ev;
        return;
    }

    // Still behind queued keys or clicks (or a chain parked again): try on a later tick
    if ( wxWasmQueuedInput > 0 || wxWasmDispatchParked() )
    {
        wxWasmMailboxEnqueueAfter(wxDomDeliverDeferred, ev, 10);
        return;
    }

    wxWindowWasm *window = ev->window.get();
    const int domId = ev->domId;
    const int kind = ev->kind;
    delete ev;

    if ( !window->IsEnabled() )
        return;

    wxDomChainOrigin chainOrigin(window);   // KICLOUD: F-P8
    wxWasmDispatchGuard dispatchGuard;
    gs_currentEventDomId = domId;
    window->OnDomEvent(static_cast<wxDomEventKind>(kind));
    gs_currentEventDomId = 0;
}
} // namespace

void wxDomQueueDomEvent(wxWindowWasm *window, int domId, int kind)
{
    wxWasmMailboxEnqueueAfter(wxDomDeliverDeferred,
                              new wxDomDeferredEvent{ wxWeakRef<wxWindow>(static_cast<wxWindow *>(window)), domId, kind },
                              0);
}


extern "C"
{

// Called from wx-dom.js event listeners as an awaited call on a promising
// export: each call is a fresh WASM entry that may itself suspend, so this
// works even while a modal dialog keeps another chain suspended.
void EMSCRIPTEN_KEEPALIVE wx_dom_event(int domId, int kind)
{
    wxDomWindowMap::iterator it = gs_domWindows.find(domId);
    if ( it == gs_domWindows.end() || !it->second )
        return;

    wxWindowWasm *window = it->second;
    if ( !window->IsEnabled() )
        return;
    // KICLOUD: P3-I T14 a control of a window under a modal dialog takes no input (the page
    // makes those windows inert; this catches what slips through)
    if ( wxWasmBlockedByModal(window) )
        return;

    // KICLOUD: PERF (docs/patches.md), D3: a control of a progress dialog (its Cancel or Skip
    // button) is delivered at once even while a chain is parked. The chain that is parked is the
    // long operation the dialog reports on (zone fill, DRC: its main thread waits for the thread
    // pool and suspends, wasm/shims/main_thread_wait.c); deferring the click until that chain
    // ends made Cancel do nothing. On the desktop the same click is read inside the dialog's
    // Update() while every other window is disabled; here the dialog's handler (it only marks the
    // dialog cancelled or skipped) runs from this entry. Other windows stay deferred: KiCad keeps
    // them disabled for the dialog's life anyway (WX_PROGRESS_REPORTER).
#if wxUSE_PROGRESSDLG
    if ( wxWasmDispatchParked() && wxDynamicCast(wxGetTopLevelParent(window), wxProgressDialog) )
    {
        wxWasmDispatchGuard progressGuard;
        gs_currentEventDomId = domId;
        window->OnDomEvent(static_cast<wxDomEventKind>(kind));
        gs_currentEventDomId = 0;
        return;
    }
#endif

    // KICLOUD: F-P8 (LB10): the Cancel of a dialog whose own run is parked waiting for the thread
    // pool is delivered at once, like a progress dialog's (see wxDomCancelOfParkedRun).
    if ( wxWasmDispatchParked() && wxDomCancelOfParkedRun(window) )
    {
        wxWasmDispatchGuard cancelGuard;
        gs_currentEventDomId = domId;
        window->OnDomEvent(static_cast<wxDomEventKind>(kind));
        gs_currentEventDomId = 0;
        return;
    }

    if ( wxWasmDispatchParked() || wxWasmQueuedInput > 0 )
    {
        // Another dispatch chain is suspended mid-handler; defer this DOM
        // event to the first tick after resume instead of running handlers
        // over its half-mutated widget state.
        // KICLOUD: P3-I (docs/patches.md): and behind every keyboard or mouse-button event
        // queued before it (wxWasmQueuedInput): the deferred event goes through the scheduler
        // mailbox and is delivered only once those are delivered. A posted CallAfter here was
        // delivered after just one of the queued keys (one event per handler per pass), so a
        // dialog's OK closed the dialog before the text typed into it arrived.
        wxDomQueueDomEvent(window, domId, kind);
        return;
    }

    wxDomChainOrigin chainOrigin(window);   // KICLOUD: F-P8: before the guard raises the depth
    wxWasmDispatchGuard dispatchGuard;

    gs_currentEventDomId = domId;
    window->OnDomEvent(static_cast<wxDomEventKind>(kind));
    gs_currentEventDomId = 0;
}

// Mouse events forwarded from the DOM layer (wx-dom.js document-level
// listeners): DOM elements swallow browser events before the #canvas
// Emscripten callbacks see them, which starves the wx-side hit-testing
// pipeline (ENTER/LEAVE hover, wheel scrolling, right-click) whenever the
// pointer is over a DOM-backed control. This entry rebuilds the
// Emscripten event structs and reuses the exact same converter + handler
// path, so capture, double-click state and hover synthesis stay in one
// place (src/wasm/app.cpp).
//
// kind: 1=motion, 2=down, 3=up, 4=wheel.
// x/y: #canvas-relative CSS px (same space as Emscripten targetX/Y).
// button/buttons/detail: DOM MouseEvent semantics.
// modifiers: 1 ctrl | 2 shift | 4 alt | 8 meta.
// Returns 1 when wx consumed the event (JS uses it for preventDefault).
int EMSCRIPTEN_KEEPALIVE wx_dom_mouse(int kind, int x, int y,
                                      int button, int buttons, int detail,
                                      int modifiers, double deltaY)
{
    if ( !wxTheApp )
        return 0;

    EmscriptenMouseEvent mouse;
    memset(&mouse, 0, sizeof(mouse));
    mouse.timestamp = emscripten_get_now();
    mouse.targetX = x;
    mouse.targetY = y;
    mouse.button = static_cast<unsigned short>(button);
    mouse.buttons = static_cast<unsigned short>(buttons);
    wxUnusedVar(detail); // EmscriptenMouseEvent has no click-count field;
                         // dclick synthesis lives in the converter's state
    mouse.ctrlKey = (modifiers & 1) != 0;
    mouse.shiftKey = (modifiers & 2) != 0;
    mouse.altKey = (modifiers & 4) != 0;
    mouse.metaKey = (modifiers & 8) != 0;

    wxMouseEvent event;

    if ( kind == 4 )
    {
        EmscriptenWheelEvent wheel;
        memset(&wheel, 0, sizeof(wheel));
        wheel.mouse = mouse;
        wheel.deltaY = deltaY;

        if ( !EmscriptenWheelEventToWXEvent(wheel, wxVERTICAL, &event) )
            return 0;

        wxTheApp->HandleMouseWheelEvent(&event);
        return 1;
    }

    int emType;
    switch ( kind )
    {
        case 2:  emType = EMSCRIPTEN_EVENT_MOUSEDOWN; break;
        case 3:  emType = EMSCRIPTEN_EVENT_MOUSEUP; break;
        default: emType = EMSCRIPTEN_EVENT_MOUSEMOVE; break;
    }

    if ( !EmscriptenMouseEventToWXEvent(emType, mouse, &event) )
        return 0;

    wxTheApp->HandleMouseEvent(&event);
    return 1;
}

} // extern "C"
