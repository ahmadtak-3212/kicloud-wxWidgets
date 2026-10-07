/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/combog.cpp
// Purpose:     Generic wxComboCtrl
// Author:      Jaakko Salli
// Modified by:
// Created:     Apr-30-2006
// Copyright:   (c) 2005 Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"


#if wxUSE_COMBOCTRL

#include "wx/combo.h"

#ifdef __EMSCRIPTEN__
    #include "wx/wasm/elementtracker.h"
#endif

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/combobox.h"
    #include "wx/dcclient.h"
    #include "wx/settings.h"
    #include "wx/textctrl.h"
#endif

#include "wx/dcbuffer.h"

// ----------------------------------------------------------------------------
// Some constant adjustments to make the generic more bearable

#if defined(__WXUNIVERSAL__)

// position adjustment for wxTextCtrl, to achieve zero left margin
// meaningless if LEFT_MARGIN_CAN_BE_SET set to 1 in combocmn.cpp
#define TEXTCTRLXADJUST                 0

#define DEFAULT_DROPBUTTON_WIDTH        19

#elif defined(__WXMSW__)

// position adjustment for wxTextCtrl, to achieve zero left margin
// meaningless if LEFT_MARGIN_CAN_BE_SET set to 1 in combocmn.cpp
#define TEXTCTRLXADJUST                 2

#define DEFAULT_DROPBUTTON_WIDTH        17

#elif defined(__WXGTK__)

// position adjustment for wxTextCtrl, to achieve zero left margin
// meaningless if LEFT_MARGIN_CAN_BE_SET set to 1 in combocmn.cpp
#define TEXTCTRLXADJUST                 -1

#define DEFAULT_DROPBUTTON_WIDTH        23

#elif defined(__WXMAC__)

// position adjustment for wxTextCtrl, to achieve zero left margin
// meaningless if LEFT_MARGIN_CAN_BE_SET set to 1 in combocmn.cpp
#define TEXTCTRLXADJUST                 0

#define DEFAULT_DROPBUTTON_WIDTH        22

#else

// position adjustment for wxTextCtrl, to achieve zero left margin
// meaningless if LEFT_MARGIN_CAN_BE_SET set to 1 in combocmn.cpp
#define TEXTCTRLXADJUST                 0

#define DEFAULT_DROPBUTTON_WIDTH        19

#endif


// ============================================================================
// implementation
// ============================================================================

#ifdef __EMSCRIPTEN__
// KICLOUD: A20 (docs/patches.md): the browser editor's pill-shaped combo boxes. Whether a combo is
// a pill, and the room its areas leave, is decided in src/common/combocmn.cpp
// (wxWasmComboIsPill, wxComboCtrlBase::CalculateAreas); the painting is below.
extern bool wxWasmComboIsPill(const wxComboCtrlBase* combo, bool plainCombo);

