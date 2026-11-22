// Offline checks of the takedown ride-along view. No game code runs.
#include <cstdio>
#include <cmath>
#include "TakedownRide.h"
using namespace TakedownRide;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static bool Near(float a,float b,float e=.01f){return std::abs(a-b)<e;}
static Matrix Bone(float yaw,float x,float y,float z){ // yaw 0: the model's rest pose, facing -Y
    Matrix m;const float s=std::sin(yaw),c=std::cos(yaw);m.m[0]=c;m.m[1]=s;m.m[4]=-s;m.m[5]=c;m.m[10]=1;m.m[12]=x;m.m[13]=y;m.m[14]=z;m.m[15]=1;return m;
}
int main() {
    const float pi=3.14159265f;
    {float e[3],y{};Check(FromBones(Bone(0,-9,0,580),Bone(0,9,0,582),Bone(0,0,0,500),e,y)&&Near(e[0],0)&&Near(e[2],581)&&Near(y,-pi/2),"the eye is midway between the eye bones and the chest faces forward");
     Matrix up=Bone(0,0,0,0);up.m[4]=0;up.m[5]=0;up.m[6]=1;Check(!FromBones(Bone(0,0,0,0),Bone(0,0,0,0),up,e,y),"a chest pointing straight up gives no heading");
     Matrix bad=Bone(0,NAN,0,0);Check(!FromBones(bad,Bone(0,0,0,0),Bone(0,0,0,0),e,y),"a bad bone is refused");}
    Check(Near(Wrap(3*pi),pi)||Near(Wrap(3*pi),-pi),"angles wrap");
    {Ride r;const float a[3]{0,0,560},b[3]{100,0,500};r.Begin(a,0,1000);Matrix v=Bone(0,0,0,560);auto o=v;r.Apply(o);
     Check(Near(o.m[12],0)&&Near(o.m[14],560),"the ride starts exactly at the held view");
     r.Update(b,0,1010);auto s=v;r.Apply(s);Check(s.m[12]<5,"it eases toward the eyes instead of jumping");
     for(double t=1020;t<1800;t+=10)r.Update(b,0,t);auto f=v;r.Apply(f);Check(Near(f.m[12],100,.1f)&&Near(f.m[14],500,.1f),"then it follows the eyes");
     Check(Near(r.maxTravel,std::sqrt(100.f*100.f+60.f*60.f),.2f),"the distance travelled is recorded");}
    {Ride r;const float e[3]{0,0,0};r.Begin(e,0,1000);float peak=0,previous=0;
     for(double t=1010;t<3000;t+=10){r.Update(e,pi/2,t);peak=(std::max)(peak,std::abs(r.yaw-previous)/.01f);previous=r.yaw;}
     Check(Near(r.yaw,pi/2,.01f),"the view turns with Jensen's chest");Check(peak<=MaxYawRate+.01f,"never faster than the turn limit");
     Matrix v=Bone(0,0,0,0);v.m[8]=1;v.m[9]=0;r.Apply(v);Check(Near(v.m[8],0)&&Near(v.m[9],1),"and the turn rotates the view the same way");}
    {Ride r;const float e[3]{0,0,0};r.Begin(e,3.0f,1000);for(double t=1010;t<3000;t+=10)r.Update(e,-3.0f,t);
     Check(Near(std::abs(r.yaw),6.2831853f-6.f,.02f),"a turn across the back takes the short way");}
    {Ride r;Matrix v=Bone(0,1,2,3);auto o=v;r.Apply(o);Check(Near(o.m[12],1)&&Near(o.m[13],2)&&Near(o.m[14],3),"with no ride the view is untouched");}
    return failures?1:0;
}
