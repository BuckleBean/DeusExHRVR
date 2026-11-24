// Offline checks of the two-handed attach logic. No game code runs.
#include <cstdio>
#include <cmath>
#include "TwoHandAttach.h"
#include "NativeSprint.h"
#include <array>
#include <cstring>
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
    {State s;s.Update(1000,true,true,120,300);s.Cancel(true);
     Check(!s.attached&&s.amount==0&&s.broken,"sprint cancels the pose immediately without blending through running animation");
     for(double t=1010;t<1800;t+=10)s.Update(t,true,true,120,300);
     Check(!s.attached&&s.amount==0,"ending sprint cannot restore the old grip");
     s.Update(1810,false,true,120,300);s.Update(1820,true,true,120,300);
     Check(s.attached,"after sprint a released and freshly pressed grip can attach");}
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
    {
        std::array<unsigned char,0x1000> memory{};
        auto read=[&](uintptr_t a,void* out,size_t n){
            if(a<0x10000||a>0x11000||n>0x11000-a)return false;
            memcpy(out,memory.data()+a-0x10000,n);return true;
        };
        auto word=[&](unsigned offset,uint32_t value){memcpy(memory.data()+offset,&value,4);};
        Check(!NativeSprint::Read(0x10000,read).known,"missing character cannot authorize a grab");
        word(0x58,0x10100);word(0x114,0x10200);word(0x240,0x10000);
        word(0x244,0x10300);word(0x38c,0x10000);word(0x3b0,0x10500);
        auto status=NativeSprint::Read(0x10000,read);
        Check(status.known&&!status.active,"valid empty action slots are not sprinting");
        word(0x508,0x10700);memory[0x705]=0x53;
        status=NativeSprint::Read(0x10000,read);
        Check(status.known&&!status.active,"ordinary action does not block walking with two hands");
        for(unsigned type=0x6b;type<=0x6d;type++) {
            memory[0x705]=static_cast<unsigned char>(type);status=NativeSprint::Read(0x10000,read);
            Check(status.known&&status.active,"native sprint, enter and leave each block independent of button state");
        }
        memory[0x705]=0x70;word(0x708,2);word(0x70c,0x10800);memory[0x800]=0x53;memory[0x801]=0x6b;
        status=NativeSprint::Read(0x10000,read);
        Check(status.known&&status.active,"derived sprint actions are found through ancestors");
        word(0x708,129);Check(!NativeSprint::Read(0x10000,read).known,"corrupt ancestry is bounded and blocks grabs");
        word(0x708,0);word(0x38c,0x10001);
        Check(!NativeSprint::Read(0x10000,read).known,"an action controller belonging to another entity is rejected");
        word(0x38c,0x10000);word(0x508,0);word(0x5b8,0x10700);memory[0x705]=0x6d;
        status=NativeSprint::Read(0x10000,read);
        Check(status.known&&status.active,"sprint exit is found in the last action slot");
    }
    return failures?1:0;
}