namespace
{

// The colour behind a pill, which shows at its four rounded corners. Input: the combo. Result:
// the colour its parent window is painted in. A wxAuiToolBar (KiCad's top toolbars) is painted
// by its art provider in wxSYS_COLOUR_MENUBAR (KiCad's common/widgets/wx_aui_art_providers.cpp,
// the browser build) while its own background colour attribute stays the default, so the
// toolbar's case is taken from the system colour table. Changes nothing.
wxColour PillCornerColour(const wxWindow* combo)
{
    const wxWindow* parent = combo->GetParent();
    static wxClassInfo* const auiToolBarClass = wxClassInfo::FindClass(wxS("wxAuiToolBar"));

    if ( parent && auiToolBarClass && parent->IsKindOf(auiToolBarClass) )
        return wxSystemSettings::GetColour(wxSYS_COLOUR_MENUBAR);

    return parent ? parent->GetBackgroundColour()
                  : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
}

// Paints the pill: the whole control in the corner colour, then a rounded rectangle with fully
// round ends (radius = half the height) with a 1 px outline, then the chevron at the right end.
// It matches the <select> pill of web/editor/appearance/theme/editor-chrome.css: the face is the
// field colour (wxSYS_COLOUR_WINDOW, the CSS --kc-field), the outline the strong line colour
// (wxSYS_COLOUR_BTNSHADOW, --line-strong), and the chevron a 10 x 6 px "v" whose right end is
// 9 px from the pill's right edge, drawn in the muted text colour (wxSYS_COLOUR_GRAYTEXT,
// --muted). When the combo has the keyboard focus the outline takes the focus colour (--focus:
// wxSYS_COLOUR_HIGHLIGHT in light, wxSYS_COLOUR_HOTLIGHT in dark), as a focused <select> does.
// Inputs: the DC to paint on, the control's whole client rectangle, the face colour and whether
// the outline shows the focus. The outline is painted as two filled shapes, not a stroked one,
// so it lands on whole pixels (a 1 px stroke on a whole-pixel edge is blurred over 2 px by the
// page's canvas). Changes only the DC's pen and brush.
void DrawPill(wxWindow* combo, wxDC& dc, const wxRect& rect, const wxColour& face, bool focused)
{
    const wxColour line = focused
        ? wxSystemSettings::GetColour(wxSystemSettings::GetAppearance().IsDark()
                                          ? wxSYS_COLOUR_HOTLIGHT : wxSYS_COLOUR_HIGHLIGHT)
        : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(PillCornerColour(combo));
    dc.DrawRectangle(rect);

    if ( rect.width < 4 || rect.height < 4 )
        return;

    dc.SetBrush(line);
    dc.DrawRoundedRectangle(rect, rect.height / 2.0);
    wxRect inner(rect);
    inner.Deflate(1);
    dc.SetBrush(face);
    dc.DrawRoundedRectangle(inner, inner.height / 2.0);

    // the chevron: (1,1) -> (5,5) -> (9,1) in a 10 x 6 box, as the CSS's SVG path "M1 1l4 4 4-4"
    const int boxLeft = rect.GetRight() + 1 - combo->FromDIP(9) - combo->FromDIP(10);
    const int boxTop = rect.y + (rect.height - combo->FromDIP(6)) / 2;
    const int u = combo->FromDIP(1);
    const wxPoint chevron[3] = { wxPoint(boxLeft + u, boxTop + u),
                                 wxPoint(boxLeft + 5 * u, boxTop + 5 * u),
                                 wxPoint(boxLeft + 9 * u, boxTop + u) };
    wxPen pen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT), combo->FromDIP(2));
    pen.SetCap(wxCAP_ROUND);
    pen.SetJoin(wxJOIN_ROUND);
    dc.SetPen(pen);
    dc.DrawLines(3, chevron);
    dc.SetPen(*wxTRANSPARENT_PEN);
}

} // anonymous namespace
#endif // __EMSCRIPTEN__

// Only implement if no native or it wasn't fully featured
#ifndef wxCOMBOCONTROL_FULLY_FEATURED


// ----------------------------------------------------------------------------
// wxGenericComboCtrl
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxGenericComboCtrl, wxComboCtrlBase)
    EVT_PAINT(wxGenericComboCtrl::OnPaintEvent)
    EVT_MOUSE_EVENTS(wxGenericComboCtrl::OnMouseEvent)
wxEND_EVENT_TABLE()


wxIMPLEMENT_DYNAMIC_CLASS(wxGenericComboCtrl, wxComboCtrlBase);

void wxGenericComboCtrl::Init()
{
}

