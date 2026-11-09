#include "ArmBodyHeading.h"
#include <stdexcept>
#include <cstdio>
using namespace ArmBodyHeading;
constexpr double Pi=3.14159265358979323846;
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
CameraMath::Matrix Frame(double degrees){double a=degrees*Pi/180;float x=float(std::sin(a)),y=float(std::cos(a));return {{y,-x,0,0,0,0,-1,0,x,y,0,0,0,0,587.326f,1}};}
CameraMath::Matrix Rotate(CameraMath::Matrix m,unsigned axis,double degrees){
    float a=float(degrees*Pi/360);CameraMath::Quaternion q{0,0,0,std::cos(a)};
    if(axis==0)q.x=std::sin(a);if(axis==1)q.y=std::sin(a);if(axis==2)q.z=std::sin(a);
    auto out=CameraMath::Multiply(m,CameraMath::Rotation(q));for(unsigned j=12;j<15;j++)out.m[j]=m.m[j];return out;
}
int main(){try{
    Context ctx{1,2,3,300};auto base=Frame(0);CameraMath::Matrix out{};State state;
    unsigned frame=1,poses=0;Check(state.Update(base,base,ctx,frame,1000,out),"seed");auto expected=out;
    for(int yaw=-180;yaw<=180;yaw+=30)for(int pitch=-90;pitch<=90;pitch+=30)for(int roll=-90;roll<=90;roll+=45){
        auto look=Rotate(Rotate(Rotate(base,2,yaw),0,pitch),1,roll);
        ++frame;Check(state.Update(look,base,ctx,frame,1000+frame*10,out),"look valid");
        Check(!memcmp(&out,&expected,sizeof(out)),"head rotation affected shoulder frame");
        poses++;
        Check(state.Update(Frame(90),base,ctx,frame,2000+frame*10,out)&&!memcmp(&out,&expected,sizeof(out)),"repeat moved frame");
    }
    for(int turn:{-90,-45,0,45,90}){
        auto moved=Frame(turn);moved.m[12]=100;moved.m[13]=-30;moved.m[14]=450;
        ++frame;Check(state.Update(moved,Frame(turn),ctx,frame,10000+frame*10,out),"base turn");
        Check(std::abs(Yaw(out)-turn*Pi/180)<1e-6&&out.m[12]==100&&out.m[13]==-30&&out.m[14]==450,"base direction/position wrong");
    }
    auto prior=out,bad=base;bad.m[0]=NAN;
    Check(!state.Update(bad,base,ctx,++frame,20000,out)&&!memcmp(&prior,&out,sizeof(out)),"invalid input writes");
    state.Reset();ctx.epoch++;Check(state.Update(Frame(90),base,ctx,++frame,21000,out),"reacquire");
    Check(std::abs(Yaw(out))<1e-9,"reacquire used head yaw");
    prior=out;Check(!state.Update(base,base,ctx,frame-1,22000,out)&&!memcmp(&prior,&out,sizeof(out)),"stale draw writes");
    printf("PASS independent look: %u yaw/pitch/roll combinations, exact stationary frame, repeated draws, base turns, translation/crouch, reset and invalid input.\n",poses);
    return 0;
}catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}}
