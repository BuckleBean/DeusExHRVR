#include "CameraMath.h"
#include <cstdio>
#include <cstdlib>
void Check(bool value,const char* name){if(!value){fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
int main() {
    using namespace CameraMath;
    Matrix game=Rotation({0,0,0,1});game.m[12]=10;game.m[13]=20;game.m[14]=30;
    Pose reference{{0,0,0,1},{0,0,0}},head=reference;
    auto neutral=HeadWorld(game,reference,head,100);
    for(int i=0;i<16;i++)Check(Near(neutral.m[i],game.m[i]),"neutral pose preserves game camera");
    head.position={0.1f,0.2f,-0.3f};
    auto moved=HeadWorld(game,reference,head,100);
    Check(Near(moved.m[12],20)&&Near(moved.m[13],0)&&Near(moved.m[14],60),"right/up/forward conversion");
    head=reference;head.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};
    auto turned=HeadWorld(game,reference,head,100);
    Check(Near(turned.m[8],-1)&&Near(turned.m[10],0),"head yaw turns forward toward left");
    auto identity=Multiply(turned,InverseRigid(turned));
    for(int i=0;i<16;i++)Check(Near(identity.m[i],i%5==0?1.f:0.f),"rigid inverse");
    Transport::Tracking t{};t.head=reference;t.valid=1;
    for(int i=0;i<2;i++) {t.eyes[i].pose=reference;t.eyes[i].pose.position.x=i?.032f:-.032f;
        t.eyes[i].left=-.8f;t.eyes[i].right=.9f;t.eyes[i].up=.85f;t.eyes[i].down=-.75f;}
    Matrix p;p.m[10]=1.0001f;p.m[14]=-1.0001f;
    for(unsigned eye=0;eye<2;eye++) {
        auto ep=EyeProjection(p,t,eye,100);
        // An eye-local frustum edge must project to exactly the clip-space edge.
        for(float side:{-1.f,1.f}) {
            float angle=side<0?t.eyes[eye].left:t.eyes[eye].right;
            float x=t.eyes[eye].pose.position.x*100+std::tan(angle)*1000;
            float clipX=x*ep.m[0]+1000*ep.m[8]+ep.m[12];
            Check(Near(clipX/1000,side),"asymmetric eye frustum and IPD");
        }
    }
    t.eyes[0].left=-.9f;t.eyes[0].right=.7f;
    t.eyes[1].left=-.7f;t.eyes[1].right=.9f;
    for(unsigned eye=0;eye<2;eye++) {
        auto hud=HudClipTransform(t,eye);
        for(float x:{-.9f,0.f,.9f}) {
            float clipX=x*hud.m[0]+hud.m[12],clipW=x*hud.m[3]+hud.m[15];
            const auto& e=t.eyes[eye];float l=std::tan(e.left),r=std::tan(e.right);
            float ray=(clipX/clipW*(r-l)+(r+l))*.5f;
            float commonX=e.pose.position.x+2.f*ray;
            Check(Near(commonX,x*1.6f),"both HUD eye rays meet the same plane point");
        }
    }
    puts("PASS camera math, asymmetric stereo frustum and binocular HUD alignment");
}
