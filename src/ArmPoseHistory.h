#pragma once
#include "FullArmIK.h"
#include <mutex>

namespace ArmPoseHistory {
struct Key {
    uint64_t frame{}, tracking{}, tick{}, epoch{};
    uintptr_t model{}, plan{}, player{}, weapon{}, scene{};
    float scale{};
    Transport::Pose reference{};
    double baseYaw{}; // artificial turn heading; transport prior world-space directions
};
// Shared across render threads. Only arm matrices are cached: finger animation,
// hand placement and each draw's shader metadata remain owned by the caller.
class Cache {
    std::mutex mutex;
    bool valid{};
    Key key{};
    FullArmIK::History history{}, generationPrior{};
    bool hasGenerationPrior{};
    CameraMath::Matrix head{}, left{}, right{};
    std::array<CameraMath::Matrix,223> solved{};
    std::array<bool,223> mask{};
    unsigned count{};
    void Clear(){valid=false;history={};generationPrior={};hasGenerationPrior=false;}
    static bool SameContext(const Key& a,const Key& b){
        return a.epoch==b.epoch && a.model==b.model && a.plan==b.plan && a.player==b.player &&
            a.weapon==b.weapon && a.scene==b.scene && a.scale==b.scale &&
            !memcmp(&a.reference,&b.reference,sizeof(a.reference));
    }
public:
    void Reset(){std::lock_guard lock(mutex);Clear();}
    bool Apply(CameraMath::Matrix* palette,unsigned n,const FullArmIK::Rig& rig,
               const CameraMath::Matrix& h,const std::array<bool,223>& used,const Key& k){
        std::lock_guard lock(mutex);
        if(!std::isfinite(k.baseYaw)||!palette || n>223 || n<=130 || !rig.valid || rig.count!=n){Clear();return false;}
        auto l=FullArmIK::Affine(palette[101]),r=FullArmIK::Affine(palette[130]);
        if(valid && k.frame<key.frame)return false; // late draw cannot rewind history
        if(valid && k.frame==key.frame){
            // Keep context/head strict. Native hand reconstruction may differ
            // between draws: solve against that draw's actual palms from the
            // SAME frozen prior, never from this generation's committed result.
            if(k.baseYaw!=key.baseYaw||!SameContext(k,key)||k.tracking!=key.tracking||n!=count||used!=mask||
               memcmp(&h,&head,sizeof(h)))return false;
            if(memcmp(&l,&left,sizeof(l))||memcmp(&r,&right,sizeof(r)))
                return FullArmIK::Apply(palette,n,rig,h,used,
                    hasGenerationPrior?&generationPrior:nullptr,nullptr);
            for(unsigned i=0;i<n;i++)if(used[i]&&!FullArmIK::Hand(i))
                for(unsigned j=0;j<16;j++)if(j%4!=3)palette[i].m[j]=solved[i].m[j];
            return true;
        }
        bool continuous=valid && n==count && used==mask && SameContext(k,key) && k.frame==key.frame+1 &&
            k.tracking>=key.tracking && k.tick>=key.tick && k.tick-key.tick<250;
        auto prior=history;
        if(continuous){
            // Heading uses atan2(forward.x,forward.y): positive yaw is negative
            // Cartesian Z rotation. Translate no vectors; these are directions.
            double delta=k.baseYaw-key.baseYaw,c=std::cos(delta),s=std::sin(delta);
            auto rotate=[&](FullArmIK::V& v){double x=v[0],y=v[1];v[0]=c*x+s*y;v[1]=-s*x+c*y;};
            for(unsigned side=0;side<2;side++){rotate(prior.bend[side]);rotate(prior.fold[side].upper);rotate(prior.fold[side].lower);}
        }
        FullArmIK::History next{};
        if(!FullArmIK::Apply(palette,n,rig,h,used,continuous?&prior:nullptr,&next)){Clear();return false;}
        generationPrior=continuous?prior:FullArmIK::History{};hasGenerationPrior=continuous;
        key=k;head=h;left=l;right=r;count=n;mask=used;history=next;valid=true;
        for(unsigned i=0;i<n;i++)solved[i]=palette[i];
        return true;
    }
};
}
