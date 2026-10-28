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

D2D1_RECT_F Island::TaskCard() const {
    auto b = Body();
    if (LayoutWidth() >= 720)
        return {b.left + (b.right - b.left) * .63f, b.top, b.right, b.top + 292};
    return {b.left, b.top + 744, b.right, b.top + 1104};
}
D2D1_RECT_F Island::TaskListRect() const {
    auto c = TaskCard();
    return {c.left + 8, c.top + 44, c.right - 8, c.bottom - 62};
}
D2D1_RECT_F Island::TaskInputRect() const {
    auto c = TaskCard();
    return {c.left + 12, c.bottom - 12 - std::min(30.0f, Layout().body.Height()), c.right - 52, c.bottom - 12};
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
          std::pair{FocusTwenty, L"Focus: 20 minutes"}, std::pair{TodayClocks, L"World clocks / stopwatch ↗"}})
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
    auto b = Body();
    bool wide = LayoutWidth() >= 720;
    float total = b.right - b.left;
    D2D1_RECT_F left{b.left, b.top, b.left + total * .20f - 12, b.top + 292};
    D2D1_RECT_F center{left.right + 12, b.top, b.left + total * .63f - 12, b.top + 292};
    if (!wide) {
        left = {b.left, b.top, b.right, b.top + 300};
        center = {b.left, b.top + 312, b.right, b.top + 620};
    }
    auto task = TaskCard();
    Fill({left.left, left.top, left.right, left.top + 174}, theme::Card, 16);
    Fill({left.left, left.top + 186, left.right, left.bottom}, theme::Card, 16);
    Fill({center.left, center.top, center.right, center.top + 142}, theme::Card, 16);
    Fill({center.left, center.top + 154, center.right, center.bottom}, theme::Card, 16);
    Fill(task, theme::Card, 16);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (renderMode || fixtureGeometry)
        now = {2026, 10, 2, 27, 9, 41, 0, 0};
    wchar_t hours[8]{}, minutes[8]{}, date[64]{};
    swprintf_s(hours, L"%02u", now.wHour > 12 ? now.wHour - 12 : now.wHour ? now.wHour : 12);
    swprintf_s(minutes, L"%02u", now.wMinute);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"ddd d", date, 64, nullptr);
    Text(hours, {left.left, left.top + 8, left.right, left.top + 66}, type::Display, theme::Text,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"• • •", {left.left, left.top + 64, left.right, left.top + 88}, type::Caption, theme::Accent,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(minutes, {left.left, left.top + 82, left.right, left.top + 140}, type::Display, theme::Text,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(std::wstring(now.wHour < 12 ? L"AM · " : L"PM · ") + date,
         {left.left, left.top + 140, left.right, left.top + 164}, type::Caption, theme::Muted,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    AddHit(2001, left, std::wstring(hours) + L":" + minutes + L". " + date, false);
    SmallButton(TodayOptions, {left.right - 28, left.top + 4, left.right - 4, left.top + 28}, L"\xE713",
                L"Today settings");
    auto weatherText = weather.available ? Number(weather.temperature) + L"°C" : L"—";
    Text(weather.available ? L"☀" : L"☁", {left.left, left.top + 190, left.right, left.top + 225}, type::Metric,
         theme::Accent, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(weatherText, {left.left, left.top + 220, left.right, left.top + 258}, type::Metric, theme::Text,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(weather.available ? WeatherService::Condition(weather.code) : L"Weather is off",
         {left.left, left.top + 260, left.right, left.bottom}, type::Caption, theme::Muted,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    AddHit(2004, {left.left, left.top + 186, left.right, left.bottom}, weatherText, false);
    Text(L"This week", {center.left + 14, center.top + 8, center.right - 14, center.top + 32}, type::Control);
    auto today = calendar::Days(std::chrono::year(now.wYear) / now.wMonth / now.wDay);
    auto monday = today - std::chrono::days((std::chrono::weekday(today).c_encoding() + 6) % 7);
    float pitch = (center.right - center.left - 24) / 7;
    for (int i = 0; i < 7; ++i) {
        auto day = calendar::SystemTime(monday + std::chrono::days(i));
        wchar_t name[16]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &day, L"ddd", name, 16, nullptr);
        float x = center.left + 12 + i * pitch;
        Text(name, {x, center.top + 36, x + pitch, center.top + 54}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        bool selected = day.wDay == now.wDay;
        if (selected)
            Fill({x + 1, center.top + 58, x + pitch - 1, center.top + 88}, theme::Accent, 10);
        Text(Number(day.wDay), {x, center.top + 58, x + pitch, center.top + 88}, type::Caption,
             selected ? theme::AccentInk : theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    std::wstring next = calendarEvents.empty() ? L"No upcoming events" : calendarEvents.front().title;
    std::wstring when = L"Next";
    if (!calendarEvents.empty()) {
        auto eventTime = calendar::SystemTime(calendarEvents.front().start);
        wchar_t formatted[40]{};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &eventTime, nullptr, formatted, 40);
        when += calendarEvents.front().allDay ? L" · All day" : L" · " + std::wstring(formatted);
    }
    Fill({center.left + 10, center.top + 100, center.right - 10, center.top + 132}, theme::CardHover, 10);
    Text(when, {center.left + 20, center.top + 100, center.right - 18, center.top + 116}, type::Caption);
    Text(next, {center.left + 20, center.top + 116, center.right - 18, center.top + 134}, type::Caption, theme::Muted);
    AddHit(2005, {center.left, center.top, center.right, center.top + 142}, L"This week. " + next, false);
    float cy = center.top + 217, cx = center.left + 48;
    DrawRing(cx, cy, 30, focusDuration ? static_cast<float>(focusSeconds) / focusDuration : 0, theme::Accent, 4);
    Text(Clock(focusSeconds), {cx - 30, cy - 15, cx + 30, cy + 15}, type::Control, theme::Text,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    const int presets[]{FocusOne, FocusShort, FocusTen, FocusMedium, FocusTwenty};
    const wchar_t *labels[]{L"1m", L"5m", L"10m", L"15m", L"20m"};
    float x = center.left + 92, span = (center.right - x - 12) / 5;
    float middle = (x + center.right - 12) / 2;
    TextButton(8970, {x, center.top + 164, middle - 4, center.top + 194},
               std::to_wstring(focusDuration / 3600) + L" h");
    TextButton(8971, {middle + 4, center.top + 164, center.right - 12, center.top + 194},
               std::to_wstring(focusDuration / 60 % 60) + L" min");
    for (int i = 0; i < 5; ++i)
        TextButton(presets[i], {x + i * span, center.top + 200, x + (i + 1) * span - 2, center.top + 224}, labels[i]);
    TextButton(FocusStart, {x, center.top + 236, middle - 4, center.top + 268}, focusRunning ? L"Pause" : L"Start",
               true);
    TextButton(FocusReset, {middle + 4, center.top + 236, center.right - 12, center.top + 268}, L"Reset");
    AddHit(2003, {center.left, center.top + 154, center.right, center.bottom}, L"Focus timer. " + Clock(focusSeconds),
           false);
    Text(L"Today", {task.left + 12, task.top + 8, task.right - 64, task.top + 36}, type::Control);
    size_t done =
        static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(), [](const auto &item) { return item.done; }));
    Text(std::to_wstring(done) + L"/" + std::to_wstring(tasks.size()),
         {task.right - 58, task.top + 8, task.right - 12, task.top + 36}, type::Caption, theme::Secondary,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto list = TaskListRect();
    float maxScroll = std::max(0.0f, static_cast<float>(tasks.size()) * 40 - (list.bottom - list.top));
    taskScroll = std::clamp(taskScroll, 0.0f, maxScroll);
    if (!layoutOnly)
        target->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < tasks.size(); ++i) {
        int id = 200 + static_cast<int>(i);
        float y = list.top + static_cast<float>(i) * 40 - taskScroll;
        D2D1_RECT_F row{list.left, y, list.right - 30, y + 36};
        AddHit(id, row, tasks[i].title);
        hits.back().clip = R(layout::Intersection(
            {hits.back().clip.left, hits.back().clip.top, hits.back().clip.right, hits.back().clip.bottom},
            {list.left, list.top - scroll, list.right, list.bottom - scroll}));
        Fill(row, theme::Inset, 10);
        Text(tasks[i].done ? L"✓" : L"○", {row.left + 4, y, row.left + 28, y + 36}, type::Body, theme::Secondary);
        Text(tasks[i].title, {row.left + 32, y, row.right - 6, y + 36}, type::Body,
             tasks[i].done ? theme::Muted : theme::Text);
        FocusRing(id, row);
        SmallButton(220 + static_cast<int>(i), {list.right - 28, y + 2, list.right, y + 34}, L"\xE711",
                    L"Delete " + tasks[i].title);
    }
    if (!layoutOnly)
        target->PopAxisAlignedClip();
    auto input = TaskInputRect();
    Fill({input.left - 4, input.top - 4, task.right - 8, input.bottom + 4}, theme::CardHover, 10);
    AddHit(400, input, L"New task, up to 120 characters", tasks.size() < TaskStore::Capacity);
    if (renderMode)
        Text(L"Add a to-do…", input, type::Caption, theme::Muted);
    SmallButton(AddTask, {task.right - 44, input.top, task.right - 12, input.bottom}, L"\xE72A", L"Add task",
                tasks.size() < TaskStore::Capacity);
    EnsureTaskEdit();
    AddHit(2123, {task.left, task.top, task.right, task.top + 36},
           L"Today tasks. " + std::to_wstring(done) + L" completed", false);
}
