#include "Island.h"

// Standard MSAA children let Windows' UI Automation bridge expose the custom-drawn
// controls as named, focusable buttons and a slider instead of an inaccessible canvas.
class IslandAccessible final : public IAccessible, public IEnumVARIANT {
    ULONG cursor_ = 0;
    std::atomic<ULONG> references_{1};
    HWND window_;
    Island *App() const {
        wchar_t className[64]{};
        if (!IsWindow(window_) || !GetClassNameW(window_, className, 64) || wcscmp(className, WindowClass))
            return nullptr;
        return reinterpret_cast<Island *>(GetWindowLongPtrW(window_, GWLP_USERDATA));
    }
    int Child(VARIANT value, Island *app) const {
        return app && value.vt == VT_I4 && value.lVal > 0 ? app->IndexForChild(value.lVal) : -1;
    }
    HRESULT String(BSTR *output, const std::wstring &value) const {
        if (!output)
            return E_POINTER;
        *output = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
        return *output ? S_OK : E_OUTOFMEMORY;
    }

  public:
    explicit IslandAccessible(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
        if (!output)
            return E_POINTER;
        if (id == IID_IUnknown || id == IID_IDispatch || id == IID_IAccessible) {
            *output = static_cast<IAccessible *>(this);
            AddRef();
            return S_OK;
        }
        if (id == IID_IEnumVARIANT) {
            *output = static_cast<IEnumVARIANT *>(this);
            AddRef();
            return S_OK;
        }
        *output = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, VARIANT *values, ULONG *fetched) override {
        if (!values || (!fetched && count != 1))
            return E_POINTER;
        auto *app = App();
        ULONG n = 0;
        while (app && n < count && cursor_ < app->hits.size()) {
            VariantInit(&values[n]);
            values[n].vt = VT_I4;
            values[n].lVal = app->hits[cursor_++].child;
            ++n;
        }
        if (fetched)
            *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        auto *app = App();
        ULONG size = app ? static_cast<ULONG>(app->hits.size()) : 0, next = std::min(size, cursor_ + count);
        bool full = next - cursor_ == count;
        cursor_ = next;
        return full ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        cursor_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT **output) override {
        if (!output)
            return E_POINTER;
        auto *copy = new IslandAccessible(window_);
        copy->cursor_ = cursor_;
        *output = static_cast<IEnumVARIANT *>(copy);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override {
        if (!count)
            return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
                                     UINT *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **parent) override {
        if (!parent)
            return E_POINTER;
        *parent = nullptr;
        return CreateStdAccessibleObject(window_, OBJID_WINDOW, IID_IDispatch, reinterpret_cast<void **>(parent));
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG *count) override {
        if (!count)
            return E_POINTER;
        auto *app = App();
        *count = app ? static_cast<LONG>(app->hits.size()) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT value, IDispatch **child) override {
        if (!child)
            return E_POINTER;
        *child = nullptr;
        auto *app = App();
        int i = Child(value, app);
        if (i >= 0 && app->hits[i].input)
            return AccessibleObjectFromWindow(app->hits[i].input, static_cast<DWORD>(OBJID_CLIENT), IID_IDispatch,
                                              reinterpret_cast<void **>(child));
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR *name) override {
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0)
            return String(name, app->hits[index].name);
        if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
            return String(name, L"Dako desktop island");
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR *value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0 && app->hits[index].id == Volume)
            return String(value, Number(app->system.volume * 100) + L" percent");
        if (index >= 0 && app->hits[index].id == Brightness)
            return String(value, std::to_wstring(app->controlState.brightness) + L" percent");
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR *description) override {
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0 && app->hits[index].id == Volume)
            return String(description, L"System volume, minimum 0, maximum 100 percent. Left and right arrows adjust "
                                       L"by two percent. Space mutes.");
        if (index >= 0 && app->hits[index].id == Brightness)
            return String(description, L"Selected display brightness. Arrow keys change five percent. Home or End "
                                       L"selects the minimum or maximum.");
        if (index >= 0)
            return String(description,
                          L"F2 reads full details. Tab moves between controls. Page Down/Up scrolls content.");
        return String(description, (app ? app->note : L"") +
                                       L" Use Tab to move between controls, Enter or Space to activate, and "
                                       L"Escape to collapse. Ctrl+Page Down/Up changes views.");
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT *role) override {
        if (!role)
            return E_POINTER;
        VariantInit(role);
        role->vt = VT_I4;
        auto *app = App();
        int index = Child(child, app);
        int id = index >= 0 ? app->hits[index].id : 0;
        role->lVal = index < 0                                               ? ROLE_SYSTEM_PANE
                     : ReadingControl(id)                                    ? ROLE_SYSTEM_STATICTEXT
                     : (id >= 100 && id < 107) || (id >= 2901 && id <= 2904) ? ROLE_SYSTEM_PAGETAB
                     : (id == Volume || id == Brightness)                    ? ROLE_SYSTEM_SLIDER
                     : (id >= 200 && id < 220) || (id >= 900 && id < 1000) || id == FlyoutToggle || id == Dark ||
                             id == Airplane || (id >= 2800 && id < 2807) || (id >= 2920 && id < 2940) || id == 2941
                         ? ROLE_SYSTEM_CHECKBUTTON
                     : id >= 400 && id <= 403 ? ROLE_SYSTEM_TEXT
                     : (id >= 500 && id < 564) || (id >= 2400 && id < 2402) || (id >= 2600 && id < 2620) ||
                             (id >= 2700 && id < 2720) || (id >= 8600 && id < 8625) || (id >= 300 && id < 400) ||
                             (id >= 1000 && id < 1328)
                         ? ROLE_SYSTEM_LISTITEM
                         : ROLE_SYSTEM_PUSHBUTTON;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT *state) override {
        if (!state)
            return E_POINTER;
        VariantInit(state);
        state->vt = VT_I4;
        state->lVal = 0;
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0) {
            int hitId = app->hits[index].id;
            bool reading = ReadingControl(hitId);
            state->lVal = reading ? STATE_SYSTEM_READONLY : STATE_SYSTEM_FOCUSABLE;
            if (!app->VisibleHit(app->hits[index]) ||
                (!app->Actionable() && !(app->hits[index].id == Expand && !app->expanded && !app->YieldResting())))
                state->lVal |= STATE_SYSTEM_OFFSCREEN;
            if (!app->hits[index].enabled && !reading)
                state->lVal |= STATE_SYSTEM_UNAVAILABLE;
            if (app->focused == index && GetFocus() == window_)
                state->lVal |= STATE_SYSTEM_FOCUSED;
            int id = app->hits[index].id;
            if (id == 2941 && app->privacySnapshot.offline)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 2901 && id <= 2904 && id - 2901 == app->featuresPage)
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 2920 && id < 2940) {
                auto descriptors = app->moduleRegistry.Descriptors();
                size_t module = static_cast<size_t>(id - 2920);
                if (module < descriptors.size() && app->moduleRegistry.Enabled(descriptors[module]->id))
                    state->lVal |= STATE_SYSTEM_CHECKED;
            }
            if (id >= 100 && id < 107 && id - 100 == static_cast<int>(app->tab))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 200 && id < 220 && id - 200 < static_cast<int>(app->tasks.size()) && app->tasks[id - 200].done)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 900 && id < 1000 && app->AppIndex(id - 900) < app->apps.size() &&
                app->Favorite(app->apps[app->AppIndex(id - 900)]))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 2800 && id < 2805 && (app->dockOptions.statusMask & (1U << (id - 2800))))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == 2805 && app->dockOptions.showDesktop)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == 2806 && app->dockOptions.showTaskView)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == Dark && app->controlState.dark)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == Airplane && app->controlState.radiosOff)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == FlyoutToggle &&
                (app->flyout == Flyout::Wifi ? app->connections.wifiOn : app->connections.bluetoothOn))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 500 && id < 564 && id - 500 == static_cast<int>(app->selectedWallpaper))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 300 && id < 400 && app->AppIndex(id - 300) < app->apps.size() &&
                app->AppKey(app->apps[app->AppIndex(id - 300)]) == app->selectedAppKey)
                state->lVal |= STATE_SYSTEM_SELECTED;
            if ((id >= 1000 && id < 1128 && id - 1000 == app->selectedWifi) ||
                (id >= 1200 && id < 1328 && id - 1200 == app->selectedBluetooth))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id == 402)
                state->lVal |= STATE_SYSTEM_PROTECTED;
            if (id == 403)
                state->lVal |= STATE_SYSTEM_READONLY;
            if ((id == WallpaperApply && app->wallpaperApplying) ||
                (id == FlyoutConnect && app->connections.wifiConnecting))
                state->lVal |= STATE_SYSTEM_BUSY;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *help) override {
        if (!help)
            return E_POINTER;
        *help = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *file, VARIANT, LONG *topic) override {
        if (file)
            *file = nullptr;
        if (topic)
            *topic = -1;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR *shortcut) override {
        if (!shortcut)
            return E_POINTER;
        *shortcut = nullptr;
        if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
            return String(shortcut, L"Ctrl+Alt+Space");
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *focus) override {
        if (!focus)
            return E_POINTER;
        VariantInit(focus);
        auto *app = App();
        if (GetFocus() == window_) {
            focus->vt = VT_I4;
            focus->lVal = app && app->focused >= 0 ? app->hits[app->focused].child : CHILDID_SELF;
        } else if (app)
            for (const auto &hit : app->hits)
                if (hit.input && hit.input == GetFocus()) {
                    focus->vt = VT_I4;
                    focus->lVal = hit.child;
                    break;
                }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *selection) override {
        if (!selection)
            return E_POINTER;
        VariantInit(selection);
        auto *app = App();
        if (!app)
            return S_FALSE;
        int id = app->flyout == Flyout::Wifi        ? 1000 + app->selectedWifi
                 : app->flyout == Flyout::Bluetooth ? 1200 + app->selectedBluetooth
                 : app->tab == Tab::Wallpaper       ? 500 + static_cast<int>(app->selectedWallpaper)
                                                    : 100 + static_cast<int>(app->tab);
        if (app->tab == Tab::Apps && app->flyout == Flyout::None)
            for (const auto &hit : app->hits)
                if (hit.id >= 300 && hit.id < 400 && app->AppIndex(hit.id - 300) < app->apps.size() &&
                    app->AppKey(app->apps[app->AppIndex(hit.id - 300)]) == app->selectedAppKey) {
                    id = hit.id;
                    break;
                }
        for (const auto &hit : app->hits)
            if (hit.id == id) {
                selection->vt = VT_I4;
                selection->lVal = hit.child;
                return S_OK;
            }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR *action) override {
        if (!action)
            return E_POINTER;
        *action = nullptr;
        auto *app = App();
        int index = Child(child, app);
        return index >= 0 && app->hits[index].enabled
                   ? String(action, app->hits[index].id == Volume ? L"Adjust" : L"Press")
                   : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags, VARIANT child) override {
        auto *app = App();
        int index = Child(child, app);
        if (index < 0 || !app->hits[index].enabled || !(flags & SELFLAG_TAKEFOCUS))
            return E_INVALIDARG;
        PostMessageW(window_, FocusControl, app->hits[index].child, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(LONG *left, LONG *top, LONG *width, LONG *height, VARIANT child) override {
        if (!left || !top || !width || !height)
            return E_POINTER;
        auto *app = App();
        int index = Child(child, app);
        RECT bounds{};
        GetWindowRect(window_, &bounds);
        if (index >= 0) {
            auto r = app->hits[index].rect;
            const auto &h = app->hits[index];
            float extra = std::abs(h.skew) * (r.bottom - r.top) / 2;
            r.left -= extra;
            r.right += extra;
            auto clipped = layout::Intersection({r.left, r.top, r.right, r.bottom},
                                                {h.clip.left, h.clip.top, h.clip.right, h.clip.bottom});
            r = Island::R(clipped);
            float scale = app->dpi / 96.0f, logicalScale = app->expanded ? app->textScale : 1.0f;
            float originX = app->expanded     ? app->PanelX()
                            : app->renderMode ? 0
                                              : (app->envelopeWidth - theme::DockWidth) / 2;
            float originY = app->expanded ? app->PanelY() : app->HandleY();
            *left = bounds.left + static_cast<LONG>((originX + r.left * logicalScale) * scale);
            *top = bounds.top + static_cast<LONG>((originY + r.top * logicalScale) * scale);
            *width = static_cast<LONG>((r.right - r.left) * logicalScale * scale);
            *height = static_cast<LONG>((r.bottom - r.top) * logicalScale * scale);
        } else {
            *left = bounds.left;
            *top = bounds.top;
            *width = bounds.right - bounds.left;
            *height = bounds.bottom - bounds.top;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction, VARIANT start, VARIANT *end) override {
        if (!end)
            return E_POINTER;
        VariantInit(end);
        auto *app = App();
        if (!app || app->hits.empty())
            return S_FALSE;
        int index = Child(start, app), destination = -1;
        if (direction == NAVDIR_FIRSTCHILD)
            destination = 0;
        else if (direction == NAVDIR_LASTCHILD)
            destination = static_cast<int>(app->hits.size()) - 1;
        else if (direction == NAVDIR_NEXT && index >= 0 && index + 1 < static_cast<int>(app->hits.size()))
            destination = index + 1;
        else if (direction == NAVDIR_PREVIOUS && index > 0)
            destination = index - 1;
        if (destination < 0)
            return S_FALSE;
        end->vt = VT_I4;
        end->lVal = app->hits[destination].child;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x, LONG y, VARIANT *child) override {
        if (!child)
            return E_POINTER;
        VariantInit(child);
        auto *app = App();
        if (!app)
            return S_FALSE;
        POINT point{x, y};
        ScreenToClient(window_, &point);
        int index = app->HitAt(point.x * 96.0f / app->dpi, point.y * 96.0f / app->dpi, true);
        child->vt = VT_I4;
        child->lVal = index >= 0 ? app->hits[index].child : CHILDID_SELF;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto *app = App();
        int index = Child(child, app);
        if (index < 0 || !app->hits[index].enabled || !app->CanInvoke(app->hits[index]))
            return E_INVALIDARG;
        PostMessageW(window_, InvokeControl, app->hits[index].child, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
};

void VerifyAccessibility(Island &app) {
    app.accessible.Attach(new IslandAccessible(app.window));
    app.tab = Tab::Controls;
    app.SetExpanded(true);
    app.RebuildLayout();
    auto verify = [&](int action, LONG role, LONG required) {
        auto found = std::find_if(app.hits.begin(), app.hits.end(), [&](const Hit &h) { return h.id == action; });
        if (found == app.hits.end())
            throw winrt::hresult_error(E_UNEXPECTED, L"Missing accessible action " + std::to_wstring(action));
        VARIANT child{}, actual{}, state{};
        child.vt = VT_I4;
        child.lVal = found->child;
        Check(app.accessible->get_accRole(child, &actual));
        Check(app.accessible->get_accState(child, &state));
        if (actual.lVal != role || (state.lVal & required) != required)
            throw winrt::hresult_error(E_UNEXPECTED, L"Incorrect accessible role/state " + std::to_wstring(action));
    };
    verify(100, ROLE_SYSTEM_PAGETAB, STATE_SYSTEM_SELECTED);
    verify(Volume, ROLE_SYSTEM_SLIDER, STATE_SYSTEM_FOCUSABLE);
    app.tab = Tab::Today;
    app.RebuildLayout();
    verify(2001, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2002, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2004, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2005, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2123, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(TodayOptions, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    verify(201, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    verify(400, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE);
    app.tab = Tab::Stats;
    app.RebuildLayout();
    verify(2020, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2024, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    app.tab = Tab::Apps;
    app.RebuildLayout();
    verify(900, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    auto savedRunning = app.runningWindows;
    desktop::WindowEntry fixture;
    fixture.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(90001));
    fixture.title = L"Fixture accessible running app";
    fixture.identity = L"fixture-accessible";
    app.runningWindows = {fixture};
    app.FilterRunning();
    app.RebuildLayout();
    verify(2400, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.runningWindows = std::move(savedRunning);
    app.FilterRunning();
    app.RebuildLayout();
    app.flyout = Flyout::Palette;
    app.paletteQuery = L"Open Today";
    app.BuildPalette();
    app.RebuildLayout();
    verify(401, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE);
    verify(2600, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::DockStatus;
    app.dockOptions.statusMask = 31;
    app.RebuildLayout();
    verify(2800, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    verify(2850, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::Recent;
    app.recentDocuments = {{L"Fixture document", L"fixture:document", L"", L""}};
    app.RebuildLayout();
    verify(2700, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::Features;
    app.featuresPage = 0;
    app.scroll = 0;
    app.RebuildLayout();
    verify(2901, ROLE_SYSTEM_PAGETAB, STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTED);
    verify(2920, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_FOCUSABLE);
    verify(6000, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    app.featuresPage = 1;
    app.RebuildLayout();
    verify(2910, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.moduleRegistry.ApplyPreset(modules::Preset::Taskbar);
    app.flyout = Flyout::Module;
    app.moduleView = L"windows";
    app.modulePage = 0;
    app.modulePanel = *app.moduleRegistry.View(L"windows", L"");
    app.RebuildLayout();
    verify(5100, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
    app.flyout = Flyout::None;
    app.RebuildLayout();
    ComPtr<IEnumVARIANT> enumeration;
    Check(app.accessible.As(&enumeration));
    ULONG fetched = 0;
    VARIANT value{};
    Check(enumeration->Next(1, &value, &fetched));
    if (fetched != 1 || app.IndexForChild(value.lVal) < 0)
        winrt::throw_hresult(E_UNEXPECTED);
    app.SetExpanded(false);
    app.RebuildLayout();
}

IAccessible *CreateIslandAccessible(HWND window) { return new IslandAccessible(window); }
