#pragma once
// Included by Island.cpp after the Island declaration.
void Island::Sparkline(D2D1_RECT_F rect, int metric, unsigned color) {
    const auto &history = hardwareState.history;
    if (!history.Size()) {
        Text(L"Collecting visible samples…", rect, type::Caption, theme::Muted);
        return;
    }
    double maximum = metric == 2 ? 1.0 : 100.0;
    if (metric == 2)
        for (size_t i = 0; i < history.Size(); ++i)
            maximum = std::max(maximum, history.At(i).receive + history.At(i).send);
    bool previousValid = false;
    D2D1_POINT_2F previous{};
    for (size_t i = 0; i < history.Size(); ++i) {
        const auto &point = history.At(i);
        bool valid = metric == 0 ? point.cpuValid : metric == 1 ? point.ramValid : point.networkValid;
        double value = metric == 0 ? point.cpu : metric == 1 ? point.ram : point.receive + point.send;
        D2D1_POINT_2F current{rect.right - (static_cast<float>(history.Size() - 1 - i)) * (rect.right - rect.left) / 59,
                              rect.bottom -
                                  static_cast<float>(std::clamp(value / maximum, 0.0, 1.0)) * (rect.bottom - rect.top)};
        if (valid && previousValid)
            Line(previous.x, previous.y, current.x, current.y, color, 2);
        previous = current;
        previousValid = valid;
    }
}
void Island::Stats() {
    auto b = Body();
    bool wide = LayoutWidth() >= 720;
    float right = wide ? b.left + (b.right - b.left) * .62f : b.right;
    float half = (right - b.left - 12) / 2;
    const telemetry::Point *current =
        hardwareState.history.Size() ? &hardwareState.history.At(hardwareState.history.Size() - 1) : nullptr;
    for (int metric = 0; metric < 2; ++metric) {
        float x = b.left + metric * (half + 12);
        bool valid = metric == 0 ? (current ? current->cpuValid : system.cpuValid)
                                 : (current ? current->ramValid : system.memoryValid);
        double value =
            metric == 0 ? (current ? current->cpu : system.totalCpu) : (current ? current->ram : system.memoryLoad);
        std::wstring label = metric == 0 ? L"CPU" : L"RAM";
        Fill({x, b.top, x + half, b.top + 112}, theme::Card, 14);
        AddHit(2020 + metric, {x, b.top, x + half, b.top + 112},
               label + L". " + (valid ? Number(value, 1) + L" percent" : L"Sample unavailable") +
                   L". 60-second visible history",
               false);
        Text(label, {x + 12, b.top + 6, x + half - 76, b.top + 32}, type::Control);
        Text(valid ? Number(value) + L"%" : L"—", {x + half - 80, b.top + 6, x + half - 12, b.top + 32}, type::Control,
             theme::Text, DWRITE_TEXT_ALIGNMENT_TRAILING);
        Sparkline({x + 12, b.top + 44, x + half - 12, b.top + 96}, metric,
                  metric == 0 ? theme::Accent : theme::Tertiary);
    }
    auto rate = [](double bytes) {
        return bytes >= 1048576 ? Number(bytes / 1048576, 1) + L" MiB/s" : Number(bytes / 1024, 1) + L" KiB/s";
    };
    float networkY = b.top + 124;
    Fill({b.left, networkY, right, networkY + 100}, theme::Inset, 14);
    bool networkValid = current ? current->networkValid : system.networkValid;
    std::wstring transfer = networkValid ? L"↓ " + rate(current ? current->receive : system.receiveRate) + L"   ↑ " +
                                               rate(current ? current->send : system.sendRate)
                                         : L"Sample unavailable";
    AddHit(2024, {b.left, networkY, right, networkY + 100},
           L"Interface transfer. " + transfer + L". 60-second history; combined rate uses an adaptive scale", false);
    Text(L"Network", {b.left + 12, networkY + 4, right - 12, networkY + 26}, type::Secondary, theme::Muted);
    Text(transfer, {b.left + 12, networkY + 26, right - 12, networkY + 50}, type::Control);
    Sparkline({b.left + 12, networkY + 58, right - 12, networkY + 88}, 2, theme::Secondary);
    D2D1_RECT_F process = wide ? D2D1_RECT_F{right + 12, b.top, b.right, b.top + 224}
                               : D2D1_RECT_F{b.left, b.top + 236, b.right, b.top + 460};
    Fill(process, theme::Card, 14);
    Text(L"Processes", {process.left + 12, process.top + 4, process.right - 12, process.top + 30}, type::Control);
    TextButton(StatsSort, {process.left + 12, process.top + 34, process.right - 12, process.top + 66},
               statsMemory ? L"Sort: Memory" : L"Sort: CPU");
    const auto &ranked = statsMemory ? ((fixtureGeometry && hardwareState.memoryTop.empty()) ? hardwareState.top
                                                                                             : hardwareState.memoryTop)
                                     : hardwareState.top;
    for (size_t i = 0; i < std::min(size_t{4}, ranked.size()); ++i) {
        const auto &entry = ranked[i];
        float y = RowY(L"process:" + std::to_wstring(entry.identity), process.top + 74 + static_cast<float>(i) * 34);
        auto value = statsMemory      ? Number(entry.workingMiB) + L" MiB"
                     : entry.cpuValid ? Number(entry.cpu, 1) + L"%"
                                      : L"Sampling…";
        AddHit(2030 + static_cast<int>(i), {process.left, y, process.right, y + 32},
               entry.name + L". PID " + std::to_wstring(entry.identity) + L". " +
                   (statsMemory ? L"Working set " : L"CPU ") + value,
               false);
        Text(entry.name, {process.left + 12, y, process.right - 90, y + 30}, type::Secondary);
        Text(value, {process.right - 90, y, process.right - 12, y + 30}, type::Secondary, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (ranked.empty())
        Paragraph(L"No readable process samples.",
                  {process.left + 12, process.top + 80, process.right - 12, process.bottom - 12});
    float y = b.top + (wide ? 236.0f : 472.0f);
    std::wstring power = !hardwareState.batteryPresent ? L"No battery estimate"
                         : hardwareState.batterySeconds < 0
                             ? L"Battery estimate unavailable"
                             : L"Battery · " + std::to_wstring(hardwareState.batterySeconds / 60) + L" min left";
    std::wstring summary = L"Uptime · " + std::to_wstring(hardwareState.uptimeSeconds / 86400) + L"d " +
                           std::to_wstring(hardwareState.uptimeSeconds / 3600 % 24) + L"h   ·   " + power;
    AddHit(2050, {b.left, y, b.right, y + 32}, summary, false);
    Text(summary, {b.left + 4, y, b.right - 4, y + 32}, type::Secondary, theme::Muted);
    y += 36;
    int columns = wide ? 2 : 1;
    for (size_t i = 0; i < hardwareState.volumes.size(); ++i) {
        const auto &volume = hardwareState.volumes[i];
        auto cell = layout::Column({b.left, y, b.right, y + 44}, static_cast<int>(i % columns), columns);
        cell.top += static_cast<float>(i / columns) * 52;
        cell.bottom = cell.top + 44;
        Fill(R(cell), theme::Inset, 12);
        std::wstring text = volume.root + (volume.valid ? L" · " + Number(volume.usedGiB, 1) + L" / " +
                                                              Number(volume.totalGiB, 1) + L" GiB"
                                                        : L" · Unavailable");
        AddHit(2060 + static_cast<int>(i), R(cell), L"Fixed volume " + text, false);
        Text(text, {cell.left + 12, cell.top, cell.right - 12, cell.bottom}, type::Secondary);
    }
    float gpuY = y + static_cast<float>((hardwareState.volumes.size() + columns - 1) / columns) * 52;
    std::wstring gpu = hardwareState.gpu + L" · " +
                       (hardwareState.gpuValid ? Number(hardwareState.gpuPercent) + L"%" : L"GPU sample unavailable") +
                       L" · " + Number(hardwareState.vramGiB, 1) + L" GiB dedicated VRAM";
    AddHit(2051, {b.left, gpuY, b.right, gpuY + 32}, gpu, false);
    Text(gpu, {b.left + 4, gpuY, b.right - 4, gpuY + 32}, type::Caption, theme::Muted);
}
