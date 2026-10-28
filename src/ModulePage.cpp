#include "Island.h"
#include "IslandBodyScope.h"

#include "ClipboardData.h"
size_t Island::ModuleSlots() const {
    if (!modulePanel.image)
        return PaletteSlots();
    float body = Body().bottom - Body().top;
    float preview = std::max(24.0f, std::min(96.0f, body - 56));
    return std::min(size_t{6}, static_cast<size_t>(std::max(1.0f, (body - preview - 12) / 44)));
}
void Island::InvokeModuleAction(const std::wstring &id) {
    auto action = moduleRegistry.FindAction(id);
    if (!action)
        return;
    if (id.starts_with(L"clipboard.paste-")) {
        clipboardSelected = std::stoull(id.substr(16));
        ClipboardAction(8755);
        return;
    }
    ModalGuard modal(modalOpen);
    std::wstring input;
    if (action->input || action->confirmation) {
        if (renderMode || fixtureGeometry)
            return;
        auto label = action->text.empty() ? modules::String(action->label) : action->text;
        auto explanation = modules::String(action->help ? action->help : IDS_CONFIRM_ACTION);
        if (!action->details.empty())
            explanation += L"\n\n" + action->details;
        if (action->confirmation &&
            MessageBoxW(window, explanation.c_str(), label.c_str(), MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES)
            return;
        if (action->input) {
            auto value =
                PromptText(window, label, modules::String(action->help ? action->help : IDS_MODULE_INPUT), L"", 4096);
            if (!value)
                return;
            input = *value;
        }
    }
    if (!moduleRegistry.Invoke(id, window, input, action->confirmation)) {
        note = modules::String(IDS_ACTION_FAILED);
        return;
    }
    auto requested = moduleRegistry.TakeView();
    if (!requested.empty())
        OpenModule(requested);
    else if (flyout == Flyout::Module) {
        if (auto panel = moduleRegistry.View(moduleView, moduleQuery))
            modulePanel = std::move(*panel);
        if (modulePage * ModuleSlots() >= modulePanel.rows.size())
            modulePage = 0;
        RebuildLayout();
    }
}
void Island::OpenModule(const std::wstring &id) {
    auto panel = moduleRegistry.View(id, L"");
    if (!panel)
        return;
    miniMode = false;
    flyout = Flyout::Module;
    tab = Tab::Apps;
    moduleView = id;
    moduleQuery.clear();
    modulePage = 0;
    modulePanel = std::move(*panel);
    scroll = 0;
    if (connectivity)
        connectivity->CancelDiscovery();
    SetExpanded(true);
    UpdateTimers();
    RebuildLayout();
    PositionInputs();
    keyboardOpen = true;
    SetForegroundWindow(window);
    SetFocus(window);
    for (size_t index = 0; index < hits.size(); ++index)
        if (hits[index].id >= 5100 && hits[index].id < 5120) {
            FocusAt(static_cast<int>(index));
            break;
        }
    InvalidateRect(window, nullptr, FALSE);
}
void Island::ModuleView() {
    auto layout = Layout();
    auto b = Body();
    if (layout.emergency)
        CompactHeader(true);
    else {
        auto h = layout.header;
        SmallButton(FlyoutBack, {h.left, 8, h.left + 40, 44}, L"\xE72B", modules::String(IDS_BACK));
        Text(modules::String(modulePanel.title), {h.left + 52, 8, h.right, 44}, type::Section);
    }
    {
        DockBodyScope scope(*this);
        float top = b.top;
        if (modulePanel.image) {
            float preview = std::max(24.0f, std::min(96.0f, b.bottom - b.top - 56));
            if (!layoutOnly) {
                const auto &image = *modulePanel.image;
                ComPtr<ID2D1Bitmap> bitmap;
                auto properties =
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(image.width, image.height), image.bgra.data(),
                                                   image.width * 4, properties, &bitmap))) {
                    float scale = std::min((b.right - b.left) / image.width, preview / image.height);
                    target->DrawBitmap(bitmap.Get(),
                                       {b.left, top, b.left + image.width * scale, top + image.height * scale});
                }
            }
            AddHit(7221, {b.left, top, b.right, top + preview}, modules::String(IDS_CLIP_IMAGE), false);
            top += preview + 12;
        }
        for (size_t slot = 0; slot < ModuleSlots() && modulePage * ModuleSlots() + slot < modulePanel.rows.size();
             ++slot) {
            const auto &row = modulePanel.rows[modulePage * ModuleSlots() + slot];
            float y = top + static_cast<float>(slot) * 44;
            if (row.action.empty()) {
                Paragraph(row.label, {b.left, y, b.right, y + 38});
                AddHit(7200 + static_cast<int>(slot), {b.left, y, b.right, y + 38}, row.label, false);
            } else
                TextButton(5100 + static_cast<int>(slot), {b.left, y, b.right, y + 38}, row.label, false, row.enabled);
        }
        if (modulePanel.rows.empty()) {
            auto message = modules::String(IDS_MODULE_EMPTY);
            Paragraph(message, b);
            AddHit(7220, b, std::move(message), false);
        }
    }
    if (!layout.emergency) {
        auto f = Footer();
        TextButton(5150, {f.left, f.top, f.left + 80, f.bottom}, modules::String(IDS_PREVIOUS_ITEMS), false,
                   modulePage > 0);
        TextButton(5152, {f.left + 92, f.top, f.right - 92, f.bottom}, modules::String(IDS_MODULE_SEARCH));
        TextButton(5151, {f.right - 80, f.top, f.right, f.bottom}, modules::String(IDS_NEXT_ITEMS), false,
                   (modulePage + 1) * ModuleSlots() < modulePanel.rows.size());
    }
}
