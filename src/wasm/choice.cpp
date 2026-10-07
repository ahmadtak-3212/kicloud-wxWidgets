/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/choice.cpp
// Purpose:     wxChoice implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHOICE

#include "wx/choice.h"

#include "wx/utils.h"         // KICLOUD: LOOK.6, wxMax
#include "wx/wasm/private/dom.h"

#define INVALID_INDEX_MESSAGE wxT("invalid choice index")

// KICLOUD: LOOK.6: defined in textctrl.cpp (a dialog's form control gets the roomy size)
extern bool wxWasmIsDialogFormControl(const wxWindow* win);

// KICLOUD: LOOK.6: the dashboard's form control height for fields, px, from its one definition
// in wx-dom.js (wxDomRoomyMinHeight); 0 when the page has no wx-dom.js
static int FormControlHeight()
{
    return EM_ASM_INT({ return typeof wxDomRoomyMinHeight === 'function' ? wxDomRoomyMinHeight(0) : 0; });
}

wxChoice::wxChoice() :
    m_selection(wxNOT_FOUND)
{
}

wxChoice::wxChoice(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   int n, const wxString choices[],
                   long style,
                   const wxValidator& validator,
                   const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, pos, size, n, choices, style, validator, name);
}

wxChoice::wxChoice(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   const wxArrayString& choices,
                   long style,
                   const wxValidator& validator,
                   const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

wxChoice::~wxChoice()
{
    // ensure that the client data objects are freed while the control is
    // still alive
    Clear();
}

bool wxChoice::Create(wxWindow *parent, wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      const wxArrayString& choices,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    return Create(parent, id, pos, size, choices.size(),
                  choices.empty() ? NULL : &choices[0],
                  style, validator, name);
}

bool wxChoice::Create(wxWindow *parent, wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      int n, const wxString choices[],
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    WasmCreateDomNode(WasmDomNodeType());

    // KICLOUD: LOOK.6: a dialog's choice or combo box gets the roomy form size (wx-dom.js
    // wxDomSetRoomy); wxComboBox::Create comes through here too
    if ( WasmGetDomId() && wxWasmIsDialogFormControl(this) )
        EM_ASM({ if (typeof wxDomSetRoomy === 'function') wxDomSetRoomy($0); }, WasmGetDomId());

    // Append() goes through DoInsertItems() which pushes the items to the
    // DOM <select>.
    if (n > 0)
        Append(n, choices);

    return true;
}

wxSize wxChoice::DoGetBestSize() const
{
    wxSize best = wxControl::DoGetBestSize();

    // A <select>'s width comes from intrinsic content sizing (available
    // without layout), but its height only resolves once the element is laid
    // out. Best size is frequently queried before that — wxAuiToolBar freezes
    // a control's min size at AddControl time, and panel sizers measure during
    // construction — so the DOM reports ~0 height and the layout pins the
    // control to an unusable sliver (collapsed toolbar dropdowns, overlapping
    // stacked combos). Floor the height to a font-derived control height,
    // which is correct regardless of when best size is measured.
    // KICLOUD: LOOK.6: in a dialog, at least the dashboard's form control height (the floor the
    // DOM measure applies too, wx-dom.js wxDomIntrinsicSize)
    int minHeight = GetCharHeight() + 8;
    if (wxWasmIsDialogFormControl(this))
        minHeight = wxMax(minHeight, FormControlHeight());
    if (best.y < minHeight)
        best.y = minHeight;

    return best;
}

void wxChoice::WasmSyncItems()
{
    if (!WasmGetDomId())
        return;

    // Rebuild the whole <option> list; this wipes the browser's selection
    // state, so re-apply the cached one (selectedIndex = -1 clears it for
    // wxNOT_FOUND).
    wxDomSetItems(WasmGetDomId(), m_items);
    wxDomSetIntValue(WasmGetDomId(), m_selection);
}

unsigned int wxChoice::GetCount() const
{
    return m_items.size();
}

wxString wxChoice::GetString(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), wxString(), INVALID_INDEX_MESSAGE);

    return m_items[n];
}

void wxChoice::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    m_items[n] = s;
    WasmSyncItems();

    InvalidateBestSize();
}

void wxChoice::SetSelection(int n)
{
    // KICLOUD: an index outside the items (e.g. 0 on an empty choice) means no
    // selection, as in the other ports; it must not follow later inserts (B1.7).
    if (n < 0 || n >= static_cast<int>(GetCount()))
        n = wxNOT_FOUND;

    m_selection = n;

    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), n);
}

int wxChoice::GetSelection() const
{
    // The user can change the selection directly in the browser, so the
    // live selectedIndex is the truth when DOM-backed (-1 == wxNOT_FOUND).
    if (WasmGetDomId())
        return wxDomGetIntValue(WasmGetDomId());

    return m_selection;
}

int wxChoice::DoInsertItems(const wxArrayStringsAdapter& items,
                            unsigned int pos,
                            void **clientData,
                            wxClientDataType type)
{
    InvalidateBestSize();

    const int ret = DoInsertItemsInLoop(items, pos, clientData, type);

    WasmSyncItems();

    return ret;
}

int wxChoice::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    // only called from DoInsertItemsInLoop(); DoInsertItems() pushes the
    // rebuilt item list to the DOM once the loop is done
    m_items.Insert(item, pos);
    m_itemsClientData.Insert(NULL, pos);

    // keep the same item selected
    if (m_selection >= static_cast<int>(pos))
        ++m_selection;

    return pos;
}

void wxChoice::DoSetItemClientData(unsigned int n, void *clientData)
{
    m_itemsClientData[n] = clientData;
}

void *wxChoice::DoGetItemClientData(unsigned int n) const
{
    return m_itemsClientData[n];
}

void wxChoice::DoClear()
{
    m_items.Clear();
    m_itemsClientData.Clear();
    m_selection = wxNOT_FOUND;

    WasmSyncItems();
}

void wxChoice::DoDeleteOneItem(unsigned int pos)
{
    wxCHECK_RET(IsValid(pos), INVALID_INDEX_MESSAGE);

    m_items.RemoveAt(pos);
    m_itemsClientData.RemoveAt(pos);

    if (m_selection == static_cast<int>(pos))
        m_selection = wxNOT_FOUND;
    else if (m_selection > static_cast<int>(pos))
        --m_selection;

    WasmSyncItems();
}

void wxChoice::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the picked index into the cache and fire wxEVT_CHOICE,
        // like any port does for user selection.
        m_selection = wxDomGetIntValue(WasmGetDomId());

        wxCommandEvent event(wxEVT_CHOICE, GetId());
        event.SetInt(m_selection);
        if (m_selection >= 0)
            event.SetString(GetString(m_selection));
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_CHOICE
