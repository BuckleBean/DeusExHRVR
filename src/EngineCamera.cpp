#include "EngineCamera.h"
#include "CameraMath.h"
#include "NativeHeading.h"
#include "CameraPosition.h"
#include "DirectionConfig.h"
#include "NativeBounds.h"
#include "SceneCache.h"
#include "EngineShaderTrace.h"
#include "EffectShader.h"
#include "ShaderSwap.h"
#include "LumaPasses.h"
#include "LumaSettingsCB.h"
#include "DisplaySettings.h"
#include "ScreenMode.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include "ImmersiveScope.h"
#include "WeaponCalibration.h"
#include "HandMeshDiagnostic.h"
#include "HandDrawDiagnostic.h"
#include "HandOnlyGeometry.h"
#include "ModelHandAlignment.h"
#include "NativeHandWeapon.h"
#include "MuzzleQueryDiagnostic.h"
#include "HandWeaponMotionDiagnostic.h"

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
bool immersiveScope{},immersiveScopeActive{};
float scopeMagnification=4.f;
ImmersiveScope::Gesture scopeGesture;
std::atomic<uint64_t> scopePulseUntil{};
uint64_t scopeToggleAfter{};
bool autoScopeOwned{};
WeaponCalibration::Session calibration;
WeaponCalibration::Store calibrationStore;
bool lockVerticalCamera{};
// From upstream PR #8 by Carnuntum (fixed dialogue camera): a conversation cuts
// the gameplay camera between shots of the NPC and Jensen. With
// FixedDialogueCamera=1 the VR base stays at the last gameplay pose (Jensen's
// eyes) while a conversation controls the camera, until the game camera has come
// back afterwards; the headset still turns and moves the view, and Jensen's own
// model is hidden.
//
// The shots run in PlayerCamera's CameraBone sub-mode, which other scripted
// shots share, so the conversation itself is hooked instead of the camera.
// Every conversation type (ConversationGraph, Tier2, Briefing) picks its camera
// shots through ConversationGraphNode_CameraTrack's execute (0x6c7d70), which
// calls 0x6bddb0 (thiscall track, flag; track 0xffff clears it) on a
// participant. A participant's +8 is its conversation; participants leave
// through 0x6bed40 and 0x6bf3e0, which both skip unless +0x50 (entered) is set.
bool fixedDialogueCamera{};
std::atomic<void*> conversationCamera{}; // conversation that last picked a camera track
CameraMath::Matrix lastPlayerBase;uint64_t lastPlayerBaseTick{},dialogueEndTick{};
bool dialogueCamera{},holdingPlayerBase{};
void NoteConversation(const char* event,void* conversation) {
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"conversation %s %p\n",event,conversation);fclose(f);}
}
using ConversationTrack=void(__thiscall*)(void*,uint32_t,uint32_t);
using ConversationExit=void(__thiscall*)(void*);
ConversationTrack originalConversationTrack{};
ConversationExit originalConversationExit[2]{};
void __fastcall ConversationTrackHook(void* self,void*,uint32_t track,uint32_t flag) {
    originalConversationTrack(self,track,flag);
    auto conversation=*reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8);
    if((track&0xffff)!=0xffff) {
        if(conversationCamera.exchange(conversation)!=conversation)NoteConversation("camera",conversation);
    } else {
        void* expected=conversation;
        if(conversationCamera.compare_exchange_strong(expected,nullptr))NoteConversation("cameraCleared",conversation);
    }
}
void ConversationLeave(void* self) {
    auto p=static_cast<unsigned char*>(self);
    if(!p || !p[0x50])return;
    void* expected=*reinterpret_cast<void**>(p+8);
    if(expected && conversationCamera.compare_exchange_strong(expected,nullptr))NoteConversation("exit",expected);
}
void __fastcall ConversationExitHook0(void* self,void*){ConversationLeave(self);originalConversationExit[0](self);}
void __fastcall ConversationExitHook1(void* self,void*){ConversationLeave(self);originalConversationExit[1](self);}
bool experimentalMotionControls{};
// fubar's per-weapon grip calibration (both thumbstick clicks, hold, release to
// save). Default on. At 0 the saved offset is still applied; only the capture
// is skipped, so play can no longer overwrite DeusExHRVR-weapons.ini.
bool weaponCalibrationEnabled=true;
bool motionControls=true;
bool controllerHideArms=true;
// ControllerHands: show the player's hands at the motion controller instead of
// hiding the whole actor mesh. The bone numbers were measured, not guessed - a
// logging build sampled the render palette head-relative over eight snapshots,
// and bones 157/158 sit +-30 cm apart and 61 cm below the eyes with the weapon
// away, then 4.3 cm apart and 23 cm ahead with it drawn. Default 0, so with this
// off not one line of behaviour changes.
bool controllerHands{};
int controllerHandBone=158;   // the hand moved onto the controller
int controllerHandBone2=999;  // optional second hand, left where the animation put it
// Which half of the effect runs. The first build did both at once and showed
// nothing, which three different faults would all produce, so they are separable
// now: 0 = move the hand and collapse the rest (the feature), 1 = collapse only,
// leaving the hand where the animation put it (does keeping this bone leave any
// geometry on screen?), 2 = move only, collapsing nothing (does this bone drag
// any vertices with it?), 3 = translate EVERY bone by the same delta, collapsing
// nothing, which asks the blunt question the first three modes could not: does
// this palette drive the arms on screen at all?
int controllerHandsMode{};
std::atomic<bool> controllerHandsModelAlignment{};
std::atomic<bool> controllerHandsLeft{true};
// Reinstated 2026-10-17. 0 in the INI restores the animated wrist, no rebuild.
std::atomic<bool> controllerHandsNeutralWrist{true};
std::atomic<uint64_t> neutralWristFailed{};
std::atomic<bool> controllerHandsForceFallback{}; // live test override, expires after 90 seconds
std::atomic<uint64_t> modelHandApplied{},modelHandNoGrip{},modelHandBadModel{},modelHandBadTransform{};
std::atomic<uint64_t> nativePairUsed{},nativePairFailed{},nativePairCompared{},nativePairMismatch{};
std::atomic<bool> nativePairFault{};
std::atomic<uint64_t> nativePairOwnerFailed{},nativePairModelFailed{},nativePairBoneFailed{},nativePairMuzzleFailed{};
std::atomic<float> nativePairMaxDifference{};

