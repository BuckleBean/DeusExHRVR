#include "CoverCrosshair.h"
#include "ThrowAim.h"
#include "CameraMath.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace CameraMath;
int main() {
    constexpr float pi=3.14159265359f,scale=300.f;
    Matrix flat{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1}};
    Pose reference{};reference.orientation.w=1;
    for(float distance:{90.f,600.f,30000.f})
    for(float pitch:{0.f,20.f})for(float yaw:{0.f,30.f}) {
        const float p=pitch*pi/180,y=yaw*pi/180;
        auto native=Multiply(Rotation({std::sin(p/2),0,0,std::cos(p/2)}),flat);
        float point[3];assert(CoverCrosshair::Endpoint(native.m,distance,point));
        Transport::Tracking t{};t.head.orientation={0,std::sin(y/2),0,std::cos(y/2)};
        t.head.position={.03f,.01f,0};
        auto world=HeadWorld(HorizontalDirection(native,native),reference,t.head,scale);
        auto view=InverseRigid(world);
        auto x=ThrowAim::WorldToTracking(point,world.m,{t.head.position.x,t.head.position.y,t.head.position.z},
            {t.head.orientation.x,t.head.orientation.y,t.head.orientation.z,t.head.orientation.w},scale);
        float camera[4]{0,0,0,1};
        for(int j=0;j<3;j++)camera[j]=point[0]*view.m[j]+point[1]*view.m[4+j]+point[2]*view.m[8+j]+view.m[12+j];
        camera[1]=-camera[1];
        for(unsigned eye=0;eye<2;eye++) {
            auto& e=t.eyes[eye];e.pose.orientation=t.head.orientation;
            auto offset=Rotate(t.head.orientation,{eye?.032f:-.032f,0,0});
            e.pose.position={t.head.position.x+offset.x,t.head.position.y+offset.y,t.head.position.z+offset.z};
            e.left=e.down=-pi/4;e.right=e.up=pi/4;
            Matrix depth{};depth.m[10]=1;depth.m[14]=-.1f;
            auto projection=EyeProjection(depth,t,eye,scale);
            float clip[4]{};
            for(int j=0;j<4;j++)for(int k=0;k<4;k++)clip[j]+=camera[k]*projection.m[4*k+j];
            auto q=e.pose.orientation;
            auto local=Rotate({-q.x,-q.y,-q.z,q.w},{x.x-e.pose.position.x,x.y-e.pose.position.y,x.z-e.pose.position.z});
            assert(std::abs(clip[0]/clip[3]-local.x/-local.z)<.0001f);
            assert(std::abs(clip[1]/clip[3]-local.y/-local.z)<.0001f);
        }
    }
    float end[3];assert(!CoverCrosshair::Endpoint(flat.m,0,end));
    assert(!CoverCrosshair::Endpoint(flat.m,std::numeric_limits<float>::infinity(),end));
    flat.m[8]=2;assert(!CoverCrosshair::Endpoint(flat.m,100,end));
    auto pixels=CoverCrosshair::Pixels();assert(pixels[64*128+64]==0&&pixels[0]==0);
    assert(pixels[64*128+85]==0xffffffffu&&pixels[67*128+85]==0xff000000u);
    assert(std::abs(CoverCrosshair::Width(1)*10-CoverCrosshair::Width(10))<1e-6f);
    puts("PASS cover crosshair: native target and OpenXR target agree in both eyes at near/mid/far depths, head yaw/translation and native pitch; invalid rays rejected; texture and angular size checked");
}
