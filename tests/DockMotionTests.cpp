#include "../src/DockMotion.h"
#include "../src/ThemePalette.h"
#include <cstdio>
#include <cstdlib>

void Require(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    Require(dock::BrowserExecutable(L"C:\\Program Files\\BraveSoftware\\Brave.exe")&&dock::BrowserExecutable(L"firefox.exe"),"browser names and paths get browser clearance");
    Require(!dock::BrowserExecutable(L"Code.exe")&&!dock::BrowserExecutable(L"discord.exe"),"Chromium-based desktop apps keep normal popup positioning");
    Require(dock::PanelTop(true)==112&&dock::PanelTop(false)==0,"only browsers detach; normal panels attach at the screen edge");
    unsigned char blue[64],red[64];for(int i=0;i<64;i+=4){blue[i]=210;blue[i+1]=110;blue[i+2]=40;blue[i+3]=255;red[i]=40;red[i+1]=60;red[i+2]=210;red[i+3]=255;}
    unsigned blueAccent=palette::Accent(blue,64),redAccent=palette::Accent(red,64);Require(blueAccent!=redAccent&&palette::ToHsl(blueAccent).h>0.5f&&palette::ToHsl(redAccent).h<0.1f,"wallpaper hue drives the accent");
    Require(palette::RevealRadius(0,1920,1080)==0&&palette::RevealRadius(0.5f,1920,1080)<palette::RevealRadius(1,1920,1080)&&palette::RevealRadius(1,1920,1080)>1101,"circular reveal grows from center through all corners");
    const auto notch=dock::Notch(0,0,216,18);Require(notch[0].y==0&&notch[16].y==18&&notch[17].y==18&&notch[33].y==0,"notch attaches to the top with downward shoulders");
    dock::Spring reveal(0);reveal.goal=1;float previous=0;
    for(int frame=0;frame<120;++frame){reveal.Step(1.0f/120);Require(reveal.value>=previous-0.00001f&&reveal.value<=1.00001f,"reveal must not overshoot");previous=reveal.value;}
    Require(reveal.value==1&&!reveal.Moving(),"finite reveal settles and stops its timer");
    dock::Spring reversal(0);reversal.goal=600;
    for(int frame=0;frame<10;++frame)reversal.Step(1.0f/60);
    float position=reversal.value,velocity=reversal.velocity;reversal.goal=148;
    Require(reversal.value==position&&reversal.velocity==velocity,"reversal preserves position and velocity");
    for(int frame=0;frame<120;++frame)reversal.Step(1.0f/60);
    Require(reversal.value==148&&!reversal.Moving(),"interrupted animation settles at new destination");
    dock::Proximity input;
    Require(input.Step(0.1f,false,true,false,false)==0,"passing through the edge does not open immediately");
    Require(input.Step(0.1f,false,false,false,false)==0,"leaving resets the hover dwell");
    Require(input.Step(0.1f,false,true,false,false)==0&&input.Step(0.1f,false,true,false,false)==1,"deliberate dwell opens the dock");
    for(int i=0;i<20;++i)Require(input.Step(0.1f,true,false,true,false)==0,"travel corridor keeps dock open");
    for(int i=0;i<20;++i)Require(input.Step(0.1f,true,false,false,true)==0,"editing and dialogs prevent auto-dismiss");
    int action=0;for(int i=0;i<6;++i)action=input.Step(0.1f,true,false,false,false);
    Require(action==-1,"pointer departure dismisses after the grace period");
    input.Dismiss();for(int i=0;i<7;++i)Require(input.Step(0.1f,false,true,false,false)==0,"Escape cooldown prevents immediate reopening");
    std::puts("PASS: spring settling/reversal, hover intent, travel corridor, modal protection, dismissal and Escape cooldown.");
}
