// Offline checks of the ceiling guard's height logic. No game code runs.
#include <cstdio>
#include <cmath>
#include "CeilingGuard.h"
using namespace CeilingGuard;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
int main() {
    {State s;Check(s.Update(300,NoCeiling,45,150,1000)==0,"no ceiling: the view is untouched");}
    {State s;Check(s.Update(300,600,45,150,1000)==0,"plenty of room: the view is untouched");}
    {State s;Check(std::abs(s.Update(300,330,45,150,1000)-15)<1e-4f,"a close ceiling keeps the chosen gap");}
    {State s;Check(s.Update(300,200,45,150,1000)==145,"the drop is as large as needed");Check(s.Update(400,200,45,150,1500)==150,"but never past its limit");}
    {State s;s.Update(300,600,45,150,1000);const float a=s.Update(300,300,45,150,1010);
     Check(std::abs(a-9)<1e-3f,"entering a low space eases down, not a jump");
     for(double t=1020;t<1200;t+=10)s.Update(300,300,45,150,t);
     Check(std::abs(s.drop-45)<1e-3f,"it settles at the needed height");
     const float b=s.Update(300,NoCeiling,45,150,1210);Check(std::abs(b-39)<1e-3f,"leaving eases back more gently");
     for(double t=1220;t<1400;t+=10)s.Update(300,NoCeiling,45,150,t);Check(s.drop==0,"and returns fully to normal");}
    {State s;s.Update(300,300,45,150,1000);Check(s.Update(300,NoCeiling,45,150,9000)==0,"after a pause or load it starts fresh");}
    {State s;Check(s.Update(NAN,300,45,150,1000)==0&&s.Update(300,NAN,45,150,1010)==0,"bad readings leave the view alone");}
    return failures?1:0;
}
