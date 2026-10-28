#include "Island.h"

void Island::EnsureTaskEdit() {
    if (taskEdit || renderMode || capturingPane || layoutOnly)
        return;
    taskEdit = CreateWindowExW(0, L"EDIT", taskDraft.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1,
                               window, reinterpret_cast<HMENU>(400), nullptr, nullptr);
    LabelInput(taskEdit, L"New task, up to 120 characters");
    SendMessageW(taskEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Add a task…"));
    SendMessageW(taskEdit, EM_SETLIMITTEXT, 120, 0);
    EnsureEditResources();
    SendMessageW(taskEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    SendMessageW(taskEdit, EM_SETSEL, draftStart, draftEnd);
    PositionInputs();
}

bool Island::SaveTasks() {
    if (renderMode || previewMode)
        return true;
    std::vector<std::pair<std::wstring, bool>> data;
    for (const auto &task : tasks)
        data.emplace_back(task.title, task.done);
    if (!taskStore.Save(data)) {
        note = L"Could not save tasks. Your draft is kept; try again.";
        return false;
    }
    return true;
}

D2D1_RECT_F Island::TaskCard() const { return R(layout::TodayColumns(Layout())[2]); }
D2D1_RECT_F Island::TaskListRect() const {
    auto c = TaskCard();
    return {c.left + 8, c.top + 44, c.right - 8, c.bottom - (c.right - c.left < 136 ? 92.0f : 62.0f)};
}
D2D1_RECT_F Island::TaskInputRect() const {
    auto c = TaskCard();
    if (c.right - c.left < 136)
        return {c.left + 8, c.bottom - 80, c.right - 8, c.bottom - 48};
    return {c.left + 16, c.bottom - 48, c.right - 56, c.bottom - 16};
}
void Island::ScrollTasks(float amount) {
    taskScroll = std::clamp(
        taskScroll + amount, 0.0f,
        std::max(0.0f, static_cast<float>(tasks.size()) * 40 - (TaskListRect().bottom - TaskListRect().top)));
    RebuildLayout();
    InvalidateRect(window, nullptr, FALSE);
}
void Island::RebuildCalendar() {
    std::wstring content = subscriptionIcs + L"\nBEGIN:VCALENDAR\n";
    for (const auto &[key, value] : localEvents) {
        (void)key;
        auto split = value.find(L'|');
        if (split == std::wstring::npos)
            continue;
        auto title = value.substr(split + 1);
        std::wstring escaped;
        for (wchar_t c : title) {
            if (c == L'\\' || c == L',' || c == L';')
                escaped += L'\\';
            escaped += c;
        }
        content += L"BEGIN:VEVENT\nDTSTART:" + value.substr(0, split) + L"\nSUMMARY:" + escaped + L"\nEND:VEVENT\n";
    }
    content += L"END:VCALENDAR\n";
    auto now = calendar::LocalNow();
    auto date = std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(now));
    auto month = date.year() / date.month() / 1 + std::chrono::months(monthOffset);
    auto first = calendar::Days(month), last = calendar::Days(month + std::chrono::months(1));
    auto result = calendar::Parse(content, std::min(first, std::chrono::floor<std::chrono::days>(now)),
                                  std::max(last, std::chrono::floor<std::chrono::days>(now) + std::chrono::days(90)));
    calendarEvents = std::move(result.events);
    if (result.skipped)
        calendarMessage = L"Some unsupported recurrence rules or time zones were skipped.";
}
void Island::RefreshToday() {
    if (renderMode || fixtureGeometry)
        return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    int date = now.wYear * 10000 + now.wMonth * 100 + now.wDay;
    if (date != todayDateKey) {
        todayDateKey = date;
        RebuildCalendar();
    }
    if (!weatherCity.empty()) {
        if (!weatherService)
            weatherService = std::make_unique<WeatherService>();
        weatherService->Refresh(window, weatherCity, !previewMode);
    }
    if (!calendarUrl.empty()) {
        if (!calendarService)
            calendarService = std::make_unique<CalendarService>();
        calendarService->Refresh(window, calendarUrl);
    }
}
void Island::TodayMenu() {
    ModalGuard guard(modalOpen);
    HMENU menu = CreatePopupMenu();
    for (auto entry :
         {std::pair{WeatherCity, L"Weather city / turn off…"}, std::pair{CalendarFeed, L"Calendar subscription…"},
          std::pair{CalendarAdd, L"Add local event…"}, std::pair{CalendarClear, L"Clear local events…"},
          std::pair{FocusShort, L"Focus: 5 minutes"}, std::pair{FocusMedium, L"Focus: 15 minutes"},
          std::pair{FocusTwenty, L"Focus: 20 minutes"}, std::pair{TodayClocks, L"World clocks / stopwatch"}})
        AppendMenuW(menu, MF_STRING, entry.first, entry.second);
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command) {
        if (command < WeatherCity)
            Invoke(command);
        else
            TodayAction(command);
    }
}
void Island::TodayAction(int id) {
    if (id == 8970 || id == 8971) {
        if (renderMode || fixtureGeometry)
            return;
        auto value = PromptText(window, L"Focus duration", id == 8970 ? L"Hours (0–23)" : L"Minutes (0–59)",
                                std::to_wstring(id == 8970 ? focusDuration / 3600 : focusDuration / 60 % 60), 2);
        if (value)
            try {
                size_t end = 0;
                int number = std::stoi(*value, &end);
                if (end != value->size() || number < 0 || number > (id == 8970 ? 23 : 59))
                    throw std::out_of_range("duration");
                int seconds = id == 8970 ? number * 3600 + (focusDuration / 60 % 60) * 60
                                         : (focusDuration / 3600) * 3600 + number * 60;
                if (seconds > 0) {
                    focusRunning = false;
                    focusSeconds = focusDuration = seconds;
                    UpdateTimers();
                }
            } catch (...) {
                note = L"Enter a duration between one minute and 23 hours 59 minutes.";
            }
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    if (id == TodayOptions) {
        TodayMenu();
        return;
    }
    if (id == TodayClocks) {
        if (!renderMode)
            Open(L"ms-clock:");
        return;
    }
    if (id == CalendarPrevious || id == CalendarNext) {
        monthOffset = std::clamp(monthOffset + (id == CalendarPrevious ? -1 : 1), -12, 12);
        RebuildCalendar();
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (renderMode || fixtureGeometry)
        return;
    ModalGuard modal(modalOpen);
    bool changed = false;
    if (id == WeatherCity) {
        auto value = PromptText(window, L"Weather · Open-Meteo",
                                L"Enter a city to opt in. Only the city and its coordinates are sent to Open-Meteo. "
                                L"Results cache for 30 minutes. Leave blank to turn weather off.",
                                weatherCity, 120);
        if (value) {
            weatherCity = *value;
            weatherService.reset();
            weather = {};
            if (!weatherCity.empty())
                weather.message = L"Loading weather…";
            changed = true;
        }
    }
    if (id == CalendarFeed) {
        auto value = PromptText(window, L"Private calendar subscription",
                                L"Paste an HTTPS ICS subscription URL from Google, Outlook or another calendar. "
                                L"It is saved locally and never logged. Leave blank to disconnect.",
                                calendarUrl, 4096);
        if (value) {
            if (!value->empty() && !value->starts_with(L"https://")) {
                note = L"Use an HTTPS calendar subscription URL.";
                return;
            }
            calendarUrl = *value;
            subscriptionIcs.clear();
            calendarService.reset();
            calendarMessage.clear();
            RebuildCalendar();
            changed = true;
        }
    }
    if (id == CalendarAdd) {
        if (localEvents.size() >= 256) {
            note = L"The local calendar holds 256 events. Clear old events to add more.";
            return;
        }
        auto now = calendar::SystemTime(calendar::LocalNow());
        wchar_t date[32]{};
        swprintf_s(date, L"%04u%02u%02uT090000", now.wYear, now.wMonth, now.wDay);
        auto when = PromptText(window, L"New local event",
                               L"Enter local date/time as YYYYMMDDTHHMMSS, "
                               L"or YYYYMMDD for an all-day event.",
                               date, 16);
        calendar::Seconds parsed;
        if (!when)
            return;
        if (!calendar::Date(*when, parsed)) {
            note = L"Use a valid date such as 20261025T090000.";
            return;
        }
        auto title = PromptText(window, L"Event title", L"This event stays on this PC.", L"", 120);
        if (title && !title->empty()) {
            auto previous = localEvents;
            unsigned key = 0;
            while (localEvents.count(std::to_wstring(key)))
                ++key;
            localEvents[std::to_wstring(key)] = *when + L"|" + *title;
            if (!previewMode && !SettingsStore().Update(L"CalendarEvents", localEvents)) {
                localEvents = std::move(previous);
                note = L"Could not save the event.";
            }
            RebuildCalendar();
        }
    }
    if (id == CalendarClear && !localEvents.empty() &&
        MessageBoxW(window, L"Delete all local calendar events?", L"Dako",
                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) {
        if (previewMode || SettingsStore().Update(L"CalendarEvents", {})) {
            localEvents.clear();
            RebuildCalendar();
        } else
            note = L"Could not clear local events.";
    }
    if (changed) {
        if (!previewMode &&
            !SettingsStore().Update(L"Today", {{L"WeatherCity", weatherCity}, {L"CalendarUrl", calendarUrl}}))
            note = L"Could not save Today settings. Changes apply to this session.";
        RefreshToday();
    }
    RebuildLayout();
    InvalidateRect(window, nullptr, FALSE);
}
void Island::Today() {
    auto columns = layout::TodayColumns(Layout());
    auto leftRows = layout::Rows(columns[0], {140, 120});
    auto centerRows = layout::Rows(columns[1], {124, columns[1].Height() - 136});
    auto clock = leftRows[0], weatherCard = leftRows[1], week = centerRows[0], focus = centerRows[1];
    auto task = TaskCard();
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (renderMode || fixtureGeometry)
        now = {2026, 10, 2, 27, 9, 41, 0, 0};
    wchar_t time[16]{}, date[64]{};
    unsigned hour = now.wHour > 12 ? now.wHour - 12 : now.wHour ? now.wHour : 12;
    swprintf_s(time, L"%02u:%02u", hour, now.wMinute);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"ddd d", date, 64, nullptr);
    {
        PaintGroup reveal(*this, 0);
        Card(R(clock));
        Text(time, {clock.left + 12, clock.top + 32, clock.right - 12, clock.top + 88}, type::Display, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(std::wstring(now.wHour < 12 ? L"AM · " : L"PM · ") + date,
             {clock.left + 12, clock.top + 96, clock.right - 12, clock.bottom - 16}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(2001, R(clock), std::wstring(time) + L". " + date, false);
        SmallButton(TodayOptions, {clock.right - 40, clock.top + 4, clock.right - 8, clock.top + 36}, L"\xE713",
                    L"Today settings");
    }
    {
        PaintGroup reveal(*this, 1);
        Card(R(weatherCard));
        Text(weather.available && weather.code < 2 ? L"\xE706" : L"\xE753",
             {weatherCard.left + 12, weatherCard.top + 12, weatherCard.right - 12, weatherCard.top + 44},
             type::LargeIcon, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        auto weatherText = weather.available ? Number(weather.temperature) + L"°C" : L"—";
        Text(weatherText, {weatherCard.left + 12, weatherCard.top + 44, weatherCard.right - 12, weatherCard.top + 80},
             type::Metric, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(weather.available ? WeatherService::Condition(weather.code) : L"Weather is off",
             {weatherCard.left + 12, weatherCard.top + 84, weatherCard.right - 12, weatherCard.bottom - 12},
             type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(2004, R(weatherCard), weatherText, false);
    }
    {
        PaintGroup reveal(*this, 2);
        Card(R(week));
        Text(L"This week", {week.left + 12, week.top + 8, week.right - 12, week.top + 28}, type::Label);
        auto today = calendar::Days(std::chrono::year(now.wYear) / now.wMonth / now.wDay);
        auto monday = today - std::chrono::days((std::chrono::weekday(today).c_encoding() + 6) % 7);
        float pitch = (week.Width() - 24) / 7;
        for (int i = 0; i < 7; ++i) {
            auto day = calendar::SystemTime(monday + std::chrono::days(i));
            wchar_t name[16]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &day, L"ddd", name, 16, nullptr);
            float x = week.left + 12 + i * pitch;
            Text(name, {x, week.top + 28, x + pitch, week.top + 48}, type::Caption, theme::Muted,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
            D2D1_RECT_F pill{x + 2, week.top + 48, x + pitch - 2, week.top + 80};
            bool selected = day.wDay == now.wDay;
            if (selected)
                Fill(pill, theme::Accent, (pill.bottom - pill.top) / 2);
            Text(Number(day.wDay), pill, type::Label, selected ? theme::AccentInk : theme::Text,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        std::wstring next = calendarEvents.empty() ? L"No upcoming events" : calendarEvents.front().title;
        Text(next, {week.left + 12, week.top + 92, week.right - 12, week.bottom - 12}, type::Caption, theme::Muted);
        AddHit(2005, R(week), L"This week. " + next, false);
    }
    {
        PaintGroup reveal(*this, 3);
        Card(R(focus));
        float cx = focus.left + 48, cy = focus.top + 44;
        if (focus.Width() >= 200) {
            DrawRing(cx, cy, 28, focusDuration ? static_cast<float>(focusSeconds) / focusDuration : 0, theme::Accent,
                     8);
            Text(L"\xE916", {cx - 16, cy - 16, cx + 16, cy + 16}, type::TileIcon, theme::Muted,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        float textLeft = focus.left + (focus.Width() >= 200 ? 92.0f : 12.0f);
        Text(Clock(focusSeconds), {textLeft, focus.top + 4, focus.right - 12, focus.top + 40}, type::Metric,
             theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        float mid = (textLeft + focus.right - 12) / 2;
        bool stacked = focus.Width() < 104;
        TextButton(8970, {textLeft, focus.top + 44, stacked ? focus.right - 12 : mid - 2, focus.top + 76},
                   std::to_wstring(focusDuration / 3600) + L"h");
        TextButton(8971,
                   {stacked ? textLeft : mid + 2, focus.top + (stacked ? 80.0f : 44.0f), focus.right - 12,
                    focus.top + (stacked ? 112.0f : 76.0f)},
                   std::to_wstring(focusDuration / 60 % 60) + L"m");
        const int actions[]{FocusOne, FocusShort, FocusTen, FocusMedium, FocusTwenty, FocusStart, FocusReset};
        const wchar_t *labels[]{L"1m", L"5m", L"10m", L"15m", L"20m"};
        int perRow = std::max(1, std::min(7, static_cast<int>((focus.Width() - 24) / 32)));
        float cell = (focus.Width() - 24) / perRow;
        for (int i = 0; i < 7; ++i) {
            float x = focus.left + 12 + i % perRow * cell;
            float y = focus.top + (stacked ? 124.0f : 88.0f) + i / perRow * 36.0f;
            D2D1_RECT_F rect{x, y, x + cell, y + 32};
            if (i < 5)
                TextButton(actions[i], rect, labels[i]);
            else
                SmallButton(actions[i], rect, i == 5 ? (focusRunning ? L"\xE769" : L"\xE768") : L"\xE777",
                            i == 5 ? (focusRunning ? L"Pause focus" : L"Start focus") : L"Reset focus");
        }
        AddHit(2003, R(focus), L"Focus timer. " + Clock(focusSeconds), false);
    }
    PaintGroup taskReveal(*this, 4);
    Card(task);
    Text(L"Today", {task.left + 16, task.top + 8, task.right - 64, task.top + 36}, type::Title);
    size_t done = static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(), [](const auto &t) { return t.done; }));
    Text(std::to_wstring(done) + L"/" + std::to_wstring(tasks.size()),
         {task.right - 60, task.top + 8, task.right - 16, task.top + 36}, type::Caption, theme::Muted,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto list = TaskListRect();
    float maxScroll = std::max(0.0f, static_cast<float>(tasks.size()) * 40 - (list.bottom - list.top));
    taskScroll = std::clamp(taskScroll, 0.0f, maxScroll);
    if (!layoutOnly)
        target->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < tasks.size(); ++i) {
        int id = 200 + static_cast<int>(i);
        float y = list.top + static_cast<float>(i) * 40 - taskScroll;
        D2D1_RECT_F row{list.left, y, list.right - 36, y + 40};
        auto [completion, inserted] = taskMotion.try_emplace(tasks[i].identity, tasks[i].done ? 1.0f : 0.0f);
        (void)inserted;
        completion->second.goal = tasks[i].done ? 1.0f : 0.0f;
        if (renderMode || reducedMotion || highContrast)
            completion->second.Snap(completion->second.goal);
        float progress = std::clamp(completion->second.value, 0.0f, 1.0f);
        if (!layoutOnly) {
            auto layer = D2D1::LayerParameters();
            layer.opacity = highContrast ? 1.0f : 1 - .4f * progress;
            target->PushLayer(layer, nullptr);
        }
        AddHit(id, row, tasks[i].title);
        hits.back().clip = R(layout::Intersection(
            {hits.back().clip.left, hits.back().clip.top, hits.back().clip.right, hits.back().clip.bottom},
            {list.left, list.top - scroll, list.right, list.bottom - scroll}));
        Fill(row, dock::Mix(theme::Card, theme::CardHover, HoverAmount(id)), layout::ControlRadius);
        if (!layoutOnly) {
            brush->SetColor(Color(tasks[i].done ? theme::Accent : theme::Muted));
            target->DrawEllipse(D2D1::Ellipse({row.left + 16, y + 20}, 8, 8), brush.Get(), 1.5f);
            if (progress > 0) {
                float first = std::min(1.0f, progress * 2), second = std::max(0.0f, progress * 2 - 1);
                target->DrawLine({row.left + 12, y + 20}, {row.left + 12 + 3 * first, y + 20 + 3 * first}, brush.Get(),
                                 1.5f);
                target->DrawLine({row.left + 15, y + 23}, {row.left + 15 + 6 * second, y + 23 - 7 * second},
                                 brush.Get(), 1.5f);
            }
        }
        Text(tasks[i].title, {row.left + 32, y, row.right - 4, y + 40}, type::Body,
             tasks[i].done ? theme::Muted : theme::Text);
        if (!layoutOnly && progress > 0 && !textLayouts.empty()) {
            DWRITE_TEXT_METRICS metrics{};
            textLayouts.back().layout->GetMetrics(&metrics);
            float length = std::min(std::max(0.0f, row.right - row.left - 36), metrics.width);
            Line(row.left + 32, y + 20, row.left + 32 + length * progress, y + 20, theme::Muted);
        }
        FocusRing(id, row);
        SmallButton(220 + static_cast<int>(i), {list.right - 32, y + 4, list.right, y + 36}, L"\xE711",
                    L"Delete " + tasks[i].title);
        if (!layoutOnly)
            target->PopLayer();
    }
    if (!layoutOnly)
        target->PopAxisAlignedClip();
    auto input = TaskInputRect();
    Fill({input.left - 4, input.top - 4, task.right - 12, input.bottom + 4}, theme::Inset, layout::ControlRadius);
    AddHit(400, input, L"New task, up to 120 characters", tasks.size() < TaskStore::Capacity);
    if (renderMode)
        Text(L"Add a to-do…", input, type::Caption, theme::Muted);
    D2D1_RECT_F add = task.right - task.left < 136
                          ? D2D1_RECT_F{task.right - 40, task.bottom - 40, task.right - 8, task.bottom - 8}
                          : D2D1_RECT_F{task.right - 48, input.top, task.right - 16, input.bottom};
    SmallButton(AddTask, add, L"\xE72A", L"Add task", tasks.size() < TaskStore::Capacity);
    EnsureTaskEdit();
    AddHit(2123, {task.left, task.top, task.right, task.top + 36},
           L"Today tasks. " + std::to_wstring(done) + L" completed", false);
}
