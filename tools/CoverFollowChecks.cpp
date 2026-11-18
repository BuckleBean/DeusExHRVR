#include "CoverFollow.h"
#include "NativeHeading.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace CoverFollow;
int main() {
    const float d=Pi/180;
    // Yaw convention matches NativeHeading::Base (the mod's first-person heading).
    for(float y:{-179.f,-90.f,-30.f,0.f,10.f,90.f,179.f}) {
        CameraMath::Matrix pos{};auto b=NativeHeading::Base(y*d,pos);
        float yaw;assert(YawFromForward(b.m+8,yaw));assert(std::abs(Wrap(yaw-y*d))<1e-5f);
    }
    const float up[3]{0.05f,0.05f,1};float yaw=7;assert(!YawFromForward(up,yaw)&&yaw==7);
    const float leash=15*d,step=3*d;
    // Inside the leash: no movement.
    assert(Follow(0,10*d,leash,step)==0);assert(Follow(0,-14*d,leash,step)==0);
    // Beyond it: pulled toward the aim, limited per update, settles on the leash edge.
    float c=0;
    for(int i=0;i<100;i++)c=Follow(c,60*d,leash,step);
    assert(std::abs(c-45*d)<1e-4f);
    c=0;c=Follow(c,60*d,leash,step);assert(std::abs(c-3*d)<1e-5f);
    // The short way round across +/-180, both directions.
    c=170*d;for(int i=0;i<100;i++)c=Follow(c,-150*d,leash,step);
    assert(std::abs(Wrap(c-(-165*d)))<1e-4f);
    c=-170*d;for(int i=0;i<100;i++)c=Follow(c,150*d,leash,step);
    assert(std::abs(Wrap(c-165*d))<1e-4f);
    // Stable: once settled, a steady aim produces no further motion.
    float settled=Follow(c,150*d,leash,step);assert(settled==c||std::abs(Wrap(settled-c))<1e-6f);
    // Invalid inputs hold the camera.
    assert(Follow(1,NAN,leash,step)==1);assert(Follow(1,2,leash,0)==1);
    // Pitch: measured from the Z-up forward, positive up.
    const float lvl[3]{0,1,0},dn[3]{0,1,-1};float pitch;
    assert(PitchFromForward(lvl,pitch)&&std::abs(pitch)<1e-6f);
    assert(PitchFromForward(dn,pitch)&&std::abs(pitch+45*d)<1e-5f);
    assert(PitchNeed(0,10*d,12*d,2*d)==0);
    assert(std::abs(PitchNeed(0,-40*d,12*d,2*d)+2*d)<1e-6f);
    assert(std::abs(PitchNeed(-27*d,-40*d,12*d,2*d)+1*d)<1e-5f);
    std::puts("PASS cover pitch: measured pitch positive up; leash and cap");
    std::puts("PASS cover follow: yaw convention equals the first-person heading; still inside 15 degrees, pulled along beyond, capped per update, settles at the leash edge, short way across 180, near-vertical aim holds");
}
