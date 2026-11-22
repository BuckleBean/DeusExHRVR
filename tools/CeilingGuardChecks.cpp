// Offline checks of the ceiling guard's height logic. No game code runs.
#include <cstdio>
#include <cmath>
#include "CeilingGuard.h"
#include "QuickBarRule.h"
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
    {State s;s.Update(300,300,45,150,1000);const float held=s.drop;
     Check(s.Hold(1010)==held&&s.Hold(3000)==held,"the wheel or a menu holds the height, however long");
     Check(s.Update(300,300,45,150,3010)==held,"closing it causes no jump");}
    {State s;Check(s.Hold(1000)==0,"holding before any measurement leaves the view alone");}
    {State s;s.Update(300,300,45,150,1000);const float a=s.Release(1010);
     Check(std::abs(a-42)<1e-3f,"cover or a conversation eases away, not a cut");
     for(double t=1020;t<1300;t+=10)s.Release(t);Check(s.drop==0,"and finishes at normal height");}
    using namespace QuickBarRule;
    Check(MenuValue(NoChoice,false,0)==2,"quickbar: controller, no choice: shows the game's auto-hide");
    Check(MenuValue(NoChoice,false,1)==1,"quickbar: keyboard, no choice: follows the game's value");
    Check(MenuValue(NoChoice,true,1)==2,"quickbar: keyboard, game value on: auto-hide");
    Check(!AutoHide(0,false,0),"quickbar: ALWAYS SHOWN wins on a controller");
    Check(AutoHide(1,false,1),"quickbar: AUTO-HIDE wins on a keyboard");
    return failures?1:0;
}
