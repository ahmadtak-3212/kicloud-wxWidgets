/////////////////////////////////////////////////////////////////////////////
// Name:        include/wx/wasm/webviewhistoryitem.h
// Purpose:     wxWebViewHistoryItem header for wxWASM
// Author:      Steven Lamerton (GTK version), kicloud W3.0P (wxWASM copy)
// Copyright:   (c) 2011 Steven Lamerton
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
// KICLOUD: W3.0P: the history item of wx's own GTK header
// (include/wx/gtk/webviewhistoryitem_webkit.h), for the wxWASM webview
// library; the backend that fills it (an iframe) is kicloud task W3.16.

#ifndef _WX_WASM_WEBVIEWHISTORYITEM_H_
#define _WX_WASM_WEBVIEWHISTORYITEM_H_

#include "wx/setup.h"

#if wxUSE_WEBVIEW && defined(__WXWASM__)

class WXDLLIMPEXP_WEBVIEW wxWebViewHistoryItem
{
public:
    wxWebViewHistoryItem(const wxString& url, const wxString& title) :
                     m_url(url), m_title(title) {}
    wxString GetUrl() { return m_url; }
    wxString GetTitle() { return m_title; }

private:
    wxString m_url, m_title;
};

#endif // wxUSE_WEBVIEW && defined(__WXWASM__)

#endif // _WX_WASM_WEBVIEWHISTORYITEM_H_
