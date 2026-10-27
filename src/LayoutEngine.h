#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace layout {
struct Rect {
    float left=0,top=0,right=0,bottom=0;
    float Width()const{return std::max(0.0f,right-left);}
    float Height()const{return std::max(0.0f,bottom-top);}
    float CenterX()const{return (left+right)/2;}
    bool Contains(float x,float y)const{return x>=left&&x<=right&&y>=top&&y<=bottom;}
};
inline bool Intersects(Rect a,Rect b){return a.left<b.right&&b.left<a.right&&a.top<b.bottom&&b.top<a.bottom;}
inline Rect Intersection(Rect a,Rect b){return {std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};}
constexpr float Padding=20,Gap=12,Section=16,PanelRadius=20,CardRadius=14,ControlRadius=10;
enum class View { Controls,Today,Stats,Clipboard,Apps,Wallpaper,Power,Wireless,Media };
struct Input {float availableWidth=900,availableHeight=748;View view=View::Controls;size_t items=0;};
struct Result {
    Rect panel,header,body,status,footer,stage,previous,next;
    float padding=Padding,contentHeight=0,headerHeight=64;
    bool narrow=false,emergency=false,editorial=false;
};
inline Result Solve(Input in){
    // Main views share a width so navigation never slides away from the pointer.
    Result r;float preferred=in.view==View::Wireless?460.0f:in.view==View::Media?344.0f:800.0f;
    float w=std::min(preferred,std::max(100.0f,in.availableWidth));r.narrow=w<640;r.emergency=in.availableHeight<240||w<240;bool compact=in.availableHeight<360;r.padding=compact?12.0f:Padding;float gap=compact?4.0f:Section,status=compact?32.0f:28.0f;
    r.headerHeight=64;
    r.editorial=false;
    float content=in.view==View::Today?(w>=720?360.0f:1104.0f):
        in.view==View::Stats?(w>=720?360.0f:580.0f):
        in.view==View::Controls?(r.narrow?476.0f:360.0f):360.0f;
    float body=in.view==View::Wireless?372.0f:in.view==View::Media?116.0f:360.0f;
    float foot=compact?32.0f:44.0f;
    float h=std::min(r.headerHeight+gap+body+gap+foot+(foot?gap:0.0f)+status+r.padding,std::max(80.0f,in.availableHeight));
    if(in.view==View::Media)h=std::min(164.0f,in.availableHeight);
    if(r.emergency&&in.view!=View::Media){
        r.headerHeight=32;r.padding=12;gap=4;status=0;foot=0;h=std::min(h,std::max(80.0f,in.availableHeight));
    }
    r.panel={0,0,w,h};r.header={r.padding,8,w-r.padding,r.headerHeight};
    r.status={r.padding,h-r.padding-status,w-r.padding,h-r.padding};
    r.footer={r.padding,r.status.top-gap-foot,w-r.padding,r.status.top-gap};
    r.body={r.padding,r.headerHeight+gap,w-r.padding,(foot?r.footer.top:r.status.top)-gap};
    if(in.view==View::Media)r.body={Padding,12,w-Padding,h-Padding};
    r.contentHeight=in.view==View::Apps?(r.emergency?100.0f:r.body.Height()):in.view==View::Clipboard?r.body.Height():r.emergency&&in.view==View::Wallpaper?110.0f:std::max(content,body);
    r.stage={r.body.left+56,r.body.top+42,r.body.right-56,r.emergency?std::max(r.body.bottom,r.body.top+110):r.body.bottom};
    float cy=(r.stage.top+r.stage.bottom)/2;
    r.previous={r.body.left,cy-20,r.body.left+40,cy+20};r.next={r.body.right-40,cy-20,r.body.right,cy+20};
    return r;
}
inline Rect Column(Rect area,int index,int count,float gap=Gap){float w=(area.Width()-gap*(count-1))/count;return {area.left+index*(w+gap),area.top,area.left+index*(w+gap)+w,area.bottom};}
inline bool RoundedContains(Rect r,float radius,float x,float y){
    if(!r.Contains(x,y))return false;
    float cx=std::clamp(x,r.left+radius,r.right-radius),cy=std::clamp(y,r.top+radius,r.bottom-radius);
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
}
inline bool SkewContains(Rect r,float skew,float radius,float x,float y){return RoundedContains(r,radius,x-skew*(y-(r.top+r.bottom)/2),y);}
struct AppPage {size_t begin=0,end=0;};
inline std::vector<AppPage> Paginate(const std::vector<int>& groups,float height,float rowHeight=44,float headingHeight=24){
    std::vector<AppPage> pages;size_t first=0;float used=0;int group=-1;
    for(size_t i=0;i<groups.size();++i){float cost=rowHeight+(group==groups[i]?0.0f:headingHeight);
        if(i>first&&used+cost>height){pages.push_back({first,i});first=i;used=0;group=-1;cost=rowHeight+headingHeight;}
        used+=cost;group=groups[i];
    }
    if(!groups.empty())pages.push_back({first,groups.size()});return pages;
}
struct RailItem {Rect base,presented;float scale=1;};
inline std::vector<RailItem> Rail(Rect area,size_t count,float pointer,float strength){
    std::vector<RailItem> out;if(!count)return out;
    float size=std::min(44.0f,(area.Width()-24-static_cast<float>(count-1)*12)/(static_cast<float>(count)*1.4f));
    size=std::max(16.0f,size);float pitch=size+12,total=0;
    for(size_t i=0;i<count;++i){float x=area.CenterX()+(static_cast<float>(i)-(static_cast<float>(count)-1)/2)*pitch;float d=(pointer-x)/(pitch*1.5f),s=1+strength*0.35f*std::exp(-d*d);out.push_back({{x-size/2,area.top,x+size/2,area.top+size},{},s});total+=size*s+12;}
    float x=area.CenterX()-(total-12)/2;
    for(auto& item:out){float w=size*item.scale;item.presented={x,area.top,x+w,area.top+w};x+=w+12;}return out;
}
}
