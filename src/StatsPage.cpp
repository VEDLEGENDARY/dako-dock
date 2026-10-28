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
    float leftRight = wide ? b.left + (b.right - b.left) * .69f - 12 : b.right;
    float cell = (leftRight - b.left - 16) / 3;
    double values[]{system.totalCpu, hardwareState.gpuPercent, static_cast<double>(system.memoryLoad)};
    bool valid[]{system.cpuValid, hardwareState.gpuValid, system.memoryValid};
    const wchar_t *names[]{L"CPU", L"GPU", L"RAM"};
    unsigned colors[]{theme::Accent, theme::Tertiary, theme::Secondary};
    for (int i = 0; i < 3; ++i) {
        float x = b.left + i * (cell + 8);
        Fill({x, b.top, x + cell, b.top + 178}, theme::Card, 16);
        DrawRing(x + cell / 2, b.top + 56, 32, valid[i] ? static_cast<float>(values[i] / 100) : 0.0f, colors[i], 6);
        Text(valid[i] ? Number(values[i]) + L"%" : L"—", {x, b.top + 40, x + cell, b.top + 72}, type::Control,
             theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(names[i], {x + 6, b.top + 100, x + cell - 6, b.top + 126}, type::Control, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        std::wstring details = i == 0   ? hardwareState.cpu
                               : i == 1 ? hardwareState.gpu
                                        : Number(hardwareState.totalGiB - system.availableGiB, 1) + L" / " +
                                              Number(hardwareState.totalGiB, 1) + L" GB";
        Text(details, {x + 8, b.top + 126, x + cell - 8, b.top + 150}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(i == 0   ? std::to_wstring(hardwareState.processes) + L" proc"
             : i == 1 ? Number(hardwareState.vramGiB, 1) + L" GB VRAM"
                      : Number(system.availableGiB, 1) + L" GB free",
             {x + 4, b.top + 150, x + cell - 4, b.top + 174}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(2020 + i, {x, b.top, x + cell, b.top + 178},
               std::wstring(names[i]) + L". " + details + L". " + Number(values[i]) + L" percent", false);
    }
    auto rate = [](double n) { return n >= 1048576 ? Number(n / 1048576, 1) + L" MB/s" : Number(n / 1024) + L" KB/s"; };
    std::wstring lower[]{system.diskRoot + L" " + Number(system.diskUsedGiB) + L" / " + Number(system.diskTotalGiB) +
                             L" GB",
                         L"↓ " + rate(system.receiveRate) + L" ↑ " + rate(system.sendRate),
                         std::to_wstring(hardwareState.uptimeSeconds / 3600) + L"h " +
                             std::to_wstring(hardwareState.uptimeSeconds / 60 % 60) + L"m"};
    const wchar_t *headers[]{L"DISK", L"NETWORK", L"UP FOR"};
    for (int i = 0; i < 3; ++i) {
        float x = b.left + i * (cell + 8);
        Fill({x, b.top + 188, x + cell, b.top + 246}, theme::Card, 14);
        Text(headers[i], {x + 10, b.top + 192, x + cell - 8, b.top + 212}, type::Caption, theme::Muted);
        Text(lower[i], {x + 10, b.top + 214, x + cell - 8, b.top + 242}, type::Caption);
        AddHit(2024 + i, {x, b.top + 188, x + cell, b.top + 246}, lower[i], false);
    }
    float x = wide ? leftRight + 12 : b.left, y = wide ? b.top : b.top + 260;
    Fill({x, y, b.right, y + 150}, theme::Card, 16);
    Text(L"Top apps · RAM", {x + 12, y + 4, b.right - 64, y + 32}, type::Caption);
    TextButton(StatsSort, {b.right - 62, y + 6, b.right - 10, y + 30}, statsMemory ? L"RAM ⇄" : L"CPU ⇄");
    const auto &ranked = statsMemory && !hardwareState.memoryTop.empty() ? hardwareState.memoryTop : hardwareState.top;
    for (size_t i = 0; i < std::min(size_t{4}, ranked.size()); ++i) {
        auto &entry = ranked[i];
        float row = RowY(L"process:" + std::to_wstring(entry.identity), y + 36 + static_cast<float>(i) * 26);
        AddHit(2040 + static_cast<int>(i), {x + 12, row, b.right - 12, row + 26},
               entry.name + L". PID " + std::to_wstring(entry.identity), false);
        Text(entry.name, {x + 12, row, b.right - 70, row + 22}, type::Caption);
        Text(statsMemory      ? Number(entry.workingMiB) + L" MB"
             : entry.cpuValid ? Number(entry.cpu) + L"%"
                              : L"—",
             {b.right - 70, row, b.right - 12, row + 22}, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        Line(x + 12, row + 23, b.right - 12, row + 23, theme::Secondary, 2);
    }
    float sy = y + 160;
    Fill({x, sy, b.right, sy + 112}, theme::Card, 16);
    Text(L"Internet speed", {x + 12, sy + 4, b.right - 12, sy + 28}, type::Caption, theme::Muted);
    auto speed = speedTest ? speedTest->Snapshot() : SpeedTest::Result{};
    Text(speed.valid ? Number(speed.down) + L" ↓  " + Number(speed.up) + L" ↑ Mbps" : L"No test yet",
         {x + 12, sy + 30, b.right - 12, sy + 60}, type::Control);
    TextButton(2980, {x + 12, sy + 70, b.right - 12, sy + 102}, speed.busy ? L"Testing…" : L"▶ Run speed test", true,
               !speed.busy);
    if (!speed.message.empty())
        AddHit(2051, {x, sy, b.right, sy + 112}, speed.message, false);
}
