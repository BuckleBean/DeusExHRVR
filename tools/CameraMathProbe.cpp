#include "CameraMath.h"
#include "NativeHeading.h"
#include "CameraPosition.h"
#include "DirectionConfig.h"
#include "NativeBounds.h"
#include "SceneCache.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
void Check(bool value,const char* name){if(!value){fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
#include "GamepadChecks.h"
#include "ImmersiveScope.h"
#include "WeaponCalibrationChecks.h"
int main(int argc,char** argv) {
    {
        auto body=NativeHeading::Base(.3f,{});body.m[14]=565;
        for(float x:{0.f,1000.f,-20000.f})for(float y:{0.f,2000.f,-30000.f}) {
            for(float sway:{-12.f,-3.f,0.f,3.f,12.f}) {
                auto camera=body;camera.m[12]=x+sway;camera.m[13]=y-sway;
                Check(CameraPosition::UseBodyXY(camera,x,y),"near-body animation offset accepted");
                Check(camera.m[12]==x && camera.m[13]==y,"horizontal camera animation removed without lagging movement or teleport");
                for(int i=0;i<16;i++)if(i!=12 && i!=13)Check(camera.m[i]==body.m[i],"body anchor preserves heading and stance height");
                Transport::Pose ref{};ref.orientation.w=1;
                auto head=ref;head.position.x=.1f;
                auto tracked=CameraMath::HeadWorld(camera,ref,head,300);
                Check(std::abs(std::hypot(tracked.m[12]-x,tracked.m[13]-y)-30.f)<.003f,"body anchor preserves physical headset movement");
            }
        }
        auto distant=body;distant.m[12]=46;
        Check(!CameraPosition::UseBodyXY(distant,0,0)&&distant.m[12]==46,"pull-back camera remains untouched");
        auto invalid=body;
        Check(!CameraPosition::UseBodyXY(invalid,std::nanf(""),0),"invalid player coordinate rejected");
        Check(invalid.m[14]==565,"rejected anchor retains eye height");
        puts("PASS camera body anchor, animation rejection, stance, pull-back guard and headset movement");
    }
    {
        using namespace CameraMath;
        Matrix captured{{.913851619f,.406048357f,0,0,
            -.401192635f,.902923405f,-.154187694f,0,
            -.0626077354f,.140904635f,.988041580f,0,
            6753.62207f,-28916.1758f,-26243.2266f,1}};
        auto clean=NativeHeading::Base(.418125629f,captured);
        Check(std::abs(HeadingDelta(captured,clean))<.00001f,"native aiming yaw matches captured camera heading convention");
        Matrix position{};position.m[12]=12;position.m[13]=-7;position.m[14]=50;
        for(float yaw:{-3.14f,-1.57f,0.f,1.57f,3.14f}) {
            auto base=NativeHeading::Base(yaw,position);
            for(float animation:{-.08f,-.02f,0.f,.02f,.08f}) {
                auto animated=Multiply(Rotation({0,0,std::sin(animation/2),std::cos(animation/2)}),base);
                auto result=NativeHeading::Base(yaw,animated);
                for(int i=0;i<16;i++)Check(Near(result.m[i],base.m[i]),"animation rotation cannot enter native heading base");
            }
            for(float turn:{-.785398163f,.785398163f,1.570796327f}) {
                auto turned=NativeHeading::Base(yaw+turn,position);
                Check(Near(HeadingDelta(base,turned),turn),"intentional snap turns pass without filtering including angle wrap");
            }
            Pose reference{};reference.orientation.w=1;
            Pose head=reference;head.position.x=.1f;
            auto tracked=HeadWorld(base,reference,head,300);
            Check(Near(std::hypot(tracked.m[12]-base.m[12],tracked.m[13]-base.m[13]),30),"native heading preserves headset translation");
        }
        NativeHeading::Sample sample{1,2,42,.5f,true};
        Check(sample.Matches(1,2,42),"same player and engine frame accepts input heading");
        Check(!sample.Matches(1,2,43)&&!sample.Matches(1,2,41),"stale and future engine frames rejected");
        Check(!sample.Matches(3,2,42)&&!sample.Matches(1,3,42)&&!sample.Matches(0,2,42),"different manager or player rejected");
        sample.valid=false;Check(!sample.Matches(1,2,42),"uninitialized native heading rejected");
    }
    CheckGamepadMapping();
    CheckWeaponCalibration();
    {
        ImmersiveScope::Gesture gesture;Transport::Tracking t{};
        t.valid=1;t.rightController.valid=1;t.head.orientation.w=t.rightController.aim.orientation.w=1;
        t.rightController.aim.position={.06f,-.08f,-.18f};t.tick=100;
        Check(!gesture.Update(t,true,100),"scope gesture requires dwell");
        t.tick=260;Check(gesture.Update(t,true,260),"scope enters with aligned controller near eye");
        t.rightController.aim.position.z=-.34f;t.tick=280;
        Check(gesture.Update(t,true,280),"scope exit hysteresis prevents threshold chatter");
        t.rightController.aim.position.z=-.5f;t.tick=300;
        Check(!gesture.Update(t,true,300),"scope exits when rifle lowered");
        t.rightController.aim.position.z=-.18f;t.tick=400;gesture.Update(t,true,400);
        Check(!gesture.Update(t,true,700),"stale tracking cannot activate scope");
        for(auto& eye:t.eyes){eye.pose.orientation.w=1;eye.left=-.7f;eye.right=.8f;eye.up=.75f;eye.down=-.65f;}
        t.head.position={1,2,3};t.eyes[0].pose.position={.968f,2,3};t.eyes[1].pose.position={1.032f,2,3};
        auto zoomed=ImmersiveScope::Zoom(t,4);
        for(unsigned eye=0;eye<2;++eye){auto optical=CameraMath::EyeWorld(zoomed,eye,300);
            Check(Near(optical.m[12],0)&&Near(optical.m[13],0)&&Near(optical.m[14],0),"scope eyes share firing origin");}
        for(unsigned eye=0;eye<2;++eye){
            CameraMath::Matrix depth{};depth.m[10]=1;depth.m[14]=-.1f;
            auto projection=CameraMath::EyeProjection(depth,zoomed,eye,300);
            auto hud=CameraMath::HudClipTransform(zoomed,eye);
            for(float distance:{300.f,3000.f,30000.f})for(int axis:{0,1})
                Check(Near((projection.m[8+axis]*distance+projection.m[12+axis])/distance,hud.m[12+axis]/hud.m[15]),"scope reticle follows firing ray at every distance in both eyes");
        }
        Check(Near(t.eyes[0].pose.position.x,.968f),"scope does not change physical eye poses");
        Check(Near(std::tan(zoomed.eyes[0].left)*4,std::tan(t.eyes[0].left)),"scope scales asymmetric frustum tangents");
        Check(t.eyes[0].left==-.7f,"scope leaves compositor frustum unchanged");
    }
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
    {
        struct Volume {float data[20]{};uint32_t type{},canary=0x12345678;} v;
        static_assert(offsetof(Volume,type)==0x50);
        v.data[0]=100;v.data[1]=-500;v.data[2]=90;v.data[3]=25;
        Check(NativeBounds::ExpandWeapon(&v,1200) && v.type==0 && v.canary==0x12345678,
            "controlled gun uses finite sphere accepted by loading cell callbacks");
        Check(v.data[0]==100 && v.data[1]==-500 && v.data[2]==90 && v.data[3]==1225,
            "expanded sphere preserves world centre and includes native bounds plus controller reach");
        v={};v.type=5;v.data[0]=3;v.data[5]=4;v.data[10]=12;v.data[12]=99;
        Check(NativeBounds::ExpandWeapon(&v,1200) && v.type==0 && v.data[0]==99 && v.data[3]>=1213,
            "native box converts to a containing finite sphere");
        v.type=7;auto old=v;
        Check(!NativeBounds::ExpandWeapon(&v,1200) && !std::memcmp(&v,&old,sizeof(v)),
            "unknown bounds retain original game data");
    }
    {
        using namespace DirectionConfig;
        Check(Parse(L"headset",Source::Mouse)==Source::Headset && Parse(L"Controller",Source::Mouse)==Source::Controller,
            "direction settings accept readable case-insensitive device names");
        Check(Parse(L"invalid",Source::Mouse)==Source::Mouse,"invalid direction safely retains mouse mode");
        Matrix level{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 10,20,30,1}};
        for(float nativeYaw:{-2.f,0.f,1.5f})for(float turn:{-1.2f,0.f,1.4f})for(float pitch:{-1.5707963f,-.8f,0.f,.9f,1.5707963f}) {
            auto base=Multiply(level,Rotation({0,0,std::sin(nativeYaw/2),std::cos(nativeYaw/2)}));
            auto yawed=Multiply(base,Rotation({0,0,std::sin(turn/2),std::cos(turn/2)}));
            auto aimed=Multiply(Rotation({std::sin(pitch/2),0,0,std::cos(pitch/2)}),yawed);
            aimed.m[12]+=200;aimed.m[14]+=150;
            auto move=HorizontalDirection(aimed,base);
            Check(Near(move.m[10],0) && Near(std::hypot(move.m[8],move.m[9]),1),"movement remains horizontal and normalized through vertical aim");
            Check(Near(move.m[8],yawed.m[8]) && Near(move.m[9],yawed.m[9]),"head/controller pitch does not alter walking heading");
            Check(Near(HeadingDelta(base,aimed),turn),"walking yaw delta preserves native heading convention across turns");
            for(auto input:{MovementAxes{0,1},MovementAxes{1,0},MovementAxes{0,-1},MovementAxes{-.4f,.6f}}) {
                auto axes=ReorientMovement(input.strafe,input.walk,base,aimed);
                Check(Near(std::hypot(axes.strafe,axes.walk),std::hypot(input.strafe,input.walk)),"walking remap preserves analog speed");
                // Native input angle is atan2(strafe,walk), whose yaw sense
                // opposes ordinary Cartesian rotation of an (x,y) vector.
                float expected=std::atan2(input.strafe,input.walk)+turn;
                Check(Near(std::remainder(std::atan2(axes.strafe,axes.walk)-expected,6.28318530718f),0),
                    "native movement angle adds selected heading instead of mirroring it");
                auto restored=ReorientMovement(axes.strafe,axes.walk,aimed,base);
                Check(Near(restored.strafe,input.strafe) && Near(restored.walk,input.walk),"movement heading remap is reversible");
            }
            for(int j=12;j<15;j++)Check(Near(move.m[j],base.m[j]),"walking reference position stays at native player camera");
        }
        Matrix left{{0,1,0,0, 0,0,-1,0, -1,0,0,0, 0,0,0,1}};
        auto axes=ReorientMovement(0,1,level,left);
        Check(Near(axes.strafe,1) && Near(axes.walk,0),"regression: native north plus headset west uses positive quarter-turn input");
        axes=ReorientMovement(1,0,level,left);
        Check(Near(axes.strafe,0) && Near(axes.walk,-1),"quarter-turn remaps the strafe axis with the opposite cross term");
        Matrix right{{0,-1,0,0, 0,0,-1,0, 1,0,0,0, 0,0,0,1}};
        axes=ReorientMovement(0,1,level,right);
        Check(Near(axes.strafe,-1) && Near(axes.walk,0),"east and west heading corrections are opposite");
    }
    {
        Matrix base{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 100,200,300,1}};
        Pose reference{{0,0,0,1},{0,0,0}};
        for(float yaw:{-.7f,0.f,.8f}) {
            Pose aim{{0,std::sin(yaw/2),0,std::cos(yaw/2)},{.3f,-.2f,-.4f}};
            auto hand=HeadWorld(base,reference,aim,300);
            auto muzzle=ControllerMuzzle(base,reference,aim,300,.25f);
            auto firing=FiringFromMuzzle(muzzle);
            for(int j=0;j<3;j++) {
                Check(Near(-muzzle.m[4+j],hand.m[8+j]),"native -Y muzzle direction follows controller aim");
                Check(Near(muzzle.m[8+j],-hand.m[4+j]),"gun top follows controller up, not down");
                Check(Near(firing.m[8+j],hand.m[8+j]),"player raycast +Z aims along the visible gun barrel");
                Check(Near(firing.m[12+j],muzzle.m[12+j]),"player shot starts at the controlled muzzle");
                Check(Near(muzzle.m[12+j],hand.m[12+j]+hand.m[8+j]*75),"muzzle offset uses game world scale");
            }
            auto native=base;auto delta=Multiply(InverseRigid(native),muzzle);
            auto visible=Multiply(native,delta);
            for(int i=0;i<16;i++)Check(Near(visible.m[i],muzzle.m[i]),"visual muzzle and native firing query share one target transform");
            auto bone=base;bone.m[12]+=15;bone.m[14]+=20;
            auto nativeRelative=Multiply(bone,InverseRigid(native));
            auto controlledRelative=Multiply(Multiply(bone,delta),InverseRigid(muzzle));
            for(int i=0;i<16;i++)Check(std::abs(nativeRelative.m[i]-controlledRelative.m[i])<.001f,"weapon animation is preserved relative to its muzzle");
            auto expectedBone=Multiply(bone,delta);
            for(float nativeW:{0.f,1.f}) {
                auto skin=bone;skin.m[15]=nativeW;
                auto moved=MoveSkinMatrix(skin,delta);
                for(int i=0;i<15;i++)Check(Near(moved.m[i],expectedBone.m[i]),"native zero-w skinning bones retain controller translation");
                Check(moved.m[15]==nativeW,"native skinning padding is preserved");
            }
        }
    }
    {
        Matrix level{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 10,20,30,1}};
        Pose neutral{{0,0,0,1},{0,0,0}};
        for(float yaw:{-2.f,0.f,1.3f})for(float pitch:{-1.55f,-.4f,0.f,.7f,1.55f}) {
            auto base=Multiply(level,Rotation({0,0,std::sin(yaw/2),std::cos(yaw/2)}));
            auto aiming=Multiply(Rotation({std::sin(pitch/2),0,0,std::cos(pitch/2)}),base);
            auto savedAim=aiming;
            auto camera=WithoutLookPitch(aiming);
            Check(!memcmp(&aiming,&savedAim,sizeof(aiming)),"camera lock leaves native gun aiming matrix unchanged");
            Check(Near(aiming.m[10],std::sin(pitch)),"native gun still aims up and down");
            for(int i=0;i<16;i++)Check(Near(camera.m[i],base.m[i]),"camera lock preserves yaw and position across near-vertical native aim");
            for(float headPitch:{-.6f,0.f,.5f}) {
                auto head=neutral;head.orientation={std::sin(headPitch/2),0,0,std::cos(headPitch/2)};
                auto tracked=HeadWorld(camera,neutral,head,300);
                Check(Near(tracked.m[10],std::sin(headPitch)),"headset pitch stays independent of native gun pitch");
            }
        }
    }
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
        auto centreView=InverseRigid(world);
        auto fullEyeView=Multiply(centreView,InverseRigid(EyeWorld(t,eye,300)));
        auto oldCombined=Multiply(centreView,EyeProjection(p,t,eye,300));
        auto newCombined=Multiply(fullEyeView,EyeFrustum(p,t,eye));
        for(int i=0;i<16;i++)Check(Near(oldCombined.m[i],newCombined.m[i]),"explicit eye view preserves geometry clip positions");
        auto eyeAbsolute=Multiply(EyeWorld(t,eye,300),world);
        auto viewIdentity=Multiply(eyeAbsolute,fullEyeView);
        for(int i=0;i<16;i++)Check(Near(viewIdentity.m[i],i%5==0?1.f:0.f),"shader eye position and view agree");
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
    {
        // A captured projected-shadow transform. Both eyes must sample the
        // same effect location for one surface point, including canted views.
        Matrix effect{{-.000248601f,.00188389f,-.00010397f,0,
            .00197977f,.000278757f,.00000880958f,0,
            -.000136736f,.000610938f,.000316581f,0,
            -2.01755f,-7.06894f,-.484532f,1}};
        auto sample=t;
        for(float cant:{0.f,.08f})for(unsigned eye=0;eye<2;eye++) {
            float angle=eye?cant:-cant;
            sample.eyes[eye].pose.orientation={0,std::sin(angle/2),0,std::cos(angle/2)};
            auto eyeWorld=EyeWorld(sample,eye,300);
            auto corrected=Multiply(eyeWorld,effect);
            auto centreToEye=InverseRigid(eyeWorld);
            for(float depth:{30.f,300.f,3000.f}) {
                float centre[]={17,-21,depth,1},local[4]{},expected[4]{},actual[4]{};
                for(int j=0;j<4;j++)for(int k=0;k<4;k++) {
                    local[j]+=centre[k]*centreToEye.m[k*4+j];
                    expected[j]+=centre[k]*effect.m[k*4+j];
                }
                for(int j=0;j<4;j++)for(int k=0;k<4;k++)actual[j]+=local[k]*corrected.m[k*4+j];
                for(int j=0;j<4;j++)Check(Near(actual[j],expected[j]),"projected effect stays on one surface for both eyes");
            }
        }
    }
    {
        auto dome=Rotation({0,0,0,1});dome.m[12]=58577;dome.m[13]=-9471;
        auto camera=Rotation({0,0,0,1});camera.m[12]=58580;camera.m[13]=-9470;
        for(unsigned eye=0;eye<2;eye++) {
            auto sky=SkyProjection(dome,camera,p,t,eye);
            auto moved=camera;moved.m[12]+=100;moved.m[14]-=200;
            auto same=SkyProjection(dome,moved,p,t,eye);
            for(int i=0;i<16;i++)Check(Near(sky.m[i],same.m[i]),"sky ignores head translation");
            const auto& e=t.eyes[eye];float l=std::tan(e.left),r=std::tan(e.right);
            for(float z:{100.f,10000.f}) {
                float ndc=(.2f*z*sky.m[0]+z*sky.m[8]+sky.m[12])/z;
                float ray=(ndc*(r-l)+(r+l))*.5f;
                Check(Near(ray,.2f),"cloud direction has no finite IPD disparity in asymmetric eyes");
            }
        }
        auto turned=Rotation({0,std::sqrt(.5f),0,std::sqrt(.5f)});
        auto sky=SkyProjection(dome,turned,p,t,0);
        auto expected=Multiply(InverseRigid(turned),EyeProjection(p,t,0,0));
        for(int i=0;i<16;i++)Check(Near(sky.m[i],expected.m[i]),"sky follows world orientation under head turns");
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
        // HudSize: the plane shrinks about its centre at the same distance; out-of-range sizes are full size.
        auto small=HudClipTransform(t,eye,.8f);
        for(float x:{-.9f,0.f,.9f}) {
            float clipX=x*small.m[0]+small.m[12],clipW=x*small.m[3]+small.m[15];
            const auto& e=t.eyes[eye];float l=std::tan(e.left),r=std::tan(e.right);
            float ray=(clipX/clipW*(r-l)+(r+l))*.5f;
            Check(Near(e.pose.position.x+2.f*ray,x*1.6f*.8f),"an 80% HUD lands 80% as far from the centre, in both eyes");
        }
        for(float bad:{0.f,-1.f,1.5f}){auto same=HudClipTransform(t,eye,bad);for(int i=0;i<16;i++)Check(Near(same.m[i],hud.m[i]),"an invalid HUD size draws the full HUD");}
    }
    {
        // Hand panel: a chosen point of the UI rectangle lands on the hand
        // anchor in both eyes, at the hand's depth, with the HUD plane's proportions.
        Transport::Tracking h{};
        h.head.orientation.w=1;h.head.position={1,2,3};
        for(auto& eye:h.eyes){eye.pose.orientation.w=1;eye.left=-.7f;eye.right=.8f;eye.up=.75f;eye.down=-.65f;}
        h.eyes[0].pose.position={.968f,2,3};h.eyes[1].pose.position={1.032f,2,3};
        Transport::Pose anchor{};anchor.orientation.w=1;
        anchor.position={1-.25f,2-.30f,3-.45f}; // left, below, in front (OpenXR -z)
        const Transport::Vector offset{.02f,.09f,0};
        for(unsigned eye=0;eye<2;eye++) {
            CameraMath::Matrix panel;
            Check(CameraMath::HudPanelTransform(h,eye,anchor,-.936f,.93f,.3f,offset,panel),"hand panel accepts a hand in front of the head");
            CameraMath::Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
            auto projection=CameraMath::EyeProjection(depth,h,eye,1.f);
            auto same=[&](float x,float y,float z,float ndcX,float ndcY) {
                float cx=x*projection.m[0]+y*projection.m[4]+z*projection.m[8]+projection.m[12];
                float cy=x*projection.m[1]+y*projection.m[5]+z*projection.m[9]+projection.m[13];
                float cw=x*projection.m[3]+y*projection.m[7]+z*projection.m[11]+projection.m[15];
                float px=ndcX*panel.m[0]+ndcY*panel.m[4]+panel.m[12];
                float py=ndcX*panel.m[1]+ndcY*panel.m[5]+panel.m[13];
                float pw=ndcX*panel.m[3]+ndcY*panel.m[7]+panel.m[15];
                return Near(cx/cw,px/pw)&&Near(cy/cw,py/pw);
            };
            Check(same(-.25f+.02f,-.30f+.09f,.45f,-.936f,.93f),"hand panel centre sits on the hand anchor in each eye");
            Check(same(-.25f+.02f+.1f*.48f,-.30f+.09f-.2f*.27f,.45f,-.836f,.73f),"hand panel keeps the HUD plane's proportions at the chosen scale");
            Check(!same(-.25f+.02f,-.30f+.09f,2.f,-.936f,.93f),"hand panel is at the hand's depth, not the HUD plane's");
            Check(panel.m[2]==0&&panel.m[6]==0&&panel.m[10]==0&&panel.m[14]==0,"hand panel stays an overlay like the HUD plane");
        }
        CameraMath::Matrix unused;
        anchor.position.z=3+.2f;
        Check(!CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"hand panel refuses a hand behind the head");
        anchor.position={1,2,3-.05f};
        Check(!CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"hand panel refuses a hand at the face");
        anchor.position={1.6f,2,2.95f};
        Check(CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"tracked hand to the side remains attached outside view");
        anchor.position={1,2,1};
        Check(CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"tracked hand beyond old depth cutoff remains attached");
        const auto hidden=CameraMath::HiddenHudTransform();
        for(float w:{.01f,1.f,10000.f,-1.f}) {
            // Test actual homogeneous clip inequalities, independent of eye FOV.
            const float x=hidden.m[12]*w,y=hidden.m[13]*w,z=hidden.m[14]*w,cw=hidden.m[15]*w;
            Check(!(x>=-cw && x<=cw && y>=-cw && y<=cw && z>=0 && z<=cw),"hidden HUD is outside clip volume, never at a default screen corner");
        }
        // A turned head: the anchor is still found in head space.
        h.head.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)}; // 90 degrees about up: forward becomes -x
        for(auto& eye:h.eyes)eye.pose.orientation=h.head.orientation;
        anchor.position={1-.45f,2,3};
        Check(CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"hand panel follows the head's own forward");
        anchor.position={1+.45f,2,3};
        Check(!CameraMath::HudPanelTransform(h,0,anchor,0,0,.3f,{0,0,0},unused),"hand panel refuses a hand behind a turned head");
    }
    {
        // Wrist band. Poses are grip poses in the hand model's frame, built
        // from their axes in OpenXR space (right, up, back); head at the origin
        // looking forward (-z).
        auto fromAxes=[](Transport::Vector x,Transport::Vector y,Transport::Vector z) {
            float m00=x.x,m11=y.y,m22=z.z,trace=m00+m11+m22;Transport::Quaternion q{};
            if(trace>0){float s=std::sqrt(trace+1)*2;q.w=.25f*s;q.x=(y.z-z.y)/s;q.y=(z.x-x.z)/s;q.z=(x.y-y.x)/s;}
            else if(m00>m11&&m00>m22){float s=std::sqrt(1+m00-m11-m22)*2;q.w=(y.z-z.y)/s;q.x=.25f*s;q.y=(y.x+x.y)/s;q.z=(z.x+x.z)/s;}
            else if(m11>m22){float s=std::sqrt(1+m11-m00-m22)*2;q.w=(z.x-x.z)/s;q.x=(y.x+x.y)/s;q.y=.25f*s;q.z=(z.y+y.z)/s;}
            else {float s=std::sqrt(1+m22-m00-m11)*2;q.w=(x.y-y.x)/s;q.x=(z.x+x.z)/s;q.y=(z.y+y.z)/s;q.z=.25f*s;}
            return q;
        };
        Transport::Tracking h{};
        h.head.orientation.w=1;
        for(auto& eye:h.eyes){eye.pose.orientation.w=1;eye.left=-.7f;eye.right=.8f;eye.up=.75f;eye.down=-.75f;}
        h.eyes[0].pose.position={-.032f,0,0};h.eyes[1].pose.position={.032f,0,0};
        auto project=[&](unsigned eye,float x,float y,float z,float& u,float& v) {
            CameraMath::Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
            auto p=CameraMath::EyeProjection(depth,h,eye,1.f);
            float cw=x*p.m[3]+y*p.m[7]+z*p.m[11]+p.m[15];
            u=(x*p.m[0]+y*p.m[4]+z*p.m[8]+p.m[12])/cw;v=(x*p.m[1]+y*p.m[5]+z*p.m[9]+p.m[13])/cw;
        };
        auto map=[](const CameraMath::Matrix& m,float nx,float ny,float& u,float& v) {
            float w=nx*m.m[3]+ny*m.m[7]+m.m[15];
            u=(nx*m.m[0]+ny*m.m[4]+m.m[12])/w;v=(nx*m.m[1]+ny*m.m[5]+m.m[13])/w;
        };
        // Hands raised in front, backs of the hands toward the face, as in
        // Mark's 9:45 PM screenshot. Left: +Y (toward the wrist) down, back -X
        // toward the face, +Z (toward the little finger) to the left.
        auto leftPose=fromAxes({0,0,-1},{0,-1,0},{-1,0,0});
        auto rz=CameraMath::Rotate(leftPose,{0,0,1}),ry=CameraMath::Rotate(leftPose,{0,1,0});
        Check(Near(rz.x,-1)&&Near(ry.y,-1),"test pose built from its axes");
        const CameraMath::WristLayout layout{-.936f,.93f,.36f,.08f,.04f};
        const Transport::Vector leftGrip{-.1f,0,-.4f};
        // Band middle: 8 cm below the palm centre (toward the elbow), 4 cm
        // toward the face. Head space (right, up, forward).
        const float sx=-.1f,sy=-.08f,sz=.36f;
        for(unsigned eye=0;eye<2;eye++) {
            CameraMath::Matrix panel;
            Check(CameraMath::WristPanelTransform(h,eye,leftGrip,leftPose,true,layout,false,0,panel),"left band shown with the hand raised");
            float u,v,eu,ev;
            map(panel,-.936f,.93f,u,v);project(eye,sx,sy,sz,eu,ev);
            Check(Near(u,eu)&&Near(v,ev),"band middle sits on the back of the left wrist");
            float ru,rv;map(panel,-.886f,.93f,ru,rv);
            Check(ru>u+.01f&&std::abs(rv-v)<.01f,"left band text reads left to right across the wrist");
            float tu,tv;map(panel,-.936f,.96f,tu,tv);
            Check(tv>v+.005f&&std::abs(tu-u)<.01f,"left band text is upright, toward the hand");
            Check(panel.m[2]==0&&panel.m[6]==0&&panel.m[10]==0&&panel.m[14]==0,"band stays an overlay like the HUD plane");
            // A piece further right wraps round the wrist: it sits on the wrist
            // 4 cm from its centre line, turned toward the thumb side.
            const float drawX=-.886f,theta=(drawX+.936f)*1.6f*.36f/.04f;
            Check(CameraMath::WristPanelTransform(h,eye,leftGrip,leftPose,true,layout,true,drawX,panel),"curved piece shown");
            map(panel,drawX,.93f,u,v);
            project(eye,-.1f+.04f*std::sin(theta),-.08f,.4f-.04f*std::cos(theta),eu,ev);
            Check(Near(u,eu)&&Near(v,ev),"curved piece wraps around the wrist at its own position");
            CameraMath::Matrix flat;CameraMath::WristPanelTransform(h,eye,leftGrip,leftPose,true,layout,false,0,flat);
            float fu,fv;map(flat,drawX,.93f,fu,fv);
            Check(!(Near(u,fu)&&Near(v,fv)),"curved piece differs from the flat block");
        }
        // Wrist turned the other way: the band is on the far side, still shown.
        auto turned=fromAxes({0,0,1},{0,-1,0},{1,0,0});
        CameraMath::Matrix unused;
        Check(CameraMath::WristPanelTransform(h,0,leftGrip,turned,true,layout,false,0,unused),"band stays when the wrist turns away");
        {
            float u,v,eu,ev;CameraMath::Matrix farSide;
            CameraMath::WristPanelTransform(h,0,leftGrip,turned,true,layout,false,0,farSide);
            map(farSide,-.936f,.93f,u,v);project(0,sx,sy,.44f,eu,ev);
            Check(Near(u,eu)&&Near(v,ev),"turned-away band sits on the far side of the wrist");
        }
        Check(!CameraMath::WristPanelTransform(h,0,leftGrip,leftPose,true,layout,true,-.936f+2.4f*.04f/(1.6f*.36f),unused),"piece wrapped past the wrist's far side is hidden");
        Check(!CameraMath::WristPanelTransform(h,0,{0,.08f,-.05f},leftPose,true,layout,false,0,unused),"band at the face is hidden");
        Check(!CameraMath::WristPanelTransform(h,0,{0,0,.4f},leftPose,true,layout,false,0,unused),"band behind the head is hidden");
        // Right hand raised, back toward the face: back +X toward the face,
        // +Z (toward the little finger) to the right.
        auto rightPose=fromAxes({0,0,1},{0,-1,0},{1,0,0});
        const Transport::Vector rightGrip{.1f,0,-.4f};
        const CameraMath::WristLayout right{.93f,-.892f,.36f,.08f,.04f};
        for(unsigned eye=0;eye<2;eye++) {
            CameraMath::Matrix panel;
            Check(CameraMath::WristPanelTransform(h,eye,rightGrip,rightPose,false,right,false,0,panel),"right band shown with the hand raised");
            float u,v,eu,ev;map(panel,.93f,-.892f,u,v);project(eye,.1f,-.08f,.36f,eu,ev);
            Check(Near(u,eu)&&Near(v,ev),"band middle sits on the back of the right wrist");
            float ru,rv;map(panel,.98f,-.892f,ru,rv);Check(ru>u+.01f&&std::abs(rv-v)<.01f,"right band text reads left to right across the wrist");
            float tu,tv;map(panel,.93f,-.862f,tu,tv);Check(tv>v+.005f&&std::abs(tu-u)<.01f,"right band text is upright, toward the hand");
        }
    }
    {   // Scope roll from the head: view axis kept, row 1 toward the head's, rigid.
        const float a=.4f,b=.3f;
        CameraMath::Matrix cam{{std::cos(a),std::sin(a),0,0, -std::sin(a),std::cos(a),0,0, 0,0,1,0, 5,6,7,1}};
        CameraMath::Matrix head{{std::cos(b),0,-std::sin(b),0, 0,1,0,0, std::sin(b),0,std::cos(b),0, 0,0,0,1}};
        auto r=WeaponCalibration::RollFrom(cam,head);
        Check(r.m[8]==cam.m[8]&&r.m[9]==cam.m[9]&&r.m[10]==cam.m[10]&&r.m[12]==5&&r.m[14]==7,"scope roll keeps the view axis and position");
        Check(Near(r.m[4],0)&&Near(r.m[5],1)&&Near(r.m[6],0),"scope row 1 follows the head's (level head: no roll)");
        const float det=r.m[0]*(r.m[5]*r.m[10]-r.m[6]*r.m[9])-r.m[1]*(r.m[4]*r.m[10]-r.m[6]*r.m[8])+r.m[2]*(r.m[4]*r.m[9]-r.m[5]*r.m[8]);
        Check(Near(det,1),"scope roll stays a rotation, same handedness");
        CameraMath::Matrix rolled{{std::cos(b),std::sin(b),0,0, -std::sin(b),std::cos(b),0,0, 0,0,1,0, 0,0,0,1}};
        auto r2=WeaponCalibration::RollFrom(cam,rolled);
        Check(Near(r2.m[4],-std::sin(b))&&Near(r2.m[5],std::cos(b)),"a rolled head rolls the scope picture with it");
        CameraMath::Matrix up{{1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1}};
        auto same=WeaponCalibration::RollFrom(up,CameraMath::Matrix{{1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1}});
        Check(Near(same.m[4],0)&&Near(same.m[6],1),"head and scope already aligned: unchanged");
    }
    puts("PASS tracking mailbox contention/expiry, camera math, asymmetric stereo frustum, binocular HUD alignment, hand panel placement and wrist band placement");
}