// Modes 4 and 5 keep bones by PROXIMITY to the hand instead of by index, because
// index 158 sits exactly at the hand yet skins nothing, so the real hand bone's
// number is unknown while its POSITION is not. Radius in centimetres.
float controllerHandsRadius=15.f;
// Mode 6 bisection bounds. The game is Z-up, so band 1 rises and band 2 falls by
// ControllerHandsRadiusCm; band 3 is untouched. Three outcomes per launch.
int controllerHandsSplitA=74,controllerHandsSplitB=148;
// Mode 7 keeps [KeepFrom, KeepTo) and collapses the rest. Bisection found the
// band; this asks whether that band is worth anything without isolating one bone.
int controllerHandsKeepFrom=74,controllerHandsKeepTo=82;
// Meters below the grip that discarded vertices are sent. 0 keeps them on the
// grip, which is also where the kept hand is - suspected cause of the ghosting.
float controllerHandsCollapseDrop{};
// Hand probe. Runs inside AttachmentHook using the pointers the GAME just
// passed, so nothing is stored and nothing can dangle across a level load -
// which is what crashed the first attempt on the loading screen.
std::atomic<uint32_t> handProbeSamples{};
std::atomic<uint64_t> handProbeNextTick{};
int controllerHandsProbeFrom=100,controllerHandsProbeTo=160;
// Default OFF, and it used to fire on controllerHands alone. It crashed the
// game twice on 2026-10-07, and the output it did produce settles that the
// route is dead as written: all 60 indices returned ONE identical point,
// equal to bone 0's palette translation from the previous day's capture -
// the actor's root. The getter searches a counted list, so palette numbers
// simply are not names it knows. Kept for reference, not armed.
bool controllerHandsProbe{};
// ---- Modes 12-14: placement from a calibrated bind pose ----
// The blocker this removes: handDelta is inverse(nativeMuzzle) x pose.muzzle,
// which pins the WEAPON's muzzle onto the controller's. Armed, the gun is in
// the hand, so the hand lands at the hand. HOLSTERED THE GUN IS ON THE BODY,
// so the same transform drags the holster to the controller and the shoulder
// arrives there - which is exactly what Mark saw on 2026-10-07.
//
// Driving the hand needs the HAND's own world transform, and the palette does
// not hold it: an entry is inverse(bindPose) x boneWorld, so its translation
// is where the model ORIGIN maps to, not where the bone is. That is what made
// bones 157/158 track the hands beautifully while skinning nothing.
//
// But boneWorld = bindPose x paletteEntry, and bindPose is a CONSTANT of the
// model. So one instant where the hand's true world transform is known solves
// for it, and Mark supplies that instant by holding his controller on the
// visible hand. Afterwards the hand's world transform follows from the palette
// alone, every frame, with nothing asked of the game - no attachment getter,
// so none of the crash risk that route carried.
//
// Everything unknown folds into that constant: the bind pose, the bone's axis
// convention, and ControllerMuzzle's forward push. None has to be solved.
//
// VERIFIED BEFORE BUILDING, from the eight snapshots still in
// DeusExHRVR-camera.log: bone 0's palette translation sits 565-568 units below
// the camera in seven of them, against a standing eye height of 565-567
// measured independently in September. That is the signature of the root's
// bind pose sitting at the model origin, i.e. of the palette holding
// inverse(bindPose) x boneWorld.
bool handCalibrated{};
uint32_t handBindCount{};
CameraMath::Matrix handBind[512]{};
// The gun's place in the hand, taken from the game's own animation rather than
// from anyone aiming at anything: offset = nativeMuzzle x inverse(wristWorld).
// Constant while the gun is gripped, which is ALSO how we know it is gripped -
// a holstered gun rides the body, so this value moves when the hand moves.
bool controllerHandsWeapon{};
// Horizontal distance from the player entity origin, in inches, beyond which
// the whole hands feature goes dormant: cover, ladders and the Icarus landing
// move the camera off the body, and a hand driven to the controller while the
// body stays behind stretches everything between them.
float controllerHandsMaxOffsetIn=12.f;
// The game's own player camera modes, read from its RTTI. Each is a subobject
// at a fixed offset inside PlayerCamera (constructor 0x6AB000); the enum order
// is PlayerCamera::GetModeByIndex's jump table at 0x6AA110. The mod already
// depends on two of these numbers - Hacking's +0x430 and its vtable 0xAA774C -
// which is the cross-check that this layout is right.
struct CameraModeInfo {const char* name;uint32_t offset;uint32_t vtable;};
const CameraModeInfo cameraModes[9]={
    {"Combat",    0x0c0,0xaa7918},   // 0  first person
    {"Cover",     0x144,0xaa79ac},   // 1  THIRD PERSON
    {"Ladder",    0x3a8,0xaa7ac8},   // 2  THIRD PERSON
    {"Dialog",    0x230,0xaa7a20},   // 3  conversation framing
    {"CameraBone",0x330,0xaa78c4},   // 4  camera on an animation bone
    {"Hacking",   0x430,0xaa774c},   // 5  the hacking screen
    {"LyingDown", 0x580,0xaa7844},   // 6  on the ground
    {"IronSight", 0x640,0xaa7a74},   // 7  first person, aiming
    {"Orbit",     0x67c,0xaa7b3c},   // 8  orbiting the body
};
// Which modes the hands step aside for. Bit N = mode N above. Default = Cover,
// Ladder, CameraBone, LyingDown, Orbit = 2+4+16+64+256 = 342. Combat and
// IronSight are first person; Dialog and Hacking already have their own
// handling and are deliberately left alone.
uint32_t controllerHandsSuspendModes=342;
// The cameras CameraManager owns, from its constructor at 0x6A0E89; the class
// names come from the vtable each subobject's constructor writes, through the
// game's RTTI. The player's camera is +0x6F0, which the mod has always used.
struct CameraSlotInfo {const char* name;uint32_t offset;};
const CameraSlotInfo cameraSlots[9]={
    {"ObjectDebug",   0x060},
    {"ICamera_150",   0x150},
    {"ICamera_220",   0x220},
    {"Cinematic",     0x340},
    {"ICamera_490",   0x490},
    {"Free",          0x550},
    {"ICamera_630",   0x630},
    {"Player",        0x6f0},
    {"OrbitDebug",   0x11a0},
};
std::atomic<bool> activeCameraIsPlayer{true};
const void* activeCameraLast=nullptr;
// -1 = not identified. Written on the update thread, read on the render thread.
std::atomic<int> cameraModeIndex{-1};
int cameraModeLast=-2;
uint32_t cameraModeUnknownLogged{};
std::atomic<bool> handsSuspendedByMode{false};
// Set ONLY on positive evidence that the camera is away from the body. Every
// failure path leaves it false, so the hands keep working.
std::atomic<bool> handsFarFromBody{false};
bool handsFarLast=false;
std::atomic<bool> handDriveAllowed{true};
bool handDriveAllowedLast=true;
// Bone whose own world position the collapse apex sits on, so the hidden
// sleeve caps the cut instead of spiking away from it. -1 = measure from the
// grip, the old behaviour.
int controllerHandsCollapseBone=-1;
// One solved grip per weapon, keyed by the asset ID at weapon+0x58 - the same
// key fubar's calibration store uses. Each weapon's animation holds its own
// grip, so a single shared value is wrong: that was the first version's bug.
// `anchor` is what this weapon measured when it was FIRST stored or loaded.
// Refinement is bounded against it rather than against `offset`, so corrections
// cannot accumulate: every step is measured from the same fixed point.
struct HandGrip {uint32_t key;CameraMath::Matrix offset;CameraMath::Matrix anchor;};
HandGrip handGrips[64]{};
uint32_t handGripCount{};
uint32_t handWeaponKey=0xffffffffu;
uint32_t handGripSolvingKey=0xffffffffu;
// Set when the equipped weapon changes: a switch is the moment to re-measure,
// because the gun has just been drawn and the hand is moving as it comes up.
// The existing value stays in use until the new one validates.
uint32_t handGripSeenKey=0xffffffffu;
bool handGripRemeasure{};
// The observer. Records what the inputs were doing across the window of
// agreeing samples that produced a solve, so a bad one can be explained instead
// of guessed at.
bool handGripWindowOpen{};
uint64_t handGripWindowStart{};
float handGripWristStart[3]{};
float handGripMuzzleStart[3]{};
uint32_t handGripWindowFlags{};   // 1 mode-suspend, 2 distance, 4 non-player camera
int handGripWindowMode=-1;
uint32_t handGripAgreed{};        // observations that matched the stored grip
uint32_t handGripLogged{};        // disagreements written, capped
// The player's own origin, published for the render thread. Walking moves the
// wrist in world space without moving it in the body's frame, and that
// difference is what separates a gun in the hand from one on the body.
std::atomic<float> handBodyOrigin[3]{};
std::atomic<bool> handBodyOriginValid{false};
float handGripBodyStart[3]{};
// Draws where no weapon was in hand. The existing noWeapon counter only moves in
// the identify path, which mode 15 never takes.
std::atomic<uint64_t> handUnarmedDraws{};
// Times the drive was refused because the transform would have torn geometry.
std::atomic<uint64_t> handDriveRejected{};
// The hand pose: the most curled unarmed hand the game has shown, kept so it can
// be put back on when interact is pressed. Band-sized, never more than 32 bones.
// OFF by default from 2026-10-14. Both of its tests - "are the hands empty" and
// "which pose is most curled" - were built on quantities that turned out not to
// mean what I assumed, and it replaced the hand band on most frames. It stays
// off until it has tests that do not depend on them.
bool controllerHandsPose=false;
uint32_t controllerHandsPoseMask=0x4000;     // XINPUT_GAMEPAD_X    = interact
uint32_t controllerHandsSprintMask=0x0100;   // XINPUT_GAMEPAD_LB   = sprint
CameraMath::Matrix handPoseBase[32]{},handPoseBest[32]{};
uint32_t handPoseCount{};
float handPoseBestScore{};
bool handPoseHaveBase{},handPoseHaveBest{};
std::atomic<bool> handPoseInteract{false};
// Sprinting, from the measured eye height rather than from a guess about which
// button it is. Standing 565-567 units, sprinting 541 (2026-09-17 trace).
std::atomic<bool> handPoseSprinting{false};
CameraMath::Matrix handPoseSprint[32]{};
bool handPoseHaveSprint{};
uint32_t handPoseLogged{},handPoseSprintLogged{};
CameraMath::Matrix handGripPrev{};
bool handGripHavePrev{};
CameraMath::Matrix handGripAccum{};
uint32_t handGripSamples{};
float handGripRef[3]{};
HandGrip* FindHandGripRecord(uint32_t key) {
    for(uint32_t i=0;i<handGripCount;i++)
        if(handGrips[i].key==key)return &handGrips[i];
    return nullptr;
}
const CameraMath::Matrix* FindHandGrip(uint32_t key) {
    for(uint32_t i=0;i<handGripCount;i++)
        if(handGrips[i].key==key)return &handGrips[i].offset;
    return nullptr;
}
// Solved for EVERY bone at one instant, not just the chosen one, so stepping
// the drive bone later costs no re-alignment - otherwise each step of the
// search would cost Mark another calibration.
int controllerHandsDriveBone=152;
// Calibration fires on a STEADY HOLD, never on a clock. A countdown coupled
// the capture to the moment a message of mine arrived, which left Mark standing
// in the headset waiting on my typing - his words: "I can't work like this."
// A hold has no such coupling: arm it, he gets into position whenever he likes,
// and it captures itself. It also measures exactly what makes a calibration
// good, so the quality check and the trigger are the same thing.
std::atomic<int> handCalibrateArmed{};
// Arming gesture state. armTick lets the capture path require that the steady
// window STARTED after arming, so a hand that happened to be still beforehand
// cannot fire the capture instantly - and it avoids writing handSteadyValid
// from the camera thread, which the render thread owns.
std::atomic<uint64_t> handCalibrateArmTick{};
uint64_t handArmGestureSince{};
// Why the target was or was not usable. A silent fallback would hide the very
// weapon dependence this change removed, so it is counted and reported.
std::atomic<uint64_t> handRawUsed{},handRawRejected{},handFallbackUsed{},handArmGestures{};
std::atomic<uint32_t> handRawFault{};   // 1 not finite, 2 bad w, 3 not orthonormal
float handRawW15{},handRawRow0{};
int controllerHandsArmGestureMs=1500;
float controllerHandsArmGestureCm=20.f;
float controllerHandsSteadyCm=2.f;
int controllerHandsSteadyMs=1500;
float handSteadyRef[3]{};
uint64_t handSteadySince{};
bool handSteadyValid{};
float handSteadyWorst{};
float handCalibrateQuality=-1.f;
int controllerHandsCalibrateDelayMs{};  // retired; kept so the INI key is harmless
// Mode 14 gate counters. Writing nothing when it fails is the one outcome that
// cannot be acted on, which is what the first attempt did.
std::atomic<uint64_t> handIdEnters{},handIdNoWeapon{},handIdBelowGate{};
std::atomic<uint32_t> handIdSampleCount{};
std::atomic<uint64_t> handIdProgressTick{};
float handIdMaxDeg{};
// Declared here so the watcher below can call it; defined once the draw
// counters it reports exist.
void WriteHandsStatus();
// Centimetres in the controller's own axes, applied after calibration.
// By-eye alignment is only so good; this is Dishonored VR's TrimX/Y/Z.
float handTrim[3]{};
// Rotation trim, degrees about the controller's own axes. Position was the only
// thing ever checked or correctable, and an angle error throws the hand much
// further off than an inch of position does.
float handTrimRot[3]{};
// Running sum of the solved bind pose across the hold, so jitter averages out
// instead of being rejected by a tight tolerance.
CameraMath::Matrix handBindAccum[512]{};
uint32_t handBindSamples{};
// Mode 14 identifies the hand bone with no calibration and no guessing. While
// armed the hand grips the gun, so the hand bone's world transform is rigidly
// related to the weapon's. Between two frames that makes the bone's palette
// change CONJUGATE to the weapon's, and conjugate rigid transforms share a
// rotation angle - a scalar that needs neither the bind pose nor the unknown
// constant offset. The hand bone is the one whose angle matches the gun's.
CameraMath::Matrix handIdPrevSkin[512]{};
CameraMath::Matrix handIdPrevMuzzle{};
bool handIdHavePrev{};
double handIdSum[512]{};
double handIdWorst[512]{};
// Each bone's OWN mean rotation per accepted sample. A slot that never moves
// matches the gun trivially whenever the gun's angle is small, so without this
// the ranking can be topped by dead slots - the same trap that made bones
// 157/158 look like hands.
double handIdOwn[512]{};
float controllerHandsIdGateDeg=.5f;
// Mesh-piece hiding. Bit N set drops piece N of the actor's own draw. This is
// the one hiding mechanism that moves nothing: a piece that is never submitted
// cannot drag a shared vertex, which is the failure every collapse variant has.
uint32_t controllerHandsSkipPrimMask{};
// Fraction to shrink a discarded bone to, instead of zeroing it. 0 = zero it,
// i.e. exactly today's behaviour. Zeroing destroys the surface normal of every
// vertex that shares weight across the wrist; scaling keeps the normal's
// direction and only shortens it, which is the one thing no collapse variant
// has tried.
float controllerHandsCollapseScale{};
// Mode 15's weld range: these bones are given the kept wrist bone's final
// matrix, so the forearm rides the hand rigidly and no vertex is dragged.
// Where the collapsed geometry's apex goes, as an offset from the grip in the
// controller's own axes, in INCHES. Back is along the muzzle's second row,
// which points from the muzzle back toward the hand and on toward the elbow -
// the direction from the palm to the wrist.
float controllerHandsCollapseBackIn{};
float controllerHandsCollapseSideIn{};
float controllerHandsCollapseUpIn{};
// The left arm, hidden outright. 0/0 = off. Bones in this range, plus any slot
// holding a transform identical to one of them, are all sent to a single point,
// so every triangle they carry has zero area.
int controllerHandsHideFrom{};
int controllerHandsHideTo{};
std::atomic<uint32_t> handHiddenSlots{};
int controllerHandsWeldFrom=112;
int controllerHandsWeldTo=130;
std::atomic<uint32_t> handPrimPieces{};
// Which pixel shader draws the arm, and whether anything else uses it. The
// answer decides whether a per-draw shader swap - already shipping machinery
// here, for the Luma port - can hide the forearm without touching any other
// character in the game.
std::atomic<uint32_t> handShaderCalls{};
uintptr_t handShaders[16]{};
uint32_t handShaderCount{};
uintptr_t otherShaders[512]{};
uint32_t otherShaderCount{};
int handShaderLogged{};
int handPrimLogged{};
struct HandPrim {uint32_t index;uintptr_t primitive,material;uint32_t flags;};
HandPrim handPrimTrace[64]{};
uint32_t handPrimTraceCount{};
uint32_t handIdSamples{};
bool handIdReported{};
// Live re-read of the ControllerHands keys. Exiting the game stalls and costs a
// Virtual Desktop audio round trip, so a relaunch per bisection round is far more
// expensive than it looks; this makes a round a glance instead.
// It runs on a DETACHED WORKER, never the render thread: GetPrivateProfileInt
// opens the file, and the comment in OnPresent forbids scheduled file IO there -
// that is what made BobTrace hitch. Only these diagnostic keys are refreshed,
// so nothing with side effects (hooks, stance hold, the sleep patch) is touched.
std::wstring handsConfigPath;
__declspec(noinline) void RefreshForcedHandFallback(const wchar_t* path) {
    static bool previousFallbackRequest=false;
    static uint64_t fallbackTestUntil=0;
    const bool forceRequested=GetPrivateProfileIntW(L"VR",L"ControllerHandsForceFallback",0,path)!=0;
    const auto testNow=GetTickCount64();
    if(forceRequested && !previousFallbackRequest)fallbackTestUntil=testNow+90000;
    if(!forceRequested)fallbackTestUntil=0;
    previousFallbackRequest=forceRequested;
    const bool forceActive=forceRequested && testNow<fallbackTestUntil;
    if(controllerHandsForceFallback.exchange(forceActive)!=forceActive) {
        FILE* testLog{};
        if(!fopen_s(&testLog,"DeusExHRVR-hand-only.log","a") && testLog) {
            fprintf(testLog,"fallbackTest active=%d requested=%d tick=%llu expiry=%llu\n",
                int(forceActive),int(forceRequested),testNow,fallbackTestUntil);
            fclose(testLog);
        }
    }
}
void StartHandsConfigWatcher() {
    static std::once_flag once;
    std::call_once(once,[]{
        std::thread([]{
            for(;;) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if(handsConfigPath.empty())continue;
                HandMeshDiagnostic::Flush();
                HandDrawDiagnostic::Flush();
                HandOnlyGeometry::Flush();
                MuzzleQueryDiagnostic::Flush();
                HandWeaponMotionDiagnostic::Flush();
                {
                    static unsigned tick{};
                    if(++tick==1 || tick%5==0){
                        FILE* nf{};
                        if(!fopen_s(&nf,"DeusExHRVR-native-pair.log","a") && nf){
                            fprintf(nf,"nativePair version=1 used=%llu failed=%llu compared=%llu mismatch=%llu disabled=%d ownerFail=%llu modelFail=%llu boneFail=%llu muzzleFail=%llu maxPaletteDifference=%.6f savedOffsets=0 refinement=0\n",
                                nativePairUsed.load(),nativePairFailed.load(),nativePairCompared.load(),nativePairMismatch.load(),int(nativePairFault.load()),
                                nativePairOwnerFailed.load(),nativePairModelFailed.load(),nativePairBoneFailed.load(),nativePairMuzzleFailed.load(),double(nativePairMaxDifference.load()));
                            fclose(nf);
                        }
                        FILE* f{};
                        if(!fopen_s(&f,"DeusExHRVR-model-hand.log","a") && f){
                            fprintf(f,"modelHand version=1 enabled=%d applied=%llu noGrip=%llu badModel=%llu badTransform=%llu neutralWrist=%d neutralWristFailed=%llu fallback=native\n",
                                int(controllerHandsModelAlignment.load()),modelHandApplied.load(),modelHandNoGrip.load(),modelHandBadModel.load(),modelHandBadTransform.load(),
                                int(controllerHandsNeutralWrist.load()),neutralWristFailed.load());
                            fclose(f);
                        }
                    }
                }
                auto path=handsConfigPath.c_str();
                RefreshForcedHandFallback(path);
                controllerHandsModelAlignment=GetPrivateProfileIntW(L"VR",L"ControllerHandsModelAlignment",0,path)!=0;
                controllerHandsMode=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsMode",0,path));
                controllerHandsSplitA=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSplitA",74,path));
                controllerHandsSplitB=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSplitB",148,path));
                controllerHandsKeepFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsKeepFrom",74,path));
                controllerHandsKeepTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsKeepTo",82,path));
                {
                    int drop=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsCollapseDropM",0,path));
                    if(drop>=0&&drop<=10000)controllerHandsCollapseDrop=float(drop);
                }
                {
                    bool want=GetPrivateProfileIntW(L"VR",L"ControllerHandsWeapon",0,path)!=0;
                    // Turning it off and on again re-measures, so a bad solve
                    // costs an INI edit rather than a relaunch.
                    if(!want&&controllerHandsWeapon) {
                        // Off then on re-measures every weapon from scratch.
                        handGripCount=0;handGripHavePrev=false;
                        handGripSolvingKey=0xffffffffu;
                        handGripSamples=0;handGripAccum=CameraMath::Matrix{};
                    }
                    controllerHandsWeapon=want;
                }
                controllerHandsCollapseBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsCollapseBone",-1,path));
                {
                    int mo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsMaxOffsetIn",12,path));
                    if(mo>0&&mo<=400)controllerHandsMaxOffsetIn=float(mo);
                    int sm=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSuspendModes",342,path));
                    if(sm>=0&&sm<512)controllerHandsSuspendModes=uint32_t(sm);
                    controllerHandsPose=GetPrivateProfileIntW(L"VR",L"ControllerHandsPose",0,path)!=0;
                    int pm=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsPoseButton",0x4000,path));
                    if(pm>0&&pm<=0xffff)controllerHandsPoseMask=uint32_t(pm);
                    int sb=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSprintButton",0x0100,path));
                    if(sb>0&&sb<=0xffff)controllerHandsSprintMask=uint32_t(sb);
                }
                controllerHandsProbe=GetPrivateProfileIntW(L"VR",L"ControllerHandsProbe",0,path)!=0;
                controllerHandsProbeFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsProbeFrom",100,path));
                controllerHandsProbeTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsProbeTo",160,path));
                controllerHandBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandBone",158,path));
                controllerHandBone2=int(GetPrivateProfileIntW(L"VR",L"ControllerHandBone2",999,path));
                float r=float(GetPrivateProfileIntW(L"VR",L"ControllerHandsRadiusCm",15,path));
                if(r>0&&r<=500)controllerHandsRadius=r;
                // No keypress anywhere: the state Mark would have had to press
                // F8 for writes itself, from this thread, on change only.
                WriteHandsStatus();
                controllerHandsDriveBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsDriveBone",152,path));
                {
                    // Edge-triggered, so the mod never writes the INI back the
                    // way snap turn's calibration does - and so a value left
                    // over from a previous session cannot fire a calibration
                    // against whatever pose happens to be live at launch.
                    controllerHandsSteadyMs=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSteadyMs",800,path));
                    controllerHandsSkipPrimMask=uint32_t(GetPrivateProfileIntW(L"VR",L"ControllerHandsSkipPrimMask",0,path));
                    {
                        wchar_t text[32]{};
                        struct {const wchar_t* key;float* out;} trims[]{
                            {L"ControllerHandsCollapseBackIn",&controllerHandsCollapseBackIn},
                            {L"ControllerHandsCollapseSideIn",&controllerHandsCollapseSideIn},
                            {L"ControllerHandsCollapseUpIn",&controllerHandsCollapseUpIn}};
                        for(auto& t:trims) {
                            GetPrivateProfileStringW(L"VR",t.key,L"0",text,32,path);
                            float v=float(_wtof(text));
                            if(std::isfinite(v)&&v>=-24.f&&v<=24.f)*t.out=v;
                        }
                    }
                    controllerHandsHideFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsHideFrom",0,path));
                    controllerHandsHideTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsHideTo",0,path));
                    controllerHandsWeldFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsWeldFrom",112,path));
                    controllerHandsWeldTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsWeldTo",130,path));
                    {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",L"ControllerHandsCollapseScale",L"0",text,32,path);
                        float v=float(_wtof(text));
                        if(std::isfinite(v)&&v>=0.f&&v<=1.f)controllerHandsCollapseScale=v;
                    }
                    controllerHandsArmGestureMs=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsArmGestureMs",1500,path));
                    {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",L"ControllerHandsIdGateDeg",L"0.5",text,32,path);
                        float v=float(_wtof(text));
                        if(std::isfinite(v)&&v>0.f&&v<90.f)controllerHandsIdGateDeg=v;
                    }
                    {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",L"ControllerHandsArmGestureCm",L"20",text,32,path);
                        float v=float(_wtof(text));
                        if(std::isfinite(v)&&v>0.f&&v<200.f)controllerHandsArmGestureCm=v;
                    }
                    {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",L"ControllerHandsSteadyCm",L"5",text,32,path);
                        float v=float(_wtof(text));
                        if(std::isfinite(v)&&v>0.f&&v<100.f)controllerHandsSteadyCm=v;
                    }
                    int req=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsCalibrate",0,path));
                    static int lastReq=-1;
                    if(lastReq<0)lastReq=req;
                    else if(req!=lastReq) {
                        // Changing the value re-arms. Arming is free and
                        // disarms itself on capture, so an early capture costs
                        // a repeat rather than a relaunch.
                        lastReq=req;
                        if(req){handCalibrateArmed.store(1);handSteadyValid=false;}
                    }
                }
                {
                    // Read as text, because GetPrivateProfileInt cannot return
                    // a negative, and trim has to go both ways.
                    const wchar_t* trimKeys[3]={L"ControllerHandsTrimXCm",L"ControllerHandsTrimYCm",L"ControllerHandsTrimZCm"};
                    for(int j=0;j<3;j++) {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",trimKeys[j],L"0",text,32,path);
                        float t=float(_wtof(text));
                        if(std::isfinite(t)&&t>-200.f&&t<200.f)handTrim[j]=t;
                    }
                    const wchar_t* rotKeys[3]={L"ControllerHandsTrimPitchDeg",L"ControllerHandsTrimYawDeg",L"ControllerHandsTrimRollDeg"};
                    for(int j=0;j<3;j++) {
                        wchar_t text[32]{};
                        GetPrivateProfileStringW(L"VR",rotKeys[j],L"0",text,32,path);
                        float t=float(_wtof(text));
                        if(std::isfinite(t)&&t>-180.f&&t<=180.f)handTrimRot[j]=t;
                    }
                }
            }
        }).detach();
    });
}
// Out-of-range values mean "no bone", which is what the 999 default relies on.
thread_local bool handDrawing{};
thread_local uint32_t handPrimIndex{};
// Controller grip position in world units. Written by ActorDrawHook and read by
// SkeletonHook on the same thread inside a single draw, so no lock is needed.
thread_local float handTarget[3]{};
// The weapon transform, recomputed per actor draw. Same quantity WeaponDrawHook
// feeds the gun, so anything it is applied to lands on the controller the way
// the gun already does.
thread_local CameraMath::Matrix handDelta{};
thread_local bool handDeltaValid{};
// The CONTROLLER pose, which is valid with the weapon holstered, kept separate
// from the WEAPON pose, which is not. That separation is the whole fix.
thread_local CameraMath::Matrix handMuzzle{};
thread_local bool handMuzzleValid{};
thread_local CameraMath::Matrix handNativeMuzzle{};
thread_local bool handNativeMuzzleValid{};
CameraMath::Matrix SkinAffine(const CameraMath::Matrix& skin) {
    // Same treatment MoveSkinMatrix applies: the engine stores skinning
    // translation with w=0 while the shader uses it as a position, so the
    // fourth column is not part of the transform.
    auto a=skin;a.m[3]=a.m[7]=a.m[11]=0;a.m[15]=1;return a;
}
CameraMath::Matrix AffineInverse(const CameraMath::Matrix& a,bool* ok) {
    // InverseRigid assumes an orthonormal 3x3. A palette entry carries
    // inverse(bindPose), which is only rigid if the model has no bind scaling -
    // unverified here, and a silent error if wrong. A general inverse costs a
    // few flops and removes the assumption.
    const float* m=a.m;
    float c00=m[5]*m[10]-m[6]*m[9];
    float c01=m[6]*m[8]-m[4]*m[10];
    float c02=m[4]*m[9]-m[5]*m[8];
    float det=m[0]*c00+m[1]*c01+m[2]*c02;
    CameraMath::Matrix r{};
    if(!(std::fabs(det)>1e-20f)) {
        if(ok)*ok=false;
        r.m[0]=r.m[5]=r.m[10]=r.m[15]=1;return r;
    }
    if(ok)*ok=true;
    float iv=1.f/det;
    r.m[0]=c00*iv;                  r.m[1]=(m[2]*m[9]-m[1]*m[10])*iv; r.m[2]=(m[1]*m[6]-m[2]*m[5])*iv;
    r.m[4]=c01*iv;                  r.m[5]=(m[0]*m[10]-m[2]*m[8])*iv; r.m[6]=(m[2]*m[4]-m[0]*m[6])*iv;
    r.m[8]=c02*iv;                  r.m[9]=(m[1]*m[8]-m[0]*m[9])*iv;  r.m[10]=(m[0]*m[5]-m[1]*m[4])*iv;
    for(int j=0;j<3;j++)r.m[12+j]=-(m[12]*r.m[j]+m[13]*r.m[4+j]+m[14]*r.m[8+j]);
    r.m[15]=1;
    return r;
}
void Orthonormalize(CameraMath::Matrix& m) {
    // Averaging rotation matrices element-wise leaves something slightly
    // non-rigid. Gram-Schmidt puts it back, which matters because AffineInverse
    // and the skinning both assume a proper transform.
    auto norm=[&](int r) {
        float l=std::sqrt(m.m[r*4]*m.m[r*4]+m.m[r*4+1]*m.m[r*4+1]+m.m[r*4+2]*m.m[r*4+2]);
        if(l>1e-6f)for(int j=0;j<3;j++)m.m[r*4+j]/=l;
    };
    auto dot=[&](int a,int b){float d=0;for(int j=0;j<3;j++)d+=m.m[a*4+j]*m.m[b*4+j];return d;};
    norm(0);
    float d01=dot(0,1);
    for(int j=0;j<3;j++)m.m[4+j]-=d01*m.m[j];
    norm(1);
    // Third row is the cross product of the first two, so handedness is kept.
    m.m[8]=m.m[1]*m.m[6]-m.m[2]*m.m[5];
    m.m[9]=m.m[2]*m.m[4]-m.m[0]*m.m[6];
    m.m[10]=m.m[0]*m.m[5]-m.m[1]*m.m[4];
    norm(2);
    m.m[3]=m.m[7]=m.m[11]=0;m.m[15]=1;
}
float RotationAngle(const CameraMath::Matrix& a) {
    float c=(a.m[0]+a.m[5]+a.m[10]-1.f)*.5f;
    if(c>1.f)c=1.f;
    if(c<-1.f)c=-1.f;
    return std::acos(c);
}
void SaveHandCalibration() {
    if(!handBindCount)return;
    // KEEP THE OLD ONE FIRST. Calibrating is a single button tap away, it
    // overwrites the file that everything else is built on, and on 2026-10-14
    // Mark triggered it twice by accident - losing a calibration that had stood
    // since 10/11, with no backup anywhere. Timestamped rather than a single
    // "previous", because the accident fired twice: one slot would have been
    // overwritten by the second one and lost the good file just the same.
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char backup[128];
        sprintf_s(backup,"DeusExHRVR-hands-%04u%02u%02u-%02u%02u%02u.cal",
            st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        // Fails harmlessly when there is nothing to back up yet.
        CopyFileA("DeusExHRVR-hands.cal",backup,TRUE);
    }
    // A ONE-SHOT write, which is not what made BobTrace hitch: that was a
    // write every frame. Checked rather than inherited, per the F8 lesson.
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-hands.cal","w") || !f)return;
    fprintf(f,"# DeusExHRVR hand calibration - do not hand-edit.\n");
    fprintf(f,"# bindPose per bone, solved from one instant when the controller\n");
    fprintf(f,"# was held on the visible hand. Delete this file to recalibrate.\n");
    fprintf(f,"bones %u\n",handBindCount);
    for(uint32_t i=0;i<handBindCount;i++) {
        fprintf(f,"b %u",i);
        for(int k=0;k<16;k++)fprintf(f," %.9g",handBind[i].m[k]);
        fprintf(f,"\n");
    }
    fclose(f);
}
// The solved grips, persisted. Same shape as the hand calibration above and for
// the same reason: it is a measurement, not a setting, and re-deriving a
// measurement every launch is how a rare bad sample gets to win. 128 pistol
// solves across six sessions produced 2.85 in 122 times and 31.74 in 3 times;
// the right answer was never in doubt, it was just not written down.
void SaveHandGrips() {
    if(!handGripCount)return;
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-grips.cal","w") || !f)return;
    fprintf(f,"# DeusExHRVR per-weapon grips - do not hand-edit.\n");
    fprintf(f,"# offset = nativeMuzzle x inverse(wristWorld), solved from the\n");
    fprintf(f,"# game's own animation. One line per weapon asset id.\n");
    fprintf(f,"# DELETE THIS FILE to re-measure every weapon.\n");
    for(uint32_t i=0;i<handGripCount;i++) {
        fprintf(f,"g %08x",handGrips[i].key);
        for(int k=0;k<16;k++)fprintf(f," %.9g",handGrips[i].offset.m[k]);
        fprintf(f,"\n");
    }
    fclose(f);
}
void LoadHandGrips() {
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-grips.cal","r") || !f)return;
    char line[1024];
    while(fgets(line,sizeof(line),f)) {
        if(line[0]!='g' || line[1]!=' ')continue;
        unsigned key=0;CameraMath::Matrix v{};
        int got=sscanf_s(line,"g %x %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",&key,
            &v.m[0],&v.m[1],&v.m[2],&v.m[3],&v.m[4],&v.m[5],&v.m[6],&v.m[7],
            &v.m[8],&v.m[9],&v.m[10],&v.m[11],&v.m[12],&v.m[13],&v.m[14],&v.m[15]);
        if(got!=17 || key>=0x18000 || handGripCount>=64)continue;
        if(FindHandGrip(key))continue;
        handGrips[handGripCount].key=key;
        handGrips[handGripCount].offset=v;
        handGrips[handGripCount].anchor=v;
        ++handGripCount;
    }
    fclose(f);
    FILE* log{};
    if(!fopen_s(&log,"DeusExHRVR-camera.log","a") && log) {
        fprintf(log,"handGrips loaded weapons=%u\n",handGripCount);
        // WORLD UNITS, not inches. Printing these as inches without dividing by
        // worldScale made ten healthy grips read as 1859 and 9555 inches and sent
        // me hunting a corruption that did not exist - the sixth time in this
        // project that a quantity was printed in the wrong units and believed.
        for(uint32_t i=0;i<handGripCount;i++)
            fprintf(log,"handGrips stored key=%08x units=%.2f\n",handGrips[i].key,
                double(sqrt(handGrips[i].offset.m[12]*handGrips[i].offset.m[12]+
                            handGrips[i].offset.m[13]*handGrips[i].offset.m[13]+
                            handGrips[i].offset.m[14]*handGrips[i].offset.m[14])));
        fclose(log);
    }
}
void LogPoseShape(FILE* f,const char* tag,const CameraMath::Matrix* pose,uint32_t count,int lo);
void SaveHandPose() {
    if(!handPoseHaveBest || !handPoseCount)return;
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-handpose.cal","w") || !f)return;
    fprintf(f,"# DeusExHRVR captured hand pose - do not hand-edit.\n");
    fprintf(f,"# The most curled unarmed hand the GAME animated, kept so it can\n");
    fprintf(f,"# be shown again when interact is pressed. Delete to re-capture.\n");
    fprintf(f,"count %u\nscore %.9g\nsprint %d\n",
        handPoseCount,double(handPoseBestScore),int(handPoseHaveSprint));
    for(uint32_t i=0;i<handPoseCount;i++) {
        fprintf(f,"base %u",i);
        for(int k=0;k<16;k++)fprintf(f," %.9g",handPoseBase[i].m[k]);
        fprintf(f,"\nbest %u",i);
        for(int k=0;k<16;k++)fprintf(f," %.9g",handPoseBest[i].m[k]);
        if(handPoseHaveSprint) {
            fprintf(f,"\nsprn %u",i);
            for(int k=0;k<16;k++)fprintf(f," %.9g",handPoseSprint[i].m[k]);
        }
        fprintf(f,"\n");
    }
    fclose(f);
}
void LoadHandPose() {
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-handpose.cal","r") || !f)return;
    char line[1024];
    uint32_t count=0;
    while(fgets(line,sizeof(line),f)) {
        unsigned i=0;CameraMath::Matrix v{};float sc=0.f;
        if(sscanf_s(line,"count %u",&i)==1 && i<=32){count=i;continue;}
        if(sscanf_s(line,"score %f",&sc)==1){handPoseBestScore=sc;continue;}
        int sp=0;
        if(sscanf_s(line,"sprint %d",&sp)==1){handPoseHaveSprint=sp!=0;continue;}
        bool isBase=line[0]=='b'&&line[1]=='a';
        bool isBest=line[0]=='b'&&line[1]=='e';
        bool isSprn=line[0]=='s'&&line[1]=='p'&&line[2]=='r'&&line[3]=='n';
        if(!isBase && !isBest && !isSprn)continue;
        int got=sscanf_s(line+5,"%u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",&i,
            &v.m[0],&v.m[1],&v.m[2],&v.m[3],&v.m[4],&v.m[5],&v.m[6],&v.m[7],
            &v.m[8],&v.m[9],&v.m[10],&v.m[11],&v.m[12],&v.m[13],&v.m[14],&v.m[15]);
        if(got!=17 || i>=32)continue;
        if(isBase)handPoseBase[i]=v;
        else if(isSprn)handPoseSprint[i]=v;
        else handPoseBest[i]=v;
    }
    fclose(f);
    if(count){handPoseCount=count;handPoseHaveBase=true;handPoseHaveBest=true;}
    FILE* log{};
    if(!fopen_s(&log,"DeusExHRVR-camera.log","a") && log) {
        fprintf(log,"handPose loaded bones=%u score=%.4f\n",handPoseCount,double(handPoseBestScore));
        // Re-describe what is already on disk with the CORRECTED measure, so the
        // poses captured on 2026-10-14 answer "is there any finger movement in
        // this" without Mark playing again. The old numbers were whole-hand
        // orientation; these are articulation relative to the wrist.
        if(handPoseCount) {
            int lo=controllerHandsKeepFrom;
            if(lo<0)lo=0;
            if(handPoseHaveSprint)LogPoseShape(log,"loaded-sprint",handPoseSprint,handPoseCount,lo);
            if(handPoseHaveBest)LogPoseShape(log,"loaded-best",handPoseBest,handPoseCount,lo);
        }
        fclose(log);
    }
}
// A bone's rotation RELATIVE TO THE WRIST: R(bone) * transpose(R(wrist)).
//
// Comparing palette entries directly was wrong, and the 2026-10-14 log shows it
// plainly - all 23 bones reported the identical angle (70.3, 51.1, 175.9...),
// which is the whole hand turning as one lump, not fingers moving. A palette
// entry carries the bone's world orientation, so comparing entries across time
// measures mainly where the hand is POINTING.
//
// This cancels that. Writing P=inv(bind)*world, the product P_i * inv(P_wrist)
// equals inv(bind_i) * [W_i * inv(W_wrist)] * bind_wrist: the finger's pose
// relative to the wrist, wrapped in constants. Comparing it against the same
// quantity at rest conjugates the difference by a constant rotation, and
// conjugation preserves the ANGLE - so the number below is exactly how far that
// joint has moved relative to the wrist, with no bind poses needed.
void RelativeRotation(const CameraMath::Matrix& bone,const CameraMath::Matrix& wrist,float out[9]) {
    for(int r=0;r<3;r++)for(int c=0;c<3;c++) {
        float v=0.f;
        for(int k=0;k<3;k++)v+=bone.m[r*4+k]*wrist.m[c*4+k];  // R_bone * R_wrist^T
        out[r*3+c]=v;
    }
}
float RelativeAngleDeg(const CameraMath::Matrix* pose,const CameraMath::Matrix* base,
                       uint32_t i,uint32_t wristIdx) {
    float a[9],b[9];
    RelativeRotation(pose[i],pose[wristIdx],a);
    RelativeRotation(base[i],base[wristIdx],b);
    float tr=0.f;
    for(int k=0;k<9;k++)tr+=a[k]*b[k];     // trace(A*B^T)=1+2cos(theta)
    float c=(tr-1.f)*.5f;
    if(c>1.f)c=1.f;
    if(c<-1.f)c=-1.f;
    return float(std::acos(c)*180.0/3.14159265358979);
}
// Per-bone articulation of a captured pose against the resting hand, in degrees.
// Written for EVERY candidate, so poses can be compared from the log instead of
// Mark looking at his hand and reporting back. 133-152 is five fingers at four
// joints: a curl turns a finger's later joints more than its base.
void LogPoseShape(FILE* f,const char* tag,const CameraMath::Matrix* pose,uint32_t count,int lo) {
    int drive=controllerHandsDriveBone;
    uint32_t wristIdx=(drive>=lo && drive<lo+int(count))?uint32_t(drive-lo):0u;
    fprintf(f,"handPose shape %s wrist=%d",tag,lo+int(wristIdx));
    for(uint32_t i=0;i<count;i++)
        fprintf(f," %d:%.1f",lo+int(i),
            double(RelativeAngleDeg(pose,handPoseBase,i,wristIdx)));
    fprintf(f,"\n");
}
// Runs on the palette the GAME filled, before the mod changes anything.
void HandPoseStep(CameraMath::Matrix* m,uint32_t n) {
    if(!controllerHandsPose)return;
    int lo=controllerHandsKeepFrom,hi=controllerHandsKeepTo;
    if(lo<0)lo=0;
    if(hi>int(n))hi=int(n);
    if(hi<=lo || hi-lo>32)return;
    uint32_t count=uint32_t(hi-lo);
    // HANDS EMPTY. The obvious test - "is there a valid weapon muzzle" - is
    // WRONG: Mark played a whole session unarmed on 2026-10-14 and the game
    // handed the mod a valid muzzle for all 3838 frames, which is why this
    // never ran once. The muzzle exists whether or not the gun is in his hand.
    //
    // What distinguishes them is DISTANCE. That same session measured the
    // weapon 31.7 inches from the wrist while his hands were empty; held, the
    // stored grip for it is 2.85. So: solve the live muzzle-to-wrist distance
    // and compare it with the one stored for this weapon. Far from it means the
    // gun is not in the hand. With no stored grip there is nothing to compare
    // against, so assume armed and do nothing - this can only ever fail to run,
    // never run when it should not.
    bool unarmed=!handNativeMuzzleValid;
    if(!unarmed) {
        int drive=controllerHandsDriveBone;
        if(drive>=0 && uint32_t(drive)<n && uint32_t(drive)<handBindCount) {
            auto world=CameraMath::Multiply(handBind[drive],SkinAffine(m[drive]));
            bool ok=false;
            auto inv=AffineInverse(world,&ok);
            if(ok) {
                auto cand=CameraMath::Multiply(handNativeMuzzle,inv);
                if(auto g=FindHandGrip(handWeaponKey)) {
                    float live=0.f,held=0.f;
                    for(int j=0;j<3;j++) {
                        live+=cand.m[12+j]*cand.m[12+j];
                        held+=g->m[12+j]*g->m[12+j];
                    }
                    // A RATIO, so no unit conversion is needed here - worldScale
                    // is not in scope this early in the file, and a ratio is the
                    // better test anyway: it scales with the weapon. Observed
                    // 2026-10-14: 2.85 inches held, 31.7 not held, a factor of
                    // eleven. Twice the stored distance is the threshold, which
                    // no animation jitter reaches and every observed miss clears.
                    unarmed=live>held*4.f;   // squared lengths, so 4 = 2x
                }
            }
        }
    }
    // REPLAY. Only unarmed, only while interact is held, only with something
    // captured - any of those false and the live hand is untouched.
    if(unarmed && handPoseCount==count && handPoseInteract.load() &&
       (handPoseHaveSprint || handPoseHaveBest)) {
        // THE SPRINT POSE WINS. It is the one Mark has actually seen curl the
        // fingers; the most-different pose is only a fallback for the case
        // where sprinting never happened, and "most different" can be a splay
        // rather than a curl.
        const CameraMath::Matrix* src=handPoseHaveSprint?handPoseSprint:handPoseBest;
        for(uint32_t i=0;i<count;i++)m[lo+i]=src[i];
        return;   // never capture a frame we just overwrote
    }
    if(!unarmed)return;
    // Sprinting: keep this hand regardless of how different it is, because it is
    // the pose that was asked for.
    if(handPoseSprinting.load() && handPoseHaveBase && handPoseCount==count) {
        for(uint32_t i=0;i<count;i++)handPoseSprint[i]=m[lo+i];
        if(!handPoseHaveSprint) {
            handPoseHaveSprint=true;
            SaveHandPose();
            FILE* f{};
            if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"handPose sprint captured bones=%u frame=%llu\n",count,frameId.load());
                // PER-BONE ROTATION against the resting hand, in degrees, so the
                // SHAPE of the pose can be read out of the log instead of Mark
                // having to look at his hand and report back. 133-152 is five
                // fingers at four joints, so the bone index is printed with each
                // angle: a curl turns the later joint of a finger more than its
                // base, a splay turns them all at the base.
                LogPoseShape(f,"sprint",handPoseSprint,count,lo);
                fclose(f);
            }
        }
        else if(handPoseSprintLogged<4) {
            ++handPoseSprintLogged;
            SaveHandPose();
        }
    }
    if(!handPoseHaveBase || handPoseCount!=count) {
        for(uint32_t i=0;i<count;i++)handPoseBase[i]=m[lo+i];
        handPoseCount=count;handPoseHaveBase=true;
        handPoseHaveBest=false;handPoseBestScore=0.f;
        return;
    }
    // ARTICULATION, not orientation. Scoring palette entries directly made every
    // bone report the identical angle, because that measures the whole hand
    // turning. Each bone is now taken relative to the wrist, which cancels the
    // hand's own orientation and leaves only the joints moving. The wrist
    // contributes nothing by construction, which is right - it is the reference.
    int scoreDrive=controllerHandsDriveBone;
    uint32_t wristIdx=(scoreDrive>=lo && scoreDrive<lo+int(count))
                      ?uint32_t(scoreDrive-lo):0u;
    float score=0.f;
    for(uint32_t i=0;i<count;i++) {
        if(i==wristIdx)continue;
        float d=RelativeAngleDeg(m+lo,handPoseBase,i,wristIdx);
        score+=d*d;
    }
    if(score>handPoseBestScore*1.15f+1e-4f) {
        for(uint32_t i=0;i<count;i++)handPoseBest[i]=m[lo+i];
        handPoseBestScore=score;handPoseHaveBest=true;
        SaveHandPose();
        if(handPoseLogged<16) {
            ++handPoseLogged;
            FILE* f{};
            if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"handPose captured score=%.4f bones=%u frame=%llu\n",
                    double(score),count,frameId.load());
                // Every candidate described, not only sprint: the question is
                // whether anything the game does closes the hand better, and
                // this answers it from ordinary play instead of by guessing.
                LogPoseShape(f,"candidate",handPoseBest,count,lo);
                fclose(f);
            }
        }
    }
}
void LoadHandCalibration() {
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-hands.cal","r") || !f)return;
    char line[1024];
    uint32_t loaded=0;
    while(fgets(line,sizeof(line),f)) {
        if(line[0]!='b' || line[1]!=' ')continue;
        unsigned i=0;CameraMath::Matrix v{};
        int got=sscanf_s(line,"b %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",&i,
            &v.m[0],&v.m[1],&v.m[2],&v.m[3],&v.m[4],&v.m[5],&v.m[6],&v.m[7],
            &v.m[8],&v.m[9],&v.m[10],&v.m[11],&v.m[12],&v.m[13],&v.m[14],&v.m[15]);
        if(got==17 && i<512) {
            handBind[i]=v;
            if(i+1>loaded)loaded=i+1;
        }
    }
    fclose(f);
    if(loaded){handBindCount=loaded;handCalibrated=true;}
    FILE* log{};
    if(!fopen_s(&log,"DeusExHRVR-camera.log","a") && log) {
        fprintf(log,"handCalibration loaded bones=%u calibrated=%d\n",loaded,int(handCalibrated));
        fclose(log);
    }
}
void LogHandCalibration(const CameraMath::Matrix* m,uint32_t n) {
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-camera.log","a") || !f)return;
    fprintf(f,"handCalibrate bones=%u drive=%d muzzle=(%.1f,%.1f,%.1f)\n",
        n,controllerHandsDriveBone,handMuzzle.m[12],handMuzzle.m[13],handMuzzle.m[14]);
    // How far the controller wandered during the accepted hold, in world units
    // (3 units is 1 cm at 300 per metre). A big number means redo the hold, NOT
    // that the drive bone is wrong - telling those two apart afterwards is the
    // whole reason this is recorded.
    fprintf(f,"handCalibrate holdUnits=%.1f tolCm=%g holdMs=%d averagedFrames=%u\n",
        handCalibrateQuality,controllerHandsSteadyCm,controllerHandsSteadyMs,handBindSamples);
    // Row norms say whether a palette entry is rigid. AffineInverse copes
    // either way, but anything that reasons about these as rotations - mode
    // 14's angle test above all - needs them at 1.
    const int probe[4]={0,112,132,152};
    for(int k=0;k<4;k++) {
        int i=probe[k];
        if(uint32_t(i)>=n)continue;
        auto a=SkinAffine(m[i]);
        fprintf(f,"handCalibrate rowNorms b=%d %.4f %.4f %.4f\n",i,
            std::sqrt(a.m[0]*a.m[0]+a.m[1]*a.m[1]+a.m[2]*a.m[2]),
            std::sqrt(a.m[4]*a.m[4]+a.m[5]*a.m[5]+a.m[6]*a.m[6]),
            std::sqrt(a.m[8]*a.m[8]+a.m[9]*a.m[9]+a.m[10]*a.m[10]));
    }
    fclose(f);
}
void ReportHandIdentification(uint32_t n) {
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-camera.log","a") || !f)return;
    fprintf(f,"handIdentify samples=%u bones=%u gateDeg=%g maxGunDeg=%.2f\n",
        handIdSamples,n,controllerHandsIdGateDeg,handIdMaxDeg);
    fprintf(f,"handIdentify READ: smallest meanDeg = tracks the gun most rigidly,"
              " but IGNORE any row whose ownDeg is near zero - a bone that never"
              " moves matches trivially.\n");
    for(uint32_t i=0;i<n;i++) {
        if(!handIdSamples)break;
        fprintf(f,"handIdentify b=%u meanDeg=%.4f worstDeg=%.4f ownDeg=%.4f\n",
            i,handIdSum[i]/handIdSamples,handIdWorst[i],handIdOwn[i]/handIdSamples);
    }
    fclose(f);
}
void IdentifyHandBone(const CameraMath::Matrix* m,uint32_t n) {
    handIdEnters.fetch_add(1);
    if(handIdReported)return;
    // Write whatever has accumulated once progress stalls. A run that stops
    // short of the target used to write nothing, and nothing is the one result
    // that cannot be acted on.
    if(handIdSamples) {
        auto last=handIdProgressTick.load();
        if(last && GetTickCount64()-last>8000) {
            handIdReported=true;ReportHandIdentification(n);return;
        }
    }
    if(!handNativeMuzzleValid){handIdNoWeapon.fetch_add(1);return;}
    if(!handIdHavePrev) {
        for(uint32_t i=0;i<n;i++)handIdPrevSkin[i]=SkinAffine(m[i]);
        handIdPrevMuzzle=handNativeMuzzle;handIdHavePrev=true;return;
    }
    bool ok=false;
    auto muzzleStep=CameraMath::Multiply(AffineInverse(handIdPrevMuzzle,&ok),handNativeMuzzle);
    if(!ok)return;
    float am=RotationAngle(muzzleStep);
    if(am*57.2957795f>handIdMaxDeg)handIdMaxDeg=am*57.2957795f;
    float gate=controllerHandsIdGateDeg>0?controllerHandsIdGateDeg*.01745329252f:.00872664626f;
    // Wait for the gun to have actually turned. With no motion every bone
    // matches trivially at zero, which would rank them all equal. The anchor is
    // deliberately NOT advanced on a reject, so the angle accumulates until the
    // gun really has moved, however slowly.
    if(!(am>gate)){handIdBelowGate.fetch_add(1);return;}
    for(uint32_t i=0;i<n;i++) {
        bool bok=false;
        auto step=CameraMath::Multiply(AffineInverse(handIdPrevSkin[i],&bok),SkinAffine(m[i]));
        // The bind pose cancels in S(1)^-1 S(2), so this IS the bone's own
        // world-space relative motion, with nothing assumed about the model.
        double own=bok?RotationAngle(step)*57.2957795:0.0;
        double d=bok?std::fabs(own-am*57.2957795):180.0;
        handIdSum[i]+=d;
        handIdOwn[i]+=own;
        if(d>handIdWorst[i])handIdWorst[i]=d;
        handIdPrevSkin[i]=SkinAffine(m[i]);
    }
    handIdPrevMuzzle=handNativeMuzzle;
    ++handIdSamples;
    handIdSampleCount.store(handIdSamples);
    handIdProgressTick.store(GetTickCount64());
    if(handIdSamples>=40){handIdReported=true;ReportHandIdentification(n);}
}
std::atomic<uint64_t> handDraws{},handSkeletons{},handSkeletonMiss{};
std::atomic<uint32_t> handBoneCount{};
void WriteHandsStatus() {
    if(!controllerHands)return;
    // Change-only, so a quiet session does not bury the log.
    static int lastMode=-1,lastDrive=-1,lastCal=-1,lastArmed=-1;
    static uint32_t lastSamples=0xffffffffu;
    static uint64_t lastNoWeapon=0xffffffffull,lastDraws=0xffffffffull;
    static float lastTrim[3]={1e9f,1e9f,1e9f};
    uint32_t samples=handIdSampleCount.load();
    uint64_t noWeapon=handIdNoWeapon.load(),draws=handDraws.load();
    static uint64_t lastRaw=0xffffffffull,lastGest=0xffffffffull;
    bool changed=lastRaw!=handRawUsed.load() || lastGest!=handArmGestures.load()
        || lastMode!=controllerHandsMode || lastDrive!=controllerHandsDriveBone
        || lastCal!=int(handCalibrated) || lastArmed!=handCalibrateArmed.load()
        || lastSamples!=samples || lastNoWeapon!=noWeapon || lastDraws!=draws
        || lastTrim[0]!=handTrim[0] || lastTrim[1]!=handTrim[1] || lastTrim[2]!=handTrim[2];
    if(!changed)return;
    lastRaw=handRawUsed.load();lastGest=handArmGestures.load();
    lastMode=controllerHandsMode;lastDrive=controllerHandsDriveBone;
    lastCal=int(handCalibrated);lastArmed=handCalibrateArmed.load();
    lastSamples=samples;lastNoWeapon=noWeapon;lastDraws=draws;
    for(int j=0;j<3;j++)lastTrim[j]=handTrim[j];
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-camera.log","a") || !f)return;
    fprintf(f,"handsStatus mode=%d drive=%d calibrated=%d armed=%d bindBones=%u hold=%.1f trim=%g/%g/%g"
              " draws=%llu skeletons=%llu miss=%llu bones=%u"
              " idSamples=%u enters=%llu noWeapon=%llu unarmedDraws=%llu driveRejected=%llu belowGate=%llu maxDeg=%.2f"
              " rawUsed=%llu rawRejected=%llu rawFault=%u rawW15=%.4f rawRow0Len2=%.4f"
              " fallback=%llu gestures=%llu rot=%g/%g/%g avgFrames=%u pieces=%u skipMask=%u scale=%g shaderCalls=%u distinctShaders=%u weld=%d/%d apex=%g/%g/%gin hide=%d-%d/%u\n",
        controllerHandsMode,controllerHandsDriveBone,int(handCalibrated),handCalibrateArmed.load(),
        handBindCount,handCalibrateQuality,handTrim[0],handTrim[1],handTrim[2],
        draws,handSkeletons.load(),handSkeletonMiss.load(),handBoneCount.load(),
        samples,handIdEnters.load(),noWeapon,handUnarmedDraws.load(),
        handDriveRejected.load(),handIdBelowGate.load(),handIdMaxDeg,
        handRawUsed.load(),handRawRejected.load(),handRawFault.load(),handRawW15,handRawRow0,
        handFallbackUsed.load(),handArmGestures.load(),
        handTrimRot[0],handTrimRot[1],handTrimRot[2],handBindSamples,
        handPrimPieces.load(),controllerHandsSkipPrimMask,controllerHandsCollapseScale,
        handShaderCalls.load(),handShaderCount,
        controllerHandsWeldFrom,controllerHandsWeldTo,
        controllerHandsCollapseBackIn,controllerHandsCollapseSideIn,
        controllerHandsCollapseUpIn,
        controllerHandsHideFrom,controllerHandsHideTo,handHiddenSlots.load());
    fclose(f);
}
DirectionConfig::Source interactionAim{};
std::atomic<DirectionConfig::Source> movementDirection{DirectionConfig::Source::Mouse};
thread_local bool senseQueries{};
std::atomic<uint64_t> interactionQueries{},movementAxes{};
std::atomic<void*> walkAction{},strafeAction{};
std::atomic<unsigned> movementResponse{};
// Bounded ordinary-session diagnostics. Counts prove the action hook is reached;
// samples report its returned input, not the character's physical velocity.
std::atomic<uint64_t> responseHookCalls{},responseActionCalls{},responsePassed{},responseReduced{};
std::atomic<float> responseInputMagnitude{},responseOutputMagnitude{};
std::atomic<uint64_t> responseSampleTick{};
unsigned responseSampleMode{}; // paired with magnitudes under stateMutex
float lastMoveInput[2]{},lastMoveOutput[2]{},lastMoveHeading{};
float controllerMuzzleForward=.25f;
uint64_t removedFlareSprites{};
bool skyFix=true,f11Down{},f12Down{};
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
bool yawOnlyCamera=false; // Local patch: drop game camera pitch/roll (head-bob tilt) from the VR base
CameraMath::Matrix RenderBase(const CameraMath::Matrix& game);
// Local patch: keep only heading (yaw) from the recenter pose so head pitch/roll
// at load or F9 does not tilt the world. Position (incl. height) is kept.
// Heading only: the same facing direction with pitch and roll removed.
Transport::Quaternion LevelHeading(Transport::Quaternion q) {
    auto f=CameraMath::Rotate(q,{0,0,-1});
    float yaw;
    if(std::hypot(f.x,f.z)<.001f){auto r=CameraMath::Rotate(q,{1,0,0});yaw=std::atan2(-r.z,r.x);}
    else yaw=std::atan2(-f.x,-f.z);
    return {0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
}
Transport::Pose LevelReference(Transport::Pose p) {
    if(!levelRecenter)return p;
    p.orientation=LevelHeading(p.orientation);
    return p;
}
// Local patch (level in-game menu): LevelMenu=1 levels the head pose the view is
// frozen at while the in-game menu (map/objectives/inventory) is open.
bool levelMenu=true;
float worldScale=100.f;
// Local patch: optional per-frame camera trace for head-bob analysis.
// Buffered in memory (4 MB stdio buffer) so it rarely touches the disk.
bool bobTrace=false;FILE* bobFile{};uint32_t bobLines{};
bool sleepFix=false; // [VR] SleepFix applied (see Install)
void BobTrace(double ms,const CameraMath::Matrix& g,const float out[3],const Transport::Tracking& t) {
    if(bobLines>=36000)return;
    if(!bobFile) {
        if(fopen_s(&bobFile,"DeusExHRVR-bob.csv","w") || !bobFile){bobTrace=false;bobFile=nullptr;return;}
        setvbuf(bobFile,nullptr,_IOFBF,4<<20);
        fprintf(bobFile,"ms,x,y,z,fx,fy,fz,outX,outY,outZ,lx,ly,headX,headY,headZ,headQx,headQy,headQz,headQw\n");
    }
    fprintf(bobFile,"%.3f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f\n",
        ms,g.m[12],g.m[13],g.m[14],g.m[8],g.m[9],g.m[10],out[0],out[1],out[2],
        int(t.gamepad.leftX),int(t.gamepad.leftY),t.head.position.x,t.head.position.y,t.head.position.z,
        t.head.orientation.x,t.head.orientation.y,t.head.orientation.z,t.head.orientation.w);
    if(++bobLines>=36000){fclose(bobFile);bobFile=nullptr;}
}
// Local patch: time-average filter, used by YawSwing below for the walk
// animation's heading swing (heading is fed in as component 0).
// A(t) = time-average of the input over the last W ms. For steady motion,
// A(now) + (A(now)-A(now-W))/2 equals the current value exactly, while a
// periodic swing whose period divides W averages out. When the difference from
// the raw input exceeds the limit the model has broken down (start, stop, a
// turn): the correction is dropped and averaging restarts, rather than being
// clipped to the limit and held there by history that no longer applies.
struct BobSample {double t;float p[3];};
struct BobFilter {
    int windowMs[2]{700,333};      // [horizontal, vertical]
    float limit[2]{6.f,5.f};       // max deviation from raw camera, game units
    bool enabled{};
    float rate[2]{60.f,300.f};     // [horizontal, vertical] max change of the correction per second
    float dev[3]{};double lastT{};double verticalFrom{-1e18},horizontalFrom{-1e18};
    std::array<BobSample,1024> ring{};size_t head{},size{};
    void Reset(){size=0;dev[0]=dev[1]=dev[2]=0;}
    const BobSample& At(size_t back) const {return ring[(head+ring.size()-1-back)%ring.size()];}
    // Mean of component c over [t0,t1], linearly interpolating between samples.
    bool Average(double t0,double t1,int c,double& out) const {
        if(size<2 || At(0).t<t1-0.5 )return false;
        double sum=0,covered=0;
        for(size_t i=0;i+1<size;i++) {
            const auto& n=At(i);const auto& o=At(i+1); // newer, older
            if(n.t<=t0)break;
            double a=std::max(o.t,t0),b=std::min(n.t,t1);
            if(b<=a)continue;
            double span=n.t-o.t;
            auto val=[&](double t){return span>0?o.p[c]+(n.p[c]-o.p[c])*(t-o.t)/span:double(n.p[c]);};
            sum+=(val(a)+val(b))*.5*(b-a);covered+=b-a;
        }
        if(covered<(t1-t0)-1.)return false; // not enough history yet
        out=sum/covered;return true;
    }
    void Apply(float p[3],double now) {
        if(!enabled)return;
        if(size) {
            const auto& last=At(0);
            float jump=std::abs(p[0]-last.p[0])+std::abs(p[1]-last.p[1])+std::abs(p[2]-last.p[2]);
            if(jump>600 || now<last.t || now-last.t>500)Reset(); // load, teleport, pause
        }
        ring[head]={now,{p[0],p[1],p[2]}};head=(head+1)%ring.size();size=std::min(size+1,ring.size());
        float out[3]{p[0],p[1],p[2]};
        for(int c=0;c<3;c++) {
            int axis=c<2?0:1;double W=windowMs[axis];
            if(W<=0)continue;
            double a1,a0;
            if(c==2 && now-2*W<verticalFrom)continue;
            if(c<2 && now-2*W<horizontalFrom)continue;
            if(!Average(now-W,now,c,a1) || !Average(now-2*W,now-W,c,a0))continue;
            out[c]=float(a1+(a1-a0)/2);
        }
        // A deviation bigger than the limit means the average no longer
        // describes the input: drop the correction and restart averaging.
        float dx=out[0]-p[0],dy=out[1]-p[1],h=std::hypot(dx,dy);
        if(h>limit[0]){horizontalFrom=now;dx=dy=0;}
        float dz=out[2]-p[2];
        if(std::abs(dz)>limit[1]) {
            // Bigger than any bounce: a real height change (crouch, jump, landing).
            // Follow it now and restart vertical averaging from here.
            verticalFrom=now;dz=0;
        }
        // Rate-limit the correction so start/stop/crouch never pop in one frame.
        double dt=size>1?std::clamp((now-lastT)/1000.,0.,0.1):0.;lastT=now;
        float target[3]{dx,dy,dz};
        for(int c=0;c<3;c++){float step=float(rate[c<2?0:1]*dt);dev[c]+=std::clamp(target[c]-dev[c],-step,step);}
        p[0]+=dev[0];p[1]+=dev[1];p[2]+=dev[2];
    }
};
// Local patch: hold the eye height instead of following the game camera's
// vertical motion.
//
// Measured against the player entity's own origin (9000 frames of walking,
// crouch-walking and sprinting): the camera's X/Y equal the entity's to 0.2
// units, so this game has no lateral bob. Its height above the entity is a
// *stance* height, not a smooth signal - the game raises it ~15 units while
// crouch-walking and lowers it ~25 while sprinting, then steps back when you
// stop, which reads as a bounce. An averaging filter can smooth such a step but
// can never cancel a sustained offset.
//
// So hold the height and follow only real stance changes. Crouched and standing
// differ by ~300 units, while gait offsets are 15-25 and walking bob is about 2,
// so a trigger of 60 separates them cleanly. A real change is followed at the
// game's own speed: its crouch and stand move the camera at about 1000 units/s
// (measured over the middle 80% of the transition on the supported build).
struct StanceHold {
    bool enabled{};
    float trigger=60.f;
    static constexpr float rate=1000.f;
    float held{};bool have{},following{};
    double lastT{};
    struct Sample {double t;float v;};
    std::array<Sample,128> ring{};size_t head{},size{};
    void Reset(){have=false;following=false;size=0;}
    // True once the camera has been within band for at least 100 ms.
    bool Settled(double now,float band) const {
        if(size<4)return false;
        float lo=1e30f,hi=-1e30f;double oldest=now;
        for(size_t i=0;i<size;i++) {
            const auto& s=ring[(head+ring.size()-1-i)%ring.size()];
            if(now-s.t>120)break;
            lo=std::min(lo,s.v);hi=std::max(hi,s.v);oldest=s.t;
        }
        return now-oldest>=100 && hi-lo<band;
    }
    float Apply(float eye,double now) {
        if(!std::isfinite(eye))return eye;
        if(!have || now<lastT || now-lastT>500) { // first frame, load, teleport, pause
            held=eye;have=true;following=false;lastT=now;
            head=0;size=1;ring[0]={now,eye};
            return held;
        }
        double dt=std::clamp((now-lastT)/1000.,0.,0.1);lastT=now;
        ring[head]={now,eye};head=(head+1)%ring.size();size=std::min(size+1,ring.size());
        if(!following && std::abs(eye-held)>trigger)following=true;
        if(following) {
            float step=float(rate*dt);
            held+=std::clamp(eye-held,-step,step);
            if(std::abs(eye-held)<5.f && Settled(now,5.f)){held=eye;following=false;}
        }
        return held;
    }
} stanceHold;
// Hold the camera's horizontal position on the player's own path.
//
// Measured while running (two 46 s runs at 5.2 m/s): the game sways the camera
// sideways relative to the player entity at the stride (667 ms) and step
// (333 ms) rates, about 1 cm peak to peak, while the entity itself travels
// straight. Standing, the camera sits exactly on the entity origin; running
// leans it about 3 cm forward. So place the camera at the entity origin plus
// its offset averaged over one stride: that offset barely changes while
// moving, so the average removes the sway without lag. Running and sprinting
// offsets stay under 7 cm; anything past the limit (camera cuts, cover,
// takedowns) passes through, and the correction eases out rather than popping.
struct SwayHold {
    int windowMs{};                      // [VR] SideSwayHoldMs; 0 = off
    static constexpr float limit=45.f;   // game units (15 cm)
    static constexpr float rate=150.f;   // how fast the correction eases out, units/s
    // Never more than 4 cm from the game's camera, so deliberate camera moves
    // (cover, pull-backs) can't lag behind. Running corrections stay under it:
    // 99th percentile 3.4 cm; the cap binds on 0.2% of running frames.
    static constexpr float cap=12.f;
    struct Sample {double t;float x,y;};
    std::array<Sample,512> ring{};size_t head{},size{};
    double sumX{},sumY{},lastT{};float cx{},cy{},lastX{},lastY{};bool have{};
    void Clear(){size=0;sumX=sumY=0;}
    void Reset(){Clear();cx=cy=0;have=false;}
    const Sample& Oldest() const {return ring[(head+ring.size()-size)%ring.size()];}
    void Apply(float& camX,float& camY,float entX,float entY,double now) {
        float dx=camX-entX,dy=camY-entY;
        bool jump=!have || now<lastT || now-lastT>500 || std::hypot(dx-lastX,dy-lastY)>300; // load, teleport, pause
        double dt=have?std::clamp((now-lastT)/1000.,0.,0.1):0.;
        if(jump)Clear();
        float tx=0,ty=0;
        bool inside=std::hypot(dx,dy)<=limit;
        if(inside) {
            if(size==ring.size()){sumX-=Oldest().x;sumY-=Oldest().y;size--;}
            ring[head]={now,dx,dy};head=(head+1)%ring.size();size++;sumX+=dx;sumY+=dy;
            while(size>1 && now-Oldest().t>windowMs){sumX-=Oldest().x;sumY-=Oldest().y;size--;}
            tx=float(sumX/double(size))-dx;ty=float(sumY/double(size))-dy;
        } else Clear();
        // Follow the target while holding; ease toward it when it would jump.
        float step=float(rate*dt),ex=tx-cx,ey=ty-cy,d=std::hypot(ex,ey);
        if(inside && d<=step*4){cx=tx;cy=ty;}
        else if(d>step && d>0){cx+=ex/d*step;cy+=ey/d*step;}
        else {cx=tx;cy=ty;}
        if(float m=std::hypot(cx,cy);m>cap){cx*=cap/m;cy*=cap/m;}
        camX+=cx;camY+=cy;
        lastT=now;lastX=dx;lastY=dy;have=true;
    }
} swayHold;
// Heading (yaw) swing filter: two cascaded stages; snap turns and other jumps
// (> 3 deg in one frame) are tracked as an offset so they pass through instantly.
struct YawSwing {
    BobFilter a,b;double offset{},last{},unwrapped{};bool have{};
    YawSwing(){a.windowMs[1]=b.windowMs[1]=0;a.limit[0]=b.limit[0]=1.5f;a.rate[0]=b.rate[0]=10.f;}
    void Reset(){a.Reset();b.Reset();have=false;offset=last=unwrapped=0;}
    double Apply(double yaw,double ms) {
        if(!a.enabled && !b.enabled)return yaw;
        if(have){double d=std::remainder(yaw-last,360.);unwrapped+=d;if(std::abs(d)>3)offset+=d;}
        else {unwrapped=offset=yaw;}
        last=yaw;have=true;
        double in=unwrapped-offset; // swing and slow turning only
        if(std::abs(in)>720){offset=unwrapped;in=0;a.Reset();b.Reset();}
        float p[3]{float(in),0,0};
        a.Apply(p,ms);b.Apply(p,ms);
        return p[0]+offset;
    }
} yawSwing;
double PreciseMs() {
    static LARGE_INTEGER frequency{};
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    return double(now.QuadPart)*1000./double(frequency.QuadPart);
}
CameraMath::Matrix RenderBase(const CameraMath::Matrix& game) {
    // HorizontalDirection keeps only heading and position in PlayerCamera's
    // right/down/forward Z-up basis; the headset supplies all pitch and roll.
    if(yawOnlyCamera)return CameraMath::HorizontalDirection(game,game);
    return lockVerticalCamera?CameraMath::WithoutLookPitch(game):game;
}
struct Snapshot {
    CameraMath::Matrix originalWorld,world,view,manager;
    CameraMath::Matrix controllerBase; // Same post-comfort-filter base used for headset and weapon targets.
    Transport::Tracking tracking{};
    Transport::Tracking presentationTracking{};
    bool immersiveScoped{};
    uintptr_t managerAddress{};
    void* playerInstance{};
    uint32_t inputIndex=~0u;
    bool active{};
    bool nativeHeading{};
    CameraMath::Matrix tiltedWorld; // world before LevelFrozenView, still accepted by CreateHook
    bool leveled{};
};
Snapshot current;
// Local patch (level in-game menu): the game stops updating its camera while the
// GameMenu screen is open, so the VR view freezes at the last head pose. These are
// the inputs of the last tracked update, used to rebuild that pose level.
std::atomic<bool> gameMenuOpen{};
CameraMath::Matrix lastRenderBase,lastManagerRaw,lastOldView;
uint32_t menuUpdates{};
// Frames presented since the last tracked camera update. The game also stops its
// camera on loading/briefing screens; after frozenLevelFrames the view is leveled
// there too. The in-game menu, whose opening is hooked, is leveled at once.
bool updatedSincePresent{};uint32_t presentsWithoutUpdate{};
constexpr uint32_t frozenLevelFrames=10;
// Local patch (reader buttons): the e-reader and news reader are Scaleform screens
// shown over live gameplay. While one is open, [ScreenButtons] applies; the camera
// and snap turn are unchanged.
std::atomic<bool> readerOpen{};
// Local patch (weapon wheel): the wheel pauses the game with the camera stopped.
// While it is open the frozen view is left alone and the gameplay button layout is
// kept, so the button being held to show the wheel keeps working.
std::atomic<bool> wheelOpen{};
// controllerRaw is the muzzle BEFORE fubar's per-weapon grip offset is applied.
// muzzle carries that offset, which is right for the gun and wrong for the
// hand: the offset is a property of the weapon, not of where Mark's hand is.
struct WeaponPose {void* weapon{};void* instance{};CameraMath::Matrix muzzle;uint64_t tick{};bool active{};void* owner{};CameraMath::Matrix controllerRaw;bool nativeRequested{},nativeValid{};CameraMath::Matrix gripTarget{},nativeSkin{},delta{};uint64_t equippedTick{};uint32_t buttons{};Transport::Gamepad diagnosticPad{};};
WeaponPose weaponPose;
void* diagnosticWeapon{};void* diagnosticInstance{};uint64_t diagnosticEquippedTick{};
std::atomic<uint64_t> controllerDraws{},controllerMuzzles{};
std::atomic<uint64_t> controllerAimQueries{},controllerHiddenArms{};
std::atomic<uint64_t> controllerAttachments{},controllerBounds{};
thread_local bool nativeWeaponQuery{};
struct AttachmentTrace {uintptr_t caller{};int bone{};bool owner{};uint64_t queries{},corrected{};};
std::array<AttachmentTrace,96> attachmentTrace{};
std::mutex attachmentTraceMutex;
thread_local bool controllerDrawing{};
thread_local CameraMath::Matrix controllerDelta;
thread_local WeaponPose motionDrawPose;
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
// Experimental upstream pitch correction. Unlike LockVerticalCamera this
// changes the native aiming state, before the animated camera is composed.
bool nativeLookPitchLock{};
bool nativeLookHeading{};
bool nativeCameraPosition{};
uint64_t nativePositionUsed{},nativePositionFallback{},nativePositionLogTick{};
float nativePositionPeak{},nativePositionSprintPeak{};
NativeHeading::Sample nativeHeadingSample; // guarded by stateMutex
uint64_t nativeHeadingUsed{},nativeHeadingFallback{},nativeHeadingLogTick{};
float nativeHeadingPeakDelta{};
// [VR] NativeLookHeadingMaxDeg. The aiming yaw may only replace the camera's
// heading while the two still agree about where the player is facing. A
// scripted third-person shot (takedown, Icarus, Typhoon) swings the camera away
// from the body on a frame that still reports Combat ownership and a same-frame
// aiming sample, so every existing guard passes and the substitution lands a
// heading tens of degrees from the rendered scene. Measured 2026-10-20 in
// Mark's camera log: ordinary play peaks under 0.2 deg per 2 s window, while the
// windows containing scripted-camera transitions recorded 90.3 and 114.5 deg.
// Reject rather than clamp: a partial heading is not a heading anyone aimed.
// 0 restores the pre-2026-10-20 behaviour of always substituting.
// The limit alone is not enough. A scripted shot ENDS by swinging the camera
// back onto the body, so the disagreement sweeps down through the limit instead
// of stopping at it, and the first frame under the limit is still tens of
// degrees wrong. Measured 2026-10-20 with the limit at 60: the log recorded an
// applied maxRemovedDeg of 59.9694, then 32.7, 17.8 and 5.7 on the way down,
// and Mark still saw the artifact at the tail of two of five takedowns. So the
// rejection latches: once out, stay out until the two headings have agreed
// within nativeHeadingCalmDeg for a continuous nativeHeadingCalmMs, rather
// than resuming the moment the sweep crosses the limit. Ordinary play sits at
// 0.1 deg, so the latch clears almost immediately in normal movement.
// The limit is 5 because that is where the measurement put it, not a guess. In
// Mark's 2026-10-20 session, 56 logged windows under the latched build split
// cleanly: 53 of them applied between 0 and 1.22 deg, and the only others were
// 12.29, 32.48 and 51.03, with nothing in between. The first limit of 60 sat
// above all three anomalies it was meant to catch, so they were applied and
// never latched. 5 leaves four times the headroom over the observed normal
// maximum and still rejects at well under half the smallest anomaly.
// One source of truth for the default. It used to be written twice - here and
// as the INI read's fallback string - and on 2026-10-20 they diverged: the
// initialiser moved to 5 while the fallback stayed 60, so an absent key read
// back 60 and silently kept the old behaviour through a rebuild, an install and
// a headset test. Install() formats its fallback from this constant.
constexpr float nativeHeadingMaxDegDefault=5.f;
uint64_t nativeHeadingRejected{},nativeHeadingCalmSince{};
float nativeHeadingMaxDeg=nativeHeadingMaxDegDefault,nativeHeadingRejectedPeak{};
bool nativeHeadingLatched{};
constexpr float nativeHeadingCalmDeg=2.f;
constexpr uint64_t nativeHeadingCalmMs=250;
bool nativeHeadingWasUsed{};
using NativeLookInput=float*(__thiscall*)(void*,float*,void*);
using NativeLookSet=void(__thiscall*)(void*,const float*);
NativeLookInput originalNativeLookInput{};
std::atomic<uint64_t> nativeLookCalls{},nativeLookApplied{};
std::atomic<float> nativeLookBefore{},nativeLookAfter{},nativeLookYaw{};

bool NativeCombatCamera(void* instance,uintptr_t manager) {
    __try {
        if(!instance || !manager)return false;
        auto pc=reinterpret_cast<unsigned char*>(manager+0x6f0);
        if(*reinterpret_cast<void**>(manager+0x30)!=pc ||
           *reinterpret_cast<void**>(pc+0xaa0)!=instance)return false;
        auto mode=*reinterpret_cast<uint16_t*>(pc+0x774);
        if(mode>=19)return false;
        auto camera=*reinterpret_cast<unsigned char**>(pc+0x778+mode*0x24+8);
        return camera==pc+0xc0 && *reinterpret_cast<uintptr_t*>(camera)==base+0xaa7918-0x400000;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Validate live ownership and current camera mode, not just last frame's mode.
// No retained engine object pointers and no engine calls while holding our lock.
bool NativeLookOwner(void* self,void* context,void* instance,uintptr_t manager) {
    __try {
        auto p=static_cast<unsigned char*>(self);
        if(!p || !context || !instance || !manager ||
           *reinterpret_cast<uintptr_t*>(p)!=base+0xab4ca4-0x400000)return false;
        auto index=*reinterpret_cast<uint32_t*>(p+0x20);
        if(index>0xffffff)return false;
        auto registry=*reinterpret_cast<uintptr_t*>(base+0x19e1870-0x400000);
        if(!registry)return false;
        auto entry=reinterpret_cast<unsigned char*>(registry+index*8);
        if(entry[0]&2)return false;
        auto owner=*reinterpret_cast<unsigned char**>(entry+4);
        if(!owner || owner!=context || *reinterpret_cast<void**>(owner+0x20)!=instance)return false;
        return NativeCombatCamera(instance,manager);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
float* __fastcall NativeLookInputHook(void* self,void*,float* out,void* context) {
    ++nativeLookCalls;
    float* result=originalNativeLookInput(self,out,context);
    if((!nativeLookPitchLock && !nativeLookHeading) || !result || result!=out)return result;
    void* instance{};uintptr_t manager{};
    {
        std::lock_guard lock(stateMutex);
        auto now=GetTickCount64();
        if(!motionControls || !requested || !current.active || screenReasons || gameMenuOpen ||
           wheelOpen || conversationCamera.load() || !current.tracking.rightController.valid ||
           now<current.tracking.tick || now-current.tracking.tick>=250)return result;
        instance=current.playerInstance;manager=current.managerAddress;
    }
    if(!NativeLookOwner(self,context,instance,manager))return result;
    for(int i=0;i<4;i++)if(!std::isfinite(result[i]))return result;
    // Native setter uses MOVAPS: supply an explicitly aligned vector. Preserve
    // all components except pitch, including the yaw produced by native input.
    alignas(16) float angles[4];memcpy(angles,result,sizeof(angles));
    if(nativeLookPitchLock) {
        nativeLookBefore=angles[0];nativeLookYaw=angles[2];angles[0]=0;
        reinterpret_cast<NativeLookSet>(base+0x7a90a0-0x400000)(self,angles);
        result[0]=0;
        nativeLookAfter=*reinterpret_cast<float*>(static_cast<unsigned char*>(self)+0x30);
        if(nativeLookApplied.fetch_add(1)==0) {
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"nativeLookPitchLock firstApply pitchBefore=%g pitchAfter=%g yaw=%g self=%p\n",
                    nativeLookBefore.load(),nativeLookAfter.load(),nativeLookYaw.load(),self);fclose(f);
            }
        }
    }
    if(nativeLookHeading) {
        std::lock_guard lock(stateMutex);
        nativeHeadingSample={manager,reinterpret_cast<uintptr_t>(instance),
            *reinterpret_cast<const uint32_t*>(base+0x1c7bfc8-0x400000),angles[2],true};
    }
    return result;
}
bool DirectionWorld(DirectionConfig::Source source,CameraMath::Matrix& result,CameraMath::Matrix* native=nullptr,uintptr_t manager=0) {
    std::lock_guard lock(stateMutex);
    auto now=GetTickCount64();
    if(source==DirectionConfig::Source::Mouse || !requested || !current.active || !current.playerInstance || screenReasons ||
       (manager && manager!=current.managerAddress) || now<current.tracking.tick || now-current.tracking.tick>=250)return false;
    if(native)*native=current.originalWorld;
    if(source==DirectionConfig::Source::Headset){result=current.world;return true;}
    auto aim=DirectionConfig::Aim(source,current.tracking);
    if(!motionControls || !aim)return false;
    auto baseWorld=current.nativeHeading?current.controllerBase:RenderBase(current.originalWorld);
    result=CameraMath::HeadWorld(baseWorld,reference,aim->aim,worldScale);
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
    ++responseHookCalls;
    float value=originalInputAxis(self,index,requireActive);
    auto walk=walkAction.load(),strafe=strafeAction.load();
    if(walk&&strafe&&(self==walk||self==strafe))++responseActionCalls;
    const auto source=movementDirection.load();
    bool gentle=motionControls&&movementResponse.load()==1;
    if((source==DirectionConfig::Source::Mouse&&!gentle) || !walk || !strafe || (self!=walk && self!=strafe))return value;
    {std::lock_guard lock(stateMutex);if(index!=current.inputIndex)return value;}
    CameraMath::Matrix target,native;
    if(!DirectionWorld(source==DirectionConfig::Source::Mouse?DirectionConfig::Source::Headset:source,target,&native))return value;
    // Both locomotion actions must be evaluated: W can become a strafe even
    // when the original strafe action is zero/inactive. Call the trampoline
    // for the partner axis so the pair is rotated exactly once.
    float x=self==strafe?value:originalInputAxis(strafe,index,requireActive);
    float y=self==walk?value:originalInputAxis(walk,index,requireActive);
    if(!std::isfinite(x) || !std::isfinite(y))return value;
    auto shaped=CameraMath::MovementResponse(x,y,gentle);
    auto axes=source==DirectionConfig::Source::Mouse?shaped:CameraMath::ReorientMovement(shaped.strafe,shaped.walk,native,target);
    ++responsePassed;
    float inputMagnitude=std::hypot(x,y),outputMagnitude=std::hypot(axes.strafe,axes.walk);
    if(inputMagnitude>.001f){
        std::lock_guard lock(stateMutex);
        responseSampleMode=gentle?1u:0u;
        responseInputMagnitude=inputMagnitude;responseOutputMagnitude=outputMagnitude;responseSampleTick=GetTickCount64();
        if(outputMagnitude<inputMagnitude-.0001f)++responseReduced;
    }
    ++movementAxes;
    if(std::abs(x)+std::abs(y)>.01f) {
        std::lock_guard lock(stateMutex);
        lastMoveInput[0]=x;lastMoveInput[1]=y;
        lastMoveOutput[0]=axes.strafe;lastMoveOutput[1]=axes.walk;
        lastMoveHeading=CameraMath::HeadingDelta(native,target);
    }
    return self==strafe?axes.strafe:axes.walk;
}
bool ResolveNativePair(WeaponPose& pose,MuzzleQueryDiagnostic::Site site=MuzzleQueryDiagnostic::Camera,void* subject=nullptr);
WeaponPose ReadWeaponPose(bool resolve=true,MuzzleQueryDiagnostic::Site site=MuzzleQueryDiagnostic::Camera,void* subject=nullptr) {
    WeaponPose pose;
    {
        std::lock_guard lock(stateMutex);pose=weaponPose;
        auto now=GetTickCount64();
        pose.active=pose.active && current.active && requested && !screenReasons && now>=pose.tick && now-pose.tick<250;
    }
    // Native animation is read at consumption time, not retained as a learned
    // grip or a previous-frame offset. No state mutex held during engine calls.
    if(resolve && pose.active && pose.nativeRequested &&
       (controllerHandsForceFallback.load() || !ResolveNativePair(pose,site,subject))) {
        // A rejected hand model must not disable the original floating weapon
        // or its firing ray. Use the uncalibrated controller muzzle consistently.
        pose.nativeValid=false;pose.nativeRequested=false;pose.muzzle=pose.controllerRaw;
        pose.active=NativeHandWeapon::Rigid(pose.muzzle);
    }
    return pose;
}

uint32_t RigidFault(const CameraMath::Matrix& m) {
    for(float v:m.m)if(!std::isfinite(v))return 1;
    if(std::abs(m.m[15]-1.f)>.01f)return 2;
    for(int r=0;r<3;r++)for(int c=r;c<3;c++) {
        float dot=0;for(int j=0;j<3;j++)dot+=m.m[r*4+j]*m.m[c*4+j];
        if(std::abs(dot-(r==c?1.f:0.f))>.02f)return 3;
    }
    return 0;
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
// CRASHED THE GAME 2026-10-14, 1:32 PM, while Mark was calibrating. The dump
// (dxhrdc_66_crash_2026_10_14T17_32_6C0.mdmp) is an access violation reading
// 0x4B1 at DXHRDC.exe+0x34F3E7 = 0x74F3E7 - the fourth instruction of this very
// function:
//     0074F3E4  mov eax,[esi+78h]           ; weapon->+0x78
//     0074F3E7  cmp byte ptr [eax+4B1h],0   ; <- eax was NULL
// The game assumes +0x78 is live and does not check it. The mod calls here with
// `pose.weapon`, which ReadWeaponPose allows to be up to 250 ms STALE - so a
// weapon torn down inside that window (a switch, a holster, a calibration tap)
// leaves a dead pointer that the game then walks off.
//
// Checked here rather than at the call site because every caller has the same
// exposure. The vtable test is the one the weapon path already uses elsewhere.
bool WeaponMuzzleSafe(void* weapon) {
    if(!weapon)return false;
    auto w=static_cast<unsigned char*>(weapon);
    // Still a weapon object, not freed memory that happens to be mapped.
    if(*reinterpret_cast<uintptr_t*>(w)!=base+0xab1944-0x400000)return false;
    // The exact field the faulting instruction dereferences.
    if(!*reinterpret_cast<void**>(w+0x78))return false;
    return true;
}
uintptr_t NativeWeaponMuzzle(void* weapon,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    if(!WeaponMuzzleSafe(weapon))return 0;
    auto previous=nativeWeaponQuery;nativeWeaponQuery=true;
    auto result=originalWeaponMuzzle(weapon,out,barrel,firstPerson);
    nativeWeaponQuery=previous;return result;
}
// Frame and pivot are immutable model data. The live bone/muzzle are never cached.
bool NativeModelFrame(void* model,const std::shared_ptr<const HandOnlyGeometry::Plan>& plan,
                      CameraMath::Matrix& frame,ModelHandAlignment::V& pivot) {
    static std::mutex guard;
    static std::shared_ptr<const HandOnlyGeometry::Plan> previous;
    static CameraMath::Matrix cachedFrame{};static ModelHandAlignment::V cachedPivot{};
    static bool ready{};static uint64_t retryAfter{};
    std::lock_guard lock(guard);
    if(previous!=plan){previous=plan;ready=false;retryAfter=0;}
    if(!ready && GetTickCount64()>=retryAfter){
        retryAfter=GetTickCount64()+1000;
        uint32_t list{},header[2]{};std::array<unsigned char,223*64> raw{};
        if(HandMeshDiagnostic::Read(uintptr_t(model)+0x68,&list,4) && list &&
           HandMeshDiagnostic::Read(list,header,sizeof(header)) && header[0]==plan->boneCount && header[1] &&
           HandOnlyGeometry::ReadBlock(header[1],raw.data(),plan->boneCount*64)) {
            std::array<ModelHandAlignment::Segment,223> segments{};
            for(unsigned i=0;i<plan->boneCount;i++){
                float xyz[3]{};memcpy(xyz,raw.data()+i*64+0x20,sizeof(xyz));
                for(int j=0;j<3;j++)segments[i].pivot[j]=xyz[j];
                memcpy(&segments[i].parent,raw.data()+i*64+0x38,4);
            }
            ready=NativeHandWeapon::Frame(segments,cachedFrame,cachedPivot,plan->boneCount);
        }
    }
    if(!ready)return false;frame=cachedFrame;pivot=cachedPivot;return true;
}
bool LeftModelFrame(void* model,const std::shared_ptr<const HandOnlyGeometry::Plan>& plan,
                      CameraMath::Matrix& frame,ModelHandAlignment::V& pivot) {
    static std::mutex guard;
    static std::shared_ptr<const HandOnlyGeometry::Plan> previous;
    static CameraMath::Matrix cachedFrame{};static ModelHandAlignment::V cachedPivot{};
    static bool ready{};static uint64_t retryAfter{};
    std::lock_guard lock(guard);
    if(previous!=plan){previous=plan;ready=false;retryAfter=0;}
    if(!ready && GetTickCount64()>=retryAfter){
        retryAfter=GetTickCount64()+1000;
        uint32_t list{},header[2]{};std::array<unsigned char,223*64> raw{};
        if(HandMeshDiagnostic::Read(uintptr_t(model)+0x68,&list,4) && list &&
           HandMeshDiagnostic::Read(list,header,sizeof(header)) && header[0]==plan->boneCount && header[1] &&
           HandOnlyGeometry::ReadBlock(header[1],raw.data(),plan->boneCount*64)) {
            std::array<ModelHandAlignment::Segment,223> segments{};
            for(unsigned i=0;i<plan->boneCount;i++){
                float xyz[3]{};memcpy(xyz,raw.data()+i*64+0x20,sizeof(xyz));
                for(int j=0;j<3;j++)segments[i].pivot[j]=xyz[j];
                memcpy(&segments[i].parent,raw.data()+i*64+0x38,4);
            }
            ready=ModelHandAlignment::BuildLeftFrame(segments,cachedFrame,plan->boneCount);
        }
    }
    if(!ready)return false;frame=cachedFrame;pivot=cachedPivot;return true;
}
bool ReadNativePalm(WeaponPose& pose,CameraMath::Matrix& skin,CameraMath::Matrix& delta) {
    if(!pose.owner || !pose.weapon)return false;
    uint32_t model{},bones{},owner{};
    if(!HandMeshDiagnostic::Read(uintptr_t(pose.weapon)+0x64,&owner,4) || owner!=uintptr_t(pose.owner) ||
       !NativeHandWeapon::OwnerModel(uintptr_t(pose.owner),HandMeshDiagnostic::Read,model,bones)){++nativePairOwnerFailed;return false;}
    auto plan=HandOnlyGeometry::Prepare(reinterpret_cast<void*>(uintptr_t(model)));
    if(!plan){++nativePairModelFailed;return false;}
    CameraMath::Matrix frame{},bone{};ModelHandAlignment::V pivot{};
    if(!NativeModelFrame(reinterpret_cast<void*>(uintptr_t(model)),plan,frame,pivot)){++nativePairModelFailed;return false;}
    if(!HandMeshDiagnostic::Read(uintptr_t(bones)+130*sizeof(bone),&bone,sizeof(bone)) ||
       !NativeHandWeapon::Skin(bone,pivot,skin) || !NativeHandWeapon::Delta(skin,frame,pose.gripTarget,delta)){++nativePairBoneFailed;return false;}
    return true;
}
bool ResolveNativePair(WeaponPose& pose,MuzzleQueryDiagnostic::Site site,void* subject) {
    pose.nativeValid=false;
    auto fail=[](){++nativePairFailed;return false;};
    if(nativePairFault.load() || handsSuspendedByMode.load() || handsFarFromBody.load() ||
       !activeCameraIsPlayer.load())return fail();
    CameraMath::Matrix skin{},delta{},native{};
    if(!ReadNativePalm(pose,skin,delta))return fail();
    auto nativeResult=NativeWeaponMuzzle(pose.weapon,&native,0,true);
    bool relevant=site==MuzzleQueryDiagnostic::Camera ||
        ((site==MuzzleQueryDiagnostic::Attachment || site==MuzzleQueryDiagnostic::Draw)?subject==pose.instance:subject==pose.weapon);
    if(!NativeHandWeapon::Rigid(native)){
        ++nativePairMuzzleFailed;
        MuzzleQueryDiagnostic::Sample sample{};sample.tick=GetTickCount64();sample.frame=frameId.load();
        sample.weapon=uintptr_t(pose.weapon);sample.instance=uintptr_t(pose.instance);sample.owner=uintptr_t(pose.owner);
        sample.nativeReturn=nativeResult;sample.matrix=native;sample.relevant=relevant;sample.safe=WeaponMuzzleSafe(pose.weapon);
        sample.poseAge=sample.tick>=pose.tick?sample.tick-pose.tick:UINT64_MAX;
        sample.weaponAge=sample.tick>=pose.equippedTick?sample.tick-pose.equippedTick:UINT64_MAX;
        sample.mode=cameraModeIndex.load();sample.buttons=pose.buttons;sample.pad=pose.diagnosticPad;
        auto read=HandMeshDiagnostic::Read;uint32_t info{},definition{},firstPerson{};uint8_t byte{};
        read(sample.weapon+0x58,&sample.key,4);
        if(read(sample.weapon+0x78,&info,4) && info){
            if(read(info+0x4b0,&byte,1))sample.sourceOnOwner=byte;
            if(read(info+0x4b1,&byte,1))sample.dualBarrel=byte;
        }
        if(read(sample.weapon+0x70,&firstPerson,4) && firstPerson && read(firstPerson+0x1b,&byte,1))sample.firstPersonFlag=byte;
        if(read(sample.weapon+0x7c,&definition,4) && definition && read(definition+0x520,&byte,1))sample.attachment=int(int8_t(byte));
        MuzzleQueryDiagnostic::Failure(site,sample);return fail();
    }
    MuzzleQueryDiagnostic::Success(site,relevant,uintptr_t(pose.weapon),GetTickCount64());
    pose.muzzle=CameraMath::Multiply(native,delta);pose.nativeSkin=skin;pose.delta=delta;
    pose.nativeValid=true;++nativePairUsed;return true;
}
// SEH, so an out-of-range index cannot take the process down. Deliberately no
// locals that need unwinding, which MSVC forbids in a __try.
static bool ProbeAttachment(void* src,void* inst,int bone,CameraMath::Matrix* out) {
    __try {
        originalAttachment(src,inst,bone,out,false);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// One shot per F8. The file IO is the same explicit-request convention the rest
// of the capture block uses; it is never scheduled.
void ProbeHandBones(void* src,void* inst,const CameraMath::Matrix& muzzle) {
    auto sample=handProbeSamples.fetch_add(1)+1;
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-camera.log","a") || !f)return;
    int lo=controllerHandsProbeFrom,hi=controllerHandsProbeTo;
    if(lo<0)lo=0;
    if(hi>lo+256)hi=lo+256;
    fprintf(f,"handProbe sample=%u muzzle=(%.1f,%.1f,%.1f) range=%d-%d source=%p instance=%p\n",
        sample,muzzle.m[12],muzzle.m[13],muzzle.m[14],lo,hi,src,inst);
    for(int b=lo;b<hi;b++) {
        CameraMath::Matrix a{};
        if(!ProbeAttachment(src,inst,b,&a)) {
            fprintf(f,"handProbe sample=%u bone=%d faulted\n",sample,b);continue;
        }
        if(!RigidWeaponMatrix(a)) {
            fprintf(f,"handProbe sample=%u bone=%d notRigid\n",sample,b);continue;
        }
        float d=0;for(int j=0;j<3;j++){float e=a.m[12+j]-muzzle.m[12+j];d+=e*e;}
        fprintf(f,"handProbe sample=%u bone=%d pos=(%.1f,%.1f,%.1f) muzzleDist=%.1f\n",
            sample,b,a.m[12],a.m[13],a.m[14],std::sqrt(d));
    }
    fclose(f);
}
uintptr_t __cdecl AttachmentHook(void* source,void* instance,int bone,CameraMath::Matrix* out,bool previousFrame) {
    auto result=originalAttachment(source,instance,bone,out,previousFrame);
    if(experimentalMotionControls && !nativeWeaponQuery && instance && out) {
        auto pose=ReadWeaponPose(true,MuzzleQueryDiagnostic::Attachment,instance);
        bool corrected=false;
        // Probe here and nowhere else: these pointers are the game's, live on
        // this call. Requiring its own result to be a valid rigid matrix also
        // keeps us out of a half-built actor during a load.
        if(controllerHandsProbe && pose.active && pose.weapon && instance==pose.owner
           && RigidWeaponMatrix(*out) && handProbeSamples.load()<8) {
            auto now=GetTickCount64();
            if(now>=handProbeNextTick.load()) {
                CameraMath::Matrix probeMuzzle;
                NativeWeaponMuzzle(pose.weapon,&probeMuzzle,0,true);
                if(RigidWeaponMatrix(probeMuzzle)) {
                    handProbeNextTick.store(now+2000);
                    ProbeHandBones(source,instance,probeMuzzle);
                }
            }
        }
        if(pose.active && instance==pose.instance && RigidWeaponMatrix(*out)) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                // Effects ask the Instance attachment getter directly, rather
                // than PrimaryWeapon's firing matrix. Preserve each attachment's
                // offset (flash, shell ejection, etc.) relative to the barrel.
                auto delta=pose.nativeValid?pose.delta:CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                *out=CameraMath::Multiply(*out,delta);++controllerAttachments;corrected=true;
            }
        }
        if(pose.active && (instance==pose.instance || instance==pose.owner)) {
            auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-base+0x400000;
            std::lock_guard lock(attachmentTraceMutex);
            for(auto& t:attachmentTrace)if(!t.queries || (t.caller==caller && t.bone==bone && t.owner==(instance==pose.owner))) {
                t.caller=caller;t.bone=bone;t.owner=instance==pose.owner;++t.queries;t.corrected+=corrected;
                break;
            }
        }
    }
    return result;
}
void __fastcall BoundsHook(void* self,void*,void* volume) {
    originalBounds(self,volume);
    if(!experimentalMotionControls || !volume || *reinterpret_cast<uintptr_t*>(self)!=base+0xaaf854-0x400000)return;
    // Cell lookup (0x5c8950) has no callback for type 10 (Everything):
    // forcing it here caused an indirect call through zero while loading.
    // Enlarge only finite bounds, preserving their native centre.
    //
    // From upstream PR #8 by Carnuntum: enlarge every gun, not only while the
    // controller pose is active. A weapon equipped while the level loads (the
    // pistol) has its volume taken before the pose is active, so it kept the
    // native size and vanished once the hand left the view around the game
    // camera's own heading - the weapon-switch-plus-roomscale-turn report.
    if(NativeBounds::ExpandWeapon(volume,4.f*worldScale))++controllerBounds;
}
uintptr_t __fastcall WeaponMuzzleHook(void* self,void*,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    auto result=NativeWeaponMuzzle(self,out,barrel,firstPerson);
    if(senseQueries)return result;
    if(experimentalMotionControls && out) {
        auto pose=ReadWeaponPose(true,MuzzleQueryDiagnostic::Muzzle,self);
        if(pose.active && pose.weapon==self){
            *out=pose.nativeValid?CameraMath::Multiply(*out,pose.delta):pose.muzzle;++controllerMuzzles;
        }
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
        auto pose=ReadWeaponPose(true,MuzzleQueryDiagnostic::Aim,self);
        // Player hit calculation 0x763370 and aim-ray getter 0x750ef0 both
        // use this matrix, bypassing WeaponMuzzle for ordinary player shots.
        // Keep the game's spread, collision and damage calculation intact.
        if(pose.active && pose.weapon==self){*out=CameraMath::FiringFromMuzzle(pose.muzzle);++controllerAimQueries;}
    }
    return result;
}
void __fastcall ActorDrawHook(void* self,void*,void* matrix,void* args) {
    if(fixedDialogueCamera) {
        // Held at Jensen's eyes in a conversation: his own head and body would
        // fill the view. current.playerInstance is PlayerCamera's Instance.
        std::lock_guard lock(stateMutex);
        if(holdingPlayerBase && current.active && current.playerInstance &&
           *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==current.playerInstance)return;
    }
    if(experimentalMotionControls && (controllerHideArms || controllerHands)) {
        auto pose=ReadWeaponPose(false); // A holstered/missing muzzle must not hide the unarmed hand.
        // Suppress only the equipped weapon owner's actor mesh in tracked
        // player view. Animation, physics and other actors still run normally.
        if(pose.active && pose.owner && *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==pose.owner) {
            // THE CAMERA IS NOT AT HIS EYES (the game's own camera mode says so:
            // cover, a ladder, an animation-driven shot). Everything this feature
            // does assumes a first-person view, so it steps aside completely:
            // the actor draws natively and handDrawing stays false, so
            // SkeletonHook never touches the skin palette. Jensen renders as the
            // game intended - no stretch to the controller, no collapsed bones,
            // and nothing hidden. Only the hands feature yields; a user with
            // ControllerHands=0 sees no change at all.
            // Not at his eyes, by either test: the player camera is in a
            // third-person MODE, or the game is rendering from a different
            // CAMERA entirely (a cinematic, the Icarus landing). The exception is
            // a conversation, where FixedDialogueCamera holds the render base at
            // Jensen's own eyes - the view is at his eyes even though the active
            // camera is not his, so the hands belong there and stay.
            if(controllerHands &&
               (handsSuspendedByMode.load() ||
                (!activeCameraIsPlayer.load() && !holdingPlayerBase) ||
                (handsFarFromBody.load() && !holdingPlayerBase))) {

                originalActorDraw(self,matrix,args);
                return;
            }
            // ControllerHands REPLACES the suppression rather than depending on
            // it: the mesh is drawn, and SkeletonHook keeps the hand bones while
            // collapsing every other bone, so only the hands reach the screen.
            if(!controllerHands || (controllerHandsModelAlignment.load() && controllerHandsMode==15 &&
               HandDrawDiagnostic::hookStatus.load()!=1)){++controllerHiddenArms;return;}
            // ControllerMuzzle() builds the muzzle from the controller's own hand
            // pose and pushes it forward along that pose's third row, which it
            // stores negated in the muzzle's second row. Undoing that push gives
            // the grip position back with no new transport data.
            float forward=controllerMuzzleForward*worldScale;
            for(int j=0;j<3;j++)handTarget[j]=pose.controllerRaw.m[12+j]+pose.controllerRaw.m[4+j]*forward;
            // The gun is held in the hand, so the transform that carries the gun
            // to the controller carries the hand with it.
            handDeltaValid=false;
            // pose.muzzle is built from the CONTROLLER, so it is valid with the
            // weapon holstered. Only the weapon's own muzzle is not. Modes
            // 12-13 use this one and never the weapon's, which is why they
            // behave the same armed and holstered.
            // The RAW controller muzzle, not pose.muzzle: the latter carries the
            // equipped weapon's saved grip offset, and one of Mark's weapons has
            // 0.91 m stored, which aimed the hand a metre off whenever it was
            // drawn. Preferred, with the calibrated muzzle as a fallback so the
            // feature still works if raw turns out to be unusable - but the
            // fallback is COUNTED, never silent, because it reintroduces exactly
            // the weapon dependence this change removed.
            auto rawFault=RigidFault(pose.controllerRaw);
            handRawFault.store(rawFault);
            handRawW15=pose.controllerRaw.m[15];
            handRawRow0=pose.controllerRaw.m[0]*pose.controllerRaw.m[0]
                       +pose.controllerRaw.m[1]*pose.controllerRaw.m[1]
                       +pose.controllerRaw.m[2]*pose.controllerRaw.m[2];
            handMuzzleValid=rawFault==0;
            if(handMuzzleValid){handMuzzle=pose.controllerRaw;handRawUsed.fetch_add(1);}
            else {
                handRawRejected.fetch_add(1);
                if(RigidWeaponMatrix(pose.muzzle)) {
                    handMuzzle=pose.muzzle;handMuzzleValid=true;handFallbackUsed.fetch_add(1);
                }
            }
            handNativeMuzzleValid=false;
            if(pose.weapon) {
                CameraMath::Matrix nativeMuzzle;
                NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
                if(RigidWeaponMatrix(nativeMuzzle)) {
                    handNativeMuzzle=nativeMuzzle;handNativeMuzzleValid=true;
                    handWeaponKey=pose.weapon
                        ?*reinterpret_cast<uint32_t*>(
                             reinterpret_cast<unsigned char*>(pose.weapon)+0x58)
                        :0xffffffffu;
                    if(handWeaponKey!=handGripSeenKey) {
                        handGripSeenKey=handWeaponKey;
                        handGripRemeasure=true;
                    }
                    handDelta=CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                    handDeltaValid=true;
                }
            }
            if(!handNativeMuzzleValid)handUnarmedDraws.fetch_add(1);
            ++handDraws;handDrawing=true;handPrimIndex=0;
            originalActorDraw(self,matrix,args);
            handDrawing=false;
            handPrimPieces.store(handPrimIndex);
            // One write per draw for the first three draws, then never again:
            // the structure is what is wanted, not a per-frame stream.
            if(handPrimLogged<3) {
                ++handPrimLogged;
                FILE* f{};
                if(!fopen_s(&f,"DeusExHRVR-camera.log","a") && f) {
                    fprintf(f,"handPrims draw=%d pieces=%u shaderCalls=%u distinctShaders=%u\n",
                        handPrimLogged,handPrimIndex,handShaderCalls.load(),handShaderCount);
                    for(uint32_t i=0;i<handShaderCount;i++) {
                        bool shared=false;
                        for(uint32_t j=0;j<otherShaderCount;j++)
                            if(otherShaders[j]==handShaders[i]){shared=true;break;}
                        fprintf(f,"handPrims armShader=%08x sharedWithOtherDraws=%d\n",
                            unsigned(handShaders[i]),int(shared));
                    }
                    fprintf(f,"handPrims otherShadersSeen=%u\n",otherShaderCount);
                    for(uint32_t i=0;i<handPrimTraceCount;i++) {
                        const auto& t=handPrimTrace[i];
                        fprintf(f,"handPrims piece=%u primitive=%08x material=%08x flags=%08x\n",
                            t.index,unsigned(t.primitive),unsigned(t.material),t.flags);
                    }
                    fclose(f);
                }
                handPrimTraceCount=0;
            }
            return;
        }
    }
    originalActorDraw(self,matrix,args);
}
void MotionContext(HandWeaponMotionDiagnostic::Sample& sample,const WeaponPose& pose,unsigned site){
    sample.site=site;sample.frame=frameId.load();sample.tick=GetTickCount64();
    sample.weapon=uintptr_t(pose.weapon);sample.instance=uintptr_t(pose.instance);
    sample.sprint=handPoseSprinting.load();sample.nativeValid=pose.nativeValid;
    sample.skin=pose.nativeSkin;sample.delta=pose.delta;sample.target=pose.gripTarget;sample.muzzle=pose.muzzle;
    if(pose.weapon)HandMeshDiagnostic::Read(uintptr_t(pose.weapon)+0x58,&sample.key,4);
    std::lock_guard lock(stateMutex);sample.tracking=current.tracking.id;
    sample.base=current.controllerBase;sample.viewWorld=current.world;
}
bool TryModelHand(void* model,unsigned char* data,CameraMath::Matrix* palette,uint32_t n) {
    CameraMath::Matrix target{},leftTarget{};bool leftValid=false;
    {
        std::lock_guard lock(stateMutex);
        if(!requested || !current.active || !referenceValid || screenReasons ||
           !ModelHandAlignment::GripTarget(current.tracking,GetTickCount64(),current.controllerBase,reference,worldScale,target)) {
            ++modelHandNoGrip;return false;
        }
        if(controllerHandsLeft.load())leftValid=
            ModelHandAlignment::GripTarget(current.tracking,GetTickCount64(),current.controllerBase,reference,worldScale,leftTarget,true);
    }
    auto plan=(HandDrawDiagnostic::hookStatus.load()==1)?HandOnlyGeometry::Prepare(model):nullptr;
    if(!plan || plan->boneCount!=n){++modelHandBadModel;return false;}
    CameraMath::Matrix frame{};ModelHandAlignment::V pivot{};
    if(!NativeModelFrame(model,plan,frame,pivot)){++modelHandBadModel;return false;}
    auto pose=ReadWeaponPose(false);
    bool capture=HandWeaponMotionDiagnostic::Wanted(0,frameId.load());
    HandWeaponMotionDiagnostic::Sample motion{};
    if(capture){MotionContext(motion,pose,0);motion.count=2;motion.before[0]=palette[129];motion.before[1]=palette[130];}
    if(pose.nativeRequested){
        CameraMath::Matrix skin{},delta{};
        bool paired=pose.active && ReadNativePalm(pose,skin,delta);
        if(capture){motion.nativeValid=paired;motion.skin=skin;motion.delta=delta;}
        // Independent live check: simulation reconstruction must match the
        // original renderer output BEFORE either is modified. Fail closed.
        if(paired){
            ++nativePairCompared;
            float difference=NativeHandWeapon::Difference(ModelHandAlignment::Affine(palette[130]),skin);
            auto maximum=nativePairMaxDifference.load();
            while(maximum<difference && !nativePairMaxDifference.compare_exchange_weak(maximum,difference)){}
            if(difference>.05f){
                ++nativePairMismatch;nativePairFault=true;paired=false;
            }
        }
        // Preserve the approved unarmed hand even if a weapon query is unavailable.
        bool moved=paired?ModelHandAlignment::MoveWithDelta(palette,n,plan->bones,delta):
            ModelHandAlignment::Move(palette,n,plan->bones,frame,pose.gripTarget);
        if(!moved){++modelHandBadTransform;return false;}
    }else if(!ModelHandAlignment::Move(palette,n,plan->bones,frame,target)){
        ++modelHandBadTransform;return false;
    }
    // Before the capture below, which records palette[129] as its "after".
    if(controllerHandsNeutralWrist.load() &&
       !ModelHandAlignment::NeutralWrist(palette,n,plan->bones))++neutralWristFailed;
    if(capture){motion.after[0]=palette[129];motion.after[1]=palette[130];HandWeaponMotionDiagnostic::Push(motion);}
    CameraMath::Matrix leftFrame{};ModelHandAlignment::V unused{};
    bool leftMoved=leftValid && LeftModelFrame(model,plan,leftFrame,unused) &&
        ModelHandAlignment::RestLeft(palette,n,plan->leftBones,leftFrame,leftTarget);
    HandOnlyGeometry::Activate(uintptr_t(data),frameId.load(),plan,palette[130].m,leftMoved);

    ++modelHandApplied;return true;
}
void __cdecl SkeletonHook(void* model,void* bones,uint32_t flags,uint32_t count,void* state) {
    originalSkeleton(model,bones,flags,count,state);
    // Every freshly generated palette invalidates any previous VR-only tag,
    // including native third-person draws that bypass handDrawing entirely.
    if(state && *static_cast<uintptr_t*>(state)==base+0xa97524-0x400000)
        HandOnlyGeometry::Invalidate(*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(state)+0x10));
    // ControllerHands: keep the hand bones, collapse the rest. Same validated
    // palette as the weapon path below, reached through the actor's own draw.
    if(handDrawing) {
        HandMeshDiagnostic::Capture(model,count);
        bool ok=false;
        if(state) {
            auto s=static_cast<unsigned char*>(state);
            if(*reinterpret_cast<uintptr_t*>(s)==base+0xa97524-0x400000) {
                if(auto data=*reinterpret_cast<unsigned char**>(s+0x10)) {
                    auto n=*reinterpret_cast<uint32_t*>(data);
                    if(n && n<=512 && n==count) {
                        HandDrawDiagnostic::MarkPose(uintptr_t(data),frameId.load());
                        handBoneCount=n;++handSkeletons;ok=true;
                        auto m=reinterpret_cast<CameraMath::Matrix*>(data+0x10);
                        // Separate experimental path: no handBind, no capture, no
                        // weapon grip learning, and no forearm collapse/weld.
                        if(controllerHandsModelAlignment.load() && controllerHandsMode==15){
                            if(controllerHandsForceFallback.load() || !TryModelHand(model,data,m,n)) {
                                uint32_t render{};
                                if(HandMeshDiagnostic::Read(uintptr_t(model)+0x64,&render,4) && render)
                                    HandOnlyGeometry::Hide(uintptr_t(data),frameId.load(),render,m[0].m);
                            }
                            return;
                        }
                        int first=controllerHandBone,second=controllerHandBone2;
                        if(first<0 || uint32_t(first)>=n)first=-1;
                        if(second<0 || uint32_t(second)>=n || second==first)second=-1;
                        if(controllerHandsMode>=12 && controllerHandsMode<=15 && handMuzzleValid) {
                            // Calibration: with the controller held on the
                            // visible hand, controllerPose = bindPose x
                            // paletteEntry, so bindPose follows directly - for
                            // every bone at once.
                            if(handCalibrateArmed.load() && !controllerHandsModelAlignment.load()) {
                                // Capture once the controller has stayed inside
                                // a small ball for long enough. Drifting out of
                                // it restarts the window, so a reach past the
                                // target cannot be mistaken for a hold.
                                auto now=GetTickCount64();
                                float tol=controllerHandsSteadyCm*worldScale/100.f;
                                float dist=0;
                                if(handSteadyValid) {
                                    float d2=0;
                                    for(int j=0;j<3;j++){float e=handMuzzle.m[12+j]-handSteadyRef[j];d2+=e*e;}
                                    dist=std::sqrt(d2);
                                }
                                if(!handSteadyValid || dist>tol || handSteadySince<handCalibrateArmTick.load()) {
                                    for(int j=0;j<3;j++)handSteadyRef[j]=handMuzzle.m[12+j];
                                    handSteadySince=now;handSteadyValid=true;handSteadyWorst=0;
                                    // Restart the average with the window.
                                    for(uint32_t i=0;i<n && i<512;i++)handBindAccum[i]=CameraMath::Matrix{};
                                    handBindSamples=0;
                                }
                                else {
                                    if(dist>handSteadyWorst)handSteadyWorst=dist;
                                    // Every frame of the hold contributes, so hand
                                    // tremor and tracking jitter cancel instead of
                                    // being rejected.
                                    for(uint32_t i=0;i<n && i<512;i++) {
                                        bool ok=false;
                                        auto inv=AffineInverse(SkinAffine(m[i]),&ok);
                                        if(ok) {
                                            auto b=CameraMath::Multiply(handMuzzle,inv);
                                            for(int k=0;k<16;k++)handBindAccum[i].m[k]+=b.m[k];
                                        }
                                    }
                                    ++handBindSamples;
                                    if(now-handSteadySince>=uint64_t(controllerHandsSteadyMs>0?controllerHandsSteadyMs:800)) {
                                        handBindCount=n;
                                        float inv=handBindSamples?1.f/float(handBindSamples):1.f;
                                        for(uint32_t i=0;i<n;i++) {
                                            auto b=handBindAccum[i];
                                            for(int k=0;k<16;k++)b.m[k]*=inv;
                                            Orthonormalize(b);
                                            handBind[i]=b;
                                        }
                                        handCalibrated=true;
                                        handCalibrateQuality=handSteadyWorst;
                                        handCalibrateArmed.store(0);
                                        handSteadyValid=false;
                                        LogHandCalibration(m,n);
                                        SaveHandCalibration();
                                        // A different tone from the arming beep, so
                                        // the two are distinguishable by ear alone.
                                        MessageBeep(MB_OK);
                                    }
                                }
                            }
                            if(controllerHandsMode==14) {
                                // Pure observation: nothing is moved, so the
                                // arms stay exactly as the game animates them.
                                IdentifyHandBone(m,n);
                            }
                            else {
                                int drive=controllerHandsDriveBone;
                                // Not while armed: the alignment needs a hand that
                                // STAYS PUT to aim at, and a driven hand follows the
                                // controller, so there would be no fixed reference.
                                // The arm snapping back to its animated pose is also
                                // the visual confirmation that the tap armed it.
                                if(!handCalibrateArmed.load() && handCalibrated
                                   && drive>=0 && uint32_t(drive)<n && uint32_t(drive)<handBindCount) {
                                    // Capture or replay BEFORE anything reads the
                                    // palette, so a replayed hand flows through the
                                    // existing drive math untouched: `world` below
                                    // is rebuilt from the substituted wrist entry
                                    // and the whole band moves as one piece.
                                    HandPoseStep(m,n);
                                    auto world=CameraMath::Multiply(handBind[drive],SkinAffine(m[drive]));
                                    auto target=handMuzzle;
                                    if(handTrim[0]||handTrim[1]||handTrim[2]||handTrimRot[0]||handTrimRot[1]||handTrimRot[2]) {
                                        // Offset and rotation in the controller's
                                        // own axes, so both ride with the hand.
                                        CameraMath::Matrix off{};
                                        off.m[0]=off.m[5]=off.m[10]=off.m[15]=1;
                                        const float d2r=.01745329252f;
                                        for(int axis=0;axis<3;axis++) {
                                            if(!handTrimRot[axis])continue;
                                            float c=std::cos(handTrimRot[axis]*d2r),sn=std::sin(handTrimRot[axis]*d2r);
                                            CameraMath::Matrix r{};
                                            r.m[0]=r.m[5]=r.m[10]=r.m[15]=1;
                                            int a=(axis+1)%3,b=(axis+2)%3;
                                            r.m[a*4+a]=c;r.m[a*4+b]=sn;r.m[b*4+a]=-sn;r.m[b*4+b]=c;
                                            off=CameraMath::Multiply(off,r);
                                        }
                                        for(int j=0;j<3;j++)off.m[12+j]=handTrim[j]*worldScale/100.f;
                                        target=CameraMath::Multiply(off,target);
                                    }
                                    bool ok=false;
                                    auto inv=AffineInverse(world,&ok);
                                    if(ok) {
                                        auto d=CameraMath::Multiply(inv,target);
                                        // THE PALETTE IS NEVER TOUCHED WITH A
                                        // TRANSFORM THAT COULD TEAR GEOMETRY.
                                        //
                                        // Mark, 2026-10-14: "the spike cannot be
                                        // there... if it can occur after a
                                        // calibration, then that's the wrong fix
                                        // for that problem. It should never
                                        // happen." He is right, and this is the
                                        // guard that makes it a property of the
                                        // code rather than of getting the
                                        // calibration right.
                                        //
                                        // The weld is supposed to make distance
                                        // irrelevant - a discarded bone gets the
                                        // kept wrist's FINAL matrix, so a vertex
                                        // weighted across the seam transforms
                                        // identically from both sides. A spike
                                        // means that guarantee broke somewhere.
                                        // Rather than guess where, this checks
                                        // the only thing that matters at the
                                        // output: is `d` a sane rigid move?
                                        //
                                        // THE FAILURE IS CORRECT BY DESIGN: when
                                        // it fires, nothing is written and the
                                        // game's own arm renders untouched. The
                                        // worst case becomes "no VR hand", never
                                        // "torn hand".
                                        bool sane=true;
                                        for(float v:d.m)if(!std::isfinite(v))sane=false;
                                        if(sane) {
                                            // Rigid: the 3x3 must still be a
                                            // rotation. Anything else scales or
                                            // shears the hand, which is exactly
                                            // what stretches a mesh.
                                            for(int r=0;r<3&&sane;r++)for(int c=r;c<3;c++) {
                                                float dot=0.f;
                                                for(int j=0;j<3;j++)dot+=d.m[r*4+j]*d.m[c*4+j];
                                                if(std::abs(dot-(r==c?1.f:0.f))>.02f){sane=false;break;}
                                            }
                                        }
                                        if(sane) {
                                            // HOW FAR THE HAND ACTUALLY MOVES:
                                            // the distance between the two world
                                            // positions, NOT the translation of
                                            // d.
                                            //
                                            // d = inverse(world) x target, and
                                            // for transforms sitting 48,000 units
                                            // from the world origin that
                                            // composition's translation is
                                            // dominated by the origin offsets. It
                                            // is not a displacement. Reading it
                                            // as inches reported 10,000 for a
                                            // hand that had moved 49 - and
                                            // refused every frame of a session,
                                            // which is how this guard came to
                                            // break the feature it was meant to
                                            // protect. The project's signature
                                            // mistake, committed inside the guard
                                            // against it.
                                            //
                                            // The limit is generous on purpose:
                                            // the job is catching a hand thrown
                                            // across the level, not policing a
                                            // long reach. 49 inches is normal -
                                            // Jensen's arm down, Mark's hand up.
                                            float moveSq=0.f;
                                            for(int j=0;j<3;j++) {
                                                float e=target.m[12+j]-world.m[12+j];
                                                moveSq+=e*e;
                                            }
                                            float lim=worldScale*120.f/39.3701f;   // 10 feet
                                            if(moveSq>lim*lim)sane=false;
                                        }
                                        if(!sane) {
                                            // WHICH SIDE IS WRONG. The move came out
                                            // at 6000 inches on 2026-10-14, which is
                                            // the distance from the world ORIGIN to
                                            // the player - so one of these two is at
                                            // the origin when it should be on Mark's
                                            // hand, and printing only the distance
                                            // cannot tell them apart. The palette
                                            // entry and the bind are printed too: a
                                            // near-zero palette would mean the model
                                            // is being drawn at the origin, which is
                                            // a different fault from a bad bind.
                                            if(handDriveRejected.fetch_add(1)<16) {
                                                FILE* sf{};
                                                if(!fopen_s(&sf,"DeusExHRVR-camera.log","a")) {
                                                    float mv=0.f;
                                                    for(int j=0;j<3;j++) {
                                                        float e=target.m[12+j]-world.m[12+j];
                                                        mv+=e*e;
                                                    }
                                                    fprintf(sf,"handDriveRejected moveIn=%.1f "
                                                        "world=%.0f,%.0f,%.0f target=%.0f,%.0f,%.0f "
                                                        "palette=%.0f,%.0f,%.0f bind=%.0f,%.0f,%.0f frame=%llu\n",
                                                        double(std::sqrt(mv)*39.3701f/worldScale),
                                                        double(world.m[12]),double(world.m[13]),double(world.m[14]),
                                                        double(target.m[12]),double(target.m[13]),double(target.m[14]),
                                                        double(m[drive].m[12]),double(m[drive].m[13]),double(m[drive].m[14]),
                                                        double(handBind[drive].m[12]),double(handBind[drive].m[13]),
                                                        double(handBind[drive].m[14]),frameId.load());
                                                    fclose(sf);
                                                }
                                            }
                                            ok=false;   // leave the palette exactly as the game built it
                                        }
                                    }
                                    if(ok) {
                                        auto d=CameraMath::Multiply(inv,target);
                                        // The gun's place in the hand, straight
                                        // from the animation. Accepted only when
                                        // it holds still across a real hand
                                        // movement, which a holstered gun cannot.
                                        // Keep measuring rather than latching: a
                                        // bad solve then self-corrects within a
                                        // second of ordinary play instead of
                                        // lasting the whole session.
                                        // A STORED GRIP IS FINAL. Measuring a
                                        // weapon whose answer is already known
                                        // can only replace a good value with a
                                        // worse one - which is exactly what the
                                        // pistol's 31.74 in did at the start of
                                        // three sessions. Unknown weapons still
                                        // solve themselves from the animation.
                                        // Delete DeusExHRVR-grips.cal to
                                        // re-measure everything.
                                        // OBSERVE ALWAYS, STORE ONLY WHEN UNKNOWN.
                                        // Measuring a weapon that already has a
                                        // grip cannot change anything now - the
                                        // result is logged and thrown away - so
                                        // this costs Mark nothing and nothing he
                                        // sees can move. It exists because the
                                        // seeded file means no weapon would ever
                                        // measure again, and the bad solve cannot
                                        // be explained without catching one.
                                        if(!controllerHandsModelAlignment.load() && handNativeMuzzleValid && handWeaponKey<0x18000
                                           && (FindHandGrip(handWeaponKey) || handGripCount<64)) {
                                            // A different weapon means a different
                                            // grip: start its measurement clean.
                                            if(handGripSolvingKey!=handWeaponKey) {
                                                handGripSolvingKey=handWeaponKey;
                                                handGripHavePrev=false;
                                                handGripSamples=0;
                                                handGripAccum=CameraMath::Matrix{};
                                                handGripWindowOpen=false;
                                            }
                                            auto cand=CameraMath::Multiply(handNativeMuzzle,inv);
                                            // THE HAND'S POSITION IN THE BODY'S
                                            // FRAME. Measuring it in world space
                                            // counts walking as hand movement,
                                            // and walking moves a body-mounted
                                            // weapon exactly as far as the wrist
                                            // - which is how 52 of 64 windows on
                                            // 2026-10-14 passed the test with the
                                            // gun nowhere near the hand.
                                            float body[3]{};
                                            const bool haveBody=handBodyOriginValid.load();
                                            if(haveBody)for(int j=0;j<3;j++)body[j]=handBodyOrigin[j].load();
                                            float rel[3];
                                            for(int j=0;j<3;j++)rel[j]=world.m[12+j]-body[j];
                                            float moved=0.f;
                                            for(int j=0;j<3;j++) {
                                                float e=rel[j]-handGripRef[j];
                                                moved+=e*e;
                                            }
                                            // THE REQUIREMENT THAT MAKES AGREEMENT
                                            // MEAN ANYTHING. Two stationary things
                                            // always agree, so consecutive frames
                                            // matching proves nothing on its own; it
                                            // is agreement ACROSS MOVEMENT that
                                            // separates "rigidly in the hand" from
                                            // "on the hip and neither moved".
                                            // Removing this on 2026-10-13 produced
                                            // 121 solves of one weapon with values
                                            // 31 inches apart; with it, four earlier
                                            // sessions solved every weapon once.
                                            float need=worldScale*.02f;   // ~0.8 inch
                                            if(moved>need*need) {
                                                bool agree=false;
                                                if(handGripHavePrev) {
                                                    float rot=0.f,pos=0.f;
                                                    for(int r=0;r<3;r++)for(int c=0;c<3;c++) {
                                                        float e=cand.m[r*4+c]-handGripPrev.m[r*4+c];
                                                        rot+=e*e;
                                                    }
                                                    for(int j=0;j<3;j++) {
                                                        float e=cand.m[12+j]-handGripPrev.m[12+j];
                                                        pos+=e*e;
                                                    }
                                                    float ptol=worldScale*.015f;
                                                    agree=rot<.02f && pos<ptol*ptol;
                                                }
                                                if(agree) {
                                                    // The window opens on the first
                                                    // accepted sample and records
                                                    // where both things started.
                                                    if(!handGripWindowOpen) {
                                                        handGripWindowOpen=true;
                                                        handGripWindowStart=frameId.load();
                                                        handGripWindowFlags=0;
                                                        handGripWindowMode=cameraModeIndex.load();
                                                        for(int j=0;j<3;j++) {
                                                            handGripWristStart[j]=world.m[12+j];
                                                            handGripMuzzleStart[j]=handNativeMuzzle.m[12+j];
                                                            handGripBodyStart[j]=body[j];
                                                        }
                                                    }
                                                    // Sticky: any suspension that was
                                                    // active at ANY point in the window.
                                                    if(handsSuspendedByMode.load())handGripWindowFlags|=1u;
                                                    if(handsFarFromBody.load())handGripWindowFlags|=2u;
                                                    if(!activeCameraIsPlayer.load())handGripWindowFlags|=4u;
                                                    for(int k=0;k<16;k++)handGripAccum.m[k]+=cand.m[k];
                                                    if(++handGripSamples>=8) {
                                                        auto avg=handGripAccum;
                                                        float f=1.f/float(handGripSamples);
                                                        for(int k=0;k<16;k++)avg.m[k]*=f;
                                                        Orthonormalize(avg);
                                                        auto* slot=FindHandGrip(handWeaponKey);
                                                        bool changed=true;
                                                        if(slot) {
                                                            float d=0.f;
                                                            for(int k=0;k<16;k++) {
                                                                float e=avg.m[k]-slot->m[k];
                                                                d+=e*e;
                                                            }
                                                            float tol=worldScale*.004f; // ~1/8 inch
                                                            changed=d>tol*tol;
                                                            // REFINEMENT, BOUNDED AGAINST THE
                                                            // ANCHOR. Mark recalibrated 39 times
                                                            // on 2026-10-14, and every
                                                            // recalibration changes what the
                                                            // correct grip is - so without this
                                                            // his guns stay wrong until the file
                                                            // is deleted. The first version of
                                                            // this bounded each step against the
                                                            // CURRENT value and a hundred small
                                                            // steps walked the launcher from
                                                            // 28.654 to 31.852. Measuring every
                                                            // step from the value this weapon had
                                                            // at load makes that impossible: the
                                                            // grip can move up to four inches
                                                            // from where it started and no
                                                            // further, ever.
                                                            {
                                                                auto* rec=FindHandGripRecord(handWeaponKey);
                                                                float live=0.f,anchor=0.f;
                                                                for(int j=0;j<3;j++) {
                                                                    live+=avg.m[12+j]*avg.m[12+j];
                                                                    if(rec)anchor+=rec->anchor.m[12+j]*rec->anchor.m[12+j];
                                                                }
                                                                float slack=worldScale*4.f/39.3701f;
                                                                bool plausible=
                                                                    std::abs(std::sqrt(live)-std::sqrt(anchor))<slack;
                                                                if(changed && plausible && rec) {
                                                                    rec->offset=avg;
                                                                    SaveHandGrips();
                                                                    FILE* rf{};
                                                                    if(!fopen_s(&rf,"DeusExHRVR-camera.log","a")) {
                                                                        fprintf(rf,"handGrip refined key=%08x anchor=%.3f now=%.3f inches\n",
                                                                            handWeaponKey,
                                                                            double(std::sqrt(anchor)*39.3701f/worldScale),
                                                                            double(std::sqrt(live)*39.3701f/worldScale));
                                                                        fclose(rf);
                                                                    }
                                                                }
                                                            }
                                                            // Earlier note kept for the record.
                                                            // Allowing a stored grip to be
                                                            // corrected let it RATCHET: each
                                                            // solve could move it up to four
                                                            // inches and a hundred small steps
                                                            // walked every weapon away from the
                                                            // truth - the launcher went
                                                            // 28.654 -> 31.852, which is the
                                                            // not-in-hand value. The bound
                                                            // guarded one bad jump and nothing
                                                            // guarded the walk. A stored grip
                                                            // is FINAL again; it is only
                                                            // observed and logged.
                                                        }
                                                        else {
                                                            handGrips[handGripCount].key=handWeaponKey;
                                                            handGrips[handGripCount].offset=avg;
                                                            handGrips[handGripCount].anchor=avg;
                                                            ++handGripCount;
                                                        }
                                                        // What the inputs were doing
                                                        // across the window. The
                                                        // decisive pair is wrist vs
                                                        // muzzle movement: a gun held
                                                        // in the hand moves with it.
                                                        {
                                                            float wm=0.f,mm=0.f,bm=0.f;
                                                            for(int j=0;j<3;j++) {
                                                                float a=world.m[12+j]-handGripWristStart[j];
                                                                float b=handNativeMuzzle.m[12+j]-handGripMuzzleStart[j];
                                                                float cb=body[j]-handGripBodyStart[j];
                                                                wm+=a*a;mm+=b*b;bm+=cb*cb;
                                                            }
                                                            float toIn=39.3701f/worldScale;
                                                            float got=0.f,had=0.f;
                                                            for(int j=0;j<3;j++) {
                                                                got+=avg.m[12+j]*avg.m[12+j];
                                                                if(slot)had+=slot->m[12+j]*slot->m[12+j];
                                                            }
                                                            if(!changed)++handGripAgreed;
                                                            if(changed && handGripLogged<64) {
                                                                ++handGripLogged;
                                                                FILE* df{};
                                                                if(!fopen_s(&df,"DeusExHRVR-camera.log","a")) {
                                                                    fprintf(df,"handGripWindow key=%08x %s inches=%.3f stored=%.3f "
                                                                        "frame=%llu span=%llu wristMoved=%.2f muzzleMoved=%.2f bodyMoved=%.2f "
                                                                        "suspendMode=%d distance=%d otherCam=%d mode=%s agreedSoFar=%u\n",
                                                                        handWeaponKey,slot?"OBSERVED":"STORED",
                                                                        double(sqrtf(got)*toIn),double(slot?sqrtf(had)*toIn:0.f),
                                                                        frameId.load(),frameId.load()-handGripWindowStart,
                                                                        double(sqrtf(wm)*toIn),double(sqrtf(mm)*toIn),double(sqrtf(bm)*toIn),
                                                                        int(handGripWindowFlags&1u),int((handGripWindowFlags>>1)&1u),
                                                                        int((handGripWindowFlags>>2)&1u),
                                                                        (handGripWindowMode>=0?cameraModes[handGripWindowMode].name:"unknown"),
                                                                        handGripAgreed);
                                                                    fclose(df);
                                                                }
                                                            }
                                                            handGripWindowOpen=false;
                                                        }
                                                        // The solve has completed:
                                                        // always reset. Only the LOG
                                                        // is conditional, which is
                                                        // what the throttle was for -
                                                        // `if(changed)` without braces
                                                        // governed one statement and
                                                        // threw both away.
                                                        handGripRemeasure=false;
                                                        handGripHavePrev=false;
                                                        handGripSamples=0;
                                                        handGripAccum=CameraMath::Matrix{};
                                                        // Only a newly STORED weapon
                                                        // changes the file. An
                                                        // observation never does.
                                                        if(!slot)SaveHandGrips();
                                                        FILE* gf{};
                                                        if(!slot && !fopen_s(&gf,"DeusExHRVR-camera.log","a")) {
                                                            fprintf(gf,"handGrip solved key=%08x weapons=%u Offset=",handWeaponKey,handGripCount);
                                                            for(int k=0;k<16;k++) {
                                                                float v=avg.m[k];
                                                                if(k==12||k==13||k==14)v/=worldScale;
                                                                fprintf(gf," %.9g",v);
                                                            }
                                                            fprintf(gf,"\n");
                                                            fclose(gf);
                                                        }
                                                    }
                                                }
                                                else {
                                                    handGripAccum=CameraMath::Matrix{};
                                                    handGripSamples=0;
                                                    handGripWindowOpen=false;
                                                }
                                                handGripPrev=cand;
                                                handGripHavePrev=true;
                                                for(int j=0;j<3;j++)handGripRef[j]=rel[j];
                                            }
                                        }
                                        auto handOnlyPlan=(controllerHandsMode==15 && drive==130 && n==223 &&
                                            HandDrawDiagnostic::hookStatus.load()==1)?HandOnlyGeometry::Prepare(model):nullptr;
                                        int lo=controllerHandsKeepFrom,hi=controllerHandsKeepTo;
                                        if(lo<0)lo=0;
                                        if(hi>int(n))hi=int(n);
                                        // The drive bone's matrix AFTER the move: the
                                        // weld copies this, so it has to be computed
                                        // before the loop overwrites anything.
                                        auto movedDrive=CameraMath::MoveSkinMatrix(m[drive],d);
                                        // THE APEX IS THE WRIST ITSELF, not a guess
                                        // at where the wrist is.
                                        //
                                        // Mark, 2026-10-14: "why not just collapse
                                        // at the wrist, like you planned?" No good
                                        // reason - it was built from handTarget,
                                        // the CONTROLLER's position, and then walked
                                        // back three inches to approximate the
                                        // wrist. That is two independently computed
                                        // quantities that are supposed to agree, and
                                        // the trim knobs exist only to paper over
                                        // the gap. Two quantities that must agree is
                                        // how a cone opens between them.
                                        //
                                        // movedDrive is the kept wrist AFTER the
                                        // move, and the drive bone is the one bone
                                        // whose bind pose is valid - that is what
                                        // this whole feature already rests on. So
                                        // its world transform is exact, and the
                                        // collapsed vertices land precisely on the
                                        // wrist they are supposed to hide behind.
                                        // One quantity, nothing to diverge.
                                        auto wristWorld=CameraMath::Multiply(handBind[drive],SkinAffine(movedDrive));
                                        // UNDO THE MUZZLE'S FORWARD PUSH, exactly as
                                        // handTarget does. wristWorld equals the
                                        // target, and the target is the MUZZLE frame
                                        // - whose origin sits ~10 inches ahead of the
                                        // visible hand, pushed along the hand's third
                                        // row by ControllerMuzzleForwardMetres. That
                                        // is dead-end #2 in this project's own notes,
                                        // and the first version of this change walked
                                        // straight into it: it put the apex 10 inches
                                        // off the hand while being described as
                                        // making it exact. The grip is what to use,
                                        // and this is the same arithmetic handTarget
                                        // uses - only derived from the hand's ACTUAL
                                        // final transform instead of from the
                                        // controller, which is the whole point.
                                        float apex[3];
                                        {
                                            const float forward=controllerMuzzleForward*worldScale;
                                            for(int j=0;j<3;j++)
                                                apex[j]=wristWorld.m[12+j]+wristWorld.m[4+j]*forward;
                                        }
                                        // The seam itself, rather than a guess
                                        // measured from the grip. Read before
                                        // the hide loop overwrites the palette.
                                        {
                                            int cb=controllerHandsCollapseBone;
                                            if(cb>=0 && uint32_t(cb)<n && uint32_t(cb)<handBindCount) {
                                                auto bw=CameraMath::Multiply(handBind[cb],SkinAffine(m[cb]));
                                                auto bm=CameraMath::Multiply(bw,d);
                                                for(int j=0;j<3;j++)apex[j]=bm.m[12+j];
                                            }
                                        }
                                        {
                                            // The trim rides the WRIST's own axes, not
                                            // the muzzle's. Offsetting along a second,
                                            // separately computed frame would undo the
                                            // whole point of the line above: whatever
                                            // these values are, the apex stays rigidly
                                            // attached to the wrist and cannot drift
                                            // away from it.
                                            const float inch=worldScale/39.37f;
                                            for(int j=0;j<3;j++)
                                                apex[j]+=wristWorld.m[4+j]*controllerHandsCollapseBackIn*inch
                                                        +wristWorld.m[j]*controllerHandsCollapseSideIn*inch
                                                        +wristWorld.m[8+j]*controllerHandsCollapseUpIn*inch;
                                        }
                                        // Work out the hide set before modifying
                                        // anything, because membership is decided by
                                        // comparing transforms and a modified slot
                                        // would no longer match.
                                        bool hide[512]{};
                                        uint32_t hidden=0;
                                        {
                                            int hlo=controllerHandsHideFrom,hhi=controllerHandsHideTo;
                                            if(hlo<0)hlo=0;
                                            if(hhi>int(n))hhi=int(n);
                                            if(hhi>hlo && hlo<512) {
                                                for(int i=hlo;i<hhi&&i<512;i++) {
                                                    // Never hide a kept bone.
                                                    if(i>=lo&&i<hi)continue;
                                                    hide[i]=true;++hidden;
                                                }
                                                // Slots duplicating a hidden transform -
                                                // 121 and 123 carry the left thumb and
                                                // index while sitting in the right arm's
                                                // range, and hardcoding them would miss
                                                // any others.
                                                for(uint32_t i=0;i<n&&i<512;i++) {
                                                    if(hide[i])continue;
                                                    if(int(i)>=lo&&int(i)<hi)continue;
                                                    for(int k=hlo;k<hhi&&k<512;k++) {
                                                        if(std::fabs(m[i].m[12]-m[k].m[12])<=.05f
                                                         &&std::fabs(m[i].m[13]-m[k].m[13])<=.05f
                                                         &&std::fabs(m[i].m[14]-m[k].m[14])<=.05f) {
                                                            hide[i]=true;++hidden;break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        handHiddenSlots.store(hidden);
                                        int wlo=controllerHandsWeldFrom,whi=controllerHandsWeldTo;
                                        if(wlo<0)wlo=0;
                                        if(whi>int(n))whi=int(n);
                                        for(uint32_t i=0;i<n;i++) {
                                            if(handOnlyPlan) {
                                                // All influences of selected right-hand vertices
                                                // receive the same rigid delta. No collapse or weld.
                                                if(handOnlyPlan->bones[i])m[i]=CameraMath::MoveSkinMatrix(m[i],d);
                                                continue;
                                            }
                                            // Hidden first, so a hidden slot inside the
                                            // weld or kept range still disappears.
                                            if(i<512&&hide[i]) {
                                                float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                                m[i]=CameraMath::Matrix{};
                                                for(int j=0;j<3;j++)m[i].m[12+j]=apex[j];
                                                m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                                                continue;
                                            }
                                            if(int(i)>=lo&&int(i)<hi){m[i]=CameraMath::MoveSkinMatrix(m[i],d);continue;}
                                            // 15 WELDS the forearm to the wrist
                                            // rather than collapsing it. A vertex
                                            // weighted across the wrist then gets
                                            // the same transform from both sides,
                                            // so there is nothing to drag and no
                                            // cone can form - which is the one
                                            // thing no collapse point can achieve.
                                            // The forearm stays full size and rides
                                            // the hand rigidly. Everything outside
                                            // both ranges is left animated, as in
                                            // mode 12, so the left arm is untouched.
                                            if(controllerHandsMode==15) {
                                                if(int(i)>=wlo&&int(i)<whi) {
                                                    float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                                    m[i]=movedDrive;
                                                    m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                                                }
                                                continue;
                                            }
                                            // 12 collapses nothing, so no
                                            // collapse artifact can disguise
                                            // what the placement is doing.
                                            if(controllerHandsMode==12)continue;
                                            float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                            // Collapse to the GRIP, which is the
                                            // controller's own hand position, NOT to
                                            // target. target is the MUZZLE, and
                                            // ControllerMuzzle pushes it forward from
                                            // the hand by ControllerMuzzleForwardMetres
                                            // (0.25 m, about 10 inches) along the hand's
                                            // third row, so after calibration the drive
                                            // bone's frame origin sits that far ahead of
                                            // the visible hand. Collapsing there hauled
                                            // every vertex shared across the wrist out
                                            // past the fingertips - a spike pointing at
                                            // the fingers, unaffected by the scale
                                            // because this point does not depend on it.
                                            float q[3]{apex[0],apex[1],apex[2]};
                                            // Shrink every discarded vertex TOWARD the
                                            // collapse point: v' = s*v + (1-s)*q, which
                                            // expands to new 3x3 = s*palette3x3 and
                                            // new t = s*p_t + (1-s)*q. It needs only the
                                            // palette's own translation.
                                            // NOT the bone's "own world position" from
                                            // handBind: those bind poses were solved
                                            // against one instant, so every bone's solved
                                            // world transform COINCIDES at that instant.
                                            // They measure a bone's motion since
                                            // calibration - which is what mode 14 used
                                            // them for - and are not world positions. A
                                            // barely-moved bone evaluates to wherever
                                            // Mark stood while calibrating, so pivoting
                                            // on it throws the nub s * that distance.
                                            float pt[3]{m[i].m[12],m[i].m[13],m[i].m[14]};
                                            float sc=controllerHandsCollapseScale;
                                            if(sc>0.f) {
                                                // A zeroed 3x3 leaves a shared vertex no
                                                // usable normal at all; a scaled one
                                                // keeps its direction and only shortens
                                                // it, so the geometry shrinks to a nub
                                                // instead of losing its lighting.
                                                for(int r=0;r<3;r++)for(int c=0;c<3;c++)
                                                    m[i].m[r*4+c]*=sc;
                                            }
                                            else {m[i]=CameraMath::Matrix{};sc=0.f;}
                                            for(int j=0;j<3;j++)
                                                m[i].m[12+j]=sc*pt[j]+(1.f-sc)*q[j];
                                            m[i].m[14]-=controllerHandsCollapseDrop*worldScale;
                                            m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                                        }
                                        if(handOnlyPlan)HandOnlyGeometry::Activate(uintptr_t(data),frameId.load(),handOnlyPlan,m[130].m);
                                    }
                                }
                            }
                        }
                        else if(controllerHandsMode>=8 && controllerHandsMode<12 && handDeltaValid) {
                            int lo=controllerHandsKeepFrom,hi=controllerHandsKeepTo;
                            if(lo<0)lo=0;
                            if(hi>int(n))hi=int(n);
                            for(uint32_t i=0;i<n;i++) {
                                bool keep=controllerHandsMode==8||(int(i)>=lo&&int(i)<hi);
                                if(keep){m[i]=CameraMath::MoveSkinMatrix(m[i],handDelta);continue;}
                                // 11 collapses nothing, so no collapse artifact can
                                // hide what the band actually owns.
                                if(controllerHandsMode==11)continue;
                                // Collapse to the grip, keeping the fourth column
                                // exactly as the engine wrote it. Zeroing it as well
                                // left the shader a zero w, which divides to
                                // infinity - the monstrosity Mark saw in mode 9.
                                float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                m[i]=CameraMath::Matrix{};
                                for(int j=0;j<3;j++)m[i].m[12+j]=handTarget[j];
                                // The game is Z-up, so this drops them straight down.
                                m[i].m[14]-=controllerHandsCollapseDrop*worldScale;
                                m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                            }
                        }
                        else if(controllerHandsMode==7) {
                            // Collapse to the first kept bone's translation. That
                            // value is meaningless as a position, but it does not
                            // need to be meaningful - everything discarded just has
                            // to land on ONE point so its triangles have no area.
                            int lo=controllerHandsKeepFrom,hi=controllerHandsKeepTo;
                            if(lo<0)lo=0;
                            if(hi>int(n))hi=int(n);
                            if(lo<hi) {
                                const float ax=m[lo].m[12],ay=m[lo].m[13],az=m[lo].m[14];
                                for(uint32_t i=0;i<n;i++) {
                                    if(int(i)>=lo&&int(i)<hi)continue;
                                    float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                    m[i]=CameraMath::Matrix{};
                                    m[i].m[12]=ax;m[i].m[13]=ay;m[i].m[14]=az;
                                    m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                                }
                            }
                        }
                        else if(controllerHandsMode==6) {
                            // Index bisection. Nothing here depends on a bone's
                            // translation meaning anything, which is why it works
                            // where the proximity modes could not.
                            const float d=controllerHandsRadius*worldScale/100.f;
                            for(uint32_t i=0;i<n;i++) {
                                float dz=0;
                                if(int(i)<controllerHandsSplitA)dz=d;
                                else if(int(i)<controllerHandsSplitB)dz=-d;
                                else continue;
                                CameraMath::Matrix move{};
                                move.m[0]=move.m[5]=move.m[10]=move.m[15]=1;
                                move.m[14]=dz;
                                m[i]=CameraMath::MoveSkinMatrix(m[i],move);
                            }
                        }
                        else if(controllerHandsMode>=4 && first>=0) {
                            // Anchor on the bone that marks the hand, keep whatever
                            // geometry sits near it, collapse everything else to it.
                            const float r=controllerHandsRadius*worldScale/100.f,r2=r*r;
                            const float ax=m[first].m[12],ay=m[first].m[13],az=m[first].m[14];
                            float dx=0,dy=0,dz=0;
                            if(controllerHandsMode==5) {
                                dx=handTarget[0]-ax;dy=handTarget[1]-ay;dz=handTarget[2]-az;
                            }
                            for(uint32_t i=0;i<n;i++) {
                                float ex=m[i].m[12]-ax,ey=m[i].m[13]-ay,ez=m[i].m[14]-az;
                                if(ex*ex+ey*ey+ez*ez<=r2) {
                                    if(controllerHandsMode==5) {
                                        CameraMath::Matrix move{};
                                        move.m[0]=move.m[5]=move.m[10]=move.m[15]=1;
                                        move.m[12]=dx;move.m[13]=dy;move.m[14]=dz;
                                        m[i]=CameraMath::MoveSkinMatrix(m[i],move);
                                    }
                                    continue;
                                }
                                float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                                m[i]=CameraMath::Matrix{};
                                m[i].m[12]=ax+dx;m[i].m[13]=ay+dy;m[i].m[14]=az+dz;
                                m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                            }
                        }
                        else if(controllerHandsMode==3) {
                            // Rigid translation of the whole palette: shape kept,
                            // so any geometry this palette skins visibly shifts.
                            int anchor=first>=0?first:0;
                            CameraMath::Matrix move{};
                            move.m[0]=move.m[5]=move.m[10]=move.m[15]=1;
                            for(int j=0;j<3;j++)move.m[12+j]=handTarget[j]-m[anchor].m[12+j];
                            for(uint32_t i=0;i<n;i++)m[i]=CameraMath::MoveSkinMatrix(m[i],move);
                        }
                        else if(controllerHandsMode>=12 && controllerHandsMode<=15) {
                            // The calibrated modes NEVER fall through to the
                            // branches below. The first-person gate shutting
                            // must leave the palette untouched so the arm
                            // animates normally - falling through to the
                            // pre-calibration collapse (keyed on bone 158, which
                            // skins nothing) erased Jensen entirely, which is the
                            // same failure as 2026-10-12.
                        }
                        else if(first>=0 && controllerHandsMode!=1) {
                            // Pure translation: the animation's own orientation is
                            // kept. Nothing here knows the bone's rotation
                            // convention, and a wrong rotation looks far worse than
                            // an animated one.
                            CameraMath::Matrix move{};
                            move.m[0]=move.m[5]=move.m[10]=move.m[15]=1;
                            for(int j=0;j<3;j++)move.m[12+j]=handTarget[j]-m[first].m[12+j];
                            m[first]=CameraMath::MoveSkinMatrix(m[first],move);
                        }
                        // Collapse everything else. A zeroed 3x3 sends every vertex
                        // of a bone to that bone's translation, so its triangles
                        // have no area. The target is the NEAREST KEPT hand's final
                        // position rather than a point far below the floor: a vertex
                        // weighted partly to the wrist then lands beside the hand
                        // instead of stretching a spike across the level.
                        if(controllerHandsMode<2 && (first>=0 || second>=0))for(uint32_t i=0;i<n;i++) {
                            if(int(i)==first || int(i)==second)continue;
                            const CameraMath::Matrix* to=&m[first>=0?first:second];
                            if(first>=0 && second>=0) {
                                float da=0,db=0;
                                for(int j=0;j<3;j++) {
                                    float a=m[i].m[12+j]-m[first].m[12+j];
                                    float b=m[i].m[12+j]-m[second].m[12+j];
                                    da+=a*a;db+=b*b;
                                }
                                to=db<da?&m[second]:&m[first];
                            }
                            // Keep the fourth column exactly as the engine wrote it;
                            // MoveSkinMatrix preserves the same four slots.
                            float k3=m[i].m[3],k7=m[i].m[7],k11=m[i].m[11],k15=m[i].m[15];
                            float t0=to->m[12],t1=to->m[13],t2=to->m[14];
                            m[i]=CameraMath::Matrix{};
                            m[i].m[12]=t0;m[i].m[13]=t1;m[i].m[14]=t2;
                            m[i].m[3]=k3;m[i].m[7]=k7;m[i].m[11]=k11;m[i].m[15]=k15;
                        }
                    }
                }
            }
        }
        if(!ok)++handSkeletonMiss;
    }
    if(!controllerDrawing || !state)return;
    // Verified native PCDX11MatrixState: poseData is +0x10, count at +0,
    // followed by aligned world-space matrices at +0x10. This is a render
    // allocation, not the simulation's animation/bone buffer.
    auto s=static_cast<unsigned char*>(state);
    if(*reinterpret_cast<uintptr_t*>(s)!=base+0xa97524-0x400000)return;
    auto data=*reinterpret_cast<unsigned char**>(s+0x10);if(!data)return;
    auto n=*reinterpret_cast<uint32_t*>(data);if(!n || n>512 || n!=count)return;
    auto matrices=reinterpret_cast<CameraMath::Matrix*>(data+0x10);
    bool capture=HandWeaponMotionDiagnostic::Wanted(1,frameId.load());
    HandWeaponMotionDiagnostic::Sample motion{};
    if(capture){MotionContext(motion,motionDrawPose,1);motion.delta=controllerDelta;motion.count=n;
        for(unsigned i=0;i<n && i<16;i++)motion.before[i]=matrices[i];}
    for(uint32_t i=0;i<n;i++)matrices[i]=CameraMath::MoveSkinMatrix(matrices[i],controllerDelta);
    if(capture){for(unsigned i=0;i<n && i<16;i++)motion.after[i]=matrices[i];HandWeaponMotionDiagnostic::Push(motion);}
    ++controllerDraws;
}
void __fastcall WeaponDrawHook(void* self,void*,void* matrix,void* args) {
    auto previous=controllerDrawing;auto savedDelta=controllerDelta;controllerDrawing=false;
    auto savedMotionPose=motionDrawPose;
    if(experimentalMotionControls) {
        auto drawable=static_cast<unsigned char*>(self);
        auto pose=ReadWeaponPose(true,MuzzleQueryDiagnostic::Draw,*reinterpret_cast<void**>(drawable+8));
        if(pose.active && *reinterpret_cast<void**>(drawable+8)==pose.instance) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                controllerDelta=pose.nativeValid?pose.delta:CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                controllerDrawing=true;
                motionDrawPose=pose;
            }
        }
    }
    originalWeaponDraw(self,matrix,args);
    controllerDrawing=previous;controllerDelta=savedDelta;
    motionDrawPose=savedMotionPose;
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
ShaderSwap shaderSwap; // Luma fix port: hash-keyed native shader swap (sidesteps ReShade)
LumaPasses lumaPasses; // Luma fix port: per-eye injected passes (XeGTAO/SMAA/ModulateLighting)
LumaSettingsCB::Manager lumaSettingsCB; // Luma fix port: LumaSettings cbuffer (b13)
uint64_t instanceCorrections{};
void __fastcall RenderStateHook(void* self,void*) {
    lumaPasses.RestoreDrawOverrides();
    shaderSwap.RestoreDrawOverrides();
    auto state=static_cast<unsigned char*>(self);
    {
        // Does a per-draw hook reach inside the actor's draw at all, and is its
        // pixel shader its own? PrimitiveHook never fired in there, which is
        // what makes this worth measuring rather than assuming either way.
        auto boundPS=*reinterpret_cast<uintptr_t*>(state+0x198);
        if(handDrawing) {
            handShaderCalls.fetch_add(1);
            bool known=false;
            for(uint32_t i=0;i<handShaderCount;i++)if(handShaders[i]==boundPS){known=true;break;}
            if(!known && handShaderCount<16)handShaders[handShaderCount++]=boundPS;
        }
        else if(otherShaderCount<512) {
            bool known=false;
            for(uint32_t i=0;i<otherShaderCount;i++)if(otherShaders[i]==boundPS){known=true;break;}
            if(!known)otherShaders[otherShaderCount++]=boundPS;
        }
    }
    float* instance{};CameraMath::Matrix saved;
    auto shader=effectShaders.Identify(*reinterpret_cast<uintptr_t*>(state+0x198));
    // Luma port: identify the bound pixel shader in ReShade's hash space so
    // the swap table can report (and later replace) Luma-targeted passes.
    // NoteBound returns the hash (0 if unreadable/not in table) for Phase 2's
    // TrySubstitutePS call below.
    uint32_t lumaHash=0;
    if(shaderSwap.Active())
        lumaHash=shaderSwap.NoteBound(static_cast<uint32_t>(frameId.load()),
                             ShaderSwap::Stage::Pixel,
                             *reinterpret_cast<uintptr_t*>(state+0x198));
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
    if(effectsCapture && shaderSwap.Active())
        shaderSwap.CaptureDraw(static_cast<uint32_t>(frameId.load()), lumaHash, state[0x5ea]?0:1);
    // Luma port: the engine has just bound its pixel shader. If substitution
    // is enabled (F12 or [Luma] SubstituteShaders=1) and this draw's PS has a
    // Luma replacement, override it now so the imminent draw uses the fix.
    // Runs per-draw on the render thread; TrySubstitutePS is a no-op when
    // disabled or when no replacement exists for this hash.
    if(lumaHash && shaderSwap.SubstitutionEnabled())
        shaderSwap.TrySubstitutePS(lumaHash,
            EffectShader::UsesCentreViewMatrix(shader));
    // Luma port Phase 3: injected passes (XeGTAO/SMAA/ModulateLighting).
    // Evaluate triggers against the bound PS hash; OnDraw is a no-op unless a
    // trigger matches and the pass is enabled. Per-eye is automatic: the hook
    // fires per draw, and the engine renders each eye separately.
    if(lumaPasses.Loaded())
        lumaPasses.OnDraw(static_cast<uint32_t>(frameId.load()), lumaHash,
                          state[0x5ea]?0:1);
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
    if((movementDirection!=DirectionConfig::Source::Mouse || movementResponse.load()) && (!walkAction.load() || !strafeAction.load())) {
        using FindAction=void*(__cdecl*)(const char*);
        auto find=reinterpret_cast<FindAction>(base+0x4ae340-0x400000);
        walkAction=find("locomotionwalk");strafeAction=find("locomotionstrafe");
    }
    {
        std::lock_guard lock(stateMutex);
        bool calibrationUpdated=false;
        if(recenterRequested || !requested || screenReasons || gameMenuOpen)calibration.Cancel();
        current.active=false;
        current.nativeHeading=false;
        current.leveled=false;
        current.inputIndex=~0u;
        weaponPose={};
        auto manager=static_cast<unsigned char*>(self);
        auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
        const bool nativeScope=ScreenMode::Scope(base,manager);
        Transport::Tracking scopeTracking{};
        const auto now=GetTickCount64();
        bool scopeWeapon=false;
        if(active==manager+0x6f0) {
            auto entity=*reinterpret_cast<void**>(active+0xaa0);
            using Equipped=unsigned char*(__cdecl*)(void*);
            auto holder=entity?reinterpret_cast<Equipped>(base+0x66af40-0x400000)(entity):nullptr;
            auto weapon=holder?*reinterpret_cast<unsigned char**>(holder+0x14):nullptr;
            auto data=weapon?*reinterpret_cast<unsigned char**>(weapon+0x7c):nullptr;
            scopeWeapon=data && data[0x425]!=0;
        }
        const bool scopeAllowed=immersiveScope && motionControls && experimentalMotionControls && requested &&
            scopeWeapon && !(screenReasons&~16u) && !gameMenuOpen && trackingReader.Read(channel,scopeTracking,now) && scopeTracking.rightController.valid;
        const bool calibrating=scopeTracking.controllerButtons==3 || calibration.Busy();
        if(calibrating)scopePulseUntil=0;
        const bool nearEye=scopeGesture.Update(scopeTracking,scopeAllowed&&!calibrating,now);
        if(scopeAllowed && !calibrating && now>=scopeToggleAfter && ((nearEye&&!nativeScope)||(!nearEye&&nativeScope&&autoScopeOwned))) {
            scopePulseUntil=now+120;scopeToggleAfter=now+650;
            autoScopeOwned=nearEye;
        }
        if(!scopeWeapon){autoScopeOwned=false;scopePulseUntil=0;}
        immersiveScopeActive=scopeAllowed && nativeScope;
        scopeScreen=nativeScope&&!immersiveScopeActive;
        // Supported build: PlayerCamera embeds CameraMode_Hacking at +0x430.
        // Its enter/leave methods (0x6a1c50 / 0x6a1dc0) set/clear +0x104.
        // Check the exact class before reading its active flag.
        auto hacking=manager+0x6f0+0x430;
        bool screen=*reinterpret_cast<uintptr_t*>(hacking)==base+0xaa774c-0x400000 && hacking[0x104]!=0;
        // THE GAME'S OWN ANSWER to "where is the camera". PlayerCamera keeps the
        // current mode in its blend stack at +0x770 (RTTI:
        // CameraOperation_MultiBlend<CameraOperation_CrossBlend>): a 16-bit index
        // at +0x774, then 19 entries of 0x24 bytes from +0x778, each holding its
        // CameraMode pointer at +0x08. Read at 0x6AA9F0, which fetches that exact
        // pointer to compare against a requested mode.
        {
            auto pc=manager+0x6f0;
            int found=-1;
            uint32_t idx=*reinterpret_cast<const uint16_t*>(pc+0x774);
            const unsigned char* modeObj=nullptr;
            if(idx<19) {
                modeObj=*reinterpret_cast<const unsigned char* const*>(pc+0x778+0x24*idx+8);
                // A mode is named only when the POINTER and the VTABLE agree.
                // Two independent values from the same analysis; one alone could
                // be a coincidence, both cannot.
                if(modeObj)for(int i=0;i<9;i++) {
                    if(modeObj!=pc+cameraModes[i].offset)continue;
                    if(*reinterpret_cast<const uintptr_t*>(modeObj)!=
                       base+cameraModes[i].vtable-0x400000)break;
                    found=i;break;
                }
            }
            cameraModeIndex.store(found);
            if(found!=cameraModeLast) {
                cameraModeLast=found;
                FILE* f{};
                if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                    if(found>=0)
                        fprintf(f,"cameraMode=%s index=%d suspendHands=%d frame=%llu\n",
                            cameraModes[found].name,found,
                            int((controllerHandsSuspendModes>>found)&1u),frameId.load());
                    else fprintf(f,"cameraMode=unknown frame=%llu\n",frameId.load());
                    fclose(f);
                }
            }
            // Never silent: if the layout is wrong this says so, a bounded number
            // of times, with the numbers needed to find out why.
            if(found<0 && cameraModeUnknownLogged<8) {
                ++cameraModeUnknownLogged;
                FILE* f{};
                if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                    fprintf(f,"cameraModeUnknown idx=%u mode=%p pcHacking=%d frame=%llu\n",
                        idx,static_cast<const void*>(modeObj),int(screen),frameId.load());
                    fclose(f);
                }
            }
            handsSuspendedByMode.store(found>=0 && ((controllerHandsSuspendModes>>found)&1u)!=0);
            // WHICH CAMERA, not just which mode. The Icarus landing left the
            // mode at Combat and still moved the camera off the body for ~10 s
            // (frames 12778-13419 of the 2026-10-13 evening session), because it
            // renders from a different camera OBJECT. If that object is not the
            // player's camera, the view is not at his eyes - no threshold
            // involved. A null active camera changes nothing.
            activeCameraIsPlayer.store(!active || active==pc);
            if(active!=activeCameraLast) {
                activeCameraLast=active;
                const char* name="unknown";
                for(int i=0;i<9;i++)
                    if(active==manager+cameraSlots[i].offset){name=cameraSlots[i].name;break;}
                FILE* f{};
                if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                    fprintf(f,"activeCamera=%s offset=%d vtable=%08x frame=%llu\n",
                        name,active?int(active-manager):-1,
                        active?unsigned(*reinterpret_cast<uintptr_t*>(active)-base+0x400000):0u,
                        frameId.load());
                    fclose(f);
                }
            }
        }
        if(screen!=interactionScreen) {
            interactionScreen=screen;
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"interactionScreen=%d requested=%d frame=%llu\n",screen,requested,frameId.load());fclose(f);
            }
        }
        const bool dialogue=conversationCamera.load()!=nullptr;
        if(dialogue!=dialogueCamera) {
            dialogueCamera=dialogue;if(!dialogue)dialogueEndTick=now;
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"dialogueCamera=%d frame=%llu\n",dialogue,frameId.load());fclose(f);
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
            auto renderBase=RenderBase(current.originalWorld);
            if(nativeLookHeading && yawOnlyCamera && motionControls && t.rightController.valid &&
               !gameMenuOpen && !wheelOpen && !dialogue && !immersiveScopeActive &&
               active==manager+0x6f0) {
                auto instance=*reinterpret_cast<void**>(active+0xaa0);
                const auto engineFrame=*reinterpret_cast<const uint32_t*>(base+0x1c7bfc8-0x400000);
                if(NativeCombatCamera(instance,reinterpret_cast<uintptr_t>(manager)) &&
                   nativeHeadingSample.Matches(reinterpret_cast<uintptr_t>(manager),
                       reinterpret_cast<uintptr_t>(instance),engineFrame)) {
                    auto clean=NativeHeading::Base(nativeHeadingSample.yaw,renderBase);
                    const float delta=std::abs(CameraMath::HeadingDelta(renderBase,clean))*57.2957795f;
                    bool reject=false;
                    if(nativeHeadingMaxDeg>0.f) {
                        if(!std::isfinite(delta))reject=true;
                        else if(nativeHeadingLatched) {
                            // Hold out through the camera's return sweep.
                            if(delta<=nativeHeadingCalmDeg) {
                                if(!nativeHeadingCalmSince)nativeHeadingCalmSince=now;
                                if(now-nativeHeadingCalmSince>=nativeHeadingCalmMs) {
                                    nativeHeadingLatched=false;nativeHeadingCalmSince=0;
                                }
                            } else nativeHeadingCalmSince=0;
                            reject=nativeHeadingLatched;
                        } else if(delta>nativeHeadingMaxDeg) {
                            nativeHeadingLatched=true;nativeHeadingCalmSince=0;reject=true;
                        }
                    }
                    if(reject) {
                        // The camera is somewhere the aim does not explain: leave
                        // it alone. NativeCameraPosition keys off
                        // current.nativeHeading, so it stands down with us.
                        if(std::isfinite(delta))
                            nativeHeadingRejectedPeak=std::max(nativeHeadingRejectedPeak,delta);
                        ++nativeHeadingRejected;
                    } else {
                        nativeHeadingPeakDelta=std::max(nativeHeadingPeakDelta,delta);
                        renderBase=clean;current.nativeHeading=true;++nativeHeadingUsed;
                    }
                } else ++nativeHeadingFallback;
                if(now-nativeHeadingLogTick>=2000) {
                    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                        fprintf(f,"nativeLookHeading used=%llu fallback=%llu active=%d engineFrame=%u sampleFrame=%u maxRemovedDeg=%g rejected=%llu maxRejectedDeg=%g limitDeg=%g latched=%d\n",
                            nativeHeadingUsed,nativeHeadingFallback,int(current.nativeHeading),engineFrame,
                            nativeHeadingSample.engineFrame,nativeHeadingPeakDelta,
                            nativeHeadingRejected,nativeHeadingRejectedPeak,nativeHeadingMaxDeg,
                            int(nativeHeadingLatched));fclose(f);
                    }
                    nativeHeadingLogTick=now;nativeHeadingPeakDelta=0;nativeHeadingRejectedPeak=0;
                }
            }
            // Do not feed an intentional input heading through animation filters.
            // Drop old history when changing source, including returning from cover.
            if(current.nativeHeading!=nativeHeadingWasUsed)yawSwing.Reset();
            nativeHeadingWasUsed=current.nativeHeading;
            // Other camera modes skip the filter; its jump/gap checks reset it when gameplay resumes.
            if(active==manager+0x6f0) {
                double ms=PreciseMs();
                // Sprinting = LB reaching the game. README line 92: "Left grip |
                // LB / sprint". Mark's layout sends it from the left stick click
                // (LeftStickClick=LB), but reading the PAD bit rather than the
                // physical click means any binding works and nothing breaks
                // silently if he moves it.
                if(controllerHands)
                    handPoseSprinting.store(t.gamepad.valid &&
                        (uint32_t(t.gamepad.buttons)&controllerHandsSprintMask)!=0);
                // Publish the body origin for the grip test on the render thread.
                if(auto pb=*reinterpret_cast<const unsigned char* const*>(active+0xaa0)) {
                    float bx=*reinterpret_cast<const float*>(pb+0x20);
                    float by=*reinterpret_cast<const float*>(pb+0x24);
                    float bz=*reinterpret_cast<const float*>(pb+0x28);
                    bool ok=std::isfinite(bx)&&std::isfinite(by)&&std::isfinite(bz);
                    if(ok) {
                        handBodyOrigin[0].store(bx);
                        handBodyOrigin[1].store(by);
                        handBodyOrigin[2].store(bz);
                    }
                    handBodyOriginValid.store(ok);
                }
                else handBodyOriginValid.store(false);
                if(stanceHold.enabled) {
                    // Player entity origin: three floats at +0x20 (measured on the supported
                    // build; the camera's X/Y match it exactly, its Z sits an eye height below).
                    auto entity=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                    float entZ=entity?*reinterpret_cast<const float*>(entity+0x28):0.f;
                    float eye=entity?renderBase.m[14]-entZ:0.f;
                    if(entity && std::isfinite(eye) && eye>1.f && eye<900.f)
                        renderBase.m[14]=entZ+stanceHold.Apply(eye,ms);
                    else stanceHold.Reset(); // no player (menus, cutscenes): leave the camera alone
                }
                if(swayHold.windowMs>0 || nativeCameraPosition) {
                    auto entity=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                    float entX=entity?*reinterpret_cast<const float*>(entity+0x20):0.f;
                    float entY=entity?*reinterpret_cast<const float*>(entity+0x24):0.f;
                    bool anchored=false;
                    if(nativeCameraPosition && current.nativeHeading && entity) {
                        const float distance=std::hypot(renderBase.m[12]-entX,renderBase.m[13]-entY);
                        anchored=CameraPosition::UseBodyXY(renderBase,entX,entY);
                        if(anchored) {
                            ++nativePositionUsed;
                            nativePositionPeak=std::max(nativePositionPeak,distance);
                            const bool sprint=t.gamepad.valid && (uint32_t(t.gamepad.buttons)&controllerHandsSprintMask)!=0;
                            if(sprint)nativePositionSprintPeak=std::max(nativePositionSprintPeak,distance);
                        } else ++nativePositionFallback;
                    }
                    if(anchored)swayHold.Reset();
                    else if(swayHold.windowMs>0 && entity && std::isfinite(entX) && std::isfinite(entY))
                        swayHold.Apply(renderBase.m[12],renderBase.m[13],entX,entY,ms);
                    else swayHold.Reset();
                    if(nativeCameraPosition && now-nativePositionLogTick>=2000) {
                        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                            fprintf(f,"nativeCameraPosition used=%llu rejected=%llu active=%d maxOffsetIn=%g sprintMaxOffsetIn=%g\n",
                                nativePositionUsed,nativePositionFallback,int(anchored),
                                nativePositionPeak*39.37f/worldScale,nativePositionSprintPeak*39.37f/worldScale);
                            fclose(f);
                        }
                        nativePositionLogTick=now;nativePositionPeak=nativePositionSprintPeak=0;
                    }
                }
                if(controllerHands) {
                    // Interact held, from the pad state the companion already
                    // sends. Reading it changes nothing - the button reaches the
                    // game exactly as before.
                    handPoseInteract.store((t.gamepad.valid &&
                        (uint32_t(t.gamepad.buttons)&controllerHandsPoseMask))!=0);
                }
                if(controllerHands) {
                    // First person only. In normal gameplay the camera's X/Y
                    // match the entity origin to 0.2 units (2026-09-17 trace);
                    // in cover the 2026-09-21 trace caught it 1.5 m away.
                    auto pe=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                    bool allow=false;
                    if(pe) {
                        float ex=*reinterpret_cast<const float*>(pe+0x20);
                        float ey=*reinterpret_cast<const float*>(pe+0x24);
                        if(std::isfinite(ex)&&std::isfinite(ey)) {
                            float dx=renderBase.m[12]-ex,dy=renderBase.m[13]-ey;
                            float lim=controllerHandsMaxOffsetIn*worldScale/39.37f;
                            allow=dx*dx+dy*dy<=lim*lim;
                        }
                    }
                    // A BACKSTOP, not the old gate. `far` is true only on
                    // positive evidence: a live entity pointer, finite
                    // coordinates, and a distance past the limit. A null pointer
                    // or a bad number leaves it false and the hands running -
                    // the opposite of the 2026-10-13 gate, which defaulted to
                    // OFF and disabled the feature whenever the pointer was
                    // missing. ControllerHandsMaxOffsetIn=0 turns it back into a
                    // log and nothing else.
                    // NOT `far`: windef.h defines that away as a legacy
                    // segment keyword, so `bool far=x` compiles to `bool =x`.
                    bool awayFromBody=false;
                    if(pe && controllerHandsMaxOffsetIn>0.f) {
                        float ex=*reinterpret_cast<const float*>(pe+0x20);
                        float ey=*reinterpret_cast<const float*>(pe+0x24);
                        if(std::isfinite(ex)&&std::isfinite(ey)) {
                            float dx=renderBase.m[12]-ex,dy=renderBase.m[13]-ey;
                            float lim=controllerHandsMaxOffsetIn*worldScale/39.37f;
                            awayFromBody=dx*dx+dy*dy>lim*lim;
                        }
                    }
                    handsFarFromBody.store(awayFromBody);
                    if(awayFromBody!=handsFarLast) {
                        handsFarLast=awayFromBody;
                        // Never silent, and it says what the OTHER two
                        // instruments thought at the same moment - which is the
                        // whole question for the Icarus landing.
                        int md=cameraModeIndex.load();
                        FILE* f{};
                        if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                            fprintf(f,"handsDistance=%d mode=%s activeCameraIsPlayer=%d frame=%llu\n",
                                int(awayFromBody),(md>=0?cameraModes[md].name:"unknown"),
                                int(activeCameraIsPlayer.load()),frameId.load());
                            fclose(f);
                        }
                    }
                    handDriveAllowed.store(allow);
                    if(allow!=handDriveAllowedLast) {
                        handDriveAllowedLast=allow;
                        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                            fprintf(f,"handsFirstPerson=%d frame=%llu\n",int(allow),frameId.load());fclose(f);
                        }
                    }
                }
                if(!current.nativeHeading && yawOnlyCamera && (yawSwing.a.enabled||yawSwing.b.enabled)) {
                    // renderBase is pure heading here: rebuild it from the filtered yaw.
                    double yaw=std::atan2(renderBase.m[9],renderBase.m[8])*180/3.14159265358979;
                    double f=yawSwing.Apply(yaw,ms)*3.14159265358979/180;
                    float x=float(std::cos(f)),y=float(std::sin(f));
                    renderBase.m[0]=y;renderBase.m[1]=-x;renderBase.m[2]=0;
                    renderBase.m[8]=x;renderBase.m[9]=y;renderBase.m[10]=0;
                }
                if(bobTrace)BobTrace(ms,current.originalWorld,renderBase.m+12,t);
                if(fixedDialogueCamera) {
                    const bool held=holdingPlayerBase;
                    // Hold only straight out of gameplay, never a pose from before a load.
                    if(dialogueCamera && now-lastPlayerBaseTick<1000)holdingPlayerBase=true;
                    // Afterwards, keep holding until the game camera is back at the held
                    // pose (at most 2 s), so its blend back from the last shot is not shown.
                    if(holdingPlayerBase && !dialogueCamera) {
                        float dx=renderBase.m[12]-lastPlayerBase.m[12];
                        float dy=renderBase.m[13]-lastPlayerBase.m[13];
                        float dz=renderBase.m[14]-lastPlayerBase.m[14];
                        float lim=.1f*worldScale;
                        if(dx*dx+dy*dy+dz*dz<lim*lim || now-dialogueEndTick>2000)holdingPlayerBase=false;
                    }
                    if(holdingPlayerBase)renderBase=lastPlayerBase;
                    else {lastPlayerBase=renderBase;lastPlayerBaseTick=now;}
                    if(held!=holdingPlayerBase) {
                        // Mark was put at FLOOR HEIGHT in two conversations
                        // (2026-10-18). Cause unknown; StanceHold was checked
                        // and ruled out (it returns the true eye height on
                        // reset, never 0). Record enough to tell next time
                        // whether the hold engaged, what height it froze, and
                        // how far that sits above the player's feet: a held
                        // eye of ~0 means a bad pose was captured, while a
                        // sane held eye with the view still on the floor means
                        // something downstream moved it.
                        auto entity=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                        float entZ=entity?*reinterpret_cast<const float*>(entity+0x28):0.f;
                        const auto& b=holdingPlayerBase?lastPlayerBase:renderBase;
                        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                            fprintf(f,"dialogueHold=%d frame=%llu heldZ=%.2f entityZ=%.2f eyeIn=%.2f ageMs=%llu stale=%d\n",
                                holdingPlayerBase,frameId.load(),b.m[14],entZ,
                                entity?b.m[14]-entZ:-1.f,now-lastPlayerBaseTick,
                                int(now-lastPlayerBaseTick>=1000));
                            fclose(f);
                        }
                    }
                }
            }
            // Re-arm calibration by holding the RIGHT controller above the head.
            // Positions only, so no axis convention can invert it, and raising a
            // hand overhead and keeping it there does not happen in normal play -
            // unlike pointing down, which is just an arm at rest. No button, so
            // it cannot collide with [Buttons] or fubar's weapon calibration.
            // It exists because the INI trigger needs a message from me, and my
            // reply time is the one thing Mark cannot plan around.
            if(controllerHands && controllerHandsArmGestureMs>0 && t.rightController.valid) {
                auto gnow=GetTickCount64();
                if(t.rightController.aim.position.y<=t.head.position.y+controllerHandsArmGestureCm*.01f)
                    handArmGestureSince=0;
                else if(!handArmGestureSince)handArmGestureSince=gnow;
                else if(gnow-handArmGestureSince>=uint64_t(controllerHandsArmGestureMs>0?controllerHandsArmGestureMs:1500)) {
                    handArmGestureSince=0;
                    if(!handCalibrateArmed.exchange(1)) {
                        handCalibrateArmTick.store(gnow);
                        handArmGestures.fetch_add(1);
                        // Audible: he can neither read a log nor press a key in
                        // the headset. Same mechanism the weapon calibration uses.
                        MessageBeep(MB_ICONASTERISK);
                    }
                }
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
                    if(instance) {
                        auto raw=CameraMath::ControllerMuzzle(renderBase,reference,t.rightController.aim,worldScale,controllerMuzzleForward);
                        // Before any per-weapon offset: this is the controller and
                        // nothing else, which is what the hand must follow.
                        auto rawController=raw;
                        // Native weapon constructor 0x761130 stores the asset-table ID at +0x58.
                        // 0x777040 resolves its model through that ID, independent of instance handles.
                        auto key=*reinterpret_cast<uint32_t*>(weapon+0x58);
                        CameraMath::Matrix modelTarget{};
                        bool nativeRequested=controllerHandsModelAlignment.load() && controllerHands && controllerHandsWeapon &&
                            controllerHandsMode==15 && !handsSuspendedByMode.load() && activeCameraIsPlayer.load() && !handsFarFromBody.load();
                        bool targetValid=nativeRequested && ModelHandAlignment::GripTarget(t,now,renderBase,reference,worldScale,modelTarget);
                        const CameraMath::Matrix* grip=
                            (controllerHandsWeapon && controllerHands)?FindHandGrip(key):nullptr;
                        if(nativeRequested){
                            // Deliberately bypass BOTH learned-grip files and manual
                            // weapon calibration. Resolve from the native pose below.
                        }
                        else if(grip) {
                            // THIS weapon's own grip, from its own animation.
                            // Replaces the per-weapon calibration rather than
                            // adding to it, so nothing downstream changes.
                            raw=CameraMath::Multiply(*grip,raw);
                        }
                        else if(key<0x18000 && !weaponCalibrationEnabled) {
                            // Apply the stored offset and never capture. Same
                            // composition the save-failure path uses below.
                            raw=WeaponCalibration::Units(
                                CameraMath::Multiply(calibrationStore.Get(key),
                                    WeaponCalibration::Metres(raw,worldScale)),
                                worldScale);
                        }
                        else if(key<0x18000) {
                            auto offset=calibrationStore.Get(key);
                            auto result=calibration.Update(WeaponCalibration::Metres(raw,worldScale),offset,key,
                                reinterpret_cast<uintptr_t>(instance),t.controllerButtons,
                                t.gamepad.valid && !nativeScope && !gameMenuOpen,now);
                            calibrationUpdated=true;
                            if(result.event==WeaponCalibration::Event::Frozen)MessageBeep(MB_OK);
                            if(result.event==WeaponCalibration::Event::Save) {
                                bool saved=calibrationStore.Save(key,result.offset);
                                FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"weaponCalibration key=%08x saved=%d\n",key,saved);fclose(f);}
                                MessageBeep(saved?MB_OK:MB_ICONERROR);
                                if(!saved)result.muzzle=CameraMath::Multiply(offset,WeaponCalibration::Metres(raw,worldScale));
                            }
                            raw=WeaponCalibration::Units(result.muzzle,worldScale);
                        }
                        weaponPose={weapon,instance,raw,t.tick,true,*reinterpret_cast<void**>(weapon+0x64),rawController};
                        weaponPose.nativeRequested=nativeRequested;weaponPose.gripTarget=modelTarget;
                        if(diagnosticWeapon!=weapon || diagnosticInstance!=instance){
                            diagnosticWeapon=weapon;diagnosticInstance=instance;diagnosticEquippedTick=now;
                        }
                        weaponPose.equippedTick=diagnosticEquippedTick;
                        weaponPose.buttons=t.controllerButtons;weaponPose.diagnosticPad=t.gamepad;
                        if(nativeRequested){
                            weaponPose.active=targetValid;
                            if(targetValid)ResolveNativePair(weaponPose);
                        }
                    }
                }
            }
            current.controllerBase=renderBase;
            current.world=CameraMath::HeadWorld(renderBase,reference,t.head,worldScale);
            if(immersiveScopeActive) {
                // Use the same calibrated firing line as the rendered rifle and shots.
                if(weaponPose.active && (!weaponPose.nativeRequested || weaponPose.nativeValid))current.world=WeaponCalibration::Camera(weaponPose.muzzle);
            }
            current.view=CameraMath::InverseRigid(current.world);
            current.manager=CameraMath::Load(manager+0x13b0);
            auto oldView=CameraMath::Load(originalView(self));
            lastRenderBase=renderBase;lastManagerRaw=current.manager;lastOldView=oldView;
            updatedSincePresent=true;
            // The camera never runs while the in-game menu is open (it stops before the
            // menu's open call). If it keeps running, the close call was missed.
            if(!gameMenuOpen)menuUpdates=0;
            else if(++menuUpdates==30) {
                gameMenuOpen=false;
                FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"gameMenu flag cleared: camera running frame=%llu\n",frameId.load());fclose(f);}
            }
            for(int col=0;col<3;col++) {
                float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*oldView.m[row*4+col];
                for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
                current.manager.m[12+col]+=(current.view.m[12+col]-oldView.m[12+col])*scale;
            }
            current.presentationTracking=t;
            current.immersiveScoped=immersiveScopeActive;
            current.tracking=immersiveScopeActive?ImmersiveScope::Zoom(t,scopeMagnification):t;
            current.active=true;current.managerAddress=reinterpret_cast<uintptr_t>(self);
            current.playerInstance=active==manager+0x6f0?*reinterpret_cast<void**>(active+0xaa0):nullptr;
            if(current.playerInstance && movementDirection!=DirectionConfig::Source::Mouse) {
                using FindPlayer=unsigned char*(__cdecl*)(void*);
                auto player=reinterpret_cast<FindPlayer>(base+0x6032c0-0x400000)(current.playerInstance);
                if(player)current.inputIndex=*reinterpret_cast<uint32_t*>(player+0x1c);
            }

        }
        if(!calibrationUpdated)calibration.Cancel();
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
        if(p[8]>0 && p[7]>1000 && (Match(matrix,snapshot.originalWorld)||Match(matrix,snapshot.world)||
            (snapshot.leveled && Match(matrix,snapshot.tiltedWorld)))) {
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
using ModelDraw=void(__thiscall*)(void*,void*,void*,void*,bool);
ModelDraw originalModelDraw{};
void __fastcall ModelDrawHook(void* self,void*,void* device,void* state,void* stream,bool twice) {
    auto frame=frameId.load();
    if(!HandOnlyGeometry::DrawSelected(self,device,state,stream,twice,frame,originalModelDraw)) {
        originalModelDraw(self,device,state,stream,twice);
        HandDrawDiagnostic::Observe(self,stream,twice,frame);
    }
}
void InstallHandDrawDiagnostic() {
    // DXHRDC 2.0.66.0: validated 0x540150..0x540264. thiscall, four stack
    // arguments, ret 0x10. Non-null stream reaches DrawIndexed at 0x540228
    // (and 0x54025e for two-sided rendering). Install independently so a probe
    // failure never disables the existing camera/hand hooks.
    auto address=base+0x540150-0x400000;
    const unsigned char signature[]={0x53,0x8b,0x5c,0x24,0x10,0x56,0x8b,0xf1,0x85,0xdb,0x0f,0x84,0x02,0x01,0x00,0x00};
    if(memcmp(reinterpret_cast<void*>(address),signature,sizeof(signature))) {
        HandDrawDiagnostic::hookStatus=-1;return;
    }
    if(MH_CreateHook(reinterpret_cast<void*>(address),reinterpret_cast<void*>(&ModelDrawHook),
        reinterpret_cast<void**>(&originalModelDraw))!=MH_OK) {HandDrawDiagnostic::hookStatus=-2;return;}
    if(MH_EnableHook(reinterpret_cast<void*>(address))!=MH_OK) {
        MH_RemoveHook(reinterpret_cast<void*>(address));HandDrawDiagnostic::hookStatus=-3;return;
    }
    HandDrawDiagnostic::hookStatus=1;
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
        if(pairInfo.mode==0){pairInfo.mode=1;pairInfo.tracking=drawing.immersiveScoped?drawing.presentationTracking:drawing.tracking;}
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
    // Inside the player's own actor draw, each submitted piece gets an index.
    if(handDrawing) {
        auto idx=handPrimIndex++;
        if(handPrimLogged<3 && handPrimTraceCount<64) {
            auto& t=handPrimTrace[handPrimTraceCount++];
            t.index=idx;t.primitive=reinterpret_cast<uintptr_t>(self);
            t.material=primitiveState?*reinterpret_cast<uintptr_t*>(primitiveState+0xc):0;
            t.flags=primitiveState?*reinterpret_cast<uint32_t*>(primitiveState):0;
        }
        // Simply do not submit it. Nothing is moved, so nothing can smear.
        if(idx<32 && (controllerHandsSkipPrimMask&(1u<<idx)))return;
    }
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
// Local patch (level in-game menu): while the in-game menu or a loading/briefing
// screen is up the game stops updating its camera, so every frame reuses the last
// head pose, pitch and roll included, and the screen (a head-relative plane) is
// pinned at that tilt. Once the pose is frozen, turn it about the head to heading
// only, the same as LevelReference, and rebuild the camera from the last update's
// inputs. The eyes keep their offsets from the head, the game world stays where it
// is and the screen comes out level. Runs under stateMutex, once per frozen pose.
void LevelFrozenView(uint64_t frame) {
    if(updatedSincePresent || !current.active)presentsWithoutUpdate=0;else ++presentsWithoutUpdate;
    updatedSincePresent=false;
    if(!levelMenu || !current.active || current.leveled)return;
    if(wheelOpen)return; // the wheel pauses gameplay; leave the view as it is
    bool menu=gameMenuOpen;
    // The in-game menu is known to be open: level at once. Anything else must stay
    // frozen for a while first, so a brief stall in gameplay is left alone.
    if(presentsWithoutUpdate<(menu?1u:frozenLevelFrames))return;
    uint64_t id=current.tracking.id;
    auto& t=current.tracking;
    auto fwd=CameraMath::Rotate(t.head.orientation,{0,0,-1}),right=CameraMath::Rotate(t.head.orientation,{1,0,0});
    double pitch=std::asin(std::clamp(double(fwd.y),-1.,1.))*180/3.14159265358979;
    double roll=std::asin(std::clamp(double(right.y),-1.,1.))*180/3.14159265358979;
    auto level=LevelHeading(t.head.orientation);
    auto turn=CameraMath::Multiply(level,CameraMath::Inverse(t.head.orientation));
    const auto head=t.head.position;
    for(auto& e:t.eyes) {
        auto offset=CameraMath::Rotate(turn,{e.pose.position.x-head.x,e.pose.position.y-head.y,e.pose.position.z-head.z});
        e.pose.position={head.x+offset.x,head.y+offset.y,head.z+offset.z};
        e.pose.orientation=CameraMath::Multiply(turn,e.pose.orientation);
    }
    t.head.orientation=level;
    current.tiltedWorld=current.world;
    current.world=CameraMath::HeadWorld(lastRenderBase,reference,t.head,worldScale);
    current.view=CameraMath::InverseRigid(current.world);
    current.manager=lastManagerRaw;
    for(int col=0;col<3;col++) {
        float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*lastOldView.m[row*4+col];
        for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
        current.manager.m[12+col]+=(current.view.m[12+col]-lastOldView.m[12+col])*scale;
    }
    current.leveled=true;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"levelMenu frame=%llu pose=%llu reason=%s removedPitch=%.1f removedRoll=%.1f\n",frame,id,menu?"menu":"frozen",pitch,roll);fclose(f);
    }
}
#if defined(_M_IX86)
// Local patch (level in-game menu, reader buttons): the in-game menu
// (map/objectives/inventory), the e-reader and the news reader are the game's
// NsGameMenu, NsIReader and NsNewsReader movie controllers: Scaleform screens, not
// IMenu classes (title, pause, ending, making-of) that ScreenMode can see. Hook
// their activate/deactivate (vtable slots 4/5, found through the game's RTTI) to
// know when they are open. Each hook is a pass-through: it saves every register
// and flag, notes the event, restores them and jumps into the original function,
// so it cannot disturb the function's arguments or calling convention. Every
// target is verified and hooked on its own: a mismatch skips only that hook and
// never affects the camera hooks. Verification bytes stop before any absolute
// address, which ASLR relocates.
struct ScreenHook {uintptr_t address;const char* name;const char* event;const char* bytes;size_t length;};
const ScreenHook screenHooks[]={
    {0x7e8630,"GameMenu","open","\x53\x56\xbb\x01\x00\x00\x00",7},{0x7e8be0,"GameMenu","close","\x53\x56\x8b\xf1\x8b\x4e\x30",7},
    {0x7f08c0,"IReader","open","\x83\xec\x30\x53\x56\x8b\xf1",7},{0x7f0530,"IReader","close","\x83\xec\x30\x53\x56\x8b\xf1\x57",8},
    {0x7f7e70,"NewsReader","open","\x83\xec\x20\x53\x56\x8b\xf1",7},{0x7f76c0,"NewsReader","close","\x83\xec\x20\x53\x56\x8b\xf1\x33",8},
    // The weapon wheel (hold Y for the quick inventory) is NsQuickSelector. It pauses
    // the game with the camera stopped, which otherwise looks exactly like a loading
    // screen: the view would be leveled and the buttons would switch layout, so the
    // held button that opened the wheel would stop being held and the wheel would
    // close. While it is open the view and the gameplay layout are kept.
    // Measured 2026-09-19: NsMenuWheel never opens on this build, and
    // NsAugmentationWheel opens once at startup and never closes (it is a permanent
    // HUD element), so neither may be treated as a pause.
    {0x7fe860,"QuickSelector","open","\x83\xec\x24\x56\x57\x8b\xf1\xe8",8},{0x7fe670,"QuickSelector","close","\xe8\x2b\x29\xfc\xff\x6a\x00\x68",8},
};
constexpr int screenHookCount=int(sizeof(screenHooks)/sizeof(screenHooks[0]));
void* screenOriginals[screenHookCount]{};
void __cdecl NoteScreen(int index,void* self) {
    bool open=index%2==0;
    if(index<2)gameMenuOpen=open;
    else if(index<6)readerOpen=open;
    else wheelOpen=open;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"screen %s %s self=%p frame=%llu\n",screenHooks[index].name,screenHooks[index].event,self,frameId.load());fclose(f);
    }
}
#define SCREEN_STUB(n) __declspec(naked) void ScreenStub##n() { \
    __asm pushad __asm pushfd __asm push ecx __asm push n __asm call NoteScreen __asm add esp,8 \
    __asm popfd __asm popad __asm jmp dword ptr [screenOriginals+n*4] }
