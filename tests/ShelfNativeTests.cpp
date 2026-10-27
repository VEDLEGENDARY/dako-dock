// Drive the shipping drop target/state with synthetic OLE sources, without a user clipboard or filesystem source.
#include "../src/ShelfModule.cpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>

void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void Complete(shelf::State &state) {
    auto until = GetTickCount64() + 5000;
    while (state.task.Busy() && GetTickCount64() < until) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        state.Poll();
        if (state.task.Busy()) MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    Require(!state.task.Busy(), "drop transfer completed within fixture deadline");
}
int wmain(int argc, wchar_t **argv) {
    clipboard::SetExtraHandler([](clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
        // This test executable accepts only its own synthetic data objects. Shipping code uses ReadDrop's lock checks.
        return shelf::HandleTransfer(operation == clipboard::TransferOp::ReadDrop ? clipboard::TransferOp::FixtureDrop : operation, payload, error);
    });
    constexpr std::wstring_view helper = L"--clipboard-transfer=";
    if (argc == 2 && std::wstring_view(argv[1]).starts_with(helper)) return clipboard::TransferHelper(argv[1] + helper.size());
    if (FAILED(OleInitialize(nullptr))) return 2;
    int code = 0;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Dako synthetic shelf check", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto state = std::make_shared<shelf::State>();
    modules::Ledger ledger;
    try {
        Require(window != nullptr, "owned native fixture window");
        ShowWindow(window, SW_SHOWNOACTIVATE); ledger.Owner(window);
        state->ledger = &ledger; state->descriptor.id = L"shelf";
        state->descriptor.needs = {L"ole-drag-drop", L"session-events", L"clipboard-write"};
        state->visible = true; state->target.Attach(new shelf::Target(state));
        Require(SUCCEEDED(RegisterDragDrop(window, state->target.Get())), "native OLE target registration");
        state->registered = window; ++shelf::drops;
        state->dropToken = ledger.Acquire(state->descriptor, L"ole-drag-drop");
        auto object = shelf::DataObject(clipboard::TextPayload(L"Owned synthetic drop 日本語"));
        DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        Require(SUCCEEDED(state->target->Drop(object.Get(), 0, {}, &effect)) && effect == DROPEFFECT_COPY &&
            state->task.Busy() && state->items.Items().empty(), "drop queues metadata retrieval without waiting on the UI");
        Complete(*state);
        Require(state->items.Items().size() == 1 && state->items.Items().front().text == L"Owned synthetic drop 日本語" &&
            !state->marshaled.Bytes() && ledger.Count(L"shelf") == 1, "completion parks content and releases its transfer lease/marshal");
        state->Clear();
        effect = DROPEFFECT_COPY;
        Require(SUCCEEDED(state->target->Drop(object.Get(), 0, {}, &effect)) && effect == DROPEFFECT_COPY, "second synthetic drop queued");
        state->Clear(); Complete(*state);
        Require(state->items.Items().empty() && !state->marshaled.Bytes(), "clear rejects late data and releases the source");
        state->locked = true; effect = DROPEFFECT_COPY;
        state->target->Drop(object.Get(), 0, {}, &effect);
        Require(effect == DROPEFFECT_NONE && !state->task.Busy(), "locked shelf rejects drops before source capture");
        state->locked = false; state->visible = false; effect = DROPEFFECT_COPY;
        state->target->Drop(object.Get(), 0, {}, &effect);
        Require(effect == DROPEFFECT_NONE, "hidden shelf cannot receive data");
        state->visible = true; effect = DROPEFFECT_MOVE;
        state->target->Drop(object.Get(), 0, {}, &effect);
        Require(effect == DROPEFFECT_NONE, "move-only drops never cause source deletion");
        effect = DROPEFFECT_COPY;
        state->target->Drop(object.Get(), 0, {}, &effect);
        auto retained = state->target;
        state->Cleanup();
        Require(!state->task.Busy() && !state->marshaled.Bytes() && state->items.Items().empty() &&
            ledger.Active().empty() && modules::ShelfModule::Empty(), "removal cancels a pending receive and revokes all resources");
        effect = DROPEFFECT_COPY; retained->Drop(object.Get(), 0, {}, &effect);
        Require(effect == DROPEFFECT_NONE, "retained COM references cannot revive a removed shelf");
        std::puts("PASS native shelf receive, late-clear rejection, lock/hidden gates, copy-only drop and removal during transfer; synthetic data only");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError()); code = 1;
    }
    state->Cleanup(); state.reset();
    if (window) DestroyWindow(window);
    OleUninitialize(); return code;
}
