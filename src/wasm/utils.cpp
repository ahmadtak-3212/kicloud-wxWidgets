
/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/utils.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/app.h"
#include "wx/window.h"
#endif // WX_PRECOMP

#include "wx/wasm/private.h"
#include "wx/private/launchbrowser.h"

#include <emscripten.h>
#include <emscripten/threading.h>
#include <vector>

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

// TODO: return os version
wxOperatingSystemId wxGetOsVersion(int *WXUNUSED(verMaj),
                                   int *WXUNUSED(verMin),
                                   int *WXUNUSED(verMicro))
{
    wxString osName;
    GET_JAVASCRIPT_STRING(platformInfo.name, osName);

    wxOperatingSystemId systemId = wxOS_UNKNOWN;

    if (osName == "Windows NT" || osName == "Windows")
    {
        systemId = wxOS_WINDOWS_NT;
    }
    else if (osName == "Mac OS X" || osName == "Macintosh")
    {
        systemId = wxOS_MAC_OSX_DARWIN;
    }
    else if (osName == "Linux")
    {
        systemId = wxOS_UNIX_LINUX;
    }
    else if (osName == "CrOS")
    {
        systemId = wxOS_CHROME_OS;
    }

    return systemId;
}

bool wxCheckOsVersion(int majorVsn, int minorVsn, int microVsn)
{
    // TODO: implement
    return true;
}

wxString wxGetOsDescription()
{
    wxString browserName;
    wxString browserVersion;
    wxString osName;
    wxString osVersion;

    GET_JAVASCRIPT_STRING(browserInfo.name, browserName);
    GET_JAVASCRIPT_STRING(browserInfo.version, browserVersion);
    GET_JAVASCRIPT_STRING(platformInfo.name, osName);
    GET_JAVASCRIPT_STRING(platformInfo.version, osVersion);

    return browserName + " " + browserVersion + " (" + osName + " " + osVersion + ")";
}

bool wxIsPlatform64Bit()
{
    return false;
}

wxString wxGetCpuArchitectureName()
{
    return "unknown";
}

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


// ----------------------------------------------------------------------------
// KICLOUD: P3-I T14 printing through the browser (menu audit: KiCad's schematic File > Print
// failed with "Fork failed (error 52)": the generic port printed through wxPostScriptPrinter,
// whose print dialog lists printers with `lpstat` and sends PostScript to `lpr`, and the browser
// can start no process). wxWasmPrinter renders the wxPrintout's pages with the application's own
// drawing code (OnPrintPage on a memory DC, at kPrintDpi), and hands them to the browser's print
// dialog as one page image each (a hidden iframe, @page sized to the paper). The browser's dialog
// chooses the printer or "Save as PDF". The jobs are also listed in globalThis.__wxPrintJobs.
// ----------------------------------------------------------------------------

#if wxUSE_PRINTING_ARCHITECTURE

#include "wx/print.h"
#include "wx/printdlg.h"
#include "wx/paper.h"
#include "wx/dcmemory.h"
#include "wx/image.h"
#include "wx/mstream.h"

