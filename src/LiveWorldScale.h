#pragma once
#include <atomic>
#include <cmath>

// Menu/input threads only request a scale. The camera consumes it while
// rebuilding a complete tracked snapshot under stateMutex. Rendered scenes
// retain that snapshot's scale, including across a later camera update.
class LiveWorldScale {
public:
    static constexpr float Default=300.f;
private:
    std::atomic<float> requested{Default},active{Default};
public:
    static bool Valid(float value){return std::isfinite(value)&&value>=10.f&&value<=1000.f;}
    void Request(float value){if(Valid(value))requested.store(value);}
    float Configured() const{return requested.load();}
    float Active() const{return active.load();}
    float BeginCamera(){const float value=requested.load();active.store(value);return value;}
};
