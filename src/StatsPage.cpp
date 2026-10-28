#include "Island.h"

void Island::SampleStats() {
    if (renderMode || fixtureGeometry || !expanded || tab != Tab::Stats || flyout != Flyout::None)
        return;
    system.Sample();
    if (!hardware)
        hardware = std::make_unique<HardwareStats>(window);
    hardware->Request();
}

void Island::Sparkline(D2D1_RECT_F rect, int metric, unsigned color) {
    const auto &history = hardwareState.history;
    if (!history.Size()) {
        Text(L"Collecting visible samples…", rect, type::Caption, theme::Muted);
        return;
    }
    bool previousValid = false;
    D2D1_POINT_2F previous{};
    for (size_t i = 0; i < history.Size(); ++i) {
        const auto &point = history.At(i);
        bool valid = metric == 0 ? point.cpuValid : metric == 1 ? point.gpuValid : point.ramValid;
        double value = metric == 0 ? point.cpu : metric == 1 ? point.gpu : point.ram;
        D2D1_POINT_2F current{rect.right - (static_cast<float>(history.Size() - 1 - i)) * (rect.right - rect.left) / 59,
                              rect.bottom -
                                  static_cast<float>(std::clamp(value / 100, 0.0, 1.0)) * (rect.bottom - rect.top)};
        if (valid && previousValid)
            Line(previous.x, previous.y, current.x, current.y, color, 2);
        previous = current;
        previousValid = valid;
    }
}
void Island::Stats() {
    auto columns = layout::StatsColumns(Layout());
    auto ringsRow = layout::StatsRingRects(Layout());
    auto miniRow = layout::StatsMiniRects(Layout());
    auto rightRows = layout::Rows(columns[1], {160, 100});
    double values[]{system.totalCpu, hardwareState.gpuPercent, static_cast<double>(system.memoryLoad)};
    bool valid[]{system.cpuValid, hardwareState.gpuValid, system.memoryValid};
    const wchar_t *names[]{L"CPU", L"GPU", L"RAM"};
    unsigned colors[]{theme::Accent, theme::Secondary, theme::Tertiary};
    for (int i = 0; i < 3; ++i) {
        auto r = ringsRow[i];
        Card(R(r));
        rings[i].goal = valid[i] ? static_cast<float>(values[i] / 100) : 0;
        if (reducedMotion || renderMode || highContrast)
            rings[i].Snap(rings[i].goal);
        else if (!layoutOnly && rings[i].Moving())
            ArmMotion();
        DrawRing(r.CenterX(), r.top + 56, 32, rings[i].value, colors[i], 8);
        Text(valid[i] ? Number(rings[i].value * 100) + L"%" : L"—",
             {r.left + 8, r.top + 40, r.right - 8, r.top + 72}, type::Metric, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(names[i], {r.left + 12, r.top + 100, r.right - 12, r.top + 120}, type::Overline, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring detail = i == 0 ? hardwareState.cpu : i == 1 ? hardwareState.gpu
            : Number(hardwareState.totalGiB - system.availableGiB, 1) + L" / " +
              Number(hardwareState.totalGiB, 1) + L" GB";
        Text(detail, {r.left + 12, r.top + 124, r.right - 12, r.top + 144}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Sparkline({r.left + 12, r.top + 152, r.right - 12, r.bottom - 12}, i, colors[i]);
        AddHit(2020 + i, R(r), std::wstring(names[i]) + L". " + detail + L". " + Number(values[i]) +
               L" percent. " + (i == 0 ? std::to_wstring(hardwareState.processes) + L" processes"
                               : i == 1 ? Number(hardwareState.vramGiB, 1) + L" GB VRAM"
                                        : Number(system.availableGiB, 1) + L" GB free"), false);
    }
    auto rate = [](double n) { return n >= 1048576 ? Number(n / 1048576, 1) + L" MB/s" : Number(n / 1024) + L" KB/s"; };
    std::wstring lower[]{Number(system.diskUsedGiB) + L" / " + Number(system.diskTotalGiB) + L" GB",
                         rate(system.receiveRate) + L" / " + rate(system.sendRate),
                         std::to_wstring(hardwareState.uptimeSeconds / 3600) + L"h " +
                             std::to_wstring(hardwareState.uptimeSeconds / 60 % 60) + L"m"};
    const wchar_t *headers[]{L"DISK", L"NETWORK", L"UP FOR"};
    for (int i = 0; i < 3; ++i) {
        auto r = miniRow[i];
        Card(R(r));
        Text(headers[i], {r.left + 12, r.top + 8, r.right - 12, r.top + 28}, type::Overline, theme::Muted);
        Text(lower[i], {r.left + 12, r.top + 32, r.right - 12, r.top + 56}, type::Body);
        if (i == 0) {
            D2D1_RECT_F track{r.left + 12, r.bottom - 16, r.right - 12, r.bottom - 12};
            Fill(track, theme::Inset, (track.bottom - track.top) / 2);
            float used = system.diskTotalGiB > 0 ? static_cast<float>(system.diskUsedGiB / system.diskTotalGiB) : 0;
            Fill({track.left, track.top, track.left + (track.right - track.left) * std::clamp(used, 0.0f, 1.0f),
                  track.bottom}, theme::Accent, (track.bottom - track.top) / 2);
        }
        AddHit(2024 + i, R(r), std::wstring(headers[i]) + L". " + lower[i], false);
    }
    auto top = rightRows[0];
    Card(R(top));
    Text(L"Top apps", {top.left + 12, top.top + 8, top.right - 108, top.top + 36}, type::Label);
    float split = top.right - 56;
    Fill({top.right - 100, top.top + 8, top.right - 12, top.top + 40}, theme::Inset, layout::ControlRadius);
    D2D1_RECT_F cpu{top.right - 100, top.top + 8, split, top.top + 40};
    D2D1_RECT_F ram{split, top.top + 8, top.right - 12, top.top + 40};
    Fill(statsMemory ? ram : cpu, theme::CardHover, layout::ControlRadius);
    Text(L"CPU", cpu, type::Caption, statsMemory ? theme::Muted : theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"RAM", ram, type::Caption, statsMemory ? theme::Text : theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    AddHit(StatsSort, statsMemory ? cpu : ram, statsMemory ? L"Show CPU processes" : L"Show RAM processes");
    FocusRing(StatsSort, statsMemory ? cpu : ram);
    const auto &ranked = statsMemory && !hardwareState.memoryTop.empty() ? hardwareState.memoryTop : hardwareState.top;
    double maximum = statsMemory && !ranked.empty() ? std::max(1.0, ranked.front().workingMiB) : 100;
    for (size_t i = 0; i < std::min(size_t{4}, ranked.size()); ++i) {
        const auto &entry = ranked[i];
        float y = top.top + 40 + static_cast<float>(i) * 28;
        AddHit(2040 + static_cast<int>(i), {top.left + 12, y, top.right - 12, y + 28},
               entry.name + L". PID " + std::to_wstring(entry.identity), false);
        Text(entry.name, {top.left + 12, y, top.right - 72, y + 24}, type::Caption);
        Text(statsMemory ? Number(entry.workingMiB) + L" MB" : entry.cpuValid ? Number(entry.cpu) + L"%" : L"—",
             {top.right - 72, y, top.right - 12, y + 24}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        float part = static_cast<float>(std::clamp((statsMemory ? entry.workingMiB : entry.cpu) / maximum, 0.0, 1.0));
        Line(top.left + 12, y + 25, top.left + 12 + (top.Width() - 24) * part, y + 25, theme::Accent, 2);
    }
    auto r = rightRows[1];
    Card(R(r));
    auto speed = speedTest ? speedTest->Snapshot() : SpeedTest::Result{};
    Text(L"Internet speed", {r.left + 12, r.top + 4, r.right - 12, r.top + 24}, type::Caption, theme::Muted);
    Text(speed.valid ? Number(speed.down) + L" Mbps" : L"No test yet",
         {r.left + 12, r.top + 24, r.right - 12, r.top + 56}, speed.valid ? type::Metric : type::Body);
    TextButton(2980, {r.left + 12, r.bottom - 40, r.right - 12, r.bottom - 8},
               speed.busy ? L"Testing…" : L"Run speed test", true, !speed.busy);
    AddHit(2051, R(r), speed.message + (speed.valid ? L". Upload " + Number(speed.up) + L" Mbps" : L""), false);
}
