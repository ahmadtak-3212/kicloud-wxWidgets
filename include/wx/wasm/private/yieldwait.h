///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/yieldwait.h
// Purpose:     Scheduler-build token waits: begin → park → resolve.
//              WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:include/wx/wasm/private/yieldwait.h (W3.0P; kicloud/docs/provenance.md)

#ifndef _WX_WASM_PRIVATE_YIELDWAIT_H_
#define _WX_WASM_PRIVATE_YIELDWAIT_H_

#include "wx/defs.h"   // KICLOUD: W3.0P (E2.4): WXDLLIMPEXP_CORE, wxDECLARE_NO_COPY_CLASS

// The doc-13 §2 yield API (pcbjam docs/features/async/17, step S4), backed by
// the jspi-scheduler.js shim's wait registry (the shim is the only runtime —
// no probe needed). The contract:
//
//   int token = wxWasmBeginWait("modal");   // BEFORE showing/suspending:
//                                           // a resolve racing ahead of the
//                                           // suspend pre-resolves the wait
//   ... show UI / start the async thing ...
//   int result = wxWasmYieldUntil(token);   // suspends THIS chain;
//                                           // scheduler-managed wake
//   ... later, from any JS/C++ path ...
//   wxWasmResolveWait(token, result);       // exact wait
//   wxWasmResolveTopWait("modal", result);  // or innermost of a kind (LIFO)
//
// No per-wait pump exists: the top-level tick is the only dispatcher (it runs
// at any DoRun depth), and the suspended chain's interlock slot is zeroed by
// its caller exactly as before. Implemented in evtloop.cpp.

extern "C" int  wxWasmBeginWait(const char *kind);
extern "C" int  wxWasmYieldUntil(int token);
extern "C" void wxWasmResolveWait(int token, int result);
extern "C" void wxWasmResolveTopWait(const char *kind, int result);

// KICLOUD: W3.0P (kicloud/TODO.md E2.4 and E2.8; W3.0P lens 2 of the K.11 retry): the
// nested blocking calls, a nested wxGUIEventLoop::DoRun() and wxDialog::ShowModal(), nest
// like native loops. Each keeps its own wait ("nested" or "modal"), begun before anything
// can end it (before Show(true)), and returns only once it has been ended AND every
// blocking call begun after it has returned: wx's ScheduleExit() contract ("the loop will
// exit as soon as the control flow returns to it, i.e. after any nested loops terminate",
// interface/wx/evtloop.h) and E2.4's "EndModal(): Resolve(token). Waits resolve in LIFO
// order." End() (ScheduleExit()/EndModal()) resolves the call's OWN wait; PCBJam resolved
// the innermost wait of the kind, whichever call was ending. A call that is ended while a
// later one still runs parks again on a wait of the kind "nested-ended"/"modal-ended", so
// the shim's error containment (resolveTopWait("nested"/"modal") after a handler threw)
// only ever finds calls that are not ended yet; a wait that the containment resolves ends
// its call, as before. Implemented in evtloop.cpp.
class WXDLLIMPEXP_CORE wxWasmNestedWait
{
public:
    // Begins the wait of `kind` ("nested" or "modal") and makes this call the innermost.
    explicit wxWasmNestedWait(const char *kind);
    ~wxWasmNestedWait();

    // false: the scheduler refused the wait (dead or terminal instance); do not block.
    bool IsOk() const { return m_listed; }

    // Suspends this chain until the call is ended and innermost; returns the result End()
    // was given (or the containment's result if it ended the call).
    int Wait();

    // Ends the call with `result`. Ending an ended call again only replaces the result.
    void End(int result);

    // true once End() ran (the containment's resolve does not count)
    bool IsEndedByOwner() const { return m_endedByOwner; }

    // blocking calls begun and not yet returned
    static size_t GetCount();

private:
    void Unlist();

    const char *m_endedKind;
    int m_token;
    int m_result;
    bool m_listed;
    bool m_ended;
    bool m_endedByOwner;

    wxDECLARE_NO_COPY_CLASS(wxWasmNestedWait);
};

#endif // _WX_WASM_PRIVATE_YIELDWAIT_H_