SCREEN_STUB(0) SCREEN_STUB(1) SCREEN_STUB(2) SCREEN_STUB(3) SCREEN_STUB(4) SCREEN_STUB(5)
SCREEN_STUB(6) SCREEN_STUB(7)
#undef SCREEN_STUB
void InstallScreenHooks() {
    void* stubs[]={(void*)&ScreenStub0,(void*)&ScreenStub1,(void*)&ScreenStub2,(void*)&ScreenStub3,
                   (void*)&ScreenStub4,(void*)&ScreenStub5,(void*)&ScreenStub6,(void*)&ScreenStub7};
    static_assert(sizeof(stubs)/sizeof(stubs[0])==screenHookCount,"one stub per screen hook");
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-camera.log","a"))f=nullptr;
    for(int i=0;i<screenHookCount;i++) {
        auto& h=screenHooks[i];auto target=reinterpret_cast<void*>(VA(h.address));
        const char* result="hooked";
        if(memcmp(target,h.bytes,h.length))result="skipped: unexpected bytes";
        else if(MH_CreateHook(target,stubs[i],&screenOriginals[i])!=MH_OK)result="skipped: create failed";
        else if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);result="skipped: enable failed";}
        if(f)fprintf(f,"screen hook %s %s at 0x%x: %s\n",h.name,h.event,unsigned(h.address),result);
    }
    if(f)fclose(f);
}
#endif
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
#if defined(_M_IX86)
    if(enabled)InstallScreenHooks(); // after, and independent of, the camera hooks
