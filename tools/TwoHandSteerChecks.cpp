// Offline checks of two-handed aim. No game code runs.
#include <cstdio>
#include <cmath>
#include "TwoHandSteer.h"
#include "ModelHandAlignment.h"
using namespace TwoHandSteer;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static Matrix At(float x,float y,float z){Matrix m;for(float& v:m.m)v=0;m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;m.m[12]=x;m.m[13]=y;m.m[14]=z;return m;}
static Matrix YawAt(float degrees,float x,float y,float z){auto m=At(x,y,z);const float a=degrees*3.14159265f/180,s=std::sin(a),c=std::cos(a);m.m[0]=c;m.m[2]=-s;m.m[8]=s;m.m[10]=c;return m;}
static bool Near(float a,float b,float e=.002f){return std::abs(a-b)<e;}
static void Settle(Steer& s,double& t,bool show,const Matrix& r,const Matrix& l,double until){for(;t<until;t+=10)s.Update(t,show,r,l,300);}
int main() {
    const float pi=3.14159265f,a20=20*pi/180;
    const auto right=At(0,0,0),left=At(0,0,100),swung=At(100*std::sin(a20),0,100*std::cos(a20));
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);auto m=At(0,0,50);
     if(s.amount>0&&s.angle>0)s.Apply(m,right);
     Check(s.angle==0&&Near(m.m[14],50),"grabbing the weapon does not move it");
     Settle(s,t,true,right,swung,1800);auto g=At(0,0,50);s.Apply(g,right);
     Check(Near(g.m[8],std::sin(a20))&&Near(g.m[10],std::cos(a20)),"moving the left hand turns the weapon toward it");
     Check(Near(g.m[12],50*std::sin(a20),.05f)&&Near(g.m[14],50*std::cos(a20),.05f),"the weapon swings around the right hand");
     auto pivot=right;s.Apply(pivot,right);Check(Near(pivot.m[12],0)&&Near(pivot.m[13],0)&&Near(pivot.m[14],0),"the right hand's position stays put");
     float n=g.m[8]*g.m[8]+g.m[9]*g.m[9]+g.m[10]*g.m[10],d=g.m[0]*g.m[8]+g.m[1]*g.m[9]+g.m[2]*g.m[10];
     Check(Near(n,1)&&Near(d,0),"the weapon is not distorted");
     const float held=s.angle;s.Update(t,false,right,swung,300);t+=10;const float eased=s.amount;
     Check(s.angle==held&&eased<1&&eased>0,"releasing eases back instead of snapping");
     Settle(s,t,false,right,swung,t+400);Check(s.amount==0&&!s.Update(t,false,right,swung,300),"and aim returns fully to the right hand");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);bool moved=false;
     for(float deg=0;deg<=40;deg+=2,t+=10){const auto r=YawAt(deg,0,0,0);const float a=deg*pi/180;s.Update(t,true,r,At(100*std::sin(a),0,100*std::cos(a)),300);moved=moved||s.angle>.002f;}
     Check(!moved,"turning both hands together adds no steering");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,At(100,0,-5),1800);Check(Near(s.angle,MaxAngle),"the turn has a limit");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,swung,1800);const float held=s.angle;
     Settle(s,t,true,right,At(1,0,1),1900);Check(Near(s.angle,held),"hands too close together keep the last direction");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,swung,1800);
     Check(!s.Update(9000,true,right,swung,300)&&s.amount==0,"after a pause or load it starts fresh with no jump");}
    {Steer s;Matrix bad=left;bad.m[12]=NAN;Check(!s.Update(1000,true,right,bad,300)&&!s.have,"a bad reading is ignored");}
    {   // The wrist HUD must land exactly where the hand model is drawn, through the real
        // placement function, for several grip poses.
        const Matrix game{{0,-1,0,0, 0,0,-1,0, 1,0,0,0, 10,20,30,1}};
        Transport::Pose reference{};reference.orientation={0,0,0,1};
        auto axisTurn=[&](float x,float y,float z,float deg){const float n=std::sqrt(x*x+y*y+z*z),h=deg*pi/360,s=std::sin(h)/n;return Transport::Quaternion{x*s,y*s,z*s,std::cos(h)};};
        auto place=[&](const Transport::Pose& p,Matrix& out){Transport::Tracking t{};t.valid=1;t.tick=1000;t.rightGrip.valid=1;t.rightGrip.pose=p;return ModelHandAlignment::GripTarget(t,1000,game,reference,300,out);};
        auto same=[](const Matrix& a,const Matrix& b,float e){bool ok=true;for(int i=0;i<16;i++)ok=ok&&std::abs(a.m[i]-b.m[i])<(i>=12&&i<15?e*40:e);return ok;};
        Transport::Pose real{axisTurn(1,2,3,40),{.2f,1.1f,-.3f}},hand{axisTurn(-2,1,.5f,75),{-.1f,.9f,-.6f}};
        Matrix T{},V{},placed{};
        Check(place(real,T)&&place(hand,V),"HUD check: poses place");
        HudShift full,none,half;
        Check(MakeShift(V,T,300,1,full)&&place(ApplyShift(real,full),placed)&&same(placed,V,.002f),"the wrist HUD lands on the visible hand");
        Check(MakeShift(V,T,300,0,none)&&place(ApplyShift(real,none),placed)&&same(placed,T,.002f),"with no shift it stays on the controller");
        Check(MakeShift(V,T,300,.5f,half),"a partial shift is valid");
        const auto mid=ApplyShift(real,half);
        const float qn=mid.orientation.x*mid.orientation.x+mid.orientation.y*mid.orientation.y+mid.orientation.z*mid.orientation.z+mid.orientation.w*mid.orientation.w;
        Check(Near(qn,1),"and stays a clean rotation part way");
        Matrix mirrored=V;for(int j=0;j<3;j++)mirrored.m[j]=-mirrored.m[j];
        HudShift rejected;Check(!MakeShift(mirrored,T,300,1,rejected),"a distorted hand transform is refused");
        Matrix bad=V;bad.m[12]=NAN;Check(!MakeShift(bad,T,300,1,rejected),"a bad position is refused");
        for(float deg:{170.f,179.f,-175.f}) {
            Transport::Pose turnedPose{CameraMath::Multiply(real.orientation,axisTurn(1,0,0,deg)),real.position};Matrix F{};HudShift s;
            Check(place(turnedPose,F)&&MakeShift(F,T,300,1,s)&&place(ApplyShift(real,s),placed)&&same(placed,F,.003f),"large hand turns convert correctly");
        }
    }
    return failures?1:0;
}
