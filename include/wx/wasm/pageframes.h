/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/pageframes.h
// Purpose:     page frames: top-level frames that each fill the page, one at a time
// Author:      kicloud contributors
// Copyright:   (c) 2026 kicloud contributors
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

// True when the page asked for page frames (Module.wxPageFrames).
WXDLLIMPEXP_CORE bool wxWasmPageFramesEnabled();

// The next top-level window created becomes the page frame `key`; nullptr or "" clears it.
// Ignored when page frames are off.
WXDLLIMPEXP_CORE void wxWasmSetNextPageFrame(const char* key);

#endif // _WX_WASM_PAGEFRAMES_H_
