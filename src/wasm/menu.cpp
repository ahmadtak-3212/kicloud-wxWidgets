/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/menu.cpp
// Purpose:     wxMenu and wxMenuBar implementations for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MENUS

#include "wx/menu.h"

#ifndef WX_PRECOMP
    #include "wx/frame.h"
#endif

#include "wx/wasm/private/dom.h"

#include <emscripten.h>   // KICLOUD: P3-I, EMSCRIPTEN_KEEPALIVE

#if wxUSE_MENUBAR

// Refresh the DOM menubar (if any) that `menu` ultimately hangs off: a
// submenu's GetMenuBar() walks up to the root menu's bar.
static void DomRefreshMenuBarOf(const wxMenuBase *menu)
{
    wxMenuBar *bar = menu->GetMenuBar();
    if (bar && bar->WasmGetDomId())
        bar->WasmRebuildMenus();
}

#else // !wxUSE_MENUBAR

static inline void DomRefreshMenuBarOf(const wxMenuBase *WXUNUSED(menu)) { }

#endif // wxUSE_MENUBAR/!wxUSE_MENUBAR

// ----------------------------------------------------------------------------
// wxMenu
// ----------------------------------------------------------------------------

// Note: the wxClassInfo for wxMenu, wxMenuBar and wxMenuItem is implemented
// centrally in src/common/menucmn.cpp, so no wxIMPLEMENT_DYNAMIC_CLASS here.

wxMenu::wxMenu(long style)
      : wxMenuBase(style)
{
}

wxMenu::wxMenu(const wxString& title, long style)
      : wxMenuBase(title, style)
{
}

