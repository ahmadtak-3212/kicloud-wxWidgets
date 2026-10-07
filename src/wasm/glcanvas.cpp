///////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/glcanvas.cpp
// Purpose:     wxGLCanvas for WebAssembly using WebGL via Emscripten
// Author:      KiCad WASM Project
// Created:     2024
// Copyright:   (c) 2024
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

#include "wx/wxprec.h"

#if wxUSE_GLCANVAS

#include "wx/glcanvas.h"
#include "wx/toplevel.h"     // KICLOUD: B1.6d
#include "wx/gdicmn.h"       // KICLOUD: A10, wxDisplayScaleFactor

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/app.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#endif

// ============================================================================
// wxGLContextAttrs implementation
// ============================================================================

wxGLContextAttrs& wxGLContextAttrs::CoreProfile()
{
    // WebGL doesn't support core profiles in the same way
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MajorVersion(int val)
{
    if ( val > 0 )
    {
        AddAttribute(WX_GL_MAJOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MinorVersion(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_MINOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::CompatibilityProfile()
{
    // WebGL doesn't support compatibility profiles
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ForwardCompatible()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ES2()
{
    AddAttribute(WX_GL_ES2);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::DebugCtx()
{
    AddAttribute(WX_GL_DEBUG);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::Robust()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::NoResetNotify()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::LoseOnReset()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ResetIsolation()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ReleaseFlush(int)
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::PlatformDefaults()
{
    return *this;
}

void wxGLContextAttrs::EndList()
{
    AddAttribute(0);
}

// ============================================================================
// wxGLAttributes implementation
// ============================================================================

wxGLAttributes& wxGLAttributes::RGBA()
{
    AddAttribute(WX_GL_RGBA);
    return *this;
}

wxGLAttributes& wxGLAttributes::BufferSize(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_BUFFER_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Level(int val)
{
    AddAttribute(WX_GL_LEVEL);
    AddAttribute(val);
    return *this;
}

wxGLAttributes& wxGLAttributes::DoubleBuffer()
{
    AddAttribute(WX_GL_DOUBLEBUFFER);
    return *this;
}

wxGLAttributes& wxGLAttributes::Stereo()
{
    // WebGL doesn't support stereo
    return *this;
}

wxGLAttributes& wxGLAttributes::AuxBuffers(int)
{
    // WebGL doesn't support aux buffers
    return *this;
}

wxGLAttributes& wxGLAttributes::MinRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    if ( mRed >= 0 )
    {
        AddAttribute(WX_GL_MIN_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0 )
    {
        AddAttribute(WX_GL_MIN_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0 )
    {
        AddAttribute(WX_GL_MIN_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0 )
    {
        AddAttribute(WX_GL_MIN_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Depth(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_DEPTH_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Stencil(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_STENCIL_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinAcumRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    // WebGL doesn't support accumulation buffers
    wxUnusedVar(mRed);
    wxUnusedVar(mGreen);
    wxUnusedVar(mBlue);
    wxUnusedVar(mAlpha);
    return *this;
}

wxGLAttributes& wxGLAttributes::PlatformDefaults()
{
    // Use WebGL 2.0 by default (ES 3.0)
    return *this;
}

wxGLAttributes& wxGLAttributes::Defaults()
{
    RGBA().DoubleBuffer().Depth(16);
    return *this;
}

wxGLAttributes& wxGLAttributes::SampleBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLE_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Samplers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLES);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::FrameBuffersRGB()
{
    AddAttribute(WX_GL_FRAMEBUFFER_SRGB);
    return *this;
}

void wxGLAttributes::EndList()
{
    AddAttribute(0);
}

void wxGLAttributes::AddDefaultsForWXBefore31()
{
    Defaults();
    EndList();
}

// ============================================================================
// wxGLContext implementation
// ============================================================================

wxIMPLEMENT_CLASS(wxGLContext, wxObject);

wxGLContext::wxGLContext(wxGLCanvas *win,
                         const wxGLContext *other,
                         const wxGLContextAttrs *ctxAttrs)
    : m_glContext(0)
{
    wxUnusedVar(other);     // Context sharing not yet implemented
    wxUnusedVar(ctxAttrs);  // Context attributes handled in canvas

    m_isOk = false;

    if ( !win )
    {
        wxLogError("wxGLContext: NULL window specified");
        return;
    }

    // Get the context from the canvas (it's created there for WebGL)
    m_glContext = win->GetWebGLContext();
    m_isOk = (m_glContext > 0);

    if ( !m_isOk )
    {
        wxLogError("wxGLContext: Failed to get WebGL context from canvas");
    }
}

wxGLContext::~wxGLContext()
{
    // Context is owned by the canvas in WebGL, don't destroy here
    m_glContext = 0;
}

bool wxGLContext::SetCurrent(const wxGLCanvas& win) const
{
    if ( !m_isOk )
        return false;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = win.GetWebGLContext();
    if ( ctx <= 0 )
        return false;

    EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(ctx);
    if ( result != EMSCRIPTEN_RESULT_SUCCESS )
        return false;

    // Initialize GLImmediate if not already done
    // This is needed because GLImmediate.init() is normally called only for the
    // main canvas context, but wxGLCanvas creates its own separate WebGL context.
    // Without this, GL emulation functions like glEnable crash with null texUnits.
    //
    // We need to set Browser.useWebGL=true first, because GLImmediate.init() checks
    // this flag and returns early without initializing TexEnvJIT if it's false.
    // The main wxWidgets canvas uses 2D context, so Browser.useWebGL would be false.
    EM_ASM({
        if (typeof GLImmediate !== 'undefined' && !GLImmediate.initted) {
            // Save old value and set useWebGL to true for init
            var oldUseWebGL = Browser.useWebGL;
            Browser.useWebGL = true;
            GLImmediate.init();
            // Restore original value
            Browser.useWebGL = oldUseWebGL;
        }
    });

    return true;
}

// ============================================================================
// wxGLCanvas implementation
// ============================================================================

wxIMPLEMENT_CLASS(wxGLCanvas, wxWindow);

void wxGLCanvas::Init()
{
    m_webglContext = 0;
    m_cssId = wxID_NONE;
    m_canvasTarget = "";  // Will be set dynamically in Create()
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       const wxGLAttributes& dispAttrs,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    Init();
    Create(parent, dispAttrs, id, pos, size, style, name, palette);
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       wxWindowID id,
                       const int *attribList,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    Init();
    Create(parent, id, pos, size, style, name, attribList, palette);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        const wxGLAttributes& dispAttrs,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const wxPalette& palette)
{
    wxUnusedVar(palette);

    if ( !wxWindow::Create(parent, id, pos, size, style, name) )
        return false;

    // Create a dedicated GL canvas element in JavaScript
    // This canvas is separate from the 2D UI canvas to avoid context conflicts
    m_cssId = EM_ASM_INT({
        return createGLCanvas(true);
    });

    // KICLOUD: a GL canvas of the main frame or of a page frame (an editor tab, B1.6d) stacks
    // just above its window, below floating windows and dialogs; one of any other top-level
    // window (the 3D viewer) stays above everything, as createGLCanvas decides for it
    wxTopLevelWindow* tlw = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    if (tlw && (tlw->IsMainFrame() || tlw->IsPageFrame()))
    {
        EM_ASM({ setGLCanvasZ($0, 100); }, m_cssId);
    }

    // Position the GL canvas element to match this window's screen position
    SyncCanvasElement();    // KICLOUD: A10

    // Set canvas selector to point to our dedicated GL canvas element
    char selectorBuf[64];
    snprintf(selectorBuf, sizeof(selectorBuf), "#glcanvas-%d", m_cssId);
    m_canvasTarget = selectorBuf;

    return CreateWebGLContext(dispAttrs);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const int *attribList,
                        const wxPalette& palette)
{
    wxGLAttributes dispAttrs;
    if ( attribList )
    {
        ParseAttribList(attribList, dispAttrs, &m_GLCTXAttrs);
    }
    else
    {
        dispAttrs.Defaults().EndList();
    }

    return Create(parent, dispAttrs, id, pos, size, style, name, palette);
}

wxGLCanvas::~wxGLCanvas()
{
    if ( m_webglContext > 0 )
    {
        // KICLOUD: P3-I T14 lose the context before Emscripten forgets it: destroying it only
        // drops it from Emscripten's table, and the browser kept its drawing buffers and GPU
        // objects until a garbage collection (each PCB 3D viewer open/close kept ~40-90 MB)
        EM_ASM({
            var c = (typeof GL !== 'undefined' && GL.contexts) ? GL.contexts[$0] : null;
            var gl = c && c.GLctx;
            if (gl && !gl.isContextLost()) {
                var ext = gl.getExtension('WEBGL_lose_context');
                if (ext) ext.loseContext();
            }
        }, m_webglContext);
        emscripten_webgl_destroy_context(m_webglContext);
        m_webglContext = 0;
    }

    // Destroy the GL canvas element we created
    if ( m_cssId != wxID_NONE )
    {
        EM_ASM({
            destroyGLCanvas($0);
        }, m_cssId);
        m_cssId = wxID_NONE;
    }
}

void wxGLCanvas::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);

    // Update the GL canvas element position to match the window
    if ( m_cssId != wxID_NONE )
        SyncCanvasElement();    // KICLOUD: A10
}

// KICLOUD: A10 (docs/patches.md, docs/future-features/FEATURE_LOOKS.md R1): the scale KiCad's
// GAL draws this canvas at. The rest of the port reports only 1x or 2x (src/wasm/display.cpp:
// window.devicePixelRatio >= 1.5 ? 2 : 1), which is right for its 2D canvases and bitmaps but
// made the board blurry at a device pixel ratio of 1.25 or 1.5: KiCad drew at 1x or 2x and the
// browser resampled the picture to the screen every frame. HIDPI_GL_CANVAS::GetScaleFactor()
// (kicad/common/gal/hidpi_gl_canvas.cpp) asks this, so its framebuffer, viewport and mouse
// mapping all use the real ratio. wxDisplayScaleFactor() is window.devicePixelRatio, refreshed
// on every page resize (wxApp::HandleSizeEvent), which a browser zoom also sends. At a ratio of
// 1 or 2 this is the value the port reported before, so those screens draw exactly as before.
double wxGLCanvas::GetDPIScaleFactor() const
{
    return wxDisplayScaleFactor();
}

// KICLOUD: A10: places this canvas's own <canvas> element over the window and sizes its
// backing store (wx.js setGLCanvasRect). The backing store is the size KiCad's GAL renders,
// computed here with KiCad's own expression, the CSS client size times the scale truncated to
// an int (HIDPI_GL_CANVAS::GetNativePixelSize: `size.x *= scaleFactor`), so the drawing buffer
// is exactly what KiCad fills: one device pixel per drawn pixel, no resampling, no unpainted
// edge. JS shows it at backing / scale CSS px (whole device pixels, at most one device pixel
// smaller than the wx client size) and moves it by less than one device pixel so its corner sits
// on a device pixel. Called at creation and on every size or position change (DoSetSize, and
// wxWindowWasm::UpdateDomGeometryRecursive when an ancestor moves).
void wxGLCanvas::SyncCanvasElement()
{
    const wxPoint screenPos = GetScreenPosition();
    const wxSize clientSize = GetClientSize();
    const double scale = GetDPIScaleFactor();

    wxSize backing = clientSize;
    backing.x *= scale;
    backing.y *= scale;

    EM_ASM({
        setGLCanvasRect($0, $1, $2, $3, $4, $5, $6, $7);
    }, m_cssId, screenPos.x, screenPos.y, clientSize.GetWidth(), clientSize.GetHeight(),
       backing.x, backing.y, scale);
}

bool wxGLCanvas::Show(bool show)
{
    bool result = wxWindow::Show(show);

    // Update the GL canvas element visibility based on actual on-screen visibility.
    // This accounts for parent visibility - if any parent is hidden, the GL canvas
    // DOM element should be hidden too, even if this window's Show() is set to true.
    if ( m_cssId != wxID_NONE )
    {
        // Use IsShownOnScreen() to check both this window AND parent visibility
        bool actuallyVisible = IsShownOnScreen();
        EM_ASM({
            setGLCanvasVisibility($0, $1);
        }, m_cssId, actuallyVisible);
    }

    return result;
}

bool wxGLCanvas::CreateWebGLContext(const wxGLAttributes& dispAttrs)
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    // Convert wxGL attributes to WebGL attributes
    ConvertWXAttrsToWebGL(dispAttrs, attrs);

    // Create the WebGL context on the canvas
    m_webglContext = emscripten_webgl_create_context(m_canvasTarget.c_str(), &attrs);

    if ( m_webglContext <= 0 )
    {
        wxLogError("Failed to create WebGL context on canvas '%s' (error: %d)",
                   m_canvasTarget, m_webglContext);
        return false;
    }

    // Make it current immediately
    EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(m_webglContext);
    if ( result != EMSCRIPTEN_RESULT_SUCCESS )
    {
        wxLogError("Failed to make WebGL context current (error: %d)", result);
        emscripten_webgl_destroy_context(m_webglContext);
        m_webglContext = 0;
        return false;
    }

    return true;
}

// KICLOUD: PERF (docs/patches.md), D4: whether the page asked for drawing buffers that survive
// compositing (globalThis.__wxGlPreserveDrawingBuffer = true, or URL ?glpreserve=1). Tests that
// read a WebGL canvas back (drawImage of the canvas: tests/lib/stable-shot.ts, tests/lib/canvas.ts)
// set it for every page; the product does not.
EM_JS(int, wxWasmGlPreserveDrawingBufferJs, (), {
    try {
        if (globalThis.__wxGlPreserveDrawingBuffer === true) return 1;
        if (typeof location !== "undefined" && /[?&]glpreserve=1\b/.test(String(location.search || ""))) return 1;
        if (typeof parent !== "undefined" && parent !== globalThis && parent.__wxGlPreserveDrawingBuffer === true) return 1;
    } catch (e) { /* a cross-origin parent */ }
    return 0;
});

/* static */
void wxGLCanvas::ConvertWXAttrsToWebGL(const wxGLAttributes& dispAttrs,
                                        EmscriptenWebGLContextAttributes& attrs)
{
    // Set sensible defaults for WebGL
    attrs.alpha = true;
    attrs.depth = true;
    attrs.stencil = false;
    // KICLOUD: PERF (docs/patches.md), D4: no multisampled default framebuffer unless the
    // canvas asks for one (WX_GL_SAMPLE_BUFFERS/WX_GL_SAMPLES below: the 3D viewer's
    // anti-aliasing setting). KiCad's 2D canvas draws into its own framebuffers, smooths them
    // with SMAA and copies the result with one full-screen quad, so a multisampled default
    // framebuffer only cost GPU memory and a resolve every frame (PCBJam: always true).
    attrs.antialias = false;
    attrs.premultipliedAlpha = true;
    // Keep the drawing buffer after compositing. The 3D viewer raytraces a static
    // frame and then stops redrawing; without this the buffer is cleared once the
    // render settles, so screenshots/read-back (and an idle re-composite) capture an
    // empty canvas even though the frame rendered correctly.
    // KICLOUD: PERF (docs/patches.md), D4: only when the page asks for it (tests that read the
    // canvas back). What is on screen stays: the browser keeps showing the last presented frame
    // until the next draw, and every KiCad repaint redraws the whole default framebuffer. Without
    // preservation the browser can hand the buffer over instead of copying it every frame.
    attrs.preserveDrawingBuffer = wxWasmGlPreserveDrawingBufferJs() ? true : false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.failIfMajorPerformanceCaveat = false;
    attrs.majorVersion = 2;  // WebGL 2.0 by default
    attrs.minorVersion = 0;
    attrs.enableExtensionsByDefault = true;

    // Parse the attribute list
    const int* attrList = dispAttrs.GetGLAttrs();
    if ( !attrList )
        return;

    for ( int i = 0; attrList[i]; i++ )
    {
        switch ( attrList[i] )
        {
            case WX_GL_RGBA:
                attrs.alpha = true;
                break;

            case WX_GL_DOUBLEBUFFER:
                // WebGL always double buffers
                break;

            case WX_GL_DEPTH_SIZE:
                i++;
                attrs.depth = (attrList[i] > 0);
                break;

            case WX_GL_STENCIL_SIZE:
                i++;
                attrs.stencil = (attrList[i] > 0);
                break;

            case WX_GL_MIN_ALPHA:
                i++;
                attrs.alpha = (attrList[i] > 0);
                break;

            case WX_GL_SAMPLE_BUFFERS:
            case WX_GL_SAMPLES:
                i++;
                attrs.antialias = (attrList[i] > 0);
                break;

            case WX_GL_MAJOR_VERSION:
                i++;
                attrs.majorVersion = attrList[i];
                break;

            case WX_GL_MINOR_VERSION:
                i++;
                attrs.minorVersion = attrList[i];
                break;

            case WX_GL_ES2:
                // Force WebGL 1.0 (ES 2.0)
                attrs.majorVersion = 1;
                break;

            default:
                // Skip unknown attributes and their values
                if ( attrList[i] != WX_GL_RGBA &&
                     attrList[i] != WX_GL_DOUBLEBUFFER )
                {
                    // Most attributes have a value following them
                    i++;
                }
                break;
        }
    }
}

bool wxGLCanvas::SwapBuffers()
{
    // WebGL automatically swaps buffers at the end of each frame
    // when using requestAnimationFrame or when the JavaScript event
    // loop yields. This is effectively a no-op.
    return m_webglContext > 0;
}

/* static */
bool wxGLCanvas::IsDisplaySupported(const wxGLAttributes& dispAttrs)
{
    wxUnusedVar(dispAttrs);
    // WebGL is always available in Emscripten (it's the only option)
    return true;
}

/* static */
bool wxGLCanvas::IsDisplaySupported(const int *attribList)
{
    wxUnusedVar(attribList);
    return true;
}

// ============================================================================
// wxGLApp implementation
// Note: wxIMPLEMENT_CLASS(wxGLApp, wxApp) is in glcmn.cpp (common code)
// ============================================================================

bool wxGLApp::InitGLVisual(const int *attribList)
{
    wxUnusedVar(attribList);
    // WebGL is always available
    return true;
}

#endif // wxUSE_GLCANVAS
