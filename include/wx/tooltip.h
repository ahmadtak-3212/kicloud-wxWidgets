/////////////////////////////////////////////////////////////////////////////
// Name:        wx/tooltip.h
// Purpose:     wxToolTip base header
// Author:      Robert Roebling
// Modified by:
// Created:
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_TOOLTIP_H_BASE_
#define _WX_TOOLTIP_H_BASE_

#include "wx/defs.h"

#if wxUSE_TOOLTIPS

#if defined(__WXMSW__)
#include "wx/msw/tooltip.h"
#elif defined(__WXMOTIF__)
// #include "wx/motif/tooltip.h"
#elif defined(__WXGTK20__)
#include "wx/gtk/tooltip.h"
#elif defined(__WXGTK__)
#include "wx/gtk1/tooltip.h"
#elif defined(__WXMAC__)
#include "wx/osx/tooltip.h"
#elif defined(__WXQT__)
#include "wx/qt/tooltip.h"
/* KICLOUD: adapted from pcbjam@8bad5f58e9:include/wx/tooltip.h (W3.0P): wxWASM dispatch line(s): select the wasm port's class/header like the other ports */
#elif defined(__WXWASM__)
#include "wx/wasm/tooltip.h"
#endif

#endif
    // wxUSE_TOOLTIPS

#endif
    // _WX_TOOLTIP_H_BASE_
