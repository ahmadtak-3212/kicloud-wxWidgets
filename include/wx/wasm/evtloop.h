///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:include/wx/wasm/evtloop.h (W3.0P; kicloud/docs/provenance.md)

#ifndef _WX_WASM_EVTLOOP_H_
#define _WX_WASM_EVTLOOP_H_

#include "wx/evtloop.h"

class WXDLLIMPEXP_FWD_CORE wxWasmNestedWait;

// ----------------------------------------------------------------------------
// wxGUIEventLoop for wxWebAssembly
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxGUIEventLoop : public wxEventLoopBase
{
public:
    // KICLOUD: W3.0P (E2.4): the exit code and the nested loop's wait (see below)
    wxGUIEventLoop() : m_exitcode(0), m_nestedWait(NULL) {}

    virtual bool IsOk() const { return true; }

    virtual void ScheduleExit(int rc = 0);
    virtual bool Pending() const;
    virtual bool Dispatch();
    virtual int DispatchTimeout(unsigned long timeout);
    virtual void WakeUp();

protected:
    virtual int DoRun();
    virtual void DoYieldFor(long eventsToProcess);

private:
    // KICLOUD: W3.0P (kicloud/TODO.md E2.4; W3.0P lens 2): Run() returns the code given to
    // Exit()/ScheduleExit(), as in wxGTK (PCBJam's DoRun returned 0), and a nested loop's
    // ScheduleExit() ends the loop's own wait (wx/wasm/private/yieldwait.h), set while its
    // DoRun() blocks.
    int m_exitcode;
    wxWasmNestedWait *m_nestedWait;

    wxDECLARE_NO_COPY_CLASS(wxGUIEventLoop);
};

#endif // _WX_WASM_EVTLOOP_H_
