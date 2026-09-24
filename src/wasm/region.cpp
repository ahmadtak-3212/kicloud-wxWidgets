/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/region.cpp
// Purpose:     wxRegion implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////
// KICLOUD: adapted from pcbjam@8bad5f58e9:src/wasm/region.cpp (W3.0P; kicloud/docs/provenance.md)

#include "wx/wxprec.h"

#include "wx/region.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

//-----------------------------------------------------------------------------
// wxRegion
//-----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(wxRegion, wxRegionGeneric)
