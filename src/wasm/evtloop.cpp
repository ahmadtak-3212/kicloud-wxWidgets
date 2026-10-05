/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.cpp
// Purpose:     wxGUIEventLoop implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/evtloop.h"
#include "wx/init.h"
#include "wx/toplevel.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <emscripten/threading.h>  // KICLOUD: PERF (docs/patches.md): wxWasmRequestTick from pool threads
#include <stdio.h>   // printf: diagnostics land in the browser console
#include <string.h>  // KICLOUD: W3.0P (E2.4): strcmp for wxWasmNestedWait's kinds

#include <atomic>    // KICLOUD: PERF (docs/patches.md): the cross-thread wake latch
#include <deque>
#include <vector>    // KICLOUD: W3.0P (E2.4): the nested blocking calls (wxWasmNestedWait)

// Run work on an activation that may suspend (defined below; declared here
// for the entries near the top of this file). See its definition for why
// plain entries must queue their jobs for the promising job tick.
extern "C" void wxWasmRunOnDispatchContext(void (*fn)(void *), void *arg);

// See wx/wasm/private/dispatch.h for the interlock contract.
int wxWasmDispatchDepth = 0;

void wxWasmDispatchAbandon()
{
    wxWasmDispatchDepth = 0;
    // KICLOUD: P3-I: input queued for a dead chain will never be delivered: let DOM events through
    wxWasmQueuedInput = 0;
}

void wxWasmDispatchRestore(int saved, const char *site)
{
    // Guards taken while the count was zeroed are about to be erased: the
    // interlock will read "nothing parked" although `erased` chains still are.
    const int erased = wxWasmDispatchDepth;

    wxWasmDispatchDepth = saved;

    if (erased != 0)
    {
        static int s_erasedCount = 0;
        ++s_erasedCount;
        // Loud for the first few, then sparse: the interesting fact is THAT it
        // happened and how often, not each instance.
        if (s_erasedCount <= 10 || s_erasedCount % 100 == 0)
        {
            printf("[wx-dispatch] ERASED %d held chain(s) restoring depth=%d at %s "
                   "(occurrence %d) - interlock now reads open while a chain is parked\n",
                   erased, saved, site, s_erasedCount);
        }
    }

    if (wxWasmDispatchDepth < 0)
    {
        printf("[wx-dispatch] NEGATIVE depth=%d at %s - accounting is corrupt\n",
               wxWasmDispatchDepth, site);
    }
}

// ----------------------------------------------------------------------------
// Scheduler mailbox (wx/wasm/private/mailbox.h; pcbjam docs/features/
// async/17 S1). The queue itself lives in the jspi-scheduler.js shim linked
// as a --pre-js — this side pushes deferred callbacks and pulls due messages
// from a fresh delivery tick. The shim is the ONLY runtime: a glue without it
// is a broken build, caught loudly by wxWasmSchedulerAssertInstalled() at
// main-loop entry.
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmMailboxJsEnabled, (), {
    return (typeof globalThis !== "undefined" &&
            globalThis.__wxSchedulerInstalled &&
            globalThis.__wxScheduler &&
            globalThis.__wxScheduler.mailbox) ? 1 : 0;
});

// The scheduler shim is linked into every glue as a --pre-js
// (scripts/kicad/build-kicad-target.sh, tests/apps/Makefile.wasm); running
// without it means the link flags were dropped and every suspend/wait/timer
// lane below would die in obscure ways. Fail fast and name the culprit.
static void wxWasmSchedulerAssertInstalled()
{
    static bool s_checked = false;
    if (s_checked)
        return;
    s_checked = true;
    if (!wxWasmMailboxJsEnabled())
    {
        printf("[wx-scheduler] FATAL: jspi-scheduler shim not present in this "
               "glue - the --pre-js scripts/common/shims/jspi-scheduler.js "
               "link flag is missing (see scripts/kicad/build-kicad-target.sh "
               "and tests/apps/Makefile.wasm)\n");
        abort();
    }
}

EM_JS(void, wxWasmMailboxJsEnqueue, (void *fn, void *arg, int ms), {
    globalThis.__wxScheduler.enqueueAfter(fn, arg, ms);
});

EM_JS(int, wxWasmMailboxJsPending, (), {
    return globalThis.__wxScheduler.mailbox.length;
});

// Pop the oldest due message into *fnOut/*argOut; 0 if the queue is empty.
EM_JS(int, wxWasmMailboxJsPop, (void **fnOut, void **argOut), {
    var m = globalThis.__wxScheduler.pop();
    if (!m) return 0;
    HEAPU32[fnOut >> 2] = m.fn;
    HEAPU32[argOut >> 2] = m.arg;
    return 1;
});

extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs)
{
    wxWasmMailboxJsEnqueue(reinterpret_cast<void *>(fn), arg, millisecs);
}