namespace
{

const int kPrintDpi = 150;

// The page DC reports kPrintDpi as its resolution (GetPPI), the resolution its pixels are drawn
// at: printouts convert the page between pixels and inches with the DC's PPI. (A pointer to
// wxDCImpl's protected member, named through a class derived from it, sets it on any DC.)
struct wxWasmDCResolution : public wxDCImpl
{
    static void Set(wxDC& dc, double dpi)
    {
        double wxDCImpl::* const x = &wxWasmDCResolution::m_mm_to_pix_x;
        double wxDCImpl::* const y = &wxWasmDCResolution::m_mm_to_pix_y;
        dc.GetImpl()->*x = dpi / 25.4;
        dc.GetImpl()->*y = dpi / 25.4;
    }
};

EM_JS(void, wxWasmPrintBeginJs, (), {
    globalThis.__wxPrintPending = [];
});

EM_JS(void, wxWasmPrintAddPageJs, (const unsigned char* data, int len), {
    var bytes = HEAPU8.slice(data, data + len);
    globalThis.__wxPrintPending.push(URL.createObjectURL(new Blob([bytes], { type: 'image/png' })));
});

EM_JS(void, wxWasmPrintRunJs, (const char* titlePtr, double wmm, double hmm), {
    var title = UTF8ToString(titlePtr);
    var pages = globalThis.__wxPrintPending || [];
    globalThis.__wxPrintPending = [];
    var job = { title: title, widthMm: wmm, heightMm: hmm, pages: pages.length, urls: pages, printed: false };
    (globalThis.__wxPrintJobs = globalThis.__wxPrintJobs || []).push(job);
    if (typeof document === 'undefined' || !pages.length) return;
    var frame = document.createElement('iframe');
    frame.setAttribute('aria-hidden', 'true');
    frame.style.cssText = 'position:fixed;right:0;bottom:0;width:0;height:0;border:0;visibility:hidden';
    document.body.appendChild(frame);
    var doc = frame.contentDocument;
    var esc = function (s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); };
    doc.open();
    doc.write('<!doctype html><html><head><meta charset="utf-8"><title>' + esc(title) + '</title><style>' +
              '@page { size: ' + wmm + 'mm ' + hmm + 'mm; margin: 0 }' +
              'html, body { margin: 0; padding: 0 }' +
              'img { display: block; width: ' + wmm + 'mm; height: ' + hmm + 'mm; page-break-after: always; break-after: page }' +
              'img:last-child { page-break-after: auto; break-after: auto }' +
              '</style></head><body>' +
              pages.map(function (u) { return '<img src="' + u + '">'; }).join('') + '</body></html>');
    doc.close();
    var cleanup = function () {
        if (frame.parentNode) frame.parentNode.removeChild(frame);
        pages.forEach(function (u) { URL.revokeObjectURL(u); });
        job.urls = [];
    };
    var imgs = Array.prototype.slice.call(doc.images);
    Promise.all(imgs.map(function (im) {
        return im.complete ? Promise.resolve() : new Promise(function (r) { im.onload = im.onerror = r; });
    })).then(function () {
        job.printed = true;
        frame.contentWindow.addEventListener('afterprint', function () { setTimeout(cleanup, 0); });
        if (globalThis.__wxPrintDryRun) {   // tests: no browser dialog; the page images stay
            if (frame.parentNode) frame.parentNode.removeChild(frame);
            return;
        }
        frame.contentWindow.focus();
        frame.contentWindow.print();
    });
});

class wxWasmPrinter : public wxPrinterBase
{
public:
    explicit wxWasmPrinter(wxPrintDialogData* data) : wxPrinterBase(data) { }

    virtual bool Setup(wxWindow* WXUNUSED(parent)) wxOVERRIDE { return true; }
    virtual wxDC* PrintDialog(wxWindow* WXUNUSED(parent)) wxOVERRIDE { return NULL; }

