#pragma once
// Uses the island's shared clipping, scrolling, focus and MSAA controls.
bool Island::ConfirmModule(const std::wstring &id) {
    if (!moduleRegistry.NeedsConsent(id)) return true;
    if (renderMode || fixtureGeometry) return false;
    for (const auto *descriptor : moduleRegistry.Descriptors()) {
        if (descriptor->id != id) continue;
        using modules::String;
        const std::pair<const wchar_t *, UINT> permissions[]{
            {L"keyboard-hook", IDS_PERMISSION_KEYBOARD}, {L"mouse-hook", IDS_PERMISSION_MOUSE},
            {L"clipboard-listener", IDS_PERMISSION_CLIPBOARD}, {L"clipboard-write", IDS_PERMISSION_CLIPBOARD_WRITE},
            {L"file-index-read", IDS_PERMISSION_INDEX},
            {L"process-query", IDS_PERMISSION_PROCESS},
            {L"ole-drag-drop", IDS_PERMISSION_DROP}, {L"session-events", IDS_PERMISSION_SESSION},
            {L"recycle-bin", IDS_PERMISSION_RECYCLE},
            {L"screen-capture", IDS_PERMISSION_CAPTURE},
            {L"network:", IDS_PERMISSION_NETWORK}, {L"file-write:", IDS_PERMISSION_FILES},
            {L"registry-write:", IDS_PERMISSION_REGISTRY}, {L"needs-admin", IDS_PERMISSION_ADMIN},
            {L"power-state", IDS_PERMISSION_POWER}, {L"device-control", IDS_PERMISSION_DEVICE},
            {L"window-event-hook", IDS_PERMISSION_WINDOW}};
        ModalGuard modal(modalOpen);
        for (const auto& capability : descriptor->needs) {
            std::wstring detail = String(descriptor->summary) + L"\n\n" + String(IDS_PERMISSION_HELP) + L"\n\n" + capability;
            for (const auto& [prefix, label] : permissions)
                if (capability.starts_with(prefix)) { detail += L"\n" + String(label); break; }
            if (MessageBoxW(window, detail.c_str(), String(descriptor->name).c_str(),
                            MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION) != IDYES) return false;
        }
        return moduleRegistry.Grant(id);
    }
    return false;
}
void Island::TransferSettings(bool importing) {
    using modules::String;
    if (renderMode || fixtureGeometry || previewMode) return;
    ModalGuard modal(modalOpen);
    if (MessageBoxW(window, String(importing ? IDS_IMPORT_WARNING : IDS_EXPORT_WARNING).c_str(),
                    String(IDS_FEATURES).c_str(), MB_OKCANCEL | MB_DEFBUTTON2 | MB_ICONINFORMATION) != IDOK) return;
    ComPtr<IFileDialog> dialog;
    HRESULT created = importing ? CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(&dialog)) :
                                 CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(&dialog));
    if (FAILED(created)) { note = String(IDS_FILE_ERROR); return; }
    auto label = String(IDS_SETTINGS_FILE);
    COMDLG_FILTERSPEC filter{label.c_str(), L"*.dako"};
    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(L"dako");
    dialog->SetFileName(L"Dako-settings.dako");
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       (importing ? FOS_FILEMUSTEXIST : FOS_OVERWRITEPROMPT));
    if (FAILED(dialog->Show(window))) return;
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        if (path) CoTaskMemFree(path);
        note = String(IDS_FILE_ERROR);
        return;
    }
    bool ok = importing ? SettingsStore(preferencesPath).Import(path) : SettingsStore(preferencesPath).Export(path);
    CoTaskMemFree(path);
    note = String(ok ? (importing ? IDS_IMPORT_SUCCESS : IDS_EXPORT_SUCCESS) : IDS_FILE_ERROR);
    if (ok && importing && SettingsStore(preferencesPath).Get(L"Privacy")[L"Offline"] == L"1")
        privacy::Policy().SetOffline(true);
}
void Island::OpenFeatures() {
    miniMode = false;
    flyout = Flyout::Features;
    tab = Tab::Apps;
    if (connectivity) connectivity->CancelDiscovery();
    scroll = 0;
    moduleCosts.clear();
    modulePanel = {};
    moduleView.clear();
    moduleQuery.clear();
    paletteQuery.clear();
    moduleRegistry.Query(L"");
    paletteItems.clear();
    commandCatalog.Clear();
    privacySnapshot = privacy::Policy().Read();
    privacyPage = 0;
    SetExpanded(true);
    UpdateTimers();
    RebuildLayout();
    PositionInputs();
    keyboardOpen = true;
    SetForegroundWindow(window);
    SetFocus(window);
    FocusAt(IndexForKey(StableKey(2901)));
    InvalidateRect(window, nullptr, FALSE);
}
void Island::FeatureAction(int id) {
    int previousPage = featuresPage;
    if (id == 2900) {
        featuresPage = 0;
        OpenFeatures();
        return;
    }
    if (id >= 2901 && id <= 2904) {
        featuresPage = id - 2901;
        scroll = 0;
        moduleCosts.clear();
    } else if (id >= 2910 && id < 2914) {
        note.clear();
        for (const auto &module : modules::Registry::PresetIds(static_cast<modules::Preset>(id - 2910)))
            if (!ConfirmModule(module)) return;
        if (!moduleRegistry.ApplyPreset(static_cast<modules::Preset>(id - 2910)))
            note = modules::String(IDS_START_FAILED);
        else if (!renderMode && !fixtureGeometry && !previewMode &&
                 !moduleRegistry.Save(SettingsStore(preferencesPath)))
            note = modules::String(IDS_SAVE_FAILED);
        else
            note = modules::String(IDS_PRESET_SAVED);
    } else if (id >= 2920 && id < 2940) {
        note.clear();
        auto descriptors = moduleRegistry.Descriptors();
        size_t index = static_cast<size_t>(id - 2920);
        if (index >= descriptors.size()) return;
        const auto &key = descriptors[index]->id;
        if (!moduleRegistry.Enabled(key) && !ConfirmModule(key)) return;
        bool ok = moduleRegistry.Enabled(key) ? moduleRegistry.Disable(key) : moduleRegistry.Enable(key);
        if (!ok)
            note = modules::String(IDS_START_FAILED);
        else if (!renderMode && !fixtureGeometry && !previewMode &&
                 !moduleRegistry.Save(SettingsStore(preferencesPath)))
            note = modules::String(IDS_SAVE_FAILED);
    } else if (id == 2940 && expanded && flyout == Flyout::Features && featuresPage == 3) {
        for (const auto *descriptor : moduleRegistry.Descriptors())
            moduleCosts[descriptor->id] = moduleRegistry.Sample(descriptor->id);
    } else if (id == 2941) {
        auto &policy = privacy::Policy();
        policy.SetOffline(!policy.Offline());
        if (weatherService) weatherService->Invalidate();
        if (calendarService) calendarService->Invalidate();
        if (!renderMode && !fixtureGeometry && !previewMode &&
            !SettingsStore(preferencesPath).Update(L"Privacy", {{L"Offline", policy.Offline() ? L"1" : L"0"}}))
            note = modules::String(IDS_SAVE_FAILED);
    } else if (id == 2942 || id == 2943) {
        TransferSettings(id == 2943);
    } else if (id == 2944 || id == 2945) {
        if (id == 2944 && privacyPage) --privacyPage;
        if (id == 2945 && (privacyPage + 1) * 8 < privacySnapshot.events.size()) ++privacyPage;
        scroll = 0;
    }
    if (featuresPage == 2) privacySnapshot = privacy::Policy().Read();
    if (featuresPage == 3 && previousPage != 3) moduleRegistry.ResetSamples();
    // No cached palette entry can bypass the registry's enabled-state check.
    BuildPalette();
    RebuildLayout();
    InvalidateRect(window, nullptr, FALSE);
}
void Island::Features() {
    using modules::String;
    auto l = Layout();
    auto b = Body();
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, l.contentHeight - (b.bottom - b.top)));
    if (l.emergency)
        CompactHeader(true);
    else {
        auto h = l.header;
        SmallButton(FlyoutBack, {h.left, 8, h.left + 40, 44}, L"\xE72B", String(IDS_BACK));
        Text(String(IDS_FEATURES), {h.left + 52, 8, h.right, 44}, type::Section);
    }
    DockBodyScope body(*this);
    int reading = 6000 + featuresPage * 256;
    auto paragraph = [&](const std::wstring &value, D2D1_RECT_F rect) {
        Paragraph(value, rect);
        AddHit(reading++, rect, value, false);
    };
    const UINT pages[]{IDS_FEATURES, IDS_PRESETS, IDS_LEDGER, IDS_COSTS};
    float half = (b.right - b.left - 12) / 2;
    for (int i = 0; i < 4; ++i) {
        float x = b.left + static_cast<float>(i % 2) * (half + 12);
        float y = b.top + static_cast<float>(i / 2) * 42;
        TextButton(2901 + i, {x, y, x + half, y + 34}, String(pages[i]), featuresPage == i);
    }
    float y = b.top + 92;
    if (featuresPage == 0) {
        paragraph(String(IDS_FEATURES_INTRO), {b.left, y, b.right, y + 70});
        y += 82;
        size_t index = 0;
        for (const auto *descriptor : moduleRegistry.Descriptors()) {
            bool enabled = moduleRegistry.Enabled(descriptor->id);
            Text(String(descriptor->name), {b.left, y, b.right, y + 28}, type::Section);
            paragraph(String(descriptor->summary), {b.left, y + 30, b.right, y + 92});
            TextButton(2920 + static_cast<int>(index++), {b.left, y + 98, b.right, y + 134},
                String(enabled ? IDS_REMOVE : IDS_ENABLE) + L" · " + String(descriptor->name), enabled);
            y += 158;
        }
        TextButton(2942, {b.left, y, b.right, y + 34}, String(IDS_EXPORT_SETTINGS), false,
                   !previewMode && !renderMode && !fixtureGeometry);
        y += 42;
        TextButton(2943, {b.left, y, b.right, y + 34}, String(IDS_IMPORT_SETTINGS), false,
                   !previewMode && !renderMode && !fixtureGeometry);
        y += 42;
    } else if (featuresPage == 1) {
        const UINT names[]{IDS_MINIMAL, IDS_TASKBAR, IDS_POWER_USER, IDS_CREATOR};
        for (int i = 0; i < 4; ++i) {
            TextButton(2910 + i, {b.left, y, b.right, y + 34},
                String(IDS_APPLY_PRESET) + L": " + String(names[i]),
                !moduleRegistry.FirstRun() && static_cast<int>(moduleRegistry.SelectedPreset()) == i &&
                    moduleRegistry.Enabled(L"windows") == (i != 0));
            y += 42;
        }
        paragraph(String(IDS_PRESET_NOTE), {b.left, y + 8, b.right, y + 158});
        y += 170;
    } else if (featuresPage == 2) {
        const auto& audit = privacySnapshot;
        TextButton(2941, {b.left, y, b.right, y + 34}, String(audit.offline ? IDS_OFFLINE : IDS_ONLINE), audit.offline);
        y += 46;
        TextButton(2944, {b.left, y, b.left + half, y + 34}, String(IDS_PREVIOUS_OPERATIONS), false, privacyPage > 0);
        TextButton(2945, {b.left + half + 12, y, b.right, y + 34}, String(IDS_NEXT_OPERATIONS), false,
                   (privacyPage + 1) * 8 < audit.events.size());
        y += 46;
        if (audit.offline && audit.requests) {
            paragraph(String(IDS_DRAINING), {b.left, y, b.right, y + 76});
            y += 88;
        }
        paragraph(String(IDS_LEDGER_SCOPE), {b.left, y, b.right, y + 90});
        y += 102;
        for (const auto *descriptor : moduleRegistry.Descriptors()) {
            Text(String(descriptor->name), {b.left, y, b.right, y + 28}, type::Section);
            y += 32;
            if (descriptor->needs.empty()) {
                paragraph(String(IDS_NO_CAPABILITIES), {b.left, y, b.right, y + 90});
                y += 102;
            }
            for (const auto &capability : descriptor->needs) {
                bool active = false;
                for (const auto &usage : moduleRegistry.Capabilities().Active())
                    active = active || (usage.module == descriptor->id && usage.capability == capability);
                paragraph(capability + L" · " + String(active ? IDS_ACTIVE :
                    moduleRegistry.Enabled(descriptor->id) ? IDS_INACTIVE : IDS_UNUSED),
                    {b.left, y, b.right, y + 58});
                y += 66;
            }
        }
        for (const auto& active : audit.active) {
            paragraph(active.owner + L" · " + active.capability + L" · " + active.reason + L" · " + String(IDS_ACTIVE),
                      {b.left, y, b.right, y + 76});
            y += 88;
        }
        paragraph(String(IDS_AUDIT_SCOPE) + L" " + String(IDS_OLDER_EVENTS) + L": " + std::to_wstring(audit.omitted),
                  {b.left, y, b.right, y + 100});
        y += 112;
        const UINT results[]{IDS_AUDIT_PENDING, IDS_AUDIT_PENDING, IDS_AUDIT_BLOCKED,
                             IDS_AUDIT_SUCCEEDED, IDS_AUDIT_FAILED};
        size_t index = 0;
        for (auto at = audit.events.rbegin(); at != audit.events.rend(); ++at, ++index) {
            if (index < privacyPage * 8 || index >= (privacyPage + 1) * 8) continue;
            paragraph(at->owner + L" · " + at->capability + L" · " + at->target + L" · " + at->reason + L" · " +
                      String(results[static_cast<size_t>(at->result)]), {b.left, y, b.right, y + 110});
            y += 122;
        }
    } else {
        paragraph(String(IDS_COST_SCOPE), {b.left, y, b.right, y + 120});
        y += 132;
        TextButton(2940, {b.left, y, b.right, y + 34}, String(IDS_REFRESH_COSTS));
        y += 46;
        for (const auto *descriptor : moduleRegistry.Descriptors()) {
            float costTop = y;
            Text(String(descriptor->name), {b.left, y, b.right, y + 28}, type::Section);
            y += 32;
            Text(String(IDS_BUDGET_MIB) + L": " + std::to_wstring(descriptor->budget.ramMiB),
                 {b.left, y, b.right, y + 28}, type::Body);
            y += 32;
            Text(String(IDS_IDLE_BUDGET) + L": " + Number(descriptor->budget.idleCpuPercent, 3),
                 {b.left, y, b.right, y + 28}, type::Body);
            y += 32;
            auto at = moduleCosts.find(descriptor->id);
            if (at != moduleCosts.end()) {
                Text(String(IDS_OWNED_KIB) + L": " + Number(static_cast<double>(at->second.ownedBytes) / 1024, 2),
                    {b.left, y, b.right, y + 28}, type::Body);
                y += 32;
                Text(String(IDS_CPU_PERCENT) + L": " + (at->second.cpuPercent < 0 ? L"—" :
                        Number(at->second.cpuPercent, 4)), {b.left, y, b.right, y + 28}, type::Body);
                y += 32;
                Text(String(IDS_HOOK_MICROS) + L": " + Number(at->second.hookMicroseconds, 2),
                     {b.left, y, b.right, y + 28}, type::Body);
                y += 32;
                Text(String(IDS_ACTION_CPU) + L": " + Number(at->second.actionCpu100ns / 10000.0, 2),
                    {b.left, y, b.right, y + 28}, type::Body);
                y += 32;
            }
            auto cost = moduleCosts.find(descriptor->id);
            AddHit(reading++, {b.left, costTop, b.right, y}, String(descriptor->name) + L". " +
                String(IDS_BUDGET_MIB) + L": " + std::to_wstring(descriptor->budget.ramMiB) +
                (cost == moduleCosts.end() ? L"" : L". " + String(IDS_OWNED_KIB) + L": " +
                    Number(static_cast<double>(cost->second.ownedBytes) / 1024, 2) + L". " +
                    String(IDS_ACTION_CPU) + L": " + Number(cost->second.actionCpu100ns / 10000.0, 2) + L". " +
                    String(IDS_CPU_PERCENT) + L": " + (cost->second.cpuPercent < 0 ? String(IDS_AUDIT_PENDING) :
                        Number(cost->second.cpuPercent, 4)) + L". " + String(IDS_HOOK_MICROS) + L": " +
                    Number(cost->second.hookMicroseconds, 2)), false);
        }
    }
    if (!note.empty()) paragraph(note, {b.left, y + 12, b.right, y + 100});
}
