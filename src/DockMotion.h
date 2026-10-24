#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <string_view>
#include <cwctype>

namespace dock {
struct Point { float x,y; };
// Flared shoulders attach to the top edge; the lower center is narrower.
inline std::array<Point,35> Notch(float left,float top,float width,float height){
    std::array<Point,35> points{};
    for(int i=0;i<=16;++i){float t=i/16.0f,u=1-t;
        points[i]={left+width*(3*u*u*t*0.11f+3*u*t*t*0.11f+t*t*t*0.22f),top+height*(u*u*u+3*u*u*t)};
        points[17+i]={left+width*(0.78f*u*u*u+3*u*u*t*0.89f+3*u*t*t*0.89f+t*t*t),top+height*(3*u*t*t+t*t*t)};
    }
    points[34]=points[0];for(auto& point:points)point.y=top+height-(point.y-top);return points;
}
inline bool BrowserExecutable(std::wstring_view path){
    path.remove_prefix(path.find_last_of(L"\\/")==std::wstring_view::npos?0:path.find_last_of(L"\\/")+1);
    constexpr std::wstring_view browsers[]{L"chrome.exe",L"msedge.exe",L"brave.exe",L"firefox.exe",L"vivaldi.exe",L"opera.exe",L"arc.exe",L"zen.exe",L"waterfox.exe",L"librewolf.exe",L"floorp.exe",L"chromium.exe",L"thorium.exe",L"browser.exe"};
    for(auto browser:browsers){if(path.size()!=browser.size())continue;bool match=true;for(size_t i=0;i<path.size();++i)if(std::towlower(path[i])!=browser[i]){match=false;break;}if(match)return true;}return false;
}
inline float PanelTop(bool browser){return browser?112.0f:0.0f;}
inline std::array<Point,69> Attached(float left,float top,float width,float height){
    std::array<Point,69> out{};float r=std::min(22.0f,height/2),shoulder=24;
    auto curve=[&](int offset,Point a,Point b,Point c,Point d){for(int i=0;i<=16;++i){float t=i/16.0f,u=1-t;out[offset+i]={u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y};}};
    curve(0,{left-shoulder,top},{left-3,top},{left,top+3},{left,top+r});
    curve(17,{left,top+height-r},{left,top+height},{left,top+height},{left+r,top+height});
    curve(34,{left+width-r,top+height},{left+width,top+height},{left+width,top+height},{left+width,top+height-r});
    curve(51,{left+width,top+r},{left+width,top+3},{left+width+3,top},{left+width+shoulder,top});out[68]=out[0];return out;
}
// Exact critically damped spring integration: reversal preserves velocity and never snaps.
struct Spring {
    float value=0,velocity=0,goal=0;
    explicit Spring(float initial=0):value(initial),goal(initial){}
    void Snap(float next){value=goal=next;velocity=0;}
    bool Moving()const{return std::abs(goal-value)>0.025f||std::abs(velocity)>0.025f;}
    void Step(float seconds,float frequency=23.0f){
        if(!Moving())return;
        seconds=std::clamp(seconds,0.0f,0.05f);
        float displacement=value-goal,c=velocity+frequency*displacement,e=std::exp(-frequency*seconds);
        value=goal+(displacement+c*seconds)*e;velocity=(velocity-frequency*c*seconds)*e;
        if(!Moving())Snap(goal);
    }
};
struct Proximity {
    float dwell=0,outside=0,cooldown=0;
    void Dismiss(){dwell=outside=0;cooldown=0.8f;}
    int Step(float seconds,bool open,bool approaching,bool inside,bool busy){
        seconds=std::clamp(seconds,0.0f,0.2f);cooldown=std::max(0.0f,cooldown-seconds);
        if(!open){outside=0;dwell=approaching&&cooldown==0?dwell+seconds:0;return dwell>=0.16f?1:0;}
        dwell=0;outside=inside||busy?0:outside+seconds;return outside>=0.55f?-1:0;
    }
};
inline unsigned Mix(unsigned from,unsigned to,float amount){
    amount=std::clamp(amount,0.0f,1.0f);unsigned result=0;
    for(int shift:{0,8,16}){float a=static_cast<float>((from>>shift)&255),b=static_cast<float>((to>>shift)&255);
        result|=static_cast<unsigned>(std::lround(a+(b-a)*amount))<<shift;}
    return result;
}
}

namespace theme {
constexpr unsigned Surface=0x0D0D0F,Card=0x1B1B1E,CardHover=0x27272B,Inset=0x141416;
constexpr unsigned Text=0xF2F0EB,Muted=0x96949E;
inline unsigned Gold=0xCBB06B,GoldHover=0xD8BE7D,GoldInk=0x201C17,AccentSoft=0x2B261B,Secondary=0xBC93D8,Tertiary=0xA6CA7D;
constexpr float Radius=16,SmallRadius=10,InsetSize=18,Gap=10;
constexpr float DockWidth=216,DockHeight=18,TopRail=0,EnvelopeWidth=900,EnvelopeHeight=760;
}
