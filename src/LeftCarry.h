#pragma once
#include <cstdint>
// While something the left hand grabbed is carried, the left trigger does the right
// trigger's job (throw) and the right trigger does the left's. A trigger already
// squeezed when the swap starts or ends sends nothing until it is released, so a
// held trigger can never turn into the other action.
namespace LeftCarry {
inline constexpr uint8_t ReleasedAt=30;
struct TriggerSwap {
    bool swapped{},holdLeft{},holdRight{};
    void Apply(bool swap,uint8_t& left,uint8_t& right) {
        if(swap!=swapped){swapped=swap;holdLeft=left>ReleasedAt;holdRight=right>ReleasedAt;}
        if(holdLeft){if(left<=ReleasedAt)holdLeft=false;else left=0;}
        if(holdRight){if(right<=ReleasedAt)holdRight=false;else right=0;}
        if(swap){const uint8_t t=left;left=right;right=t;}
    }
};
}