    // The browser shows the print dialog (printer, copies, range), so `prompt` shows none here
    virtual bool Print(wxWindow* WXUNUSED(parent), wxPrintout* printout, bool WXUNUSED(prompt)) wxOVERRIDE
    {
        sm_lastError = wxPRINTER_NO_ERROR;
        if ( !printout )
        {
            sm_lastError = wxPRINTER_ERROR;
            return false;
        }

        // the paper, in mm: its size if set, else its id's size (tenths of a mm)
        const wxPrintData& data = m_printDialogData.GetPrintData();
        wxSize mm = data.GetPaperSize();
        if ( mm.x <= 0 || mm.y <= 0 )
        {
            const wxPrintPaperType* paper = wxThePrintPaperDatabase
                ? wxThePrintPaperDatabase->FindPaperType(data.GetPaperId()) : NULL;
            if ( !paper && wxThePrintPaperDatabase )
                paper = wxThePrintPaperDatabase->FindPaperType(wxPAPER_A4);
            mm = paper ? wxSize(paper->GetWidth() / 10, paper->GetHeight() / 10) : wxSize(210, 297);
        }
        if ( (data.GetOrientation() == wxLANDSCAPE) != (mm.x > mm.y) )
            mm = wxSize(mm.y, mm.x);

        const int w = wxRound(mm.x * kPrintDpi / 25.4);
        const int h = wxRound(mm.y * kPrintDpi / 25.4);
        wxBitmap page(w, h, 24);
        wxMemoryDC dc(page);
        if ( !dc.IsOk() )
        {
            sm_lastError = wxPRINTER_ERROR;
            return false;
        }
        wxWasmDCResolution::Set(dc, kPrintDpi);

        printout->SetPPIScreen(96, 96);
        printout->SetPPIPrinter(kPrintDpi, kPrintDpi);
        printout->SetPageSizePixels(w, h);
        printout->SetPageSizeMM(mm.x, mm.y);
        printout->SetPaperRectPixels(wxRect(0, 0, w, h));
        printout->SetDC(&dc);

        printout->OnPreparePrinting();
        int minPage = 0, maxPage = 0, fromPage = 0, toPage = 0;
        printout->GetPageInfo(&minPage, &maxPage, &fromPage, &toPage);
        if ( maxPage == 0 )
        {
            sm_lastError = wxPRINTER_ERROR;
            printout->SetDC(NULL);
            return false;
        }
        // the caller's range, when it set one; else every page the printout has
        if ( m_printDialogData.GetFromPage() > 0 )
        {
            fromPage = m_printDialogData.GetFromPage();
            toPage = m_printDialogData.GetToPage();
        }
        if ( fromPage < minPage ) fromPage = minPage;
        if ( toPage > maxPage || toPage < fromPage ) toPage = maxPage;

        printout->OnBeginPrinting();
        wxWasmPrintBeginJs();
        bool ok = printout->OnBeginDocument(fromPage, toPage);
        for ( int p = fromPage; ok && p <= toPage && printout->HasPage(p); ++p )
        {
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            dc.SetDeviceOrigin(0, 0);
            dc.SetLogicalOrigin(0, 0);
            dc.SetUserScale(1.0, 1.0);
            dc.SetAxisOrientation(true, false);
            dc.DestroyClippingRegion();
            printout->OnPrintPage(p);

            dc.SelectObject(wxNullBitmap);
            wxMemoryOutputStream png;
            if ( !page.ConvertToImage().SaveFile(png, wxBITMAP_TYPE_PNG) )
            {
                ok = false;
                break;
            }
            const size_t len = png.GetLength();
            std::vector<unsigned char> bytes(len);
            png.CopyTo(bytes.data(), len);
            wxWasmPrintAddPageJs(bytes.data(), (int) len);
            dc.SelectObject(page);
        }
        if ( ok )
            printout->OnEndDocument();
        printout->OnEndPrinting();
        printout->SetDC(NULL);

        if ( !ok )
        {
            sm_lastError = wxPRINTER_ERROR;
            return false;
        }
        wxWasmPrintRunJs(printout->GetTitle().utf8_str(), mm.x, mm.y);
        return true;
    }
};

class wxWasmPrintFactory : public wxNativePrintFactory
{
public:
    virtual wxPrinterBase* CreatePrinter(wxPrintDialogData* data) wxOVERRIDE
    {
        return new wxWasmPrinter(data);
    }
    // no printer setup dialog: it lists printers with `lpstat` (the browser's dialog chooses)
    virtual bool HasPrintSetupDialog() wxOVERRIDE { return false; }
    virtual wxDialog* CreatePrintSetupDialog(wxWindow* WXUNUSED(parent), wxPrintData* WXUNUSED(data)) wxOVERRIDE
    {
        return NULL;
    }
};

} // anonymous namespace

void wxWasmInstallPrintFactory()
{
    wxPrintFactory::SetPrintFactory(new wxWasmPrintFactory);
}

#else

void wxWasmInstallPrintFactory() { }

#endif // wxUSE_PRINTING_ARCHITECTURE
