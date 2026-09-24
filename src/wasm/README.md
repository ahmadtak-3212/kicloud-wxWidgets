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
port uses a generic class, so do we. JavaScript glue lives in `src/wasm/js/`
(`wx.js` is the Emscripten `--js-library`, `wx-dom.js` the runtime module;
added by W3.3).

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

## Status

W3.1: toolkit wiring and the wxBase build only; the groups above are empty.
W3.1b adds every class as a stub (link closure); W3.3–W3.16 implement them.
