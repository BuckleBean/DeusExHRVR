#include "EngineCamera.h"
#include "CameraMath.h"
#include "DirectionConfig.h"
#include "NativeBounds.h"
#include "SceneCache.h"
#include "EngineShaderTrace.h"
#include "EffectShader.h"
#include "ScreenMode.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <intrin.h>

// Supported executable only. All preferred addresses are rebased for ASLR.
// F6 toggles tracking. Camera poses are attached to the scene and then to the
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
bool requested=true,referenceValid{},recenterRequested{},f6Down{},f9Down{},f7Down{},effectsFix=true,f4Down{},eyeViewFix=true,f3Down{},instanceFix=true;
bool interactionScreen{},scopeScreen{};
bool lockVerticalCamera{};
bool experimentalMotionControls{};
bool motionControls=true;
bool controllerHideArms=true;
DirectionConfig::Source interactionAim{},movementDirection{};
thread_local bool senseQueries{};
std::atomic<uint64_t> interactionQueries{},movementAxes{};
std::atomic<void*> walkAction{},strafeAction{};
float lastMoveInput[2]{},lastMoveOutput[2]{},lastMoveHeading{};
float controllerMuzzleForward=.25f;
uint64_t removedFlareSprites{};
bool skyFix=true,f11Down{};
uint64_t skyCorrections{};
ScreenMode screenMode;
unsigned screenReasons{};
void RefreshScreenMode() {
    unsigned reasons=(interactionScreen?1u:0u)|(ScreenMode::Menu(base)?2u:0u)|(screenMode.Video()?4u:0u)|
        (ScreenMode::GameOver(base)?8u:0u)|(scopeScreen?16u:0u);
    if(reasons!=screenReasons) {
        screenReasons=reasons;
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            fprintf(f,"automaticScreen reasons=%u requested=%d frame=%llu\n",reasons,requested,frameId.load());fclose(f);
        }
    }
}
Transport::Pose reference{};
bool levelRecenter=true;
// Local patch: keep only heading (yaw) from the recenter pose so head pitch/roll
// at load or F9 does not tilt the world. Position (incl. height) is kept.
Transport::Pose LevelReference(Transport::Pose p) {
    if(!levelRecenter)return p;
    auto f=CameraMath::Rotate(p.orientation,{0,0,-1});
    float yaw;
    if(std::hypot(f.x,f.z)<.001f){auto r=CameraMath::Rotate(p.orientation,{1,0,0});yaw=std::atan2(-r.z,r.x);}
    else yaw=std::atan2(-f.x,-f.z);
    p.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
    return p;
}
float worldScale=100.f;
struct Snapshot {
    CameraMath::Matrix originalWorld,world,view,manager;
    Transport::Tracking tracking{};
    uintptr_t managerAddress{};
    void* playerInstance{};
    uint32_t inputIndex=~0u;
    bool active{};
};
Snapshot current;
struct WeaponPose {void* weapon{};void* instance{};CameraMath::Matrix muzzle;uint64_t tick{};bool active{};void* owner{};};
WeaponPose weaponPose;
std::atomic<uint64_t> controllerDraws{},controllerMuzzles{};
std::atomic<uint64_t> controllerAimQueries{},controllerHiddenArms{};
std::atomic<uint64_t> controllerAttachments{},controllerBounds{};
thread_local bool nativeWeaponQuery{};
struct AttachmentTrace {uintptr_t caller{};int bone{};bool owner{};uint64_t queries{},corrected{};};
std::array<AttachmentTrace,96> attachmentTrace{};
std::mutex attachmentTraceMutex;
thread_local bool controllerDrawing{};
thread_local CameraMath::Matrix controllerDelta;
SceneCache<Snapshot> scenes;
Transport::RenderInfo pairInfo{};
uint64_t taggedScenes{},stereoCalls{};
uint64_t hudDraws{},hudMatrices{};
thread_local bool insideUpdate{};
thread_local Snapshot drawing;
thread_local unsigned drawnEyes{};
thread_local Snapshot lastWorld;
thread_local Snapshot uiSnapshot;
thread_local bool uiDrawing{};
bool effectsCapture{};
EngineShaderTrace shaderTrace;
struct EffectTrace {
    uintptr_t primitive{},material{};
    uint32_t flags{},eye{},active{},stereo{},overrideStereo{};
    float params[8]{};
    CameraMath::Matrix projection,view,world;
};
std::array<EffectTrace,2048> effectTrace{};
size_t effectCount{};
void SaveEffects(uint64_t frame) {
    std::error_code ec;std::filesystem::create_directory("DeusExHRVR-captures",ec);if(ec)return;
    char name[180];sprintf_s(name,"DeusExHRVR-captures/effects-%lu-%llu.csv",GetCurrentProcessId(),frame);
    std::ofstream out(name);out<<"primitive,material,flags,eye,active,stereo,overrideStereo";
    for(int i=0;i<8;i++)out<<",param"<<i;
    for(const char* name:{"projection","view","world"})for(int i=0;i<16;i++)out<<','<<name<<i;out<<'\n';
    for(size_t i=0;i<effectCount;i++) {
        const auto& t=effectTrace[i];out<<t.primitive<<','<<t.material<<','<<t.flags<<','<<t.eye<<','<<t.active<<','<<t.stereo<<','<<t.overrideStereo;
        for(float v:t.params)out<<','<<v;
        for(const auto* m:{&t.projection,&t.view,&t.world})for(float v:m->m)out<<','<<v;out<<'\n';
    }
}
struct SceneTrace {
    uint64_t frame{},tick{},pose{};
    uintptr_t scene{};
    uint32_t event{},reason{},cached{};
    float fov{},nearZ{},farZ{};
    CameraMath::Matrix viewport,original,tracked;
};
std::array<SceneTrace,32768> sceneTrace{};
size_t traceNext{},traceCount{};
void Trace(SceneTrace t){sceneTrace[traceNext]=t;traceNext=(traceNext+1)%sceneTrace.size();traceCount=std::min(traceCount+1,sceneTrace.size());}
void SaveTrace(uint64_t frame) {
    std::error_code ec;std::filesystem::create_directory("DeusExHRVR-captures",ec);if(ec)return;
    char name[180];sprintf_s(name,"DeusExHRVR-captures/camera-history-%lu-%llu.csv",GetCurrentProcessId(),frame);
    std::ofstream out(name);out<<"frame,tick,pose,scene,event,reason,cached,fov,near,far";
    for(const char* name:{"viewport","original","tracked"})for(int i=0;i<16;i++)out<<','<<name<<i;out<<'\n';
    for(size_t i=0;i<traceCount;i++) {
        const auto& t=sceneTrace[(traceNext+sceneTrace.size()-traceCount+i)%sceneTrace.size()];
        out<<t.frame<<','<<t.tick<<','<<t.pose<<','<<t.scene<<','<<t.event<<','<<t.reason<<','<<t.cached<<','<<t.fov<<','<<t.nearZ<<','<<t.farZ;
        for(const auto* m:{&t.viewport,&t.original,&t.tracked})for(float v:m->m)out<<','<<v;out<<'\n';
    }
}
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
Update originalUniforms{};
Update originalRenderState{};
using WeaponMuzzle=uintptr_t(__thiscall*)(void*,CameraMath::Matrix*,int,bool);
using WeaponAim=CameraMath::Matrix*(__thiscall*)(void*,CameraMath::Matrix*,bool,bool);
using WeaponDraw=void(__thiscall*)(void*,void*,void*);
using Skeleton=void(__cdecl*)(void*,void*,uint32_t,uint32_t,void*);
using Attachment=uintptr_t(__cdecl*)(void*,void*,int,CameraMath::Matrix*,bool);
using Bounds=void(__thiscall*)(void*,void*);
WeaponMuzzle originalWeaponMuzzle{};
WeaponAim originalWeaponAim{};
WeaponDraw originalWeaponDraw{};
WeaponDraw originalActorDraw{};
Skeleton originalSkeleton{};
Attachment originalAttachment{};
Bounds originalBounds{};
using SenseUpdate=void(__thiscall*)(void*,float);
using InputAxis=float(__thiscall*)(void*,uint32_t,bool);
SenseUpdate originalSenseUpdate{};
Getter originalPlayerWorld{};
InputAxis originalInputAxis{};
bool DirectionWorld(DirectionConfig::Source source,CameraMath::Matrix& result,CameraMath::Matrix* native=nullptr,uintptr_t manager=0) {
    std::lock_guard lock(stateMutex);
    auto now=GetTickCount64();
    if(source==DirectionConfig::Source::Mouse || !requested || !current.active || !current.playerInstance || screenReasons ||
       (manager && manager!=current.managerAddress) || now<current.tracking.tick || now-current.tracking.tick>=250)return false;
    if(native)*native=current.originalWorld;
    if(source==DirectionConfig::Source::Headset){result=current.world;return true;}
    if(!motionControls || !current.tracking.rightController.valid)return false;
    auto baseWorld=lockVerticalCamera?CameraMath::WithoutLookPitch(current.originalWorld):current.originalWorld;
    result=CameraMath::HeadWorld(baseWorld,reference,current.tracking.rightController.aim,worldScale);
    return true;
}
void __fastcall SenseUpdateHook(void* self,void*,float dt) {
    auto previous=senseQueries;
    {std::lock_guard lock(stateMutex);
        senseQueries=current.active && current.playerInstance &&
            *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==current.playerInstance;
    }
    originalSenseUpdate(self,dt);senseQueries=previous;
}
void* __fastcall PlayerWorldHook(void* self,void*) {
    if(senseQueries) {
        thread_local CameraMath::Matrix target;
        if(DirectionWorld(interactionAim,target)){++interactionQueries;return target.m;}
    }
    return originalPlayerWorld(self);
}
float __fastcall InputAxisHook(void* self,void*,uint32_t index,bool requireActive) {
    float value=originalInputAxis(self,index,requireActive);
    auto walk=walkAction.load(),strafe=strafeAction.load();
    if(movementDirection==DirectionConfig::Source::Mouse || !walk || !strafe || (self!=walk && self!=strafe))return value;
    {std::lock_guard lock(stateMutex);if(index!=current.inputIndex)return value;}
    CameraMath::Matrix target,native;
    if(!DirectionWorld(movementDirection,target,&native))return value;
    // Both locomotion actions must be evaluated: W can become a strafe even
    // when the original strafe action is zero/inactive. Call the trampoline
    // for the partner axis so the pair is rotated exactly once.
    float x=self==strafe?value:originalInputAxis(strafe,index,requireActive);
    float y=self==walk?value:originalInputAxis(walk,index,requireActive);
    if(!std::isfinite(x) || !std::isfinite(y))return value;
    auto axes=CameraMath::ReorientMovement(x,y,native,target);
    ++movementAxes;
    if(std::abs(x)+std::abs(y)>.01f) {
        std::lock_guard lock(stateMutex);
        lastMoveInput[0]=x;lastMoveInput[1]=y;
        lastMoveOutput[0]=axes.strafe;lastMoveOutput[1]=axes.walk;
        lastMoveHeading=CameraMath::HeadingDelta(native,target);
    }
    return self==strafe?axes.strafe:axes.walk;
}
WeaponPose ReadWeaponPose() {
    std::lock_guard lock(stateMutex);auto pose=weaponPose;
    auto now=GetTickCount64();
    pose.active=pose.active && current.active && requested && !screenReasons && now>=pose.tick && now-pose.tick<250;
    return pose;
}
bool RigidWeaponMatrix(const CameraMath::Matrix& m) {
    for(float v:m.m)if(!std::isfinite(v))return false;
    if(std::abs(m.m[15]-1.f)>.01f)return false;
    for(int r=0;r<3;r++)for(int c=r;c<3;c++) {
        float dot=0;for(int j=0;j<3;j++)dot+=m.m[r*4+j]*m.m[c*4+j];
        if(std::abs(dot-(r==c?1.f:0.f))>.02f)return false;
    }
    return true;
}
uintptr_t NativeWeaponMuzzle(void* weapon,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    auto previous=nativeWeaponQuery;nativeWeaponQuery=true;
    auto result=originalWeaponMuzzle(weapon,out,barrel,firstPerson);
    nativeWeaponQuery=previous;return result;
}
uintptr_t __cdecl AttachmentHook(void* source,void* instance,int bone,CameraMath::Matrix* out,bool previousFrame) {
    auto result=originalAttachment(source,instance,bone,out,previousFrame);
    if(experimentalMotionControls && !nativeWeaponQuery && instance && out) {
        auto pose=ReadWeaponPose();
        bool corrected=false;
        if(pose.active && instance==pose.instance && RigidWeaponMatrix(*out)) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                // Effects ask the Instance attachment getter directly, rather
                // than PrimaryWeapon's firing matrix. Preserve each attachment's
                // offset (flash, shell ejection, etc.) relative to the barrel.
                auto delta=CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                *out=CameraMath::Multiply(*out,delta);++controllerAttachments;corrected=true;
            }
        }
        if(pose.active && (instance==pose.instance || instance==pose.owner)) {
            auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-base+0x400000;
            std::lock_guard lock(attachmentTraceMutex);
            for(auto& t:attachmentTrace)if(!t.queries || (t.caller==caller && t.bone==bone && t.owner==(instance==pose.owner))) {
                t.caller=caller;t.bone=bone;t.owner=instance==pose.owner;++t.queries;t.corrected+=corrected;break;
            }
        }
    }
    return result;
}
void __fastcall BoundsHook(void* self,void*,void* volume) {
    originalBounds(self,volume);
    if(!experimentalMotionControls || !volume || *reinterpret_cast<uintptr_t*>(self)!=base+0xaaf854-0x400000)return;
    auto pose=ReadWeaponPose();
    if(pose.active && *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==pose.instance) {
        // Cell lookup (0x5c8950) has no callback for type 10 (Everything):
        // forcing it here caused an indirect call through zero while loading.
        // Enlarge only finite bounds, preserving their native world centre.
        if(NativeBounds::ExpandWeapon(volume,4.f*worldScale))++controllerBounds;
    }
}
uintptr_t __fastcall WeaponMuzzleHook(void* self,void*,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    auto result=NativeWeaponMuzzle(self,out,barrel,firstPerson);
    if(senseQueries)return result;
    if(experimentalMotionControls && out) {
        auto pose=ReadWeaponPose();
        if(pose.active && pose.weapon==self){*out=pose.muzzle;++controllerMuzzles;}
    }
    return result;
}
CameraMath::Matrix* __fastcall WeaponAimHook(void* self,void*,CameraMath::Matrix* out,bool firstPerson,bool forceCamera) {
    auto result=originalWeaponAim(self,out,firstPerson,forceCamera);
    // DXSense may also ask the weapon for an aim ray. Interaction selection
    // must use its own setting even when the gun follows the controller.
    if(senseQueries) {
        CameraMath::Matrix target;
        if(out && DirectionWorld(interactionAim,target)){*out=target;++interactionQueries;}
        return result;
    }
    if(experimentalMotionControls && out) {
        auto pose=ReadWeaponPose();
        // Player hit calculation 0x763370 and aim-ray getter 0x750ef0 both
        // use this matrix, bypassing WeaponMuzzle for ordinary player shots.
        // Keep the game's spread, collision and damage calculation intact.
        if(pose.active && pose.weapon==self){*out=CameraMath::FiringFromMuzzle(pose.muzzle);++controllerAimQueries;}
    }
    return result;
}
void __fastcall ActorDrawHook(void* self,void*,void* matrix,void* args) {
    if(experimentalMotionControls && controllerHideArms) {
        auto pose=ReadWeaponPose();
        // Suppress only the equipped weapon owner's actor mesh in tracked
        // player view. Animation, physics and other actors still run normally.
        if(pose.active && pose.owner && *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==pose.owner) {
            ++controllerHiddenArms;return;
        }
    }
    originalActorDraw(self,matrix,args);
}
void __cdecl SkeletonHook(void* model,void* bones,uint32_t flags,uint32_t count,void* state) {
    originalSkeleton(model,bones,flags,count,state);
    if(!controllerDrawing || !state)return;
    // Verified native PCDX11MatrixState: poseData is +0x10, count at +0,
    // followed by aligned world-space matrices at +0x10. This is a render
    // allocation, not the simulation's animation/bone buffer.
    auto s=static_cast<unsigned char*>(state);
    if(*reinterpret_cast<uintptr_t*>(s)!=base+0xa97524-0x400000)return;
    auto data=*reinterpret_cast<unsigned char**>(s+0x10);if(!data)return;
    auto n=*reinterpret_cast<uint32_t*>(data);if(!n || n>512 || n!=count)return;
    auto matrices=reinterpret_cast<CameraMath::Matrix*>(data+0x10);
    for(uint32_t i=0;i<n;i++)matrices[i]=CameraMath::MoveSkinMatrix(matrices[i],controllerDelta);
    ++controllerDraws;
}
void __fastcall WeaponDrawHook(void* self,void*,void* matrix,void* args) {
    auto previous=controllerDrawing;auto savedDelta=controllerDelta;controllerDrawing=false;
    if(experimentalMotionControls) {
        auto pose=ReadWeaponPose();auto drawable=static_cast<unsigned char*>(self);
        if(pose.active && *reinterpret_cast<void**>(drawable+8)==pose.instance) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                controllerDelta=CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                controllerDrawing=true;
            }
        }
    }
    originalWeaponDraw(self,matrix,args);
    controllerDrawing=previous;controllerDelta=savedDelta;
}
struct BillboardTint {float x,y,z,w;};
// Native stack: matrix +8, sprite-list +12, sprite-count +16, tint +20.
// Both call sites push count, then list, then matrix (right-to-left).
using Billboards=void(__cdecl*)(void*,void*,uint32_t,BillboardTint,void*,float);
Billboards originalBillboards{};
void __cdecl BillboardsHook(void* matrix,void* items,uint32_t count,BillboardTint tint,void* params,float z) {
    // Only LensFlareAndCoronaID's call site. Keep the native centre/depth
    // calculation (used by its remaining light meshes), but emit no sprites.
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==base+0x722708-0x400000) {
        removedFlareSprites+=count;count=0;
    }
    originalBillboards(matrix,items,count,tint,params,z);
}
using VideoCreate=void*(__thiscall*)(void*,void*);
VideoCreate originalVideoCreate{};
Update originalVideoDestroy{};
void* __fastcall VideoCreateHook(void* self,void*,void* heap) {
    auto result=originalVideoCreate(self,heap);if(result)screenMode.Add(result);return result;
}
void __fastcall VideoDestroyHook(void* self,void*) {screenMode.Remove(self);originalVideoDestroy(self);}
EffectShader effectShaders;
uint64_t instanceCorrections{};
void __fastcall RenderStateHook(void* self,void*) {
    auto state=static_cast<unsigned char*>(self);
    float* instance{};CameraMath::Matrix saved;
    auto shader=effectShaders.Identify(*reinterpret_cast<uintptr_t*>(state+0x198));
    float* skyConstants{};CameraMath::Matrix savedSky;
    // Use the native sky-layer marker, shared by sky materials across levels.
    // Verified in this executable: model draws select +5a5 from depthLayer;
    // immediate sprites clear it at 0x532d86. Cached depth constants alone
    // can still describe the preceding sky draw, so require both live flags.
    if(drawing.active && skyFix && state[0x5a4] && state[0x5a5]) {
        auto sceneCB=*reinterpret_cast<unsigned char**>(state+0x5ac);
        auto worldCB=*reinterpret_cast<unsigned char**>(state+0x5a8);
        auto sceneData=sceneCB?*reinterpret_cast<float**>(sceneCB+8):nullptr;
        if(sceneData && *reinterpret_cast<unsigned*>(sceneCB+12)>22 && sceneData[22*4+1]>.95f &&
            worldCB && *reinterpret_cast<unsigned*>(worldCB+12)>=12) {
            skyConstants=*reinterpret_cast<float**>(worldCB+8);
            if(skyConstants) {
                savedSky=CameraMath::Load(skyConstants);
                auto centre=drawing.world;for(int i=4;i<7;i++)centre.m[i]=-centre.m[i];
                auto overrideP=*reinterpret_cast<float**>(state+0x540);
                auto projection=CameraMath::Load(overrideP?static_cast<void*>(overrideP):state+0x440);
                auto corrected=CameraMath::SkyProjection(CameraMath::Load(skyConstants+16),centre,
                    projection,drawing.tracking,state[0x5ea]?0:1);
                memcpy(skyConstants,corrected.m,64);state[0x5c4]=1;state[0x1c]=1;++skyCorrections;
            }
        }
    }
    if(drawing.active && effectsFix && instanceFix && EffectShader::UsesCentreViewMatrix(shader)) {
        auto cb=*reinterpret_cast<unsigned char**>(state+0x5b8);
        if(cb && *reinterpret_cast<unsigned*>(cb+12)>=4) {
            instance=*reinterpret_cast<float**>(cb+8);
            if(instance) {
                saved=CameraMath::Load(instance);
                auto eye=CameraMath::EyeWorld(drawing.tracking,state[0x5ea]?0:1,worldScale);
                auto corrected=CameraMath::Multiply(eye,saved);memcpy(instance,corrected.m,64);
                state[0x5c8]=1;state[0x1c]=1;++instanceCorrections;
            }
        }
    }
    originalRenderState(self);
    if(effectsCapture)shaderTrace.Record(state,drawing.active);
    // Upload copied the corrected constants. Restore the engine's centre-view
    // copy so subsequent draws/eyes cannot accumulate the eye transform.
    if(instance){memcpy(instance,saved.m,64);state[0x5c8]=1;state[0x1c]=1;}
    if(skyConstants){memcpy(skyConstants,savedSky.m,64);state[0x5c4]=1;state[0x1c]=1;}
}
void __fastcall UniformsHook(void* self,void*) {
    originalUniforms(self);
    if(!drawing.active)return;
    auto scene=static_cast<unsigned char*>(self);
    auto device=*reinterpret_cast<unsigned char**>(base+0x12ab940-0x400000);
    auto state=*reinterpret_cast<unsigned char**>(device+0x150);
    unsigned eye=state[0x5ea]?0:1;
    auto cb=*reinterpret_cast<unsigned char**>(state+0x5ac);
    auto data=*reinterpret_cast<float**>(cb+8);
    float before[20];memcpy(before,data+15*4,sizeof(before));
    if(effectsFix) {
        auto world=CameraMath::Load(scene+0x40);
        if(eyeViewFix) {
            // Keep V * eyeInverse * P unchanged for geometry, but expose the
            // actual eye view and camera position to all shared shader inputs.
            auto eyeWorld=CameraMath::EyeWorld(drawing.tracking,eye,worldScale);
            auto view=CameraMath::Multiply(CameraMath::Load(scene+0x2b0),CameraMath::InverseRigid(eyeWorld));
            memcpy(state+0x480,view.m,64);state[0x545]=1;
            auto eyeWorldAbsolute=CameraMath::Multiply(eyeWorld,world);
            for(int i=0;i<3;i++){data[10*4+i]=eyeWorldAbsolute.m[12+i];data[11*4+i]=eyeWorldAbsolute.m[8+i];}
        }
        auto depth=CameraMath::DepthToWorld(world,drawing.tracking,eye,worldScale);
        // SceneBuffer stores the three output components as transposed rows.
        for(int col=0;col<3;col++)for(int row=0;row<4;row++)data[(15+col)*4+row]=depth.m[row*4+col];
        const auto& e=drawing.tracking.eyes[eye];
        float l=std::tan(e.left),r=std::tan(e.right),u=std::tan(e.up),d=std::tan(e.down);
        float view[]={r-l,d-u,l,u,1/(r-l),1/(d-u),0,0};memcpy(data+18*4,view,sizeof(view));
        state[0x5c5]=1;state[0x1c]=1;
    }
    if(effectsCapture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")) {
            fprintf(f,"depth frame=%llu eye=%u fix=%d eyeView=%d scene=%p\n",frameId.load()+1,eye,effectsFix,eyeViewFix,self);
            fprintf(f,"before");for(float v:before)fprintf(f," %.8g",v);fprintf(f,"\nafter");
            for(int i=0;i<20;i++)fprintf(f," %.8g",data[15*4+i]);fprintf(f,"\n");fclose(f);
        }
    }
}
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
    if(movementDirection!=DirectionConfig::Source::Mouse && (!walkAction.load() || !strafeAction.load())) {
        using FindAction=void*(__cdecl*)(const char*);
        auto find=reinterpret_cast<FindAction>(base+0x4ae340-0x400000);
        walkAction=find("locomotionwalk");strafeAction=find("locomotionstrafe");
    }
    {
        std::lock_guard lock(stateMutex);
        current.active=false;
        current.inputIndex=~0u;
        weaponPose={};
        auto manager=static_cast<unsigned char*>(self);
        auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
        scopeScreen=ScreenMode::Scope(base,manager);
        // Supported build: PlayerCamera embeds CameraMode_Hacking at +0x430.
        // Its enter/leave methods (0x6a1c50 / 0x6a1dc0) set/clear +0x104.
        // Check the exact class before reading its active flag.
        auto hacking=manager+0x6f0+0x430;
        bool screen=*reinterpret_cast<uintptr_t*>(hacking)==base+0xaa774c-0x400000 && hacking[0x104]!=0;
        if(screen!=interactionScreen) {
            interactionScreen=screen;
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"interactionScreen=%d requested=%d frame=%llu\n",screen,requested,frameId.load());fclose(f);
            }
        }
        Transport::Tracking t{};
        RefreshScreenMode();
        if(requested && !screenReasons && active && trackingReader.Read(channel,t,GetTickCount64())) {
            if(!referenceValid || recenterRequested){reference=LevelReference(t.head);referenceValid=true;recenterRequested=false;}
            // Interaction cameras implement the same virtual getters as the
            // player camera; use that interface instead of its private layout.
            current.originalWorld=CameraMath::Load(originalWorld(self));
            // Keep native aiming/input intact. Level only the VR rendering
            // base, then add the headset's complete orientation and position.
            auto renderBase=lockVerticalCamera?CameraMath::WithoutLookPitch(current.originalWorld):current.originalWorld;
            current.world=CameraMath::HeadWorld(renderBase,reference,t.head,worldScale);
            current.view=CameraMath::InverseRigid(current.world);
            current.manager=CameraMath::Load(manager+0x13b0);
            auto oldView=CameraMath::Load(originalView(self));
            for(int col=0;col<3;col++) {
                float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*oldView.m[row*4+col];
                for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
                current.manager.m[12+col]+=(current.view.m[12+col]-oldView.m[12+col])*scale;
            }
            current.tracking=t;current.active=true;current.managerAddress=reinterpret_cast<uintptr_t>(self);
            current.playerInstance=active==manager+0x6f0?*reinterpret_cast<void**>(active+0xaa0):nullptr;
            if(current.playerInstance && movementDirection!=DirectionConfig::Source::Mouse) {
                using FindPlayer=unsigned char*(__cdecl*)(void*);
                auto player=reinterpret_cast<FindPlayer>(base+0x6032c0-0x400000)(current.playerInstance);
                if(player)current.inputIndex=*reinterpret_cast<uint32_t*>(player+0x1c);
            }
            if(experimentalMotionControls && t.rightController.valid && active==manager+0x6f0) {
                auto entity=*reinterpret_cast<void**>(active+0xaa0);
                using Equipped=unsigned char*(__cdecl*)(void*);
                auto holder=entity?reinterpret_cast<Equipped>(base+0x66af40-0x400000)(entity):nullptr;
                auto weapon=holder?*reinterpret_cast<unsigned char**>(holder+0x14):nullptr;
                if(weapon && *reinterpret_cast<uintptr_t*>(weapon)==base+0xab1944-0x400000) {
                    using FindInstance=void*(__cdecl*)(uint32_t);
                    auto handle=*reinterpret_cast<uint32_t*>(weapon+0x6c);
                    auto instance=handle!=0x7fffffffu?reinterpret_cast<FindInstance>(base+0x6082a0-0x400000)(handle):nullptr;
                    if(instance)weaponPose={weapon,instance,CameraMath::ControllerMuzzle(renderBase,reference,
                        t.rightController.aim,worldScale,controllerMuzzleForward),t.tick,true,*reinterpret_cast<void**>(weapon+0x64)};
                }
            }
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
void* Get(void* self,Getter original,int kind,uintptr_t caller) {
    if(senseQueries && kind<2) {
        CameraMath::Matrix target;
        thread_local CameraMath::Matrix targetMatrices[2];
        if(DirectionWorld(interactionAim,target,nullptr,reinterpret_cast<uintptr_t>(self))) {
            targetMatrices[kind]=kind?CameraMath::InverseRigid(target):target;
            ++interactionQueries;return targetMatrices[kind].m;
        }
        return original(self);
    }
    // Locomotion already receives reoriented axes; its secondary camera
    // consumers must keep the native basis to avoid applying head yaw twice.
    if(kind<2 && caller>=base+0x77a930-0x400000 && caller<base+0x77bd80-0x400000)return original(self);
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
void* __fastcall WorldHook(void* self,void*){return Get(self,originalWorld,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
void* __fastcall ViewHook(void* self,void*){return Get(self,originalView,1,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
void* __fastcall ManagerHook(void* self,void*){return Get(self,originalManager,2,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
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
    SceneTrace trace{};trace.event=1;trace.reason=1;
    if(viewport){auto v=static_cast<float*>(viewport);trace.fov=v[8];trace.nearZ=v[6];trace.farZ=v[7];trace.viewport=CameraMath::Load(v+12);}
    trace.original=snapshot.originalWorld;trace.tracked=snapshot.world;trace.pose=snapshot.tracking.id;
    if(viewport && snapshot.active) {
        auto p=static_cast<float*>(viewport);auto matrix=CameraMath::Load(p+12);
        if(p[8]>0 && p[7]>1000 && (Match(matrix,snapshot.originalWorld)||Match(matrix,snapshot.world))) {
            trace.reason=Match(matrix,snapshot.originalWorld)?5:6;
            memcpy(adjusted,viewport,sizeof(adjusted));auto v=reinterpret_cast<float*>(adjusted);
            memcpy(v+12,snapshot.world.m,64);for(int i=4;i<7;i++)v[12+i]=-v[12+i];
            float maxX=0,maxY=0;
            for(const auto& eye:snapshot.tracking.eyes) {
                maxX=std::max(maxX,std::max(std::abs(std::tan(eye.left)),std::abs(std::tan(eye.right))));
                maxY=std::max(maxY,std::max(std::abs(std::tan(eye.up)),std::abs(std::tan(eye.down))));
            }
            v[8]=2*std::atan(maxY);v[9]=maxX/maxY;viewport=adjusted;
        } else {trace.reason=(p[8]>0 && p[7]>1000)?2:3;snapshot.active=false;}
    }
    void* result=originalCreate(self,viewport,target,depth,source,sourceDepth,flags);
    {
        std::lock_guard lock(stateMutex);
        if(snapshot.active && result) {scenes.Store(result,snapshot,frameId.load()+1);++taggedScenes;}
        else scenes.Erase(result);
        if(trace.fov>0 && trace.farZ>1000){trace.frame=frameId.load()+1;trace.tick=GetTickCount64();trace.scene=reinterpret_cast<uintptr_t>(result);trace.cached=uint32_t(scenes.Size());Trace(trace);}
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
    {
        std::lock_guard lock(stateMutex);auto scene=static_cast<unsigned char*>(self)-4;drawing=scenes.Find(scene);
        auto v=reinterpret_cast<float*>(scene+0x10);
        if(v[8]>0 && v[7]>1000){
            SceneTrace trace{};trace.frame=frameId.load()+1;trace.tick=GetTickCount64();trace.pose=drawing.tracking.id;trace.scene=reinterpret_cast<uintptr_t>(scene);
            trace.event=2;trace.reason=drawing.active?5:1;trace.cached=uint32_t(scenes.Size());trace.fov=v[8];trace.nearZ=v[6];trace.farZ=v[7];
            trace.viewport=CameraMath::Load(v+12);trace.original=drawing.originalWorld;trace.tracked=drawing.world;Trace(trace);
        }
    }
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
    if(effectsCapture && stream && primitiveState && !*reinterpret_cast<void**>(primitiveState+0x20) && effectCount<effectTrace.size()) {
        auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        auto& t=effectTrace[effectCount++];t={};t.primitive=reinterpret_cast<uintptr_t>(self);t.material=*reinterpret_cast<uintptr_t*>(primitiveState+0xc);
        t.flags=*reinterpret_cast<uint32_t*>(primitiveState);t.eye=state[0x5ea]?0:1;t.active=snapshot.active;t.stereo=state[0x5e9];t.overrideStereo=state[0x5e8];
        auto params=*reinterpret_cast<float**>(primitiveState+0x1c);if(params)memcpy(t.params,params,sizeof(t.params));
        auto projection=*reinterpret_cast<float**>(state+0x540);t.projection=CameraMath::Load(projection?projection:reinterpret_cast<float*>(state+0x440));
        t.view=CameraMath::Load(state+0x480);t.world=CameraMath::Load(state+0x500);
    }
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
        auto source=CameraMath::Load(projection);
        auto p=effectsFix&&eyeViewFix?CameraMath::EyeFrustum(source,drawing.tracking,eye):CameraMath::EyeProjection(source,drawing.tracking,eye,worldScale);
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
        ,{0x545cf0,(void*)&UniformsHook,(void**)&originalUniforms,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x04\x02\x00\x00",12}
        ,{0x552130,(void*)&RenderStateHook,(void**)&originalRenderState,"\x56\x8b\xf1\x80\x7e\x19\x00",7}
        ,{0x986cd0,(void*)&VideoCreateHook,(void**)&originalVideoCreate,"\x33\xc0\x56\x8b\xf1",5}
        ,{0x986b40,(void*)&VideoDestroyHook,(void**)&originalVideoDestroy,"\x56\x8b\xf1\x57",4}
        ,{0x71bb20,(void*)&BillboardsHook,(void**)&originalBillboards,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x74f3e0,(void*)&WeaponMuzzleHook,(void**)&originalWeaponMuzzle,"\x53\x56\x8b\xf1\x8b\x46\x78",7}
        ,{0x750dc0,(void*)&WeaponAimHook,(void**)&originalWeaponAim,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x720930,(void*)&ActorDrawHook,(void**)&originalActorDraw,"\x56\x8b\xf1\x8b\x46\x08",6}
        ,{0x723190,(void*)&WeaponDrawHook,(void**)&originalWeaponDraw,"\x8b\x44\x24\x08\x56",5}
        ,{0x60c200,(void*)&SkeletonHook,(void**)&originalSkeleton,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x4899d0,(void*)&AttachmentHook,(void**)&originalAttachment,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x5b1a70,(void*)&BoundsHook,(void**)&originalBounds,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x689b40,(void*)&SenseUpdateHook,(void**)&originalSenseUpdate,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x69fc90,(void*)&PlayerWorldHook,(void**)&originalPlayerWorld,"\x8d\x41\x40\xc3",4}
        ,{0x4aeae0,(void*)&InputAxisHook,(void**)&originalInputAxis,"\x80\x7c\x24\x08\x00",5}
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
    lockVerticalCamera=GetPrivateProfileIntW(L"VR",L"LockVerticalCamera",0,config)!=0;
    levelRecenter=GetPrivateProfileIntW(L"VR",L"LevelRecenter",1,config)!=0;
    motionControls=DirectionConfig::MotionEnabled(config);
    experimentalMotionControls=motionControls && GetPrivateProfileIntW(L"VR",L"ExperimentalMotionControls",0,config)!=0;
    controllerHideArms=GetPrivateProfileIntW(L"VR",L"ControllerHideArms",1,config)!=0;
    interactionAim=DirectionConfig::Read(config,L"InteractionAim");
    movementDirection=DirectionConfig::Read(config,L"MovementDirection");
    GetPrivateProfileStringW(L"VR",L"ControllerMuzzleForwardMetres",L"0.25",scaleText,32,config);
    float muzzleForward=static_cast<float>(_wtof(scaleText));
    if(std::isfinite(muzzleForward) && muzzleForward>=0 && muzzleForward<=1)controllerMuzzleForward=muzzleForward;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"Camera hooks base=%p enabled=%d unitsPerMetre=%g lockVerticalCamera=%d levelRecenter=%d F6=toggle F9=recenter\n",reinterpret_cast<void*>(base),enabled,worldScale,lockVerticalCamera,levelRecenter);fclose(f);
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
    // Menus/videos can keep presenting while simulation (and camera updates)
    // is paused. Refresh here as well, before the following frame is built.
    RefreshScreenMode();if(screenReasons)current.active=false;
    if(effectsCapture){SaveEffects(frame);effectCount=0;shaderTrace.End();}effectsCapture=capture;
    if(capture)shaderTrace.Begin(frame+1);
    auto completed=pairInfo;pairInfo={};
    scenes.Complete(frame);
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
        SaveTrace(frame);
        FILE* motion{};if(!fopen_s(&motion,"DeusExHRVR-camera.log","a")) {
            fprintf(motion,"controller enabled=%d valid=%u active=%d weapon=%p instance=%p modelDraws=%llu muzzleQueries=%llu aimQueries=%llu hiddenArms=%llu owner=%p attachments=%llu bounds=%llu\n",
                experimentalMotionControls,current.tracking.rightController.valid,weaponPose.active,weaponPose.weapon,weaponPose.instance,
                controllerDraws.load(),controllerMuzzles.load(),controllerAimQueries.load(),controllerHiddenArms.load(),weaponPose.owner,
                controllerAttachments.load(),controllerBounds.load());
            Matrix(motion,"controllerMuzzle",weaponPose.muzzle.m);
            fprintf(motion,"directions interaction=%s movement=%s interactionQueries=%llu movementAxes=%llu motionControls=%d inputIndex=%u actions=%p/%p lastInput=%g,%g lastOutput=%g,%g heading=%g\n",
                DirectionConfig::Name(interactionAim),DirectionConfig::Name(movementDirection),
                interactionQueries.load(),movementAxes.load(),motionControls,current.inputIndex,walkAction.load(),strafeAction.load(),
                lastMoveInput[0],lastMoveInput[1],lastMoveOutput[0],lastMoveOutput[1],lastMoveHeading);
            {std::lock_guard lock(attachmentTraceMutex);for(const auto& t:attachmentTrace)if(t.queries)
                fprintf(motion,"attachment caller=%08x bone=%d owner=%d queries=%llu corrected=%llu\n",
                    unsigned(t.caller),t.bone,t.owner,t.queries,t.corrected);}
            fclose(motion);
        }
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"HUD plane draws=%llu matrices=%llu frame=%llu trackingReadContentions=%llu rejectedSamples=%llu\n",hudDraws,hudMatrices,frame,trackingReader.reused,trackingReader.rejected);fclose(f);}
    }
    bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0,f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")) {
            fprintf(f,"sprites frame=%llu flaresRemoved=%llu skyFix=%d skyCorrections=%llu renderPitchLock=%d screenReasons=%u\n",frame,removedFlareSprites,skyFix,skyCorrections,lockVerticalCamera,screenReasons);fclose(f);
        }
    }
    bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
    if(f7&&!f7Down){effectsFix=!effectsFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"effectsFix=%d frame=%llu\n",effectsFix,frame);fclose(f);}}f7Down=f7;
    bool f4=(GetAsyncKeyState(VK_F4)&0x8000)!=0;
    if(f4&&!f4Down){eyeViewFix=!eyeViewFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"eyeViewFix=%d frame=%llu\n",eyeViewFix,frame);fclose(f);}}f4Down=f4;
    bool f3=(GetAsyncKeyState(VK_F3)&0x8000)!=0;
    bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    if(f11&&!f11Down){skyFix=!skyFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"skyFix=%d frame=%llu\n",skyFix,frame);fclose(f);}}f11Down=f11;
    if(f3&&!f3Down){instanceFix=!instanceFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"instanceFix=%d frame=%llu\n",instanceFix,frame);fclose(f);}}f3Down=f3;
    if(capture){FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"instanceFix=%d correctedDraws=%llu frame=%llu\n",instanceFix,instanceCorrections,frame);fclose(f);}}
    if(f6&&!f6Down){requested=!requested;referenceValid=false;current.active=false;}
    if(f9&&!f9Down)recenterRequested=true;
    if((f6&&!f6Down)||(f9&&!f9Down)) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"tracking requested=%d recenter=%d frame=%llu\n",requested,recenterRequested,frame);fclose(f);}
    }
    f6Down=f6;f9Down=f9;return completed;
}
void SetChannel(Transport::Header* header){std::lock_guard lock(stateMutex);channel=header;trackingReader={};if(!header){current.active=false;referenceValid=false;}}
}
