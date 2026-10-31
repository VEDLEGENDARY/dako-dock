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
    auto rightRows = layout::Rows(columns[1], {124, 104});
    double values[]{system.totalCpu, hardwareState.gpuPercent, static_cast<double>(system.memoryLoad)};
    bool valid[]{system.cpuValid, hardwareState.gpuValid, system.memoryValid};
    const wchar_t *names[]{L"CPU", L"GPU", L"RAM"};
    unsigned colors[]{theme::Accent, theme::Secondary, theme::Tertiary};
    for (int i = 0; i < 3; ++i) {
        auto r = ringsRow[i];
        PaintGroup reveal(*this, i);
        Card(R(r));
        rings[i].goal = valid[i] ? static_cast<float>(values[i] / 100) : 0;
        if (reducedMotion || renderMode || highContrast)
            rings[i].Snap(rings[i].goal);
        else if (!layoutOnly && rings[i].Moving())
            ArmMotion();
        DrawRing(r.CenterX(), r.top + 56, std::min(40.0f, (r.Width() - 24) / 2), rings[i].value, colors[i], 6);
        int rounded = valid[i] ? static_cast<int>(std::lround(rings[i].value * 100)) : -1;
        if (rounded != percentValues[i] || percentText[i].empty()) {
            percentValues[i] = rounded;
            wchar_t buffer[16]{};
            if (rounded >= 0)
                swprintf_s(buffer, L"%d%%", rounded);
            else
                wcscpy_s(buffer, L"—");
            percentText[i] = buffer;
        }
        Text(percentText[i], {r.left + 8, r.top + 40, r.right - 8, r.top + 72}, type::Title, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(names[i], {r.left + 12, r.top + 104, r.right - 12, r.top + 124}, type::Label, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring detail = i == 0   ? hardwareState.cpu
                              : i == 1 ? hardwareState.gpu
                                       : Number(hardwareState.totalGiB - system.availableGiB, 1) + L" / " +
                                             Number(hardwareState.totalGiB, 1) + L" GB";
        Text(detail, {r.left + 12, r.top + 126, r.right - 12, r.top + 144}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring chip = i == 0   ? std::to_wstring(hardwareState.processes) + L" proc"
                            : i == 1 ? Number(hardwareState.vramGiB, 1) + L" GB VRAM"
                                     : Number(system.availableGiB, 1) + L" GB free";
        float badgeTop = r.bottom - (r.Height() < 200 ? 28 : 48);
        D2D1_RECT_F badge{r.left + 12, badgeTop, r.right - 12, std::min(r.bottom - 8, badgeTop + 24)};
        if (i == 0) {
            float mid = (badge.left + badge.right) / 2;
            D2D1_RECT_F frequency{badge.left, badge.top, mid - 3, badge.bottom};
            badge.left = mid + 3;
            Fill(frequency, theme::CardHover, layout::Metrics::SmallRadius);
            Text(hardwareState.cpuBaseMHz ? Number(hardwareState.cpuBaseMHz / 1000.0, 1) + L" GHz" : L"— GHz",
                 frequency, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        Fill(badge, theme::CardHover, layout::Metrics::SmallRadius);
        Text(chip, badge, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (r.Height() >= 200)
            Sparkline({r.left + 12, r.bottom - 18, r.right - 12, r.bottom - 10}, i, colors[i]);
        AddHit(2020 + i, R(r),
               std::wstring(names[i]) + L". " + detail + L". " + Number(values[i]) + L" percent. " +
                   (i == 0   ? std::to_wstring(hardwareState.processes) + L" processes"
                    : i == 1 ? Number(hardwareState.vramGiB, 1) + L" GB VRAM"
                             : Number(system.availableGiB, 1) + L" GB free") +
                   (i == 0 ? L". Base clock " + Number(hardwareState.cpuBaseMHz / 1000.0, 1) + L" GHz" : L""),
               false);
    }
    auto rate = [](double n) { return n >= 1048576 ? Number(n / 1048576, 1) + L" MB/s" : Number(n / 1024) + L" KB/s"; };
    std::wstring lower[]{Number(system.diskUsedGiB) + L" / " + Number(system.diskTotalGiB) + L" GB",
                         rate(system.receiveRate) + L" / " + rate(system.sendRate),
                         std::to_wstring(hardwareState.uptimeSeconds / 3600) + L"h " +
                             std::to_wstring(hardwareState.uptimeSeconds / 60 % 60) + L"m"};
    const wchar_t *headers[]{L"DISK", L"NETWORK", L"UP FOR"};
    for (int i = 0; i < 3; ++i) {
        auto r = miniRow[i];
        PaintGroup reveal(*this, 3 + i);
        Card(R(r));
        Text(headers[i], {r.left + 12, r.top + 4, r.right - 12, r.top + 20}, type::Overline, theme::Muted);
        if (i == 1 && r.Width() >= 144) {
            float mid = r.CenterX();
            Text(L"\xE896", {r.left + 8, r.top + 22, r.left + 24, r.top + 46}, type::InlineIcon, theme::Muted);
            Text(rate(system.receiveRate), {r.left + 24, r.top + 22, mid - 4, r.top + 46}, type::Caption);
            Text(L"\xE898", {mid, r.top + 22, mid + 16, r.top + 46}, type::InlineIcon, theme::Muted);
            Text(rate(system.sendRate), {mid + 16, r.top + 22, r.right - 8, r.top + 46}, type::Caption);
        } else
            Text(lower[i], {r.left + 12, r.top + 22, r.right - 12, r.top + 46}, type::Body);
        AddHit(2024 + i, R(r), std::wstring(headers[i]) + L". " + lower[i], false);
    }
    auto top = rightRows[0];
    {
        PaintGroup reveal(*this, 6);
        Card(R(top));
        Text(statsMemory ? L"Top apps · RAM" : L"Top apps · CPU",
             {top.left + 12, top.top + 4, top.right - 76, top.top + 28}, type::Caption, theme::Muted);
        D2D1_RECT_F sort{top.right - 72, top.top, top.right - 8, top.top + 32};
        AddHit(StatsSort, sort, statsMemory ? L"Show CPU processes" : L"Show RAM processes");
        Text(statsMemory ? L"CPU" : L"RAM", {sort.left, sort.top, sort.right - 20, sort.bottom}, type::Caption,
             theme::Accent, DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(L"\xE8CB", {sort.right - 16, sort.top, sort.right, sort.bottom}, type::InlineIcon, theme::Accent);
        FocusRing(StatsSort, sort);
        const auto &ranked =
            statsMemory && !hardwareState.memoryTop.empty() ? hardwareState.memoryTop : hardwareState.top;
        double maximum = statsMemory && !ranked.empty() ? std::max(1.0, ranked.front().workingMiB) : 100;
        float rowHeight = std::min(28.0f, (top.Height() - 36) / 4);
        for (size_t i = 0; i < std::min(size_t{4}, ranked.size()); ++i) {
            const auto &entry = ranked[i];
            float y =
                RowY(L"process:" + std::to_wstring(entry.identity), top.top + 32 + static_cast<float>(i) * rowHeight);
            AddHit(2040 + static_cast<int>(i), {top.left + 12, y, top.right - 12, y + rowHeight},
                   entry.name + L". PID " + std::to_wstring(entry.identity), false);
            D2D1_RECT_F icon{top.left + 12, y + 2, top.left + 28, y + 18};
            Fill(icon, theme::CardHover, layout::Metrics::SmallRadius);
            Text(entry.name.substr(0, 1), icon, type::Caption, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(entry.name, {top.left + 40, y, top.right - 72, y + rowHeight - 2}, type::Caption);
            Text(statsMemory      ? Number(entry.workingMiB) + L" MB"
                 : entry.cpuValid ? Number(entry.cpu) + L"%"
                                  : L"—",
                 {top.right - 72, y, top.right - 12, y + rowHeight - 2}, type::Caption, theme::Muted,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
            float part =
                static_cast<float>(std::clamp((statsMemory ? entry.workingMiB : entry.cpu) / maximum, 0.0, 1.0));
            Line(top.left + 40, y + rowHeight - 1, top.left + 40 + (top.Width() - 52) * part, y + rowHeight - 1,
                 statsMemory ? theme::Tertiary : theme::Accent, 2);
        }
    }
    auto r = rightRows[1];
    PaintGroup reveal(*this, 7);
    Card(R(r));
    auto speed = speedTest ? speedTest->Snapshot() : SpeedTest::Result{};
    Text(L"Internet speed", {r.left + 12, r.top + 4, r.right - 12, r.top + 24}, type::Caption, theme::Muted);
    float middle = r.CenterX();
    Text(speed.valid ? Number(speed.down) : L"—", {r.left + 12, r.top + 24, middle - 12, r.top + 56}, type::Metric);
    Text(speed.valid ? Number(speed.up) : L"—", {middle + 12, r.top + 24, r.right - 12, r.top + 56}, type::Metric);
    Text(L"Mbps down", {r.left + 12, r.top + 48, middle - 12, r.top + 64}, type::Caption, theme::Muted);
    Text(L"Mbps up", {middle + 12, r.top + 48, r.right - 12, r.top + 64}, type::Caption, theme::Muted);
    D2D1_RECT_F button{r.left + 12, r.bottom - 40, r.right - 12, r.bottom - 8};
    AddHit(2980, button, L"Run speed test", !speed.busy);
    Fill(Tactile(2980, button), theme::Accent, layout::ControlRadius);
    if (speed.busy)
        Shimmer(button);
    Text(speed.busy ? L"Testing…" : L"Run speed test", button, type::Label, theme::AccentInk,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    FocusRing(2980, button);
    AddHit(2051, R(r), speed.message + (speed.valid ? L". Upload " + Number(speed.up) + L" Mbps" : L""), false);
}