extern "C" void wxWasmMailboxDeliver()
{
    // S6 teardown parity with ProcessEvents: after the main loop exits the
    // app object is being (or has been) destroyed — a queued timer message
    // delivered now calls into freed timer state.
    if (!wxTheApp)
        return;

    // Snapshot the count: a handler that re-arms its timer with delay 0 must
    // not extend this drain unboundedly.
    int budget = wxWasmMailboxJsPending();
    while (budget-- > 0)
    {
        // A delivered handler may itself suspend (a lib fetch inside a timer
        // handler): its chain then holds the interlock, and delivering more
        // messages would interleave them with that suspended chain — exactly
        // the collision the mailbox exists to prevent. Leave the remainder
        // queued; the JS delivery tick retries after the resume.
        if (wxWasmDispatchParked())
            break;

        void *fn = NULL;
        void *arg = NULL;
        if (!wxWasmMailboxJsPop(&fn, &arg))
            break;
        reinterpret_cast<void (*)(void *)>(fn)(arg);
    }
}

int wxWasmMailboxNestedBaseline = -1;
int wxWasmMailboxSleptDepth = -1;

extern "C" void wxWasmNoteSleep()
{
    wxWasmMailboxSleptDepth = wxWasmDispatchDepth;
}

extern "C" void wxWasmMailboxDeliverNested()
{
    if (!wxTheApp)
        return;

    // Consent is scoped to this depth (see mailbox.h): handlers run with the
    // caller's guard on the stack, then take their own like any dispatch.
    const int savedBaseline = wxWasmMailboxNestedBaseline;
    wxWasmMailboxNestedBaseline = wxWasmDispatchDepth;

    // Same snapshot budget as wxWasmMailboxDeliver: a timer re-armed with
    // delay 0 must not turn one wxYield into an endless drain.
    int budget = wxWasmMailboxJsPending();
    while (budget-- > 0)
    {
        void *fn = NULL;
        void *arg = NULL;
        if (!wxWasmMailboxJsPop(&fn, &arg))
            break;
        reinterpret_cast<void (*)(void *)>(fn)(arg);
    }

    wxWasmMailboxNestedBaseline = savedBaseline;
}

