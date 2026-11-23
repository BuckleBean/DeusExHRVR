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
static const float* Step(Selector& s,const Candidate* l,unsigned n,const Frame& f,uint64_t now,bool held) {
    s.BeginUpdate(now,false);return s.Update(l,n,f,now,held);
}
static Candidate T(uintptr_t owner,float hand,float x){Candidate c{};c.owner=owner;c.score=1e6f;c.handDistance=hand;c.point[0]=c.contactPoint[0]=x;c.point[2]=c.contactPoint[2]=300;return c;}
static const float* Go(Selector& s,const Candidate* l,unsigned n,const Frame& f,uint64_t now,bool press=false) {
    auto t=s.BeginUpdate(now,false);if(!t.search)s.BeginUpdate(now,false);return s.Update(l,n,f,now,press);
}
static void ContactChecks() {
    Frame f=Aim(0);f.contactReach=45;
    const float x20[3]{20,0,300},x10[3]{10,0,300};
    {Selector s;Candidate l[2]{T(1,40,10),T(2,12,20)};const auto* p=Go(s,l,2,f,1000);
     Check(At(p,x20)&&s.AimingContact(),"hand on a cluster picks the nearest item, whichever way it points");
     s.NoteSelection(2,p,1000);Frame away=Aim(5);away.contactReach=45;bool kept=true;
     for(uint64_t t=1020;t<2000;t+=20){const auto* q=Go(s,l,2,away,t);kept=kept&&s.Held()==2&&At(q,x20);s.NoteSelection(2,q,t);}
     Check(kept,"a touched item stays selected while the hand stays on it");}
    {Selector s;Candidate first=T(1,30,10);const auto* p=Go(s,&first,1,f,1000);s.NoteSelection(1,p,1000);Candidate a[2]{T(1,30,10),T(2,28,20)};
     p=Go(s,a,2,f,1020);Check(At(p,x10)&&s.Held()==1,"two items about equally close: the selection does not flicker");s.NoteSelection(1,p,1020);
     Candidate b[2]{T(1,30,10),T(2,15,20)};p=Go(s,b,2,f,1040);Check(At(p,x20),"clearly closer to another item: it takes over");}
    {Selector s;Candidate first=T(1,27,10);const auto* p=Go(s,&first,1,f,1000);Check(s.ContactOwner()==1,"the touched item is named for the game's check");s.NoteSelection(1,p,1000);
     Candidate a[2]{T(1,27,10),T(2,22,20)};p=Go(s,a,2,f,1020);Check(At(p,x20)&&s.ContactOwner()==2,"moving onto a neighbour 5 units closer takes over at once");}
    {Selector s;Candidate first=T(1,27,10);const auto* p=Go(s,&first,1,f,1000);s.NoteSelection(1,p,1000);
     Candidate a[2]{T(1,27,10),T(2,23,20)};p=Go(s,a,2,f,1020);Check(At(p,x10)&&s.Held()==1,"less than 1.5 cm closer: no flicker");}
    {Selector s;Candidate locker=C(9,.1f);Go(s,&locker,1,f,1000);s.NoteSelection(9,P,1000);Candidate l[2]{locker,T(2,10,20)};
     const auto* p=Go(s,l,2,f,1020);Check(At(p,x20),"an item at the hand takes over from the locker at once");}
    {Selector s;Candidate a=T(1,20,10);const auto* p=Go(s,&a,1,f,1000);s.NoteSelection(1,p,1000);Candidate l[2]{a,T(2,5,20)};
     p=Go(s,l,2,f,1020,true);Check(At(p,x10)&&s.Held()==1,"while Interact is held the selection never changes");}
    {Selector s;Candidate a=T(3,10,5);Check(Go(s,&a,1,f,1000)!=nullptr,"touched item is tried");
     Check(!Go(s,&a,1,f,1200),"if the game never accepts it, it is left out");Check(!Go(s,&a,1,f,1300),"and stays out for a moment");
     Check(Go(s,&a,1,f,2800)!=nullptr,"then it can be tried again");}
    {Selector s;Candidate a=T(3,10,5);const auto* p=Go(s,&a,1,f,1000);(void)p;s.NoteSelection(4,P,1000);
     Check(s.Held()==4,"the game's own choice is respected");const auto* q=Go(s,&a,1,f,1020);Check(At(q,P)&&s.Held()==4,"and the refused item does not keep fighting it");}
    {Frame off=Aim(0);Selector s;Candidate a=T(1,5,10);Check(!Go(s,&a,1,off,1000),"switched off, hand contact does nothing");}
    {Selector s;Candidate a=T(1,50,10);Check(!Go(s,&a,1,f,1000),"an item beyond the contact distance is not touched");}
    {Selector s;Candidate a=T(1,20,10);const auto* p=Go(s,&a,1,f,1000);s.NoteSelection(1,p,1000);Candidate b=T(1,60,10);p=Go(s,&b,1,f,1020);
     Check(At(p,x10)&&s.AimingContact(),"small hand movements off a touched item keep it");}
    {Selector s;Candidate a=T(1,20,10);const auto* p=Go(s,&a,1,f,1000);s.NoteSelection(1,p,1000);Candidate closest=T(2,8,20);
     Check(s.HasContact(&closest,1,45,1010),"the other hand on an item can take over");Check(!s.HasContact(&a,1,45,1010),"not when it is at the same item");}
    {const float hand[3]{0,0,0},target[3]{0,0,100};float o[3]{};Check(BackedOrigin(hand,target,45,o)&&std::abs(o[2]+45)<1e-4f,"the game's check starts behind the hand");
     Check(!BackedOrigin(hand,hand,45,o),"a hand exactly at the point falls back to the normal aim");}
}
int main() {
    ContactChecks();
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
        Check(Step(s,none,0,Aim(0),1000,false)==nullptr,"nothing selected starts on the plain line");
        s.NoteSelection(1,P,1000);
        Check(At(Step(s,none,0,Aim(0),1010,false),P),"a selected object is held on the point the game accepted");
        s.NoteSelection(1,P,1010);
        Check(At(Step(s,none,0,Aim(100),1020,false),P),"a small hand movement keeps the prompt");
        s.NoteSelection(1,P,1020);
        Check(At(Step(s,none,0,Aim(200),1030,false),P),"a larger wobble still keeps the prompt at first");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        uint64_t t=1010;
        for(;t<1250;t+=10){Step(s,none,0,Aim(200),t,false);s.NoteSelection(1,P,t);}
        const auto* back=Step(s,none,0,Aim(0),t,false);s.NoteSelection(1,P,t);
        Check(At(back,P)&&s.GetStats().wobblesHeld==1&&s.GetStats().released==0,"returning within the hold time never drops it");
        t+=10;
        for(uint64_t end=t+290;t<end;t+=10){Step(s,none,0,Aim(200),t,false);s.NoteSelection(1,P,t);}
        Check(s.Held()==1,"aim away for under 300 ms still holds");
        for(int i=0;i<3;i++,t+=10){Step(s,none,0,Aim(200),t,false);if(s.Held())s.NoteSelection(1,P,t);}
        Check(!s.Held()&&s.GetStats().released==1,"clearly aiming away for 300 ms releases it");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        Step(s,none,0,Aim(0),1050,false);
        Check(s.Held()==1,"one missed update does not drop the object");
        Step(s,none,0,Aim(0),1130,false);
        Check(!s.Held()&&s.GetStats().lost==1,"object the game stops offering (used or gone) is let go");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float* p{};
        for(uint64_t t=1010;t<1600;t+=10){p=Step(s,none,0,Aim(250),t,true);}
        Check(At(p,P)&&s.Held()==1,"holding Interact keeps the object regardless of aim or selection");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float q[3]{120,0,300};Candidate l[]={C(2,.05f,120)};const Frame f=Aim(120);
        const float* p=Step(s,l,1,f,1010,false);s.NoteSelection(1,P,1010);
        Check(At(p,P),"a neighbour does not steal the prompt immediately");
        p=Step(s,l,1,f,1060,false);s.NoteSelection(1,P,1060);
        Check(At(p,P),"a brief pass over a neighbour does not steal it");
        p=Step(s,l,1,f,1135,false);
        Check(At(p,q)&&s.GetStats().switches==1,"clearly pointing at another object moves to it");
        s.NoteSelection(2,q,1135);
        Check(s.Held()==2,"the game's new selection becomes the held object");
    }
    {
        Selector s;Candidate l[]={C(1,.6f,10),C(2,.2f,20)};const float two[3]{20,0,300};
        Check(Step(s,l,2,Aim(0),1000,false)==nullptr,"with nothing selected the plain line gets its turn");
        Check(At(Step(s,l,2,Aim(0),1010,false),two),"then the object closest to the aim is tried");
        Check(Step(s,l,2,Aim(0),1020,false)==nullptr,"plain line and near-miss attempts alternate");
        Step(s,l,2,Aim(0),1030,false);Step(s,l,2,Aim(0),1100,false);Step(s,l,2,Aim(0),1170,false);
        Check(s.GetStats().refused==1&&s.GetStats().lastRefused==2,"an object the game refuses is recorded and skipped");
        const float one[3]{10,0,300};const float* p=Step(s,l,2,Aim(0),1180,false);if(!p)p=Step(s,l,2,Aim(0),1190,false);
        Check(At(p,one),"the next closest object is tried");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        Step(s,none,0,Aim(0),9000,false);
        Check(!s.Held()&&s.GetStats().lost==0&&s.GetStats().refused==0,"returning from a hacking screen starts fresh without a refusal");
    }
    {
        Selector s;Step(s,none,0,Aim(0),1000,false);s.NoteSelection(1,P,1000);
        const float moved[3]{5,0,300};Step(s,none,0,Aim(0),1010,false);s.NoteSelection(1,moved,1010);
        Check(At(Step(s,none,0,Aim(0),1020,false),moved),"the held point follows what the game reports");
    }
    {
        Selector s;const Turn a=s.BeginUpdate(1000,true),b=s.BeginUpdate(1010,true),c=s.BeginUpdate(1020,true),d=s.BeginUpdate(1030,true);
        Check(a.hand==0&&!a.search&&b.hand==1&&!b.search&&c.hand==0&&c.search&&d.hand==1&&d.search,"both hands get the same plain-line and near-miss turns");
        Selector off;bool right=true;for(uint64_t t=1000;t<1080;t+=10)right=right&&off.BeginUpdate(t,false).hand==0;
        Check(right,"with the left hand switched off only the right hand gets turns");
    }
    {
        Selector s;s.BeginUpdate(1000,true);s.BeginUpdate(1010,true);s.NoteSelection(1,P,1010);
        Check(s.Held()==1&&s.HeldHand()==1,"a selection on the left hand's turn belongs to the left hand");
        const Turn t=s.BeginUpdate(1020,true);
        Check(t.hand==1&&t.search,"the left hand keeps every turn while it holds the prompt");
        Check(At(s.Update(none,0,Aim(100),1020,false),P),"a small left-hand movement keeps the prompt, as on the right");
        s.NoteSelection(1,P,1020);uint64_t now=1030;
        for(;now<=1330;now+=10){s.BeginUpdate(now,true);s.Update(none,0,Aim(200),now,false);if(s.Held())s.NoteSelection(1,P,now);}
        Check(!s.Held()&&s.GetStats().released==1,"aiming the left hand clearly away releases it, as on the right");
    }
    {
        Selector s;Candidate l[]={C(7,.2f,20)};const float seven[3]{20,0,300};
        s.BeginUpdate(1000,true);s.BeginUpdate(1010,true);s.BeginUpdate(1020,true);
        const Turn t=s.BeginUpdate(1030,true);
        Check(t.hand==1&&At(s.Update(l,1,Aim(0),1030,false),seven),"the left hand gets the same near-miss help");
    }
    {
        Selector s;s.BeginUpdate(1000,true);s.NoteSelection(1,P,1000);bool right=true;
        for(uint64_t t=1010;t<1060;t+=10){right=right&&s.BeginUpdate(t,true).hand==0;s.NoteSelection(1,P,t);}
        Check(right&&s.HeldHand()==0,"a right-hand prompt is not disturbed by the left hand");
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
