
/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/popupwin.cpp
// Purpose:     wxPopupWindow implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:src/wasm/popupwin.cpp (W3.0P; kicloud/docs/provenance.md)

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/wasm/private/display.h"
#include "wx/popupwin.h"

#ifndef WX_PRECOMP
#endif //WX_PRECOMP

// ----------------------------------------------------------------------------
// wxPopupWindow
// ----------------------------------------------------------------------------

wxPopupWindow::~wxPopupWindow()
{
}

bool wxPopupWindow::Create(wxWindow *parent, int flags)
{
    if (!wxNonOwnedWindow::Create(parent, -1, wxDefaultPosition, wxDefaultSize, flags, wxT("popup")))
    {
        return false;
    }

    Hide();

    return true;
}