extern "C" {

    // The mailbox's own dispatch entry (docs/features/async/17 S1). Called by
    // the shim's self-armed delivery tick from a fresh JS task — never from
    // inside another activation's awaited export. A fresh entry is exactly
    // the context bare setTimeout timer callbacks always ran in — the mailbox
    // changes WHEN a message runs (queued, in order, interlock free), not the
    // kind of entry it runs on.
    void EMSCRIPTEN_KEEPALIVE wxWasmMailboxTick()
    {
        // A delivered timer handler can suspend exactly like a DOM handler
        // can, so it takes the same route: wxWasmRunOnDispatchContext runs it
        // here when this tick is a promising activation and defers it to the
        // promising job tick otherwise (a plain entry would trap with
        // SuspendError at the first wait below it).
        wxWasmRunOnDispatchContext([](void *) { wxWasmMailboxDeliver(); }, NULL);
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Scheduler token waits (wx/wasm/private/yieldwait.h; doc 17 S4).
// The wait registry lives in the jspi-scheduler.js shim; these are thin
// bridges. wxWasmYieldUntil is the ONE suspend primitive the waits share — a
// JSPI await the scheduler core manages like any other (deferred wakes,
// registry, recorder).
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmBeginWaitJs, (const char *kind), {
    return globalThis.__wxScheduler.beginWait(UTF8ToString(kind));
});

EM_ASYNC_JS(int, wxWasmYieldUntilJs, (int token), {
    return await globalThis.__wxScheduler.waitPromise(token);
});

EM_JS(void, wxWasmResolveWaitJs, (int token, int result), {
    globalThis.__wxScheduler.resolveWait(token, result);
});

EM_JS(void, wxWasmResolveTopWaitJs, (const char *kind, int result), {
    globalThis.__wxScheduler.resolveTopWait(UTF8ToString(kind), result);
});

extern "C" int wxWasmBeginWait(const char *kind)
{
    return wxWasmBeginWaitJs(kind);
}

// Early-resolve window: a bridge whose request settles before the C++ frame
// reaches the suspend (a provider answering from cache, or a test page with
// no provider at all) resolves the wait first. The shim retains such entries
// with the result attached; peek-and-consume here instead of suspending on a
// wait whose resolve has already been spent.
EM_JS(int, wxWasmWaitEarlyResolvedJs, (int token), {
    return globalThis.__wxScheduler.waitEarlyResolved(token);
});

EM_JS(int, wxWasmTakeWaitResultJs, (int token), {
    return globalThis.__wxScheduler.takeWaitResult(token);
});

extern "C" int wxWasmYieldUntil(int token)
{
    // Every activation suspends uniformly through the wait import.
    // Early-resolve still short-circuits.
    if (wxWasmWaitEarlyResolvedJs(token))
        return wxWasmTakeWaitResultJs(token);

    return wxWasmYieldUntilJs(token);
}

extern "C" void wxWasmResolveWait(int token, int result)
{
    wxWasmResolveWaitJs(token, result);
}

extern "C" void wxWasmResolveTopWait(const char *kind, int result)
{
    wxWasmResolveTopWaitJs(kind, result);
}

// Ungated dispatch body: used by the pump once the interlock check passed and
// by wxGUIEventLoop::Dispatch()/wxYield, which deliberately dispatch NESTED
// inside a running handler chain (the interlock only forbids interleaving
// with a PARKED chain, not same-stack recursion).
// Completed ProcessIdle() passes, for the e2e harness (wxWasmIdlePassCount):
// idle runs only every third tick, so a test that just committed something
// whose consequences run from wxEVT_UPDATE_UI (a wxGrid auto-size that
// accepts+hides the open cell editor, say) can wait for the pass that
// delivers them instead of racing it with the next click.
static unsigned s_idlePasses = 0;

// ----------------------------------------------------------------------------
// Event-driven top-level loop: the wake requests
// ----------------------------------------------------------------------------
// KICLOUD: PERF (docs/patches.md), D2. The top-level loop parks until something asks for a tick
// (jspi-scheduler.js loopWait/requestTick). Two kinds of request:
//   * a repaint only (idle = 0): a window was invalidated (wxWindowWasm::Invalidate);
//   * a tick that also owes wx idle processing (idle = 1): an input event, a timer that fired, a
//     queued wx event (wxWakeUpIdle -> wxGUIEventLoop::WakeUp), a host call into the model, or an
//     idle handler that asked for more. Native wx sends idle after events the same way.
// A repaint alone does not owe idle: UpdateUI handlers that refresh a control would otherwise keep
// idle running forever.
// State is main-thread only, except s_threadWakePosted, the latch that limits wakes from other
// threads to one queued main-thread call at a time.
static bool s_wakeRequested = true;   // a JS wake was sent since the last tick began
static bool s_idleOwed = true;        // the next idle-capable tick must run ProcessIdle
static std::atomic<bool> s_threadWakePosted(false);
static unsigned s_missedWakes = 0;    // heartbeat ticks that found unannounced work

EM_JS(void, wxWasmRequestTickJs, (), {
    if (globalThis.__wxScheduler && globalThis.__wxScheduler.requestTick) globalThis.__wxScheduler.requestTick();
});

// 1 when the tick now running was started by the loop's heartbeat rather than by a wake.
EM_JS(int, wxWasmTickWasHeartbeatJs, (), {
    var S = globalThis.__wxScheduler;
    return (S && S._lastTickHeartbeat) ? 1 : 0;
});

static void wxWasmRequestTickMain(int idle)
{
    if (idle)
        s_idleOwed = true;
    // Already asked since the last tick began: that tick is still coming (the JS flag is set).
    if (s_wakeRequested)
        return;
    s_wakeRequested = true;
    wxWasmRequestTickJs();
}

static void wxWasmRequestTickFromThread()
{
    s_threadWakePosted.store(false);
    wxWasmRequestTickMain(1);
}

extern "C" {

    // Ask the top-level loop for a tick (see above). idle != 0 also owes wx idle processing.
    // Callable from any thread: a pool thread (wxQueueEvent from a worker) queues one call to
    // the main thread instead of touching main-thread state. Sets flags only and never suspends,
    // so it is safe from plain entries and while a dispatch chain is parked. Exported for the
    // scheduler shim's host-call wrappers (Module._wxWasmRequestTick).
    void EMSCRIPTEN_KEEPALIVE wxWasmRequestTick(int idle)
    {
        if (!emscripten_is_main_runtime_thread())
        {
            if (!s_threadWakePosted.exchange(true))
                emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_V, wxWasmRequestTickFromThread);
            return;
        }
        wxWasmRequestTickMain(idle);
    }

    // Plain export for diagnostics and tests: heartbeat ticks that found work no wake announced.
    unsigned EMSCRIPTEN_KEEPALIVE wxWasmMissedWakeCount()
    {
        return s_missedWakes;
    }

}  // extern "C"

// Whether a shown, unfrozen, non-empty top-level window still waits for a repaint. Hidden
// windows keep their flags until shown (DoPaint skips them), so they do not count.
static bool wxWasmVisiblePaintPending()
{
    for (wxWindowList::iterator it = wxTopLevelWindows.begin(); it != wxTopLevelWindows.end(); ++it)
    {
        wxWindow *w = *it;
        if (w && w->IsShown() && !w->IsFrozen() && w->NeedsPaint())
        {
            const wxSize sz = w->GetClientSize();
            if (sz.GetWidth() > 0 && sz.GetHeight() > 0)
                return true;
        }
    }
    return false;
}

