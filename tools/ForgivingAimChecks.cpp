// Offline checks for the one-path interaction choice. No game code runs.
#include <cstdio>
#include <cmath>
#include "ForgivingAim.h"
#include "InteractionTiming.h"
#include <cstring>
using namespace ForgivingAim;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static const float Tan15=.26795f;
// Aim from the origin toward (side,0,300); the object sits at (0,0,300).
static Frame Aim(float side,float reach=45) {
    Frame f{};f.dir[0]=side;f.dir[2]=300;Normalize(f.dir);f.tanCone=Tan15;f.minRadius=45;f.contactReach=reach;return f;
}
static const float P[3]{0,0,300};
static Candidate C(uintptr_t owner,float score,float x=0){Candidate c{};c.owner=owner;c.point[0]=c.contactPoint[0]=x;c.point[2]=c.contactPoint[2]=300;c.score=score;return c;}
static Candidate T(uintptr_t owner,float hand,float x){Candidate c=C(owner,1e6f,x);c.handDistance=hand;return c;}
static bool At(const float* p,const float* q){return p&&std::abs(p[0]-q[0])<1e-4f&&std::abs(p[1]-q[1])<1e-4f&&std::abs(p[2]-q[2])<1e-4f;}
static ViewInput View(bool eye=true){ViewInput v{};v.valid=true;v.eyeAllowed=eye;v.eye[1]=-100;v.forward[2]=1;return v;}
struct Rig {
    Selector s;HandInput hands[2]{};ViewInput view=View();
    void Right(const Frame& f,const Candidate* l=nullptr,unsigned n=0){hands[0]={true,f,l,n};}
    void Left(const Frame& f,const Candidate* l=nullptr,unsigned n=0){hands[1]={true,f,l,n};}
    Plan Go(uint64_t now,bool press=false){return s.Update(hands,view,now,press);}
    // One update in which the game accepts whatever was aimed at.
    Plan Accept(uint64_t now,bool press=false){Plan p=Go(now,press);if(p.aimed)s.Note(p.owner,p.point,now);else s.Note(0,nullptr,now);return p;}
};
static void Choosing() {
    {Rig r;Candidate l[2]{T(1,40,10),T(2,12,20)};r.Right(Aim(5),l,2);auto p=r.Go(1000);
     Check(p.aimed&&p.touch&&p.owner==2,"hand on a cluster picks the nearest item, whichever way it points");
     Check(p.fromEye,"the game is asked from the eye first");
     r.s.Note(0,nullptr,1000);p=r.Go(1010);Check(p.owner==2&&!p.fromEye,"then from the hand");}
    {Rig r;r.view.eyeAllowed=false;Candidate a=T(1,10,5);r.Right(Aim(0),&a,1);Check(!r.Go(1000).fromEye,"InteractionFromEye=0 asks only from the hand");}
    {Rig r;Candidate a=T(3,10,5);r.Right(Aim(0),&a,1);bool again=false,plain=false;
     for(uint64_t t=1000;t<1200;t+=10){auto p=r.Go(t);r.s.Note(0,nullptr,t);if(t>1040&&p.aimed&&p.owner==3)again=true;if(!p.aimed)plain=true;}
     Check(again,"an item the game declines is tried again while the hand stays on it");
     Check(plain,"and the plain line still gets its turn");}
    {Rig r;Candidate l[2]{T(1,10,5),T(2,20,20)};r.Right(Aim(0),l,2);bool second=false;
     for(uint64_t t=1000;t<1060;t+=10){auto p=r.Go(t);r.s.Note(0,nullptr,t);if(p.owner==2)second=true;}
     Check(second,"if the nearest item is declined the next one is tried");}
    {Rig r;Candidate first=T(1,30,10);r.Right(Aim(0),&first,1);r.Accept(1000);Candidate a[2]{T(1,30,10),T(2,28,20)};r.Right(Aim(0),a,2);
     auto p=r.Accept(1020);Check(p.owner==1&&r.s.Held()==1,"two items about equally close: no flicker");
     Candidate b[2]{T(1,30,10),T(2,15,20)};r.Right(Aim(0),b,2);p=r.Accept(1040);Check(p.owner==2&&r.s.Held()==2,"clearly closer to another item: it takes over");}
    {Rig r;Candidate first=T(1,27,10);r.Right(Aim(0),&first,1);r.Accept(1000);Candidate a[2]{T(1,27,10),T(2,22,20)};r.Right(Aim(0),a,2);
     Check(r.Go(1020).owner==2,"moving onto a neighbour 1.5 cm closer takes over");}
    {Rig r;Candidate locker=C(9,.1f);r.Right(Aim(0),&locker,1);r.Accept(1000);Check(r.s.Held()==9,"the locker can be selected by pointing");
     Candidate l[2]{locker,T(2,10,20)};r.Right(Aim(0),l,2);Check(r.Go(1020).owner==2,"an item at the hand takes over from the locker at once");}
    {Rig r;Candidate a=T(1,20,10);r.Right(Aim(0),&a,1);r.Accept(1000);Candidate l[2]{a,T(2,5,20)};r.Right(Aim(0),l,2);
     auto p=r.Go(1020,true);Check(p.owner==1,"while Interact is held the selection never changes");}
    {Rig r;Candidate a=T(1,20,10);r.Right(Aim(0),&a,1);r.Accept(1000);Candidate b=T(1,60,10);r.Right(Aim(0),&b,1);
     auto p=r.Accept(1020);Check(p.owner==1&&p.touch,"small hand movements off a touched item keep it");}
    {Rig r;Candidate a=T(1,5,10);r.Right(Aim(0,0),&a,1);Check(!r.Go(1000).aimed,"hand contact switched off does nothing");}
    {Rig r;Candidate a=T(1,50,10);r.Right(Aim(0),&a,1);Check(!r.Go(1000).aimed,"an item beyond the contact distance is not touched");}
    // Both hands, same rules.
    {Rig r;Candidate a=T(1,20,10),b=T(2,8,20);r.Right(Aim(0),&a,1);r.Left(Aim(0),&b,1);auto p=r.Go(1000);
     Check(p.owner==2&&p.hand==1,"the hand closer to its item wins, either hand");}
    {Rig r;Candidate a=T(1,20,10);r.Right(Aim(0),&a,1);r.Accept(1000);Candidate b=T(2,8,20);r.Left(Aim(0),&b,1);auto p=r.Go(1020);
     Check(p.owner==2&&p.hand==1,"the other hand on an item can take over");}
    {Rig r;Candidate a=T(1,20,10),b=T(1,8,10);r.Right(Aim(0),&a,1);r.Left(Aim(0),&b,1);auto p=r.Accept(1000);
     Check(p.hand==1&&r.s.HeldHand()==1,"one item at both hands belongs to the closer hand");}
    {Rig r;Candidate behind=T(1,8,0);behind.contactPoint[2]=-500;r.Right(Aim(0),&behind,1);
     Check(!r.Go(1000).aimed,"an item behind the view cannot take the selection");}
    {Rig r;Frame down{};down.dir[1]=1;down.tanCone=Tan15;down.minRadius=45;r.Right(Aim(0));r.Left(down);bool left=false;
     for(uint64_t t=1000;t<1100;t+=10)left=left||r.Go(t).hand==1;
     Check(!left,"a hand hanging out of view does not get plain-line turns");}
    {Rig r;r.Right(Aim(0));r.Left(Aim(0));bool a=false,b=false;
     for(uint64_t t=1000;t<1040;t+=10){auto p=r.Go(t);(p.hand?b:a)=true;}
     Check(a&&b,"two raised hands share the plain line");}
    // A declined item does not fight the one holding the prompt.
    {Rig r;Candidate a=C(4,.1f);r.Right(Aim(0),&a,1);r.Accept(1000);Candidate l[2]{a,T(3,10,5)};r.Right(Aim(0),l,2);
     auto p=r.Go(1010);r.s.Note(4,P,1010);Check(p.owner==3,"a touched item is tried over the held one");
     p=r.Go(1020);r.s.Note(4,P,1020);Check(p.owner==3&&!p.fromEye,"from both places");
     p=r.Go(1030);r.s.Note(4,P,1030);Check(p.owner==4&&r.s.Held()==4,"declined twice: the prompt stays steady");
     Frame moved=Aim(0);moved.origin[0]=20;r.Right(moved,l,2);p=r.Go(1040);Check(p.owner==3,"once the hand moves it is tried again");}
}
static void Holding() {
    {Rig r;r.Right(Aim(0));Check(!r.Go(1000).aimed,"nothing selected uses the plain line");r.s.Note(1,P,1000);
     auto p=r.Go(1010);Check(p.aimed&&At(p.point,P),"a selected object is held on the point the game accepted");r.s.Note(1,P,1010);
     r.Right(Aim(100));p=r.Go(1020);Check(At(p.point,P),"a small hand movement keeps the prompt");r.s.Note(1,P,1020);
     r.Right(Aim(200));p=r.Go(1030);Check(At(p.point,P),"a larger wobble still keeps the prompt at first");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);uint64_t t=1010;r.Right(Aim(200));
     for(;t<1250;t+=10){r.Go(t);r.s.Note(1,P,t);}
     r.Right(Aim(0));r.Go(t);r.s.Note(1,P,t);
     Check(r.s.GetStats().wobblesHeld==1&&r.s.GetStats().released==0,"returning within the hold time never drops it");
     t+=10;r.Right(Aim(200));for(uint64_t end=t+290;t<end;t+=10){r.Go(t);r.s.Note(1,P,t);}
     Check(r.s.Held()==1,"aim away for under 300 ms still holds");
     for(int i=0;i<3;i++,t+=10){r.Go(t);if(r.s.Held())r.s.Note(1,P,t);}
     Check(!r.s.Held()&&r.s.GetStats().released==1,"clearly aiming away for 300 ms releases it");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);auto p=r.Go(1050);r.s.Note(0,nullptr,1050);Check(r.s.Held()==1,"one missed update does not drop the object");
     bool first=p.fromEye;p=r.Go(1060);Check(p.fromEye!=first,"a held item the game misses is asked from the other place");r.s.Note(0,nullptr,1060);
     r.Go(1250);Check(!r.s.Held()&&r.s.GetStats().lost==1,"an object the game stops offering is let go");
     Candidate a=C(1,.1f);r.Right(Aim(0),&a,1);Check(r.Go(1260).owner==1,"and it can be chosen again at once");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);r.Right(Aim(250));Plan p{};
     for(uint64_t t=1010;t<1600;t+=10)p=r.Go(t,true);
     Check(At(p.point,P)&&r.s.Held()==1,"holding Interact keeps the object regardless of aim or selection");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);const float q[3]{120,0,300};Candidate l[]={C(2,.05f,120)};r.Right(Aim(120),l,1);
     auto p=r.Go(1010);r.s.Note(1,P,1010);Check(At(p.point,P),"a neighbour does not steal the prompt immediately");
     p=r.Go(1060);r.s.Note(1,P,1060);Check(At(p.point,P),"a brief pass over a neighbour does not steal it");
     p=r.Go(1135);Check(At(p.point,q)&&r.s.GetStats().switches==1,"clearly pointing at another object moves to it");
     r.s.Note(2,q,1135);Check(r.s.Held()==2,"the game's new selection becomes the held object");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);r.Go(9000);
     Check(!r.s.Held()&&r.s.GetStats().lost==0,"returning from a hacking screen starts fresh");}
    {Rig r;r.Right(Aim(0));r.Go(1000);r.s.Note(1,P,1000);const float moved[3]{5,0,300};r.Go(1010);r.s.Note(1,moved,1010);
     Check(At(r.Go(1020).point,moved),"the held point follows what the game reports");}
    {Rig r;r.Right(Aim(0));r.Left(Aim(0));Plan p{};do{p=r.Go(1000);}while(p.hand!=1);r.s.Note(1,P,1000);
     Check(r.s.Held()==1&&r.s.HeldHand()==1,"a selection from the left hand's line belongs to the left hand");
     r.Left(Aim(100));p=r.Go(1020);Check(p.hand==1&&At(p.point,P),"a small left-hand movement keeps the prompt, as on the right");r.s.Note(1,P,1020);
     r.Left(Aim(200));uint64_t now=1030;for(;now<=1340;now+=10){r.Go(now);if(r.s.Held())r.s.Note(1,P,now);}
     Check(!r.s.Held()&&r.s.GetStats().released==1,"aiming the left hand clearly away releases it, as on the right");}
}
static void RegressionChecks() {
    {Rig r;Candidate c[3]{C(1,.1f),C(2,.11f),C(3,.3f)};r.Right(Aim(0),c,3);
     unsigned third=0,hand=0,plain=0;
     for(unsigned i=0;i<100;i++) {
         c[0].score=(i&1)?.11f:.1f;c[1].score=(i&1)?.1f:.11f;
         const auto p=r.Go(1000+i*10);third+=p.owner==3;hand+=p.aimed&&!p.fromEye;plain+=!p.aimed;
         r.s.Note(0,nullptr,1000+i*10);
     }
     Check(third>0&&hand>0&&plain>0,"changing candidate ranks cannot starve other items, hand checks or plain rays");}
    {Rig r;Candidate c[2]{C(1,.1f),C(2,.2f)};r.Right(Aim(0),c,2);r.Go(1000);r.s.Note(0,nullptr,1000);
     r.Right(Aim(0),c+1,1);auto p=r.Go(1010);
     Check(p.owner==2,"a removed candidate is skipped within the current cycle");
     r.s.Note(0,nullptr,1010);r.hands[0].valid=false;r.Left(Aim(0),c+1,1);p=r.Go(1020);
     Check(p.owner==2&&p.hand==1,"cycle identities use the currently tracked hand, not a stale plan");}
    {Rig r;Candidate c[2]{C(1,.1f),C(2,.2f)};r.Right(Aim(0),c,1);r.Go(1000);r.s.Note(0,nullptr,1000);
     r.Right(Aim(0),c,2);bool added=false;
     for(unsigned i=1;i<20;i++){auto p=r.Go(1000+i*10);added|=p.owner==2;r.s.Note(0,nullptr,1000+i*10);}
     Check(added,"a new candidate joins the next bounded cycle");}
    {Candidate out[MaxCandidates]{};unsigned n=0;
     for(unsigned i=0;i<MaxCandidates;i++)MergeCandidate(out,n,C(i+1,.8f),45);
     auto better=T(1,10,25);better.score=.05f;MergeCandidate(out,n,better,45);
     Check(n==MaxCandidates&&out[0].handDistance==10&&out[0].score==.05f&&out[0].point[0]==25,
           "a full buffer still refines an existing owner's contact and pointing score");
     MergeCandidate(out,n,T(100,5,15),45);bool inserted=false,kept=false;
     for(unsigned i=0;i<n;i++){inserted|=out[i].owner==100;kept|=out[i].owner==1;}
     Check(inserted&&kept&&n==MaxCandidates,"a new close contact displaces a worse candidate without dropping the better contact");
     MergeCandidate(out,n,C(200,.99f),45);bool poor=false;
     for(unsigned i=0;i<n;i++)poor|=out[i].owner==200;
     Check(!poor,"a worse candidate cannot displace better entries in a full buffer");}
    {Candidate out[MaxCandidates]{};unsigned n=0;
     // One long object overlaps every shell, but its centre is off the aim line.
     // Only sufficiently small shells survive the production contact-distance filter.
     for(float share:ContactShells){const float d=ContactDistance(45*share,120);
         if(d<=45*ContactKeep)MergeCandidate(out,n,T(1,d,0),45);}
     Check(n==1&&std::abs(out[0].handDistance-27)<.001f,"nested contact searches retain the closest surface measurement");}
}
static void TimingChecks() {
    InteractionTiming::Second s;
    InteractionTiming::AddUpdate(s,10,100,2,500,false);
    InteractionTiming::AddUpdate(s,30,300,4,700,true);
    Check(s.updates==2&&s.carrying==1,"timing counts updates and updates while carrying");
    Check(s.beginTotal==40&&s.beginMax==30&&s.gameTotal==400&&s.gameMax==300&&s.endMax==4&&s.gapMax==700,"timing keeps separate totals and worst cases");
    InteractionTiming::AddQuery(s,-1);InteractionTiming::AddQuery(s,5);InteractionTiming::AddQuery(s,32);
    Check(s.queries==3&&s.hitsTotal==37&&s.hitsMax==32,"timing counts searches and physics hits");
    char line[260]{};
    const int n=InteractionTiming::Format(s,1.,line,sizeof(line));
    Check(n>0&&n<int(sizeof(line))&&std::strstr(line,"beginAvgUs=20")&&std::strstr(line,"gameMaxUs=300")&&std::strstr(line,"gapMaxMs=0.7")&&std::strstr(line,"hitsMax=32"),"timing line reports microseconds and milliseconds");
}
static void TouchAndSearchChecks() {
    TimingChecks();
    {Rig r;Candidate l[6]{C(1,.1f),C(2,.2f),C(3,.3f),C(4,.4f),C(5,.5f),C(6,.6f)};r.Right(Aim(0),l,6);
     uint64_t t=1000;for(int i=0;i<5;i++,t+=10){r.Go(t);r.s.Note(0,nullptr,t);}
     Candidate m[6]{T(7,10,5),C(1,.1f),C(2,.2f),C(3,.3f),C(4,.4f),C(5,.5f)};r.Right(Aim(0),m,6);
     auto p=r.Go(t);Check(p.aimed&&p.touch&&p.owner==7,"a newly touched item is tried on the very next update");
     Check(p.fromEye,"and starts with the check from the eye");}
    {Rig r;Candidate l[6]{C(1,.1f),C(2,.2f),C(3,.3f),C(4,.4f),C(5,.5f),C(6,.6f)};r.Right(Aim(0),l,6);
     uint64_t t=1000;for(int i=0;i<3;i++,t+=10){r.Go(t);r.s.Note(0,nullptr,t);}
     Candidate m[6]{T(6,10,5),C(1,.1f),C(2,.2f),C(3,.3f),C(4,.4f),C(5,.5f)};r.Right(Aim(0),m,6);
     auto p=r.Go(t);Check(p.touch&&p.owner==6,"a pointed item the hand comes onto moves to the front");}
    {Rig r;Candidate l[1]{C(1,.1f)};r.Right(Aim(0),l,1);
     uint64_t t=1000;for(int i=0;i<2;i++,t+=10){r.Go(t);r.s.Note(0,nullptr,t);}
     Candidate m[1]{T(1,10,5)};r.Right(Aim(0),m,1);auto p=r.Go(t);
     Check(p.touch&&p.owner==1&&p.fromEye,"an item already declined by pointing gets fresh tries once touched");}
    {Rig r;unsigned third=0,plain=0;uint64_t t=1000;
     for(int i=0;i<200;i++,t+=10) {
         Candidate a[2]{T(10+unsigned(i%8),10,5),C(3,.3f)};r.Right(Aim(0),a,2);
         auto p=r.Go(t);third+=p.aimed&&p.owner==3;plain+=!p.aimed;r.s.Note(0,nullptr,t);
     }
     Check(third>0&&plain>0,"items touched in turn cannot keep the others or the plain line from their turn");}
    {uintptr_t ids[MaxThrough+1]{99};unsigned casts=0,passed=0;
     bool ok=ReachThrough(5,7,ids,1,MaxThrough+1,[&](uintptr_t*,unsigned,uintptr_t& next){++casts;next=5;return true;},&passed);
     Check(ok&&casts==1&&passed==1&&ids[1]==7,"a neighbour in the line is ignored and the touched item is reached");}
    {uintptr_t ids[MaxThrough+1]{99};unsigned casts=0;
     bool ok=ReachThrough(5,7,ids,1,MaxThrough+1,[&](uintptr_t*,unsigned,uintptr_t& next){++casts;next=0;return true;});
     Check(!ok&&casts==1,"a wall behind the neighbour still stops the line");}
    {uintptr_t ids[MaxThrough+1]{99};unsigned casts=0;
     bool ok=ReachThrough(5,7,ids,1,MaxThrough+1,[&](uintptr_t*,unsigned,uintptr_t& next){++casts;next=7;return true;});
     Check(!ok&&casts==1,"if the game cannot ignore the neighbour the normal result stands");}
    {uintptr_t ids[MaxThrough+1]{99};uintptr_t n=20;unsigned casts=0;
     bool ok=ReachThrough(5,7,ids,1,MaxThrough+1,[&](uintptr_t*,unsigned,uintptr_t& next){++casts;next=n++;return true;});
     Check(!ok&&casts==MaxThrough,"a line through many items gives up after a fixed number of tries");}
    {uintptr_t ids[MaxThrough+1]{99};
     bool ok=ReachThrough(5,7,ids,1,MaxThrough+1,[&](uintptr_t*,unsigned,uintptr_t&){return false;});
     Check(!ok,"a failed check leaves the normal result");}
    {unsigned calls=0;bool ok=SearchShells([&](float,bool& more){++calls;more=false;return true;});
     Check(ok&&calls==1,"nothing usable in the largest sphere: the smaller searches are skipped");}
    {unsigned calls=0;float first=0;bool ok=SearchShells([&](float s,bool& more){if(!calls)first=s;++calls;more=true;return true;});
     Check(ok&&calls==4&&first==ContactShells[3],"something usable in the largest sphere: all smaller searches run");}
    {unsigned calls=0;bool ok=SearchShells([&](float,bool& more){++calls;more=calls==1;return calls<2;});
     Check(!ok&&calls==2,"a failed search stops and reports the failure");}
}
int main() {
    RegressionChecks();
    TouchAndSearchChecks();
    Check(std::abs(ContactDistance(9,120)-27)<1e-3f,"hand on the far end of a long item counts as touching it");
    Check(ContactDistance(9,6)==6,"a small item is never farther than its own position");
    Check(ContactDistance(9,20)<ContactDistance(9,240),"an item in a locker beats the locker when the hand is at both");
    Check(ContactDistance(22.5f,40)<ContactDistance(9,240),"an item a few inches away still beats a locker wall under the hand");
    {const float hand[3]{0,0,0},target[3]{0,0,100};float o[3]{};Check(BackedOrigin(hand,target,45,o)&&std::abs(o[2]+45)<1e-4f,"the hand's check starts behind the hand");
     Check(!BackedOrigin(hand,hand,45,o),"a hand exactly at the point falls back to the normal aim");}
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
    Choosing();
    Holding();
    {
        CameraMath::Matrix hint{};hint.m[0]=1;hint.m[5]=1;hint.m[10]=1;hint.m[15]=1;
        const float o[3]{10,20,30},p[3]{13,24,30};CameraMath::Matrix r{};
        bool ok=RayMatrix(hint,o,p,r);
        Check(ok&&std::abs(r.m[8]-.6f)<1e-5f&&std::abs(r.m[9]-.8f)<1e-5f&&r.m[12]==10&&r.m[13]==20,"ray runs from the origin to the target");
        float c[3];Cross(r.m,r.m+4,c);
        Check(ok&&std::abs(Dot(r.m,r.m)-1)<1e-5f&&std::abs(Dot(r.m,r.m+8))<1e-5f&&Dot(c,r.m+8)>0,"ray matrix is rigid with the same handedness");
        ok=RayMatrix(hint,o,o,r);
        Check(ok&&std::abs(r.m[10]-1)<1e-5f,"target at the origin falls back to the aim direction");
    }
    return failures?1:0;
}