wxMenuItem *wxMenu::DoAppend(wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoAppend(item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

wxMenuItem *wxMenu::DoInsert(size_t pos, wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoInsert(pos, item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

wxMenuItem *wxMenu::DoRemove(wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoRemove(item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

// Serializes this menu's items (recursing into submenus) to the JSON array
// consumed by the DOM menu popups: [{id,label,kind,checked,enabled,items}]
// with kind one of "normal" | "separator" | "check" | "radio" | "submenu".
// Defined outside #if wxUSE_MENUBAR so wxWindowWasm::DoPopupMenu() can use it
// for standalone context menus.
wxString wxMenu::WasmItemsToJson() const
{
    wxString json(wxT("["));

    bool first = true;
    for (wxMenuItemList::compatibility_iterator
            node = GetMenuItems().GetFirst();
         node;
         node = node->GetNext())
    {
        wxMenuItem *item = node->GetData();

        if (!first)
            json += wxT(",");
        first = false;

        const char *kind;
        if (item->IsSeparator())
            kind = "separator";
        else if (item->GetSubMenu())
            kind = "submenu";
        else if (item->GetKind() == wxITEM_CHECK)
            kind = "check";
        else if (item->GetKind() == wxITEM_RADIO)
            kind = "radio";
        else
            kind = "normal";

        // The label is concatenated, NOT passed through wxString::Format:
        // Format returns an empty string when vsnprintf rejects the
        // argument (seen with KiCad menu labels), which silently emitted
        // an empty item object and broke the whole JSON document.
        json += wxString::Format(wxT("{\"id\":%d,\"kind\":\"%s\","),
                                 item->GetId(), kind);
        json += wxT("\"label\":\"");
        // no mnemonics/accelerators in the browser menus (yet)
        json += wxDomJsonEscape(item->GetItemLabelText());
        json += wxT("\"");
        // KICLOUD: P3-K the accelerator text (after the tab in the label) is shown right-aligned
        {
            const wxString full = item->GetItemLabel();
            const int tab = full.Find(wxT('\t'));
            if (tab != wxNOT_FOUND && !item->GetSubMenu())
                json += wxT(",\"accel\":\"") + wxDomJsonEscape(full.Mid(tab + 1));
            else
                json += wxT(",\"accel\":\"");
        }
        json += wxString::Format(
            wxT("\",\"checked\":%s,\"enabled\":%s"),
            item->IsCheckable() && item->IsChecked() ? "true" : "false",
            item->IsEnabled() ? "true" : "false");

        if (item->GetSubMenu())
            json += wxT(",\"items\":") + item->GetSubMenu()->WasmItemsToJson();

        json += wxT("}");
    }

    json += wxT("]");

    return json;
}

// ----------------------------------------------------------------------------
// wxMenuBar
// ----------------------------------------------------------------------------

#if wxUSE_MENUBAR

wxMenuBar::wxMenuBar()
{
}

wxMenuBar::wxMenuBar(long WXUNUSED(style))
{
}

wxMenuBar::wxMenuBar(size_t n, wxMenu *menus[], const wxString titles[],
                     long WXUNUSED(style))
{
    for (size_t i = 0; i < n; i++)
        Append(menus[i], titles[i]);
}

bool wxMenuBar::Append(wxMenu *menu, const wxString& title)
{
    if (!wxMenuBarBase::Append(menu, title))
        return false;

    // the base class only stores the menu, not its title
    menu->SetTitle(title);
    m_enabledTop.push_back(true);

    WasmRebuildMenus();

    return true;
}

bool wxMenuBar::Insert(size_t pos, wxMenu *menu, const wxString& title)
{
    if (!wxMenuBarBase::Insert(pos, menu, title))
        return false;

    menu->SetTitle(title);
    m_enabledTop.insert(m_enabledTop.begin() + pos, true);

    WasmRebuildMenus();

    return true;
}

wxMenu *wxMenuBar::Remove(size_t pos)
{
    wxMenu *menu = wxMenuBarBase::Remove(pos);
    if (menu)
    {
        m_enabledTop.erase(m_enabledTop.begin() + pos);

        WasmRebuildMenus();
    }

    return menu;
}

void wxMenuBar::EnableTop(size_t pos, bool enable)
{
    wxCHECK_RET(pos < m_enabledTop.size(), wxT("invalid menu index"));

    m_enabledTop[pos] = enable;

    WasmRebuildMenus();
}

bool wxMenuBar::IsEnabledTop(size_t pos) const
{
    wxCHECK_MSG(pos < m_enabledTop.size(), true, wxT("invalid menu index"));

    return m_enabledTop[pos];
}

void wxMenuBar::SetMenuLabel(size_t pos, const wxString& label)
{
    wxCHECK_RET(pos < GetMenuCount(), wxT("invalid menu index"));

    GetMenu(pos)->SetTitle(label);

    WasmRebuildMenus();
}

wxString wxMenuBar::GetMenuLabel(size_t pos) const
{
    wxCHECK_MSG(pos < GetMenuCount(), wxString(), wxT("invalid menu index"));

    return GetMenu(pos)->GetTitle();
}

void wxMenuBar::Attach(wxFrame *frame)
{
    wxCHECK_RET(frame, wxT("wxMenuBar::Attach(NULL) called"));

    wxMenuBarBase::Attach(frame);

    // The menubar window is created lazily here: applications construct
    // wxMenuBar without a parent, so the wxWindow part (and its DOM node)
    // can only exist once the frame is known (as in src/univ/menu.cpp).
    if (!IsWasmCreated())
        Create(frame, wxID_ANY);

    if (!WasmGetDomId())
        WasmCreateDomNode("menubar");

    WasmRebuildMenus();

    // give the bar its intrinsic height right away: the frame's
    // PositionMenuBar() only positions and stretches, it never measures
    SetSize(wxDefaultCoord, GetBestSize().y);
}

void wxMenuBar::Detach()
{
    // Here the menu bar is a real child window of the frame (Attach() calls
    // Create(frame)), so unlink it from the parent's child list: the base
    // Detach() only clears m_parent, which would leave a dangling child entry in
    // the frame once the bar is destroyed. The DOM node lives until destruction.
    if ( wxWindow* parent = GetParent() )
        parent->RemoveChild(this);

    wxMenuBarBase::Detach();
}

// KICLOUD: P3-I (docs/patches.md): the menubar's DOM structure is pushed when the menus are
// built, so an item enabled or disabled later (wxUpdateUIEvent: KiCad greys Undo, Delete, Save,
// and every editing item in a read-only view) kept the state it was built with. When a menubar
// menu is opened, the page asks for that menu's items as they are now: the menu's update-UI
// handlers run first, as the native ports do on wxEVT_MENU_OPEN. Returns "" for an unknown bar.
wxWindowWasm *wxDomFindWindowById(int domId);   // domevents.cpp

extern "C" EMSCRIPTEN_KEEPALIVE const char *wxWasmMenuItemsNow(int domId, int pos)
{
    static wxCharBuffer out;
    out = wxCharBuffer("");

    wxMenuBar *bar = wxDynamicCast(wxDomFindWindowById(domId), wxMenuBar);
    if (!bar || pos < 0 || static_cast<size_t>(pos) >= bar->GetMenuCount())
        return out.data();

    wxMenu *menu = bar->GetMenu(pos);
    menu->UpdateUI();
    out = menu->WasmItemsToJson().utf8_str();
    return out.data();
}

void wxMenuBar::WasmRebuildMenus()
{
    if (!WasmGetDomId())
        return;

    wxString json(wxT("["));

    for (size_t pos = 0; pos < GetMenuCount(); pos++)
    {
        if (pos > 0)
            json += wxT(",");

        json += wxString::Format(wxT("{\"title\":\"%s\",\"items\":"),
                                 wxDomJsonEscape(GetMenuLabelText(pos)));
        json += GetMenu(pos)->WasmItemsToJson();
        json += wxT("}");
    }

    json += wxT("]");

    wxDomMenuSetStructure(WasmGetDomId(), json);
    InvalidateBestSize();
}

#if wxUSE_ACCEL

// Depth-first search for an enabled, non-separator item whose parsed
// accelerator ("\tCtrl+S" in the label) matches keyCode+accelFlags.
static wxMenuItem *WasmFindAccelItem(wxMenu *menu, int keyCode, int accelFlags)
{
    for (wxMenuItemList::compatibility_iterator node =
             menu->GetMenuItems().GetFirst();
         node;
         node = node->GetNext())
    {
        wxMenuItem *item = node->GetData();

        if (item->IsSubMenu())
        {
            wxMenuItem *found =
                WasmFindAccelItem(item->GetSubMenu(), keyCode, accelFlags);
            if (found)
                return found;
            continue;
        }

        if (item->IsSeparator() || !item->IsEnabled())
            continue;

        wxAcceleratorEntry *accel = item->GetAccel();
        if (!accel)
            continue;

        int entryKey = accel->GetKeyCode();
        if (entryKey >= 'a' && entryKey <= 'z')
            entryKey -= 'a' - 'A';

        const bool match =
            entryKey == keyCode &&
            (accel->GetFlags() & (wxACCEL_ALT | wxACCEL_CTRL | wxACCEL_SHIFT))
                == accelFlags;
        delete accel;

        if (match)
            return item;
    }

    return NULL;
}

bool wxMenuBar::WasmTranslateAccel(const wxKeyEvent& event)
{
    int keyCode = event.GetKeyCode();
    if (keyCode >= 'a' && keyCode <= 'z')
        keyCode -= 'a' - 'A';

    const int flags = (event.ControlDown() ? wxACCEL_CTRL : 0) |
                      (event.ShiftDown() ? wxACCEL_SHIFT : 0) |
                      (event.AltDown() ? wxACCEL_ALT : 0);

    // Only modifier chords: plain and shift-only keys stay with whatever
    // widget has focus (canvas hotkeys, text entry), matching the pre-accel
    // behavior of the port.
    if (!(flags & (wxACCEL_CTRL | wxACCEL_ALT)))
        return false;

    for (size_t pos = 0; pos < GetMenuCount(); pos++)
    {
        if (!IsEnabledTop(pos))
            continue;

        wxMenuItem *item = WasmFindAccelItem(GetMenu(pos), keyCode, flags);
        if (!item)
            continue;

        // Dispatch like OnDomEvent() does for a DOM menu click.
        const bool checkable = item->IsCheckable();
        if (checkable)
            item->Toggle();

        wxMenu *menu = item->GetMenu();
        if (menu)
            menu->SendEvent(item->GetId(),
                            checkable ? item->IsChecked() : -1);

        if (checkable)
            WasmRebuildMenus();

        return true;
    }

    return false;
}

#else // !wxUSE_ACCEL

bool wxMenuBar::WasmTranslateAccel(const wxKeyEvent& WXUNUSED(event))
{
    return false;
}

#endif // wxUSE_ACCEL/!wxUSE_ACCEL

void wxMenuBar::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_MENU)
    {
        // Dispatch the activated item like the univ port does: toggle
        // checkable items first, then let the menu fire wxEVT_MENU.
        const int id = wxDomGetLastCommandId(WasmGetDomId());

        wxMenu *menu = NULL;
        wxMenuItem *item = FindItem(id, &menu);
        if (item)
        {
            const bool checkable = item->IsCheckable();
            if (checkable)
                item->Toggle();

            // KICLOUD: B1.7 section 1 (Place-menu clicks reached the canvas): the native ports
            // send wxEVT_MENU_HIGHLIGHT while the pointer is over a row, so the chosen item
            // was highlighted just before its wxEVT_MENU. KiCad's ACTION_MENU relies on that
            // to tell a menubar choice (no cursor position) from a hotkey command (the cursor
            // position): without it, Place > Draw Buses started the bus under the menu item.
            if (menu)
            {
                wxMenuEvent highlight(wxEVT_MENU_HIGHLIGHT, id, menu);
                wxMenuBase::ProcessMenuEvent(menu, highlight, menu->GetWindow());
            }

            if (menu)
                menu->SendEvent(id, checkable ? item->IsChecked() : -1);

            // refresh the check mark in the DOM structure
            if (checkable)
                WasmRebuildMenus();
        }
        return;
    }

    wxMenuBarBase::OnDomEvent(kind);
}

wxSize wxMenuBar::DoGetBestSize() const
{
    // DOM-backed bars report their intrinsic (content-driven) size,
    // measured on the live element, like wxControl::DoGetBestSize().
    if (WasmGetDomId())
    {
        int w = 0;
        int h = 0;
        wxDomGetIntrinsicSize(WasmGetDomId(), &w, &h);
        if (w > 0 && h > 0)
            return wxSize(w, h);
    }

    // stub bars (no DOM node yet): a plausible menubar height so
    // wxFrame::PositionMenuBar() keeps the layout sane
    return wxSize(100, 24);
}

#endif // wxUSE_MENUBAR

#endif // wxUSE_MENUS
