#pragma once
#include "DockShell.h"
#include <oleacc.h>
#include <atomic>

namespace desktop {
class DockAccessible final : public IAccessible {
    std::atomic<ULONG> references_{1};
    HWND window_;
    LONG focused_ = 0;
    std::wstring rootName_;
    std::function<std::vector<DockHit>()> hits_;
    std::function<float()> scale_;
    static HRESULT String(BSTR *out, const std::wstring &value) {
        if (!out)
            return E_POINTER;
        *out = SysAllocString(value.c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    std::optional<DockHit> Find(VARIANT child) {
        if (child.vt != VT_I4)
            return {};
        for (const auto &hit : hits_())
            if (hit.child == child.lVal)
                return hit;
        return {};
    }

  public:
    DockAccessible(HWND window, std::function<std::vector<DockHit>()> hits, std::function<float()> scale,
                   std::wstring name = L"Dako floating dock")
        : window_(window), rootName_(std::move(name)), hits_(std::move(hits)), scale_(std::move(scale)) {}
    void Focus(LONG child) { focused_ = child; }
    void Disconnect() {
        window_ = nullptr;
        hits_ = [] { return std::vector<DockHit>{}; };
        scale_ = [] { return 1.0f; };
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDispatch && id != IID_IAccessible)
            return E_NOINTERFACE;
        *out = static_cast<IAccessible *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = --references_;
        if (!count)
            delete this;
        return count;
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
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **out) override {
        if (!out)
            return E_POINTER;
        return CreateStdAccessibleObject(window_, OBJID_WINDOW, IID_IDispatch, reinterpret_cast<void **>(out));
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG *out) override {
        if (!out)
            return E_POINTER;
        *out = static_cast<LONG>(hits_().size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR *out) override {
        auto hit = Find(child);
        return String(out, hit ? hit->name : rootName_);
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR *out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT, BSTR *out) override {
        return String(out, L"Arrows or Tab move between controls. Enter activates. Shift+Enter opens a new instance. "
                           L"Ctrl+Left/Right reorders a pinned app. Escape closes previews.");
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = Find(child) ? ROLE_SYSTEM_PUSHBUTTON : ROLE_SYSTEM_TOOLBAR;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        out->vt = VT_I4;
        auto hit = Find(child);
        out->lVal = hit ? STATE_SYSTEM_FOCUSABLE : 0;
        if (hit && hit->active)
            out->lVal |= STATE_SYSTEM_PRESSED;
        if (hit && GetFocus() == window_ && hit->child == focused_)
            out->lVal |= STATE_SYSTEM_FOCUSED;
        if (!IsWindowVisible(window_))
            out->lVal |= STATE_SYSTEM_OFFSCREEN;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *out, VARIANT, LONG *topic) override {
        if (out)
            *out = nullptr;
        if (topic)
            *topic = 0;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR *out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        if (GetFocus() == window_) {
            out->vt = VT_I4;
            out->lVal = focused_;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        for (const auto &hit : hits_())
            if (hit.active) {
                out->vt = VT_I4;
                out->lVal = hit.child;
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR *out) override {
        return String(out, Find(child) ? L"Activate" : L"");
    }
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags, VARIANT child) override {
        auto hit = Find(child);
        if (!hit)
            return E_INVALIDARG;
        if (flags & SELFLAG_TAKEFOCUS) {
            SetForegroundWindow(window_);
            SetFocus(window_);
            PostMessageW(window_, DockInvoke, hit->action, 1);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(LONG *left, LONG *top, LONG *width, LONG *height, VARIANT child) override {
        if (!left || !top || !width || !height)
            return E_POINTER;
        RECT rect{};
        GetWindowRect(window_, &rect);
        auto hit = Find(child);
        if (hit) {
            float scale = scale_();
            *left = rect.left + static_cast<LONG>(hit->rect.left * scale);
            *top = rect.top + static_cast<LONG>(hit->rect.top * scale);
            *width = static_cast<LONG>(hit->rect.Width() * scale);
            *height = static_cast<LONG>(hit->rect.Height() * scale);
        } else {
            *left = rect.left;
            *top = rect.top;
            *width = rect.right - rect.left;
            *height = rect.bottom - rect.top;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction, VARIANT start, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        auto hits = hits_();
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].child == start.lVal) {
                int next = static_cast<int>(i) + (direction == NAVDIR_NEXT || direction == NAVDIR_RIGHT ? 1 : -1);
                if (next >= 0 && next < static_cast<int>(hits.size())) {
                    out->vt = VT_I4;
                    out->lVal = hits[next].child;
                    return S_OK;
                }
            }
        if (!hits.empty() && (direction == NAVDIR_FIRSTCHILD || direction == NAVDIR_LASTCHILD)) {
            out->vt = VT_I4;
            out->lVal = direction == NAVDIR_FIRSTCHILD ? hits.front().child : hits.back().child;
            return S_OK;
        }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x, LONG y, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        POINT point{x, y};
        ScreenToClient(window_, &point);
        float scale = scale_();
        for (const auto &hit : hits_())
            if (hit.rect.Contains(point.x / scale, point.y / scale)) {
                out->vt = VT_I4;
                out->lVal = hit.child;
                return S_OK;
            }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto hit = Find(child);
        if (!hit)
            return E_INVALIDARG;
        PostMessageW(window_, DockInvoke, hit->action, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
};
}
