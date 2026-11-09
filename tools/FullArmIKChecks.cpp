#include "FullArmIK.h"
#include "ArmPoseHistory.h"
#include "ArmBodyHeading.h"
#include "InstalledArmPoseHistory.h"
#include "HandOnlyGeometry.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstdio>
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
    static_assert(sizeof(void*)==4);
    Check(argc==3,"mesh and skeleton required");
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});
    Check(bytes.size()>128,"mesh missing");
    auto h=reinterpret_cast<uint32_t*>(bytes.data());auto addr=[](void*p){return uint32_t(uintptr_t(p));};
    auto patch=[&](uint32_t& o){Check(o<bytes.size(),"fixture offset");o+=addr(bytes.data());};
    patch(h[21]);patch(h[22]);patch(h[24]);
    for(unsigned i=0;i<(h[25]>>16);i++){
        auto b=reinterpret_cast<uint32_t*>(h[22])+i*24;patch(b[14]);patch(b[15]);patch(b[19]);
    }
    uint32_t model[32]{},render[16]{};model[25]=addr(render);render[6]=addr(h);
    HandOnlyGeometry::fullArmsEnabled=true;
    auto plan=HandOnlyGeometry::Prepare(model);Check(plan&&plan->armsReady,"arm mesh validation");
    unsigned total=0;for(const auto& part:plan->arms)for(const auto& side:part.runs)for(auto r:side)total+=r.triangles;
    Check(total==(h[26]==223?6543u:13439u),"full arm triangle count");
    std::ifstream s(argv[2],std::ios::binary);std::array<ModelHandAlignment::Segment,223> segments{};
    for(unsigned i=0;i<h[26];i++){float xyz[3]{};s.read(reinterpret_cast<char*>(xyz),12);s.read(reinterpret_cast<char*>(&segments[i].parent),4);for(int j=0;j<3;j++)segments[i].pivot[j]=xyz[j];}
    Check(bool(s),"skeleton missing");auto rig=FullArmIK::Build(segments,h[26]);Check(rig.valid,"rig validation");
    auto bad=segments;bad[128].parent=0;Check(!FullArmIK::Build(bad,h[26]).valid,"bad arm hierarchy rejected");
    using namespace FullArmIK;
    Matrix head{{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,587.326f,1}};
    ArmPoseHistory::Cache cache;InstalledArmPoseHistory::Cache installedCache;History previous{};
    for(int trial=0;trial<160;trial++){
        auto body=CameraMath::Rotation({0,0,1,0});std::array<Matrix,223> palette;
        palette.fill(body);
        for(auto sh:{96u,125u}){
            auto hand=sh+5;float angle=float(trial)*.04f;
            auto rotation=CameraMath::Multiply(CameraMath::Rotation({0,std::sin(angle/2),0,std::cos(angle/2)}),body);
            V target{sh==96?-65.:65.,80.+40*std::sin(angle),470.+70*std::cos(angle)};
            auto palm=Map(rotation,rig.p[hand],target);
            unsigned lo=sh==96?100:129,hi=sh==96?116:145;
            for(unsigned i=lo;i<=hi;i++)palette[i]=palm;
        }
        // Exercise actual accepted finger-curl implementation before arm solve.
        auto curl=InteractionCurl::Build(segments,h[26]);
        Check(InteractionCurl::Apply(palette.data(),h[26],curl,float(trial%11)/10),"seed curl");
        for(auto& m:palette){m.m[3]=7;m.m[7]=8;m.m[11]=9;m.m[15]=10;}
        auto before=palette;
        ArmPoseHistory::Key key{};key.frame=trial+1;key.tracking=trial+1;key.tick=1000+trial*11;
        key.model=1;key.plan=2;key.player=3;key.weapon=4;key.scale=300;
        auto expected=before;History prior=previous,next{};
        Check(Apply(expected.data(),h[26],rig,head,plan->armBones,trial?&previous:nullptr,&next),"reference history solve");previous=next;
        Check(cache.Apply(palette.data(),h[26],rig,head,plan->armBones,key),"cached arm solve");
        Check(!memcmp(expected.data(),palette.data(),sizeof(palette)),"cache differs from one advance per generation");
        InstalledArmPoseHistory::Key oldKey{};oldKey.frame=key.frame;oldKey.tracking=key.tracking;oldKey.tick=key.tick;oldKey.epoch=key.epoch;
        oldKey.model=key.model;oldKey.plan=key.plan;oldKey.player=key.player;oldKey.weapon=key.weapon;oldKey.scene=key.scene;oldKey.scale=key.scale;oldKey.reference=key.reference;
        auto installed=before;Check(installedCache.Apply(installed.data(),h[26],rig,head,plan->armBones,oldKey),"installed cache seed");
        auto tiny=before;tiny[130].m[12]=std::nextafter(tiny[130].m[12],INFINITY);auto tinySaved=tiny;
        Check(!installedCache.Apply(tiny.data(),h[26],rig,head,plan->armBones,oldKey),"installed one-ULP rejection not reproduced");
        Check(!memcmp(tiny.data(),tinySaved.data(),sizeof(tiny)),"installed rejection wrote input");
        for(float delta:{0.f,1.f,3.f}){
            auto changed=tinySaved;changed[130].m[12]+=delta;auto referenceDraw=changed;
            Check(Apply(referenceDraw.data(),h[26],rig,head,plan->armBones,trial?&prior:nullptr),"draw reference solve");
            Check(cache.Apply(changed.data(),h[26],rig,head,plan->armBones,key),"benign draw difference rejected");
            Check(!memcmp(changed.data(),referenceDraw.data(),sizeof(changed)),"draw did not use frozen prior/current palms");
        }
        auto badContext=key;badContext.epoch++;auto mismatch=before;
        Check(!cache.Apply(mismatch.data(),h[26],rig,head,plan->armBones,badContext),"changed context accepted");
        Check(!memcmp(mismatch.data(),before.data(),sizeof(mismatch)),"context rejection wrote input");
        auto badHand=before;badHand[130].m[12]=1e8f;auto badSaved=badHand;
        Check(!cache.Apply(badHand.data(),h[26],rig,head,plan->armBones,key),"invalid draw accepted");
        Check(!memcmp(badHand.data(),badSaved.data(),sizeof(badHand)),"invalid draw wrote input");
        for(unsigned i=0;i<h[26];i++){
            if(Hand(i)||!plan->armBones[i])Check(!memcmp(&palette[i],&before[i],64),"hand/finger/unselected palette changed");
            for(auto j:{3u,7u,11u,15u})Check(palette[i].m[j]==before[i].m[j],"shader metadata changed");
            Check(ValidSkin(Affine(palette[i])),"invalid output matrix");
        }
        // Regression: checking the abstract solver alone misses a later rotation
        // blend that turns the actual lower-arm skin away from its hand endpoint.
        for(auto sh:{96u,125u}){
            auto hand=sh+5,el=sh+3;
            auto anchored=Point(rig.p[sh],Affine(palette[sh]));
            auto wanted=Point(rig.p[sh],CameraMath::Rotation({0,0,1,0}));
            auto shoulderError=Sub(anchored,wanted);
            Check(std::sqrt(Dot(shoulderError,shoulderError))<.002,"shoulder anchor drift");
            auto predicted=Point(rig.p[hand],Affine(palette[el]));
            auto actual=Point(rig.p[hand],Affine(palette[hand]));
            auto error=Sub(predicted,actual);
            Check(std::sqrt(Dot(error,error))<.002,"final forearm skin misses wrist endpoint");
        }
        auto same=before;Check(cache.Apply(same.data(),h[26],rig,head,plan->armBones,key),"repeat solve");
        Check(!memcmp(same.data(),palette.data(),sizeof(palette)),"stereo repeat differs");
        auto metadata=before;for(auto& m:metadata){m.m[3]=11;m.m[7]=12;m.m[11]=13;m.m[15]=14;}
        Check(cache.Apply(metadata.data(),h[26],rig,head,plan->armBones,key),"repeat metadata solve");
        for(unsigned i=0;i<h[26];i++)for(unsigned j=0;j<16;j++)
            Check(metadata[i].m[j]==(j%4==3?float(11+j/4):palette[i].m[j]),"cached draw overwrites metadata");
        if(trial){auto stale=before;auto staleKey=key;staleKey.frame--;Check(!cache.Apply(stale.data(),h[26],rig,head,plan->armBones,staleKey),"late draw rewound history");Check(!memcmp(stale.data(),before.data(),sizeof(stale)),"late draw writes");}
        auto fail=before;fail[130].m[12]=1e8f;auto saved=fail;
        Check(!Apply(fail.data(),h[26],rig,head,plan->armBones),"distant hand accepted");
        Check(!memcmp(fail.data(),saved.data(),sizeof(fail)),"failed solve partially writes");
    }
    for(unsigned reset=0;reset<12;reset++){
        ArmPoseHistory::Cache c;std::array<Matrix,223> input{};input.fill(CameraMath::Rotation({0,0,1,0}));
        for(unsigned hand:{101u,130u})input[hand]=Map(input[hand],rig.p[hand],{hand==101?-65.:65.,100.,490.});
        ArmPoseHistory::Key key{};key.frame=10;key.tracking=10;key.tick=1000;key.model=1;key.scale=300;
        auto first=input;Check(c.Apply(first.data(),h[26],rig,head,plan->armBones,key),"reset seed");
        key.frame++;key.tracking++;key.tick+=11;
        switch(reset){case 0:key.model++;break;case 1:key.scale=310;break;case 2:key.reference.position.x=1;break;
            case 3:key.frame+=2;break;case 4:key.tick+=300;break;case 5:key.scene++;break;case 6:c.Reset();break;case 7:key.tracking=1;break;case 8:key.player++;break;case 9:key.weapon++;break;case 11:key.epoch++;break;
            case 10:{auto invalidPose=input;invalidPose[130].m[12]=1e8f;auto saved=invalidPose;Check(!c.Apply(invalidPose.data(),h[26],rig,head,plan->armBones,key),"cache accepts impossible reach");Check(!memcmp(invalidPose.data(),saved.data(),sizeof(invalidPose)),"failed cache solve writes");key.frame++;key.tick++;break;}}
        input[130].m[12]+=3;auto expected=input,result=input;
        Check(Apply(expected.data(),h[26],rig,head,plan->armBones),"fresh reset reference");
        Check(c.Apply(result.data(),h[26],rig,head,plan->armBones,key),"reset solve");
        Check(!memcmp(expected.data(),result.data(),sizeof(result)),"context/gap reset differs from fresh");
    }

    for(int degrees:{-90,-45,45,90}){
        ArmPoseHistory::Cache turnCache;auto body=CameraMath::Rotation({0,0,1,0});std::array<Matrix,223> input{};input.fill(body);
        for(unsigned hand:{101u,130u})input[hand]=Map(body,rig.p[hand],{hand==101?-65.:65.,100.,490.});
        ArmPoseHistory::Key key{};key.frame=1;key.tracking=1;key.tick=1000;key.model=1;key.scale=300;
        auto prior=input;Check(turnCache.Apply(prior.data(),h[26],rig,head,plan->armBones,key),"turn prior");
        float angle=degrees*3.14159265358979323846f/180;auto turn=CameraMath::Rotation({0,0,std::sin(angle/2),std::cos(angle/2)});
        auto turnedHead=CameraMath::Multiply(head,turn);auto moved=input;
        for(unsigned i=0;i<h[26];i++)moved[i]=CameraMath::Multiply(moved[i],turn);
        key.frame++;key.tracking++;key.tick+=11;key.baseYaw=-angle;
        Check(turnCache.Apply(moved.data(),h[26],rig,turnedHead,plan->armBones,key),"turn history solve");
        for(unsigned bone:{96u,99u,101u,125u,128u,130u}){
            auto expected=Point(Point(rig.p[bone],prior[bone]),turn),actual=Point(rig.p[bone],moved[bone]);
            auto d=Sub(expected,actual);Check(std::sqrt(Dot(d,d))<.002,"artificial turn failed to rotate history with body");
        }
    }

    {
        ArmBodyHeading::State direction;ArmPoseHistory::Cache lookCache;
        auto body=CameraMath::Rotation({0,0,1,0});std::array<Matrix,223> input{};input.fill(body);
        for(unsigned hand:{101u,130u})input[hand]=Map(body,rig.p[hand],{hand==101?-65.:65.,100.,490.});
        auto expected=input;Check(Apply(expected.data(),h[26],rig,head,plan->armBones),"fixed-heading reference");
        unsigned frame=0;
        for(int yaw=-180;yaw<=180;yaw+=15)for(int pitch=-90;pitch<=90;pitch+=15){
            float y=yaw*3.14159265358979323846f/360,p=pitch*3.14159265358979323846f/360;
            auto looking=CameraMath::Multiply(CameraMath::Multiply(head,CameraMath::Rotation({0,0,std::sin(y),std::cos(y)})),CameraMath::Rotation({std::sin(p),0,0,std::cos(p)}));
            for(unsigned j=12;j<15;j++)looking.m[j]=head.m[j];
            ++frame;Matrix shoulder{};
            Check(direction.Update(looking,head,{1,2,3,300},frame,1000+frame*11,shoulder),"body-frame look");
            ArmPoseHistory::Key key{};key.frame=frame;key.tracking=frame;key.tick=1000+frame*11;key.model=1;key.scale=300;
            auto actual=input;Check(lookCache.Apply(actual.data(),h[26],rig,shoulder,plan->armBones,key),"integrated look solve");
            for(unsigned bone:{96u,99u,101u,125u,128u,130u}){
                auto d=Sub(Point(rig.p[bone],actual[bone]),Point(rig.p[bone],expected[bone]));
                Check(std::sqrt(Dot(d,d))<.001,"looking toward arms moved joints");
            }
            for(unsigned i=0;i<h[26];i++)if(Hand(i))Check(!memcmp(&actual[i],&input[i],64),"look changed hand");
        }
        printf("PASS %u integrated yaw/pitch look poses with fixed hands: stationary shoulders/elbows/wrists.\n",frame);
    }
    for(auto target:{V{0,0,0},V{0,0,-100},V{0,170,0}}){
        Joints j;Check(Solve({0,0,0},target,87.223,85.694,{0,0,-1},j),"singular solve");
        auto a=Sub(j.elbow,j.shoulder),b=Sub(j.wrist,j.elbow);
        Check(std::sqrt(Dot(j.shoulder,j.shoulder))<1e-8,"shoulder moved");
        Check(std::sqrt(Dot(a,a))<87.223*1.151&&std::sqrt(Dot(b,b))<85.694*1.151,"segment stretch limit");
    }
    std::vector<unsigned char> pose(16+h[26]*64);auto wrist=pose.data()+16+130*64;memset(wrist,0x35,64);
    HandOnlyGeometry::Activate(uintptr_t(pose.data()),10,plan,wrist,true,true);
    auto unchanged=bytes;unsigned drawn=0;
    uint32_t drawable[14]{};drawable[3]=addr(render);drawable[8]=addr(pose.data());
    auto draw=[&](void*object,void*,void*,void*,bool){
        auto d=static_cast<uint32_t*>(object);auto g=reinterpret_cast<uint32_t*>(d[6]);
        Check(object!=drawable,"shared drawable edited");drawn+=g[5];
    };
    for(const auto& part:plan->arms){
        drawable[5]=uint32_t(part.batch);drawable[6]=uint32_t(plan->groups)+part.group*64;drawable[13]=part.triangles<<8|2;
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"arm draw interception");
    }
    Check(drawn==total&&bytes==unchanged,"arm draw counts/shared writes");
    drawable[6]=uint32_t(plan->groups); // head group, must stay hidden
    Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw)&&drawn==total,"body leak");
    HandOnlyGeometry::Activate(uintptr_t(pose.data()),10,plan,wrist,true,false);
    for(const auto& part:plan->arms){
        drawable[5]=uint32_t(part.batch);drawable[6]=uint32_t(plan->groups)+part.group*64;drawable[13]=part.triangles<<8|2;
        Check(HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"fallback filtering");
    }
    Check(drawn==total,"failed arm pose leaks native arms");
    HandOnlyGeometry::Invalidate(uintptr_t(pose.data()));
    Check(!HandOnlyGeometry::DrawSelected(drawable,nullptr,nullptr,render,false,10,draw),"native third person filtered");
    printf("FullArmIK checks passed: %u bones, %u arm triangles, hand/curl preservation, fallback and draw isolation.\n",h[26],total);
    printf("PASS installed one-ULP rejection reproduced; revised per-draw solves preserve frozen history and current palms.\n");
    return 0;
}catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}}