bool wxGenericComboCtrl::Create(wxWindow *parent,
                                wxWindowID id,
                                const wxString& value,
                                const wxPoint& pos,
                                const wxSize& size,
                                long style,
                                const wxValidator& validator,
                                const wxString& name)
{
    //
    // Note that technically we only support 'default' border and wxNO_BORDER.
    long border = style & wxBORDER_MASK;
    int tcBorder = wxNO_BORDER;

#if defined(__WXUNIVERSAL__)
    if ( !border )
        border = wxBORDER_SIMPLE;
#elif defined(__WXMSW__)
    if ( !border )
        border = wxBORDER_SUNKEN;
#else

    //
    // Generic version is optimized for wxGTK
    //

    #define UNRELIABLE_TEXTCTRL_BORDER

    if ( !border )
    {
        if ( style & wxCB_READONLY )
        {
            m_widthCustomBorder = 1;
        }
        else
        {
            m_widthCustomBorder = 0;
            tcBorder = 0;
        }
    }
    else
    {
        // Have textctrl instead use the border given.
        tcBorder = border;
    }

    // Because we are going to have button outside the border,
    // let's use wxBORDER_NONE for the whole control.
    border = wxBORDER_NONE;

    Customize( wxCC_BUTTON_OUTSIDE_BORDER |
               wxCC_NO_TEXT_AUTO_SELECT |
               wxCC_BUTTON_STAYS_DOWN );

#endif

    style = (style & ~(wxBORDER_MASK)) | border;
    if ( style & wxCC_STD_BUTTON )
        m_iFlags |= wxCC_POPUP_ON_MOUSE_UP;

    // create main window
    if ( !wxComboCtrlBase::Create(parent,
                                  id,
                                  value,
                                  pos,
                                  size,
                                  style | wxFULL_REPAINT_ON_RESIZE,
                                  validator,
                                  name) )
        return false;

    // Create textctrl, if necessary
    CreateTextCtrl( tcBorder );

    // Set background style for double-buffering, when needed
    // (cannot use when system draws background automatically)
    if ( !HasTransparentBackground() )
        SetBackgroundStyle( wxBG_STYLE_PAINT );

    // SetInitialSize should be called last
    SetInitialSize(size);

    return true;
}

wxGenericComboCtrl::~wxGenericComboCtrl()
{
}

bool wxGenericComboCtrl::HasTransparentBackground()
{
#if wxALWAYS_NATIVE_DOUBLE_BUFFER
  #ifdef __WXGTK__
    // Sanity check for GTK+
    return IsDoubleBuffered();
  #else
    return true;
  #endif
#else
    return false;
#endif
}

void wxGenericComboCtrl::OnResize()
{

    // Recalculates button and textctrl areas
    CalculateAreas(DEFAULT_DROPBUTTON_WIDTH);

#if 0
    // Move separate button control, if any, to correct position
    if ( m_btn )
    {
        wxSize sz = GetClientSize();
        m_btn->SetSize( m_btnArea.x + m_btnSpacingX,
                        (sz.y-m_btnSize.y)/2,
                        m_btnSize.x,
                        m_btnSize.y );
    }
#endif

    // Move textctrl, if any, accordingly
    PositionTextCtrl( TEXTCTRLXADJUST );
}

void wxGenericComboCtrl::OnPaintEvent( wxPaintEvent& WXUNUSED(event) )
{
    // Determine wxDC to use based on need to double-buffer or
    // use system-generated transparent background portions
    wxDC* dcPtr;
    if ( HasTransparentBackground() )
        dcPtr = new wxPaintDC(this);
    else
        dcPtr = new wxAutoBufferedPaintDC(this);
    wxDC& dc = *dcPtr;

    wxSize sz = GetClientSize();
    const wxRect& butRect = m_btnArea;
    wxRect tcRect = m_tcArea;
    wxRect fullRect(0, 0, sz.x, sz.y);

#ifdef __EMSCRIPTEN__
    // KICLOUD: A20: a pill (see DrawPill above) replaces the square border, the background clear
    // and the square drop button below. Its face is always the field colour, like a <select>'s:
    // the combo's own background colour is not used, because OnThemeChange() (combocmn.cpp) sets
    // it to its parent's colour on every theme change.
    const bool pill = wxWasmComboIsPill(this, m_widthCustomBorder > 0 && m_btnSide == wxRIGHT &&
                                              !m_bmpNormal.IsOk() && !m_btn);
    if ( pill )
        DrawPill(this, dc, fullRect, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW),
                 ShouldDrawFocus());
#else
    const bool pill = false;