// One pass of pending events, repaint and (every third pass) wx idle processing.
// topLevel = false: wxGUIEventLoop::Dispatch()/wxYield, nested in a running chain: unchanged
// behaviour (idle every third pass). topLevel = true: the event-driven loop's tick (KICLOUD: PERF,
// D2): idle runs only when owed, and the tick asks for the next one while idle is still owed (so
// the third-pass cadence reaches it) or an idle handler asked for more.
static void wxWasmProcessEventsUngated(bool topLevel = false)
{
    static int counter = 0;

    wxWasmDispatchGuard guard;
    if (topLevel)
        s_wakeRequested = false;   // wakes from here on ask for the next tick
    wxTheApp->ProcessPendingEvents();
    wxTheApp->Paint();
    if (counter++ % 3 == 0 && (!topLevel || s_idleOwed))
    {
        if (topLevel)
            s_idleOwed = false;
        if (wxTheApp->ProcessIdle() && topLevel)
            s_idleOwed = true;     // RequestMore: native wx keeps sending idle events
        ++s_idlePasses;
    }
    if (topLevel && s_idleOwed)
        wxWasmRequestTickMain(1);
}

extern "C" {

    // Plain export (reads a counter, runs no wx code): callable from any JS
    // context, including while a dispatch chain is parked.
    unsigned EMSCRIPTEN_KEEPALIVE wxWasmIdlePassCount()
    {
        return s_idlePasses;
    }

    // Called by a JS entry point whose ccall into wx died abnormally (trap or
    // Emscripten abort): that chain's guard destructor never ran, so release
    // the interlock it still holds. Without this the first such failure wedges
    // every later event behind a chain that no longer exists.
    void EMSCRIPTEN_KEEPALIVE wx_dispatch_abandon()
    {
        wxWasmDispatchAbandon();
    }

    void EMSCRIPTEN_KEEPALIVE ProcessEvents()
    {
        if (!wxTheApp)
            return;

        if (wxWasmDispatchParked())
        {
            // Another dispatch chain is suspended mid-handler (e.g. a library
            // bridge fetch inside a key handler). Running more handlers now
            // would interleave two C++ stacks over the same widget state.
            // Keep painting so the UI stays live; queued events dispatch on
            // the first tick after the suspended chain resumes.
            // KICLOUD: PERF (docs/patches.md), D2: keep ticking every frame while parked (the
            // old loop's behaviour): the page repaints during the wait, and the first tick after
            // the chain resumes dispatches what queued meanwhile.
            s_wakeRequested = false;
            wxTheApp->Paint();
            wxWasmRequestTickMain(0);
            return;
        }

        wxWasmProcessEventsUngated(true);
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Event loops via JSPI (top-level and nested quasi-modal)
// ----------------------------------------------------------------------------
//
// Neither loop uses emscripten_set_main_loop's simulate_infinite_loop=1, which throws
// an "unwind" to ABANDON the C++ stack: that throw is fatal under native wasm-EH (the
// compiler's catch_all cleanup pads catch the foreign exception and run destructors
// that tear down the main frame before it paints — docs/features/wasm-exceptions/08+09).
//
//   * top level (DoRun depth 0): a plain C++ while-loop on main()'s promising
//     activation. Each tick schedules dispatch from a fresh JS task and suspends for
//     ONE animation frame via wxWasmYieldToBrowser — a JSPI suspension the engine
//     resumes on the next frame.
//   * nested (DoRun depth >0): a registered "nested" scheduler wait (doc 17 S4) — no
//     pump; the top-level tick keeps dispatching at any depth.
// KICLOUD: W3.0P (kicloud/TODO.md E2.4/E2.8; W3.0P lens 2): the nested loop's wait is a
// wxWasmNestedWait (wx/wasm/private/yieldwait.h), and so is a modal's: ScheduleExit() ends
// the exiting loop's OWN wait (PCBJam resolved the innermost "nested" wait, whichever loop
// was exiting, and DoRun returned 0), and a loop returns, with the code ScheduleExit() was
// given, once every blocking call begun after it has returned. The top-level loop likewise
// exits only once no nested blocking call is left.

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

// ----------------------------------------------------------------------------
// wxWasmNestedWait: the nested blocking calls, innermost last
// ----------------------------------------------------------------------------
// KICLOUD: W3.0P (kicloud/TODO.md E2.4; W3.0P lens 2 of the K.11 retry). The contract is in
// wx/wasm/private/yieldwait.h. The list holds pointers to the calls' own wxWasmNestedWait
// objects, which live on the suspended stacks of the calls (each activation has its own
// stack region, scheduler.js), so a call's End() from another activation reaches it.

namespace
{

std::vector<wxWasmNestedWait *> &wxWasmNestedWaits()
{
    static std::vector<wxWasmNestedWait *> s_waits;
    return s_waits;
}

}  // namespace

wxWasmNestedWait::wxWasmNestedWait(const char *kind)
    : m_endedKind(strcmp(kind, "modal") == 0 ? "modal-ended" : "nested-ended"),
      m_token(wxWasmBeginWait(kind)),
      m_result(0),
      m_listed(false),
      m_ended(false),
      m_endedByOwner(false)
{
    // Token 0 = the scheduler refused the wait (dead or terminal instance):
    // never begin a park nothing can resolve (IsOk() is false).
    if ( m_token > 0 )
    {
        wxWasmNestedWaits().push_back(this);
        m_listed = true;
    }
}

wxWasmNestedWait::~wxWasmNestedWait()
{
    // Wait() takes the call off the list when it returns; a call that never waited (or
    // unwound) leaves it here.
    if ( m_listed )
        Unlist();
}

size_t wxWasmNestedWait::GetCount()
{
    return wxWasmNestedWaits().size();
}

void wxWasmNestedWait::Unlist()
{
    std::vector<wxWasmNestedWait *> &waits = wxWasmNestedWaits();
    for ( size_t i = waits.size(); i-- > 0; )
    {
        if ( waits[i] == this )
        {
            waits.erase(waits.begin() + i);
            break;
        }
    }
    m_listed = false;

    // The enclosing call is innermost now. If it was ended while this one ran, resume it
    // so that it returns (wxGTK: the returning loop's gtk_main_quit() of the enclosing one).
    // A wait of it that is resolved already (its resume is queued) makes this a no-op.
    if ( !waits.empty() && waits.back()->m_ended )
        wxWasmResolveWait(waits.back()->m_token, waits.back()->m_result);
}

int wxWasmNestedWait::Wait()
{
    if ( !m_listed )
        return m_result;

    for ( ;; )
    {
        const int result = wxWasmYieldUntil(m_token);   // suspends until resolved
        if ( !m_ended )
        {
            // Resolved by someone other than End(): the shim's error containment
            // (resolveTopWait after a handler threw). That ends the call, as before.
            m_ended = true;
            m_result = result;
        }
        if ( wxWasmNestedWaits().back() == this )
            break;

        // Ended while a call begun after this one still runs: park again until that one
        // has returned (its Unlist() resolves the new wait). The "-ended" kind keeps this
        // wait out of the containment's "nested"/"modal" stacks.
        m_token = wxWasmBeginWait(m_endedKind);
        if ( m_token <= 0 )
            break;   // refused (dead or terminal instance): nothing could resume this call
    }

    Unlist();
    return m_result;
}

void wxWasmNestedWait::End(int result)
{
    m_result = result;
    m_endedByOwner = true;
    if ( m_ended )
        return;

    m_ended = true;
    // Resolve the call's own wait (E2.4). Resolved before its Wait() parks (EndModal inside
    // Show), the wait returns at once; a call that is not innermost parks again (Wait()).
    if ( m_listed )
        wxWasmResolveWait(m_token, result);
}

// KICLOUD: PERF (docs/patches.md), D2: the top-level loop's park: until a wake asks for a tick
// (then the next animation frame) or the heartbeat expires (jspi-scheduler.js loopWait). With
// ?wxloop=frame it is the old one-frame yield.
EM_ASYNC_JS(void, wxWasmLoopWait, (), {
    await globalThis.__wxScheduler.loopWait();
});

// Top-level main loop: yield to the browser for ONE animation frame, then
// return. Called in a plain C++ while-loop in DoRun (below), so each tick
// suspends main()'s promising activation for exactly one frame and resumes.
// Route through the shim so the frame yield shares the one shadow-stack
// discipline implementation (jspi-scheduler.js _suspendOn; emscripten #27364).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await globalThis.__wxScheduler.frameYield();
});

