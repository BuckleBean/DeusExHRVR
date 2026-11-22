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
    {State s;Check(s.Update(300,200,45,150,1000)==145,"the drop is as large as needed");float v=0;bool within=true;for(double t=1010;t<3000;t+=10){v=s.Update(400,200,45,150,t);within=within&&v<=150;}Check(within&&v==150,"but never past its limit");}
    {State s;s.Update(300,600,45,150,1000);const float a=s.Update(300,300,45,150,1010),b=s.Update(300,300,45,150,1020);
     Check(a>0&&a<1,"entering a low space starts gently");
     Check(b-a>a,"and picks up speed instead of starting at full speed");
     float previous=b;bool rising=true,over=false;
     for(double t=1030;t<2500;t+=10){const float v=s.Update(300,300,45,150,t);rising=rising&&v>=previous;over=over||v>45;previous=v;}
     Check(rising&&!over,"it moves one way and never overshoots");
     Check(s.drop==45&&s.speed==0,"it settles at the needed height");
     const float c=s.Update(300,NoCeiling,45,150,2510);Check(c<45&&c>44,"leaving starts gently too");
     for(double t=2520;t<4500;t+=10)s.Update(300,NoCeiling,45,150,t);Check(s.drop==0,"and returns fully to normal");}
    {State s;s.Update(300,600,45,150,1000);float previous=0,largest=0;
     for(double t=1010;t<2500;t+=10){const float v=s.Update(450,300,45,150,t);largest=std::fmax(largest,v-previous);previous=v;}
     Check(largest<=DownSpeed*.01f+1e-3f,"the largest drop is limited to its top speed");Check(s.drop==150,"and still arrives");}
    {State s;s.Update(300,300,45,150,1000);Check(s.Update(300,NoCeiling,45,150,9000)==0,"after a pause or load it starts fresh");}
    {State s;Check(s.Update(NAN,300,45,150,1000)==0&&s.Update(300,NAN,45,150,1010)==0,"bad readings leave the view alone");}
    {State s;s.Update(300,300,45,150,1000);const float held=s.drop;
     Check(s.Hold(1010)==held&&s.Hold(3000)==held,"the wheel or a menu holds the height, however long");
     Check(s.Update(300,300,45,150,3010)==held,"closing it causes no jump");}
    {State s;Check(s.Hold(1000)==0,"holding before any measurement leaves the view alone");}
    {State s;s.Update(300,300,45,150,1000);const float a=s.Release(1010);
     Check(a<45&&a>44,"standing, cover or a conversation eases away, not a cut");
     for(double t=1020;t<3000;t+=10)s.Release(t);Check(s.drop==0,"and finishes at normal height");}
    Check(Crouched(253)&&Crouched(268),"crouched heights count as crouched");
    Check(!Crouched(541)&&!Crouched(566),"sprinting and standing heights do not");
    Check(!Crouched(NAN),"a bad camera height does not");
    {Ahead a;float x=0,y=0;Check(!a.Update(0,0,1000,x,y),"look-ahead: no guess on the first reading");
     Check(!a.Update(0,0,1100,x,y),"look-ahead: none while standing still");
     Check(a.Update(30,0,1200,x,y)&&std::abs(x-120)<.01f&&y==0,"look-ahead: points where the player is moving");
     Check(a.Update(130,0,1300,x,y)&&std::abs(x-280)<.01f,"look-ahead: never more than 50 cm ahead");
     Check(!a.Update(5000,0,1400,x,y),"look-ahead: ignores a teleport");
     a.Update(5000,0,1500,x,y);Check(!a.Update(5100,0,3000,x,y),"look-ahead: starts fresh after a pause");}
    using namespace QuickBarRule;
    Check(MenuValue(NoChoice,false,0)==2,"quickbar: controller, no choice: shows the game's auto-hide");
    Check(MenuValue(NoChoice,false,1)==1,"quickbar: keyboard, no choice: follows the game's value");
    Check(MenuValue(NoChoice,true,1)==2,"quickbar: keyboard, game value on: auto-hide");
    Check(!AutoHide(0,false,0),"quickbar: ALWAYS SHOWN wins on a controller");
    Check(AutoHide(1,false,1),"quickbar: AUTO-HIDE wins on a keyboard");
    return failures?1:0;
}
