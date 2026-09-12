#include "CameraMath.h"
#include "SceneCache.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
void Check(bool value,const char* name){if(!value){fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
int main(int argc,char** argv) {
    {
        SceneCache<uint64_t> scenes;
        for(uintptr_t i=1;i<=5000;i++)scenes.Store(reinterpret_cast<void*>(i),42,10);
        for(uintptr_t i=1;i<=5000;i++)Check(scenes.Find(reinterpret_cast<void*>(i))==42,"large native pair retains every camera tag");
        scenes.Store(reinterpret_cast<void*>(1),43,11);
        scenes.Complete(11);
        Check(scenes.Size()==5000,"recent completed scenes retained");
        scenes.Complete(12);
        Check(scenes.Size()==1 && scenes.Find(reinterpret_cast<void*>(1))==43,"old scenes expire without erasing reused address");
        scenes.Store(reinterpret_cast<void*>(2),44,14);
        scenes.Complete(13);
        Check(scenes.Size()==1 && scenes.Find(reinterpret_cast<void*>(2))==44,"future queued scene survives cleanup");
        scenes.Erase(reinterpret_cast<void*>(2));
        Check(!scenes.Find(reinterpret_cast<void*>(2)),"untracked reuse removes old tag");
    }
    if(argc==2) {
        // Replay a private camera-history CSV through the production cache.
        // The bounded recording can begin in the middle of a frame.
        std::ifstream input(argv[1]);Check(bool(input),"open camera history");
        std::string line;std::getline(input,line);
        SceneCache<uint64_t> scenes;uint64_t first=0,previous=0,draws=0,recovered=0;
        while(std::getline(input,line)) {
            std::istringstream row(line);std::string columns[7];
            for(auto& c:columns)Check(bool(std::getline(row,c,',')),"parse camera history");
            uint64_t frame=std::stoull(columns[0]),pose=std::stoull(columns[2]);
            auto scene=reinterpret_cast<void*>(static_cast<uintptr_t>(std::stoull(columns[3])));
            int event=std::stoi(columns[4]),reason=std::stoi(columns[5]);
            if(!first)first=frame;
            if(previous && previous!=frame)scenes.Complete(previous);
            previous=frame;
            if(event==1) {
                if(reason==5 || reason==6 || reason==7)scenes.Store(scene,pose,frame);else scenes.Erase(scene);
            } else if(event==2 && frame!=first) {
                Check(scenes.Find(scene)!=0,"recorded world draw retains its camera");
                if(pose)Check(scenes.Find(scene)==pose,"recorded draw retains the correct pose");
                ++draws;if(reason==1)++recovered;
            }
        }
        Check(draws>0,"camera replay contains draws");
        printf("PASS camera history replay: %llu draws, %llu previously missing camera tags recovered\n",draws,recovered);
    }
    {
        Transport::Header mailbox{};Transport::TrackingReader reader;Transport::Tracking sample{},result{};
        sample.id=7;sample.tick=1000;sample.valid=1;Transport::WriteTracking(&mailbox,sample);
        Check(reader.Read(&mailbox,result,1000) && result.id==7,"read fresh tracking");
        InterlockedExchange(&mailbox.trackingLock,1);
        Check(reader.Read(&mailbox,result,1011) && result.id==7,"writer contention preserves fresh camera sample");
        Check(!reader.Read(&mailbox,result,1250),"contended sample expires after 250ms");
        InterlockedExchange(&mailbox.trackingLock,0);
        sample.id=8;sample.tick=1250;Transport::WriteTracking(&mailbox,sample);
        Check(reader.Read(&mailbox,result,1251) && result.id==8,"fresh sample replaces contended cache");
        sample.valid=0;Transport::WriteTracking(&mailbox,sample);
        Check(!reader.Read(&mailbox,result,1252),"runtime tracking loss invalidates cached sample");
        Check(!reader.Read(nullptr,result,1253) && !reader.latest.valid,"closed channel clears sample cache");
    }
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
        Matrix world=Rotation({0,0,0,1});world.m[12]=100;world.m[13]=-20;world.m[14]=300;
        auto reconstruct=DepthToWorld(world,t,eye,300);
        auto project=EyeProjection(p,t,eye,300);
        for(float z:{100.f,1000.f})for(float u:{.1f,.5f,.9f})for(float v:{.2f,.8f}) {
            float input[]={u*z,v*z,z,1},point[4]{},clip[4]{};
            for(int j=0;j<4;j++)for(int k=0;k<4;k++)point[j]+=input[k]*reconstruct.m[k*4+j];
            point[0]-=100;point[1]+=20;point[2]-=300;
            for(int j=0;j<4;j++)for(int k=0;k<4;k++)clip[j]+=point[k]*project.m[k*4+j];
            Check(Near(clip[0]/clip[3],2*u-1)&&Near(clip[1]/clip[3],1-2*v),"lighting depth reconstruction matches asymmetric eye projection");
        }
    }
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
    puts("PASS tracking mailbox contention/expiry, camera math, asymmetric stereo frustum and binocular HUD alignment");
}
