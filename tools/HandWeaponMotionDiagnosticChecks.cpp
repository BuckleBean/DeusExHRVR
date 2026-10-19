#include "HandWeaponMotionDiagnostic.h"
#include <stdexcept>
#include <iostream>
using namespace HandWeaponMotionDiagnostic;
void Check(bool b){if(!b)throw std::runtime_error("motion diagnostic check failed");}
int main(){
    Check(Wanted(0,3));Check(!Wanted(0,3));Check(Wanted(1,3));Check(!Wanted(1,4));
    Sample s{};s.frame=3;s.count=2;s.before[0].m[12]=12.5f;
    Push(s);s.before[0].m[12]=99;Check(queue[0].before[0].m[12]==12.5f);
    for(unsigned i=0;i<128;i++)Push(s);
    Check(used==128 && dropped==1);
    written=24000;Check(!Wanted(0,6));
    std::cout<<"PASS sampled matching frames, duplicate suppression, copied matrices, bounded queue and capture limit\n";
}
