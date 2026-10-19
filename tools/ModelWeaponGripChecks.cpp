#include "ModelWeaponGrips.h"
#include <random>
#include <stdexcept>
#include <filesystem>
using namespace ModelWeaponGrips;
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
Matrix Identity(){Matrix m{};m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;return m;}
bool Near(const Matrix& a,const Matrix& b,float tolerance=.004f){for(int j=0;j<16;j++)if(std::abs(a.m[j]-b.m[j])>tolerance)return false;return true;}
int main(int argc,char** argv){try{
    if(argc==3){
        std::ifstream hands(argv[1]),grips(argv[2]);std::string line;Matrix bind{};bool found=false;
        while(std::getline(hands,line)){std::istringstream row(line);char tag{};unsigned index{};if(row>>tag>>index && tag=='b' && index==130){for(auto& v:bind.m)Check(bool(row>>v),"read calibration");found=true;break;}}
        Check(found,"actual palm calibration found");
        Matrix frame{{-.579404238f,-.062891331f,.812610244f,0, .795649170f,.172542985f,.580664547f,0, -.176728963f,.982992166f,-.049932701f,0, -222.427796f,-18.315311f,403.403194f,1}};
        unsigned converted=0;
        while(std::getline(grips,line)){std::istringstream row(line);char tag{};unsigned key{};if(!(row>>tag>>std::hex>>key)||tag!='g')continue;Matrix old{},value{};for(auto& v:old.m)Check(bool(row>>v),"read actual grip");Check(Convert(old,bind,frame,value),"actual saved grip converts to valid rigid model offset");++converted;}
        Check(converted>0,"actual stored grips present");printf("PASS: all %u actual stored weapon grips convert to valid model-frame offsets\n",converted);
    }
    std::mt19937 rng(131913);std::uniform_real_distribution<float> d(-1,1);
    auto random=[&]{Transport::Quaternion q{d(rng),d(rng),d(rng),d(rng)};float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);q.x/=n;q.y/=n;q.z/=n;q.w/=n;auto m=CameraMath::Rotation(q);for(int j=12;j<15;j++)m.m[j]=d(rng)*500;return m;};
    for(unsigned test=0;test<1000;test++){
        auto bind=random(),frame=random(),skin=random(),native=random(),target=random();
        auto oldPalm=CameraMath::Multiply(bind,skin),newPalm=CameraMath::Multiply(frame,skin);
        Matrix old{},converted{},direct{},inv{};
        Check(Candidate(native,oldPalm,old)&&Convert(old,bind,frame,converted)&&Candidate(native,newPalm,direct),"solve both reference frames");
        Check(Near(converted,direct),"legacy-to-model conversion equals direct native solve");
        Check(ModelHandAlignment::Invert(newPalm,inv),"invert native palm");
        auto delta=CameraMath::Multiply(inv,target);
        auto movedNative=CameraMath::Multiply(native,delta),placed=CameraMath::Multiply(converted,target);
        Check(Near(movedNative,placed),"gun follows exactly the hand delta");
        Check(Near(CameraMath::FiringFromMuzzle(placed),CameraMath::FiringFromMuzzle(movedNative)),"firing transform matches moved native muzzle");
    }
    Store store;auto identity=Identity(),offset=Identity();offset.m[12]=20;
    Check(store.Seed(1,offset,identity,identity),"seed known grip");
    Matrix got{};Check(store.Get(1,got)&&Near(got,offset),"seed readback");
    auto different=offset;different.m[12]=30;
    Check(store.Seed(1,different,identity,identity)&&store.Get(1,got)&&Near(got,offset),"repeated seed cannot overwrite a grip");
    uint64_t frame=1,tick=1000;
    auto sample=[&](uint32_t key,float palmX,float nativeExtra,bool eligible=true){
        auto palm=Identity();palm.m[12]=palmX;auto candidate=offset;candidate.m[12]+=nativeExtra;
        auto native=CameraMath::Multiply(candidate,palm);
        return store.Observe(key,frame++,tick+=10,eligible,native,palm,{0,0,0},300);
    };
    for(int i=0;i<40;i++)sample(2,0,0);
    Check(!store.Get(2,got),"stationary holstered-like pose never learned");
    store.Cancel();
    for(int i=0;i<9;i++)sample(2,float(i*7),0);
    Check(store.Get(2,got)&&Near(got,offset),"eight consistent moving samples solve unknown weapon");
    store.Cancel();for(int i=0;i<9;i++)sample(1,float(i*7),5);
    Check(store.Get(1,got)&&std::abs(got.m[12]-25)<.001f,"bounded refinement updates model grip");
    store.Cancel();for(int i=0;i<9;i++)sample(1,float(i*7),100);
    Check(store.Get(1,got)&&std::abs(got.m[12]-25)<.001f,"large drift rejected against original anchor");
    store.Cancel();for(int i=0;i<20;i++)sample(3,float(i*7),i%2?50.f:0.f);
    Check(!store.Get(3,got),"transition/inconsistent candidate rejected");
    store.Cancel();for(int i=0;i<7;i++)sample(4,float(i*7),0);
    sample(4,49,0,false);sample(4,56,0);sample(4,63,0);
    Check(!store.Get(4,got),"suspension clears partial solve");
    store.Cancel();for(int i=0;i<7;i++)sample(5,float(i*7),0);
    tick+=300;sample(5,49,0);sample(5,56,0);
    Check(!store.Get(5,got),"stale window reset");
    // Walking the body and a holstered weapon together is not hand-relative motion.
    store.Cancel();for(int i=0;i<20;i++){
        auto p=Identity();p.m[12]=float(i*10);auto n=CameraMath::Multiply(offset,p);
        store.Observe(6,frame++,tick+=10,true,n,p,{double(i*10),0,0},300);
    }
    Check(!store.Get(6,got),"body translation cannot qualify a holstered solve");
    auto path="2026-10-16-model-grips/test-model-grips.cal",log="2026-10-16-model-grips/test-model-grips.log";
    store.Flush(path,log);Store loaded;loaded.Load(path);
    Check(loaded.Get(1,got)&&std::abs(got.m[12]-25)<.001f,"separate model-file round trip");
    Check(loaded.Get(2,got)&&Near(got,offset),"multiple weapons persisted");
    Matrix invalid=identity;invalid.m[0]=0;Check(!Convert(invalid,identity,identity,got),"singular legacy offset rejected");
    printf("PASS: 1000 conversion/direct-solve/hand-delta/firing cases; seeds; moving-window solve; refinement bounds; transition, suspension, stale and body-motion rejection; separate persistence\n");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
