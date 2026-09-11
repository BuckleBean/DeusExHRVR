#include "EngineCamera.h"
#include "CameraMath.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <algorithm>

// Supported executable only. All preferred addresses are rebased for ASLR.
// F6 opt-in experiment. Camera poses are attached to the scene and then to the
// completed native pair, never replaced with the companion's newer predicted pose.
namespace EngineCamera {
namespace {
uintptr_t base{};
std::atomic<int> budget{};
std::atomic<uint64_t> frameId{};
std::mutex output;
std::mutex stateMutex;
Transport::Header* channel{};
Transport::TrackingReader trackingReader;
bool requested{},referenceValid{},recenterRequested{},f6Down{},f9Down{};
Transport::Pose reference{};
float worldScale=100.f;
struct Snapshot {
    CameraMath::Matrix originalWorld,world,view,manager;
    Transport::Tracking tracking{};
    uintptr_t managerAddress{};
    bool active{};
};
Snapshot current;
std::unordered_map<void*,Snapshot> scenes;
Transport::RenderInfo pairInfo{};
uint64_t taggedScenes{},stereoCalls{};
uint64_t hudDraws{},hudMatrices{};
thread_local bool insideUpdate{};
thread_local Snapshot drawing;
thread_local unsigned drawnEyes{};
thread_local Snapshot lastWorld;
thread_local Snapshot uiSnapshot;
thread_local bool uiDrawing{};
using Update = void(__thiscall*)(void*);
using CreateScene = void*(__thiscall*)(void*,void*,void*,void*,void*,void*,uint32_t);
using Getter = void*(__thiscall*)(void*);
using Draw = void(__thiscall*)(void*,uint32_t,void*);
using Stereo = void(__cdecl*)(float*,bool,float,float);
using Primitive = void(__thiscall*)(void*,void*,bool,uint32_t);
Update originalUpdate{};
CreateScene originalCreate{};
Getter originalWorld{},originalView{},originalManager{};
Draw originalDraw{};Stereo originalStereo{};
Primitive originalPrimitive{};Update originalMatrices{};
uintptr_t VA(uintptr_t preferred) { return base+preferred-0x400000; }
void Matrix(FILE* f,const char* name,const void* data) {
    const float* m=static_cast<const float*>(data);
    fprintf(f,"%s",name);
    for(int i=0;i<16;i++)fprintf(f," %.7g",m[i]);
    fputc('\n',f);
}
void __fastcall UpdateHook(void* self,void*) {
    insideUpdate=true;
    originalUpdate(self);
    insideUpdate=false;
    {
        std::lock_guard lock(stateMutex);
        current.active=false;
        auto manager=static_cast<unsigned char*>(self);
        auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
        Transport::Tracking t{};
        if(requested && active==manager+0x6f0 && trackingReader.Read(channel,t,GetTickCount64())) {
            if(!referenceValid || recenterRequested){reference=t.head;referenceValid=true;recenterRequested=false;}
            current.originalWorld=CameraMath::Load(active+0x40);
            current.world=CameraMath::HeadWorld(current.originalWorld,reference,t.head,worldScale);
            current.view=CameraMath::InverseRigid(current.world);
            current.manager=CameraMath::Load(manager+0x13b0);
            auto oldView=CameraMath::Load(active+0x80);
            for(int col=0;col<3;col++) {
                float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*oldView.m[row*4+col];
                for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
                current.manager.m[12+col]+=(current.view.m[12+col]-oldView.m[12+col])*scale;
            }
            current.tracking=t;current.active=true;current.managerAddress=reinterpret_cast<uintptr_t>(self);
        }
    }
    if(budget.load()<=0)return;
    std::lock_guard lock(output);
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-camera.log","a"))return;
    auto manager=static_cast<unsigned char*>(self);
    auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
    fprintf(f,"camera frame=%llu manager=%p active=%p typeVtable=%08x player=%d\n",frameId.load(),self,active,
        active?unsigned(*reinterpret_cast<uintptr_t*>(active)-base+0x400000):0,active==manager+0x6f0);
    Matrix(f,"manager",manager+0x13b0);
    if(active==manager+0x6f0) {Matrix(f,"playerWorld",active+0x40);Matrix(f,"playerView",active+0x80);}
    fclose(f);
}
void* Get(void* self,Getter original,int kind) {
    if(insideUpdate)return original(self);
    thread_local CameraMath::Matrix values[3];
    {
        std::lock_guard lock(stateMutex);
        if(current.active && current.managerAddress==reinterpret_cast<uintptr_t>(self)) {
            values[kind]=kind==0?current.world:kind==1?current.view:current.manager;
            return values[kind].m;
        }
    }
    return original(self);
}
void* __fastcall WorldHook(void* self,void*){return Get(self,originalWorld,0);}
void* __fastcall ViewHook(void* self,void*){return Get(self,originalView,1);}
void* __fastcall ManagerHook(void* self,void*){return Get(self,originalManager,2);}
bool Match(const CameraMath::Matrix& viewport,const CameraMath::Matrix& player) {
    for(int i=0;i<16;i++) {
        float value=(i>=4&&i<7)?-player.m[i]:player.m[i];
        if(std::abs(viewport.m[i]-value)>(i>=12?2.f:0.02f))return false;
    }
    return true;
}
void* __fastcall CreateHook(void* self,void*,void* viewport,void* target,void* depth,void* source,void* sourceDepth,uint32_t flags) {
    Snapshot snapshot;
    {std::lock_guard lock(stateMutex);snapshot=current;}
    alignas(16) unsigned char adjusted[0xf0];
    if(viewport && snapshot.active) {
        auto p=static_cast<float*>(viewport);auto matrix=CameraMath::Load(p+12);
        if(p[8]>0 && p[7]>1000 && (Match(matrix,snapshot.originalWorld)||Match(matrix,snapshot.world))) {
            memcpy(adjusted,viewport,sizeof(adjusted));auto v=reinterpret_cast<float*>(adjusted);
            memcpy(v+12,snapshot.world.m,64);for(int i=4;i<7;i++)v[12+i]=-v[12+i];
            float maxX=0,maxY=0;
            for(const auto& eye:snapshot.tracking.eyes) {
                maxX=std::max(maxX,std::max(std::abs(std::tan(eye.left)),std::abs(std::tan(eye.right))));
                maxY=std::max(maxY,std::max(std::abs(std::tan(eye.up)),std::abs(std::tan(eye.down))));
            }
            v[8]=2*std::atan(maxY);v[9]=maxX/maxY;viewport=adjusted;
        } else snapshot.active=false;
    }
    void* result=originalCreate(self,viewport,target,depth,source,sourceDepth,flags);
    {
        std::lock_guard lock(stateMutex);
        if(snapshot.active && result) {if(scenes.size()>4096)scenes.clear();scenes[result]=snapshot;++taggedScenes;}
        else scenes.erase(result);
    }
    if(budget.load()>0 && budget.fetch_sub(1)>0 && viewport && result) {
        std::lock_guard lock(output);
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            auto vp=static_cast<unsigned char*>(viewport);auto s=static_cast<unsigned char*>(result);
            auto v=reinterpret_cast<float*>(vp);
            fprintf(f,"scene frame=%llu scene=%p target=%p depth=%p flags=%08x parent=%p near=%g far=%g fov=%g aspect=%g width=%g height=%g\n",
                frameId.load(),result,target,depth,flags,*reinterpret_cast<void**>(s+0x404),v[6],v[7],v[8],v[9],v[10],v[11]);
            Matrix(f,"viewport",vp+0x30);Matrix(f,"view",s+0x2b0);Matrix(f,"projection",s+0x2f0);
            fclose(f);
        }
    }
    return result;
}
void __fastcall DrawHook(void* self,void*,uint32_t pass,void* other) {
    auto previous=drawing;auto previousEyes=drawnEyes;
    {std::lock_guard lock(stateMutex);auto it=scenes.find(static_cast<unsigned char*>(self)-4);drawing=it==scenes.end()?Snapshot{}:it->second;}
    drawnEyes=0;
    originalDraw(self,pass,other);
    if(drawing.active && drawnEyes) {
        lastWorld=drawing;
        std::lock_guard lock(stateMutex);
        if(pairInfo.mode==0){pairInfo.mode=1;pairInfo.tracking=drawing.tracking;}
        if(pairInfo.tracking.id!=drawing.tracking.id)pairInfo.mode=2;
        pairInfo.eyeMask|=drawnEyes;
    }
    drawing=previous;drawnEyes=previousEyes;
}
void __fastcall MatricesHook(void* self,void*) {
    if(!uiDrawing){originalMatrices(self);return;}
    auto state=static_cast<unsigned char*>(self);
    auto& overrideMatrix=*reinterpret_cast<float**>(state+0x540);
    auto saved=overrideMatrix;
    auto source=CameraMath::Load(saved?saved:reinterpret_cast<float*>(state+0x440));
    // Scaleform uses a perspective override here too. The correction operates
    // on its resulting clip coordinates, so the source projection may be either.
    unsigned eye=state[0x5ea]?0:1;
    auto projected=CameraMath::Multiply(source,CameraMath::HudClipTransform(uiSnapshot.tracking,eye));
    auto stereoEnabled=state[0x5e9];
    overrideMatrix=projected.m;state[0x5e9]=0;state[0x546]=1;
    originalMatrices(self);
    overrideMatrix=saved;state[0x5e9]=stereoEnabled;
    ++hudMatrices;
}
void __fastcall PrimitiveHook(void* self,void*,void* stream,bool backBeforeFront,uint32_t flags) {
    auto primitive=static_cast<unsigned char*>(self);
    auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
    auto snapshot=drawing.active?drawing:lastWorld;
    // scaleformData is consumed only by the engine's UI shader path at 0x532d3c.
    bool isUI=stream && snapshot.active && primitiveState && *reinterpret_cast<void**>(primitiveState+0x20);
    if(!isUI){originalPrimitive(self,stream,backBeforeFront,flags);return;}
    auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
    auto state=*reinterpret_cast<unsigned char**>(device+0x150);
    auto previousUI=uiDrawing;auto previousSnapshot=uiSnapshot;
    uiDrawing=true;uiSnapshot=snapshot;
    MatricesHook(state,nullptr);
    originalPrimitive(self,stream,backBeforeFront,flags);
    uiDrawing=previousUI;uiSnapshot=previousSnapshot;
    state[0x546]=1;originalMatrices(state);
    ++hudDraws;
}
void __cdecl StereoHook(float* projection,bool firstEye,float width,float plane) {
    if(drawing.active && std::abs(projection[11]-1.f)<0.001f && std::abs(projection[15])<0.001f) {
        unsigned eye=firstEye?0:1;
        auto p=CameraMath::EyeProjection(CameraMath::Load(projection),drawing.tracking,eye,worldScale);
        memcpy(projection,p.m,64);drawnEyes|=1u<<eye;
        {std::lock_guard lock(stateMutex);++stereoCalls;}
    } else originalStereo(projection,firstEye,width,plane);
}
void Install() {
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const unsigned char updateBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x34};
    const unsigned char sceneBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x64};
    bool supported=nt->FileHeader.TimeDateStamp==0x52840914 && nt->OptionalHeader.SizeOfImage==0x1c54000;
    supported=supported&&!memcmp(reinterpret_cast<void*>(VA(0x6a15c0)),updateBytes,sizeof(updateBytes))&&
        !memcmp(reinterpret_cast<void*>(VA(0x53a5b0)),sceneBytes,sizeof(sceneBytes));
    if(!supported)return; // Probe and other executable versions are never patched.
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return;
    auto update=reinterpret_cast<void*>(VA(0x6a15c0)),scene=reinterpret_cast<void*>(VA(0x53a5b0));
    struct Hook {uintptr_t address;void* hook;void** original;const char* bytes;size_t length;};
    Hook hooks[]={
        {0x6a15c0,(void*)&UpdateHook,(void**)&originalUpdate,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x34",9},
        {0x53a5b0,(void*)&CreateHook,(void**)&originalCreate,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x64",9},
        {0x6a00f0,(void*)&WorldHook,(void**)&originalWorld,"\x8b\x49\x30\x8b\x01",5},
        {0x6a0100,(void*)&ViewHook,(void**)&originalView,"\x8b\x49\x30\x8b\x01",5},
        {0x6a0110,(void*)&ManagerHook,(void**)&originalManager,"\x8d\x81\xb0\x13\x00\x00\xc3",7},
        {0x546520,(void*)&DrawHook,(void**)&originalDraw,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x34",9},
        {0x51ebf0,(void*)&StereoHook,(void**)&originalStereo,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x1c\x01\x00\x00",12},
        {0x532c70,(void*)&PrimitiveHook,(void**)&originalPrimitive,"\x83\xec\x20\x53\x8b\x5c\x24\x28",8},
        {0x550930,(void*)&MatricesHook,(void**)&originalMatrices,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x84\x00\x00\x00",12}
    };
    for(auto& h:hooks)if(memcmp((void*)VA(h.address),h.bytes,h.length))return;
    bool enabled=true;
    for(auto& h:hooks)if(MH_CreateHook((void*)VA(h.address),h.hook,h.original)!=MH_OK){enabled=false;break;}
    if(enabled) {
        for(auto& h:hooks)MH_QueueEnableHook((void*)VA(h.address));enabled=MH_ApplyQueued()==MH_OK;
    }
    if(!enabled)for(auto& h:hooks){MH_DisableHook((void*)VA(h.address));MH_RemoveHook((void*)VA(h.address));}
    wchar_t config[MAX_PATH]{};GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,config,nullptr);
    wchar_t scaleText[32];GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"100",scaleText,32,config);
    float scale=static_cast<float>(_wtof(scaleText));if(std::isfinite(scale)&&scale>=10&&scale<=1000)worldScale=scale;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"Camera hooks base=%p enabled=%d unitsPerMetre=%g F6=toggle F9=recenter\n",reinterpret_cast<void*>(base),enabled,worldScale);fclose(f);
    }
}
}
Transport::RenderInfo OnPresent(uint64_t frame,bool capture) {
    static std::once_flag once;std::call_once(once,Install);
    frameId=frame;
    // Detailed camera dumps perform synchronous file IO. Never schedule them
    // periodically on the render thread; F8 is the explicit diagnostic request.
    budget=capture?32:0;
    std::lock_guard lock(stateMutex);
    auto completed=pairInfo;pairInfo={};
    lastWorld={};
    if(completed.mode==1 && completed.eyeMask!=3)completed.mode=2;
    static uint32_t lastMode=99;
    if(completed.mode!=lastMode || capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            fprintf(f,"nativePair frame=%llu mode=%u eyeMask=%u pose=%llu active=%d taggedScenes=%llu stereoCalls=%llu\n",frame,completed.mode,completed.eyeMask,completed.tracking.id,current.active,taggedScenes,stereoCalls);fclose(f);
        }
        lastMode=completed.mode;
    }
    if(capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"HUD plane draws=%llu matrices=%llu frame=%llu trackingReadContentions=%llu rejectedSamples=%llu\n",hudDraws,hudMatrices,frame,trackingReader.reused,trackingReader.rejected);fclose(f);}
    }
    bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0,f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(f6&&!f6Down){requested=!requested;referenceValid=false;current.active=false;}
    if(f9&&!f9Down)recenterRequested=true;
    if((f6&&!f6Down)||(f9&&!f9Down)) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"tracking requested=%d recenter=%d frame=%llu\n",requested,recenterRequested,frame);fclose(f);}
    }
    f6Down=f6;f9Down=f9;return completed;
}
void SetChannel(Transport::Header* header){std::lock_guard lock(stateMutex);channel=header;trackingReader={};if(!header){current.active=false;referenceValid=false;}}
}
