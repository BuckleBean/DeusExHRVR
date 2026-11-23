#pragma once
#include <cmath>
#include <cstdint>
#include "CameraMath.h"

// Forgiving aim with a prompt hold, for either hand (November 21). Hand 0 is the
// right/normal aim, hand 1 the left controller, with identical rules. Once the game selects an object,
// its single interaction check keeps being aimed at the exact point it accepted,
// so small hand movements do not drop the prompt. With nothing selected, updates
// alternate between the plain aim line and the usable object closest to it. The
// game still applies its range and rules. Owner values are compared only; this
// file never reads game memory.
namespace ForgivingAim {
inline constexpr float KeepScore=1.25f;      // candidates are listed up to here
inline constexpr float ReleaseScore=1.5f;    // aim must leave this far to start releasing
inline constexpr uint64_t HoldMs=300;        // and stay out this long
inline constexpr uint64_t LostMs=120;        // held, but the game stopped selecting it
inline constexpr float SwitchRatio=.5f;      // another object must be twice as close to the aim
inline constexpr uint64_t SwitchMs=120;      // for this long
inline constexpr uint64_t RejectMs=150;      // an attempt the game never accepts
inline constexpr uint64_t BlockMs=1500;      // how long a refused object is left out
inline constexpr uint64_t GapMs=200;         // no updates this long (a screen): start fresh
inline constexpr float MinRadiusMetres=.15f; // the cone never gets narrower than this up close
inline constexpr float SearchUnits=948.15f;  // longest interaction reach seen in this game
// Hand contact (November 22): a usable object at the hand wins, whichever way the hand points.
// Measured to the object's own position, so a small item in a locker beats the large locker.
inline constexpr float NoContact=1e30f;
inline constexpr float ContactKeep=1.4f;     // a touched object stays until the hand is this much further
inline constexpr float ContactSwitchMargin=.1f; // another touched object takes over once closer by this share of the reach (1.5 cm at 15 cm)
inline constexpr float ContactBackMetres=.15f; // the game's check starts behind the hand, never inside the object

struct Candidate {uintptr_t owner{};float point[3]{};float score{};float handDistance=NoContact;float contactPoint[3]{};uintptr_t object{};};
struct Frame {float origin[3]{};float dir[3]{};float tanCone{};float minRadius{};float contactReach{};};
struct Turn {int hand{};bool search{};};
struct Stats {unsigned wobblesHeld{},released{},lost{},refused{},switches{};uintptr_t lastRefused{};};

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

class Selector {
    struct Block {uintptr_t owner{};uint64_t until{};};
    Block blocks[8]{};unsigned nextBlock{};
    uintptr_t held{};int heldHand{};float heldPoint[3]{};uint64_t lastSelected{},outsideSince{};
    uintptr_t switchOwner{};uint64_t switchSince{};
    Candidate attempt[2]{};uint64_t attemptSince[2]{};
    bool attemptContact[2]{},heldContact{},contactAim{};
    uint64_t lastUpdate{};unsigned phase{3};Turn turn{};
    Stats stats{};
    bool Blocked(uintptr_t owner,uint64_t now) const {
        for(const auto& b:blocks)if(b.owner==owner&&now<b.until)return true;
        return false;
    }
    void AddBlock(uintptr_t owner,uint64_t now) {
        blocks[nextBlock]={owner,now+BlockMs};nextBlock=(nextBlock+1)%8;
    }
    const Candidate* Find(const Candidate* list,unsigned count,uintptr_t owner) const {
        for(unsigned i=0;i<count;i++)if(list[i].owner==owner)return &list[i];
        return nullptr;
    }
    const Candidate* Best(const Candidate* list,unsigned count,uint64_t now,uintptr_t except) const {
        const Candidate* best=nullptr;
        for(unsigned i=0;i<count;i++) {
            const auto& c=list[i];
            if(!c.owner||c.owner==except||!std::isfinite(c.score)||c.score>1.f||Blocked(c.owner,now))continue;
            if(!best||c.score<best->score)best=&c;
        }
        return best;
    }
    const Candidate* NearestToHand(const Candidate* list,unsigned count,float reach,uint64_t now) const {
        if(!(reach>0))return nullptr;
        const Candidate* best=nullptr;
        for(unsigned i=0;i<count;i++) {
            const auto& c=list[i];
            if(!c.owner||!std::isfinite(c.handDistance)||c.handDistance>reach||Blocked(c.owner,now))continue;
            if(!best||c.handDistance<best->handDistance)best=&c;
        }
        return best;
    }
    const float* StartContact(const Candidate& c,int hand,uint64_t now) {
        auto& a=attempt[hand];a=c;
        a.point[0]=c.contactPoint[0];a.point[1]=c.contactPoint[1];a.point[2]=c.contactPoint[2];
        attemptContact[hand]=true;attemptSince[hand]=now;contactAim=true;
        return a.point;
    }
    bool ShouldTake(const Candidate* closest,const Candidate* mine,float reach) const {
        if(!closest||closest->owner==held)return false;
        const bool touching=mine&&mine->handDistance<=reach*ContactKeep;
        return !touching||closest->handDistance<mine->handDistance-reach*ContactSwitchMargin;
    }
    void DropHeld(){held=0;heldHand=0;outsideSince=0;switchOwner=0;switchSince=0;heldContact=false;}
public:
    void Reset(){DropHeld();attempt[0]={};attempt[1]={};attemptSince[0]=attemptSince[1]=0;attemptContact[0]=attemptContact[1]=false;contactAim=false;lastSelected=0;}
    bool AimingContact() const {return contactAim;}
    // The object this update's check is meant for when the hand is on it, or 0.
    uintptr_t ContactOwner() const {
        if(!contactAim)return 0;
        return attemptContact[turn.hand]?attempt[turn.hand].owner:heldContact?held:0;
    }
    // The other hand is at an object that should take over from the one being held.
    bool HasContact(const Candidate* list,unsigned count,float reach,uint64_t now) const {
        return ShouldTake(NearestToHand(list,count,reach,now),Find(list,count,held),reach);
    }
    void UseHand(int hand){turn.hand=hand;}
    uintptr_t Held() const {return held;}
    int HeldHand() const {return heldHand;}
    const Stats& GetStats() const {return stats;}
    // Which hand this update belongs to. A held object keeps its hand. Otherwise
    // the turns run: right line, left line, right near miss, left near miss.
    Turn BeginUpdate(uint64_t now,bool twoHands) {
        if(lastUpdate&&now-lastUpdate>GapMs)Reset();
        lastUpdate=now;
        if(held)turn={heldHand,true};
        else if(twoHands){phase=(phase+1)%4;turn={int(phase&1),phase>=2};}
        else {phase=(phase+1)%2;turn={0,phase==1};}
        return turn;
    }
    // The point to aim this turn's check at, or nullptr for that hand's plain line.
    const float* Update(const Candidate* list,unsigned count,const Frame& f,uint64_t now,bool interactHeld) {
        if(!list){Reset();return nullptr;}
        const int hand=turn.hand;
        contactAim=false;
        const auto* closest=turn.search?NearestToHand(list,count,f.contactReach,now):nullptr;
        if(held) {
            if(interactHeld){outsideSince=0;switchOwner=0;contactAim=heldContact;return heldPoint;}
            if(now-lastSelected>LostMs){AddBlock(held,now);++stats.lost;DropHeld();return nullptr;}
            if(f.contactReach>0) {
                const auto* mine=Find(list,count,held);
                if(ShouldTake(closest,mine,f.contactReach)) {
                    const Candidate target=*closest;
                    DropHeld();
                    return StartContact(target,hand,now);
                }
                if(mine&&mine->handDistance<=f.contactReach*ContactKeep) {
                    outsideSince=0;switchOwner=0;heldContact=true;contactAim=true;
                    return heldPoint;
                }
                heldContact=false;
            }
            const float off=OffAxis(f.origin,f.dir,heldPoint,f.tanCone,f.minRadius);
            if(off>ReleaseScore) {
                if(!outsideSince)outsideSince=now;
                if(now-outsideSince>=HoldMs){++stats.released;DropHeld();return nullptr;}
            } else if(outsideSince){outsideSince=0;++stats.wobblesHeld;}
            const auto* other=Best(list,count,now,held);
            if(other&&other->score<off*SwitchRatio) {
                if(switchOwner!=other->owner){switchOwner=other->owner;switchSince=now;}
                else if(now-switchSince>=SwitchMs) {
                    ++stats.switches;
                    attempt[hand]=*other;attemptSince[hand]=now;DropHeld();
                    return attempt[hand].point;
                }
            } else switchOwner=0;
            return heldPoint;
        }
        if(!turn.search)return nullptr;
        auto& a=attempt[hand];
        if(closest) {
            if(!attemptContact[hand]||a.owner!=closest->owner)return StartContact(*closest,hand,now);
            if(now-attemptSince[hand]<=RejectMs){contactAim=true;return a.point;}
            AddBlock(a.owner,now);++stats.refused;stats.lastRefused=a.owner;a={};attemptContact[hand]=false;
        } else if(attemptContact[hand]){a={};attemptContact[hand]=false;}
        if(a.owner) {
            const auto* same=Find(list,count,a.owner);
            if(!same)a={};
            else if(now-attemptSince[hand]>RejectMs) {
                AddBlock(a.owner,now);++stats.refused;stats.lastRefused=a.owner;a={};
            } else a=*same;
        }
        if(!a.owner) {
            const auto* best=Best(list,count,now,0);
            if(!best)return nullptr;
            a=*best;attemptSince[hand]=now;
        }
        return a.point;
    }
    // What the game selected in the update that just ran (owner 0 = nothing).
    void NoteSelection(uintptr_t owner,const float* point,uint64_t now) {
        if(!owner||!point)return;
        const int turnHand=turn.hand;
        const bool aimedContact=contactAim&&attemptContact[turnHand]&&attempt[turnHand].owner;
        // The game chose something else on the way to the touched object: try that one later.
        if(aimedContact&&attempt[turnHand].owner!=owner){AddBlock(attempt[turnHand].owner,now);++stats.refused;stats.lastRefused=attempt[turnHand].owner;}
        const bool byContact=aimedContact&&attempt[turnHand].owner==owner;
        if(owner!=held){DropHeld();held=owner;heldHand=turnHand;heldContact=byContact;}
        else if(byContact)heldContact=true;
        heldPoint[0]=point[0];heldPoint[1]=point[1];heldPoint[2]=point[2];
        lastSelected=now;
        attempt[0]={};attempt[1]={};attemptSince[0]=attemptSince[1]=0;attemptContact[0]=attemptContact[1]=false;
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