// Deliver the tick's events from a FRESH JS task instead of inline in the main
// loop (docs/features/async/16 round 6). Dispatching from a fresh entry is
// exactly what every HEALTHY dispatch already does (the DOM handlers and timer
// callbacks that run while main is suspended), and it keeps a handler's own
// suspension out of the main loop's frame-yield continuation. ProcessEvents
// itself is re-entrancy-safe: it no-ops into a repaint whenever a chain is
// parked.
EM_JS(void, wxWasmScheduleProcessEvents, (), {
    setTimeout(function () {
        // A throwing handler must not leave the dispatch interlock held nor a
        // suspended quasi-modal unresolved (silent stall — the asyncify-races
        // nested_quasi_modal_pump_error case). The containment releases the
        // innermost registered waits: the nested loop AND the top modal
        // (5101 = wxID_CANCEL).
        var contain = function (e) {
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            if (globalThis.__wxScheduler) {
                globalThis.__wxScheduler.resolveTopWait('nested', 0);
                globalThis.__wxScheduler.resolveTopWait('modal', 5101);
            }
        };
        var p;
        try {
            p = Module["_wxWasmTopLevelTick"]();
        } catch (e) {
            // Defensive: a promising export delivers throws as rejections,
            // but keep the sync arm for a call that fails before the export
            // even runs (dead runtime, missing export).
            contain(e);
            throw e;
        }
        // The export is promising, so ANY throw — even one before the first
        // suspension — arrives as a promise REJECTION, never the sync catch
        // above. Same containment, async path.
        Promise.resolve(p).catch(function (e) {
            contain(e);
            console.warn("[wx] top-level tick rejected: " + e);
        });
    }, 0);
});

