#pragma once
#include "NativeHandWeapon.h"
namespace ArmBodyHeading {
inline double Yaw(const CameraMath::Matrix& m){return std::atan2(m.m[8],m.m[9]);}
struct Context {uintptr_t player{},scene{};uint64_t epoch{};float scale{};};
inline bool Same(Context a,Context b){return a.player==b.player&&a.scene==b.scene&&a.epoch==b.epoch&&a.scale==b.scale;}
// Controlled candidate: heading comes only from the game base; the headset
// contributes position. No headset-yaw following, threshold, or smoothing.
// This does not estimate physical torso turns. Caller owns synchronization.
class State {
    bool valid{};Context context{};uint64_t frame{};CameraMath::Matrix output{};
public:
    void Reset(){valid=false;}
    bool Update(const CameraMath::Matrix& head,const CameraMath::Matrix& base,
                Context ctx,uint64_t generation,uint64_t /*now*/,CameraMath::Matrix& result){
        if(!NativeHandWeapon::Rigid(head)||!NativeHandWeapon::Rigid(base)){Reset();return false;}
        bool same=valid&&Same(ctx,context);
        if(same&&generation<frame)return false;
        if(same&&generation==frame){result=output;return true;}
        output=CameraMath::HorizontalDirection(base,head);
        context=ctx;frame=generation;valid=true;result=output;return true;
    }
};
}
