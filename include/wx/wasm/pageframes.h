/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/pageframes.h
// Purpose:     page frames: top-level frames that each fill the page, one at a time
// Author:      Ahmad Taka
// Copyright:   (c) 2026 Ahmad Taka
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// KICLOUD: B1.6d (docs/patches.md). With page frames, one application hosts several full
// editor windows in one browser page, as tabs: a page frame has no DOM title bar, always fills
// the page, and showing one hides the page frame that was shown (and the non-modal windows
// that belong to it). The page follows them through window.wxWasmPageFrame(event, key, title),
// called with "shown", "title" and "closed".
//
// Page frames are on when the page sets Module.wxPageFrames before the runtime starts. The
// application then marks the next top-level window it creates as a page frame by calling
// wxWasmSetNextPageFrame() with the key the page knows it by, just before creating it.

#ifndef _WX_WASM_PAGEFRAMES_H_
#define _WX_WASM_PAGEFRAMES_H_

#include "wx/defs.h"
#include "wx/gdicmn.h"     // KICLOUD: S4.8 (wxRect)
#include "wx/vector.h"     // KICLOUD: S4.8

// True when the page asked for page frames (Module.wxPageFrames).
WXDLLIMPEXP_CORE bool wxWasmPageFramesEnabled();

// The next top-level window created becomes the page frame `key`; nullptr or "" clears it.
// Ignored when page frames are off.
WXDLLIMPEXP_CORE void wxWasmSetNextPageFrame(const char* key);

// ----------------------------------------------------------------------------
// KICLOUD: S4.8 (docs/patches.md): attached windows ("frame slots")
// ----------------------------------------------------------------------------
//
// A page frame can be drawn in a second browser window (an editor tab torn off), while the
// application keeps running in the page. The page registers that window with wx.js
// (window.wxAttachFrameDocument(win), which returns a slot number >= 1) and then moves the page
// frame there with wxWasmAttachPageFrame(key, slot). Slot 0 is the page itself.
//
// wx screen coordinates stay one flat space: slot n covers the area that starts at
// (n * wxWASM_SLOT_STRIDE, 0) and is as large as that window. A top-level window belongs to the
// slot of its top-level ancestor, so the page frame's dialogs, popups and GL canvases follow it.
// Page frames are shown one at a time per slot: two page frames can be on screen at once, one
// in the page and one in an attached window.

class WXDLLIMPEXP_FWD_CORE wxWindow;

// The x distance between the origins of two neighbouring slots, in wx screen px.
#define wxWASM_SLOT_STRIDE 65536

// Move the page frame `key` into slot `slot` (0: back into the page) with its dialogs, floating
// windows and GL canvases, size it to that slot and show it there. Moving it back to the page
// hides it: the page then shows it as a tab again (Show), or keeps the frame it shows. Returns
// false when no page frame has that key. Must run on the event loop (not inside a paint).
WXDLLIMPEXP_CORE bool wxWasmAttachPageFrame(const char* key, int slot);

// The slot (0: the page) the window `win` is drawn in: the slot of its top-level ancestor.
WXDLLIMPEXP_CORE int wxWasmWindowSlot(const wxWindow* win);

// The area of slot `slot` in wx screen coordinates (its window's viewport size). Slot 0 is the
// page (the display's screen size).
WXDLLIMPEXP_CORE wxRect wxWasmSlotRect(int slot);

// The attached slots, ascending (slot 0 not included).
WXDLLIMPEXP_CORE wxVector<int> wxWasmAttachedSlots();

#endif // _WX_WASM_PAGEFRAMES_H_
