#define HUD_ISOLATION_TEST
#include "HudIsolationCheck.h"
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <limits>
using namespace HudIsolationCheck;
// These checks drive the real dispatch in HudIsolationCheck.h: Read and Write
// call slot 23/24 of whichever wrapper the controller holds. The wrappers here
// are stand-ins. The child stand-in models native 0x7298F0 as disassembled; it
// is a model of the game, not the game, so native GFx calls and rendering
// remain untested. The recovery checks likewise run against stand-ins for the
// engine's device state, its matrix composition and its HUD vertex shader.
namespace {
std::map<std::string,double> values;   // keyed by the path the ROOT movie receives
std::vector<std::string> rootPaths;    // every path the root was asked for
int reads{},writes{};
bool rejectOffset{};
struct FakeChild {
    uintptr_t vt{};char padding[0x3c]{};void* parent{};char padding2[0xc]{};const char* prefix{};
};
static_assert(offsetof(FakeChild,parent)==0x40 && offsetof(FakeChild,prefix)==0x50);
bool __fastcall RootGet(void*,void*,Value* out,const char* path) {
    ++reads;rootPaths.push_back(path);
    auto i=values.find(path);if(i==values.end())return false;
    // As the game does (revision 3/5 runs): _visible comes back as a boolean
    // (type 2, the flag in the first byte), not a number.
    const std::string p(path);
    if(p.size()>9 && p.compare(p.size()-9,9,"._visible")==0) {
        out->type=2;out->number=0;const unsigned char b=i->second!=0;memcpy(&out->number,&b,1);return true;
    }
    out->type=3;out->number=i->second;return true;
}
bool __fastcall RootSet(void*,void*,const char* path,const Value* in) {
    ++writes;rootPaths.push_back(path);
    if(rejectOffset && in->number==250)return false;
    values[path]=in->number;return true;
}
// Native 0x7298F0: with a non-empty prefix, "_root." is dropped and the rest is
// joined as "%s.%s"; with no prefix the path is passed on unchanged.
std::string Qualify(const FakeChild* child,const char* path) {
    if(!child->prefix || !*child->prefix)return path;
    const char* rest=strncmp(path,"_root.",6)?path:path+6;
    return std::string(child->prefix)+"."+rest;
}
bool __fastcall ChildGet(void* self,void*,Value* out,const char* path) {
    auto child=static_cast<FakeChild*>(self);
    const auto qualified=Qualify(child,path);
    const auto table=*reinterpret_cast<const uintptr_t* const*>(child->parent);
    return reinterpret_cast<Getter>(table[23])(child->parent,out,qualified.c_str());
}
bool __fastcall ChildSet(void* self,void*,const char* path,const Value* in) {
    auto child=static_cast<FakeChild*>(self);
    const auto qualified=Qualify(child,path);
    const auto table=*reinterpret_cast<const uintptr_t* const*>(child->parent);
    return reinterpret_cast<Setter>(table[24])(child->parent,qualified.c_str(),in);
}
int nativeUpdates{};
void __fastcall NoUpdate(void*,void*,float) {++nativeUpdates;}
void __fastcall NoInit(void*,void*,void*,void*) {}
int submitted{};
void __fastcall CountPrimitive(void*,void*,void*,bool,uint32_t) { ++submitted; }
struct Config {void* a{};void* b{};const char* component{};const char* functionPrefix{};};
static_assert(offsetof(Config,component)==8 && offsetof(Config,functionPrefix)==0xC);
struct Controller {uintptr_t vt{};char padding[0x1c]{};void* movie{};void* reserved{};void* config{};};
static_assert(offsetof(Controller,movie)==0x20 && offsetof(Controller,config)==0x28);
void ResetPreview() {
    phase=Phase::WaitHud;period=-1;hudSeen=phaseStart=settledAt=0;capturedPresents=0;
    capturing=false;capturesWritten=0;mode=0;
    for(auto& row:periodApplied)for(auto& cell:row)cell=false;
    for(auto& count:drawCount)count=0;
}
// out = a*b for row-major matrices applied to row vectors.
void Multiply(const float* a,const float* b,float* out) {
    float r[16];
    for(int i=0;i<4;++i)for(int j=0;j<4;++j) {
        double v=0;for(int k=0;k<4;++k)v+=double(a[i*4+k])*double(b[k*4+j]);
        r[i*4+j]=float(v);
    }
    memcpy(out,r,64);
}
// Stand-in for native 0x550930 as disassembled: WVP = World * View * Projection,
// using the override projection when one is set, then the dirty flags clear.
float fakeWvp[16];
int matricesCalls{};
void __fastcall FakeMatrices(void* self,void*) {
    ++matricesCalls;
    auto s=static_cast<unsigned char*>(self);
    auto overrideProjection=*reinterpret_cast<float**>(s+0x540);
    float vp[16];
    Multiply(reinterpret_cast<float*>(s+0x480),overrideProjection?overrideProjection:reinterpret_cast<float*>(s+0x440),vp);
    Multiply(reinterpret_cast<float*>(s+0x500),vp,fakeWvp);
    s[0x544]=s[0x545]=s[0x546]=0;
}
// Stand-in for the engine submitting the draw: remembers the WVP it would use.
// It also leaves the draw's X and Y rows in the stand-in for buffer 4, as the
// first preview run showed the engine does.
float drawnWvp[16];
float* shadowRows{};
const float* currentX{};
const float* currentY{};
void __fastcall DrawPrimitive(void*,void*,void*,bool,uint32_t) {
    ++submitted;memcpy(drawnWvp,fakeWvp,64);
    if(shadowRows && currentX && currentY){memcpy(shadowRows,currentX,16);memcpy(shadowRows+4,currentY,16);}
}
// What the three HUD vertex shaders compute (read from their disassembly).
void Clip(const float* xRow,const float* yRow,const float* depthRow,const float* wvp,const float* v,double* out) {
    double X=0,Y=0,Z=0;
    for(int i=0;i<4;++i){X+=double(xRow[i])*v[i];Y+=double(yRow[i])*v[i];Z+=double(depthRow[i])*v[i];}
    for(int j=0;j<4;++j)out[j]=X*wvp[j]+Y*wvp[4+j]+Z*wvp[8+j]+wvp[12+j];
}
bool SameClip(const double* a,const double* b) {
    // Clip values here are in the tens of thousands; 0.25 is about 1e-5 of the
    // screen. Single-precision rounding of the parked values is well inside it.
    for(int j=0;j<4;++j)if(std::abs(a[j]-b[j])>0.25)return false;
    return true;
}
}
int main() {
    std::vector<unsigned char> memory(0x1000000);
    image=reinterpret_cast<uintptr_t>(memory.data());
    BindNative();
    assert(rootVtable==VA(0xAB022C) && childVtable==VA(0xAAFDCC));
    assert(rootGet==VA(0x72F8E0) && rootSet==VA(0x72F8F0));
    assert(childGet==VA(0x729DC0) && childSet==VA(0x729E20));
    // Point the expected slot addresses at the stand-ins so the vtable guard
    // and the dispatch are both the production code.
    rootGet=reinterpret_cast<uintptr_t>(&RootGet);rootSet=reinterpret_cast<uintptr_t>(&RootSet);
    childGet=reinterpret_cast<uintptr_t>(&ChildGet);childSet=reinterpret_cast<uintptr_t>(&ChildSet);
    auto rootTable=reinterpret_cast<uintptr_t*>(rootVtable);
    auto childTable=reinterpret_cast<uintptr_t*>(childVtable);
    rootTable[23]=rootGet;rootTable[24]=rootSet;
    childTable[23]=childGet;childTable[24]=childSet;
    // Large enough for the diagnostics to read the fields a real wrapper has.
    uintptr_t rootObject[0x20]{};uintptr_t& movie=rootObject[0];movie=rootVtable;
    Controller controller{VA(0xABBD3C),{},&movie};
    originalUpdate=reinterpret_cast<Update>(&NoUpdate);
    originalInit=reinterpret_cast<Init>(&NoInit);
    logLines=64; // No filesystem logs in tests.
    auto reset=[&]{values.clear();rootPaths.clear();reads=writes=0;rejectOffset=false;Reset(slots[0],&controller,controller.movie);};

    // ---- Root wrapper: the earlier failure and lifecycle checks, unchanged in meaning.
    reset();for(int n=0;n<130;++n)UpdateHook(&controller,nullptr,0.01f);
    assert(writes==0 && reads==120 && slots[0].finished);
    reset();values[slots[0].path]=10;
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[0].path]==250 && slots[0].applied);
    for(int n=0;n<120;++n)UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].finished && writes==2);
    InitHook(&controller,nullptr,&movie,nullptr);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[0].path]==250 && slots[0].original==10); // no drift
    values[slots[0].path]=20;auto before=writes;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].finished && values[slots[0].path]==20 && writes==before);
    reset();values[slots[0].path]=10;rejectOffset=true;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].finished && !slots[0].applied && values[slots[0].path]==10);
    reset();values[slots[0].path]=std::numeric_limits<double>::quiet_NaN();
    UpdateHook(&controller,nullptr,0.01f);assert(writes==0);
    reset();controller.vt=0;
    UpdateHook(&controller,nullptr,0.01f);assert(reads==0 && writes==0);
    controller.vt=VA(0xABBD3C);movie=0;
    UpdateHook(&controller,nullptr,0.01f);assert(reads==0 && writes==0);
    movie=rootVtable;
    // A root whose slots are not the expected functions is refused untouched.
    rootTable[23]=rootSet;
    reset();values[slots[0].path]=10;
    UpdateHook(&controller,nullptr,0.01f);assert(reads==0 && writes==0 && !slots[0].applied);
    rootTable[23]=rootGet;
    for(int index=0;index<3;++index) {
        auto& s=slots[index];controller.vt=VA(s.vtable);
        values.clear();values[s.path]=500;rejectOffset=false;
        Reset(s,&controller,&movie);UpdateHook(&controller,nullptr,0.01f);
        assert(s.applied && values[s.path]==500+s.offset);
        for(auto& pair:values)assert(pair.first==s.path || pair.first.find(".__dxhrvrHudIsolationOriginalX")!=std::string::npos);
    }

    // ---- Child wrapper with a prefix: the defect found on November 12.
    // The root must only ever be asked for the qualified path.
    controller.vt=VA(slots[0].vtable);
    FakeChild child{};child.vt=childVtable;child.parent=&movie;child.prefix="hud_mc.healthHolder_mc";
    controller.movie=&child;
    const std::string qualified="hud_mc.healthHolder_mc.mainHealthBar_mc.healthBar_mc._x";
    reset();values[qualified]=40;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].applied && values[qualified]==40+240);
    assert(!rootPaths.empty());
    for(auto& path:rootPaths)assert(path.rfind("hud_mc.healthHolder_mc.",0)==0);
    assert(values.find(slots[0].path)==values.end());
    // The authored "_root..." location is NOT where a prefixed child's clip
    // lives. The earlier unwrap-to-parent code would have read and moved this.
    reset();values[slots[0].path]=40;
    UpdateHook(&controller,nullptr,0.01f);
    assert(!slots[0].applied && writes==0 && values[slots[0].path]==40);
    // A child with no prefix passes the authored path through unchanged.
    child.prefix="";
    reset();values[slots[0].path]=40;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].applied && values[slots[0].path]==280);
    child.prefix=nullptr;
    reset();values[slots[0].path]=40;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].applied && values[slots[0].path]==280);
    // Nested children each add their own prefix, outermost first.
    child.prefix="outer_mc";
    FakeChild inner{};inner.vt=childVtable;inner.parent=&child;inner.prefix="inner_mc";
    controller.movie=&inner;
    const std::string nested="outer_mc.inner_mc.mainHealthBar_mc.healthBar_mc._x";
    reset();values[nested]=40;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].applied && values[nested]==280);
    // A child whose slots are not the expected functions is refused untouched.
    childTable[24]=childGet;
    reset();values[nested]=40;
    UpdateHook(&controller,nullptr,0.01f);assert(reads==0 && writes==0 && !slots[0].applied);
    childTable[24]=childSet;
    // An object that is neither wrapper is refused untouched.
    uintptr_t stranger=VA(0xABBD3C);controller.movie=&stranger;
    reset();UpdateHook(&controller,nullptr,0.01f);assert(reads==0 && writes==0);
    controller.movie=&movie;

    // ---- Desktop requests, root wrapper.
    desktopMode=true;Request(0,0,0);
    controller.vt=VA(slots[0].vtable);values.clear();values[slots[0].path]=10;
    Reset(slots[0],&controller,&movie);
    assert(!resolved[0]);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[0].path]==10 && std::string(status[0])=="BEFORE readback OK");
    assert(resolved[0] && appliedGeneration[0].load()==generation.load());
    Request(480,480,-480);
    assert(appliedGeneration[0].load()!=generation.load());
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[0].path]==490 && std::string(status[0])=="AFTER readback OK");
    assert(appliedGeneration[0].load()==generation.load());
    Request(0,0,0);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[0].path]==10);
    // Within one request, a value the game changed back is reported, not fought.
    values[slots[0].path]=99;auto unchangedWrites=writes;
    UpdateHook(&controller,nullptr,0.01f);
    assert(slots[0].finished && writes==unchangedWrites && std::string(status[0])=="FAILED native override");
    // Missing lookup: one read, two ancestor probes, then one retry at update 61.
    Reset(slots[0],&controller,&movie);values.clear();rootPaths.clear();reads=writes=0;
    for(int n=0;n<120;++n)UpdateHook(&controller,nullptr,0.01f);
    assert(reads==4 && writes==0 && std::string(status[0])=="UNRESOLVED property");
    assert(rootPaths[1]=="_root.mainHealthBar_mc._x" && rootPaths[2]=="_root._x");
    assert(!resolved[0]);
    controller.vt=VA(0xABF63C);values.clear();values[slots[2].path]=600;
    Reset(slots[2],&controller,&movie);Request(480,480,-480);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[slots[2].path]==120 && std::string(status[2])=="AFTER readback OK");

    // ---- Desktop requests through a prefixed child, with a live config.
    Config config{};config.component="_root.mainHealthBar_mc.healthBar_mc";config.functionPrefix="Health_";
    controller.vt=VA(slots[0].vtable);controller.config=&config;
    child.prefix="hud_mc.healthHolder_mc";controller.movie=&child;
    values.clear();rootPaths.clear();values[qualified]=10;Request(0,0,0);
    Reset(slots[0],&controller,&child);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[qualified]==10 && std::string(status[0])=="BEFORE readback OK");
    Request(480,480,-480);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[qualified]==490 && std::string(status[0])=="AFTER readback OK");
    for(auto& path:rootPaths)assert(path.rfind("hud_mc.healthHolder_mc.",0)==0);
    compareRequested=true;
    Toggle();assert(!compareRequested);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[qualified]==10 && std::string(status[0])=="BEFORE readback OK");
    Toggle();assert(compareRequested);
    UpdateHook(&controller,nullptr,0.01f);
    assert(values[qualified]==490 && std::string(status[0])=="AFTER readback OK");
    // Diagnostics tolerate unreadable game pointers instead of faulting.
    char text[32];
    SafeText(reinterpret_cast<const void*>(uintptr_t(8)),text,sizeof(text));
    assert(std::string(text)=="(unreadable)");
    assert(SafePointer(reinterpret_cast<void*>(uintptr_t(8)),0)==nullptr);
    controller.config=reinterpret_cast<void*>(uintptr_t(8));
    Reset(slots[0],&controller,&child);
    UpdateHook(&controller,nullptr,0.01f);
    assert(std::string(status[0])=="AFTER readback OK");

    // ---- Recovery preview against a stand-in for the engine device state.
    std::vector<unsigned char> device(0x200),state(0x600);
    *reinterpret_cast<unsigned char**>(VA(0x12ab940))=device.data();
    *reinterpret_cast<unsigned char**>(device.data()+0x150)=state.data();
    // View and the two region projections are the values the census recorded.
    const float view[16]={1,0,0,0, 0,-1,0,0, 0,0,-1,0, -12800,7200,-24588.57f,1};
    const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float topLeft[16]={0.6403273f,0,0,0, 0,1.140472f,0,0, 0.6666667f,-0.6660482f,-1.00001f,-1, 0,0,-1.00001f,0};
    float bottomRight[16];memcpy(bottomRight,topLeft,64);bottomRight[8]=-0.6666667f;bottomRight[9]=0.6660482f;
    float centre[16];memcpy(centre,topLeft,64);centre[8]=0;centre[9]=0.6660482f;
    float full[16]={1.91698f,0,0,0, 0,3.415079f,0,0, 0,0,-1.00001f,-1, 0,0,-1.00001f,0};
    memcpy(state.data()+0x440,full,64);memcpy(state.data()+0x480,view,64);memcpy(state.data()+0x500,identity,64);
    auto& overrideProjection=*reinterpret_cast<float**>(state.data()+0x540);
    state[0x5ea]=1;state[0x5e9]=1;
    float shadowXY[8]{},shadowDepth[4]{};shadowRows=shadowXY;
    uint32_t buffer0[5]{},buffer3[5]{},buffer4[5]{};
    buffer0[2]=uint32_t(reinterpret_cast<uintptr_t>(fakeWvp));buffer0[3]=12;
    buffer3[2]=uint32_t(reinterpret_cast<uintptr_t>(shadowDepth));buffer3[3]=42;
    buffer4[2]=uint32_t(reinterpret_cast<uintptr_t>(shadowXY));buffer4[3]=8;
    *reinterpret_cast<uint32_t**>(state.data()+0x5a8)=buffer0;
    *reinterpret_cast<uint32_t**>(state.data()+0x5a8+12)=buffer3;
    *reinterpret_cast<uint32_t**>(state.data()+0x5a8+16)=buffer4;
    float constants[12]{};
    unsigned char primitiveState[0x40]{},primitive[0x40]{};
    *reinterpret_cast<uint32_t*>(primitiveState)=0x1442;
    *reinterpret_cast<uintptr_t*>(primitiveState+0xc)=0xAAAA;
    *reinterpret_cast<float**>(primitiveState+0x20)=constants;
    *reinterpret_cast<unsigned char**>(primitive+0x10)=primitiveState;
    *reinterpret_cast<uint32_t*>(primitive+0x28)=6;
    matrices=reinterpret_cast<Matrices>(&FakeMatrices);
    originalPrimitive=reinterpret_cast<Primitive>(&DrawPrimitive);
    ResetPreview();

    // Classification, on numbers taken from the census log.
    {
        float sine{};
        const float healthParked[12]={-0.0743f,0,0.9396926f,40890.f, 0,0,0,1, 0,0,0,0};
        const float energyParked[12]={0.0116f,0,0.9396926f,61400.f, 0,0,0,1, 0,0,0,0};
        const float ammoParked[12]={0.0247f,0,0.9396926f,40500.f, 0,0,0,1, 0,0,0,0};
        const float atHome[12]={-0.0743f,0,0.9396926f,875.59f, 0,0,0,1, 0,0,0,0};
        const float untilted[12]={0,0,1,2303.386f, 0,0,0,1, 0,0,0,0};
        assert(Classify(topLeft,healthParked,sine)==1 && std::abs(sine-0.3420201f)<1e-5f);
        assert(Classify(topLeft,energyParked,sine)==2 && sine>0);
        assert(Classify(bottomRight,ammoParked,sine)==3 && std::abs(sine+0.3420201f)<1e-5f);
        // Every other draw the census saw was between -807 and 2,303.
        assert(Classify(topLeft,atHome,sine)==0 && Classify(bottomRight,atHome,sine)==0);
        assert(Classify(full,untilted,sine)==0 && Classify(topLeft,untilted,sine)==0);
        // Right number, wrong region: not ours.
        assert(Classify(bottomRight,energyParked,sine)==0);
        assert(Classify(centre,healthParked,sine)==0 && Classify(full,healthParked,sine)==0);
        float broken[12];memcpy(broken,healthParked,48);broken[3]=std::numeric_limits<float>::quiet_NaN();
        assert(Classify(topLeft,broken,sine)==0);
        memcpy(broken,healthParked,48);broken[7]=0;assert(Classify(topLeft,broken,sine)==0);
        // Changed November 13 (hackfix): the third vector may be non-zero. After
        // a hack every HUD draw carries values there, and rejecting them hid the HUD.
        memcpy(broken,healthParked,48);broken[9]=0.5f;assert(Classify(topLeft,broken,sine)==1);
        float brokenProjection[16];memcpy(brokenProjection,topLeft,64);
        brokenProjection[8]=std::numeric_limits<float>::infinity();
        assert(Classify(brokenProjection,healthParked,sine)==0);
    }

    // End to end through the hook, with the shader's arithmetic and the
    // matrix composition modelled as disassembled. This checks this file's
    // arithmetic and bookkeeping. It is NOT evidence that the game behaves so.
    // The model follows what the first preview run measured in the game: the X
    // row is the depth row times a per-region ratio (3.57169 top left, -2.74748
    // bottom right) plus a constant, and parking by D movie units moves the
    // depth row by sine*D*20.
    const float cosine=0.9396926f,tilt=0.3420201f;
    struct Element { int index; float* projection; float sign; float ratio; float x[4]; float y[4]; float k; };
    Element elements[3]={
        {0,topLeft,1.f,3.57169f,{0.8f,0,0,1916.2f},{0,0.8f,0,963.2f},55.f},
        {1,topLeft,1.f,3.57169f,{-0.115252f,0.01f,0,2610.5f},{0.02f,0.0960264f,0,1820.f},-140.f},
        {2,bottomRight,-1.f,-2.74748f,{1.f,0,0,3100.f},{0,1.f,0,-1213.f},12.f}};
    const float vertices[4][4]={{0,0,0,1},{1000,0,0,1},{0,700,0,1},{-350,420,0,1}};
    auto depthOf=[&](const Element& e,const float* xRow,float* out) {
        out[0]=xRow[0]/e.ratio;out[1]=xRow[1]/e.ratio;out[2]=cosine;out[3]=xRow[3]/e.ratio+e.k;
    };
    // How far the X row moves for a native move of `units` movie units.
    auto rowMove=[&](const Element& e,int units) { return e.ratio*e.sign*tilt*float(units)*20.f; };
    // No ratio has been seen yet: a parked draw must be left alone, and counted.
    {
        const Element& e=elements[0];overrideProjection=e.projection;
        memcpy(state.data()+0x500,identity,64);state[0x546]=1;FakeMatrices(state.data(),nullptr);
        float homeWvp[16];memcpy(homeWvp,fakeWvp,64);
        float haveX[4];memcpy(haveX,e.x,16);haveX[3]+=rowMove(e,park[0]);
        float haveDepth[4];depthOf(e,haveX,haveDepth);memcpy(constants,haveDepth,16);constants[7]=1;
        currentX=haveX;currentY=e.y;mode=int(Mode::Restored);matricesCalls=0;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(noRatio==1 && matricesCalls==0 && fixedDraws==0 && !memcmp(drawnWvp,homeWvp,64));
        // That same draw taught the region its ratio.
        assert(ratios[Region(topLeft)].known && std::abs(ratios[Region(topLeft)].value-e.ratio)<1e-4f);
        assert(!ratios[Region(bottomRight)].known);
        // A ratio seen with a different tilt is not used.
        ratios[Region(topLeft)].cosine=0.9f;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(noRatio==2 && fixedDraws==0);
        for(auto& r:ratios)r=Ratio{};
        noRatio=0;
    }
    for(const Element& e:elements) for(int m=0;m<modeCount;++m) {
        overrideProjection=e.projection;
        memcpy(state.data()+0x500,identity,64);state[0x546]=1;FakeMatrices(state.data(),nullptr);
        float homeWvp[16];memcpy(homeWvp,fakeWvp,64);
        // Where the game itself would draw it: at home, or moved natively by
        // the preview offset.
        float wantX[4],wantY[4],wantDepth[4];memcpy(wantX,e.x,16);memcpy(wantY,e.y,16);
        if(m==int(Mode::Shifted)){wantX[3]+=rowMove(e,shiftX[e.index]);wantY[3]+=float(shiftY[e.index])*20.f;}
        depthOf(e,wantX,wantDepth);
        // What the engine holds for the draw in this mode.
        float haveX[4];memcpy(haveX,e.x,16);
        if(m!=int(Mode::Normal))haveX[3]+=rowMove(e,park[e.index]);
        float haveDepth[4];depthOf(e,haveX,haveDepth);
        memcpy(constants,haveDepth,16);constants[7]=1;
        currentX=haveX;currentY=e.y;
        mode=m;matricesCalls=0;const int submittedBefore=submitted;const unsigned fixedBefore=fixedDraws;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(submitted==submittedBefore+1);
        const float* targetX=m==int(Mode::Normal)?e.x:wantX;
        const float* targetY=m==int(Mode::Normal)?e.y:wantY;
        float targetDepth[4];depthOf(e,targetX,targetDepth);
        for(const auto& v:vertices) {
            double got[4],want[4];
            Clip(haveX,e.y,haveDepth,drawnWvp,v,got);
            Clip(targetX,targetY,targetDepth,homeWvp,v,want);
            assert(SameClip(got,want));
        }
        if(m==int(Mode::Normal)) {
            // Nothing parked: the hook must not touch the draw at all.
            assert(matricesCalls==0 && fixedDraws==fixedBefore && !memcmp(drawnWvp,homeWvp,64));
        } else {
            assert(matricesCalls==2 && fixedDraws==fixedBefore+1);
            // A parked draw left alone would land far away; the fix is doing the work.
            double off[4],home[4];
            Clip(haveX,e.y,haveDepth,homeWvp,vertices[0],off);Clip(e.x,e.y,targetDepth,homeWvp,vertices[0],home);
            assert(!SameClip(off,home));
        }
        // World is put back and the matrices rebuilt for whatever draws next.
        assert(!memcmp(state.data()+0x500,identity,64) && !memcmp(fakeWvp,homeWvp,64));
    }
    assert(fixFaults==0 && nonIdentityWorld==0 && noRatio==0 && learnFaults==0);

    // A parked draw whose World is not identity is left exactly as it was.
    {
        const Element& e=elements[0];overrideProjection=e.projection;
        float haveX[4];memcpy(haveX,e.x,16);haveX[3]+=rowMove(e,park[0]);
        float haveDepth[4];depthOf(e,haveX,haveDepth);memcpy(constants,haveDepth,16);constants[7]=1;
        currentX=haveX;currentY=e.y;
        float odd[16];memcpy(odd,identity,64);odd[12]=5;
        memcpy(state.data()+0x500,odd,64);state[0x544]=1;FakeMatrices(state.data(),nullptr);
        float untouched[16];memcpy(untouched,fakeWvp,64);
        mode=int(Mode::Restored);matricesCalls=0;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(nonIdentityWorld==1 && matricesCalls==0 && !memcmp(drawnWvp,untouched,64));
        assert(!memcmp(state.data()+0x500,odd,64));
        memcpy(state.data()+0x500,identity,64);state[0x544]=1;FakeMatrices(state.data(),nullptr);

        // No stream: forwarded, nothing examined. Not a Scaleform primitive: same.
        matricesCalls=0;int forwardedBefore=submitted;
        PrimitiveHook(primitive,nullptr,nullptr,false,0);assert(submitted==forwardedBefore+1 && matricesCalls==0);
        *reinterpret_cast<float**>(primitiveState+0x20)=nullptr;
        PrimitiveHook(primitive,nullptr,primitive,false,0);assert(submitted==forwardedBefore+2 && matricesCalls==0);
        *reinterpret_cast<float**>(primitiveState+0x20)=constants;
        // An unreadable primitive costs a fault count, not the process, and is still forwarded.
        PrimitiveHook(reinterpret_cast<void*>(uintptr_t(8)),nullptr,primitive,false,0);
        assert(submitted==forwardedBefore+3 && fixFaults==1 && matricesCalls==0);

        // Recording: what the draw used, read after the engine submitted it.
        memcpy(shadowDepth,haveDepth,16);
        capturing=true;captureSlot=2;capturePresent=1;drawCount[2]=0;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(drawCount[2]==1 && recordFaults==0);
        const Draw& d=draws[2][0];
        assert(d.present==1 && d.eye==1 && d.tag==1 && d.applied==1);
        assert(d.flags==0x1442 && d.indices==6 && d.material==0xAAAA);
        assert(!memcmp(d.projection,topLeft,64) && !memcmp(d.constants,constants,48));
        // The parking move measured in the game: 41,042.4 in depth, 146,590.7 in X.
        assert(std::abs(d.d[2]+41042.41f)<0.1f && std::abs(d.d[0]+146590.7f)<1.f && d.d[1]==0);
        assert(std::abs(d.d[3]-e.ratio)<1e-4f);
        assert(d.rows[0]==12 && d.rows[1]==42 && d.rows[2]==8);
        assert(!memcmp(d.wvp,drawnWvp,64) && d.xy[3]==haveX[3] && d.xy[7]==e.y[3] && !memcmp(d.depth,haveDepth,16));
        // An unowned draw is recorded too, untagged.
        constants[3]=875.59f;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(drawCount[2]==2 && draws[2][1].tag==0 && !draws[2][1].applied && draws[2][1].d[0]==0 && draws[2][1].d[3]==0);
        // A missing buffer is reported as zero rows, not a fault.
        *reinterpret_cast<uint32_t**>(state.data()+0x5a8+16)=nullptr;
        PrimitiveHook(primitive,nullptr,primitive,false,0);
        assert(drawCount[2]==3 && draws[2][2].rows[2]==0 && draws[2][2].rows[0]==12 && recordFaults==0);
        *reinterpret_cast<uint32_t**>(state.data()+0x5a8+16)=buffer4;
        // A full capture stays full.
        drawCount[2]=drawCapacity;PrimitiveHook(primitive,nullptr,primitive,false,0);assert(drawCount[2]==drawCapacity);
        capturing=false;
    }

    // ---- Sequencing: three live elements behind two prefixed children.
    FakeChild healthMovie{},weaponMovie{};
    healthMovie.vt=weaponMovie.vt=childVtable;healthMovie.parent=weaponMovie.parent=&movie;
    healthMovie.prefix="_level0.MainRootMovie.mainLayer_mc.HUD1_mc.Health5_mc";
    weaponMovie.prefix="_level0.MainRootMovie.mainLayer_mc.HUD1_mc.WeaponIndicator6_mc";
    Controller health{VA(0xABBD3C),{},&healthMovie},energy{VA(0xABB3AC),{},&healthMovie},weapon{VA(0xABF63C),{},&weaponMovie};
    const std::string paths[3]={
        std::string(healthMovie.prefix)+".mainHealthBar_mc.healthBar_mc._x",
        std::string(healthMovie.prefix)+".mainHealthBar_mc.energyBar_mc._x",
        std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.weaponInfo_mc._x"};
    const double originals[3]={-91.75,-75.35,0.55};
    auto runPreview=[&](bool weaponPresent) {
        ResetPreview();Request(0,0,0);values.clear();
        for(int i=0;i<3;++i)values[paths[i]]=originals[i];
        if(!weaponPresent)values.erase(paths[2]);
        InitHook(&health,nullptr,&healthMovie,nullptr);InitHook(&energy,nullptr,&healthMovie,nullptr);
        InitHook(&weapon,nullptr,&weaponMovie,nullptr);
        int captures=0,lastPeriod=-1;uint64_t firstCapture=0,periodBegan=0;
        // Run well past the six recorded periods: the preview keeps cycling.
        for(uint64_t now=1000;now<1000+16*5000;now+=16) {
            UpdateHook(&health,nullptr,0.016f);UpdateHook(&energy,nullptr,0.016f);UpdateHook(&weapon,nullptr,0.016f);
            const bool was=capturing;
            Sequence(now);
            if(period!=lastPeriod) {
                // Each mode lasts at least five seconds, in order, and repeats.
                assert(period==lastPeriod+1 && mode==period%modeCount);
                if(lastPeriod>=0)assert(now-periodBegan>=5000);
                lastPeriod=period;periodBegan=now;
            }
            if(!was && capturing) {
                // When recording starts, every present element is where this mode put it.
                const int k=captureSlot;assert(k==captures && k==period && k<captureCount);
                if(!captures)firstCapture=now;
                for(int i=0;i<3;++i) {
                    if(i==2 && !weaponPresent){assert(!periodApplied[k][i]);continue;}
                    assert(periodApplied[k][i]);
                    const double expected=originals[i]+(k%modeCount?park[i]:0);
                    assert(std::abs(values[paths[i]]-expected)<1e-9);
                }
                ++captures;
            }
        }
        assert(captures==captureCount && capturesWritten==captureCount && !capturing);
        assert(lastPeriod>=12);
        return firstCapture;
    };
    const uint64_t withWeapon=runPreview(true);
    const uint64_t withoutWeapon=runPreview(false);
    // With no weapon indicator the preview still runs, after the longer wait.
    assert(withoutWeapon>withWeapon && withoutWeapon-1000>=8000 && withWeapon-1000<8000);
    assert(park[0]==6000 && park[1]==9000 && park[2]==-6000);

    // ---- Hand HUD mode: the same addressing and requests, no sequencer, and
    // the matrix rebuild is left to EngineCamera.
    {
        desktopMode=false;handHudMode=true;
        assert(Requesting() && std::string(LogName())=="DeusExHRVR-hand-hud-quiet.log");
        Request(park[0],park[1],park[2]);values.clear();
        for(int i=0;i<3;++i)values[paths[i]]=originals[i];
        InitHook(&health,nullptr,&healthMovie,nullptr);InitHook(&energy,nullptr,&healthMovie,nullptr);
        InitHook(&weapon,nullptr,&weaponMovie,nullptr);
        for(int n=0;n<10;++n){UpdateHook(&health,nullptr,0.016f);UpdateHook(&energy,nullptr,0.016f);UpdateHook(&weapon,nullptr,0.016f);}
        for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9);
        // The HUD movie is rebuilt (a load, a level change): the elements are parked again.
        for(int i=0;i<3;++i)values[paths[i]]=originals[i];
        values.erase(std::string(healthMovie.prefix)+".mainHealthBar_mc.healthBar_mc.__dxhrvrHudIsolationOriginalX");
        values.erase(std::string(healthMovie.prefix)+".mainHealthBar_mc.energyBar_mc.__dxhrvrHudIsolationOriginalX");
        values.erase(std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.weaponInfo_mc.__dxhrvrHudIsolationOriginalX");
        InitHook(&health,nullptr,&healthMovie,nullptr);InitHook(&energy,nullptr,&healthMovie,nullptr);
        InitHook(&weapon,nullptr,&weaponMovie,nullptr);
        for(int n=0;n<10;++n){UpdateHook(&health,nullptr,0.016f);UpdateHook(&energy,nullptr,0.016f);UpdateHook(&weapon,nullptr,0.016f);}
        for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9);

        // Persistence (Mark, November 13: the layout reverted after hacking and
        // stayed so after a save and reload). Long after the first 120 updates,
        // the game puts every element back: each is parked again within one
        // update, and none gives up.
        auto updateAll=[&](int count){for(int n=0;n<count;++n){UpdateHook(&health,nullptr,0.016f);UpdateHook(&energy,nullptr,0.016f);UpdateHook(&weapon,nullptr,0.016f);}};
        updateAll(500);
        for(int i=0;i<3;++i)values[paths[i]]=originals[i];
        updateAll(1);
        for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9 && !slots[i].finished && slots[i].reparks>=1);
        // Again, several times: still parked, still not given up.
        for(int k=0;k<5;++k){for(int i=0;i<3;++i)values[paths[i]]=originals[i];updateAll(3);}
        for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9 && !slots[i].finished && slots[i].reparks>=6);
        // The clips vanish for far longer than the desktop check's 3,600-update
        // limit (a long hack), then come back unparked with no saved original
        // (a rebuilt movie on the same controller): found and parked again.
        {
            std::map<std::string,double> keep=values;
            for(int i=0;i<3;++i){values.erase(paths[i]);}
            updateAll(8000);
            for(int i=0;i<3;++i)assert(!slots[i].finished && slots[i].missing && slots[i].lost>=1);
            for(int i=0;i<3;++i)values[paths[i]]=originals[i];
            values.erase(std::string(healthMovie.prefix)+".mainHealthBar_mc.healthBar_mc.__dxhrvrHudIsolationOriginalX");
            values.erase(std::string(healthMovie.prefix)+".mainHealthBar_mc.energyBar_mc.__dxhrvrHudIsolationOriginalX");
            values.erase(std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.weaponInfo_mc.__dxhrvrHudIsolationOriginalX");
            updateAll(61);
            for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9 && !slots[i].missing);
            // Found again, it is checked every update, not every 60th.
            for(int i=0;i<3;++i)values[paths[i]]=originals[i];
            updateAll(1);
            for(int i=0;i<3;++i)assert(std::abs(values[paths[i]]-(originals[i]+park[i]))<1e-9);
            (void)keep;
        }
        // The desktop check keeps its one-write rule (tested above, "FAILED
        // native override"); only hand HUD mode re-parks.

        // Unnamed black plates behind ammo and the consumable: found by name
        // search at their authored positions, hidden; a look-alike at another
        // position, and the plates' siblings, are not touched.
        {
            const std::string wp=std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.";
            for(auto& p:unnamedPlates){p.found[0]=0;p.next=1;p.pass=p.wait=p.calls=p.logs=0;p.written=-1;p.announced=false;}
            values[wp+"weaponInfo_mc.instance900._x"]=-5.75;values[wp+"weaponInfo_mc.instance900._y"]=40;   // same x, wrong y
            values[wp+"weaponInfo_mc.instance900._alpha"]=100;
            values[wp+"weaponInfo_mc.instance1500._x"]=185.85;values[wp+"weaponInfo_mc.instance1500._y"]=-8.65;
            values[wp+"weaponInfo_mc.instance1500._alpha"]=100;
            values[wp+"weaponInfo_mc.instance2301._x"]=-5.75;values[wp+"weaponInfo_mc.instance2301._y"]=-10.65;
            values[wp+"weaponInfo_mc.instance2301._alpha"]=100;
            values[wp+"grenadeInfo_mc.instance2290._x"]=4.0;values[wp+"grenadeInfo_mc.instance2290._y"]=5.75;
            values[wp+"grenadeInfo_mc.instance2290._alpha"]=100;
            int updates=0;
            while((values[wp+"weaponInfo_mc.instance2301._alpha"]!=0 || values[wp+"grenadeInfo_mc.instance2290._alpha"]!=0) && updates<100000)
                {Backplates(&weapon,2);++updates;}
            assert(values[wp+"weaponInfo_mc.instance2301._alpha"]==0 && values[wp+"grenadeInfo_mc.instance2290._alpha"]==0);
            assert(!strcmp(unnamedPlates[0].found,"instance2301") && !strcmp(unnamedPlates[1].found,"instance2290"));
            assert(values[wp+"weaponInfo_mc.instance900._alpha"]==100);
            // November 15: the clip at 185.85,-8.65 is the three dots, now hidden
            // too, by _visible (not drawn); its _alpha is left alone. The three
            // searches share a 0.5 ms budget per update, so how many names each
            // gets per update depends on the machine's speed: keep updating until
            // it is found (bounded), rather than assume it is found first.
            for(int n=0;n<100000 && !unnamedPlates[2].found[0];++n)Backplates(&weapon,2);
            assert(!strcmp(unnamedPlates[2].found,"instance1500"));
            Backplates(&weapon,2);
            assert(values[wp+"weaponInfo_mc.instance1500._visible"]==0 && values[wp+"weaponInfo_mc.instance1500._alpha"]==100);
            // Its hide is confirmed through the boolean read (revision 6): no
            // FAILED line, and it is not rewritten every update.
            assert(unnamedPlates[2].written==0 && unnamedPlates[2].logs==0);
            // Revision 9: the aug cross's background is searched only on the aug
            // controller's updates (slot 5), never on the weapon's; Mark &
            // Track's named plate belongs to its controller (slot 6).
            assert(unnamedPlates[3].slot==5 && !strcmp(unnamedPlates[3].property,"_visible") &&
                   unnamedPlates[3].x==-128.0 && unnamedPlates[3].y==-64.0 &&
                   std::string(unnamedPlates[3].parent)=="_root.mainAugmentationBar_mc.augmentationWheel_mc");
            assert(unnamedPlates[0].slot==2 && unnamedPlates[1].slot==2 && unnamedPlates[2].slot==2);
            assert(unnamedPlates[3].next==1 && !unnamedPlates[3].found[0]);   // weapon updates did not touch it
            {bool mt=false;for(const auto& p:plates)if(p.slot==6 && std::string(p.path)=="_root.Main.markingCounter_mc.bg_outline_mc")mt=true;assert(mt);}
            assert(values.find(wp+"weaponInfo_mc.instance900._visible")==values.end());
            // Bounded per update: never more than 128 names per plate.
            assert(updates>=2301/128);
            // Shown again by the game: hidden again within 30 updates.
            values[wp+"weaponInfo_mc.instance2301._alpha"]=100;
            for(int n=0;n<30;++n)Backplates(&weapon,2);
            assert(values[wp+"weaponInfo_mc.instance2301._alpha"]==0);
            // A rebuilt movie gives the plate a new name: the old one is dropped and the new one found.
            values.erase(wp+"weaponInfo_mc.instance2301._x");values.erase(wp+"weaponInfo_mc.instance2301._y");
            values[wp+"weaponInfo_mc.instance4100._x"]=-5.75;values[wp+"weaponInfo_mc.instance4100._y"]=-10.65;
            values[wp+"weaponInfo_mc.instance4100._alpha"]=100;
            updates=0;
            while(values[wp+"weaponInfo_mc.instance4100._alpha"]!=0 && updates<100000){Backplates(&weapon,2);++updates;}
            assert(!strcmp(unnamedPlates[0].found,"instance4100") && values[wp+"weaponInfo_mc.instance4100._alpha"]==0);
            // Health and energy controllers never search.
            const unsigned lookups=unnamedLookups;
            Backplates(&health,0);Backplates(&energy,1);
            assert(unnamedLookups==lookups);
            // November 15 (Mark: "Reloading moves the HUD and then it goes
            // back"): while weaponInfo_mc plays its reload/empty frames (3 on),
            // and for 30 updates after, the ammo's draws are not sampled for
            // its middle; the consumable likewise off its frame 1. Resting
            // frames (Normal 1, Laser 2) and unreadable frames sample as before.
            {
                const std::string af=wp+"weaponInfo_mc._currentframe",gf=wp+"grenadeInfo_mc._currentframe";
                values[af]=1;values[gf]=1;Backplates(&weapon,2);
                assert(BandSteady(1) && BandSteady(3) && BandSteady(0) && BandSteady(2));
                values[af]=2;Backplates(&weapon,2);assert(BandSteady(1)); // Laser: resting
                values[af]=3;Backplates(&weapon,2);                        // Reload starts
                assert(!BandSteady(1) && BandSteady(3) && BandSteady(0) && BandSteady(2));
                values[af]=40;Backplates(&weapon,2);assert(!BandSteady(1)); // loopReload
                values[af]=1;
                for(int n=0;n<kSteadyAfter-1;++n){Backplates(&weapon,2);assert(!BandSteady(1));}
                Backplates(&weapon,2);assert(BandSteady(1));               // 30 updates back at rest
                values[gf]=3;Backplates(&weapon,2);assert(!BandSteady(3) && BandSteady(1)); // EmptyThrow
                values.erase(af);values.erase(gf);Backplates(&weapon,2);
                assert(BandSteady(1) && BandSteady(3));                    // unreadable: as before
                // The skip guards the samples, not the middle: a published
                // window is kept while a piece animates.
                for(auto& m:bandMiddles)m=BandMiddle{};
                BandSample(1,1000,500);BandSample(1,3000,1500);
                float mx=0,my=0;BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);
                for(auto& m:bandMiddles)m=BandMiddle{};
            }
        }

        const Element& e=elements[2];overrideProjection=e.projection;
        float haveX[4];memcpy(haveX,e.x,16);haveX[3]+=rowMove(e,park[2]);
        float haveDepth[4];depthOf(e,haveX,haveDepth);memcpy(constants,haveDepth,16);constants[7]=1;
        memcpy(state.data()+0x500,identity,64);state[0x544]=0;matricesCalls=0;mode=int(Mode::Normal);
        Fix fix;fix.state=nullptr;fix.tag=0;fix.applied=false;fix.ratio=0;
        PrepareFix(primitive,fix);
        assert(fix.applied && fix.tag==3 && matricesCalls==0 && state[0x544]==1);
        assert(memcmp(state.data()+0x500,identity,64));
        // Parked ammo comes home: X by the measured 112,763, depth by 41,042.
        assert(std::abs(fix.d[0]-112763.1f)<1.f && std::abs(fix.d[2]+41042.41f)<0.1f && fix.d[1]==0);
        state[0x544]=0;RestoreWorld(fix);
        assert(!memcmp(state.data()+0x500,identity,64) && state[0x544]==1 && matricesCalls==0);
        Fix none;none.state=nullptr;none.tag=0;none.applied=false;none.ratio=0;
        state[0x544]=0;RestoreWorld(none);assert(state[0x544]==0);
        // Layout: a single native weapon update services both sibling clips.
        requested[3]=park[3];requested[4]=0;Request(park[0],park[1],park[2]);
        const auto consumable=Qualify(&weaponMovie,slots[3].path);
        const auto prompt=Qualify(&weaponMovie,"_root.mainWeaponIndicator_mc.detonatePrompt_mc._x");
        values[consumable]=25;values[prompt]=77;
        InitHook(&weapon,nullptr,&weaponMovie,nullptr);
        nativeUpdates=0;UpdateHook(&weapon,nullptr,0.016f);
        assert(nativeUpdates==1 && values[consumable]==25+park[3]);
        assert(values[paths[2]]==originals[2]+park[2] && values[prompt]==77);
        assert(slots[2].applied && slots[3].applied);
        // Reinitialization preserves the saved original; no accumulated parking.
        InitHook(&weapon,nullptr,&weaponMovie,nullptr);UpdateHook(&weapon,nullptr,0.016f);
        assert(values[consumable]==25+park[3]);
        // Live switch (Nov 17): HUD set to DEFAULT puts back each element's own
        // x and each hidden plate's value once, then writes nothing; HANDS parks
        // and hides again. The draw side follows handHudActive.
        {
            handHooksReady=true;SetHandHudShown(true);assert(handHudActive);
            const auto outline=Qualify(&weaponMovie,"_root.mainWeaponIndicator_mc.weaponInfo_mc.outline_mc._alpha");
            const std::string wp=std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.";
            const std::string blackPlate=wp+"weaponInfo_mc.instance4100._alpha",dots=wp+"weaponInfo_mc.instance1500._visible";
            for(auto& p:plates)p.haveOriginal=false;
            for(auto& p:unnamedPlates)p.haveOriginal=false;
            values[outline]=100;values[blackPlate]=100;values[dots]=1;
            for(int n=0;n<31;++n)UpdateHook(&weapon,nullptr,0.016f);
            assert(values[outline]==0 && values[blackPlate]==0 && values[dots]==0);
            assert(values[paths[2]]==originals[2]+park[2] && values[consumable]==25+park[3]);
            SetHandHudShown(false);assert(!handHudActive);
            const unsigned beforeRestore=writes;
            UpdateHook(&weapon,nullptr,0.016f);
            assert(values[paths[2]]==originals[2] && values[consumable]==25);
            assert(values[outline]==100 && values[blackPlate]==100 && values[dots]==1);
            const unsigned restoreWrites=writes-beforeRestore;
            for(int n=0;n<100;++n)UpdateHook(&weapon,nullptr,0.016f);
            assert(writes-beforeRestore==restoreWrites);                      // nothing more is written
            values[paths[2]]=originals[2]+3;UpdateHook(&weapon,nullptr,0.016f);
            assert(values[paths[2]]==originals[2]+3);                  // the game's own changes are left alone
            values[paths[2]]=originals[2];
            SetHandHudShown(true);assert(handHudActive);
            for(int n=0;n<31;++n)UpdateHook(&weapon,nullptr,0.016f);
            assert(values[paths[2]]==originals[2]+park[2] && values[consumable]==25+park[3]);
            assert(values[outline]==0 && values[blackPlate]==0 && values[dots]==0);
            // Hooks not in place: the switch never claims the hand HUD is drawn.
            handHooksReady=false;SetHandHudShown(true);assert(!handHudActive);
        }
        FakeChild radarMovie{};radarMovie.vt=childVtable;radarMovie.parent=&movie;
        radarMovie.prefix="hud_mc.radarHolder_mc";
        Controller radar{VA(0xABDCFC),{},&radarMovie};
        auto radarPath=Qualify(&radarMovie,slots[4].path);
        values[radarPath]=40;
        InitHook(&radar,nullptr,&radarMovie,nullptr);UpdateHook(&radar,nullptr,0.016f);
        // The radar is addressed but not parked.
        assert(slots[4].applied && values[radarPath]==40);
        // No scale factors readable yet: the radar scale stays unmeasured.
        assert(!(radarScale.load()>0) && chainTried[4] && chainScale[4]==0.f);
        // Health's chain is 1.25 and the radar's 0.75: what matters is the ratio, 0.6.
        values[std::string(healthMovie.prefix)+".mainHealthBar_mc._xscale"]=100;
        values[std::string(healthMovie.prefix)+"._xscale"]=125;
        values[std::string(radarMovie.prefix)+".mainRadar_mc._xscale"]=100;
        values[std::string(radarMovie.prefix)+"._xscale"]=75;
        InitHook(&radar,nullptr,&radarMovie,nullptr);UpdateHook(&radar,nullptr,0.016f);
        assert(std::abs(chainScale[4]-0.75f)<1e-6f && !(radarScale.load()>0)); // health not yet measured
        InitHook(&health,nullptr,&healthMovie,nullptr);UpdateHook(&health,nullptr,0.016f);
        assert(std::abs(chainScale[0]-1.25f)<1e-6f && std::abs(radarScale-0.6f)<1e-6f);
        // Re-parking after a rebuild does not re-measure, and a later reading cannot change it.
        values[std::string(radarMovie.prefix)+"._xscale"]=80;
        UpdateHook(&radar,nullptr,0.016f);assert(std::abs(radarScale-0.6f)<1e-6f);
        // The radar is recognised where it is, by its projection and tilt, and
        // carried as it is: no translation, World untouched.
        {
            float rp[16];memcpy(rp,topLeft,64);rp[8]=0;rp[9]=0;
            const float rc=std::cos(17.f*3.14159265f/180.f),rs=std::sqrt(1-rc*rc);
            overrideProjection=rp;
            memset(constants,0,48);constants[0]=-0.003f;constants[2]=0.99889f;constants[7]=1;constants[3]=-12.f;
            memcpy(state.data()+0x500,identity,64);mode=int(Mode::Normal);
            // The 2.7-degree signature was not the radar: nothing is recognised.
            Fix rf;rf.state=nullptr;rf.tag=0;rf.applied=false;rf.ratio=0;
            const unsigned recognisedBefore=recognisedTags[4];
            PrepareFix(primitive,rf);
            assert(rf.tag==0 && !rf.applied && recognisedTags[4]==recognisedBefore);
            assert(!memcmp(state.data()+0x500,identity,64));
            // The radar as the grip-run log found it: bottom-left corner offset,
            // tilt 0.95514. Recognised and carried with no translation.
            rp[8]=.5833f;rp[9]=.5824f;constants[2]=0.95514f;constants[3]=745.f;
            Fix gf;gf.state=nullptr;gf.tag=0;gf.applied=false;gf.ratio=0;
            PrepareFix(primitive,gf);
            assert(gf.tag==5 && gf.applied && gf.d[0]==0 && gf.d[1]==0 && gf.d[2]==0 && recognisedTags[4]==recognisedBefore+1);
            assert(!memcmp(state.data()+0x500,identity,64));
            RestoreWorld(gf);
            // Its tilt changes after the HUD appears (0.95630 later): still the radar.
            constants[2]=0.95630f;
            Fix nf;nf.state=nullptr;nf.tag=0;nf.applied=false;nf.ratio=0;
            PrepareFix(primitive,nf);assert(nf.tag==5 && nf.applied);RestoreWorld(nf);
            rp[8]=0;rp[9]=0;constants[2]=0.99889f;constants[3]=-12.f;
            // Its origin is read from its own parameter block, with no corner relation.
            float radarParams[8]={0.2f,0,0,1234.f, 0,0.2f,0,-567.f};
            *reinterpret_cast<float**>(primitiveState+0x1c)=radarParams;
            float radarOrigin[3]{};
            const unsigned fromParamsBefore=rowsFromParams;
            assert(DrawOrigin(primitive,radarOrigin,true)==1 && radarOrigin[0]==1234.f && radarOrigin[1]==-567.f && radarOrigin[2]==-12.f);
            assert(rowsFromParams==fromParamsBefore+1);
            radarParams[3]=std::numeric_limits<float>::quiet_NaN();
            assert(DrawOrigin(primitive,radarOrigin,true)==0);
            *reinterpret_cast<float**>(primitiveState+0x1c)=nullptr;
            assert(DrawOrigin(primitive,radarOrigin,true)==0);
            rowsNone=0;rowsFromParams=0; // the checks below count from zero
            // A different tilt in the same place is ordinary HUD.
            constants[2]=0.99949f;
            Fix of;of.state=nullptr;of.tag=0;of.applied=false;of.ratio=0;
            PrepareFix(primitive,of);assert(of.tag==0 && !of.applied);
            // Anything tilted in that corner is the radar; an untilted draw is not.
            constants[2]=0.99889f;rp[8]=.6666667f;rp[9]=.6660482f;
            Fix cf;cf.state=nullptr;cf.tag=0;cf.applied=false;cf.ratio=0;
            PrepareFix(primitive,cf);assert(cf.tag==5 && cf.applied);RestoreWorld(cf);
            constants[2]=1.f;
            Fix uf0;uf0.state=nullptr;uf0.tag=0;uf0.applied=false;uf0.ratio=0;
            PrepareFix(primitive,uf0);assert(uf0.tag==0 && !uf0.applied);
            // A far draw in another region that is not a known element is counted,
            // not moved. (Top middle; until November 15 this used the right middle,
            // which is now the parked aug cross's region.)
            rp[8]=0;rp[9]=-.6666667f;constants[0]=0.02f;constants[2]=rc;constants[3]=6000*20*rs;
            const unsigned unownedBefore=unownedParked;
            Fix uf;uf.state=nullptr;uf.tag=0;uf.applied=false;uf.ratio=0;
            PrepareFix(primitive,uf);
            assert(uf.tag==0 && !uf.applied && unownedParked==unownedBefore+1);
            // Backup recognition (hack candidate): a parked piece drawn in a
            // projection with no corner offset (p8 = p9 = 0, tilt cos 0.99972,
            // as twelve early samples in the failsafe run were) is recognised
            // from its parking distance and its own layout height, and carried
            // home by its own X-to-depth relation.
            {
                float bp[16];memcpy(bp,rp,64);bp[8]=0;bp[9]=0;overrideProjection=bp;
                const float bc=0.99972f,bs=std::sqrt(1-bc*bc);
                float rows[8]={0,0,0,0, 0,0.05f,0,0};
                *reinterpret_cast<float**>(primitiveState+0x1c)=rows;
                memset(constants,0,48);constants[0]=0.02f;constants[2]=bc;constants[7]=1;
                auto backup=[&](float units,float height,float ratio,Fix& f) {
                    constants[3]=units*twips*bs;rows[0]=ratio*constants[0];rows[3]=ratio*constants[3]+77.f;rows[7]=height;
                    memcpy(state.data()+0x500,identity,64);
                    f.state=nullptr;f.tag=0;f.applied=false;f.ratio=0;f.backup=false;
                    PrepareFix(primitive,f);
                };
                // Without both groups' heights seen by the normal test: nothing.
                for(auto& g:groupHeights)g=GroupHeight{};
                Fix none0;backup(8391.8f,1400.f,2.5f,none0);
                assert(none0.tag==0 && !none0.applied && backupNoGroups>=1 && !memcmp(state.data()+0x500,identity,64));
                // Heights as the normal test records them from recognised pieces.
                rows[7]=1500.f;for(int n=0;n<100;++n)RecordHeight(1,primitiveState);
                rows[7]=12500.f;for(int n=0;n<100;++n)RecordHeight(3,primitiveState);
                assert(std::abs(groupHeights[0].Mean()-1500.f)<1e-3f && std::abs(groupHeights[1].Mean()-12500.f)<1e-3f);
                // Energy (8,000-10,000 units, top of the layout): left wrist.
                Fix energyFix;backup(8391.8f,1400.f,2.5f,energyFix);
                assert(energyFix.tag==2 && energyFix.backup && energyFix.applied && energyFix.ratio==2.5f);
                const float energyMove=bs*(-9000.f)*twips;
                assert(std::abs(energyFix.d[2]-energyMove)<1e-2f && std::abs(energyFix.d[0]-2.5f*energyMove)<5e-2f && energyFix.d[1]==0);
                const float* w=reinterpret_cast<float*>(state.data()+0x500);
                assert(w[12]==energyFix.d[0] && w[14]==energyFix.d[2] && state[0x544]==1);
                RestoreWorld(energyFix);assert(!memcmp(state.data()+0x500,identity,64));
                // Its own rows give the origin, as for the radar.
                float o[3]{};assert(DrawOrigin(primitive,o,energyFix.backup)==1 && o[1]==1400.f);
                // Ammo (5,000-7,000 units, bottom of the layout): right wrist, its own ratio.
                Fix ammoFix;backup(5655.1f,12000.f,-2.7f,ammoFix);
                assert(ammoFix.tag==3 && ammoFix.backup && ammoFix.ratio==-2.7f);
                const float ammoMove=-bs*(6000.f)*twips;
                assert(std::abs(ammoFix.d[2]-ammoMove)<1e-2f && std::abs(ammoFix.d[0]+2.7f*ammoMove)<5e-2f);
                RestoreWorld(ammoFix);
                // Health and consumable.
                Fix healthFix;backup(5655.1f,2000.f,3.5f,healthFix);assert(healthFix.tag==1);RestoreWorld(healthFix);
                Fix consumableFix;backup(8400.f,13000.f,-2.7f,consumableFix);assert(consumableFix.tag==4);RestoreWorld(consumableFix);
                // A draw whose X-to-depth term is too small to divide by keeps the piece's last ratio.
                constants[0]=1e-6f;
                Fix tinyFix;backup(5655.1f,12000.f,-99.f,tinyFix);
                assert(tinyFix.tag==3 && tinyFix.ratio==-2.7f);RestoreWorld(tinyFix);
                constants[0]=0.02f;
                // Not parked, or between the bands: not a piece.
                Fix home;backup(300.f,1400.f,2.5f,home);assert(home.tag==0 && !home.applied);
                Fix between;backup(7500.f,1400.f,2.5f,between);assert(between.tag==0 && !between.applied);
                // Logging off (no DeusExHRVR-hand-hud-log.enable): the census that
                // only feeds the log does not run; on, it does. Recognition is the same.
                {
                    const int region=Region(bp);
                    verbose=false;
                    const unsigned before=censusParked[region];
                    Fix quiet;backup(7500.f,1400.f,2.5f,quiet);
                    assert(quiet.tag==0 && censusParked[region]==before);
                    Fix quietEnergy;backup(8391.8f,1400.f,2.5f,quietEnergy);
                    assert(quietEnergy.tag==2 && quietEnergy.applied);RestoreWorld(quietEnergy);
                    verbose=true;
                    Fix loud;backup(7500.f,1400.f,2.5f,loud);
                    assert(censusParked[region]==before+1);
                    // Problems are still written when logging is off.
                    assert(Problem("park-write FAILED path=%s") && Problem("unnamed-plate NOT FOUND parent=%s")
                        && Problem("hand-band FAILED to identify vertex shader crc=%08X") && Problem("FAILED native override"));
                    assert(!Problem("slot-first-sight path=%s") && !Problem("draw-status last-10s placed=%u failed=%u")
                        && !Problem("UNRESOLVED property"));
                }
                // Untilted: no distance to read; not a piece.
                constants[2]=1.f;Fix flat;backup(8391.8f,1400.f,2.5f,flat);assert(flat.tag==0);constants[2]=bc;
                // A non-identity World is left alone.
                Fix odd;constants[3]=8391.8f*twips*bs;rows[0]=2.5f*constants[0];rows[7]=1400.f;
                float shifted[16];memcpy(shifted,identity,64);shifted[12]=5;memcpy(state.data()+0x500,shifted,64);
                odd.state=nullptr;odd.tag=0;odd.applied=false;odd.ratio=0;
                PrepareFix(primitive,odd);assert(odd.tag==0 && !odd.applied);
                // The normal corner test still takes precedence where it applies.
                bp[8]=.6666667f;bp[9]=-.6660482f;
                ratios[Region(bp)]=Ratio{3.5f,bc,true,0,false};
                Fix normal;backup(5655.1f,2000.f,3.5f,normal);
                assert(normal.tag==1 && !normal.backup && normal.applied);RestoreWorld(normal);
                ratios[Region(bp)]=Ratio{};
                // The once-a-second watcher: events at the second they happen,
                // each once per episode, using the failsafe run's counts.
                {
                    Watch w;
                    assert(WatchStep(w,40000,100,0)==0);              // normal play
                    assert(WatchStep(w,0,0,0)==0);                    // hacking screen: HUD not drawn
                    assert(WatchStep(w,0,9039,0)==1 && w.lost);       // after the hack (542,362 a minute)
                    assert(WatchStep(w,0,9000,0)==0);                 // reported once, not every second
                    assert(WatchStep(w,40000,0,0)==2 && !w.lost);     // recognised again
                    assert(WatchStep(w,40000,0,5000)==3 && w.backupOn); // backup takes over
                    assert(WatchStep(w,40000,0,5000)==0);
                    assert(WatchStep(w,40000,0,0)==4 && !w.backupOn); // backup no longer needed
                    Watch v;assert(WatchStep(v,0,99,0)==0);           // a few stray far draws: not a loss
                    assert(WatchStep(v,0,9000,0)==1);
                    assert(WatchStep(v,30000,0,30000)==3 && !v.lost); // the backup ends the loss
                }
                for(auto& g:groupHeights)g=GroupHeight{};
                for(auto& r:backupRatio)r=0;
                rowsNone=0;rowsFromParams=0; // the checks below count from zero
                *reinterpret_cast<float**>(primitiveState+0x1c)=nullptr;
                memcpy(state.data()+0x500,identity,64);
                overrideProjection=rp;
                memset(constants,0,48);constants[0]=0.02f;constants[2]=rc;constants[7]=1;constants[3]=6000*20*rs;
            }
            // Each band's middle is measured from its draws; bad samples are ignored.
            for(auto& m:bandMiddles)m=BandMiddle{};
            float mx=-9,my=-9;BandMiddleOf(0,mx,my);assert(mx==-9 && my==-9); // nothing yet: fallback kept
            BandSample(0,-.5f,.2f);BandSample(0,-.3f,.6f);BandSample(0,std::numeric_limits<float>::quiet_NaN(),0);
            BandMiddleOf(0,mx,my);assert(std::abs(mx+.4f)<1e-6f && std::abs(my-.4f)<1e-6f);
            assert(bandMiddles[1].current.samples==0 && bandMiddles[2].current.samples==0);
            // After a full window the next one starts; a slide-in early on does not stick.
            for(int n=0;n<3998;++n)BandSample(0,-.4f,.4f);
            assert(bandMiddles[0].published.samples==4000 && bandMiddles[0].current.samples==0);
            for(int n=0;n<4000;++n)BandSample(0,-.9f,.9f);
            BandMiddleOf(0,mx,my);assert(std::abs(mx+.9f)<1e-6f && std::abs(my-.9f)<1e-6f);
            BandSample(7,0,0);BandSample(-1,0,0); // out of range: ignored
            // November 15: the consumable is measured apart (band 3), and the
            // radar's top-left origin is available, unmoved by origins added
            // to its right and below (alarm timer, blips).
            {
                float nx=-9,ny=-9;assert(!BandMinOf(3,nx,ny) && nx==-9 && ny==-9);
                BandSample(3,600,11000);BandSample(3,2200,13900);BandSample(3,1500,12000);
                assert(BandMinOf(3,nx,ny) && nx==600 && ny==11000);
                BandMiddleOf(3,nx,ny);assert(nx==1400 && ny==12450);
                assert(bandMiddles[1].current.samples==0 && !BandMinOf(4,nx,ny));
                bandMiddles[3]=BandMiddle{};
            }
            // Revision 6 (Mark: reloading still moved the ammo): the ammo and the
            // consumable keep the narrowest full window since the last HUD
            // build; a wider one (a reload adding draws) never moves them; a HUD
            // rebuild measures afresh. Other bands still follow the last window.
            {
                auto window=[](int band,float x0,float x1,float y0,float y1){
                    for(int n=0;n<4000;++n)BandSample(band,n%2?x1:x0,n%2?y1:y0);
                };
                for(auto& m:bandMiddles)m=BandMiddle{};
                float mx=0,my=0;
                window(1,1000,3000,500,1500);BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);   // first window
                window(1,1100,2900,520,1480);BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);   // narrower, same centre
                window(1,1100,3500,400,1480);BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);   // reload: wider, not used
                assert(bandMiddles[1].published.maxX==3500 && Spread(bandMiddles[1].narrowest)==1800+960);
                window(1,1100,2900,520,1480);BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);   // after: unchanged
                // A HUD rebuild: the old narrowest is set aside at once; the next
                // window is taken whatever its width.
                ++hudRebuilds;
                BandMiddleOf(1,mx,my);assert(mx==2000 && my==1000);                               // last window until then
                window(1,5000,7000,500,1500);BandMiddleOf(1,mx,my);assert(mx==6000 && my==1000);
                // The consumable likewise; health/energy and the aug cross follow the last window.
                window(3,0,100,0,100);window(3,0,300,0,100);BandMiddleOf(3,mx,my);assert(mx==50);
                window(0,0,100,0,100);window(0,0,300,0,100);BandMiddleOf(0,mx,my);assert(mx==150);
                // Revision 9: the aug cross keeps its narrowest window too; Mark &
                // Track follows the last window and is anchored on its largest origins.
                window(4,0,100,0,100);window(4,0,300,0,100);BandMiddleOf(4,mx,my);assert(mx==50);
                window(5,0,100,0,100);window(5,0,300,0,100);BandMiddleOf(5,mx,my);assert(mx==150);
                float ex=0,ey=0;assert(BandMaxOf(5,ex,ey) && ex==300 && ey==100);
                assert(!BandMaxOf(0,ex,ey)==false);
                assert(KeepsNarrowest(1) && KeepsNarrowest(3) && KeepsNarrowest(4) && !KeepsNarrowest(0) && !KeepsNarrowest(2) && !KeepsNarrowest(5));
                for(auto& m:bandMiddles)m=BandMiddle{};
            }
            // A draw's own origin for the wrist curvature: accepted only when its
            // X row matches the region's relation to this draw's depth row.
            {
                rp[8]=.6666667f;rp[9]=.6660482f;overrideProjection=rp;
                Ratio& known=ratios[Region(rp)];
                known=Ratio{1.96248f,rc,true,0,false};
                memset(constants,0,48);constants[0]=0.02f;constants[1]=0.001f;constants[2]=rc;constants[7]=1;constants[3]=880.f;
                float origin[3]{};
                assert(DrawOrigin(primitive,origin)==0 && rowsNone==1); // offset not yet learned
                // After a draw the engine's buffer holds its X row: the offset is learned.
                shadowXY[0]=1.96248f*constants[0];shadowXY[1]=1.96248f*constants[1];shadowXY[3]=1.96248f*constants[3]+3120.f;
                Learn(primitive);
                assert(known.offsetKnown && std::abs(known.offset-3120.f)<0.05f);
                // The next draw's own parameter block holds its rows.
                float params[8]={1.96248f*0.02f,1.96248f*0.001f,0,1.96248f*900.f+3120.f, 0.001f,0.05f,0,-4321.f};
                *reinterpret_cast<float**>(primitiveState+0x1c)=params;
                constants[3]=900.f;
                assert(DrawOrigin(primitive,origin)==1 && origin[0]==params[3] && origin[1]==-4321.f && origin[2]==900.f && rowsFromParams==1);
                // A block from another draw (wrong X translation) is refused; the buffer is tried next.
                params[3]+=50.f;
                shadowXY[3]=1.96248f*900.f+3120.f;shadowXY[7]=-777.f;
                assert(DrawOrigin(primitive,origin)==2 && origin[1]==-777.f && rowsFromBuffer==1);
                // Neither matches: no origin, the piece is laid flat.
                shadowXY[3]+=50.f;
                assert(DrawOrigin(primitive,origin)==0 && rowsNone==2);
                // A different tilt than the ratio was measured with: refused.
                shadowXY[3]-=50.f;constants[2]=0.9f;
                assert(DrawOrigin(primitive,origin)==0 && rowsNone==3);
                *reinterpret_cast<float**>(primitiveState+0x1c)=nullptr;
                known=Ratio{};
            }
            overrideProjection=nullptr;
        }
        // Backplates: all five plates hidden, the health plate included, even
        // while it plays a damage level (Mark, November 13).
        {
            const std::string hp=std::string(healthMovie.prefix)+".mainHealthBar_mc.";
            const std::string wp=std::string(weaponMovie.prefix)+".mainWeaponIndicator_mc.";
            for(auto& p:plates){p.written=-1;p.calls=0;p.logs=0;}
            values[hp+"healthBar_mc.background_mc._currentframe"]=1;
            values[hp+"healthBar_mc.background_mc._alpha"]=100;
            values[hp+"energyBar_mc.outline_mc._alpha"]=100;
            values[hp+"energyBar_mc.background_mc._alpha"]=100;
            values[wp+"weaponInfo_mc.outline_mc._alpha"]=100;
            values[wp+"grenadeInfo_mc.outline_mc._alpha"]=100;
            values[hp+"energyBar_mc.cell0_mc.mainCellContainer_mc.container_mc.outline_mc._alpha"]=100;
            Backplates(&health,0);Backplates(&energy,1);Backplates(&weapon,2);
            assert(values[hp+"healthBar_mc.background_mc._alpha"]==0);
            assert(values[hp+"energyBar_mc.outline_mc._alpha"]==0 && values[hp+"energyBar_mc.background_mc._alpha"]==0);
            assert(values[wp+"weaponInfo_mc.outline_mc._alpha"]==0 && values[wp+"grenadeInfo_mc.outline_mc._alpha"]==0);
            // The energy cells' own outlines are indicators, not the plate.
            assert(values[hp+"energyBar_mc.cell0_mc.mainCellContainer_mc.container_mc.outline_mc._alpha"]==100);
            // Damage: the plate stays hidden through every damage level.
            for(double frame:{17.0,45.0,70.0,1.0}) {
                values[hp+"healthBar_mc.background_mc._currentframe"]=frame;
                for(int n=0;n<31;++n)Backplates(&health,0);
                assert(values[hp+"healthBar_mc.background_mc._alpha"]==0 && damageShows==0);
            }
            // Re-hidden if the game shows it again.
            values[hp+"healthBar_mc.background_mc._alpha"]=100;
            for(int n=0;n<31;++n)Backplates(&health,0);
            assert(values[hp+"healthBar_mc.background_mc._alpha"]==0);
            // A rebuilt movie shows the plates again; they are re-hidden within 30 updates.
            values[hp+"energyBar_mc.outline_mc._alpha"]=100;
            int updates=0;
            while(values[hp+"energyBar_mc.outline_mc._alpha"]!=0 && updates<40){Backplates(&energy,1);++updates;}
            assert(updates>0 && updates<=30);
            // Not written every update: no change, no write, until the next re-set.
            const unsigned writesBefore=plateWrites;
            Backplates(&energy,1);
            assert(plateWrites==writesBefore);
            // The radar is never touched.
            for(const auto& p:plates)assert(std::string(p.path).find("Radar")==std::string::npos && p.slot!=4);
            // Outside hand HUD mode nothing is touched.
            handHudMode=false;values[hp+"energyBar_mc.outline_mc._alpha"]=100;
            for(int n=0;n<40;++n)Backplates(&energy,1);
            assert(values[hp+"energyBar_mc.outline_mc._alpha"]==100);
            handHudMode=true;
        }
        // After a hack (hackdiag run, November 13, 155.8 s): the same corners,
        // tilts and parking distances, but c[8..11] = -8.34077, -4.53185e-06,
        // 95.3358, 95.7 on every HUD draw. Numbers copied from the log's samples.
        {
            float hp[16]{};float hc[12]={0.28951f,0,0.93969f,40475.97f, 0,0,0,1, -8.34077f,-4.53185e-06f,95.3358f,95.7f};
            float hs{};
            hp[8]=.5833f;hp[9]=-.5824f;
            assert(Classify(hp,hc,hs)==1);                     // health, top-left, units 5917
            hc[0]=0.02471f;hc[3]=40974.29f;assert(Classify(hp,hc,hs)==1);
            // Ammo in its corner (bottom-right) with the post-hack values.
            hp[8]=-.5833f;hp[9]=.5824f;hc[3]=6000*20*std::sqrt(1-hc[2]*hc[2]);
            assert(Classify(hp,hc,hs)==3);
            // The radar in its corner with them (r8, tilt 0.90257 from the samples).
            hp[8]=.5833f;hp[9]=.5824f;hc[2]=0.90257f;hc[3]=37.5f;
            assert(Classify(hp,hc,hs)==5);
            // The second vector still has to be (0,0,0,1).
            hc[7]=0.5f;assert(Classify(hp,hc,hs)==0);hc[7]=1;hc[4]=0.1f;assert(Classify(hp,hc,hs)==0);hc[4]=0;
            // And everything has to be a number.
            hc[10]=std::numeric_limits<float>::quiet_NaN();assert(Classify(hp,hc,hs)==0);
        }
        // New tags only classify their region and parking band, never normal UI.
        float p[16]{};p[8]=-.666f;p[9]=.666f;
        float c[12]{};c[2]=std::cos(20.f*3.14159265f/180.f);c[7]=1;
        float sine=std::sqrt(1-c[2]*c[2]);c[3]=9000*20*sine;
        float signedTilt{};assert(Classify(p,c,signedTilt)==4 && signedTilt<0);
        // Radar: anything tilted in the bottom-left corner.
        p[8]=0;p[9]=0;c[2]=0.99889f;c[3]=-12;assert(Classify(p,c,signedTilt)==0);
        p[8]=.666f;p[9]=.666f;c[2]=std::cos(17.f*3.14159265f/180.f);c[3]=6000*20*std::sqrt(1-c[2]*c[2]);assert(Classify(p,c,signedTilt)==5);
        p[8]=.5833f;p[9]=.5824f;c[2]=0.95514f;c[3]=300;assert(Classify(p,c,signedTilt)==5);
        c[2]=0.95630f;assert(Classify(p,c,signedTilt)==5);
        p[8]=-.5833f;assert(Classify(p,c,signedTilt)==0);
        p[8]=.5833f;
        p[8]=.666f;p[9]=.666f;c[2]=0.99889f;assert(Classify(p,c,signedTilt)==5);
        c[2]=1.f;assert(Classify(p,c,signedTilt)==0);
        // Revision 8: the aug cross (region 3, right middle) and Mark & Track
        // (region 5, left middle) are recognised where they are, by region
        // alone, like the radar (revision 7 run: during play nothing else drew
        // tilted there). Numbers from the runs: right cos 0.93969 / 0.9389-0.9404,
        // left 0.90257 / 0.9014-0.9038; also with the post-hack c[8..11].
        {
            float rp[16]{};rp[8]=-.5833f;rp[9]=0;
            float rc[12]={-0.07432f,0,0.93969f,874.83f, 0,0,0,1, -8.34077f,-4.53185e-06f,95.3358f,95.7f};
            float ts{};
            assert(Classify(rp,rc,ts)==6);
            rc[2]=0.9389f;assert(Classify(rp,rc,ts)==6);rc[2]=0.9404f;assert(Classify(rp,rc,ts)==6);
            float lp[16]{};lp[8]=.5833f;lp[9]=0;
            float lc[12]={0.02186f,0,0.90257f,37.5f, 0,0,0,1, 0,0,0,0};
            assert(Classify(lp,lc,ts)==7);
            lc[2]=0.9014f;assert(Classify(lp,lc,ts)==7);lc[2]=0.9038f;assert(Classify(lp,lc,ts)==7);lc[2]=0.90257f;
            // The revision 7 centre-region test is gone: centre draws are not ours.
            {
                float ap[16]{};float ac[12]={-0.01172f,0,0.99587f,150*20*std::sqrt(1-0.99587f*0.99587f), 0,0,0,1, 0,0,0,0};
                assert(Classify(ap,ac,ts)==0);
            }
            // Same numbers in a corner: the corners' own rules, not these.
            float cp[16]{};cp[8]=.5833f;cp[9]=.5824f;assert(Classify(cp,lc,ts)==5);   // bottom-left stays the radar
            cp[8]=.5833f;cp[9]=-.5824f;assert(Classify(cp,lc,ts)==0);                  // top-left at home: not a health band
            cp[8]=0;cp[9]=-.5824f;assert(Classify(cp,lc,ts)==0);                       // top middle
            // Untilted: never ours.
            lc[2]=1.f;assert(Classify(lp,lc,ts)==0);lc[2]=0.90257f;
            // Outside hand HUD mode: never.
            handHudMode=false;assert(Classify(rp,rc,ts)==0 && Classify(lp,lc,ts)==0);handHudMode=true;
            // Not parked any more (revision 8): only health, energy, ammo and
            // the consumable are; park[] still holds the old distances, unused.
            assert(park[5]==-12000 && park[6]==12000);
            assert(std::string(slots[5].path)=="_root.mainAugmentationBar_mc._x" && slots[5].vtable==0xAB8C4C);
            assert(std::string(slots[6].path)=="_root.Main.markingCounter_mc._x" && slots[6].vtable==0xABED1C);
        }
        p[8]=0;p[9]=0;handHudMode=false;
        assert(Classify(p,c,signedTilt)==0 && Match(&radar)==nullptr);
        desktopMode=true;
    }
    puts("PASS: root lifecycle and failure cases; prefixed, unprefixed and nested child wrappers are addressed through their own getter/setter; desktop requests, native override, throttled missing lookup with ancestor probes, fault-tolerant diagnostics; recovery classification on census numbers, the per-region ratio learned from the engine's buffer and refused until seen, the World-translation arithmetic for health, energy and ammo in all three modes, untouched draws, non-identity World, fault handling, recording and mode sequencing with and without a weapon indicator; hand HUD mode parks through the same addressing, parks again after the movie is rebuilt, and leaves the matrix rebuild to its caller; radar recognised unparked as everything tilted in the bottom-left corner, carried without translation; each band's middle measured from its own draws over rolling windows; a draw's own origin for the wrist curvature accepted from its parameter block or the engine buffer only when it matches the region's learned relation. Wrappers, engine state, matrix composition and the shader are stand-ins modelled on disassembly. Native GFx calls and rendering are NOT tested.");
}
