# wxWASM: the WebAssembly port of wxWidgets (kicloud)

KICLOUD: W3.1 (kicloud/docs/patches.md). This directory holds the wxWASM toolkit
port used by kicloud to run KiCad in the browser. It is a **native toolkit
port** (`__WXWASM__`), not wxUniversal: simple controls are real DOM elements,
owner-drawn and generic widgets paint through a Canvas2D-backed `wxDC`, and
layout stays in wx sizers (kicloud/TODO.md B4 and E3).

## Layout and the Qt-port mirroring rule

The port mirrors the Qt port (`src/qt`, `include/wx/qt`) one-for-one:

| Qt port | wxWASM port |
|---|---|
| `src/qt/<x>.cpp` | `src/wasm/<x>.cpp` |
| `include/wx/qt/<x>.h` | `include/wx/wasm/<x>.h` |
| `include/wx/qt/private/*.h` | `include/wx/wasm/private/*.h` |
| `#elif defined(__WXQT__)` + `#include "wx/qt/<x>.h"` in `include/wx/*.h` | `#elif defined(__WXWASM__)` + `#include "wx/wasm/<x>.h"` |
| generic implementation chosen by Qt (`wxUSE_GENERIC_*`, generic dialogs, list/tree/dataview controls) | the same generic implementation |

Every class the Qt port declares gets a file of the same name here; where the Qt
port uses a generic class, so do we. The JavaScript runtime lives in
`src/wasm/js/` (KICLOUD: W3.0P, imported from PCBJam): `wx.js` (element registry
for tests, browser/platform info, window chrome), `wx-dom.js` (the native DOM
controls) and `scheduler.js` (the JSPI scheduler and wait registry the C++ calls
as `globalThis.__wxScheduler`). They are `--pre-js` files, installed to
`<prefix>/share/wxwidgets/wasm/`; `wx-config --libs` prints them for GUI
libraries (`build/cmake/config.cmake`). `docs/` holds the port's design notes.

## Build wiring (wx CMake build only; kicloud/TODO.md B5)

- `build/cmake/toolkit.cmake`: an Emscripten toolchain selects the `wasm`
  toolkit by default (`wxBUILD_TOOLKIT=wasm`, CMake variable `WXWASM`, define
  `__WXWASM__`) and links no toolkit libraries.
- `build/cmake/init.cmake`: the browser feature matrix (file system watcher,
  secret store, dial-up, sound, media control, joystick and task bar icon are
  forced off; OpenGL is WebGL 2 through emscripten's GL library).
- `build/files` → `build/cmake/files.cmake` (via `build/upmake`; only
  `files.cmake` is committed, never bakefile/autoconf outputs):
  `WASM_LOWLEVEL_SRC/HDR` (app, event loop, DC, bitmap, ...),
  `WASM_SRC/HDR` (native DOM controls), `OPENGL_WASM_SRC/HDR` (`wxGLCanvas`).
- `build/cmake/lib/core/CMakeLists.txt` appends `WASM_LOWLEVEL` and `WASM` to
  wxcore; `build/cmake/lib/gl/CMakeLists.txt` appends `OPENGL_WASM` to wxgl.

Build with `kicloud/scripts/build-wx-wasm.sh` (uniform kicloud ABI from
`kicloud/toolchain/env.sh`; heavy lock; stamped install into the kicloud
sysroot). `--gui off` builds wx's base-only configuration (wxbase, wxnet,
wxxml); `--gui on` builds every library with this toolkit (kicloud W3.1b).

## Rules for code in this port

- **Base vs GUI:** code compiled into the base libraries (wxbase, wxnet,
  wxxml) must test `__EMSCRIPTEN__`, never `__WXWASM__`: base-only builds
  (`wxUSE_GUI=OFF`) define `__WXBASE__` instead, and the base library must be
  the same in both configurations. `__WXWASM__` is for GUI code.
- **Upstream edits outside this directory** are a last resort: each one gets a
  `KICLOUD:` comment and a row in `kicloud/docs/patches.md`, except pure
  dispatch lines (`defined(__WXWASM__)` added to a toolkit guard or dispatch
  chain), which are listed there as one class.
- **Reused code** (PCBJam, Hilss wxWidgets-wasm) keeps its licence header, gets
  a `KICLOUD: adapted from <repo>@<commit>:<path>` line and a row in
  `kicloud/docs/provenance.md`. Adapted port code is LGPL v2 without the
  wxWindows exception; wholly original files may use the wxWindows Licence.
  One exception: `js/scheduler.js` comes from PCBJam's main repository
  (`scripts/common/shims/jspi-scheduler.js`), which is GPL-3.0; it keeps that
  licence (kicloud ADR 0005; the combined browser application is GPLv3).

