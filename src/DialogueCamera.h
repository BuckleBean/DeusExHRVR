#pragma once
#include "CameraMath.h"
#include <cmath>
#include <cstdint>

// A conversation may hold only an actual, recent first-person view. The
// conversation's own transition matrices must never replenish this cache.
namespace DialogueCamera {
struct State {
    CameraMath::Matrix cached;
    uintptr_t owner{};
    uint64_t sampled{},ended{},lastUpdate{};
    bool valid{},holding{},wasDialogue{};
    void Reset(bool dialogue=false) {valid=holding=false;owner=0;wasDialogue=dialogue;lastUpdate=0;}
    bool Update(bool enabled,bool dialogue,bool gameplay,uintptr_t player,
                const Transport::Vector& body,float scale,uint64_t now,CameraMath::Matrix& view) {
        if(!enabled||!player){Reset(dialogue);return false;}
        if((owner&&owner!=player)||(lastUpdate&&now-lastUpdate>=1000)) {
            valid=holding=false; // allow a new entry to fall back, never reuse across a gap
        }
        lastUpdate=now;
        bool finite=true;for(float v:view.m)finite&=std::isfinite(v);
        const float eye=view.m[14]-body.z;
        // Game units, matching the existing stance filter's accepted eye range.
        const bool good=gameplay&&finite&&std::isfinite(body.x)&&std::isfinite(body.y)&&
            std::isfinite(body.z)&&eye>1.f&&eye<900.f&&
            std::hypot(view.m[12]-body.x,view.m[13]-body.y)<.5f*scale;
        if(dialogue&&!wasDialogue)holding=valid&&owner==player&&now-sampled<1000;
        if(!dialogue&&wasDialogue)ended=now;
        wasDialogue=dialogue;
        if(holding&&!dialogue){
            const float dx=view.m[12]-cached.m[12],dy=view.m[13]-cached.m[13],dz=view.m[14]-cached.m[14];
            if((good&&dx*dx+dy*dy+dz*dz<.01f*scale*scale)||now-ended>=2000)holding=false;
        }
        if(holding)view=cached;
        else if(!dialogue&&good){cached=view;sampled=now;owner=player;valid=true;}
        return holding;
    }
};
}
