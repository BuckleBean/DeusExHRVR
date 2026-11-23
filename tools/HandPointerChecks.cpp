#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "SharedPair.h"
#include "CameraMath.h"
#include "ModelHandAlignment.h"
#include "HandPointer.h"
static void Check(bool ok,const char* name){if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
static float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
int main() {
    using namespace HandPointer;
    // Settings
    Check(Parse(L"")==Both&&Parse(L"nonsense")==Both,"absent setting shows both");
    for(uint32_t m:{Off,Right,Left,Both})Check(Parse(Value(m))==m,"setting round trip");
    Check(Parse(L"0")==Off,"0 is off");
    Check(Next(Next(Next(Next(Off))))==Off,"menu cycle returns");
    Check(!Shows(Off,false)&&!Shows(Off,true),"off shows neither");
    Check(Shows(Right,false)&&!Shows(Right,true)&&Shows(Left,true)&&!Shows(Left,false),"one hand");
    Check(Shows(Both,false)&&Shows(Both,true),"both hands");
    // Texture: both cells drawn, different colours, transparent corners.
    auto p=Pixels();Check(p.size()==AtlasWidth*Cell,"texture size");
    Check(p[0]==0&&p[Cell]==0,"transparent corners");
    Check(p[31*AtlasWidth+31]!=p[31*AtlasWidth+Cell+31]&&(p[31*AtlasWidth+31]>>24)==255,"each hand has its colour");
    // The line, against the same grip axes that place the visible hand.
    CameraMath::Matrix game{};game.m[0]=1;game.m[6]=-1;game.m[9]=1;game.m[15]=1;game.m[12]=10;game.m[13]=20;game.m[14]=30;
    Transport::Pose reference{{0,0,0,1},{0,0,0}};
    Transport::Tracking t{};t.valid=1;t.tick=1000;
    const float n=std::sqrt(.1f*.1f+.5f*.5f+.3f*.3f+.8f*.8f);
    Transport::Pose grip{{.1f/n,.5f/n,-.3f/n,.8f/n},{.2f,1.1f,-.4f}};
    t.rightGrip={grip,1};t.leftGrip={grip,1};
    for(int side=0;side<2;side++) {
        const bool left=side==1;
        CameraMath::Matrix axes;
        Check(ModelHandAlignment::GripTarget(t,1000,game,reference,300.f,axes,left),"grip axes");
        auto head=CameraMath::HeadWorld(game,reference,grip,300.f);
        float fingers[3],palm[3];
        for(int i=0;i<3;i++){fingers[i]=-.999f*axes.m[4+i]-.047f*axes.m[8+i];palm[i]=(left?1.f:-1.f)*axes.m[i];}
        const float len=std::sqrt(Dot(fingers,fingers));for(auto& v:fingers)v/=len;
        float origin[3],dir[3];
        Check(Line(head.m,left,0,origin,dir),"line, no tilt");
        Check(Dot(dir,fingers)>.9999f,"no tilt follows the visible fingers");
        for(int i=0;i<3;i++)Check(std::abs(origin[i]-axes.m[12+i])<1e-3f,"line starts at the visible palm");
        Check(Line(head.m,left,15,origin,dir),"line, 15 degrees");
        Check(std::abs(Dot(dir,fingers)-std::cos(15*.0174532925f))<1e-3f,"15 degrees from the fingers");
        Check(std::abs(Dot(dir,palm)-std::sin(15*.0174532925f))<1e-3f,"tilted toward this hand's palm");
        Check(std::abs(Dot(dir,dir)-1)<1e-4f,"unit direction");
    }
    float o[3],d[3],bad[16]{};bad[12]=NAN;
    Check(!Line(bad,false,15,o,d),"invalid pose refused");
    std::printf("PASS hand pointer: settings, texture, line follows visible fingers and tilts to each palm\n");
    return 0;
}