extern "C" {

    // The top-level loop's scheduled dispatch. A separate entry point from
    // ProcessEvents so the JS side has one obvious name to schedule, and so
    // any future top-level-only policy has a home that direct ProcessEvents
    // calls do not share.
    //
    // Deliberately NOT gated on s_wxRunDepth: nested loops are registered
    // waits, not pumps (doc 17 S4), so while a long operation is suspended
    // inside a quasi-modal loop this tick is the only dispatcher there is;
    // refusing to dispatch there would stall exactly the loads this entry
    // exists to keep moving. A throwing handler tears a nested loop down from
    // the error path in wxWasmScheduleProcessEvents, which releases the
    // suspended nested DoRun.
    void EMSCRIPTEN_KEEPALIVE wxWasmTopLevelTick()
    {
        // KICLOUD: PERF (docs/patches.md), D2: a tick started by the loop's heartbeat (no wake for
        // HEARTBEAT_MS) only looks for work. Finding some means a wake was missed somewhere: it
        // is processed now and counted (wxWasmMissedWakeCount) and logged, so the source can be
        // found and given its wake.
        if (wxTheApp && wxWasmTickWasHeartbeatJs() && !s_wakeRequested)
        {
            const bool pending = wxTheApp->HasPendingEvents();
            const bool paint = wxWasmVisiblePaintPending();
            if (!pending && !paint && !s_idleOwed)
                return;
            ++s_missedWakes;
            if (s_missedWakes <= 10 || s_missedWakes % 100 == 0)
                printf("[wx-loop] heartbeat found unannounced work (pending events %d, repaint %d, idle %d; occurrence %u)\n",
                       pending ? 1 : 0, paint ? 1 : 0, s_idleOwed ? 1 : 0, s_missedWakes);
        }

        // The tick itself is a promising export — dispatch directly; a
        // handler that suspends parks this tick's own activation.
        ProcessEvents();
    }

}  // extern "C"

// JSPI plain-entry job lane: emscripten_set_*_callback entries cannot suspend
// (they are not promising exports), so their jobs queue here and the promising
// wxWasmJobTick delivers them from a fresh task, in order.
namespace
{
struct wxWasmJspiJob
{
    void (*fn)(void *);
    void *arg;
};

std::deque<wxWasmJspiJob> &wxWasmJspiJobs()
{
    static std::deque<wxWasmJspiJob> s_jobs;
    return s_jobs;
}
}  // namespace

EM_JS(int, wxWasmOnPromisingActivationJs, (), {
    const S = globalThis.__wxScheduler;
    return (S && S._actStack && S._actStack.length > 0) ? 1 : 0;
});

EM_JS(void, wxWasmArmJspiJobTickJs, (), {
    const S = globalThis.__wxScheduler;
    if (S.__jobTickArmed) return;
    S.__jobTickArmed = true;
    setTimeout(function () {
        S.__jobTickArmed = false;
        if (S.dead) return;
        var p = Module["_wxWasmJobTick"]();
        Promise.resolve(p).catch(function (e) {
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            S.resolveTopWait('nested', 0);
            S.resolveTopWait('modal', 5101);
            console.warn("[wx-scheduler] job tick error: " + ((e && e.stack) || e)); /* KICLOUD: P3-I T14 with the stack */
        });
    }, 0);
});

extern "C" void EMSCRIPTEN_KEEPALIVE wxWasmJobTick()
{
    // Deliver ONE job per tick: a job that suspends (a click opening a modal)
    // parks THIS activation; the next job must not run beneath it on the same
    // activation, so re-arm and let a fresh tick (fresh activation) take it.
    if (wxWasmJspiJobs().empty())
        return;

    wxWasmJspiJob job = wxWasmJspiJobs().front();
    wxWasmJspiJobs().pop_front();

    if (!wxWasmJspiJobs().empty())
        wxWasmArmJspiJobTickJs();

    job.fn(job.arg);
}

extern "C" void wxWasmRunOnDispatchContext(void (*fn)(void *), void *arg)
{
    if (!fn)
        return;

    // WHERE the job runs matters: only a PROMISING activation may suspend;
    // the emscripten_set_*_callback DOM entries (canvas mouse/key/wheel/
    // touch) arrive as plain table calls and trap with SuspendError if a
    // handler below reaches a wait (#22493, observed on the context-menu
    // suite). So: on a tracked promising activation run directly; on a plain
    // entry, queue the job and let the promising job tick deliver it — the
    // same queued semantics the dispatch interlock already gives these
    // entries while a chain is parked, and the job lifetime contract
    // (heap-owned, abandoned == self-owned) is built for exactly this.
    // Callers that need an answer (a key handler's preventDefault) read it
    // from their own job struct; callers whose job suspends (a click that
    // opens a modal) get "returns while the work continues" semantics.
    if (wxWasmOnPromisingActivationJs())
    {
        fn(arg);
    }
    else
    {
        wxWasmJspiJobs().push_back({fn, arg});
        wxWasmArmJspiJobTickJs();
    }
}

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int rc)
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    // KICLOUD: W3.0P (E2.4; W3.0P lens 2): keep the code for Run() (wxGTK's m_exitcode)
    m_exitcode = rc;
    m_shouldExit = true;

    // The top-level loop is a plain while-loop that checks m_shouldExit (DoRun). A nested
    // (quasi-modal) loop blocks on its own wait.
    // KICLOUD: W3.0P (E2.4, wx's ScheduleExit contract; W3.0P lens 2): end THIS loop's
    // wait; PCBJam resolved the innermost "nested" wait whenever any loop but the top-level
    // one was exiting, which ended an inner loop instead of an outer one, and the top-level
    // loop's own ScheduleExit ended a nested loop. The loop returns once every blocking call
    // begun after it has returned (wxWasmNestedWait::Wait).
    if ( m_nestedWait )
        m_nestedWait->End(rc);
}