#endif
    wchar_t config[MAX_PATH]{};GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,config,nullptr);
    wchar_t scaleText[32];GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"100",scaleText,32,config);
    float scale=static_cast<float>(_wtof(scaleText));if(std::isfinite(scale)&&scale>=10&&scale<=1000)worldScale=scale;
    lockVerticalCamera=GetPrivateProfileIntW(L"VR",L"LockVerticalCamera",0,config)!=0;
    levelRecenter=GetPrivateProfileIntW(L"VR",L"LevelRecenter",1,config)!=0;
    levelMenu=GetPrivateProfileIntW(L"VR",L"LevelMenu",1,config)!=0;
    yawOnlyCamera=GetPrivateProfileIntW(L"VR",L"YawOnlyCamera",0,config)!=0;
    controllerHandsForceFallback=GetPrivateProfileIntW(L"VR",L"ControllerHandsForceFallback",0,config)!=0;
    fixedDialogueCamera=GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",0,config)!=0;
    if(enabled && fixedDialogueCamera) {
        // Hooked on their own, like the screen hooks: a mismatch or failure only
        // turns this option off and never affects the camera hooks.
        const Hook conversation[]={
            {0x6bddb0,(void*)&ConversationTrackHook,(void**)&originalConversationTrack,"\x66\x8b\x44\x24\x04\xba\xff\xff\x00\x00",10},
            {0x6bed40,(void*)&ConversationExitHook0,(void**)&originalConversationExit[0],"\x83\xec\x08\x56\x8b\xf1\x80\x7e\x50\x00",10},
            {0x6bf3e0,(void*)&ConversationExitHook1,(void**)&originalConversationExit[1],"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x74\x53\x56\x8b\xf1\x80\x7e\x50\x00",17}};
        bool ok=true;
        for(auto& h:conversation)if(memcmp((void*)VA(h.address),h.bytes,h.length))ok=false;
        int created=0;
        if(ok)for(auto& h:conversation){if(MH_CreateHook((void*)VA(h.address),h.hook,h.original)!=MH_OK){ok=false;break;}++created;}
        if(ok)for(auto& h:conversation)if(MH_EnableHook((void*)VA(h.address))!=MH_OK)ok=false;
        if(!ok){for(int i=0;i<created;i++){MH_DisableHook((void*)VA(conversation[i].address));MH_RemoveHook((void*)VA(conversation[i].address));}fixedDialogueCamera=false;}
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"conversation hooks: %s\n",ok?"hooked":"skipped");fclose(f);}
    }
    bobTrace=GetPrivateProfileIntW(L"VR",L"BobTrace",0,config)!=0;
    // Optional stutter fix from HRDCfix (github.com/imring/HRDCfix, MIT).
    // 0x54c800 spin-waits on Sleep(1) while a busy flag is set. The game never
    // calls timeBeginPeriod, so each Sleep(1) can last a whole timer tick
    // (~15.6 ms, nearly two frames at 120 Hz); Sleep(0) only yields. Bytes
    // 13-16 hold Sleep's import address, relocated each launch, so they are
    // not compared. An already-patched byte simply doesn't match.
    if(GetPrivateProfileIntW(L"VR",L"SleepFix",0,config)!=0) {
        static const unsigned char spin[]={0x56,0x8b,0xf1,0x8a,0x46,0x08,0x84,0xc0,0x74,0x13,0x57,0x8b,0x3d,0,0,0,0,0x6a,0x01};
        auto code=reinterpret_cast<unsigned char*>(VA(0x54c800));
        bool match=true;
        for(size_t i=0;i<sizeof(spin);i++)if((i<13||i>16)&&code[i]!=spin[i]){match=false;break;}
        DWORD old{};
        if(match&&VirtualProtect(code+0x12,1,PAGE_EXECUTE_READWRITE,&old)) {
            code[0x12]=0x00; // push 1 -> push 0
            VirtualProtect(code+0x12,1,old,&old);
            FlushInstructionCache(GetCurrentProcess(),code+0x12,1);
            sleepFix=true;
        }
    }
    auto readInt=[&](const wchar_t* key,int fallback,int hi){return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"VR",key,fallback,config)),0,hi);};
    auto readFloat=[&](const wchar_t* key,const wchar_t* fallback,float lo,float hi,float& target) {
        wchar_t text[32]{};GetPrivateProfileStringW(L"VR",key,fallback,text,32,config);
        float v=static_cast<float>(_wtof(text));if(std::isfinite(v)&&v>=lo&&v<=hi)target=v;
    };
    yawSwing.a.windowMs[0]=readInt(L"HeadSwayYawMs",0,2000);
    yawSwing.b.windowMs[0]=readInt(L"HeadSwayYawMs2",0,2000);
    readFloat(L"HeadSwayYawLimit",L"1.5",0,20,yawSwing.a.limit[0]);
    yawSwing.b.limit[0]=yawSwing.a.limit[0];
    yawSwing.a.enabled=yawSwing.a.windowMs[0]>0;yawSwing.b.enabled=yawSwing.b.windowMs[0]>0;
    stanceHold.enabled=GetPrivateProfileIntW(L"VR",L"StanceHold",0,config)!=0;
    readFloat(L"StanceHoldTrigger",L"60",5,400,stanceHold.trigger);
    swayHold.windowMs=readInt(L"SideSwayHoldMs",0,2000);
    stanceHold.Reset();
    motionControls=DirectionConfig::MotionEnabled(config);
    nativeLookPitchLock=GetPrivateProfileIntW(L"VR",L"NativeLookPitchLock",0,config)!=0;
    nativeLookHeading=GetPrivateProfileIntW(L"VR",L"NativeLookHeading",0,config)!=0;
    nativeCameraPosition=GetPrivateProfileIntW(L"VR",L"NativeCameraPosition",0,config)!=0;
    {
        wchar_t headingLimit[32];
        wchar_t fallback[32];swprintf_s(fallback,L"%g",double(nativeHeadingMaxDegDefault));
        GetPrivateProfileStringW(L"VR",L"NativeLookHeadingMaxDeg",fallback,headingLimit,32,config);
        float value=static_cast<float>(_wtof(headingLimit));
        if(std::isfinite(value) && value>=0 && value<=180)nativeHeadingMaxDeg=value;
    }
    HandWeaponMotionDiagnostic::enabled=GetPrivateProfileIntW(L"VR",L"HandWeaponMotionLog",0,config)!=0;
    if(enabled && (nativeLookPitchLock || nativeLookHeading)) {
        auto target=reinterpret_cast<void*>(VA(0x7ac8c0));
        const char inputBytes[]="\x55\x8b\xec\x83\xe4\xf0\x81\xec\x74\x01\x00\x00";
        const char setterBytes[]="\x55\x8b\xec\x83\xe4\xf0\x83\xec\x28\x56\x8b\xf1";
        bool ok=!memcmp(target,inputBytes,sizeof(inputBytes)-1) &&
            !memcmp(reinterpret_cast<void*>(VA(0x7a90a0)),setterBytes,sizeof(setterBytes)-1);
        if(ok) {
            ok=MH_CreateHook(target,reinterpret_cast<void*>(&NativeLookInputHook),
                reinterpret_cast<void**>(&originalNativeLookInput))==MH_OK;
            if(ok && MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);ok=false;}
        }
        nativeLookPitchLock=nativeLookPitchLock && ok;
        nativeLookHeading=nativeLookHeading && ok;
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            fprintf(f,"nativeLookPitchLock hook=%s controllerOnly=1 combatOnly=1\n",ok?"ready":"failed");fclose(f);
        }
        FILE* headingLog{};if(!fopen_s(&headingLog,"DeusExHRVR-camera.log","a")) {
            fprintf(headingLog,"nativeLookHeading enabled=%d sameEngineFrame=1 combatOnly=1\n",nativeLookHeading);
            fclose(headingLog);
        }
    }
    experimentalMotionControls=motionControls && GetPrivateProfileIntW(L"VR",L"ExperimentalMotionControls",0,config)!=0;
    weaponCalibrationEnabled=GetPrivateProfileIntW(L"VR",L"WeaponCalibration",1,config)!=0;
    controllerHideArms=GetPrivateProfileIntW(L"VR",L"ControllerHideArms",1,config)!=0;
    controllerHands=GetPrivateProfileIntW(L"VR",L"ControllerHands",0,config)!=0;
    controllerHandBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandBone",158,config));
    controllerHandBone2=int(GetPrivateProfileIntW(L"VR",L"ControllerHandBone2",999,config));
    controllerHandsMode=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsMode",0,config));
    controllerHandsModelAlignment=GetPrivateProfileIntW(L"VR",L"ControllerHandsModelAlignment",0,config)!=0;
    controllerHandsLeft=GetPrivateProfileIntW(L"VR",L"ControllerHandsLeft",1,config)!=0;
    controllerHandsNeutralWrist=GetPrivateProfileIntW(L"VR",L"ControllerHandsNeutralWrist",1,config)!=0;
    controllerHandsRadius=float(GetPrivateProfileIntW(L"VR",L"ControllerHandsRadiusCm",15,config));
    controllerHandsSplitA=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSplitA",74,config));
    controllerHandsSplitB=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSplitB",148,config));
    controllerHandsWeapon=GetPrivateProfileIntW(L"VR",L"ControllerHandsWeapon",0,config)!=0;
    controllerHandsCollapseBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsCollapseBone",-1,config));
    {
        int mo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsMaxOffsetIn",12,config));
        if(mo>0&&mo<=400)controllerHandsMaxOffsetIn=float(mo);
        int sm=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSuspendModes",342,config));
        if(sm>=0&&sm<512)controllerHandsSuspendModes=uint32_t(sm);
        controllerHandsPose=GetPrivateProfileIntW(L"VR",L"ControllerHandsPose",0,config)!=0;
        int pm=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsPoseButton",0x4000,config));
        if(pm>0&&pm<=0xffff)controllerHandsPoseMask=uint32_t(pm);
        int sb=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSprintButton",0x0100,config));
        if(sb>0&&sb<=0xffff)controllerHandsSprintMask=uint32_t(sb);
    }
    controllerHandsKeepFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsKeepFrom",74,config));
    controllerHandsKeepTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsKeepTo",82,config));
    {
        int drop=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsCollapseDropM",0,config));
        if(drop>=0&&drop<=10000)controllerHandsCollapseDrop=float(drop);
    }
    controllerHandsProbe=GetPrivateProfileIntW(L"VR",L"ControllerHandsProbe",0,config)!=0;
    controllerHandsProbeFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsProbeFrom",100,config));
    controllerHandsProbeTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsProbeTo",160,config));
    controllerHandsDriveBone=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsDriveBone",152,config));
    readFloat(L"ControllerHandsTrimXCm",L"0",-200,200,handTrim[0]);
    readFloat(L"ControllerHandsTrimYCm",L"0",-200,200,handTrim[1]);
    readFloat(L"ControllerHandsTrimZCm",L"0",-200,200,handTrim[2]);
    controllerHandsSteadyMs=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsSteadyMs",800,config));
    readFloat(L"ControllerHandsSteadyCm",L"5",.1f,100,controllerHandsSteadyCm);
    readFloat(L"ControllerHandsTrimPitchDeg",L"0",-180,180,handTrimRot[0]);
    readFloat(L"ControllerHandsTrimYawDeg",L"0",-180,180,handTrimRot[1]);
    readFloat(L"ControllerHandsTrimRollDeg",L"0",-180,180,handTrimRot[2]);
    readFloat(L"ControllerHandsIdGateDeg",L"0.5",.01f,90,controllerHandsIdGateDeg);
    controllerHandsSkipPrimMask=uint32_t(GetPrivateProfileIntW(L"VR",L"ControllerHandsSkipPrimMask",0,config));
    readFloat(L"ControllerHandsCollapseBackIn",L"0",-24,24,controllerHandsCollapseBackIn);
    readFloat(L"ControllerHandsCollapseSideIn",L"0",-24,24,controllerHandsCollapseSideIn);
    readFloat(L"ControllerHandsCollapseUpIn",L"0",-24,24,controllerHandsCollapseUpIn);
    controllerHandsHideFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsHideFrom",0,config));
    controllerHandsHideTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsHideTo",0,config));
    controllerHandsWeldFrom=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsWeldFrom",112,config));
    controllerHandsWeldTo=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsWeldTo",130,config));
    readFloat(L"ControllerHandsCollapseScale",L"0",0,1,controllerHandsCollapseScale);
    controllerHandsArmGestureMs=int(GetPrivateProfileIntW(L"VR",L"ControllerHandsArmGestureMs",1500,config));
    readFloat(L"ControllerHandsArmGestureCm",L"20",1,200,controllerHandsArmGestureCm);
    if(controllerHands) {
        LoadHandCalibration();
        LoadHandGrips();

        LoadHandPose();
        // Armed from the start when asked for and nothing is saved yet, so the
        // capture never has to wait on a message from me. Not armed when a
        // calibration already loaded, or a later launch could overwrite a good
        // one with whatever pose happened to be still first.
        if(!handCalibrated && GetPrivateProfileIntW(L"VR",L"ControllerHandsCalibrate",0,config)!=0)
            handCalibrateArmed.store(1);
    }
    // Only while the feature is on, so fubar's default build starts no thread.
    if(controllerHands){
        if(enabled)InstallHandDrawDiagnostic();
        handsConfigPath=config;StartHandsConfigWatcher();
    }
    if(!(controllerHandsRadius>0)||controllerHandsRadius>500)controllerHandsRadius=15.f;
    interactionAim=DirectionConfig::Read(config,L"InteractionAim");
    movementDirection=DirectionConfig::Read(config,L"MovementDirection");
    movementResponse=GetPrivateProfileIntW(L"VR",L"MovementResponse",0,config)==1?1u:0u;
    {FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){
        fprintf(f,"MovementResponse startup mode=%u motionControls=%d movement=%s\n",movementResponse.load(),int(motionControls),DirectionConfig::Name(movementDirection));fclose(f);
    }}
    immersiveScope=GetPrivateProfileIntW(L"VR",L"ImmersiveScope",0,config)!=0;
    GetPrivateProfileStringW(L"VR",L"ScopeMagnification",L"4",scaleText,32,config);
    {float value=static_cast<float>(_wtof(scaleText));if(std::isfinite(value)&&value>=1&&value<=12)scopeMagnification=value;}
    GetPrivateProfileStringW(L"VR",L"ControllerMuzzleForwardMetres",L"0.25",scaleText,32,config);
    float muzzleForward=static_cast<float>(_wtof(scaleText));
    if(std::isfinite(muzzleForward) && muzzleForward>=0 && muzzleForward<=1)controllerMuzzleForward=muzzleForward;
    // Luma port: load the native shader-swap table from beside the companion.
    // Gated by [Luma] Enable (default on) so the feature is opt-out. The
    // table lives at <game>/DeusExHRVR/shaders/dxhr/table.csv; a missing or
    // empty table leaves the swap inactive and NoteBound is a no-op.
    bool lumaEnable=GetPrivateProfileIntW(L"Luma",L"Enable",1,config)!=0;
    if(lumaEnable) {
        // config holds the absolute path to DeusExHRVR.ini in the game folder;
        // its directory is the game root the shader table is relative to.
        std::filesystem::path configPath=config;
        shaderSwap.Load(configPath.parent_path());
        // Phase 2: substitution defaults off; opt in via ini. F12 can toggle
        // it live regardless of this starting state.
        bool sub=GetPrivateProfileIntW(L"Luma",L"SubstituteShaders",0,config)!=0;
        shaderSwap.SetSubstitutionEnabled(sub);
        // Phase 3: injected-pass enables. All default off until tested.
        // XeGTAO experiment withdrawn: retain the game's native AO.
        lumaPasses.SetXeGTAOEnabled(false);
        lumaPasses.SetSMAAEnabled(GetPrivateProfileIntW(L"Luma",L"SMAAEnable",0,config)!=0);
        lumaPasses.SetModulateLightingEnabled(GetPrivateProfileIntW(L"Luma",L"ModulateLightingEnable",0,config)!=0);
        // LumaSettings cbuffer values. Defaults match Luma's DXHR main.cpp
        // (lines 1589-1599): "not vanilla like" tuned values. Override via ini.
        // These take effect on the next Bind() (lumaSettingsCB is init'd later
        // in SetShaderSwapDevice, but setting values now is fine — SetDefaults
        // runs first, then these override before any bind happens).
        auto readFloat=[&](const wchar_t* key,float def)->float{
            wchar_t buf[32]{};
            if (!GetPrivateProfileStringW(L"Luma",key,L"",buf,32,config)) return def;
            wchar_t* end{};
            float v=wcstof(buf,&end);
            while (end && (*end==L' ' || *end==L'\t')) ++end;
            return end!=buf && end && !*end && std::isfinite(v) && v>=0.0f && v<=10.0f ? v : def;
        };
        auto& gs=lumaSettingsCB.Get().GameSettings;
        gs.BloomIntensity=readFloat(L"BloomIntensity",0.8f);
        gs.FogIntensity=readFloat(L"FogIntensity",0.0f);
        gs.ColorGradingIntensity=readFloat(L"ColorGradingIntensity",1.0f);
        gs.DesaturationIntensity=readFloat(L"DesaturationIntensity",0.333f);
        gs.AmbientLightingIntensity=readFloat(L"AmbientLightingIntensity",0.8f);
        gs.EmissiveIntensity=readFloat(L"EmissiveIntensity",0.667f);
        gs.HDRBoostIntensity=readFloat(L"HDRBoostIntensity",1.0f);
        gs.LightingColor[0]=readFloat(L"LightingRed",1.0f);
        gs.LightingColor[1]=readFloat(L"LightingGreen",1.0f);
        gs.LightingColor[2]=readFloat(L"LightingBlue",1.0f);
        gs.AmbientLightColor[0]=readFloat(L"AmbientRed",1.0f);
        gs.AmbientLightColor[1]=readFloat(L"AmbientGreen",1.0f);
        gs.AmbientLightColor[2]=readFloat(L"AmbientBlue",1.0f);
        lumaSettingsCB.MarkDirty();
        // Phase 4: overlap dedup. Default on — skip Luma substitution for
        // shaders DXHRVR already corrects per-eye (projected light/shadow via
        // F3, identified by EffectShader::UsesCentreViewMatrix).
        shaderSwap.SetDedupWithDXHRVR(GetPrivateProfileIntW(L"Luma",L"DedupWithDXHRVR",1,config)!=0);
        // Always retain native ambient occlusion.
        {
            shaderSwap.AddSkipHash(0xD44718C4u); // GenerateAmbientOcclusion (DC)
            shaderSwap.AddSkipHash(0x7A054979u); // GenerateAmbientOcclusion (OG)
        }
    }
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"Camera hooks base=%p enabled=%d unitsPerMetre=%g lockVerticalCamera=%d levelRecenter=%d levelMenu=%d yawOnlyCamera=%d stanceHold=%d/%g/%g yawMs=%d/%d yawLimit=%g sideSwayHold=%d sleepFix=%d weaponCalibration=%d handsWeapon=%d controllerHideArms=%d controllerHands=%d/%d/%d/%d/%gcm split=%d/%d keep=%d-%d drive=%d calibrated=%d trim=%g/%g/%g fixedDialogueCamera=%d nativeLook=%d/%d/%d headingMaxDeg=%g F6=toggle F9=recenter\n",reinterpret_cast<void*>(base),enabled,worldScale,lockVerticalCamera,levelRecenter,levelMenu,yawOnlyCamera,stanceHold.enabled,stanceHold.trigger,stanceHold.rate,yawSwing.a.windowMs[0],yawSwing.b.windowMs[0],yawSwing.a.limit[0],swayHold.windowMs,int(sleepFix),int(weaponCalibrationEnabled),int(controllerHandsWeapon),int(controllerHideArms),int(controllerHands),controllerHandBone,controllerHandBone2,controllerHandsMode,controllerHandsRadius,controllerHandsSplitA,controllerHandsSplitB,controllerHandsKeepFrom,controllerHandsKeepTo,
            controllerHandsDriveBone,int(handCalibrated),handTrim[0],handTrim[1],handTrim[2],int(fixedDialogueCamera),
            int(nativeLookPitchLock),int(nativeLookHeading),int(nativeCameraPosition),nativeHeadingMaxDeg);fclose(f);
    }
}
}
Transport::RenderInfo OnPresent(uint64_t frame,bool capture) {
    static std::once_flag once;std::call_once(once,Install);
    frameId=frame;
    // Luma port: reset per-frame scheduling flags at frame start (mirrors
    // Luma resetting game_device_data on frame boundary).
    if(lumaPasses.Loaded()) lumaPasses.OnFrameStart();
    // Luma port: update LumaSettings per-frame (frame index + resolution).
    // Resolution comes from HeadsetDisplay (queried at startup). Luma's
    // main.cpp does this at line 528-529.
    if(lumaSettingsCB.Initialized()) {
        lumaSettingsCB.SetFrameIndex(static_cast<uint32_t>(frame));
        // Use headset eye resolution as the output resolution (each eye is
        // rendered at this size). HeadsetDisplay::Active() + Settings hold it.
        if(HeadsetDisplay::Active()) {
            auto s=HeadsetDisplay::GetSettings();
            lumaSettingsCB.SetOutputResolution(static_cast<float>(s.width),
                                               static_cast<float>(s.height));
        }
    }
    // Detailed camera dumps perform synchronous file IO. Never schedule them
    // periodically on the render thread; F8 is the explicit diagnostic request.
    budget=capture?32:0;
    std::lock_guard lock(stateMutex);
    // Menus/videos can keep presenting while simulation (and camera updates)
    // is paused. Refresh here as well, before the following frame is built.
    RefreshScreenMode();if(screenReasons)current.active=false;
    if(screenReasons || gameMenuOpen || !requested || recenterRequested)calibration.Cancel();
    LevelFrozenView(frame);
    if(effectsCapture){SaveEffects(frame);effectCount=0;shaderTrace.End();}effectsCapture=capture;
    // At most 60 summary lines per game process; no per-frame disk writes.
    static uint64_t responseLogTick{};static unsigned responseLogCount{};
    auto responseNow=GetTickCount64();
    if(responseLogCount<60&&(!responseLogTick||responseNow-responseLogTick>=10000)){
        responseLogTick=responseNow;++responseLogCount;
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){
            fprintf(f,"MovementResponse runtime tick=%llu mode=%u motionControls=%d direction=%s hookCalls=%llu actionCalls=%llu passed=%llu reduced=%llu lastNonzeroInput=%g lastReturnedMagnitude=%g sampleTick=%llu sampleMode=%u screenReasons=%u\n",
                responseNow,movementResponse.load(),int(motionControls),DirectionConfig::Name(movementDirection),
                responseHookCalls.load(),responseActionCalls.load(),responsePassed.load(),responseReduced.load(),
                responseInputMagnitude.load(),responseOutputMagnitude.load(),responseSampleTick.load(),responseSampleMode,screenReasons);
            fclose(f);
        }
    }
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
            fprintf(motion,"immersiveScope enabled=%d active=%d owned=%d zoom=%g\n",immersiveScope,immersiveScopeActive,autoScopeOwned,scopeMagnification);
            fprintf(motion,"nativeLookPitchLock enabled=%d calls=%llu applied=%llu pitchBefore=%g pitchAfter=%g yaw=%g\n",
                nativeLookPitchLock,nativeLookCalls.load(),nativeLookApplied.load(),nativeLookBefore.load(),nativeLookAfter.load(),nativeLookYaw.load());
            fprintf(motion,"directions interaction=%s movement=%s interactionQueries=%llu movementAxes=%llu motionControls=%d inputIndex=%u actions=%p/%p lastInput=%g,%g lastOutput=%g,%g heading=%g\n",
                DirectionConfig::Name(interactionAim),DirectionConfig::Name(movementDirection),
                interactionQueries.load(),movementAxes.load(),motionControls,current.inputIndex,walkAction.load(),strafeAction.load(),
                lastMoveInput[0],lastMoveInput[1],lastMoveOutput[0],lastMoveOutput[1],lastMoveHeading);
            fprintf(motion,"controllerHands enabled=%d mode=%d bone=%d bone2=%d draws=%llu skeletons=%llu miss=%llu bones=%u\n",
                int(controllerHands),controllerHandsMode,controllerHandBone,controllerHandBone2,
                handDraws.load(),handSkeletons.load(),handSkeletonMiss.load(),handBoneCount.load());
            fprintf(motion,"controllerHandsDrive calibrated=%d bindBones=%u drive=%d trim=%g/%g/%g armed=%d holdUnits=%.1f\n",
                int(handCalibrated),handBindCount,controllerHandsDriveBone,
                handTrim[0],handTrim[1],handTrim[2],handCalibrateArmed.load(),handCalibrateQuality);
            fprintf(motion,"controllerHandsIdentify samples=%u reported=%d enters=%llu noWeapon=%llu belowGate=%llu maxDeg=%.2f\n",
                handIdSamples,int(handIdReported),handIdEnters.load(),handIdNoWeapon.load(),
                handIdBelowGate.load(),handIdMaxDeg);
            {std::lock_guard lock(attachmentTraceMutex);for(const auto& t:attachmentTrace)if(t.queries)
                fprintf(motion,"attachment caller=%08x bone=%d owner=%d queries=%llu corrected=%llu\n",
                    unsigned(t.caller),t.bone,t.owner,t.queries,t.corrected);}
            fclose(motion);
        }
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"HUD plane draws=%llu matrices=%llu frame=%llu trackingReadContentions=%llu rejectedSamples=%llu\n",hudDraws,hudMatrices,frame,trackingReader.reused,trackingReader.rejected);fclose(f);}
        // Luma port: fold shader-swap match stats into the capture log so the
        // dry run can be confirmed without a separate file read.
        if(shaderSwap.Active()) {
            auto ss=shaderSwap.GetStats();
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"shaderSwap notes=%llu matches=%llu unique=%llu substitutions=%llu enabled=%d frame=%llu\n",ss.notes,ss.matches,ss.uniqueMatches,ss.substitutions,int(shaderSwap.SubstitutionEnabled()),frame);fclose(f);}
        }
        // Phase 3: injected-pass counts.
        if(lumaPasses.Loaded()) {
            auto ps=lumaPasses.GetStats();
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"lumaPasses xegtao=%llu smaa=%llu modulate=%llu enabled=xg%d/smaa%d/mod%d frame=%llu\n",ps.xegtaoRuns,ps.smaaRuns,ps.modulateRuns,int(lumaPasses.XeGTAOEnabled()),int(lumaPasses.SMAAEnabled()),int(lumaPasses.ModulateLightingEnabled()),frame);fclose(f);}
        }
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
    // Luma port: F12 toggles live shader substitution (off by default; opt in
    // via [Luma] SubstituteShaders=1). Lets you A/B the swap in-headset without
    // restarting. Matches the existing F3/F4/F7 toggle pattern.
    bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    if(f12&&!f12Down && !(GetAsyncKeyState(VK_CONTROL)&0x8000)){
        bool on=ToggleShaderSubstitution();
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"lumaSubstitute=%d frame=%llu\n",on,frame);fclose(f);}
    }f12Down=f12;
    if(f6&&!f6Down){requested=!requested;referenceValid=false;current.active=false;}
    if(f9&&!f9Down){recenterRequested=true;conversationCamera=nullptr;} // F9 also releases a stuck conversation hold
    if((f6&&!f6Down)||(f9&&!f9Down)) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"tracking requested=%d recenter=%d frame=%llu\n",requested,recenterRequested,frame);fclose(f);}
    }
    f6Down=f6;f9Down=f9;return completed;
}
void SetChannel(Transport::Header* header){std::lock_guard lock(stateMutex);channel=header;trackingReader={};if(!header){current.active=false;referenceValid=false;}}
// Local patch (snap turn): the game's own camera heading while full tracked VR
// is showing gameplay. False in menus, terminals, scope and other screen modes.
// Bits 32 (in-game menu) and 64 (e-reader, news reader) select [ScreenButtons] only;
// the display stays tracked. Bit 128 is the opposite: a wheel screen pausing
// gameplay, where [Buttons] must be kept.
unsigned MovementResponse(){return movementResponse.load();}
void SetMovementResponse(unsigned value){
    if(value>1)return;
    auto previous=movementResponse.exchange(value);
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){
        fprintf(f,"MovementResponse menu previous=%u effective=%u tick=%llu\n",previous,value,GetTickCount64());fclose(f);
    }
}
unsigned MovementDirection(){return unsigned(movementDirection.load());}
void Recenter(){recenterRequested=true;conversationCamera=nullptr;}
void SetMovementDirection(unsigned value){if(value<=3)movementDirection.store(static_cast<DirectionConfig::Source>(value));}
unsigned CurrentScreenReasons(){std::lock_guard lock(stateMutex);return screenReasons|(gameMenuOpen?32u:0u)|(readerOpen?64u:0u)|(wheelOpen?128u:0u);}
bool SnapTurnView(float& yaw) {
    std::lock_guard lock(stateMutex);
    auto now=GetTickCount64();
    if(!current.active || screenReasons || gameMenuOpen || wheelOpen || !current.playerInstance || now<current.tracking.tick || now-current.tracking.tick>=250)return false;
    const auto& turnBase=current.nativeHeading?current.controllerBase:current.originalWorld;
    yaw=std::atan2(turnBase.m[9],turnBase.m[8]);
    return std::isfinite(yaw);
}
// Local patch (snap turn): the game's own quickbar auto-hide, g_quickBarAutoHide,
// the setting the tilde key toggles. The registration at 0xa68190 passes the
// value's address (0x01c79eb8) to the setting's constructor, which keeps it at
// +0x104 of the setting object (0x01c7bb60) and writes the default there; the
// game's own setter writes that one byte. All of it is checked before touching
// anything. Returns the value found (before setting it, if asked), or -1 when
// the layout doesn't match.
int QuickBarAutoHide(bool set) {
    if(!base)return -1;
    auto code=reinterpret_cast<const unsigned char*>(VA(0xa68190));
    auto dword=[&](size_t at){uint32_t v;memcpy(&v,code+at,4);return uintptr_t(v);};
    static const unsigned char pushes[]={0x6a,0x01,0x6a,0x00,0x6a,0x00,0x68}; // push 1; push 0; push 0; push value
    auto value=VA(0x01c79eb8),object=VA(0x01c7bb60),name=VA(0xaa4004);
    if(memcmp(code,pushes,sizeof(pushes)) || dword(7)!=value || code[11]!=0x68 || dword(12)!=name ||
        code[16]!=0xb9 || dword(17)!=object || strcmp(reinterpret_cast<const char*>(name),"g_quickBarAutoHide") ||
        *reinterpret_cast<const uintptr_t*>(object+0x104)!=value)return -1;
    auto flag=reinterpret_cast<volatile unsigned char*>(value);
    int found=*flag;
    if(set && !found)*flag=1;
    return found;
}