#endif

    // artificial simple border
    if ( m_widthCustomBorder && !pill )
    {
        int customBorder = m_widthCustomBorder;

        // Set border colour
#ifdef __WXMAC__
        wxPen pen1( wxColour(133,133,133),
                    customBorder,
                    wxPENSTYLE_SOLID );
#else
        wxPen pen1( wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT),
                    customBorder,
                    wxPENSTYLE_SOLID);
#endif
        dc.SetPen( pen1 );

        // area around both controls
        wxRect rect2(fullRect);
        if ( m_iFlags & wxCC_IFLAG_BUTTON_OUTSIDE )
        {
            rect2 = tcRect;
            if ( customBorder == 1 )
            {
                rect2.Inflate(1);
            }
            else
            {
            #ifdef __WXGTK__
                rect2.x -= 1;
                rect2.y -= 1;
            #else
                rect2.x -= customBorder;
                rect2.y -= customBorder;
            #endif
                rect2.width += 1 + customBorder;
                rect2.height += 1 + customBorder;
            }
        }

        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.DrawRectangle(rect2);
    }

    // Clear the main background if the system doesn't do it by itself
    if ( !pill && !HasTransparentBackground() &&
         (tcRect.x > 0 || tcRect.y > 0) )
    {
        wxColour winCol = GetParent()->GetBackgroundColour();
        dc.SetBrush(winCol);
        dc.SetPen(winCol);

        dc.DrawRectangle(fullRect);
    }

    if ( !m_btn && !pill )
    {
        // Standard button rendering
        DrawButton(dc, butRect);
    }

    // paint required portion on the control
    if ( !m_text || m_widthCustomPaint )
    {
        wxASSERT( m_widthCustomPaint >= 0 );

        // Clear the text-control area background (KICLOUD: A20, a pill's face is already
        // painted, and a rectangle here would cut into its rounded outline)
        if ( !pill )
        {
            wxColour tcCol = GetBackgroundColour();
            dc.SetBrush(tcCol);
            dc.SetPen(tcCol);
            dc.DrawRectangle(tcRect);
        }

        // this is intentionally here to allow drawn rectangle's
        // right edge to be hidden
        if ( m_text )
            tcRect.width = m_widthCustomPaint;

        dc.SetFont( GetFont() );

        dc.SetClippingRegion(tcRect);
        if ( m_popupInterface )
            m_popupInterface->PaintComboControl(dc, tcRect);
        else
            wxComboPopup::DefaultPaintComboControl(this, dc, tcRect);
    }

#ifdef __EMSCRIPTEN__
    // Register combo/choice button for element tracking
    WasmUnregisterRenderedElementsByParent(this);

    // The dropdown button area
    wxWasmTrackElement(this, "combobutton",
                       IsPopupShown() ? "open" : "closed", 0,
                       GetValue(), GetName(), m_btnArea, IsEnabled());

    // The text/selection area for clicking
    wxWasmTrackElement(this, "combotextarea",
                       HasFlag(wxCB_READONLY) ? "readonly" : "editable", 1,
                       GetValue(), GetName(), m_tcArea, IsEnabled());
#endif

    delete dcPtr;
}

void wxGenericComboCtrl::OnMouseEvent( wxMouseEvent& event )
{
    int mx = event.m_x;
    bool isOnButtonArea = m_btnArea.Contains(mx,event.m_y);
    int handlerFlags = isOnButtonArea ? wxCC_MF_ON_BUTTON : 0;

    if ( PreprocessMouseEvent(event,handlerFlags) )
        return;

    const bool ctrlIsButton = wxPlatformIs(wxOS_WINDOWS);

    if ( ctrlIsButton &&
         (m_windowStyle & (wxCC_SPECIAL_DCLICK|wxCB_READONLY)) == wxCB_READONLY )
    {
        // if no textctrl and no special double-click, then the entire control acts
        // as a button
        handlerFlags |= wxCC_MF_ON_BUTTON;
        if ( HandleButtonMouseEvent(event,handlerFlags) )
            return;
    }
    else
    {
        if ( isOnButtonArea || HasCapture() ||
             (m_widthCustomPaint && mx < (m_tcArea.x+m_widthCustomPaint)) )
        {
            handlerFlags |= wxCC_MF_ON_CLICK_AREA;

            if ( HandleButtonMouseEvent(event,handlerFlags) )
                return;
        }
        else if ( m_btnState )
        {
            // otherwise need to clear the hover status
            m_btnState = 0;
            RefreshRect(m_btnArea);
        }
    }

    //
    // This will handle left_down and left_dclick events outside button in a Windows/GTK-like manner.
    // See header file for further information on this method.
    HandleNormalMouseEvent(event);

}

