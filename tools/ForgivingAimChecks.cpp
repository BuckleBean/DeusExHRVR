// Offline checks for forgiving aim's target choice and ray. No game code runs.
#include <cstdio>
#include <cmath>
#include "ForgivingAim.h"
using namespace ForgivingAim;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static Candidate C(uintptr_t owner,float score){Candidate c{};c.owner=owner;c.point[0]=score;c.score=score;return c;}
int main() {
    {
        const float o[3]{0,0,0},d[3]{0,0,1},t=std::tan(15.f*3.14159265f/180.f);float s{};
        const float centre[3]{0,0,300};
        Check(Score(o,d,centre,948,t,45,s)&&s<1e-5f,"object on the line scores zero");
        const float miss[3]{t*300*.5f,0,300};
        Check(Score(o,d,miss,948,t,45,s)&&std::abs(s-.5f)<1e-3f,"near miss at normal distance is inside the cone");
        const float outside[3]{t*300*1.5f,0,300};
        Check(!Score(o,d,outside,948,t,45,s),"object well off to the side is not chosen");
        const float close[3]{30,0,20};
        Check(Score(o,d,close,948,t,45,s)&&s<1,"up close the cone keeps a minimum width");
        const float behind[3]{0,0,-50};
        Check(!Score(o,d,behind,948,t,45,s),"object behind the hand is not chosen");
        const float distant[3]{0,0,700};
        Check(!Score(o,d,distant,632,t,45,s),"object beyond its own interaction reach is not chosen");
    }
    {
        Selector s;Candidate l[]={C(1,.6f),C(2,.2f),C(3,1.1f)};
        auto* t=s.Update(l,3,1000,false);
        Check(t&&t->owner==2,"object closest to the aim is chosen");
        Selector e;Candidate o[]={C(3,1.1f)};
        Check(!e.Update(o,1,1000,false),"nothing in the cone uses the plain aim line");
    }
    {
        Selector s;Candidate a[]={C(1,.5f)};s.Update(a,1,1000,false);s.NoteSelection(1,1010);
        Candidate b[]={C(1,1.2f),C(2,.8f)};
        auto* t=s.Update(b,2,1020,false);
        Check(t&&t->owner==1,"a selected object stays while aim drifts just past the cone");
        s.NoteSelection(1,1030);
        Candidate c[]={C(1,.8f),C(2,.3f)};
        t=s.Update(c,2,1040,false);
        Check(t&&t->owner==2,"pointing clearly at another object switches to it");
        s.NoteSelection(2,1050);
        Candidate d[]={C(2,1.3f),C(1,.9f)};
        t=s.Update(d,2,1060,false);
        Check(t&&t->owner==1,"aiming away from the object releases it");
    }
    {
        Selector s;Candidate a[]={C(1,.2f),C(2,.4f)};s.Update(a,2,1000,false);
        s.NoteSelection(0,1010);s.Update(a,2,1100,false);s.NoteSelection(0,1110);
        auto* t=s.Update(a,2,1160,false);
        Check(t&&t->owner==2&&s.Rejected()==1,"object the game refuses is skipped for the next one");
        s.NoteSelection(2,1170);
        Candidate only[]={C(1,.2f)};
        t=s.Update(only,1,1200,false);
        Check(!t,"a refused object stays out for a moment");
        t=s.Update(only,1,2650,false);
        Check(!t,"a refused object is still out just before its 1.5 s block ends");
        t=s.Update(only,1,2670,false);
        Check(t&&t->owner==1,"a refused object can return after its block");
    }
    {
        Selector s;Candidate a[]={C(1,.2f),C(2,.4f)};s.Update(a,2,1000,false);s.NoteSelection(1,1010);
        auto* t=s.Update(a,2,9000,false);
        Check(t&&t->owner==1&&s.Rejected()==0,"returning from a hacking screen is not a refusal");
    }
    {
        Selector s;Candidate a[]={C(1,.2f),C(2,.1f)};s.Update(a,2,1000,false);
        s.NoteSelection(1,1010);
        auto* t=s.Update(a,2,1020,false);
        Check(t&&t->owner==1,"the object the game actually selected is the one kept");
        t=s.Update(a,2,1100,true);t=s.Update(a,2,1190,true);
        Check(t&&t->owner==1&&s.Rejected()==0,"holding Interact keeps the target");
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
