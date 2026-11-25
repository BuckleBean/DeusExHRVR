#include "CarryAttachment.h"
#include <cstdio>
#include <limits>
using namespace CameraMath;
bool Near(float a,float b){return std::abs(a-b)<.002f;}
bool ContactAtHand(const CarryAttachment::Attachment& a,const Matrix& object,const Matrix& hand) {
    for(int j=0;j<3;j++){float p=object.m[12+j];for(int i=0;i<3;i++)p+=a.contact[i]*object.m[i*4+j];if(!Near(p,hand.m[12+j]))return false;}return true;
}
int main() {
    int failures=0;auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    Matrix hand=Rotation({0,0,0,1}),object=hand,out{};hand.m[12]=10;object.m[12]=30;
    float contact[3]{10,0,0};CarryAttachment::Attachment a;
    check(a.Capture(hand,object,contact,300),"capture a real surface offset");
    check(a.Target(hand,out)&&Near(out.m[12],30)&&ContactAtHand(a,out,hand),"initial touching grab has no position jump or gap");
    hand.m[12]=45;hand.m[13]=20;hand.m[14]=80;
    check(a.Target(hand,out)&&Near(out.m[12],65)&&Near(out.m[13],20)&&Near(out.m[14],80),"translation follows the hand in all three axes");
    const float r=std::sqrt(.5f);
    hand=Rotation({0,0,r,r});hand.m[12]=10;
    check(a.Target(hand,out)&&ContactAtHand(a,out,hand)&&Near(out.m[12],10)&&Near(out.m[13],20),"quarter turn pivots the object around the grabbed surface");
    for(int axis=0;axis<3;axis++) {
        float q[4]{0,0,0,1};q[axis]=r;q[3]=r;hand=Rotation({q[0],q[1],q[2],q[3]});
        float native[4]{};bool ok=a.Target(hand,out)&&CarryAttachment::Quat(out,native);
        auto back=Rotation({native[0],native[1],native[2],native[3]});
        for(int i=0;i<12;i++)ok=ok&&Near(back.m[i],out.m[i]);
        check(ok&&ContactAtHand(a,out,hand),"native quaternion round trip preserves the wrist turn and anchor");
    }
    for(int axis=0;axis<3;axis++) {
        float q[4]{};q[axis]=1;hand=Rotation({q[0],q[1],q[2],q[3]});float converted[4]{};
        bool ok=CarryAttachment::Quat(hand,converted);auto back=Rotation({converted[0],converted[1],converted[2],converted[3]});
        for(int i=0;i<12;i++)ok=ok&&Near(hand.m[i],back.m[i]);check(ok,"half-turn conversion stays finite");
    }
    Matrix tilted=Rotation({r,0,0,r});tilted.m[12]=30;
    hand=Rotation({0,r,0,r});contact[0]=30;contact[1]=5;contact[2]=0;
    check(a.Capture(hand,tilted,contact,300)&&a.Target(hand,out)&&ContactAtHand(a,out,hand),"pre-rotated object retains orientation relative to either hand");
    for(int i=0;i<12;i++)if(!Near(out.m[i],tilted.m[i])){check(false,"original orientation preserved");break;}
    a.Reset();check(!a.Target(hand,out),"drop or failed grab discards attachment");
    Matrix bad=hand;bad.m[0]=std::numeric_limits<float>::quiet_NaN();
    check(!a.Capture(bad,object,contact,300),"invalid tracking cannot create attachment");
    bad=Rotation({0,0,0,1});bad.m[0]=-1;
    check(!CarryAttachment::Rigid(bad),"reflected frame rejected");
    contact[0]=10000;check(!a.Capture(hand,object,contact,300),"implausible surface offset rejected");
    return failures?1:0;
}
