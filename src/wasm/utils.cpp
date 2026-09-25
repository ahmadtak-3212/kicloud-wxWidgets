
/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/utils.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:src/wasm/utils.cpp (W3.0P; kicloud/docs/provenance.md)
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/app.h"
#include "wx/window.h"
#endif // WX_PRECOMP

#include "wx/wasm/private.h"
#include "wx/private/launchbrowser.h"

#include <emscripten.h>
#include <emscripten/threading.h>

#define GET_JAVASCRIPT_STRING(varName, string) \
  { \
    int length = EM_ASM_INT({ \
      return lengthBytesUTF8(varName); \
    }); \
    char* buffer = new char[length + 1]; \
    EM_ASM({ \
      stringToUTF8(varName, $0, $1); \
    }, buffer, length + 1); \
    string = buffer; \
    delete [] buffer; \
  }

// KICLOUD: W3.0P. pcbjam@8bad5f58e9 defined wxGetOsVersion(), wxCheckOsVersion(),
// wxGetOsDescription(), wxIsPlatform64Bit() and wxGetCpuArchitectureName() here
// and removed them from src/unix/utilsunx.cpp under __WXWASM__. Those are wxBase
// functions, while this file is compiled into wxcore: a base-only consumer of a
// GUI build (test_base, a console program) would not link, and the base library
// would differ between the base-only and the GUI configuration (src/wasm/README.md).
// Their browser versions (from the user agent) live in wxBase instead, in
// src/unix/utilsunx.cpp under __EMSCRIPTEN__ (kicloud/docs/patches.md).

wxBrowserInfo wxGetBrowserInfo()
{
    wxBrowserId browserId = wxBROWSER_UNKNOWN;

    wxString userAgent;
    wxString browserName;
    wxString browserVersion;

    GET_JAVASCRIPT_STRING(navigator.userAgent, userAgent);
    GET_JAVASCRIPT_STRING(browserInfo.name, browserName);
    GET_JAVASCRIPT_STRING(browserInfo.version, browserVersion);

    if (browserName == "Firefox")
    {
        browserId = wxBROWSER_FIREFOX;
    }
    else if (browserName == "Chrome")
    {
        browserId = wxBROWSER_CHROME;
    }
    else if (browserName == "Safari")
    {
        browserId = wxBROWSER_SAFARI;
    }
    else if (browserName == "Edge")
    {
        browserId = wxBROWSER_EDGE;
    }
    else if (browserName == "MSIE")
    {
        browserId = wxBROWSER_MSIE;
    }
    else if (browserName == "Opera")
    {
        browserId = wxBROWSER_OPERA;
    }

    // TODO: populate version

    return wxBrowserInfo(browserId, browserName, userAgent, browserVersion);
}

void EmscriptenDoLaunchBrowser(const wxString& url)
{
    EM_ASM({
        openUrl(UTF8ToString($0))
    }, static_cast<const char *>(url.utf8_str()));
}

void EmscriptenDoLaunchBrowserAsync(void *arg)
{
    wxString *url = static_cast<wxString *>(arg);
    EmscriptenDoLaunchBrowser(*url);
    delete url;
}

bool wxDoLaunchDefaultBrowser(const wxLaunchBrowserParams& params)
{
    // TODO: handle wxBROWSER_NEW_WINDOW flag

    if (emscripten_is_main_runtime_thread())
    {
        EmscriptenDoLaunchBrowser(params.url);
    }
    else
    {
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI,
                &EmscriptenDoLaunchBrowserAsync,
                new wxString(params.url));
    }

    return true;
}

bool wxLaunchDefaultApplication(const wxString& path, int WXUNUSED(flags))
{
    // In a browser context, we can only launch URLs
    // Check if the path looks like a URL
    if (path.StartsWith("http://") || path.StartsWith("https://") ||
        path.StartsWith("mailto:") || path.StartsWith("file://"))
    {
        if (emscripten_is_main_runtime_thread())
        {
            EmscriptenDoLaunchBrowser(path);
        }
        else
        {
            emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI,
                    &EmscriptenDoLaunchBrowserAsync,
                    new wxString(path));
        }
        return true;
    }

    // For local file paths, we cannot launch external applications in a browser
    // Return false to indicate the operation is not supported
    return false;
}

void wxBell()
{
}

wxWindow* wxFindWindowAtPointer(wxPoint& pt)
{
    pt = wxGetMousePosition();
    return wxFindWindowAtPoint(pt);
}

