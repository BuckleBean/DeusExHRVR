#pragma once
#include "CameraMath.h"
#include <algorithm>
#include <cmath>
namespace ImmersiveScope {
// Where the gun controller's aim pose is relative to the head, in head space (metres;
// x right, y up, z back, so in front is negative z), and which of the gesture's limits
// it misses. Also what ScopeGestureLog writes.
struct Measure {
    CameraMath::Vector d{},f{};float distance{};
    // Missed limits: 1 too far from the head, 2 not in front of the face, 4 too far to
    // the side, 8 too high or low, 16 not pointing forward enough, 32 not a number.
    unsigned missed{};
    bool Aligned() const {return missed==0;}
};
inline Measure Measured(const Transport::Tracking& t,bool engaged) {
    Measure m;
    m.d=CameraMath::Rotate(CameraMath::Inverse(t.head.orientation),{
        t.rightController.aim.position.x-t.head.position.x,
        t.rightController.aim.position.y-t.head.position.y,
        t.rightController.aim.position.z-t.head.position.z});
    m.f=CameraMath::Rotate(CameraMath::Multiply(CameraMath::Inverse(t.head.orientation),t.rightController.aim.orientation),{0,0,-1});
    const auto& d=m.d;
    if(!(std::isfinite(d.x)&&std::isfinite(d.y)&&std::isfinite(d.z)&&std::isfinite(m.f.z))){m.missed=32;return m;}
    m.distance=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    const float radius=engaged?.42f:.28f;
    if(!(d.x*d.x+d.y*d.y+d.z*d.z<radius*radius))m.missed|=1;
    if(!(d.z<.08f))m.missed|=2;
    if(!(std::abs(d.x)<.22f))m.missed|=4;
    if(!(std::abs(d.y)<.25f))m.missed|=8;
    if(!(m.f.z<-(engaged?.35f:.65f)))m.missed|=16;
    return m;
}
class Gesture {
    bool engaged{};uint64_t since{};
public:
    void Reset(){engaged=false;since=0;}
    bool Engaged() const {return engaged;}
    bool Update(const Transport::Tracking& t,bool eligible,uint64_t now) {
        if(!eligible || !t.valid || !t.rightController.valid || now<t.tick || now-t.tick>=250){Reset();return false;}
        if(!Measured(t,engaged).Aligned()){Reset();return false;}
        if(!since)since=now;
        if(now>=since+150)engaged=true;
        return engaged;
    }
};
inline Transport::Tracking Zoom(const Transport::Tracking& input,float magnification) {
    auto t=input;const float zoom=std::clamp(magnification,1.f,12.f);
    // A rifle scope has one optical origin. Keeping physical eye offsets here
    // introduces magnified parallax between its reticle and the firing ray.
    // Retain each eye orientation/frustum, but project from the same origin.
    for(auto& e:t.eyes){e.pose.position=t.head.position;e.left=std::atan(std::tan(e.left)/zoom);e.right=std::atan(std::tan(e.right)/zoom);e.up=std::atan(std::tan(e.up)/zoom);e.down=std::atan(std::tan(e.down)/zoom);}
    return t;
}
}
