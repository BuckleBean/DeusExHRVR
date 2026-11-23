// Offline checks of the two-handed attach logic. No game code runs.
#include <cstdio>
#include <cmath>
#include "TwoHandAttach.h"
using namespace TwoHandAttach;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
int main() {
    Check(GripFor(0x6AB8)==Grip::None&&GripFor(0x50BE)==Grip::None,"the PEPS and stun gun stay one-handed");
    Check(GripFor(0x05FB)==Grip::Support&&GripFor(0x1EF1)==Grip::Support&&GripFor(0x5AB1)==Grip::Support&&GripFor(0x5AB0)==Grip::Support,"pistols and revolvers: the left hand holds, the right hand aims");
    Check(GripFor(0x2D3B)==Grip::Full&&GripFor(0x72A2)==Grip::Full,"the shotgun and sniper rifle can be held two-handed");
    Check(Supported(120)&&!Supported(400)&&!Supported(2)&&!Supported(NAN),"a weapon qualifies only with the game's hands close together");
    {State s;Check(s.Update(1000,false,true,120,300)==0,"no grip: the hand stays free");
     const float a=s.Update(1010,true,true,120,300);Check(a>0&&a<1&&s.attached,"grip: it moves onto the weapon, not a jump");
     for(double t=1020;t<1200;t+=10)s.Update(t,true,true,125,300);Check(s.amount==1,"and arrives");
     const float b=s.Update(1210,false,true,125,300);Check(b<1&&b>0&&!s.attached,"letting go eases it back");
     for(double t=1220;t<1400;t+=10)s.Update(t,false,true,125,300);Check(s.amount==0,"and returns fully to the free hand");}
    {State s;s.Update(1000,true,true,120,300);for(double t=1010;t<1200;t+=10)s.Update(t,true,true,200,300);
     Check(s.attached,"moving the hands apart a little stays attached");
     s.Update(1210,true,true,211,300);Check(!s.attached&&s.broken,"pulling well away lets go");
     for(double t=1220;t<1500;t+=10)s.Update(t,true,true,120,300);Check(!s.attached&&s.amount==0,"and it does not grab again until the grip is released");
     s.Update(1510,false,true,120,300);s.Update(1520,true,true,120,300);Check(s.attached,"a new press can attach again");}
    {State s;s.Update(1000,true,true,120,300);for(double t=1010;t<1200;t+=10)s.Update(t,true,true,120,300);
     for(double t=1210;t<1500;t+=10)s.Update(t,true,false,120,300);Check(s.attached&&s.amount==0,"sprinting moves the hand off and keeps the hold");
     for(double t=1510;t<1800;t+=10)s.Update(t,true,true,120,300);Check(s.amount==1,"and it returns afterwards");}
    {State s;s.Update(1000,true,true,120,300);Check(s.Update(9000,true,false,120,300)==0,"after a pause or load nothing is left half way");}
    {State s;s.Update(1000,true,true,NAN,300);Check(!s.attached,"a bad reading does not attach");}
    Check(WeaponReady(true,1,0,false,false),"a drawn weapon can be held");
    Check(!WeaponReady(true,4,0,false,false),"a holstered weapon cannot");
    Check(!WeaponReady(true,6,0,false,false),"nor one that loaded holstered");
    Check(WeaponReady(true,8,0,false,false),"a fully drawn weapon can");
    Check(!WeaponReady(true,1,7,false,false),"nor one being changed");
    Check(!WeaponReady(true,1,0,true,false)&&!WeaponReady(true,1,0,false,true),"nor during a grenade throw or a carry");
    Check(!WeaponReady(false,1,0,false,false),"nor an unconfirmed weapon");
    Check(!Eligible(true,false,true,false,400),"an unsupported pose cannot start a hold");
    Check(Eligible(true,false,true,true,400),"but a reload animation does not end one");
    Check(!Eligible(true,true,true,true,120)&&!Eligible(true,false,false,true,120),"sprinting or holstering does");
    {State s;s.Update(1000,true,true,120,300);s.Drop(true);s.Update(1010,true,true,120,300);
     Check(!s.attached&&s.broken,"changing weapons lets go until the next press");
     s.Update(1020,false,true,120,300);s.Update(1030,true,true,120,300);Check(s.attached,"and the next press can hold the new weapon");}
    Check(Supported(239.1f)&&Supported(228.6f),"the two weapons measured just over the old limit now qualify");
    {const float right[3]{0,0,0},muzzle[3]{0,0,200},spot[3]{0,-20,120};
     const float mid[3]{30,0,160},tip[3]{0,40,205},far[3]{100,0,100},behind[3]{0,0,-40};
     Check(std::abs(GrabDistance(mid,right,muzzle,spot)-30)<.01f,"grabbing near the front of the gun counts");
     Check(GrabDistance(tip,right,muzzle,spot)<=GrabMetres*300,"so does the muzzle end");
     Check(GrabDistance(behind,right,muzzle,spot)<=GrabMetres*300,"and the back of the gun");
     Check(GrabDistance(far,right,muzzle,spot)>GrabMetres*300,"a hand well away from the gun does not");
     const float bad[3]{0,0,5000},nearSpot[3]{0,-20,125};
     Check(GrabDistance(nearSpot,right,bad,spot)<6,"a bad muzzle position falls back to the hand spot");
     Check(GrabDistance(far,right,bad,spot)>GrabMetres*300,"and cannot make the whole world grabbable");}
    return failures?1:0;
}