## Pages that host the port

A page must provide `#main-window` wrapping the 2D `#canvas` (the main frame
paints into it and it defines the wx screen origin: wx screen coordinates are
`#canvas`-relative CSS px), `#window-container` for other top-level windows, and
a global `mainWindow` (the display size). The JSPI promising exports (DOM/mouse
events, window move/close/resize, the scheduler ticks) are marked
`KICLOUD-JSPI-EXPORT` and reach `-sJSPI_EXPORTS` through
`kicloud/toolchain/jspi-exports.txt`. See `kicloud/tests/wx/smoke/`.

Link: `wx-config --libs` prints the three `--pre-js` runtime files and the
archives, and no `-s` setting. The emscripten runtime pieces the JavaScript
uses (`ccall`, `stackSave`/`stackRestore`, the `HEAP*` views, ...) are declared
with `EM_JS_DEPS` in `app.cpp`, and the JavaScript calls the glue's in-scope
names, so a program may set its own `EXPORTED_RUNTIME_METHODS` and similar
list settings.

Browser entries (TODO.md E2.1/E2.2): the html5 callbacks registered in
`app.cpp` (keyboard, mouse, wheel, touch, window resize, window focus/blur)
are plain entries that cannot suspend, so they only package the event and hand
it to the scheduler as a DOM job (`wxWasmRunDomJob`), which runs wx handlers on
a promising activation. `beforeunload` runs no wx code at all: the page asks
the browser to confirm leaving while any top-level window is marked modified
with `wxTopLevelWindow::OSXSetModified(true)` (published to
`globalThis.__wxModifiedWindows`); a program that wants the prompt keeps that
flag current. No `wxEVT_CLOSE_WINDOW` is sent when the page unloads.

Nested blocking calls (TODO.md E2.4/E2.8): a nested `wxGUIEventLoop::Run()`
and `wxDialog::ShowModal()` suspend their chain on a scheduler wait of their
own (`wxWasmNestedWait`, `include/wx/wasm/private/yieldwait.h`); the top-level
tick keeps dispatching meanwhile. `ScheduleExit()`/`Exit()` and `EndModal()`
end their own call's wait, and a call returns only once every blocking call
begun after it has returned (wx's `ScheduleExit()` contract: "after any nested
loops terminate"), so ending an outer quasi-modal or modal while an inner one
runs is safe. `Run()` returns the code given to `Exit()`/`ScheduleExit()`, and
the top-level loop exits only once no nested call is left. `ShowModal()`
disables no other window (native ports use a `wxWindowDisabler`); the page's
modal barrier only blocks windows an upper one overlaps.

Yields (TODO.md E2.5): `wxYield()`/`YieldFor(mask)` run on the caller's stack
and return. `wxGUIEventLoop::DoYieldFor()` processes the pending wx events once.
That includes the input the port queues while a dispatch chain is parked. Only
the categories in the mask are processed (wx's `YieldFor()` contract); the
others stay pending for the main loop. The yield repaints only when the mask
has `wxEVT_CATEGORY_UI`, and the wx base sends idle events only for
`wxEVT_CATEGORY_ALL`, once. KiCad's `DrainPendingEvents()` and the generic
progress dialog therefore return with, for example, a CallAfter still pending.
Due timers from the mailbox run inside a yield only right after the calling
chain slept at the same dispatch depth: KiCad's RunSynchronousAction spin,
`wxYield(); wxMilliSleep(1);`. The sleep is noted by `wxWasmNoteSleep()`, which
PCBJam calls from its main-thread `nanosleep` shim
(`pcbjam/wasm/shims/nanosleep_yield.c`). kicloud does not link that shim yet
(TODO.md E2.6; W3.0P inventory row E2, owner W3.4). Until it does, the nested
timer delivery never runs, and a main-thread sleep busy-waits
(`emscripten_thread_sleep()`) without letting the browser run.

## Status

W3.1: toolkit wiring and the wxBase build. W3.0P: PCBJam's DOM port
(`pcbjam/wxwidgets@8bad5f58e9`) imported with provenance
(`kicloud/docs/decisions/W3.0P-import-inventory.md`,
`kicloud/docs/provenance.md`); every GUI library builds and a smoke app runs in
Chromium and Firefox. W3.1b–W3.16 verify and complete it against their own
gates (coverage table in `kicloud/progress/W3.0P.md`).
