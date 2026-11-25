// Offline checks of two-handed aim. No game code runs.
#include <cstdio>
#include <cmath>
#include "TwoHandSteer.h"
#include "ModelHandAlignment.h"
using namespace TwoHandSteer;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static Matrix At(float x,float y,float z){Matrix m;for(float& v:m.m)v=0;m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;m.m[12]=x;m.m[13]=y;m.m[14]=z;return m;}
static Matrix YawAt(float degrees,float x,float y,float z){auto m=At(x,y,z);const float a=degrees*3.14159265f/180,s=std::sin(a),c=std::cos(a);m.m[0]=c;m.m[2]=-s;m.m[8]=s;m.m[10]=c;return m;}
static bool Near(float a,float b,float e=.002f){return std::abs(a-b)<e;}
static void Settle(Steer& s,double& t,bool show,const Matrix& r,const Matrix& l,double until){for(;t<until;t+=10)s.Update(t,show,r,l,300);}
int main() {
    const float pi=3.14159265f,a20=20*pi/180;
    const auto right=At(0,0,0),left=At(0,0,100),swung=At(100*std::sin(a20),0,100*std::cos(a20));
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);auto m=At(0,0,50);
     if(s.amount>0&&s.angle>0)s.Apply(m,right);
     Check(s.angle==0&&Near(m.m[14],50),"grabbing the weapon does not move it");
     Settle(s,t,true,right,swung,1800);auto g=At(0,0,50);s.Apply(g,right);
     Check(Near(g.m[8],std::sin(a20))&&Near(g.m[10],std::cos(a20)),"moving the left hand turns the weapon toward it");
     Check(Near(g.m[12],50*std::sin(a20),.05f)&&Near(g.m[14],50*std::cos(a20),.05f),"the weapon swings around the right hand");
     auto pivot=right;s.Apply(pivot,right);Check(Near(pivot.m[12],0)&&Near(pivot.m[13],0)&&Near(pivot.m[14],0),"the right hand's position stays put");
     float n=g.m[8]*g.m[8]+g.m[9]*g.m[9]+g.m[10]*g.m[10],d=g.m[0]*g.m[8]+g.m[1]*g.m[9]+g.m[2]*g.m[10];
     Check(Near(n,1)&&Near(d,0),"the weapon is not distorted");
     const float held=s.angle;s.Update(t,false,right,swung,300);t+=10;const float eased=s.amount;
     Check(s.angle==held&&eased<1&&eased>0,"releasing eases back instead of snapping");
     Settle(s,t,false,right,swung,t+400);Check(s.amount==0&&!s.Update(t,false,right,swung,300),"and aim returns fully to the right hand");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);bool moved=false;
     for(float deg=0;deg<=40;deg+=2,t+=10){const auto r=YawAt(deg,0,0,0);const float a=deg*pi/180;s.Update(t,true,r,At(100*std::sin(a),0,100*std::cos(a)),300);moved=moved||s.angle>.002f;}
     Check(!moved,"turning both hands together adds no steering");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,At(100,0,-5),1800);Check(Near(s.angle,MaxAngle),"the turn has a limit");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,swung,1800);const float held=s.angle;
     Settle(s,t,true,right,At(1,0,1),1900);Check(Near(s.angle,held),"hands too close together keep the last direction");}
    {Steer s;double t=1000;Settle(s,t,true,right,left,1400);Settle(s,t,true,right,swung,1800);
     Check(!s.Update(9000,true,right,swung,300)&&s.amount==0,"after a pause or load it starts fresh with no jump");}
    {Steer s;Matrix bad=left;bad.m[12]=NAN;Check(!s.Update(1000,true,right,bad,300)&&!s.have,"a bad reading is ignored");}
    {ScopeHold h;Check(!h.Update(false,false,true,1000),"no hold before the scope");
     Check(!h.Update(true,false,true,1010),"scoping without the left hand on the gun stays with the right hand");}
    {ScopeHold h;Check(h.Update(true,true,true,1000),"a hold that was steering carries into the scope");
     Check(h.Update(true,false,true,5000),"and lasts while scoped");
     Check(h.Update(false,false,true,5010),"leaving the scope keeps it for a moment");
     Check(h.Update(false,false,true,5200),"long enough for the hands to return");
     Check(!h.Update(false,false,true,5300),"then the normal hold takes over");}
    {ScopeHold h;h.Update(true,true,true,1000);Check(!h.Update(true,false,false,1010),"letting go in the scope gives aim back to the right hand");
     Check(!h.Update(true,false,true,1020),"and pressing again in the scope does not grab");}
    {   // The wrist HUD must land exactly where the hand model is drawn, through the real
        // placement function, for several grip poses.
        const Matrix game{{0,-1,0,0, 0,0,-1,0, 1,0,0,0, 10,20,30,1}};
        Transport::Pose reference{};reference.orientation={0,0,0,1};
        auto axisTurn=[&](float x,float y,float z,float deg){const float n=std::sqrt(x*x+y*y+z*z),h=deg*pi/360,s=std::sin(h)/n;return Transport::Quaternion{x*s,y*s,z*s,std::cos(h)};};
        auto place=[&](const Transport::Pose& p,Matrix& out){Transport::Tracking t{};t.valid=1;t.tick=1000;t.rightGrip.valid=1;t.rightGrip.pose=p;return ModelHandAlignment::GripTarget(t,1000,game,reference,300,out);};
        auto same=[](const Matrix& a,const Matrix& b,float e){bool ok=true;for(int i=0;i<16;i++)ok=ok&&std::abs(a.m[i]-b.m[i])<(i>=12&&i<15?e*40:e);return ok;};
        Transport::Pose real{axisTurn(1,2,3,40),{.2f,1.1f,-.3f}},hand{axisTurn(-2,1,.5f,75),{-.1f,.9f,-.6f}};
        Matrix T{},V{},placed{};
        Check(place(real,T)&&place(hand,V),"HUD check: poses place");
        HudShift full,none,half;
        Check(MakeShift(V,T,300,1,full)&&place(ApplyShift(real,full),placed)&&same(placed,V,.002f),"the wrist HUD lands on the visible hand");
        Check(MakeShift(V,T,300,0,none)&&place(ApplyShift(real,none),placed)&&same(placed,T,.002f),"with no shift it stays on the controller");
        Check(MakeShift(V,T,300,.5f,half),"a partial shift is valid");
        const auto mid=ApplyShift(real,half);
        const float qn=mid.orientation.x*mid.orientation.x+mid.orientation.y*mid.orientation.y+mid.orientation.z*mid.orientation.z+mid.orientation.w*mid.orientation.w;
        Check(Near(qn,1),"and stays a clean rotation part way");
        Matrix mirrored=V;for(int j=0;j<3;j++)mirrored.m[j]=-mirrored.m[j];
        HudShift rejected;Check(!MakeShift(mirrored,T,300,1,rejected),"a distorted hand transform is refused");
        Matrix bad=V;bad.m[12]=NAN;Check(!MakeShift(bad,T,300,1,rejected),"a bad position is refused");
        for(float deg:{170.f,179.f,-175.f}) {
            Transport::Pose turnedPose{CameraMath::Multiply(real.orientation,axisTurn(1,0,0,deg)),real.position};Matrix F{};HudShift s;
            Check(place(turnedPose,F)&&MakeShift(F,T,300,1,s)&&place(ApplyShift(real,s),placed)&&same(placed,F,.003f),"large hand turns convert correctly");
        }
        // Left-handed mode: the support hand is placed in the gun hand's mirror world.
        {
            Transport::Tracking t{};t.valid=1;t.tick=1000;t.leftGrip.valid=1;t.rightGrip.valid=1;
            t.leftGrip.pose=hand;t.rightGrip.pose=real;
            Matrix gunHand{},support{},physicalRight{},physicalLeft{},mirror{};
            ModelHandAlignment::swapHands=true;
            const bool placedBoth=ModelHandAlignment::GripTarget(t,1000,game,reference,300,gunHand)&&
                ModelHandAlignment::GripTarget(t,1000,game,reference,300,support,true);
            ModelHandAlignment::swapHands=false;
            Check(placedBoth&&ModelHandAlignment::GripTarget(t,1000,game,reference,300,physicalRight)&&
                  ModelHandAlignment::GripTarget(t,1000,game,reference,300,physicalLeft,true),"left-handed: both hands place");
            Check(same(gunHand,physicalLeft,.0001f),"left-handed: the gun hand is placed at the left controller");
            const auto& v=support;
            const float det=v.m[0]*(v.m[5]*v.m[10]-v.m[6]*v.m[9])-v.m[1]*(v.m[4]*v.m[10]-v.m[6]*v.m[8])+v.m[2]*(v.m[4]*v.m[9]-v.m[5]*v.m[8]);
            Check(Near(det,1,.001f),"left-handed: the support hand's frame is an ordinary rotation");
            Check(Near(ModelHandAlignment::Dot(ModelHandAlignment::V{gunHand.m[12]-v.m[12],gunHand.m[13]-v.m[13],gunHand.m[14]-v.m[14]},
                                               ModelHandAlignment::V{gunHand.m[12]-v.m[12],gunHand.m[13]-v.m[13],gunHand.m[14]-v.m[14]}),
                       ModelHandAlignment::Dot(ModelHandAlignment::V{physicalLeft.m[12]-physicalRight.m[12],physicalLeft.m[13]-physicalRight.m[13],physicalLeft.m[14]-physicalRight.m[14]},
                                               ModelHandAlignment::V{physicalLeft.m[12]-physicalRight.m[12],physicalLeft.m[13]-physicalRight.m[13],physicalLeft.m[14]-physicalRight.m[14]}),1.f),
                  "left-handed: the hands are as far apart as the controllers");
            // The final mirror across the left grip brings the support hand back to the right
            // controller (its side axis reversed, as for every mirrored hand).
            Check(ModelHandAlignment::MirrorAcross(physicalLeft,mirror),"left-handed: gun hand mirror built");
            auto back=CameraMath::Multiply(support,mirror);for(int j=0;j<3;j++)back.m[j]=-back.m[j];
            Check(same(back,physicalRight,.0005f),"left-handed: mirrored back, the support hand is at the right controller");
            // Same result as mirroring the support hand across its own controller (stage 1).
            Matrix own{};ModelHandAlignment::MirrorAcross(physicalRight,own);
            auto stage1=CameraMath::Multiply(physicalRight,own);auto now=CameraMath::Multiply(support,mirror);
            Check(same(stage1,now,.0005f),"left-handed: with no gun held the support hand is drawn as before");
            Transport::Tracking lost=t;lost.leftGrip.valid=0;Matrix none{};
            ModelHandAlignment::swapHands=true;
            Check(!ModelHandAlignment::GripTarget(lost,1000,game,reference,300,none,true),"left-handed: no left controller, no support hand target");
            ModelHandAlignment::swapHands=false;
        }
    }
    // Left-handed mirror mode: reflection across a grip's side-to-side axis.
    {
        using namespace ModelHandAlignment;
        Matrix grip=YawAt(90,10,5,3),r{};   // first axis now points along world z
        Check(MirrorAcross(grip,r),"mirror built from a valid grip");
        const float p[4]{12,7,8,1};float q[4]{};
        for(int j=0;j<4;j++){q[j]=0;for(int k=0;k<4;k++)q[j]+=p[k]*r.m[k*4+j];}
        V n{grip.m[0],grip.m[1],grip.m[2]};Normalize(n);
        const double before=(p[0]-10)*n[0]+(p[1]-5)*n[1]+(p[2]-3)*n[2];
        const double after=(q[0]-10)*n[0]+(q[1]-5)*n[1]+(q[2]-3)*n[2];
        Check(Near(float(after),float(-before)),"a point ends up the same distance on the other side of the hand's plane");
        Check(Near(q[0]-10,p[0]-10-float(2*before*n[0]))&&Near(q[1],p[1])&&Near(q[3],1),"movement along the plane is unchanged");
        const float det=r.m[0]*(r.m[5]*r.m[10]-r.m[6]*r.m[9])-r.m[1]*(r.m[4]*r.m[10]-r.m[6]*r.m[8])+r.m[2]*(r.m[4]*r.m[9]-r.m[5]*r.m[8]);
        Check(Near(det,-1),"it is a true mirror (determinant -1)");
        Matrix twice=CameraMath::Multiply(r,r);bool identity=true;
        for(int i=0;i<16;i++)identity=identity&&Near(twice.m[i],(i%5==0)?1.f:0.f);
        Check(identity,"mirroring twice gives back the original");
        // Pose mirror: the model's left hand takes the mirror image of the right hand's pose.
        std::array<Matrix,223> palette{};std::array<bool,223> right{},left{};
        for(unsigned i=129;i<=145;i++)right[i]=true;
        for(unsigned i=100;i<=116;i++)left[i]=true;
        for(unsigned k=0;k<17;k++) {
            palette[129+k]=YawAt(20.f+k,5.f+k,-3,40);palette[129+k].m[3]=.25f;   // right: placed, w slot marked
            palette[100+k]=YawAt(-50.f-k,-30,4,41);palette[100+k].m[3]=.5f;      // left: elsewhere
        }
        const auto oldRight=palette[133],oldLeft=palette[104];
        Matrix atLeft{},atRight{};
        Check(MirrorAcross(YawAt(0,5,-3,40),atLeft)&&MirrorAcross(YawAt(0,-30,4,41),atRight),"both controller mirrors built");
        Check(MirrorPose(palette.data(),223,right,left,atLeft,atRight),"pose mirror applies");
        auto apply=[](const Matrix& m,const float* v,float* o){for(int j=0;j<3;j++){o[j]=m.m[12+j];for(int k=0;k<3;k++)o[j]+=v[k]*m.m[k*4+j];}};
        const float vr[3]{11,-2,7},vl[3]{-11,-2,7};   // a right-hand bind vertex and its mirror on the left hand
        float placedRight[3]{},mirrorOfIt[3]{},leftNow[3]{};
        apply(oldRight,vr,placedRight);
        {Matrix pt=At(placedRight[0],placedRight[1],placedRight[2]);auto q=CameraMath::Multiply(pt,atLeft);for(int j=0;j<3;j++)mirrorOfIt[j]=q.m[12+j];}
        apply(palette[104],vl,leftNow);
        Check(Near(leftNow[0],mirrorOfIt[0],.01f)&&Near(leftNow[1],mirrorOfIt[1],.01f)&&Near(leftNow[2],mirrorOfIt[2],.01f),
              "the left hand lands exactly where the mirrored right hand would be");
        float placedLeft[3]{},mirrorOfLeft[3]{},rightNow[3]{};
        apply(oldLeft,vl,placedLeft);
        {Matrix pt=At(placedLeft[0],placedLeft[1],placedLeft[2]);auto q=CameraMath::Multiply(pt,atRight);for(int j=0;j<3;j++)mirrorOfLeft[j]=q.m[12+j];}
        apply(palette[133],vr,rightNow);
        Check(Near(rightNow[0],mirrorOfLeft[0],.01f)&&Near(rightNow[1],mirrorOfLeft[1],.01f)&&Near(rightNow[2],mirrorOfLeft[2],.01f),
              "and the right hand where the mirrored left hand would be");
        const auto& m=palette[104];
        const float poseDet=m.m[0]*(m.m[5]*m.m[10]-m.m[6]*m.m[9])-m.m[1]*(m.m[4]*m.m[10]-m.m[6]*m.m[8])+m.m[2]*(m.m[4]*m.m[9]-m.m[5]*m.m[8]);
        Check(Near(poseDet,1),"the result is an ordinary rotation (draws normally, no inside-out)");
        Check(palette[104].m[3]==.5f&&palette[133].m[3]==.25f,"each hand keeps its own shader w-column slots");
        auto saved=palette;std::array<bool,223> uneven=right;uneven[130]=false;
        Check(!MirrorPose(palette.data(),223,uneven,left,atLeft,atRight)&&palette[104].m[12]==saved[104].m[12],"unpaired bones change nothing");
        Matrix bad=atLeft;bad.m[0]=std::nanf("");
        Check(!MirrorPose(palette.data(),223,right,left,bad,atRight)&&palette[133].m[12]==saved[133].m[12],"an invalid mirror changes nothing");
        // The gun: its own plane, then the hand's plane -> a rigid move.
        {
            Matrix muzzle=YawAt(10,20,0,30),hand=YawAt(0,12,0,30),c{};   // muzzle row 0 = gun's right
            Check(MirrorGun(muzzle,hand,c),"gun mirror built");
            const float gunDet=c.m[0]*(c.m[5]*c.m[10]-c.m[6]*c.m[9])-c.m[1]*(c.m[4]*c.m[10]-c.m[6]*c.m[8])+c.m[2]*(c.m[4]*c.m[9]-c.m[5]*c.m[8]);
            Check(Near(gunDet,1),"the gun moves rigidly (draws normally)");
            auto moved=CameraMath::Multiply(muzzle,c);
            Matrix handMirror{};MirrorAcross(hand,handMirror);
            auto expected=CameraMath::Multiply(At(20,0,30),handMirror);
            Check(Near(moved.m[12],expected.m[12])&&Near(moved.m[13],expected.m[13])&&Near(moved.m[14],expected.m[14]),
                  "the muzzle lands at the mirror of the right-handed muzzle");
            V barrel{-muzzle.m[4],-muzzle.m[5],-muzzle.m[6]},hn{hand.m[0],hand.m[1],hand.m[2]};Normalize(hn);
            const double along=Dot(barrel,hn);V want{barrel[0]-2*along*hn[0],barrel[1]-2*along*hn[1],barrel[2]-2*along*hn[2]};
            Check(Near(-moved.m[4],float(want[0]))&&Near(-moved.m[5],float(want[1]))&&Near(-moved.m[6],float(want[2])),
                  "and the barrel points where the mirrored barrel would");
            Matrix zero{};Check(!MirrorGun(muzzle,zero,c),"no grip target, no gun mirror");
        }
        Matrix nan=grip;nan.m[0]=nan.m[1]=nan.m[2]=0;Check(!MirrorAcross(nan,r),"a grip without a side axis gives no mirror");
    }
    return failures?1:0;
}
