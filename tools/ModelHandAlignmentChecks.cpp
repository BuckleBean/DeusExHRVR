#include "ModelHandAlignment.h"
#include <fstream>
#include <random>
#include <stdexcept>
#include <cstdio>
#include <limits>
using namespace ModelHandAlignment;
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
bool Near(const Matrix& a,const Matrix& b,float eps=.004f){for(int j=0;j<16;j++)if(std::abs(a.m[j]-b.m[j])>eps)return false;return true;}
Matrix Identity(){Matrix m{};m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;return m;}
int main(int argc,char** argv){try{
    Check(argc==2,"skeleton fixture path required");
    std::ifstream file(argv[1],std::ios::binary);
    std::array<Segment,223> segments{};
    for(auto& s:segments){float xyz[3];file.read(reinterpret_cast<char*>(xyz),12);file.read(reinterpret_cast<char*>(&s.parent),4);for(int j=0;j<3;j++)s.pivot[j]=xyz[j];}
    Check(bool(file),"read original skeleton fixture");
    Matrix lf{};Check(BuildLeftFrame(segments,lf),"left skeleton accepted");
    Check(std::abs(lf.m[12]-222.42785f)<.001f && std::abs(lf.m[2]+.81261736f)<.0001f,"left frame matches offline derivation");
    std::array<Matrix,223> lp{};for(auto& m:lp)m=Identity();
    auto leftOriginal=lp;std::array<bool,223> selectedLeft{};
    for(unsigned i=100;i<=116;i++)selectedLeft[i]=true;
    auto lt=Identity();lt.m[12]=53;lt.m[13]=-17;lt.m[14]=400;
    Check(RestLeft(lp.data(),223,selectedLeft,lf,lt),"rest left placed");
    Check(Near(CameraMath::Multiply(lf,lp[101]),lt),"left palm reaches target");
    for(unsigned i=0;i<223;i++)Check(Near(lp[i],i>=100&&i<=116?lp[101]:leftOriginal[i]),"only left bones changed and rest shape retained");
    Transport::Tracking leftTracking{};leftTracking.valid=leftTracking.leftGrip.valid=1;leftTracking.tick=1000;
    leftTracking.leftGrip.pose.orientation.w=1;Transport::Pose leftReference{};leftReference.orientation.w=1;
    Matrix leftTarget{};Check(GripTarget(leftTracking,1000,Identity(),leftReference,300,leftTarget,true),"left grip works independently of right");
    Check(!GripTarget(leftTracking,1250,Identity(),leftReference,300,leftTarget,true),"stale left rejected");
    Matrix frame{};Check(BuildRightFrame(segments,frame),"actual skeleton accepted");
    Check(std::abs(frame.m[12]+222.427796f)<.001f && std::abs(frame.m[13]+18.315311f)<.001f && std::abs(frame.m[14]-403.403194f)<.001f,"candidate center matches offline derivation");
    Check(std::abs(frame.m[0]+.57940424f)<1e-5f && std::abs(frame.m[9]-.98299217f)<1e-5f,"candidate axis signs match derivation");
    auto bad=segments;bad[130].parent=130;Check(!BuildRightFrame(bad,frame),"cyclic skeleton rejected");
    bad=segments;bad[130].pivot[0]+=10;Check(!BuildRightFrame(bad,frame),"different skeleton rejected");
    bad=segments;bad[145].pivot[0]=std::numeric_limits<double>::quiet_NaN();Check(!BuildRightFrame(bad,frame),"nonfinite skeleton rejected");
    Check(BuildRightFrame(segments,frame),"restore frame");
    Transport::Tracking tracking{};tracking.valid=tracking.rightGrip.valid=1;tracking.tick=1000;
    tracking.rightGrip.pose.orientation.w=1;tracking.rightGrip.pose.position={.2f,.3f,-.4f};
    Transport::Pose reference{};reference.orientation.w=1;
    Matrix target{};auto base=Identity();
    Check(GripTarget(tracking,1000,base,reference,300,target),"valid grip accepted without aim");
    Check(target.m[0]==1 && target.m[5]==-1 && target.m[10]==-1,"grip axes converted once");
    Check(std::abs(target.m[12]-60)<.001 && std::abs(target.m[13]+90)<.001 && std::abs(target.m[14]-120)<.001,"grip has no muzzle forward offset");
    Check(!GripTarget(tracking,1250,base,reference,300,target),"stale tracking rejected");
    Check(!GripTarget(tracking,999,base,reference,300,target),"future timestamp rejected");
    tracking.rightGrip.valid=0;Check(!GripTarget(tracking,1000,base,reference,300,target),"lost grip rejected");tracking.rightGrip.valid=1;
    tracking.rightGrip.pose.orientation.w=0;Check(!GripTarget(tracking,1000,base,reference,300,target),"invalid quaternion rejected");tracking.rightGrip.pose.orientation.w=1;
    // Reference shift must preserve placement when absolute tracking translates equally.
    Check(GripTarget(tracking,1000,base,reference,300,target),"baseline target");auto before=target;
    reference.position={10,20,30};tracking.rightGrip.pose.position={10.2f,20.3f,29.6f};
    Check(GripTarget(tracking,1000,base,reference,300,target)&&Near(target,before,.002f),"reference translation invariance");
    reference={};reference.orientation.w=1;
    tracking.rightGrip.pose.position={0,0,0};tracking.rightGrip.pose.orientation={0,0,.7071067812f,.7071067812f};
    Check(GripTarget(tracking,1000,base,reference,300,target),"rotated grip");
    auto rotated=Identity();rotated.m[0]=rotated.m[5]=0;rotated.m[1]=rotated.m[4]=-1;rotated.m[10]=-1;
    Check(Near(target,rotated,.00001f),"90 degree grip rotation handedness");
    reference.orientation=tracking.rightGrip.pose.orientation;reference.position={1,2,3};tracking.rightGrip.pose.position={1,3,3};
    auto recentered=Identity();recentered.m[5]=recentered.m[10]=-1;recentered.m[12]=300;
    Check(GripTarget(tracking,1000,base,reference,300,target)&&Near(target,recentered,.0001f),"rotated reference and translation");
    std::array<bool,223> selected{};for(unsigned i=129;i<=145;i++)selected[i]=true;
    std::mt19937 rng(87370);std::uniform_real_distribution<float> dist(-1,1);
    float maxError=0;
    for(int test=0;test<1000;test++){
        auto randomMatrix=[&]{Transport::Quaternion q{dist(rng),dist(rng),dist(rng),dist(rng)};float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);q.x/=n;q.y/=n;q.z/=n;q.w/=n;auto m=CameraMath::Rotation(q);for(int j=0;j<3;j++)m.m[12+j]=dist(rng)*1000;return m;};
        std::array<Matrix,223> palette{};for(auto& m:palette)m=randomMatrix();
        // Preserve the engine's non-affine shader metadata exactly.
        for(unsigned i=129;i<=145;i++){palette[i].m[3]=.123f;palette[i].m[7]=.456f;palette[i].m[11]=.789f;palette[i].m[15]=0;}
        auto original=palette;auto desired=randomMatrix();
        Check(Move(palette.data(),223,selected,frame,desired),"move actual-model frame under random poses");
        auto placed=CameraMath::Multiply(frame,Affine(palette[130]));
        for(int j=0;j<16;j++)maxError=(std::max)(maxError,std::abs(placed.m[j]-desired.m[j]));
        Check(Near(placed,desired),"palm anchor lands on target");
        for(unsigned i=0;i<223;i++){
            if(!selected[i])Check(!memcmp(&palette[i],&original[i],sizeof(Matrix)),"unselected bones untouched");
            else for(int j:{3,7,11,15})Check(palette[i].m[j]==original[i].m[j],"shader metadata preserved");
        }
        // Relative finger/wrist transforms survive the common delta.
        Matrix invOld{},invNew{};Check(Invert(Affine(original[130]),invOld)&&Invert(Affine(palette[130]),invNew),"invert palms");
        Check(Near(CameraMath::Multiply(Affine(original[134]),invOld),CameraMath::Multiply(Affine(palette[134]),invNew),.008f),"relative finger animation preserved");
        // Neutral wrist: 129 takes 130's spatial transform, keeps its own
        // shader metadata, and nothing else in the palette moves.
        auto beforeWrist=palette;
        Check(NeutralWrist(palette.data(),223,selected),"neutral wrist applies");
        for(int j:{0,1,2,4,5,6,8,9,10,12,13,14})
            Check(palette[129].m[j]==palette[130].m[j],"wrist takes the palm transform");
        for(int j:{3,7,11,15})
            Check(palette[129].m[j]==beforeWrist[129].m[j],"wrist shader metadata kept");
        for(unsigned i=0;i<223;i++)if(i!=129)
            Check(!memcmp(&palette[i],&beforeWrist[i],sizeof(Matrix)),"only the wrist changed");
        // A bad late bone must fail without writing earlier bones.
        original[145].m[0]=std::numeric_limits<float>::infinity();palette=original;
        Check(!Move(palette.data(),223,selected,frame,desired)&&!memcmp(palette.data(),original.data(),sizeof(palette)),"transactional rejection");
    }
    std::array<Matrix,223> palette{};for(auto& m:palette)m=Identity();auto saved=palette;
    palette[130].m[0]=0;saved=palette;Check(!Move(palette.data(),223,selected,frame,Identity())&&!memcmp(palette.data(),saved.data(),sizeof(palette)),"singular palm rejected");
    palette[130]=Identity();palette[130].m[0]=2;saved=palette;Check(!Move(palette.data(),223,selected,frame,Identity())&&!memcmp(palette.data(),saved.data(),sizeof(palette)),"scale distortion rejected");
    printf("PASS: neutral wrist; actual skeleton; target axes/position; reference shift; loss/staleness; 1000 compiled float transforms (max anchor error %.6f units); animation preservation; shader metadata; atomic failure\n",maxError);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