bool wxGUIEventLoop::Pending() const
{
    return wxTheApp && wxTheApp->HasPendingEvents();
}

bool wxGUIEventLoop::Dispatch()
{
    // Ungated on purpose: Dispatch()/wxYield run nested within the calling
    // handler chain (same C++ stack), which the interlock permits.
    if (wxTheApp)
        wxWasmProcessEventsUngated();
    return true;
}

int wxGUIEventLoop::DispatchTimeout(unsigned long WXUNUSED(timeout))
{
    // TODO: implement
    wxFAIL_MSG(wxT("DispatchTimeout is not implemented"));
    return 0;
}

void wxGUIEventLoop::WakeUp()
{
    // KICLOUD: PERF (docs/patches.md), D2: wxWakeUpIdle() (a queued event, CallAfter, a thread's
    // wxQueueEvent) asks the event-driven loop for a tick that also runs idle, as native wx wakes
    // its blocked loop. Any thread.
    wxWasmRequestTick(1);
}

void wxGUIEventLoop::DoYieldFor(long eventsToProcess)
{
    // Native wxYield semantics: due timers (and queued wheel ticks) run here
    // on behalf of the calling chain, even while it holds the interlock -
    // see wxWasmMailboxDeliverNested. Their handlers may post events, so
    // drain the mailbox before the pending-event loop.
    //
    // Only for the RunSynchronousAction spin signature: a yield that asks
    // for timer events from a chain that SLEPT at this depth since the last
    // delivery (`wxYield(); wxMilliSleep(1);`). The mask alone is not enough:
    // KiCad's symbol-editor boot issues a bare wxEVT_CATEGORY_TIMER yield at
    // depth 1 with timers pending, and running those nested hung the boot
    // (staging CI 2026-09-16). Boot and progress-dialog yields never sleep
    // between yields, so they never match; the paste spin matches from its
    // second iteration on (1 ms later).
    //
    // The sleep mark is consumed by the FIRST yield after it, whether or not
    // that yield qualifies: a sleep in some other loop (progress reporter,
    // simulator wait, a startup library wait) must not leave a mark behind
    // for an unrelated timer-capable yield later at the same depth.
    //
    // KICLOUD: W3.0P (K.11 retry, round r2; kicloud/TODO.md E2.5/E2.6), updated for the
    // adopted base (B1.2): the mark is set only by PCBJam's main-thread nanosleep shim
    // (wxWasmNoteSleep(), wasm/shims/nanosleep_yield.c), which the kicad_editor link includes,
    // so this delivery is live. Upstream KiCad's progress loops sleep between yields
    // (PROGRESS_REPORTER_BASE::KeepRefreshing(true): wxMilliSleep(33) between updateUI() calls
    // that end in DrainPendingEvents() = YieldFor(wxEVT_CATEGORY_TIMER)); PCBJam's KiCad fork
    // skips those updates on wasm.
    const bool sleptJustBefore = wxWasmMailboxSleptDepth == wxWasmDispatchDepth;
    wxWasmMailboxSleptDepth = -1;

    if (sleptJustBefore && (eventsToProcess & wxEVT_CATEGORY_TIMER))
        wxWasmMailboxDeliverNested();

    // KICLOUD: W3.0P (kicloud/TODO.md E2.5 and wx's YieldFor() contract, interface/wx/evtloop.h:
    // events outside the mask are "delayed (i.e. processed by the main loop later)"; W3.0P
    // lens 2 of the K.11 retry, round r1 review). PCBJam looped `while (Pending()) Dispatch();`.
    // Pending() is HasPendingEvents(), which stays true while an event outside the mask waits
    // (wxEvtHandler::ProcessPendingEvents() delays it and wxAppConsoleBase puts it back on the
    // pending list), so a selective yield never returned: KiCad's DrainPendingEvents()
    // (YieldFor(wxEVT_CATEGORY_TIMER) on every progress update) with a CallAfter pending froze
    // the page. Every pass also repainted, and every third one sent idle events, which wx never
    // does inside a selective yield. Like wxGTK and wxMSW, do the port's own work for the asked
    // categories once and leave wxEVT_CATEGORY_ALL's extra work to the base class:
    //  - the wx pending events, including the input the port queues while a chain is parked
    //    (wxApp::HandleMouseEvent/HandleKeyEvent, wxEVT_CATEGORY_USER_INPUT). Inside a yield,
    //    ProcessPendingEvents() processes only the events the mask allows and returns once only
    //    delayed ones are left, which it keeps for the main loop;
    //  - the repaint, the port's expose work (native ports put GDK_EXPOSE/WM_PAINT in
    //    wxEVT_CATEGORY_UI), only for a yield that asks for UI events;
    //  - wxEventLoopBase::DoYieldFor(): pending and idle events for wxEVT_CATEGORY_ALL only,
    //    "just once".
    // The guard is the one Dispatch() takes: a handler that suspends inside the yield keeps
    // the interlock held.
    if ( wxTheApp )
    {
        wxWasmDispatchGuard guard;

        wxTheApp->ProcessPendingEvents();

        if ( (eventsToProcess & wxEVT_CATEGORY_UI) && wxTheApp->GetTopWindow() )
            wxTheApp->Paint();
    }

    wxEventLoopBase::DoYieldFor(eventsToProcess);
}