void wxGenericComboCtrl::SetCustomPaintWidth( int width )
{
#ifdef UNRELIABLE_TEXTCTRL_BORDER
    //
    // If starting/stopping to show an image in front
    // of a writable text-field, then re-create textctrl
    // with different kind of border (because we can't
    // assume that textctrl fully supports wxNO_BORDER).
    //
    wxTextCtrl* tc = GetTextCtrl();

    if ( tc && (m_iFlags & wxCC_BUTTON_OUTSIDE_BORDER) )
    {
        int borderType = tc->GetWindowStyle() & wxBORDER_MASK;
        int tcCreateStyle = -1;

        if ( width > 0 )
        {
            // Re-create textctrl with no border
            if ( borderType != wxNO_BORDER )
            {
                m_widthCustomBorder = 1;
                tcCreateStyle = wxNO_BORDER;
            }
        }
        else if ( width == 0 )
        {
            // Re-create textctrl with normal border
            if ( borderType == wxNO_BORDER )
            {
                m_widthCustomBorder = 0;
                tcCreateStyle = 0;
            }
        }

        // Common textctrl re-creation code
        if ( tcCreateStyle != -1 )
        {
            CreateTextCtrl( tcCreateStyle );
        }
    }
#endif // UNRELIABLE_TEXTCTRL_BORDER

    wxComboCtrlBase::SetCustomPaintWidth( width );
}

bool wxGenericComboCtrl::IsKeyPopupToggle(const wxKeyEvent& event) const
{
    int keycode = event.GetKeyCode();
    bool isPopupShown = IsPopupShown();

    // This code is AFAIK appropriate for wxGTK.

    if ( isPopupShown )
    {
        if ( keycode == WXK_ESCAPE ||
             ( keycode == WXK_UP && event.AltDown() ) )
            return true;
    }
    else
    {
        if ( (keycode == WXK_DOWN && event.AltDown()) ||
             (keycode == WXK_F4) )
            return true;
    }

    return false;
}

#if defined(__WXOSX__)
wxTextWidgetImpl * wxGenericComboCtrl::GetTextPeer() const
{
    return m_text ? m_text->GetTextPeer() : NULL;
}
#endif

#ifdef __WXUNIVERSAL__

bool wxGenericComboCtrl::PerformAction(const wxControlAction& action,
                                       long numArg,
                                       const wxString& strArg)
{
    bool processed = false;
    if ( action == wxACTION_COMBOBOX_POPUP )
    {
        if ( !IsPopupShown() )
        {
            ShowPopup();

            processed = true;
        }
    }
    else if ( action == wxACTION_COMBOBOX_DISMISS )
    {
        if ( IsPopupShown() )
        {
            HidePopup();

            processed = true;
        }
    }

    if ( !processed )
    {
        // pass along
        return wxControl::PerformAction(action, numArg, strArg);
    }

    return true;
}

#endif // __WXUNIVERSAL__

// If native wxComboCtrl was not defined, then prepare a simple
// front-end so that wxRTTI works as expected.
#ifndef _WX_COMBOCONTROL_H_
wxIMPLEMENT_DYNAMIC_CLASS(wxComboCtrl, wxGenericComboCtrl);
#endif

#endif // !wxCOMBOCONTROL_FULLY_FEATURED

#endif // wxUSE_COMBOCTRL
