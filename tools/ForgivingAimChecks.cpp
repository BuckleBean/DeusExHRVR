// Offline checks for forgiving aim's prompt hold and ray. No game code runs.
#include <cstdio>
#include <cmath>
#include "ForgivingAim.h"
using namespace ForgivingAim;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static const float Tan15=.26795f;
// Aim from the origin toward (side,0,300); the object sits at (0,0,300).
static Frame Aim(float side) {
    Frame f{};f.dir[0]=side;f.dir[2]=300;Normalize(f.dir);f.tanCone=Tan15;f.minRadius=45;return f;
}
static const float P[3]{0,0,300};
static Candidate C(uintptr_t owner,float score,float x=0){Candidate c{};c.owner=owner;c.point[0]=x;c.point[2]=300;c.score=score;return c;}
static bool At(const float* p,const float* q){return p&&std::abs(p[0]-q[0])<1e-4f&&std::abs(p[1]-q[1])<1e-4f&&std::abs(p[2]-q[2])<1e-4f;}
int main() {
    Candidate none[1]{};
    {
        float s{};const Frame f=Aim(0);
        const float miss[3]{Tan15*150,0,300},outside[3]{Tan15*450,0,300},close[3]{30,0,20},behind[3]{0,0,-50},distant[3]{0,0,700};
        Check(Score(f.origin,f.dir,P,948,f.tanCone,f.minRadius,s)&&s<1e-5f,"object on the line scores zero");
        Check(Score(f.origin,f.dir,miss,948,f.tanCone,f.minRadius,s)&&std::abs(s-.5f)<1e-3f,"near miss at normal distance is inside the cone");
        Check(!Score(f.origin,f.dir,outside,948,f.tanCone,f.minRadius,s),"object well off to the side is not chosen");
        Check(Score(f.origin,f.dir,close,948,f.tanCone,f.minRadius,s)&&s<1,"up close the cone keeps a minimum width");
        Check(!Score(f.origin,f.dir,behind,948,f.tanCone,f.minRadius,s),"object behind the hand is not chosen");
        Check(!Score(f.origin,f.dir,distant,632,f.tanCone,f.minRadius,s),"object beyond its own interaction reach is not chosen");
    }
    {
        Selector s;
        Check(s.Update(none,0,Aim(0),1000,false)==nullptr,"nothing selected starts on the plain line");
        s.NoteSelection(1,P,1000);
        Check(At(s.Update(none,0,Aim(0),1010,false),P),"a selected object is held on the point the game accepted");
        s.NoteSelection(1,P,1010);
        Check(At(s.Update(none,0,Aim(100),1020,false),P),"a small hand movement keeps the prompt");
        s.NoteSelection(1,P,1020);
        Check(At(s.Update(none,0,Aim(200),1030,false),P),"a larger wobble still keeps the prompt at first");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        uint64_t t=1010;
        for(;t<1250;t+=10){s.Update(none,0,Aim(200),t,false);s.NoteSelection(1,P,t);}
        const auto* back=s.Update(none,0,Aim(0),t,false);s.NoteSelection(1,P,t);
        Check(At(back,P)&&s.GetStats().wobblesHeld==1&&s.GetStats().released==0,"returning within the hold time never drops it");
        t+=10;
        for(uint64_t end=t+290;t<end;t+=10){s.Update(none,0,Aim(200),t,false);s.NoteSelection(1,P,t);}
        Check(s.Held()==1,"aim away for under 300 ms still holds");
        for(int i=0;i<3;i++,t+=10){s.Update(none,0,Aim(200),t,false);if(s.Held())s.NoteSelection(1,P,t);}
        Check(!s.Held()&&s.GetStats().released==1,"clearly aiming away for 300 ms releases it");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        s.Update(none,0,Aim(0),1050,false);
        Check(s.Held()==1,"one missed update does not drop the object");
        s.Update(none,0,Aim(0),1130,false);
        Check(!s.Held()&&s.GetStats().lost==1,"object the game stops offering (used or gone) is let go");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float* p{};
        for(uint64_t t=1010;t<1600;t+=10){p=s.Update(none,0,Aim(250),t,true);}
        Check(At(p,P)&&s.Held()==1,"holding Interact keeps the object regardless of aim or selection");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float q[3]{120,0,300};Candidate l[]={C(2,.05f,120)};const Frame f=Aim(120);
        const float* p=s.Update(l,1,f,1010,false);s.NoteSelection(1,P,1010);
        Check(At(p,P),"a neighbour does not steal the prompt immediately");
        p=s.Update(l,1,f,1060,false);s.NoteSelection(1,P,1060);
        Check(At(p,P),"a brief pass over a neighbour does not steal it");
        p=s.Update(l,1,f,1135,false);
        Check(At(p,q)&&s.GetStats().switches==1,"clearly pointing at another object moves to it");
        s.NoteSelection(2,q,1135);
        Check(s.Held()==2,"the game's new selection becomes the held object");
    }
    {
        Selector s;Candidate l[]={C(1,.6f,10),C(2,.2f,20)};const float two[3]{20,0,300};
        Check(s.Update(l,2,Aim(0),1000,false)==nullptr,"with nothing selected the plain line gets its turn");
        Check(At(s.Update(l,2,Aim(0),1010,false),two),"then the object closest to the aim is tried");
        Check(s.Update(l,2,Aim(0),1020,false)==nullptr,"plain line and near-miss attempts alternate");
        s.Update(l,2,Aim(0),1030,false);s.Update(l,2,Aim(0),1100,false);s.Update(l,2,Aim(0),1170,false);
        Check(s.GetStats().refused==1&&s.GetStats().lastRefused==2,"an object the game refuses is recorded and skipped");
        const float one[3]{10,0,300};const float* p=s.Update(l,2,Aim(0),1180,false);if(!p)p=s.Update(l,2,Aim(0),1190,false);
        Check(At(p,one),"the next closest object is tried");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        s.Update(none,0,Aim(0),9000,false);
        Check(!s.Held()&&s.GetStats().lost==0&&s.GetStats().refused==0,"returning from a hacking screen starts fresh without a refusal");
    }
    {
        Selector s;s.Update(none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float moved[3]{5,0,300};s.Update(none,0,Aim(0),1010,false);s.NoteSelection(1,moved,1010);
        Check(At(s.Update(none,0,Aim(0),1020,false),moved),"the held point follows what the game reports");
    }
    {
        CameraMath::Matrix hint{};hint.m[0]=1;hint.m[5]=1;hint.m[10]=1;hint.m[15]=1;
        const float o[3]{10,20,30},p[3]{13,24,30};CameraMath::Matrix r{};
        bool ok=RayMatrix(hint,o,p,r);
        Check(ok&&std::abs(r.m[8]-.6f)<1e-5f&&std::abs(r.m[9]-.8f)<1e-5f&&r.m[12]==10&&r.m[13]==20,"ray runs from the aim origin to the target");
        float c[3];Cross(r.m,r.m+4,c);
        Check(ok&&std::abs(Dot(r.m,r.m)-1)<1e-5f&&std::abs(Dot(r.m,r.m+8))<1e-5f&&Dot(c,r.m+8)>0,"ray matrix is rigid with the same handedness");
        ok=RayMatrix(hint,o,o,r);
        Check(ok&&std::abs(r.m[10]-1)<1e-5f,"target at the origin falls back to the aim direction");
    }
    return failures?1:0;
}
