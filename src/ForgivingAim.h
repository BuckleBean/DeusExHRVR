#pragma once
#include <cmath>
#include <cstdint>
#include "CameraMath.h"

// Forgiving aim (November 21). A near miss with the interaction line still counts:
// the usable object closest to the line, inside a cone around it, receives the
// game's own single interaction check. The game still applies its range and rules.
// Owner values are compared only; this file never reads game memory.
namespace ForgivingAim {
inline constexpr float KeepScore=1.25f;      // a held target may drift this far past the cone
inline constexpr float SwitchRatio=.5f;      // another object must be twice as close to the line
inline constexpr uint64_t RejectMs=150;      // no game selection for this long: try another
inline constexpr uint64_t BlockMs=1500;      // how long a refused object is left out
inline constexpr uint64_t GapMs=200;         // no updates this long (a screen): start fresh
inline constexpr float MinRadiusMetres=.15f; // the cone never gets narrower than this up close
inline constexpr float SearchUnits=948.15f;  // longest interaction reach seen in this game

struct Candidate {uintptr_t owner{};float point[3]{};float score{};};

inline float Dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline void Cross(const float* a,const float* b,float* out) {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
inline bool Normalize(float* v) {
    const float n=std::sqrt(Dot(v,v));
    if(!std::isfinite(n)||n<1e-5f)return false;
    v[0]/=n;v[1]/=n;v[2]/=n;return true;
}
// 0 = exactly on the line, 1 = at the edge of the cone. False: behind, too far or outside.
inline bool Score(const float* origin,const float* dir,const float* point,float reach,float tanCone,float minRadius,float& score) {
    const float v[3]{point[0]-origin[0],point[1]-origin[1],point[2]-origin[2]};
    const float distance=std::sqrt(Dot(v,v));
    if(!std::isfinite(distance)||distance>reach)return false;
    const float along=Dot(v,dir);
    if(!std::isfinite(along)||along<=0)return false;
    const float off[3]{v[0]-dir[0]*along,v[1]-dir[1]*along,v[2]-dir[2]*along};
    const float offset=std::sqrt(Dot(off,off));
    const float wide=tanCone*along;
    const float allowed=wide>minRadius?wide:minRadius;
    if(!std::isfinite(offset)||allowed<=0)return false;
    score=offset/allowed;
    return score<=KeepScore;
}

class Selector {
    struct Block {uintptr_t owner{};uint64_t until{};};
    Block blocks[8]{};unsigned nextBlock{};
    Candidate held{};uint64_t lastOk{},lastUpdate{};uintptr_t selected{};unsigned rejected{};
    bool Blocked(uintptr_t owner,uint64_t now) const {
        for(const auto& b:blocks)if(b.owner==owner&&now<b.until)return true;
        return false;
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
public:
    void Reset(){held={};lastOk=0;selected=0;}
    uintptr_t Owner() const {return held.owner;}
    unsigned Rejected() const {return rejected;}
    // The target for this update, or nullptr to use the plain aim line.
    const Candidate* Update(const Candidate* list,unsigned count,uint64_t now,bool interactHeld) {
        if(!list){Reset();lastUpdate=now;return nullptr;}
        if(lastUpdate&&now-lastUpdate>GapMs)Reset();
        lastUpdate=now;
        // Whatever the game actually selected is what the prompt shows: keep it.
        if(selected&&selected!=held.owner) {
            if(const auto* chosen=Find(list,count,selected)){held=*chosen;lastOk=now;}
        }
        if(held.owner) {
            const auto* same=Find(list,count,held.owner);
            if(same&&same->score<=KeepScore) {
                if(interactHeld){held=*same;return &held;}
                if(now-lastOk<=RejectMs) {
                    const auto* other=Best(list,count,now,held.owner);
                    if(other&&other->score<same->score*SwitchRatio){held=*other;lastOk=now;selected=0;return &held;}
                    held=*same;return &held;
                }
                blocks[nextBlock]={held.owner,now+BlockMs};nextBlock=(nextBlock+1)%8;++rejected;
            }
            held={};lastOk=0;selected=0;
        }
        const auto* best=Best(list,count,now,0);
        if(!best)return nullptr;
        held=*best;lastOk=now;
        return &held;
    }
    void NoteSelection(uintptr_t owner,uint64_t now) {
        selected=owner;
        if(owner&&owner==held.owner)lastOk=now;
    }
};

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
