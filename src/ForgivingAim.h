#pragma once
#include <cmath>
#include <cstdint>
#include "CameraMath.h"

// Interaction for both hands, as one path (November 22): choose one target, ask the
// game about it from the eye and then the hand, and hold the prompt through small
// movements. The game still applies its range and rules. Owner values are compared
// only; this file never reads game memory.
namespace ForgivingAim {
inline constexpr float KeepScore=1.25f;      // candidates are listed up to here
inline constexpr float ReleaseScore=1.5f;    // aim must leave this far to start releasing
inline constexpr uint64_t HoldMs=300;        // and stay out this long
inline constexpr uint64_t LostMs=200;        // held, but the game stopped selecting it
inline constexpr float SwitchRatio=.5f;      // another object must be twice as close to the aim
inline constexpr uint64_t SwitchMs=120;      // for this long
inline constexpr unsigned MaxTries=2;        // one check from the eye, one from the hand
inline constexpr unsigned MaxQueue=6;        // items in line at once
inline constexpr float ViewCos=.342f;        // within 70 degrees of where the head faces
inline constexpr float SettleShare=.2f;      // of MinRadius: 3 cm of hand or head movement
inline constexpr uint64_t GapMs=200;         // no updates this long (a screen): start fresh
inline constexpr float MinRadiusMetres=.15f; // the cone never gets narrower than this up close
inline constexpr float SearchUnits=948.15f;  // longest interaction reach seen in this game
// Hand contact (November 22): a usable object at the hand wins, whichever way the hand points.
// Measured to the object's own position, so a small item in a locker beats the large locker.
inline constexpr float NoContact=1e30f;
inline constexpr float ContactKeep=1.4f;     // a touched object stays until the hand is this much further
inline constexpr float ContactSwitchMargin=.1f; // another touched object takes over once closer by this share of the reach (1.5 cm at 15 cm)
// Touching is measured by the game's own collision search: small spheres at the hand,
// as shares of the contact distance, smallest first. An object found in a sphere has its
// shape within that radius of the hand, wherever on the object the hand is.
// Closest first; a large shell's filtered result cannot rule out a smaller shell.
inline constexpr float ContactShells[4]={.2f,.5f,1.f,1.4f};
// Part of the distance to the object's own position still counts, so an item in a locker
// beats the large locker around it when the hand is at both.
inline constexpr float ContactPositionWeight=.15f;
inline float ContactDistance(float shell,float position) {
    const float surface=shell+position*ContactPositionWeight;
    return surface<position?surface:position;
}
inline constexpr float ContactBackMetres=.15f; // the game's check starts behind the hand, never inside the object

struct Candidate {uintptr_t owner{};float point[3]{};float score{};float handDistance=NoContact;float contactPoint[3]{};};
inline constexpr unsigned MaxCandidates=32;
// Keep the best contacts before pointed candidates. Capacity limits insertion,
// never refinement of an owner already found by a different shell or cone.
inline bool BetterCandidate(const Candidate& a,const Candidate& b,float contactReach) {
    const bool at=contactReach>0&&a.handDistance<=contactReach*ContactKeep;
    const bool bt=contactReach>0&&b.handDistance<=contactReach*ContactKeep;
    if(at!=bt)return at;
    return at?a.handDistance<b.handDistance:a.score<b.score;
}
inline void MergeCandidate(Candidate* out,unsigned& count,const Candidate& c,float contactReach) {
    for(unsigned i=0;i<count;i++)if(out[i].owner==c.owner) {
        if(c.handDistance<out[i].handDistance) {
            out[i].handDistance=c.handDistance;
            for(int j=0;j<3;j++)out[i].contactPoint[j]=c.contactPoint[j];
        }
        if(c.score<out[i].score){out[i].score=c.score;for(int j=0;j<3;j++)out[i].point[j]=c.point[j];}
        return;
    }
    if(count<MaxCandidates){out[count++]=c;return;}
    unsigned worst=0;
    for(unsigned i=1;i<count;i++)if(BetterCandidate(out[worst],out[i],contactReach))worst=i;
    if(BetterCandidate(c,out[worst],contactReach))out[worst]=c;
}
struct Frame {float origin[3]{};float dir[3]{};float tanCone{};float minRadius{};float contactReach{};};
struct Stats {unsigned wobblesHeld{},released{},lost{},switches{};};

inline float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline void Cross(const float* a,const float* b,float* out) {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
inline bool Normalize(float* v) {
    const float n=std::sqrt(Dot(v,v));
    if(!std::isfinite(n)||n<1e-5f)return false;
    v[0]/=n;v[1]/=n;v[2]/=n;return true;
}
// 0 = on the line, 1 = at the edge of the cone. Behind the hand counts as far off.
inline float OffAxis(const float* origin,const float* dir,const float* point,float tanCone,float minRadius) {
    const float v[3]{point[0]-origin[0],point[1]-origin[1],point[2]-origin[2]};
    const float along=Dot(v,dir);
    if(!std::isfinite(along)||along<=0)return 1e6f;
    const float off[3]{v[0]-dir[0]*along,v[1]-dir[1]*along,v[2]-dir[2]*along};
    const float wide=tanCone*along;
    const float allowed=wide>minRadius?wide:minRadius;
    const float score=std::sqrt(Dot(off,off))/allowed;
    return std::isfinite(score)?score:1e6f;
}
inline bool Score(const float* origin,const float* dir,const float* point,float reach,float tanCone,float minRadius,float& score) {
    const float v[3]{point[0]-origin[0],point[1]-origin[1],point[2]-origin[2]};
    const float distance=std::sqrt(Dot(v,v));
    if(!std::isfinite(distance)||distance>reach)return false;
    score=OffAxis(origin,dir,point,tanCone,minRadius);
    return score<=KeepScore;
}

// One path (November 22). Every update looks at both hands and makes one decision:
// the item a hand is on, else the item a hand points at, else that hand's plain line.
// The game is asked about the choice from the eye, then from the hand. Nothing is
// banned for a time: an item the game declines is passed over for the next one in
// line and comes round again while the hand stays there.
struct HandInput {bool valid{};Frame frame{};const Candidate* list{};unsigned count{};};
struct ViewInput {bool valid{};bool eyeAllowed{};float eye[3]{};float forward[3]{};};
// aimed=false: use this hand's plain line.
struct Plan {int hand{};uintptr_t owner{};float point[3]{};bool aimed{},touch{},fromEye{};};

class Selector {
    struct Entry {uintptr_t owner{};int hand{};float point[3]{};float rank{};bool touch{};};
    struct Settle {uintptr_t owner{};float where[3]{};float eye[3]{};};
    uintptr_t held{};int heldHand{};float heldPoint[3]{};uint64_t lastSelected{},outsideSince{};
    bool heldTouch{},heldFromEye{true},heldMissed{};
    uintptr_t switchOwner{};uint64_t switchSince{};
    uintptr_t focusOwner{},cycleOwners[MaxQueue]{};unsigned focusTries{},cycleCount{},cursor{},plainTurns{};int plainHand{1};
    Settle settled[4]{};unsigned nextSettle{};
    float planWhere[3]{},planEye[3]{};
    uint64_t lastUpdate{};Plan plan{};Stats stats{};

    static bool Before(const Entry& a,const Entry& b){return a.touch!=b.touch?a.touch:a.rank<b.rank;}
    static void Insert(Entry* q,unsigned& n,const Entry& e) {
        for(unsigned i=0;i<n;i++)if(q[i].owner==e.owner) {
            if(!Before(e,q[i]))return;
            for(unsigned j=i;j+1<n;j++)q[j]=q[j+1];
            --n;break;
        }
        unsigned at=n;
        while(at>0&&Before(e,q[at-1]))--at;
        if(at>=MaxQueue)return;
        for(unsigned j=n<MaxQueue?n:MaxQueue-1;j>at;j--)q[j]=q[j-1];
        q[at]=e;
        if(n<MaxQueue)++n;
    }
    static bool InView(const ViewInput& view,const float* p) {
        if(!view.valid)return true;
        float v[3]{p[0]-view.eye[0],p[1]-view.eye[1],p[2]-view.eye[2]};
        return !Normalize(v)||Dot(v,view.forward)>=ViewCos;
    }
    static float Apart(const float* a,const float* b) {
        const float v[3]{a[0]-b[0],a[1]-b[1],a[2]-b[2]};
        return std::sqrt(Dot(v,v));
    }
    unsigned Build(const HandInput* hands,const ViewInput& view,Entry* q) const {
        unsigned n=0;
        for(int h=0;h<2;h++) {
            if(!hands[h].valid)continue;
            const float reach=hands[h].frame.contactReach;
            for(unsigned i=0;i<hands[h].count;i++) {
                const auto& c=hands[h].list[i];
                if(!c.owner)continue;
                const float limit=reach*(c.owner==held?ContactKeep:1.f);
                const bool touch=reach>0&&std::isfinite(c.handDistance)&&c.handDistance<=limit;
                if(!touch&&!(std::isfinite(c.score)&&c.score<=1.f))continue;
                const float* p=touch?c.contactPoint:c.point;
                if(!InView(view,p))continue;
                Entry e{};e.owner=c.owner;e.hand=h;e.touch=touch;
                e.point[0]=p[0];e.point[1]=p[1];e.point[2]=p[2];
                e.rank=touch?c.handDistance-(c.owner==held?reach*ContactSwitchMargin:0.f):c.score;
                Insert(q,n,e);
            }
        }
        return n;
    }
    bool HeldTouching(const HandInput* hands,int& hand) const {
        float best=NoContact;bool found=false;
        for(int h=0;h<2;h++) {
            if(!hands[h].valid||!(hands[h].frame.contactReach>0))continue;
            for(unsigned i=0;i<hands[h].count;i++) {
                const auto& c=hands[h].list[i];
                if(c.owner==held&&c.handDistance<=hands[h].frame.contactReach*ContactKeep&&c.handDistance<best){best=c.handDistance;hand=h;found=true;}
            }
        }
        return found;
    }
    // Declined while something else holds the prompt: leave it until the hand or head moves.
    bool Settled(uintptr_t owner,int hand,const HandInput* hands,const ViewInput& view) {
        for(auto& s:settled) {
            if(s.owner!=owner)continue;
            const float radius=hands[hand].frame.minRadius*SettleShare;
            if(Apart(hands[hand].frame.origin,s.where)>radius||(view.valid&&Apart(view.eye,s.eye)>radius)){s.owner=0;return false;}
            return true;
        }
        return false;
    }
    void AddSettle(uintptr_t owner) {
        for(auto& s:settled)if(s.owner==owner)s.owner=0;
        auto& s=settled[nextSettle];nextSettle=(nextSettle+1)%4;
        s.owner=owner;
        for(int i=0;i<3;i++){s.where[i]=planWhere[i];s.eye[i]=planEye[i];}
    }
    void Remember(int hand,const HandInput* hands,const ViewInput& view) {
        for(int i=0;i<3;i++){planWhere[i]=hands[hand].frame.origin[i];planEye[i]=view.eye[i];}
    }
    const Plan& Attempt(const Entry& e,const HandInput* hands,const ViewInput& view) {
        if(focusOwner!=e.owner){focusOwner=e.owner;focusTries=0;}
        plan={};plan.hand=e.hand;plan.owner=e.owner;plan.aimed=true;plan.touch=e.touch;
        plan.point[0]=e.point[0];plan.point[1]=e.point[1];plan.point[2]=e.point[2];
        plan.fromEye=view.valid&&view.eyeAllowed&&focusTries%2==0;
        ++focusTries;
        Remember(e.hand,hands,view);
        return plan;
    }
    const Plan& AimHeld(const HandInput* hands,const ViewInput& view) {
        if(!hands[heldHand].valid)heldHand=1-heldHand;
        if(heldMissed){heldFromEye=!heldFromEye;heldMissed=false;}
        plan={};plan.hand=heldHand;plan.owner=held;plan.aimed=true;plan.touch=heldTouch;
        plan.point[0]=heldPoint[0];plan.point[1]=heldPoint[1];plan.point[2]=heldPoint[2];
        plan.fromEye=view.valid&&view.eyeAllowed&&heldFromEye;
        Remember(heldHand,hands,view);
        return plan;
    }
    const Plan& Plain(const HandInput* hands,const ViewInput& view) {
        int options[2]{},count=0;
        for(int h=0;h<2;h++) {
            if(!hands[h].valid)continue;
            if(!view.valid||Dot(hands[h].frame.dir,view.forward)>=ViewCos)options[count++]=h;
        }
        if(!count)options[count++]=hands[0].valid?0:1; // neither hand is in view
        plainHand=count==2?1-plainHand:options[0];
        plan={};plan.hand=plainHand;
        return plan;
    }
    const Plan& Seek(const Entry* q,unsigned n,const HandInput* hands,const ViewInput& view) {
        if(!n){cycleCount=cursor=plainTurns=focusTries=0;focusOwner=0;return Plain(hands,view);}
        // Snapshot identities, not poses/ranks. Reordering cannot restart the cycle;
        // every attempt still uses the current eligible candidate and tracked hand.
        if(!cycleCount){cycleCount=n;for(unsigned i=0;i<n;i++)cycleOwners[i]=q[i].owner;}
        while(cursor<cycleCount) {
            const Entry* e=nullptr;
            for(unsigned i=0;i<n;i++)if(q[i].owner==cycleOwners[cursor]){e=&q[i];break;}
            if(!e||(e->owner==focusOwner&&focusTries>=MaxTries)){++cursor;focusOwner=0;focusTries=0;continue;}
            return Attempt(*e,hands,view);
        }
        // Everything in line had its tries: give the plain lines a turn, then start over.
        const unsigned lines=(hands[0].valid?1u:0u)+(hands[1].valid?1u:0u);
        if(plainTurns<lines){++plainTurns;return Plain(hands,view);}
        cycleCount=plainTurns=cursor=focusTries=0;focusOwner=0;
        return Seek(q,n,hands,view);
    }
    void DropHeld(){held=0;heldHand=0;outsideSince=0;switchOwner=0;switchSince=0;heldTouch=false;heldMissed=false;heldFromEye=true;}
public:
    void Reset() {
        DropHeld();plan={};lastSelected=0;
        focusOwner=0;focusTries=cycleCount=cursor=plainTurns=0;
        for(auto& s:settled)s.owner=0;
    }
    void ClearPlan(){plan={};}
    const Plan& LastPlan() const {return plan;}
    uintptr_t Held() const {return held;}
    int HeldHand() const {return heldHand;}
    const Stats& GetStats() const {return stats;}
    const Plan& Update(const HandInput* hands,const ViewInput& view,uint64_t now,bool interactHeld) {
        if(lastUpdate&&now-lastUpdate>GapMs)Reset();
        lastUpdate=now;
        if(held&&interactHeld){outsideSince=0;switchOwner=0;return AimHeld(hands,view);}
        Entry q[MaxQueue]{};
        const unsigned n=Build(hands,view,q);
        if(held&&now-lastSelected>LostMs){++stats.lost;DropHeld();}
        if(!held)return Seek(q,n,hands,view);
        // Another item the hand is on, and closer than the held one.
        for(unsigned i=0;i<n;i++) {
            if(q[i].owner==held)break;
            if(Settled(q[i].owner,q[i].hand,hands,view))continue;
            if(q[i].touch)return Attempt(q[i],hands,view);
            break;
        }
        int touchingHand=heldHand;
        if(HeldTouching(hands,touchingHand)) {
            heldHand=touchingHand;heldTouch=true;outsideSince=0;switchOwner=0;
            return AimHeld(hands,view);
        }
        heldTouch=false;
        if(!hands[heldHand].valid)heldHand=1-heldHand;
        const auto& f=hands[heldHand].frame;
        const float off=OffAxis(f.origin,f.dir,heldPoint,f.tanCone,f.minRadius);
        if(off>ReleaseScore) {
            if(!outsideSince)outsideSince=now;
            if(now-outsideSince>=HoldMs){++stats.released;DropHeld();return Seek(q,n,hands,view);}
        } else if(outsideSince){outsideSince=0;++stats.wobblesHeld;}
        const Entry* other=nullptr;
        for(unsigned i=0;i<n&&!other;i++)
            if(q[i].owner!=held&&!q[i].touch&&!Settled(q[i].owner,q[i].hand,hands,view))other=&q[i];
        if(other&&other->rank<off*SwitchRatio) {
            if(switchOwner!=other->owner){switchOwner=other->owner;switchSince=now;}
            else if(now-switchSince>=SwitchMs){++stats.switches;return Attempt(*other,hands,view);}
        } else switchOwner=0;
        return AimHeld(hands,view);
    }
    // What the game selected in the update that just ran (owner 0 = nothing).
    void Note(uintptr_t owner,const float* point,uint64_t now) {
        const bool declined=plan.aimed&&plan.owner!=owner&&plan.owner!=held&&focusOwner==plan.owner&&focusTries>=MaxTries;
        if(!owner||!point) {
            if(held&&plan.aimed&&plan.owner==held)heldMissed=true;
            if(declined&&held)AddSettle(plan.owner);
            return;
        }
        const bool planned=plan.aimed&&plan.owner==owner;
        if(owner!=held) {
            DropHeld();held=owner;heldHand=plan.hand;
            heldTouch=planned&&plan.touch;heldFromEye=plan.aimed&&plan.fromEye;
        } else if(planned&&plan.touch)heldTouch=true;
        if(planned&&owner==held)heldFromEye=plan.fromEye;
        heldMissed=false;
        heldPoint[0]=point[0];heldPoint[1]=point[1];heldPoint[2]=point[2];
        lastSelected=now;
        if(planned){focusOwner=0;focusTries=cycleCount=cursor=plainTurns=0;}
        else if(declined)AddSettle(plan.owner);
    }
};

// Where the game's check starts for a touched object: just behind the hand, on the line to it.
inline bool BackedOrigin(const float* hand,const float* target,float back,float* out) {
    float v[3]{target[0]-hand[0],target[1]-hand[1],target[2]-hand[2]};
    if(!Normalize(v)||!std::isfinite(back))return false;
    for(int i=0;i<3;i++)out[i]=hand[i]-v[i]*back;
    return true;
}
// A rigid camera matrix with the same axis layout and handedness as `hint`:
// row 2 forward (origin to target), row 3 position.
inline bool RayMatrix(const CameraMath::Matrix& hint,const float* origin,const float* target,CameraMath::Matrix& out) {
    float f[3]{target[0]-origin[0],target[1]-origin[1],target[2]-origin[2]};
    if(!Normalize(f)) {
        f[0]=hint.m[8];f[1]=hint.m[9];f[2]=hint.m[10];
        if(!Normalize(f))return false;
    }
    float r0[3],r1[3],check[3];
    Cross(hint.m+4,f,r0);
    if(!Normalize(r0)) {
        Cross(f,hint.m,r0);
        if(!Normalize(r0))return false;
    }
    Cross(f,r0,r1);
    if(!Normalize(r1))return false;
    Cross(hint.m,hint.m+4,check);
    const float hintHand=Dot(check,hint.m+8);
    Cross(r0,r1,check);
    if((hintHand<0)!=(Dot(check,f)<0))for(float& v:r0)v=-v;
    for(int i=0;i<3;i++) {
        out.m[i]=r0[i];out.m[4+i]=r1[i];out.m[8+i]=f[i];out.m[12+i]=origin[i];
        if(!std::isfinite(out.m[i])||!std::isfinite(out.m[4+i])||!std::isfinite(out.m[12+i]))return false;
    }
    out.m[3]=out.m[7]=out.m[11]=0;out.m[15]=1;
    return true;
}
}
