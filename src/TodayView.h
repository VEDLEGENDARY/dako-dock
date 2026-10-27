#pragma once
// Included by Island.cpp after the Island declaration.
D2D1_RECT_F Island::TaskCard()const{
    auto b=Body();
    if(LayoutWidth()>=720){auto column=layout::Column(Layout().body,2,3);return R({column.left,b.top,column.right,b.top+360});}
    return {b.left,b.top+744,b.right,b.top+1104};
}
D2D1_RECT_F Island::TaskListRect()const{
    auto c=TaskCard();return {c.left+8,c.top+44,c.right-8,c.top+236};
}
D2D1_RECT_F Island::TaskInputRect()const{
    auto c=TaskCard();return {c.left+12,c.bottom-12-std::min(30.0f,Layout().body.Height()),c.right-52,c.bottom-12};
}
void Island::ScrollTasks(float amount){
    taskScroll=std::clamp(taskScroll+amount,0.0f,
        std::max(0.0f,static_cast<float>(tasks.size())*40-(TaskListRect().bottom-TaskListRect().top)));
    RebuildLayout();InvalidateRect(window,nullptr,FALSE);
}
void Island::RebuildCalendar(){
    std::wstring content=subscriptionIcs+L"\nBEGIN:VCALENDAR\n";
    for(const auto& [key,value]:localEvents){
        (void)key;auto split=value.find(L'|');if(split==std::wstring::npos)continue;
        auto title=value.substr(split+1);std::wstring escaped;
        for(wchar_t c:title){if(c==L'\\'||c==L','||c==L';')escaped+=L'\\';escaped+=c;}
        content+=L"BEGIN:VEVENT\nDTSTART:"+value.substr(0,split)+L"\nSUMMARY:"+escaped+L"\nEND:VEVENT\n";
    }
    content+=L"END:VCALENDAR\n";
    auto now=calendar::LocalNow();auto date=std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(now));
    auto month=date.year()/date.month()/1+std::chrono::months(monthOffset);
    auto first=calendar::Days(month),last=calendar::Days(month+std::chrono::months(1));
    auto result=calendar::Parse(content,std::min(first,std::chrono::floor<std::chrono::days>(now)),
        std::max(last,std::chrono::floor<std::chrono::days>(now)+std::chrono::days(90)));
    calendarEvents=std::move(result.events);
    if(result.skipped)calendarMessage=L"Some unsupported recurrence rules or time zones were skipped.";
}
void Island::RefreshToday(){
    if(renderMode||fixtureGeometry)return;
    SYSTEMTIME now{};GetLocalTime(&now);
    int date=now.wYear*10000+now.wMonth*100+now.wDay;
    if(date!=todayDateKey){todayDateKey=date;RebuildCalendar();}
    if(!weatherCity.empty()){
        if(!weatherService)weatherService=std::make_unique<WeatherService>();
        weatherService->Refresh(window,weatherCity,!previewMode);
    }
    if(!calendarUrl.empty()){
        if(!calendarService)calendarService=std::make_unique<CalendarService>();
        calendarService->Refresh(window,calendarUrl);
    }
}
void Island::TodayMenu(){
    ModalGuard guard(modalOpen);HMENU menu=CreatePopupMenu();
    for(auto entry:{std::pair{WeatherCity,L"Weather city / turn off…"},
        std::pair{CalendarFeed,L"Calendar subscription…"},std::pair{CalendarAdd,L"Add local event…"},
        std::pair{CalendarClear,L"Clear local events…"},std::pair{FocusShort,L"Focus: 5 minutes"},
        std::pair{FocusMedium,L"Focus: 15 minutes"},std::pair{FocusTwenty,L"Focus: 20 minutes"},std::pair{TodayClocks,L"World clocks / stopwatch ↗"}})
        AppendMenuW(menu,MF_STRING,entry.first,entry.second);
    POINT point{};GetCursorPos(&point);
    SetForegroundWindow(window);int command=TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,window,nullptr);
    DestroyMenu(menu);if(command){if(command<WeatherCity)Invoke(command);else TodayAction(command);}
}
void Island::TodayAction(int id){
    if(id==TodayOptions){TodayMenu();return;}
    if(id==TodayClocks){if(!renderMode)Open(L"ms-clock:");return;}
    if(id==CalendarPrevious||id==CalendarNext){
        monthOffset=std::clamp(monthOffset+(id==CalendarPrevious?-1:1),-12,12);
        RebuildCalendar();RebuildLayout();InvalidateRect(window,nullptr,FALSE);return;
    }
    if(renderMode||fixtureGeometry)return;
    ModalGuard modal(modalOpen);bool changed=false;
    if(id==WeatherCity){
        auto value=PromptText(window,L"Weather · Open-Meteo",
            L"Enter a city to opt in. Only the city and its coordinates are sent to Open-Meteo. "
            L"Results cache for 30 minutes. Leave blank to turn weather off.",weatherCity,120);
        if(value){weatherCity=*value;weatherService.reset();weather={};
            if(!weatherCity.empty())weather.message=L"Loading weather…";changed=true;}
    }
    if(id==CalendarFeed){
        auto value=PromptText(window,L"Private calendar subscription",
            L"Paste an HTTPS ICS subscription URL from Google, Outlook or another calendar. "
            L"It is saved locally and never logged. Leave blank to disconnect.",calendarUrl,4096);
        if(value){
            if(!value->empty()&&!value->starts_with(L"https://")){
                note=L"Use an HTTPS calendar subscription URL.";return;
            }
            calendarUrl=*value;subscriptionIcs.clear();calendarService.reset();calendarMessage.clear();
            RebuildCalendar();changed=true;
        }
    }
    if(id==CalendarAdd){
        if(localEvents.size()>=256){note=L"The local calendar holds 256 events. Clear old events to add more.";return;}
        auto now=calendar::SystemTime(calendar::LocalNow());wchar_t date[32]{};
        swprintf_s(date,L"%04u%02u%02uT090000",now.wYear,now.wMonth,now.wDay);
        auto when=PromptText(window,L"New local event",L"Enter local date/time as YYYYMMDDTHHMMSS, "
            L"or YYYYMMDD for an all-day event.",date,16);
        calendar::Seconds parsed;
        if(!when)return;
        if(!calendar::Date(*when,parsed)){note=L"Use a valid date such as 20261025T090000.";return;}
        auto title=PromptText(window,L"Event title",L"This event stays on this PC.",L"",120);
        if(title&&!title->empty()){
            auto previous=localEvents;unsigned key=0;while(localEvents.count(std::to_wstring(key)))++key;
            localEvents[std::to_wstring(key)]=*when+L"|"+*title;
            if(!previewMode&&!SettingsStore().Update(L"CalendarEvents",localEvents)){
                localEvents=std::move(previous);note=L"Could not save the event.";
            }
            RebuildCalendar();
        }
    }
    if(id==CalendarClear&&!localEvents.empty()&&MessageBoxW(window,L"Delete all local calendar events?",
        L"Dako",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES){
        if(previewMode||SettingsStore().Update(L"CalendarEvents",{})){localEvents.clear();RebuildCalendar();}
        else note=L"Could not clear local events.";
    }
    if(changed){
        if(!previewMode&&!SettingsStore().Update(L"Today",{{L"WeatherCity",weatherCity},{L"CalendarUrl",calendarUrl}}))
            note=L"Could not save Today settings. Changes apply to this session.";
        RefreshToday();
    }
    RebuildLayout();InvalidateRect(window,nullptr,FALSE);
}
void Island::Today(){
    auto b=Body();bool wide=LayoutWidth()>=720;
    auto left=R(layout::Column(Layout().body,0,3)),center=R(layout::Column(Layout().body,1,3));
    if(!wide){left={b.left,b.top,b.right,b.top+360};center={b.left,b.top+372,b.right,b.top+732};}
    else{left.bottom=left.top+360;center.bottom=center.top+360;}
    auto task=TaskCard();Fill(left,theme::Card,14);Fill(center,theme::Card,14);Fill(task,theme::Card,14);
    SYSTEMTIME now{};GetLocalTime(&now);if(renderMode||fixtureGeometry)now={2026,10,5,23,14,42,0,0};
    wchar_t time[96]{},date[128]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,time,96);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_LONGDATE,&now,nullptr,date,128,nullptr);
    Text(time,{left.left+12,left.top+14,left.right-12,left.top+76},type::Display);
    Paragraph(date,{left.left+12,left.top+80,left.right-12,left.top+136});
    AddHit(2001,{left.left,left.top,left.right,left.top+136},std::wstring(time)+L". "+date,false);
    Line(left.left+12,left.top+148,left.right-12,left.top+148,theme::Border);
    std::wstring weatherText=weather.available?Number(weather.temperature)+L"°C · "+
        WeatherService::Condition(weather.code):weatherCity.empty()?L"Weather is off":weather.message;
    Text(weather.available?weather.city:L"WEATHER",{left.left+12,left.top+158,left.right-12,left.top+184},
        type::Caption,theme::Muted);
    Paragraph(weatherText,{left.left+12,left.top+186,left.right-12,left.top+238},theme::Text);
    std::wstring range=weather.available?L"High "+Number(weather.high)+L"° · Low "+Number(weather.low)+L"°":
        L"Choose a city to opt in";
    Text(range,{left.left+12,left.top+240,left.right-12,left.top+266},type::Caption,theme::Muted);
    AddHit(2004,{left.left,left.top+154,left.right,left.top+266},weatherText+L". "+range+L". "+weather.message,false);
    TextButton(WeatherCity,{left.left+12,left.top+276,left.right-12,left.top+306},L"Weather settings…");
    TextButton(TodayClocks,{left.left+12,left.top+316,left.right-12,left.top+346},L"Clocks / stopwatch ↗");

    auto month=std::chrono::year(now.wYear)/now.wMonth/1+std::chrono::months(monthOffset);
    auto first=calendar::Days(month);auto next=calendar::Days(month+std::chrono::months(1));
    auto monthTime=calendar::SystemTime(first);wchar_t label[80]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&monthTime,L"MMMM yyyy",label,80,nullptr);
    Text(label,{center.left+12,center.top+8,center.right-72,center.top+36},type::Control);
    SmallButton(CalendarPrevious,{center.right-68,center.top+8,center.right-42,center.top+36},L"\xE76B",
        L"Previous month",monthOffset>-12);
    SmallButton(CalendarNext,{center.right-36,center.top+8,center.right-10,center.top+36},L"\xE76C",
        L"Next month",monthOffset<12);
    DWORD week=0;GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_IFIRSTDAYOFWEEK|LOCALE_RETURN_NUMBER,
        reinterpret_cast<LPWSTR>(&week),sizeof(week)/sizeof(wchar_t));
    int firstWeek=(static_cast<int>(week)+1)%7;
    int start=(static_cast<int>(std::chrono::weekday(first).c_encoding())-firstWeek+7)%7;
    float pitch=(center.right-center.left-24)/7;
    for(int i=0;i<7;++i){
        auto weekday=calendar::SystemTime(calendar::Days(std::chrono::year(2026)/10/25)+
            std::chrono::days((firstWeek+i)%7));wchar_t day[16]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&weekday,L"ddd",day,16,nullptr);
        Text(day,{center.left+12+i*pitch,center.top+40,center.left+12+(i+1)*pitch,center.top+60},
            type::Caption,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    int days=static_cast<int>((next-first).count());
    for(int day=1;day<=days;++day){
        int cell=start+day-1;float x=center.left+12+(cell%7)*pitch,y=center.top+62+(cell/7)*24;
        auto dateValue=first+std::chrono::days(day-1);
        bool current=month.year()==std::chrono::year(now.wYear)&&month.month()==std::chrono::month(now.wMonth)&&
            day==now.wDay;
        bool event=std::any_of(calendarEvents.begin(),calendarEvents.end(),[&](const calendar::Event& item){
            return std::chrono::floor<std::chrono::days>(item.start)==dateValue;});
        if(current)Fill({x+1,y,x+pitch-1,y+22},theme::AccentSoft,8);
        Text(Number(day),{x,y,x+pitch,y+22},type::Caption,current?theme::SelectedText:theme::Text,
            DWRITE_TEXT_ALIGNMENT_CENTER);
        if(event)Line(x+pitch/2-2,y+21,x+pitch/2+2,y+21,theme::Focus,2);
        AddHit(2100+day,{x,y,x+pitch,y+24},Number(day)+L" "+label+(event?L", events":L""),false);
    }
    std::wstring upcoming;int events=0;auto localNow=(renderMode||fixtureGeometry)?
        calendar::FromSystemTime(now):calendar::LocalNow();
    for(const auto& item:calendarEvents){
        if(item.start<(item.allDay?std::chrono::floor<std::chrono::days>(localNow):localNow)||events==3)continue;
        auto eventTime=calendar::SystemTime(item.start);wchar_t when[48]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&eventTime,L"MMM d",when,48,nullptr);
        wchar_t eventClock[48]{};
        if(!item.allDay)GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&eventTime,nullptr,eventClock,48);
        auto description=std::wstring(when)+(item.allDay?L"":L" "+std::wstring(eventClock))+L" · "+item.title;
        float y=center.top+216+events*26;
        Text(description,{center.left+12,y,center.right-12,y+26},type::Caption);
        if(!upcoming.empty())upcoming+=L". ";upcoming+=description;++events;
    }
    if(events==0)Paragraph(calendarMessage.empty()?L"No upcoming events. Add one or connect a calendar.":calendarMessage,
        {center.left+12,center.top+216,center.right-12,center.top+294});
    AddHit(2005,{center.left+12,center.top+216,center.right-12,center.top+294},
        (upcoming.empty()?L"No upcoming events":upcoming)+L". "+calendarMessage,false);
    TextButton(CalendarAdd,{center.left+12,center.top+310,center.right-56,center.top+344},L"Add event");
    SmallButton(TodayOptions,{center.right-48,center.top+310,center.right-12,center.top+344},L"\xE713",
        L"Today settings and calendar subscription");

    Text(L"Tasks · "+std::to_wstring(tasks.size())+L" / 20",{task.left+12,task.top+8,task.right-12,task.top+36},
        type::Control);
    auto list=TaskListRect();float maxScroll=std::max(0.0f,static_cast<float>(tasks.size())*40-(list.bottom-list.top));
    taskScroll=std::clamp(taskScroll,0.0f,maxScroll);
    if(!layoutOnly)target->PushAxisAlignedClip(list,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for(size_t i=0;i<tasks.size()&&i<TaskStore::Capacity;++i){
        int id=200+static_cast<int>(i);float y=list.top+static_cast<float>(i)*40-taskScroll;
        D2D1_RECT_F row{list.left,y,list.right-30,y+36};
        AddHit(id,row,tasks[i].title);
        auto clip=R(layout::Intersection({hits.back().clip.left,hits.back().clip.top,
            hits.back().clip.right,hits.back().clip.bottom},{list.left,list.top-scroll,list.right,list.bottom-scroll}));
        hits.back().clip=clip;
        Fill(Tactile(id,row),tasks[i].done?theme::AccentSoft:theme::Inset,10);
        Text(tasks[i].done?L"\xE73E":L"\xEA3A",{row.left+4,y,row.left+28,y+36},type::Icon,
            tasks[i].done?theme::SelectedText:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(tasks[i].title,{row.left+32,y,row.right-6,y+36},type::Body,
            tasks[i].done?theme::Muted:theme::Text);FocusRing(id,row);
        SmallButton(220+static_cast<int>(i),{list.right-28,y+2,list.right,y+34},L"\xE711",L"Delete "+tasks[i].title);
        hits.back().clip=clip;
    }
    if(tasks.empty())Paragraph(L"Add up to 20 tasks. Your draft stays here until saved.",
        {list.left+4,list.top+20,list.right-4,list.top+110});
    if(!layoutOnly)target->PopAxisAlignedClip();
    if(maxScroll>0){float track=list.bottom-list.top,thumb=std::max(16.0f,track*track/(track+maxScroll));
        float y=list.top+(track-thumb)*taskScroll/maxScroll;
        Line(task.right-4,y,task.right-4,y+thumb,theme::Muted,2);}
    float focus=task.top+248;
    TextButton(FocusStart,{task.left+12,focus,task.right-50,focus+32},
        (focusRunning?L"Pause · ":L"Focus · ")+Clock(focusSeconds),true);
    SmallButton(FocusReset,{task.right-44,focus,task.right-12,focus+32},L"\xE777",L"Reset focus");
    AddHit(2003,{task.left+12,focus+34,task.right-12,focus+54},L"Focus timer. "+Clock(focusSeconds)+
        (focusRunning?L", running":L", paused"),false);
    Text(Number(focusDuration/60)+L" MIN FOCUS",{task.left+12,focus+34,task.right-12,focus+54},type::Mono,theme::Muted);
    auto input=TaskInputRect();Fill({input.left-4,input.top-4,task.right-8,input.bottom+4},theme::CardHover,10);
    AddHit(400,input,L"New task, up to 120 characters",tasks.size()<TaskStore::Capacity);
    if(renderMode)Text(tasks.size()>=TaskStore::Capacity?L"20 tasks · list full":L"Add a task…",input,
        type::Secondary,theme::Muted);
    SmallButton(AddTask,{task.right-44,input.top,task.right-12,input.bottom},L"\xE72A",L"Add task",
        tasks.size()<TaskStore::Capacity);EnsureTaskEdit();
}
