// Cover aiming by controller (November 16). The DLL builds the cover aim
// frame exactly as DirectionWorld does for a controller:
//   HeadWorld(base, reference, rightController.aim, scale)
// and the crosshair is exported with ThrowAim::WorldToTracking against the
// rendered head. This checks that a point anywhere along that frame's
// forward row lands, in tracking space, on the controller's own pointing
// line (OpenXR aim pose, forward -Z), for varied bases, recentre references,
// head poses, controller poses, distances and world scales.
#include "CameraMath.h"
#include "ThrowAim.h"
#include "ProjectileAim.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace CameraMath;
static Transport::Quaternion Q(float ax,float ay,float az,float deg) {
    const float n=std::sqrt(ax*ax+ay*ay+az*az),h=deg*3.14159265f/360.f,s=std::sin(h)/n;
    return {ax*s,ay*s,az*s,std::cos(h)};
}
int main() {
    unsigned cases=0;
    const Matrix flat{{1,0,0,0, 0,0,-1,0, 0,1,0,0, -63414.f,5346.f,-70070.f,1}};
    for(float baseYaw:{0.f,73.f,-150.f})
    for(float refYaw:{0.f,40.f})
    for(float headYaw:{0.f,-35.f})for(float headPitch:{0.f,25.f})
    for(float ctrlYaw:{0.f,60.f,-120.f})for(float ctrlPitch:{-30.f,0.f,45.f})
    for(float scale:{100.f,300.f})for(float distance:{60.f,1500.f,40000.f}) {
        auto base=Multiply(Rotation(Q(0,1,0,baseYaw)),flat); // turn about the camera's down axis (world vertical)
        Transport::Pose reference{Q(0,1,0,refYaw),{.2f,1.5f,-.3f}};
        Transport::Pose head{Multiply(Q(0,1,0,headYaw),Q(1,0,0,headPitch)),{.25f,1.62f,-.1f}};
        Transport::Pose aim{Multiply(Q(0,1,0,ctrlYaw),Q(1,0,0,ctrlPitch)),{.45f,1.1f,-.5f}};
        // DLL side.
        auto frame=HeadWorld(base,reference,aim,scale);
        const float* f=frame.m+8;
        assert(std::abs(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]-1)<.001f); // passes CoverPointingFrame and Endpoint
        float point[3];for(int i=0;i<3;i++)point[i]=frame.m[12+i]+f[i]*distance;
        // Companion side: rendered head world, as ExportCoverTarget uses it.
        auto world=HeadWorld(base,reference,head,scale);
        auto x=ThrowAim::WorldToTracking(point,world.m,{head.position.x,head.position.y,head.position.z},
            {head.orientation.x,head.orientation.y,head.orientation.z,head.orientation.w},scale);
        auto fwd=Rotate(aim.orientation,{0,0,-1});
        const float e[3]{aim.position.x+fwd.x*distance/scale,aim.position.y+fwd.y*distance/scale,aim.position.z+fwd.z*distance/scale};
        const float err=std::sqrt((x.x-e[0])*(x.x-e[0])+(x.y-e[1])*(x.y-e[1])+(x.z-e[2])*(x.z-e[2]));
        const float tolerance=.0005f+distance/scale*2e-5f; // float precision at large world coordinates
        if(!(err<tolerance)){std::printf("FAIL err=%g tol=%g base=%g ref=%g head=%g/%g ctrl=%g/%g scale=%g d=%g\n",err,tolerance,baseYaw,refYaw,headYaw,headPitch,ctrlYaw,ctrlPitch,scale,distance);return 1;}
        ++cases;
    }
    assert(!std::strcmp(ProjectileAim::ReasonName(ProjectileAim::CoverPointing),"cover-pointing"));
    assert(!std::strcmp(ProjectileAim::ReasonName(ProjectileAim::BarrelMismatch),"barrel-mismatch"));
    std::printf("PASS cover pointing: %u cases; a target along the controller aim frame appears on the controller's own pointing line in the headset (bases, recentre, head, controller, scale, 0.6-400 m); rocket log reason named\n",cases);
}
