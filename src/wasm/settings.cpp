/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/settings.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/log.h"
#include "wx/settings.h"

#ifndef WX_PRECOMP
#endif

#include <stdlib.h>

static wxFont gs_fontDefault(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

//-----------------------------------------------------------------------------
// wxSystemSettings
//-----------------------------------------------------------------------------

namespace {

// Light/dark chrome appearance. Initialized from the PCBJAM_DARK_CHROME env
// var (the embedder sets Module.ENV before main() — an env var rather than a
// DOM probe because main() runs in a pthread worker with no `document`, and
// wasm `environ` lives in shared linear memory so every thread agrees) —
// the very FIRST widget paint matches the shell theme. Flipped at runtime via
// wxWasmSetDarkAppearance + a wxSysColourChangedEvent broadcast (the embedder
// owns that — see the app layer's theme bridge).
// wxSystemAppearance::IsDark() needs no extra work: it compares the WINDOW /
// WINDOWTEXT luminance, which this table inverts.
bool wasmInitialDark()
{
    const char* v = getenv("PCBJAM_DARK_CHROME");
    return v != NULL && v[0] == '1';
}

bool& wasmDarkChrome()
{
    static bool s_dark = wasmInitialDark();
    return s_dark;
}

} // anonymous namespace

extern "C" void wxWasmSetDarkAppearance(bool dark)
{
    wasmDarkChrome() = dark;
}

extern "C" bool wxWasmGetDarkAppearance()
{
    return wasmDarkChrome();
}

// KICLOUD: the editor's light and warm dark chrome (B1.20, IDEAS.md #8): the dashboard's "Soft"
// colours, replacing the classic grey light table and the cool blue-grey dark one. Every value is
// one of web/live/theme/tokens.css (the trailing "// light|dark --token" notes are checked by
// tests/lint/theme-tokens.mjs). "Fields" (WINDOW) and dialogs/panels (BTNFACE) take the editor's
// surface colour; KiCad's toolbars and dock background take the panel colour from its own art
// providers (common/widgets/wx_aui_art_providers.cpp).
wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    // Default window backgrounds come from wxSYS_COLOUR_BTNFACE — without
    // this, canvas islands and dialog bodies erase to black.
    if (wasmDarkChrome())
    {
        switch (index)
        {
            case wxSYS_COLOUR_WINDOW:
            case wxSYS_COLOUR_LISTBOX:
            case wxSYS_COLOUR_INFOBK:
            case wxSYS_COLOUR_BTNFACE:        // == wxSYS_COLOUR_3DFACE, _FRAMEBK
            case wxSYS_COLOUR_MENU:
                return wxColour(0x2a, 0x27, 0x23); // dark --panel (the editor's surface)

            case wxSYS_COLOUR_MENUBAR:
            case wxSYS_COLOUR_SCROLLBAR:
            case wxSYS_COLOUR_ACTIVEBORDER:
            case wxSYS_COLOUR_INACTIVEBORDER:
                return wxColour(0x22, 0x1f, 0x1c); // dark --surface (the editor's panel)

            case wxSYS_COLOUR_APPWORKSPACE:
            case wxSYS_COLOUR_DESKTOP:
                return wxColour(0x18, 0x17, 0x15); // dark --bg

            case wxSYS_COLOUR_BTNHIGHLIGHT:   // == wxSYS_COLOUR_3DHILIGHT
            case wxSYS_COLOUR_3DLIGHT:
            case wxSYS_COLOUR_BTNSHADOW:      // == wxSYS_COLOUR_3DSHADOW
            case wxSYS_COLOUR_3DDKSHADOW:
            case wxSYS_COLOUR_WINDOWFRAME:
                return wxColour(0x4a, 0x45, 0x3e); // dark --line-strong

            case wxSYS_COLOUR_INACTIVECAPTION:
            case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
                return wxColour(0x36, 0x32, 0x2d); // dark --panel-strong

            case wxSYS_COLOUR_MENUHILIGHT:
                return wxColour(0x26, 0x2c, 0x4a); // dark --accent-soft

            case wxSYS_COLOUR_GRAYTEXT:
            case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
                return wxColour(0xb0, 0xa9, 0x9e); // dark --muted

            case wxSYS_COLOUR_HOTLIGHT:
                return wxColour(0x9d, 0xb0, 0xff); // dark --accent

            case wxSYS_COLOUR_HIGHLIGHT:
            case wxSYS_COLOUR_ACTIVECAPTION:
            case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
                return wxColour(0x42, 0x63, 0xeb); // dark --accent-bg

            case wxSYS_COLOUR_HIGHLIGHTTEXT:
            case wxSYS_COLOUR_CAPTIONTEXT:
            // The generic renderer paints a SELECTED item rect in HIGHLIGHT
            // whether or not the control has focus; the unfocused text colour
            // must stay readable on it (wxTreeCtrl/wxListCtrl draw unfocused
            // selections with LISTBOXHIGHLIGHTTEXT — defaulting it to the
            // window text colour gave black-on-blue in the hierarchy pane).
            case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
                return wxColour(0xff, 0xff, 0xff); // dark --accent-fg

            default:
                // text colours and everything else
                return wxColour(0xf3, 0xef, 0xe8); // dark --fg
        }
    }

    switch (index)
    {
        case wxSYS_COLOUR_WINDOW:
        case wxSYS_COLOUR_LISTBOX:
        case wxSYS_COLOUR_INFOBK:
        case wxSYS_COLOUR_BTNFACE:        // == wxSYS_COLOUR_3DFACE, _FRAMEBK
        case wxSYS_COLOUR_MENU:
        case wxSYS_COLOUR_BTNHIGHLIGHT:   // == wxSYS_COLOUR_3DHILIGHT
        case wxSYS_COLOUR_3DLIGHT:
            return wxColour(0xff, 0xff, 0xff); // light --surface

        case wxSYS_COLOUR_MENUBAR:
        case wxSYS_COLOUR_SCROLLBAR:
        case wxSYS_COLOUR_ACTIVEBORDER:
        case wxSYS_COLOUR_INACTIVEBORDER:
            return wxColour(0xef, 0xec, 0xe6); // light --panel

        case wxSYS_COLOUR_APPWORKSPACE:
        case wxSYS_COLOUR_DESKTOP:
            return wxColour(0xf6, 0xf4, 0xf0); // light --bg

        case wxSYS_COLOUR_BTNSHADOW:      // == wxSYS_COLOUR_3DSHADOW
        case wxSYS_COLOUR_3DDKSHADOW:
        case wxSYS_COLOUR_WINDOWFRAME:
            return wxColour(0xd6, 0xd0, 0xc5); // light --line-strong

        case wxSYS_COLOUR_INACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
            return wxColour(0xe5, 0xe1, 0xd9); // light --panel-strong

        case wxSYS_COLOUR_MENUHILIGHT:
            return wxColour(0xee, 0xf1, 0xff); // light --accent-soft

        case wxSYS_COLOUR_GRAYTEXT:
        case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
            return wxColour(0x5f, 0x5a, 0x52); // light --muted

        case wxSYS_COLOUR_HOTLIGHT:
            return wxColour(0x34, 0x51, 0xd1); // light --accent

        case wxSYS_COLOUR_HIGHLIGHT:
        case wxSYS_COLOUR_ACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
            return wxColour(0x3b, 0x5b, 0xdb); // light --accent-bg

        case wxSYS_COLOUR_HIGHLIGHTTEXT:
        case wxSYS_COLOUR_CAPTIONTEXT:
        case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT: // see the dark scheme note
            return wxColour(0xff, 0xff, 0xff); // light --accent-fg

        default:
            // text colours and everything else
            return wxColour(0x1d, 0x1b, 0x18); // light --fg
    }
}

wxFont wxSystemSettingsNative::GetFont(wxSystemFont WXUNUSED(index))
{
    // TODO: implement
    return gs_fontDefault;
}

int wxSystemSettingsNative::GetMetric(wxSystemMetric WXUNUSED(index), const wxWindow* WXUNUSED(win))
{
    // TODO: implement
    return 0;
}

bool wxSystemSettingsNative::HasFeature(wxSystemFeature index)
{
    switch (index)
    {
        case wxSYS_CAN_ICONIZE_FRAME:
            return false;
        case wxSYS_CAN_DRAW_FRAME_DECORATIONS:
            // Suppresses drawing of frame border and title bar.
            return true;
        default:
            return false;
    }
}
