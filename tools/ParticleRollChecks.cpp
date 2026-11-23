#include "ParticleRoll.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
void Check(bool ok,const char* name){if(!ok){printf("FAIL %s\n",name);std::exit(1);}printf("PASS %s\n",name);}
int main() {
    // Sweep yaw, pitch and roll. Native viewport is right/up/forward.
    bool sweep=true,unchanged=true;
    for(float yaw=-3;yaw<3;yaw+=.3f)for(float pitch=-1.4f;pitch<1.4f;pitch+=.2f)
    for(float roll=-3;roll<3;roll+=.3f) {
        float x=std::sin(yaw)*std::cos(pitch),y=std::cos(yaw)*std::cos(pitch),z=std::sin(pitch);
        float r[3]={std::cos(yaw),-std::sin(yaw),0};
        float u[3]={-std::sin(yaw)*z,-std::cos(yaw)*z,std::cos(pitch)};
        float m[16]={0,0,0,7,0,0,0,8,x,y,z,9,12,13,14,1},before[16];
        for(int j=0;j<3;j++){m[j]=r[j]*std::cos(roll)+u[j]*std::sin(roll);m[4+j]=u[j]*std::cos(roll)-r[j]*std::sin(roll);}
        std::memcpy(before,m,sizeof(m));sweep&=ParticleRoll::Level(m);
        for(int j=0;j<3;j++)sweep&=std::abs(m[j]-r[j])<.0001f&&std::abs(m[4+j]-u[j])<.0001f;
        unchanged&=m[3]==7&&m[7]==8&&!std::memcmp(m+8,before+8,8*sizeof(float));
    }
    Check(sweep,"yaw/pitch/roll sweep removes roll and preserves pitch/yaw");
    Check(unchanged,"forward position and homogeneous lanes unchanged");
    float vertical[16]={1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1},saved[16];
    std::memcpy(saved,vertical,sizeof(saved));
    Check(!ParticleRoll::Level(vertical)&&!std::memcmp(saved,vertical,sizeof(saved)),"vertical view remains native");
    float wrong[16]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
    Check(!ParticleRoll::Level(wrong),"player-camera handedness rejected");
    wrong[6]=1;wrong[0]=2;Check(!ParticleRoll::Level(wrong),"scaled basis rejected");
    wrong[0]=std::numeric_limits<float>::quiet_NaN();Check(!ParticleRoll::Level(wrong),"nonfinite basis rejected");
    {float a[16]={1,0,0,0,0,0,1,0,0,1,0,0,5,6,7,1},v[16];std::memcpy(v,a,sizeof(a));
     for(int i=4;i<7;i++)v[i]=-a[i];float r=9,p=9;ParticleRoll::MatchGap(v,a,r,p);
     Check(r==0&&p==0,"log gap is zero for the matching camera");
     v[0]+=.05f;v[12]+=3;ParticleRoll::MatchGap(v,a,r,p);
     Check(std::abs(r-.05f)<.0001f&&std::abs(p-3)<.0001f,"log gap reports rotation and position separately");}
    {float level[3]={1,0,0},tilted[3]={.9f,0,.2f};
     Check(!ParticleRoll::Tilted(level)&&ParticleRoll::Tilted(tilted),"log recognises a tilted head");}
    return 0;
}