int wxGUIEventLoop::DoRun()
{
    wxASSERT_MSG(IsOk(), wxT("invalid event loop"));

    wxWasmSchedulerAssertInstalled();

    // A nested loop (a quasi-modal dialog opened from a tool) suspends on a
    // registered wait; the first (top-level) DoRun runs the per-frame loop on
    // main()'s promising activation. Neither throws (see the header comment
    // and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // A nested loop is a registered "nested" wait that suspends whatever
        // activation is running — a tool coroutine's own activation included.
        // KICLOUD: W3.0P (E2.4; W3.0P lens 2): the loop's own wxWasmNestedWait, which its
        // ScheduleExit() ends; it returns once every blocking call begun after it has
        // returned, with the code ScheduleExit() was given (PCBJam: 0).
        wxWasmNestedWait wait("nested");
        if ( !wait.IsOk() )
        {
            // The scheduler refused the wait (dead or terminal instance): return
            // without touching the dispatch interlock, as before.
            --s_wxRunDepth;
            return 0;
        }
        m_nestedWait = &wait;

        // The opener's chain suspends here for the dialog's whole lifetime; the
        // interlock is zeroed for that whole span so the legitimate dispatcher keeps
        // running meanwhile (manual save/restore: wxWasmDispatchRestore centralizes
        // the erased-guard reporting).
        const int savedDispatchDepth = wxWasmDispatchDepth;
        wxWasmDispatchDepth = 0;
        const int result = wait.Wait();   // suspends until ended and innermost
        wxWasmDispatchRestore(savedDispatchDepth, "NestedLoop");

        m_nestedWait = NULL;
        --s_wxRunDepth;
        return result;
    }

    if (!wxTopLevelWindows.empty())
    {
        wxWindow *topWindow = wxTopLevelWindows.front();

        int width = EM_ASM_INT({
            return window.innerWidth;
        });
        int height = EM_ASM_INT({
            return window.innerHeight - mainWindow.offsetTop;
        });
        topWindow->SetSize(0, 0, width, height);
        topWindow->Refresh();
    }

    // One tick per animation frame. No throw (fatal under native wasm-EH):
    // this loop runs inline on main()'s promising activation, and the
    // per-frame wait suspends that activation until the next frame.
    // m_shouldExit, set by ScheduleExit(), ends the loop after the current tick.
    // KICLOUD: W3.0P (E2.4, wx's ScheduleExit contract: "after any nested loops terminate";
    // W3.0P lens 2): and only once no nested blocking call is left (PCBJam's ScheduleExit
    // ended the innermost nested loop instead); the ticks below dispatch the events that
    // end them.
    while (!m_shouldExit || wxWasmNestedWait::GetCount() > 0)
    {
        // Schedule, don't dispatch: see wxWasmScheduleProcessEvents. The tick's
        // events run from a fresh JS task while this loop is suspended below,
        // so a handler's own suspension never nests inside this activation.
        //
        // Unconditional at any DoRun depth: nested loops are waits, not pumps
        // (doc 17 S4), so gating on depth would leave a quasi-modal with no
        // dispatcher at all.
        wxWasmScheduleProcessEvents();

        // main() is a promising export; this loop's activation suspends for
        // exactly one animation frame per tick.
        // KICLOUD: PERF (docs/patches.md), D2: ... or, with nothing to do, until a wake asks for
        // the next tick (event-driven loop; see wxWasmRequestTick).
        wxWasmLoopWait();
    }
    --s_wxRunDepth;

    // S6 (doc 17): the main loop has ended — wx cleanup follows. Latch the
    // scheduler DEAD so already-queued ticks, messages, mutators, and wakes
    // are dropped/rejected loudly instead of delivering into teardown. Any
    // stranded work is beaconed ("shutdown ... stranded:"), which is the
    // visibility the plan's lifetime step asks for.
    EM_ASM({
        if (globalThis.__wxScheduler) globalThis.__wxScheduler.shutdown("main loop exited");
    });

    // KICLOUD: W3.0P (E2.4): the code given to Exit()/ScheduleExit(), as in wxGTK (PCBJam: 0)
    return m_exitcode;
}
