/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/private.cpp
// Purpose      wxWasm private classes
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/app.h"
#endif // WX_PRECOMP

#include "wx/display.h"
#include "wx/private/display.h"
#include "wx/wasm/private/display.h"
#include "wx/wasm/pageframes.h"     // KICLOUD: S4.8 (display per attached window)

#include <emscripten.h>
#include <emscripten/html5.h>

double GetDevicePixelRatio()
{
    return emscripten_get_device_pixel_ratio();
}

int GetScreenWidth()
{
    return EM_ASM_INT({
        // Query DOM dynamically with fallback if mainWindow not ready
        if (typeof mainWindow !== 'undefined' && mainWindow) {
            return mainWindow.offsetWidth;
        }
        return 1280;  // Reasonable default fallback
    });
}

int GetScreenHeight()
{
    return EM_ASM_INT({
        // Query DOM dynamically with fallback if mainWindow not ready
        if (typeof mainWindow !== 'undefined' && mainWindow) {
            return mainWindow.offsetHeight;
        }
        return 720;  // Reasonable default fallback
    });
}

// ===========================================================================
// wxWasmDisplay
// ===========================================================================

wxWasmDisplay::wxWasmDisplay()
    : m_screenSize(GetScreenWidth(), GetScreenHeight()),
      m_deviceScaleFactor(GetDevicePixelRatio()),
      m_contentScaleFactor(m_deviceScaleFactor >= 1.5 ? 2.0 : 1.0)
{
}

wxSize wxWasmDisplay::GetScreenSize() const
{
    // Query DOM fresh each time instead of returning cached value.
    // This matches how GTK/MSW ports work - they always query native APIs.
    // Fixes the bug where GetClientSize() returns 20x20 if called before Show().
    return wxSize(GetScreenWidth(), GetScreenHeight());
}

void wxWasmDisplay::UpdateScaleFactor()
{
    m_deviceScaleFactor = GetDevicePixelRatio();
    m_contentScaleFactor = m_deviceScaleFactor >= 1.5 ? 2.0 : 1.0;
}

const int DEFAULT_DEPTH = 32;

// ----------------------------------------------------------------------------
// display characteristics
// ----------------------------------------------------------------------------

class wxDisplayImplSingleWasm : public wxDisplayImplSingle
{
public:
    virtual wxRect GetGeometry() const wxOVERRIDE
    {
        wxSize screenSize = wxTheApp->GetDisplay()->GetScreenSize();
        return wxRect(0, 0, screenSize.x, screenSize.y);
    }

    virtual int GetDepth() const wxOVERRIDE
    {
        return DEFAULT_DEPTH;
    }

    virtual wxSize GetPPI() const wxOVERRIDE
    {
        // CSS reference pixel size is 1/96 in
        // see http://www.w3.org/TR/css3-values/#reference-pixel
        const double ppi = 96.0;
        return wxSize(ppi, ppi);
    }
};

// KICLOUD: S4.8 (docs/patches.md, wx/wasm/pageframes.h): an attached browser window (a torn-off
// editor tab) is a display of its own: display n >= 1 is the n-th attached slot, in ascending
// order, with that slot's screen area. KiCad and wx place dialogs on "their" display
// (wxTopLevelWindow::DoCentre centres on the parent's display), so a torn-off editor's dialogs open
// in its window. The geometry is read when asked, as the window can be resized at any time.
class wxDisplayImplSlotWasm : public wxDisplayImpl
{
public:
    wxDisplayImplSlotWasm(unsigned n, int slot) : wxDisplayImpl(n), m_slot(slot) { }

    virtual wxRect GetGeometry() const wxOVERRIDE { return wxWasmSlotRect(m_slot); }
    virtual int GetDepth() const wxOVERRIDE { return DEFAULT_DEPTH; }
    virtual wxSize GetPPI() const wxOVERRIDE { return wxSize(96, 96); }
    virtual bool IsPrimary() const wxOVERRIDE { return false; }

#if wxUSE_DISPLAY
    virtual wxArrayVideoModes GetModes(const wxVideoMode& WXUNUSED(mode)) const wxOVERRIDE
    {
        return wxArrayVideoModes();
    }
    virtual wxVideoMode GetCurrentMode() const wxOVERRIDE { return wxVideoMode(); }
    virtual bool ChangeMode(const wxVideoMode& WXUNUSED(mode)) wxOVERRIDE { return false; }
#endif // wxUSE_DISPLAY

private:
    const int m_slot;
};

double wxDisplayScaleFactor()
{
    return wxTheApp->GetDisplay()->GetDeviceScaleFactor();
}

double wxContentScaleFactor()
{
    return wxTheApp->GetDisplay()->GetContentScaleFactor();
}

// KICLOUD: S4.8: display 0 is the page; displays 1.. are the attached slots (wx/wasm/pageframes.h).
// wxDisplay::InvalidateCache() is called whenever a slot comes or goes (src/wasm/toplevel.cpp), so
// the cached displays always match the slots.
class wxDisplayFactorySingleWasm : public wxDisplayFactory
{
public:
    virtual unsigned GetCount() wxOVERRIDE
    {
        return 1 + static_cast<unsigned>(wxWasmAttachedSlots().size());
    }

    virtual int GetFromPoint(const wxPoint& pt) wxOVERRIDE
    {
        const wxVector<int> slots = wxWasmAttachedSlots();

        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (wxWasmSlotRect(slots[i]).Contains(pt))
                return static_cast<int>(i + 1);
        }

        return wxWasmSlotRect(0).Contains(pt) ? 0 : wxNOT_FOUND;
    }

    // The base implementation bails out with wxNOT_FOUND for windows whose
    // GetHandle() is null — which in this port is EVERY window (wxWindowWasm
    // has no native handle; windows are DOM-backed). Callers persisting window
    // geometry (e.g. KiCad's SaveWindowSettings) then store display = -1 and
    // their restore path treats the position as invalid and re-centres the
    // frame on every reopen. A window is on the display of its slot (0, the
    // page, unless its frame is torn off).
    virtual int GetFromWindow(const wxWindow *window) wxOVERRIDE
    {
        if (!window)
            return wxNOT_FOUND;

        const int slot = wxWasmWindowSlot(window);

        if (slot == 0)
            return 0;

        const wxVector<int> slots = wxWasmAttachedSlots();

        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i] == slot)
                return static_cast<int>(i + 1);
        }

        return 0;
    }

protected:
    virtual wxDisplayImpl *CreateDisplay(unsigned n) wxOVERRIDE
    {
        if (n == 0)
            return new wxDisplayImplSingleWasm();

        const wxVector<int> slots = wxWasmAttachedSlots();

        if (n - 1 < slots.size())
            return new wxDisplayImplSlotWasm(n, slots[n - 1]);

        return new wxDisplayImplSingleWasm();
    }
};

wxDisplayFactory *wxDisplay::CreateFactory()
{
    return new wxDisplayFactorySingleWasm();
}

