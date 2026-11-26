// Offline checks of the VR crosshair setting. No game code runs.
#include <cstdio>
#include "CoverCrosshair.h"
using namespace CoverCrosshair;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
int main() {
    Check(ShowsFirst(First)&&ShowsFirst(Both)&&!ShowsFirst(Off)&&!ShowsFirst(Third),"first-person crosshair shows only for 1st person and Both");
    Check(ShowsThird(Third)&&ShowsThird(Both)&&!ShowsThird(Off)&&!ShowsThird(First),"cover crosshair shows only for 3rd person and Both");
    Check(Next(Off)==First&&Next(First)==Third&&Next(Third)==Both&&Next(Both)==Off,"the menu row cycles through all four");
    bool round=true;for(uint32_t m=Off;m<=Both;m++)round=round&&Parse(Value(m))==m;
    Check(round,"every setting survives being saved and read back");
    Check(Parse(L"")==Third&&Parse(L"nonsense")==Third,"a missing or unknown setting keeps today's behaviour");
    Check(Parse(L"off")==Off&&Parse(L"0")==Off&&Parse(L"BOTH")==Both,"the INI value ignores upper and lower case");
    {bool kept=true;for(uint32_t t=0;t<16;t++)kept=kept&&KeptWithReticleOff(t)==(t==5||t==6||t==9||t==10);
     Check(kept,"with the crosshair off, exactly the game's own kept reticles stay: scope, crossbow scope, metering scope, rocket launcher");}
    return failures?1:0;
}
