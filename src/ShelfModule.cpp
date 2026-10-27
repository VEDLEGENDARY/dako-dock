#include "ShelfModule.h"
#include "ShelfOle.h"
#include "ClipboardTask.h"
#include "ClipboardData.h"
#include "ModuleStrings.h"
#include <ole2.h>
#include <wtsapi32.h>
#include <powrprof.h>
#include <dbt.h>
#include <atomic>

namespace shelf {
using modules::String;
using Microsoft::WRL::ComPtr;
inline std::atomic<unsigned> windows = 0, classes = 0, drops = 0, sessions = 0, powers = 0, cleanupFailures = 0;
struct State {
    modules::Ledger *ledger = nullptr;
    modules::Descriptor descriptor;
    clipboard::History items;
    clipboard::Task task;
    MarshaledDrop marshaled;
    ComPtr<IDropTarget> target;
    HWND listener = nullptr, registered = nullptr;
    HPOWERNOTIFY power = nullptr;
    std::wstring className;
    uint64_t sessionToken = 0, dropToken = 0, transferToken = 0, selected = 0, clearGeneration = 0;
    size_t dragBytes = 0;
    uint64_t dropCpu = 0;
    UINT status = IDS_SHELF_HINT;
    bool active = true, fixture = false, diagnostic = false, locked = false, session = false, reading = false, visible = false, capturing = false;
    void Notify() { if (ledger && ledger->Owner()) PostMessageW(ledger->Owner(), modules::Changed, 0, 0); }
    void Clear() { items.Clear(); selected = 0; task.Cancel(); status = IDS_SHELF_CLEARED; Notify(); }
    bool Add(clipboard::Payload payload) {
        if (!Valid(payload)) return false;
        auto item = clipboard::ToItem(std::move(payload));
        if (!item || !items.Add(std::move(*item), false, clipboard::HistoryFlag::Allowed, GetTickCount64())) return false;
        if (!items.Items().front().pinned) items.Pin(items.Items().front().id);
        selected = 0; status = IDS_SHELF_HINT; return true;
    }
    bool Accept() const { return active && !fixture && !diagnostic && !locked && visible && ledger &&
        registered == ledger->Owner() && IsWindowVisible(registered) && !task.Busy() && !capturing; }
    bool BeginDrop(IDataObject *object) {
        if (!Accept()) return false;
        capturing = true;
        auto payload = marshaled.Capture(object);
        capturing = false;
        if (!payload || !Accept()) { marshaled.Clear(); return false; }
        reading = true; clearGeneration = items.ClearGeneration();
        transferToken = ledger->Acquire(descriptor, L"ole-drag-drop");
        if (!task.Start(clipboard::TransferOp::ReadDrop, std::move(*payload), ledger->Owner(), descriptor.id, 2000)) {
            ledger->Release(transferToken); transferToken = 0; marshaled.Clear(); return false;
        }
        status = IDS_SHELF_READING; Notify(); return true;
    }
    void Revoke() noexcept {
        if (registered) {
            if (SUCCEEDED(RevokeDragDrop(registered))) --drops; else ++cleanupFailures;
            registered = nullptr;
        }
        if (dropToken && ledger) ledger->Release(dropToken);
        dropToken = 0;
    }
    void Cleanup() noexcept {
        active = false; visible = false;
        Revoke();
        task.Stop(); marshaled.Clear();
        if (transferToken && ledger) ledger->Release(transferToken);
        transferToken = 0;
        if (power) { if (UnregisterSuspendResumeNotification(power)) --powers; else ++cleanupFailures; power = nullptr; }
        if (session) { if (WTSUnRegisterSessionNotification(listener)) --sessions; else ++cleanupFailures; session = false; }
        if (listener) { if (DestroyWindow(listener)) --windows; else ++cleanupFailures; listener = nullptr; }
        if (!className.empty()) {
            if (UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr))) --classes; else ++cleanupFailures;
            className.clear();
        }
        if (sessionToken && ledger) ledger->Release(sessionToken);
        sessionToken = 0; target.Reset(); items.Clear(); selected = 0; ledger = nullptr;
    }
    void Poll() {
        if (auto reply = task.Consume()) {
            if (transferToken && ledger) ledger->Release(transferToken);
            transferToken = 0; marshaled.Clear();
            if (reading && !locked && items.ClearGeneration() == clearGeneration) {
                status = reply->status == clipboard::TransferStatus::Ok && Add(std::move(reply->payload)) ? IDS_SHELF_HINT : IDS_SHELF_FAILED;
            } else if (!reading && !locked) status = reply->status == clipboard::TransferStatus::Ok ? IDS_COPIED_RESULT : IDS_COPY_FAILED;
        }
    }
};
class Target final : public IDropTarget {
    std::atomic<ULONG> refs_ = 1;
    std::shared_ptr<State> state_;
public:
    explicit Target(std::shared_ptr<State> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDropTarget) return E_NOINTERFACE;
        *out = static_cast<IDropTarget *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --refs_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *, DWORD, POINTL, DWORD *effect) override { return DragOver(0, {}, effect); }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD *effect) override {
        if (!effect) return E_POINTER;
        *effect = state_->Accept() ? (*effect & DROPEFFECT_COPY) : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *object, DWORD, POINTL, DWORD *effect) override {
        if (!effect) return E_POINTER;
        bool allowed = (*effect & DROPEFFECT_COPY) != 0; *effect = DROPEFFECT_NONE;
        auto cpu = [] {
            FILETIME c{}, e{}, k{}, u{}; ULARGE_INTEGER kernel{}, user{};
            if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return uint64_t{0};
            kernel.LowPart = k.dwLowDateTime; kernel.HighPart = k.dwHighDateTime;
            user.LowPart = u.dwLowDateTime; user.HighPart = u.dwHighDateTime;
            return static_cast<uint64_t>(kernel.QuadPart + user.QuadPart);
        };
        auto before = cpu();
        try { if (allowed && state_->BeginDrop(object)) *effect = DROPEFFECT_COPY; }
        catch (...) { state_->capturing = false; state_->status = IDS_SHELF_FAILED; state_->Notify(); }
        auto after = cpu(); if (after >= before) state_->dropCpu += after - before;
        return S_OK;
    }
};
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *state = reinterpret_cast<State *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<State *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state && state->active) {
        if (message == WM_WTSSESSION_CHANGE) {
            if (wparam == WTS_SESSION_UNLOCK || wparam == WTS_SESSION_LOGON) state->locked = false;
            else if (wparam == WTS_SESSION_LOCK || wparam == WTS_SESSION_LOGOFF || wparam == WTS_CONSOLE_DISCONNECT ||
                     wparam == WTS_REMOTE_DISCONNECT) { state->locked = true; state->Clear(); }
        } else if (message == WM_POWERBROADCAST) {
            if (wparam == PBT_APMSUSPEND) { state->locked = true; state->Clear(); }
            if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) state->locked = !clipboard::SessionUnlocked();
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
std::wstring Id(const wchar_t *operation, uint64_t id) { return std::wstring(L"shelf.") + operation + L"-" + std::to_wstring(id); }
std::wstring Label(const clipboard::Item &item) {
    auto text = item.text.substr(0, 100);
    for (auto &c : text) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
    return String(item.kind == clipboard::Kind::Files ? IDS_CLIP_FILES : IDS_CLIP_TEXT) + L" · " + text;
}
}
namespace modules {
bool ShelfModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) {
    state_ = std::make_shared<shelf::State>(); auto &state = *state_;
    state.ledger = &ledger; state.descriptor = descriptor;
    state.fixture = ledger.Fixture() || ledger.Preview(); state.diagnostic = ledger.Diagnostic();
    if (state.fixture) { state.Add(clipboard::TextPayload(L"https://example.invalid/parked-link")); return true; }
    if (FAILED(OleInitialize(nullptr))) return false;
    ole_ = true;
    state.className = L"Dako.Shelf." + std::to_wstring(reinterpret_cast<UINT_PTR>(this));
    WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = shelf::Window;
    type.lpszClassName = state.className.c_str();
    if (!RegisterClassW(&type)) { state.className.clear(); return false; }
    ++shelf::classes;
    state.listener = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, &state);
    if (!state.listener) return false;
    ++shelf::windows;
    state.session = WTSRegisterSessionNotification(state.listener, NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (state.session) ++shelf::sessions;
    state.power = RegisterSuspendResumeNotification(state.listener, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (state.power) ++shelf::powers;
    if (!state.session || !state.power) return false;
    state.sessionToken = ledger.Acquire(descriptor, L"session-events");
    state.locked = !clipboard::SessionUnlocked();
    state.target.Attach(new shelf::Target(state_));
    if (state.diagnostic) {
        if (FAILED(RegisterDragDrop(state.listener, state.target.Get()))) return false;
        state.registered = state.listener; ++shelf::drops;
        state.dropToken = ledger.Acquire(descriptor, L"ole-drag-drop");
    }
    return true;
}
void ShelfModule::Stop() noexcept {
    if (state_) { state_->Cleanup(); state_.reset(); }
    if (ole_) { OleUninitialize(); ole_ = false; }
}
void ShelfModule::Visible(bool visible) {
    auto retained = state_; if (!retained) return; auto &state = *retained;
    state.visible = visible;
    if (state.fixture || state.diagnostic || !state.active) return;
    if (visible && !state.registered && state.ledger->Owner()) {
        auto window = state.ledger->Owner();
        auto target = state.target;
        if (SUCCEEDED(RegisterDragDrop(window, target.Get()))) {
            state.registered = window; ++shelf::drops;
            if (state.active && state.ledger) state.dropToken = state.ledger->Acquire(state.descriptor, L"ole-drag-drop");
            else state.Revoke();
        } else state.status = IDS_SHELF_FAILED;
    } else if (!visible) state.Revoke();
}
void ShelfModule::Poll() {
    auto retained = state_; if (retained) retained->Poll();
}
bool ShelfModule::Invoke(const std::wstring &id, HWND owner) { return InvokeWithText(id, owner, L""); }
bool ShelfModule::InvokeWithText(const std::wstring &id, HWND, const std::wstring &input) {
    if (!state_) return false; auto state = state_;
    if (id == L"shelf.open") return true;
    if (id == L"shelf.clear") { state->Clear(); return true; }
    if (id == L"shelf.back") { state->selected = 0; return true; }
    if (id == L"shelf.add") {
        if (state->locked) return false;
        if (!state->Add(clipboard::TextPayload(input))) { state->selected = 0; state->status = IDS_SHELF_FAILED; }
        return true;
    }
    for (const auto &item : state->items.Items()) {
        if (id == shelf::Id(L"preview", item.id)) { state->selected = item.id; return true; }
        if (id == shelf::Id(L"remove", item.id)) { state->selected = 0; return state->items.Remove(item.id); }
        if (id != shelf::Id(L"copy", item.id)) continue;
        if (state->fixture || state->diagnostic || state->locked || state->task.Busy()) return false;
        state->reading = false;
        state->transferToken = state->ledger->Acquire(state->descriptor, L"clipboard-write");
        if (state->task.Start(clipboard::TransferOp::Write, clipboard::FromItem(item), state->ledger->Owner(), L"shelf")) {
            state->status = IDS_COPYING_RESULT; return true;
        }
        state->ledger->Release(state->transferToken); state->transferToken = 0; return false;
    }
    return false;
}
bool ShelfModule::Drag(const std::wstring &id, HWND) {
    if (!state_) return false; auto state = state_;
    if (state->fixture || state->diagnostic || state->locked || state->task.Busy()) return false;
    for (const auto &item : state->items.Items()) if (id == shelf::Id(L"copy", item.id)) {
        auto payload = clipboard::FromItem(item);
        // The payload is copied before OLE pumps messages; clearing/removing the shelf cannot invalidate it.
        state->dragBytes = 3 * clipboard::TransferLimit;
        auto key = item.id;
        HRESULT result = shelf::Drag(payload, [state, key] { return !state->active || state->locked || !state->items.Find(key); });
        state->dragBytes = 0;
        return result == DRAGDROP_S_DROP || result == DRAGDROP_S_CANCEL;
    }
    return false;
}
std::vector<Action> ShelfModule::Actions() const {
    std::vector<Action> actions; if (!state_) return actions;
    actions.push_back({L"shelf.back", IDS_SHELF_BACK});
    for (const auto &item : state_->items.Items()) {
        actions.push_back({shelf::Id(L"preview", item.id), 0, String(IDS_SHELF_PREVIEW) + L" · " + shelf::Label(item), true});
        actions.push_back({shelf::Id(L"copy", item.id), 0, String(IDS_SHELF_COPY) + L" · " + shelf::Label(item)});
        actions.push_back({shelf::Id(L"remove", item.id), 0, String(IDS_CLIP_REMOVE) + L" · " + shelf::Label(item)});
    }
    return actions;
}
Panel ShelfModule::BuildPanel(const std::wstring &query) const {
    Panel panel{IDS_SHELF, {}}; if (!state_) return panel;
    auto &state = *state_;
    if (auto item = state.items.Find(state.selected)) {
        panel.rows = {{String(IDS_SHELF_COPY), shelf::Id(L"copy", item->id), !state.fixture && !state.diagnostic && !state.locked},
            {String(IDS_CLIP_REMOVE), shelf::Id(L"remove", item->id)}, {String(IDS_SHELF_BACK), L"shelf.back"}};
        for (size_t at = 0; at < item->text.size(); at += 120) panel.rows.push_back({item->text.substr(at, 120), L""});
    } else {
        panel.rows.push_back({String(state.fixture && state.status == IDS_SHELF_HINT ? IDS_SHELF_INERT : state.status), L""});
        for (const auto &entry : state.items.Items()) {
            auto label = shelf::Label(entry);
            if (query.empty() || FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE,
                entry.text.c_str(), -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0) >= 0)
                panel.rows.push_back({std::move(label), shelf::Id(L"preview", entry.id)});
        }
        panel.rows.push_back({String(IDS_SHELF_ADD), L"shelf.add"});
        panel.rows.push_back({String(IDS_SHELF_CLEAR), L"shelf.clear"});
    }
    return panel;
}
size_t ShelfModule::OwnedBytes() const noexcept {
    if (!state_) return 0;
    return sizeof(*state_) + DescriptorAllocations(state_->descriptor) + state_->items.Bytes() + state_->task.Bytes() +
        state_->marshaled.Bytes() + state_->dragBytes + state_->className.capacity() * sizeof(wchar_t);
}
uint64_t ShelfModule::BackgroundCpu100ns() const noexcept { return state_ ? state_->task.Cpu() + state_->dropCpu : 0; }
bool ShelfModule::Empty() { return !shelf::windows && !shelf::classes && !shelf::drops && !shelf::sessions && !shelf::powers && !shelf::cleanupFailures; }
void RegisterShelf(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"shelf"; descriptor.name = IDS_SHELF; descriptor.summary = IDS_SHELF_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM; descriptor.needs = {L"ole-drag-drop", L"session-events", L"clipboard-write"};
    descriptor.budget = {88, 0}; descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"shelf.open", IDS_SHELF, L"", true},
        {L"shelf.add", IDS_SHELF_ADD, L"", true, true, false, IDS_SHELF_INPUT}, {L"shelf.clear", IDS_SHELF_CLEAR}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<ShelfModule>(); });
}
}
