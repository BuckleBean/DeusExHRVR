#pragma once
#include "ModelHandAlignment.h"
#include <mutex>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>

namespace ModelWeaponGrips {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // CameraMath::Matrix intentionally requires 16-byte alignment.
#endif
using ModelHandAlignment::Matrix;
inline bool Rigid(const Matrix& m){
    if(!ModelHandAlignment::Finite(m)||std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])>.001f||std::abs(m.m[15]-1)>.001f)return false;
    for(int i=0;i<3;i++)for(int j=0;j<3;j++){
        float d=0;for(int k=0;k<3;k++)d+=m.m[i*4+k]*m.m[j*4+k];
        if(std::abs(d-(i==j?1.f:0.f))>.02f)return false;
    }
    using namespace ModelHandAlignment;
    return Dot(Cross({m.m[0],m.m[1],m.m[2]},{m.m[4],m.m[5],m.m[6]}),{m.m[8],m.m[9],m.m[10]})>.98;
}
inline float PositionDistance2(const Matrix& a,const Matrix& b){float d=0;for(int j=12;j<15;j++){float v=a.m[j]-b.m[j];d+=v*v;}return d;}
inline float RotationDistance2(const Matrix& a,const Matrix& b){float d=0;for(int i=0;i<3;i++)for(int j=0;j<3;j++){float v=a.m[i*4+j]-b.m[i*4+j];d+=v*v;}return d;}
inline bool OffsetValid(const Matrix& m){Matrix origin{};return Rigid(m)&&PositionDistance2(m,origin)<10000.f*10000.f;}
// N: native muzzle, S: native palm skin, B: legacy learned anchor, F: model frame.
// Old O=N*inverse(B*S). New O=N*inverse(F*S)=oldO*B*inverse(F).
inline bool Convert(const Matrix& old,const Matrix& bind,const Matrix& frame,Matrix& result){
    Matrix inv{};if(!OffsetValid(old)||!Rigid(bind)||!Rigid(frame)||!ModelHandAlignment::Invert(frame,inv))return false;
    auto converted=CameraMath::Multiply(CameraMath::Multiply(old,bind),inv);
    if(!OffsetValid(converted))return false;result=converted;return true;
}
inline bool Candidate(const Matrix& native,const Matrix& palm,Matrix& result){
    Matrix inv{};if(!Rigid(native)||!Rigid(palm)||!ModelHandAlignment::Invert(palm,inv))return false;
    auto candidate=CameraMath::Multiply(native,inv);if(!OffsetValid(candidate))return false;result=candidate;return true;
}
struct Record{uint32_t key{};Matrix offset{},anchor{};};
class Store {
    std::mutex mutex;
    std::array<Record,64> records{};unsigned count{};
    uint64_t revision{},savedRevision{},lastFrame{},lastTick{};
    uint32_t solving=UINT32_MAX;bool havePrevious{};
    Matrix previous{},sum{};ModelHandAlignment::V lastRelative{};unsigned samples{};
    uint64_t seeded{},solved{},refined{},rejected{},used{};
    Record* Find(uint32_t key){for(unsigned i=0;i<count;i++)if(records[i].key==key)return &records[i];return nullptr;}
    void ClearWindow(){havePrevious=false;samples=0;sum={};}
public:
    bool Seed(uint32_t key,const Matrix& legacy,const Matrix& bind,const Matrix& frame){
        std::lock_guard lock(mutex);if(Find(key))return true;
        Matrix value{};if(key>=0x18000||count==64||!Convert(legacy,bind,frame,value)){++rejected;return false;}
        records[count++]={key,value,value};++revision;++seeded;return true;
    }
    bool Get(uint32_t key,Matrix& result){std::lock_guard lock(mutex);auto* r=Find(key);if(!r)return false;result=r->offset;++used;return true;}
    void Cancel(){std::lock_guard lock(mutex);ClearWindow();lastTick=0;solving=UINT32_MAX;}
    bool Observe(uint32_t key,uint64_t frameId,uint64_t tick,bool eligible,const Matrix& native,
                 const Matrix& palm,const ModelHandAlignment::V& body,float scale){
        std::lock_guard lock(mutex);
        if(!eligible||key>=0x18000||!std::isfinite(scale)||scale<=0){ClearWindow();++rejected;return false;}
        if(frameId==lastFrame)return false; // stereo/repeated draw, even if tick advanced
        if(key!=solving||tick<lastTick||tick-lastTick>=250){ClearWindow();solving=key;}
        lastFrame=frameId;lastTick=tick;
        Matrix cand{};if(!Candidate(native,palm,cand)){ClearWindow();++rejected;return false;}
        ModelHandAlignment::V relative{};
        for(int j=0;j<3;j++){if(!std::isfinite(body[j])){ClearWindow();return false;}relative[j]=palm.m[12+j]-body[j];}
        if(!havePrevious){previous=cand;lastRelative=relative;havePrevious=true;return false;}
        auto movement=ModelHandAlignment::Sub(relative,lastRelative);
        if(ModelHandAlignment::Dot(movement,movement)<scale*scale*.02*.02)return false;
        bool agree=RotationDistance2(cand,previous)<.02f&&PositionDistance2(cand,previous)<scale*scale*.015f*.015f;
        previous=cand;lastRelative=relative;
        if(!agree){samples=0;sum={};++rejected;return false;}
        for(int j=0;j<16;j++)sum.m[j]+=cand.m[j];
        if(++samples<8)return false;
        auto average=sum;for(auto& v:average.m)v/=float(samples);
        using namespace ModelHandAlignment;
        V x{average.m[0],average.m[1],average.m[2]},y{average.m[4],average.m[5],average.m[6]};
        if(!Normalize(x)){ClearWindow();return false;}y=Sub(y,Scale(x,Dot(x,y)));
        if(!Normalize(y)){ClearWindow();return false;}auto z=Cross(x,y);
        for(int j=0;j<3;j++){average.m[j]=float(x[j]);average.m[4+j]=float(y[j]);average.m[8+j]=float(z[j]);}
        average=Affine(average);ClearWindow();
        if(!OffsetValid(average)){++rejected;return false;}
        auto* r=Find(key);
        if(r){
            float limit=scale*4.f/39.3701f;
            // Bound against original load/seed anchor, not each previous refinement.
            if(PositionDistance2(average,r->anchor)>limit*limit||RotationDistance2(average,r->anchor)>.1363f){++rejected;return false;}
            if(PositionDistance2(average,r->offset)<scale*scale*.004f*.004f&&RotationDistance2(average,r->offset)<.0001f)return false;
            r->offset=average;++refined;
        }else {if(count==64)return false;records[count++]={key,average,average};++solved;}
        ++revision;return true;
    }
    void Load(const char* path="DeusExHRVR-model-grips.cal"){
        std::lock_guard lock(mutex);std::ifstream f(path);std::string line;
        if(!std::getline(f,line)||line!="model-grips-v1 palm130 frame1")return;
        while(std::getline(f,line)){
            std::istringstream row(line);char tag{};uint32_t key{};Matrix m{};
            if(!(row>>tag>>std::hex>>key>>std::dec)||tag!='g'||key>=0x18000)continue;
            bool valid=true;for(float& v:m.m)if(!(row>>v)){valid=false;break;}
            std::string extra;if(row>>extra)valid=false;
            if(valid&&OffsetValid(m)&&!Find(key)&&count<64)records[count++]={key,m,m};
        }
    }
    void Flush(const char* path="DeusExHRVR-model-grips.cal",const char* log="DeusExHRVR-model-grips.log"){
        // Called only by the existing background watcher. No file IO in draw hooks.
        std::lock_guard lock(mutex);
        if(revision!=savedRevision){
            std::string tmp=std::string(path)+".tmp";FILE* f{};
            if(!fopen_s(&f,tmp.c_str(),"w")&&f){
                fprintf(f,"model-grips-v1 palm130 frame1\n");
                for(unsigned i=0;i<count;i++){fprintf(f,"g %08x",records[i].key);for(float v:records[i].offset.m)fprintf(f," %.9g",v);fprintf(f,"\n");}
                bool good=!ferror(f);if(fclose(f))good=false;
                if(good&&MoveFileExA(tmp.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))savedRevision=revision;
            }
        }
        FILE* f{};if(!fopen_s(&f,log,"a")&&f){fprintf(f,"modelGrips count=%u seeded=%llu solved=%llu refined=%llu rejected=%llu used=%llu pendingSave=%d\n",count,seeded,solved,refined,rejected,used,int(revision!=savedRevision));fclose(f);}
    }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
