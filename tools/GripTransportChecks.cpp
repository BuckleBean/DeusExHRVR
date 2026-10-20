// Exercise the production ControllerInput against a deterministic OpenXR mock.
// No headset/runtime is loaded by this executable.
#include "ControllerInput.h"
#include <map>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
namespace Mock {
std::map<XrAction,std::string> names;
std::map<XrPath,std::string> paths;
std::map<XrSpace,XrAction> spaces;
struct State {bool active=true;XrResult stateResult=XR_SUCCESS,locateResult=XR_SUCCESS;XrSpaceLocationFlags flags=3;XrPosef pose{{0,0,0,1},{0,0,0}};};
std::map<std::string,State> states;
std::set<std::string> bindings;
uintptr_t next=1;
XrResult sync=XR_SUCCESS;
unsigned locateCalls{},destroyedSpaces{};
XrSpace expectedBase=(XrSpace)(uintptr_t)900;
constexpr XrTime expectedTime=123456789;
}
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out){*out=(XrActionSet)(uintptr_t)1;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet,const XrActionCreateInfo* i,XrAction* out){*out=(XrAction)Mock::next++;Mock::names[*out]=i->actionName;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance,const char* s,XrPath* out){*out=Mock::next++;Mock::paths[*out]=s;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding* b){
    for(uint32_t i=0;i<b->countSuggestedBindings;i++)Mock::bindings.insert(Mock::paths[b->interactionProfile]+":"+Mock::paths[b->suggestedBindings[i].binding]);return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession,const XrActionSpaceCreateInfo* i,XrSpace* out){
    Check(i->poseInActionSpace.orientation.w==1 && i->poseInActionSpace.position.x==0,"action space must have identity offset");
    *out=(XrSpace)Mock::next++;Mock::spaces[*out]=i->action;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace s){Check(Mock::spaces.erase(s)==1,"space destroyed twice");++Mock::destroyedSpaces;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession,const XrActionsSyncInfo*){return Mock::sync;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession,const XrActionStateGetInfo* i,XrActionStatePose* out){auto& s=Mock::states[Mock::names[i->action]];out->isActive=s.active;return s.stateResult;}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace source,XrSpace base,XrTime time,XrSpaceLocation* out){
    Check(base==Mock::expectedBase && time==Mock::expectedTime,"poses must share base and predicted display time");++Mock::locateCalls;
    auto& s=Mock::states[Mock::names[Mock::spaces.at(source)]];out->pose=s.pose;out->locationFlags=s.flags;return s.locateResult;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* out){out->isActive=1;out->currentState=0;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession,const XrActionStateGetInfo*,XrActionStateFloat* out){out->isActive=1;out->currentState=.25f;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* out){out->isActive=1;out->currentState={.1f,.2f};return XR_SUCCESS;}
}
Transport::Tracking Fixture(){
    Transport::Tracking t{};t.id=0x1020304050607080ull;t.tick=1000;t.valid=1;
    t.head.orientation.w=1;t.head.position={1,2,3};
    t.rightController={{{0,0,0,1},{.1f,.2f,.3f}},1};
    t.leftController={{{0,0,0,1},{-7,2,1}},1};
    t.leftGrip={{{.1f,.2f,.3f,.9f},{-1,-2,-3}},1};
    t.rightGrip={{{-.1f,-.2f,-.3f,.9f},{4,5,6}},1};
    t.controllerButtons=3;t.gamepad.valid=1;t.gamepad.buttons=0x1234;return t;
}
int main(int argc,char** argv){try{
    ControllerInput input;
    Check(input.Init((XrInstance)(uintptr_t)1,(XrSession)(uintptr_t)1),"initialize actions");
    for(auto profile:{"/interaction_profiles/oculus/touch_controller","/interaction_profiles/khr/simple_controller"})
        for(auto path:{"/user/hand/right/input/aim/pose","/user/hand/left/input/aim/pose","/user/hand/left/input/grip/pose","/user/hand/right/input/grip/pose"})
            Check(Mock::bindings.contains(std::string(profile)+":"+path),"profile missing pose binding");
    auto& left=Mock::states["left_hand_grip_pose"];auto& right=Mock::states["right_hand_grip_pose"];auto& aim=Mock::states["right_weapon_aim"];
    left.pose.position={-1,-2,-3};right.pose.position={4,5,6};aim.pose.position={7,8,9};
    Transport::Tracking t{};t.tick=1000;
    auto sample=[&](bool focus=true){input.Sample((XrSession)(uintptr_t)1,Mock::expectedBase,Mock::expectedTime,focus,t);};
    auto& leftAim=Mock::states["left_movement_aim"];leftAim.pose.position={-7,2,1};
    sample();Check(t.leftController.valid&&t.leftController.aim.position.x==-7,"left aim transported independently");
    leftAim.flags=1;sample();Check(!t.leftController.valid&&t.rightController.valid,"invalid left aim does not disable right aim");
    leftAim.flags=3;sample(false);Check(!t.leftController.valid,"left aim cleared on focus loss");
    sample();
    Check(t.leftGrip.valid && t.rightGrip.valid && t.rightController.valid,"all three poses valid");
    Check(t.leftGrip.pose.position.x==-1 && t.rightGrip.pose.position.x==4 && t.rightController.aim.position.x==7,"poses independently copied without offsets");
    for(auto flags:{XrSpaceLocationFlags(0),XrSpaceLocationFlags(1),XrSpaceLocationFlags(2)}){
        left.flags=flags;sample();Check(!t.leftGrip.valid && t.leftGrip.pose.position.x==0 && t.rightGrip.valid && t.rightController.valid,"partial left tracking cleared independently");
    }
    left.flags=3;right.active=false;sample();Check(t.leftGrip.valid && !t.rightGrip.valid && t.rightController.valid,"inactive right grip does not disable aim");
    right.active=true;aim.active=false;sample();Check(!t.rightController.valid && t.leftGrip.valid && t.rightGrip.valid,"grips independent of aim");aim.active=true;
    left.stateResult=XR_ERROR_RUNTIME_FAILURE;sample();Check(!t.leftGrip.valid && t.rightGrip.valid,"pose query failure");left.stateResult=XR_SUCCESS;
    right.locateResult=XR_ERROR_RUNTIME_FAILURE;sample();Check(t.leftGrip.valid && !t.rightGrip.valid,"locate failure");right.locateResult=XR_SUCCESS;
    sample();unsigned calls=Mock::locateCalls;sample(false);
    Check(!t.leftGrip.valid && !t.rightGrip.valid && !t.rightController.valid && !t.gamepad.valid && Mock::locateCalls==calls,"focus loss clears sample without querying poses");
    sample();Mock::sync=XR_SESSION_NOT_FOCUSED;sample();Check(!t.leftGrip.valid && !t.rightGrip.valid && !t.rightController.valid,"unfocused sync result clears poses");
    Mock::sync=XR_ERROR_RUNTIME_FAILURE;sample();Check(!t.leftGrip.valid && !t.rightGrip.valid,"sync failure clears poses");Mock::sync=XR_SUCCESS;
    sample();Check(t.leftGrip.valid && t.rightGrip.valid && t.rightController.valid,"tracking recovers");
    input.Reset();input.Reset();Check(Mock::destroyedSpaces==4 && Mock::spaces.empty(),"all spaces cleaned up once");sample();Check(!t.leftGrip.valid && !t.rightGrip.valid,"reset clears sampling");
    Transport::Header h{};h.magic=Transport::Magic;h.version=Transport::Version;h.pid=123;
    Check(Transport::Compatible(&h,123),"matching transport");h.version=5;Check(!Transport::Compatible(&h,123),"v5 must be rejected");h.version=Transport::Version;
    Check(!Transport::Compatible(&h,124) && !Transport::Compatible(nullptr,123),"wrong process/null rejected");
    auto fixture=Fixture();Transport::WriteTracking(&h,fixture);Transport::Tracking out{};Transport::TrackingReader reader;
    Check(reader.Read(&h,out,1000) && !memcmp(&out,&fixture,sizeof(out)),"mailbox preserves both grip poses");
    h.trackingLock=1;Check(reader.Read(&h,out,1249) && out.leftGrip.valid,"brief contention retains fresh sample");
    Check(!reader.Read(&h,out,1250),"stale sample rejected");h.trackingLock=0;
    fixture.tick=1251;fixture.leftGrip={};Transport::WriteTracking(&h,fixture);
    Check(reader.Read(&h,out,1251) && !out.leftGrip.valid && out.rightGrip.valid,"new invalid left pose overrides cache");
    Check(!reader.Read(nullptr,out,1252) && !reader.latest.rightGrip.valid,"disconnected mailbox clears cache");
    if(argc==3){
        fixture=Fixture();
        if(std::string(argv[1])=="--write"){
            std::ofstream file(argv[2],std::ios::binary);file.write(reinterpret_cast<const char*>(&fixture),sizeof(fixture));Check(bool(file),"write cross-architecture fixture");
        }else if(std::string(argv[1])=="--read"){
            std::ifstream file(argv[2],std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});
            Check(bytes.size()==sizeof(fixture) && !memcmp(bytes.data(),&fixture,sizeof(fixture)),"cross-architecture fixture mismatch");
        }else Check(false,"unknown mode");
    }
    printf("PASS %zu-bit: both grip bindings, independent validity, focus/loss/recovery, cleanup, v5 rejection, mailbox expiry and ABI (%zu/%zu/%zu)\n",sizeof(void*)*8,sizeof(Transport::Tracking),sizeof(Transport::RenderInfo),sizeof(Transport::Header));return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