void SetShaderSwapDevice(ID3D11Device* device){
    shaderSwap.SetDevice(device);
    // Phase 3: also init the injected-pass subsystem now that the device is
    // available. Loads the injected-pass .cso blobs from the same compiled/
    // dir the shader swap uses. Missing blobs leave the corresponding pass
    // disabled; the first call to OnDraw is a no-op until enabled via ini.
    if(device) {
        // The compiled/ dir is <game>/DeusExHRVR/shaders/dxhr/compiled/, which
        // is shaderSwap's shaderDir + "compiled". We reconstruct it from the
        // exe path (same logic NativeTransport uses for the host exe).
        wchar_t module[MAX_PATH]{};HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&SetShaderSwapDevice),&self);
        GetModuleFileNameW(self,module,MAX_PATH);
        wchar_t* slash=wcsrchr(module,L'\\');
        if(slash) {
            slash[1]=0;
            std::filesystem::path compiled=std::filesystem::path(module)/L"DeusExHRVR"/L"shaders"/L"dxhr"/L"compiled";
            lumaPasses.Load(compiled, device);
        }
        // LumaSettings cbuffer: init + set defaults + cross-link so both
        // ShaderSwap (Phase 2) and LumaPasses (Phase 3) can bind it at b13
        // before their shaders run.
        lumaSettingsCB.Init(device);
        shaderSwap.SetLumaSettingsCB(&lumaSettingsCB);
        lumaPasses.SetLumaSettingsCB(&lumaSettingsCB);
    }
}
bool ImmersiveScopeButton(){return GetTickCount64()<scopePulseUntil.load();}
// Arms hands calibration from a bindable [Buttons] target. Out here with the
// other exported entry points: inside the anonymous namespace above it had
// internal linkage and GamepadBridge could not see it.
void ArmHandCalibration() {
    if(!controllerHands)return;
    // GAMEPLAY ONLY. This had exactly one check - controllerHands - and nothing
    // else, which made a destructive action reachable from a menu.
    //
    // Mark worked this out on 2026-10-14: the game needs A in the opening menus
    // to reach the loading screen, and his RightATap is HandCalibrate. So A
    // armed it, and then it captured the moment he held still - which is what
    // anyone does during a loading screen. That is almost certainly the source
    // of the calibrations that wrecked his hand placement, and the file it
    // overwrote had stood since 10/11 with no backup.
    //
    // Same three flags the weapon pose already gates on, so this adds no new
    // notion of "in gameplay" - it uses the one the mod already trusts.
    if(!requested || screenReasons || gameMenuOpen) {
        FILE* f{};
        if(!fopen_s(&f,"DeusExHRVR-camera.log","a") && f) {
            fprintf(f,"handCalibrate REFUSED (not in gameplay) requested=%d screens=%u menu=%d\n",
                int(requested),screenReasons,int(gameMenuOpen));
            fclose(f);
        }
        return;
    }
    // exchange, so a held button cannot keep pushing the arming time forward
    // and starve the steady hold that has to follow it.
    if(handCalibrateArmed.exchange(1))return;
    handCalibrateArmTick.store(GetTickCount64());
    handArmGestures.fetch_add(1);
    MessageBeep(MB_ICONASTERISK);
    FILE* f{};
    if(!fopen_s(&f,"DeusExHRVR-camera.log","a") && f) {
        fprintf(f,"handCalibrate armed by button\n");fclose(f);
    }
}
bool ToggleShaderSubstitution(){bool on=!shaderSwap.SubstitutionEnabled();shaderSwap.SetSubstitutionEnabled(on);return on;}
}
