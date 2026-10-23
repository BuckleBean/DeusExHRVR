#include "MuzzleQueryDiagnostic.h"
#include <stdexcept>
#include <limits>
using namespace MuzzleQueryDiagnostic;
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){
    DiagnosticOptions::hands=true;try{
    CameraMath::Matrix m{};Check(Fault(m)==2,"zero result classified as non-affine");
    m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;Check(Fault(m)==0,"valid native pose");
    m.m[0]=2;Check(Fault(m)==3,"scaled pose");m.m[0]=-1;Check(Fault(m)==4,"reflected pose");
    m.m[0]=std::numeric_limits<float>::infinity();Check(Fault(m)==1,"nonfinite result");
    Sample s{};s.weapon=123;s.key=0x50be;s.relevant=true;s.frame=10;s.tick=1000;
    Failure(Draw,s);Failure(Draw,s);++s.frame;s.tick=1010;Failure(Draw,s);
    Check(bursts[Draw*2+1].calls==3&&bursts[Draw*2+1].frames==2,"deduplicate calls into frames");
    Success(Draw,false,s.weapon,1020);Check(count==0,"unrelated success cannot close relevant failure");
    Success(Draw,true,s.weapon,1020);Check(count==1&&queue[0].reason==2&&queue[0].burst.recoveredTick==1020,"same-weapon recovery");
    Failure(Draw,s);++s.weapon;Failure(Draw,s);Check(count==2&&queue[1].reason==1,"weapon switch splits failure");
    Flush();Check(pending[Draw*2+1]&&bursts[Draw*2+1].active,"flush retains recovery tracking");
    Success(Draw,true,s.weapon,1030);Check(count==1&&queue[0].reason==2,"recovery after periodic flush");
    for(unsigned i=0;i<200;i++){++s.weapon;Failure(Aim,s);Success(Aim,true,s.weapon,1040);}
    Check(count==128&&dropped.load()>0,"bounded overflow counted");Flush();
    printf("PASS muzzle diagnostic fault classes, call/frame counts, relevant vs incidental queries, switch/recovery context, periodic flush and bounded overflow\n");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
