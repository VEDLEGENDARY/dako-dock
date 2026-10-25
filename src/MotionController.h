#pragma once
#include "DockMotion.h"

namespace dock {
// Ownership only: the HWND adapter arms/kills once when this state changes.
struct Scheduler {
    bool active=false;
    bool Start(){if(active)return false;active=true;return true;}
    bool Stop(){bool was=active;active=false;return was;}
};
// Exact underdamped feedback channel. Geometry channels retain critical damping.
struct Feedback {
    float value=0,velocity=0,goal=0;
    void Snap(float next){value=goal=next;velocity=0;}
    bool Moving()const{return std::abs(value-goal)>0.001f||std::abs(velocity)>0.01f;}
    void Step(float dt){
        if(!Moving())return;
        if(dt>0.5f){Snap(goal);return;}
        float w=26,z=0.65f,d=w*std::sqrt(1-z*z),a=value-goal,b=(velocity+z*w*a)/d,e=std::exp(-z*w*dt);
        value=goal+e*(a*std::cos(d*dt)+b*std::sin(d*dt));
        velocity=e*((b*d-z*w*a)*std::cos(d*dt)-(a*d+z*w*b)*std::sin(d*dt));
        if(!Moving())Snap(goal);
    }
};
}
