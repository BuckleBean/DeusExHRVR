#include "HandDrawDiagnostic.h"
#include "WorldScaleConfig.h"
#include "SceneCache.h"
#include "SettingsMenuDraw.h"
#include "CameraMath.h"
#include "InteractionDiagnostics.h"
#include "SettingsPersistence.h"
#include "NativeMenuPointer.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

// Centre of the closed VR SETTINGS shortcut (top-right corner since Nov 15).
constexpr float EX=SettingsMenu::Entry.x+SettingsMenu::Entry.w*.5f,EY=SettingsMenu::Entry.y+SettingsMenu::Entry.h*.5f;
// Main page in tabs (Nov 17): centre of the snap-angle +, the turning, movement
// and recenter rows (Movement tab), and of each tab.
constexpr float PX=SettingsMenu::Plus.x+SettingsMenu::Plus.w*.5f,PY=SettingsMenu::Plus.y+SettingsMenu::Plus.h*.5f;
constexpr float TY=SettingsMenu::MainRowY(0)+25.f,MY=SettingsMenu::MainRowY(2)+25.f,RY=SettingsMenu::Recenter.y+25.f;
inline float TX(int i){return SettingsMenu::Tab(i).x+SettingsMenu::Tab(i).w*.5f;}
inline float TabY(){return SettingsMenu::Tab(0).y+SettingsMenu::Tab(0).h*.5f;}

int wmain(int argc,wchar_t** argv) {
    // Disabled diagnostics must return before pointer reads, counters or locks.
    assert(!DiagnosticOptions::hands&&!DiagnosticOptions::runtime&&!DiagnosticOptions::liveHandConfig);
    HandMeshDiagnostic::Capture(nullptr,0);
    HandDrawDiagnostic::MarkPose(123,1);
    HandDrawDiagnostic::Observe(nullptr,nullptr,false,1);
    assert(HandMeshDiagnostic::seen.load()==0&&HandMeshDiagnostic::captured==0);
    assert(HandDrawDiagnostic::calls.load()==0&&HandDrawDiagnostic::poseCursor==0);
    // Opt-in still collects; ReadProcessMemory rejects null safely.
    DiagnosticOptions::hands=true;HandMeshDiagnostic::Capture(nullptr,0);
    HandDrawDiagnostic::MarkPose(123,1);
    assert(HandMeshDiagnostic::seen.load()==1&&HandDrawDiagnostic::poseCursor==1);
    DiagnosticOptions::hands=false;
    // Production scale handoff: requests never mutate an already-built view.
    // Cached eyes, head and controller must all retain the old units until
    // the camera builds a complete new snapshot, including multiple requests.
    LiveWorldScale scale;scale.Request(300);assert(scale.BeginCamera()==300);
    struct View {float scale;float padding[3]{};CameraMath::Matrix head,hand;};
    SceneCache<View> cache;
    auto base=CameraMath::Rotation({0,0,0,1});
    CameraMath::Pose reference{},head{},hand{};
    reference.orientation.w=head.orientation.w=hand.orientation.w=1;
    head.position.x=.2f;hand.position.x=.5f;
    auto view=[&](float units){return View{units,{},CameraMath::HeadWorld(base,reference,head,units),CameraMath::HeadWorld(base,reference,hand,units)};};
    int scene=0;cache.Store(&scene,view(scale.Active()),1);
    scale.Request(100);scale.Request(200);
    assert(scale.Active()==300&&scale.Configured()==200);
    auto old=cache.Find(&scene);assert(old.scale==300&&std::abs(old.head.m[12]-60)<.001&&old.hand.m[12]==150);
    auto fresh=view(scale.BeginCamera());
    assert(fresh.scale==200&&std::abs(fresh.head.m[12]-40)<.001&&fresh.hand.m[12]==100);
    assert(cache.Find(&scene).scale==300); // a queued scene still uses its own stereo scale
    Transport::Tracking eyes{};eyes.head.orientation.w=1;
    eyes.eyes[0].pose.orientation.w=eyes.eyes[1].pose.orientation.w=1;
    eyes.eyes[0].pose.position.x=-.032f;eyes.eyes[1].pose.position.x=.032f;
    auto left=CameraMath::EyeWorld(eyes,0,old.scale),right=CameraMath::EyeWorld(eyes,1,old.scale);
    assert(std::abs(std::abs(right.m[12]-left.m[12])-19.2f)<.001);
    left=CameraMath::EyeWorld(eyes,0,fresh.scale);right=CameraMath::EyeWorld(eyes,1,fresh.scale);
    assert(std::abs(std::abs(right.m[12]-left.m[12])-12.8f)<.001);
    for(float bad:{0.f,1001.f,std::numeric_limits<float>::quiet_NaN()})scale.Request(bad);
    assert(scale.Configured()==200&&scale.BeginCamera()==200);
    scale.Request(300);assert(scale.BeginCamera()==300); // reversible without restarting
    assert(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    // Exercise the exact Win32 reader called by EngineCamera::Install, using
    // scratch INIs only. Existing valid choices must survive a default change.
    const auto config=std::filesystem::absolute(out/L"world-scale-default-test.ini");
    auto readScale=[&](const char* entry,float expected){
        {std::ofstream f(config,std::ios::binary);f<<"[VR]\n"<<entry<<"\n";}
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,config.c_str());
        const auto value=WorldScaleConfig::Read(config.c_str());
        LiveWorldScale loaded;loaded.Request(value);
        assert(value==expected&&loaded.BeginCamera()==expected);
    };
    LiveWorldScale defaults;assert(defaults.Configured()==300&&defaults.Active()==300);
    readScale("",300);
    for(const char* invalid:{"", "garbage", "0", "1001", "nan", "inf"}){
        const auto entry=std::string("WorldUnitsPerMetre=")+invalid;
        readScale(entry.c_str(),300);
    }
    readScale("WorldUnitsPerMetre=100",100);
    readScale("WorldUnitsPerMetre=300",300);
    readScale("WorldUnitsPerMetre=275.5",275.5f);
    readScale("WorldUnitsPerMetre=10",10);
    readScale("WorldUnitsPerMetre=1000",1000);
    std::filesystem::remove(config);
    assert(WorldScaleConfig::Read(config.c_str())==300); // absent INI
    Transport::Header h{};h.magic=Transport::Magic;h.version=9;h.pid=42;
    assert(!Transport::Compatible(&h,42));h.version=Transport::Version;assert(Transport::Compatible(&h,42));
    assert(Transport::LockMenu(&h));Transport::MenuState read{};assert(!Transport::ReadMenu(&h,read));Transport::UnlockMenu(&h);
    assert(Transport::ReadMenu(&h,read));assert(Transport::Fresh(100,599));assert(!Transport::Fresh(100,600));assert(!Transport::Fresh(100,99));
    assert(SettingsMenu::Eligible(2));assert(SettingsMenu::Eligible(2|32));
    for(unsigned reason:{0u,1u,4u,8u,16u,32u,64u,128u,2u|4u,2u|16u})assert(!SettingsMenu::Eligible(reason));
    // Countdown preserves drafts, fires once, and cancels on focus/screen loss.
    for(int cancel=0;cancel<4;++cancel){
        SettingsMenu::State rc;Transport::MenuState gm{};GamepadMapping::Input input{};input.active=true;
        rc.open=true;rc.draft=45;rc.original=30;
        rc.Update(true,true,true,1000,RY,input,gm,100);
        input.rightTrigger=1;rc.Update(true,true,true,1000,RY,input,gm,200);
        assert(rc.recenterAt==3200&&rc.recenterSeconds==3&&!rc.request);
        input.rightTrigger=0;rc.Update(true,true,false,0,0,input,gm,1200);
        assert(rc.recenterSeconds==2&&!rc.recenterPulse);
        if(cancel==1)input.menu=true;
        rc.Update(cancel!=2,cancel!=3,false,0,0,input,gm,2200);
        rc.Update(cancel!=2,cancel!=3,false,0,0,input,gm,3200);
        assert(rc.recenterPulse==(cancel==0)&&!rc.recenterAt&&!rc.request);
        if(!cancel)assert(rc.draft==45&&rc.Dirty());
        rc.Update(true,true,false,0,0,input,gm,3300);assert(!rc.recenterPulse);
    }
    for(float heading : {0.f,.5f,1.5f,3.f}){
        float previous=0;
        for(int n=0;n<=1000;++n){
            float mag=n/1000.f,x=mag*std::cos(heading),y=mag*std::sin(heading);
            auto normal=CameraMath::MovementResponse(x,y,false);assert(normal.strafe==x&&normal.walk==y);
            auto gentle=CameraMath::MovementResponse(x,y,true);float length=std::hypot(gentle.strafe,gentle.walk);
            assert(length+1e-6f>=previous&&length<=mag+1e-6f);
            assert(std::abs(x*gentle.walk-y*gentle.strafe)<1e-6f);
            if(n==1000)assert(std::abs(length-1)<1e-6f);previous=length;
        }
    }
    // Dense radial sweep checks direction and monotonicity for the new curve.
    for(float heading:{0.f,.5f,1.5f,3.f}){
        float previous=0;
        for(int n=0;n<=1000;++n){
            float r=n/1000.f,x=r*std::cos(heading),y=r*std::sin(heading);
            auto v=CameraMath::MovementResponse(x,y,2);float length=std::hypot(v.strafe,v.walk);
            assert(std::isfinite(length)&&length+.00001f>=previous&&length<=1.00001f);
            if(r>=.2f&&r<=.8f)assert(length>=.19999f&&length<=.55001f);
            assert(length-previous<.0031f);
            assert(std::abs(x*v.walk-y*v.strafe)<.00001f);previous=length;
        }
    }
    for(auto point:{CameraMath::MovementAxes{0,0},{.2f,.2f},{.8f,.55f},{1,1}}){
        auto v=CameraMath::MovementResponse(0,point.strafe,2);
        assert(std::abs(v.walk-point.walk)<.00001f&&v.strafe==0);
    }
    for(float knot:{.2f,.8f}){
        const float h=.0001f;
        auto f=[](float r){return CameraMath::MovementResponse(0,r,2).walk;};
        assert(std::abs((f(knot)-f(knot-h))/h-(f(knot+h)-f(knot))/h)<.02f);
    }
    // Exact center remains finite; tiny movements do not introduce a new deadzone.
    assert(CameraMath::MovementResponse(0,0,2).walk==0);
    assert(CameraMath::MovementResponse(0,1e-6f,2).walk>0);
    auto full=CameraMath::MovementResponse(1,1,2);assert(full.strafe==1&&full.walk==1);
    auto invalid=CameraMath::MovementResponse(.2f,.3f,3);assert(invalid.strafe==.2f&&invalid.walk==.3f);
    auto diagonal=CameraMath::MovementResponse(1,1,true);assert(diagonal.strafe==1&&diagonal.walk==1);
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;GamepadMapping::Input input{};input.active=true;
     ui.Update(true,true,true,EX,EY,input,gm);input.rightTrigger=1;ui.Update(true,true,true,EX,EY,input,gm);
     input.rightTrigger=0;ui.Update(true,true,true,1000,float(SettingsMenu::Response.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,1000,float(SettingsMenu::Response.y+25),input,gm);
     assert(ui.draftResponse==1&&ui.Dirty()&&!ui.request);
     ui.Submit();assert(ui.requestedResponse==1);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.interaction=2;
     gm.bindings=ButtonBindings::Defaults;gm.bindings[0]=XINPUT_GAMEPAD_B;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);assert(ui.draftInteraction==2&&ui.tab==SettingsMenu::TabMovement);
     click(SettingsMenu::Interaction);assert(ui.draftInteraction==2); // Movement tab shown: that spot is Turning
     ui.draftSnap=ui.originalSnap;click(SettingsMenu::Tab(SettingsMenu::TabControls));assert(ui.tab==SettingsMenu::TabControls&&!ui.Dirty());
     click(SettingsMenu::Interaction);assert(ui.draftInteraction==1&&!ui.request);
     click(SettingsMenu::Remap);click(SettingsMenu::ResetBindings);assert(ui.resetConfirm);
     click(SettingsMenu::Cancel);assert(!ui.resetConfirm&&ui.draftBindings==gm.bindings);
     click(SettingsMenu::ResetBindings);click(SettingsMenu::Confirm);
     assert(ui.draftReset&&ui.draftBindings==ButtonBindings::Defaults&&!ui.request);
     click(SettingsMenu::ListDiscard);assert(!ui.open&&!ui.request);
     click(SettingsMenu::Entry);assert(!ui.draftReset&&ui.draftBindings==gm.bindings&&ui.draftInteraction==2);
     click(SettingsMenu::Remap);click(SettingsMenu::ResetBindings);click(SettingsMenu::Confirm);click(SettingsMenu::ListSave);
     assert(ui.requestedReset&&ui.requestedBindingMask==((1u<<ButtonBindings::Count)-1));
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);assert(ui.draftScale==300&&!ui.Dirty());click(SettingsMenu::WorldScale);assert(ui.scalePage);
     click(SettingsMenu::ScaleMinus);assert(ui.draftScale==290&&!ui.request);
     click(SettingsMenu::Back);assert(!ui.scalePage&&ui.open&&ui.draftScale==290);
     click(SettingsMenu::Discard);click(SettingsMenu::Entry);assert(ui.draftScale==300);
     click(SettingsMenu::WorldScale);click(SettingsMenu::ScalePlus);assert(ui.draftScale==310);
     click(SettingsMenu::ScaleReset);assert(ui.draftScale==300);ui.draftScale=999;click(SettingsMenu::ScalePlus);assert(ui.draftScale==1000);
     ui.draftScale=11;click(SettingsMenu::ScaleMinus);assert(ui.draftScale==10);
     click(SettingsMenu::ScaleReset);click(SettingsMenu::ScaleMinus);click(SettingsMenu::Save);
     assert(ui.requestedScale==290&&ui.saving&&gm.activeWorldScale==300);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.scopeZoom=4;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);assert(ui.draftZoom==4&&!ui.Dirty());click(SettingsMenu::Tab(SettingsMenu::TabView));
     click(SettingsMenu::ZoomMinus);assert(ui.draftZoom==3.5f&&ui.Dirty()&&!ui.request);
     click(SettingsMenu::ZoomPlus);click(SettingsMenu::ZoomPlus);assert(ui.draftZoom==4.5f);
     ui.draftZoom=12;click(SettingsMenu::ZoomPlus);assert(ui.draftZoom==12);ui.draftZoom=3;click(SettingsMenu::ZoomMinus);assert(ui.draftZoom==3);
     ui.draftZoom=2.5f;click(SettingsMenu::ZoomMinus);assert(ui.draftZoom==2.5f);click(SettingsMenu::ZoomPlus);assert(ui.draftZoom==3); // INI value below the menu floor
     ui.draftZoom=1;click(SettingsMenu::ZoomPlus);assert(ui.draftZoom==3);ui.draftZoom=3.5f;
     click(SettingsMenu::Discard);assert(!ui.open);click(SettingsMenu::Entry);assert(ui.draftZoom==4);
     click(SettingsMenu::ZoomMinus);click(SettingsMenu::ZoomMinus);click(SettingsMenu::Save);assert(ui.requestedZoom==3&&ui.saving);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.handHud=gm.activeHandHud=2;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);assert(ui.draftHandHud&&ui.handHudAvailable&&!ui.Dirty());click(SettingsMenu::Tab(SettingsMenu::TabView));
     click(SettingsMenu::HandHud);assert(!ui.draftHandHud&&ui.Dirty()&&!ui.request);
     click(SettingsMenu::HandHud);assert(ui.draftHandHud&&!ui.Dirty());
     click(SettingsMenu::HandHud);click(SettingsMenu::Discard);click(SettingsMenu::Entry);assert(ui.draftHandHud);
     click(SettingsMenu::HandHud);click(SettingsMenu::Save);assert(ui.requestedHandHud==1&&ui.saving);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.response=1;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);click(SettingsMenu::Tab(SettingsMenu::TabView));click(SettingsMenu::HandHud);assert(!ui.handHudAvailable&&!ui.Dirty()); // older DLL: row inert
     click(SettingsMenu::Tab(SettingsMenu::TabMovement));click(SettingsMenu::Response);click(SettingsMenu::Save);assert(ui.requestedHandHud==0); // unchanged hand HUD is not sent
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.response=1;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);click(SettingsMenu::Tab(SettingsMenu::TabView));click(SettingsMenu::ZoomPlus);assert(ui.draftZoom==0&&!ui.Dirty()); // older DLL: no zoom, row inert
     click(SettingsMenu::Tab(SettingsMenu::TabMovement));click(SettingsMenu::Response);click(SettingsMenu::Save);assert(ui.requestedZoom==0); // unchanged zoom is not sent
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=300;gm.response=1;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect r){input.rightTrigger=0;ui.Update(true,true,true,float(r.x+20),float(r.y+20),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(r.x+20),float(r.y+20),input,gm);};
     click(SettingsMenu::Entry);assert(ui.draftResponse==1);
     click(SettingsMenu::Response);assert(ui.draftResponse==2&&ui.Dirty());
     click(SettingsMenu::Discard);assert(!ui.open&&!ui.request);
     click(SettingsMenu::Entry);assert(ui.draftResponse==1);
     click(SettingsMenu::Response);click(SettingsMenu::Response);assert(ui.draftResponse==0);
     click(SettingsMenu::Response);assert(ui.draftResponse==1);
     click(SettingsMenu::Response);click(SettingsMenu::Save);assert(ui.requestedResponse==2&&ui.saving);
    }
    // Menu tabs (Nov 17): rows act only on their own tab; one Save covers all tabs.
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;
     gm.throwAim=3;gm.throwMarker=2;gm.coverFollow=2;gm.immersiveScope=1;gm.handHud=gm.activeHandHud=2;gm.scopeZoom=4;gm.dialogueAvailable=1;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     static_assert(SettingsMenu::Tab(2).x+SettingsMenu::Tab(2).w<=SettingsMenu::Panel.x+SettingsMenu::Panel.w-40&&SettingsMenu::Tab(0).y>=SettingsMenu::Panel.y+60);
     static_assert(SettingsMenu::MainRowY(5)+52<SettingsMenu::StatusY&&SettingsMenu::StatusY+40<SettingsMenu::ListBack.y); // six rows, then the status line, then the buttons
     static_assert(SettingsMenu::Tab(0).y+SettingsMenu::Tab(0).h+9<SettingsMenu::MainRowY(0)); // tab bar clear of the first row
     click(SettingsMenu::Entry);assert(ui.originalThrowAim==3&&ui.throwMarkerAvailable&&ui.draftThrowMarker&&ui.coverFollowAvailable&&ui.draftCoverFollow&&ui.scopeAvailable&&!ui.draftScope&&!ui.Dirty());
     click(SettingsMenu::ThrowAimRow);assert(ui.draftThrowAim==3&&!ui.Dirty()); // Movement tab shown: that spot is the snap angle (inactive here)
     ui.draftSnap=ui.originalSnap;ui.draft=ui.original;
     click(SettingsMenu::Tab(SettingsMenu::TabControls));
     click(SettingsMenu::ThrowAimRow);assert(ui.draftThrowAim==4&&ui.Dirty()&&!ui.request); // right -> left
     click(SettingsMenu::ThrowAimRow);assert(ui.draftThrowAim==2);                          // -> head
     click(SettingsMenu::ThrowAimRow);assert(ui.draftThrowAim==1);                          // -> the game's own
     click(SettingsMenu::ThrowAimRow);assert(ui.draftThrowAim==3&&!ui.Dirty());             // -> right again
     click(SettingsMenu::ThrowMarkerRow);assert(!ui.draftThrowMarker&&ui.Dirty());
     click(SettingsMenu::Tab(SettingsMenu::TabView));
     click(SettingsMenu::ScopeRow);assert(ui.draftScope);
     click(SettingsMenu::CoverFollowRow);assert(!ui.draftCoverFollow);
     click(SettingsMenu::Discard);assert(!ui.open&&!ui.request);
     click(SettingsMenu::Entry);assert(ui.tab==SettingsMenu::TabView&&ui.draftThrowMarker&&!ui.draftScope&&ui.draftCoverFollow&&!ui.Dirty()); // tab remembered, drafts reset
     click(SettingsMenu::ScopeRow);click(SettingsMenu::Tab(SettingsMenu::TabControls));click(SettingsMenu::ThrowAimRow);click(SettingsMenu::ThrowAimRow);
     click(SettingsMenu::Save);
     assert(ui.saving&&ui.requestedImmersiveScope==2&&ui.requestedThrowAim==2&&ui.requestedThrowMarker==0&&ui.requestedCoverFollow==0);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.response=1; // older DLL: rows inert, nothing sent
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);click(SettingsMenu::Tab(SettingsMenu::TabControls));click(SettingsMenu::ThrowAimRow);click(SettingsMenu::ThrowMarkerRow);
     click(SettingsMenu::Tab(SettingsMenu::TabView));click(SettingsMenu::ScopeRow);click(SettingsMenu::CoverFollowRow);assert(!ui.Dirty());
     click(SettingsMenu::Tab(SettingsMenu::TabMovement));click(SettingsMenu::Response);click(SettingsMenu::Save);
     assert(ui.saving&&!ui.requestedThrowAim&&!ui.requestedThrowMarker&&!ui.requestedCoverFollow&&!ui.requestedImmersiveScope);
    }
    // Crosshair row: cycles on the View tab, one Save, nothing sent when unchanged.
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300;gm.crosshair=CoverCrosshair::Third;
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     static_assert(SettingsMenu::CrosshairRow.y+SettingsMenu::CrosshairRow.h<SettingsMenu::StatusY);
     click(SettingsMenu::Entry);click(SettingsMenu::Tab(SettingsMenu::TabView));assert(ui.draftCrosshair==CoverCrosshair::Third&&!ui.Dirty());
     click(SettingsMenu::CrosshairRow);assert(ui.draftCrosshair==CoverCrosshair::Both&&ui.Dirty());
     click(SettingsMenu::CrosshairRow);assert(ui.draftCrosshair==CoverCrosshair::Off);
     click(SettingsMenu::CrosshairRow);assert(ui.draftCrosshair==CoverCrosshair::First);
     click(SettingsMenu::CrosshairRow);assert(ui.draftCrosshair==CoverCrosshair::Third&&!ui.Dirty());
     click(SettingsMenu::CrosshairRow);click(SettingsMenu::CrosshairRow);click(SettingsMenu::Save);assert(ui.saving&&ui.requestedCrosshair==CoverCrosshair::Off);
    }
    {SettingsMenu::State ui;Transport::MenuState gm{};gm.degrees=30;gm.worldScale=gm.activeWorldScale=300; // unavailable: inert
     GamepadMapping::Input input{};input.active=true;
     auto click=[&](SettingsMenu::Rect rect){input.rightTrigger=0;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);input.rightTrigger=1;ui.Update(true,true,true,float(rect.x+30),float(rect.y+25),input,gm);};
     click(SettingsMenu::Entry);click(SettingsMenu::Tab(SettingsMenu::TabView));click(SettingsMenu::CrosshairRow);assert(!ui.draftCrosshair&&!ui.Dirty());
    }
    float degrees=30;double counts=300;
    // Validate the diagnostic's native frame offsets and failed-read handling.
    {
        alignas(16) unsigned char frame[0xe0]{};
        struct Descriptor {uint32_t type;float reach;} descriptor{9,2.f};
        float origin[3]{1,2,3},hit[3]{1,2,303},extra=40000;
        memcpy(frame+0x60,origin,12);memcpy(frame+0xd0,hit,12);
        memcpy(frame+0x28,&extra,4);frame[0x13]=1;
        InteractionDiagnostics::Hit sample;
        assert(InteractionDiagnostics::ReadHit(&sample,frame,&descriptor));
        assert(sample.type==9&&sample.extended&&sample.origin[2]==3&&sample.point[2]==303);
        assert(std::abs(sample.range-632.10004f)<.001f&&sample.extraRangeSquared==extra);
        descriptor.reach=std::numeric_limits<float>::quiet_NaN();
        assert(!InteractionDiagnostics::ReadHit(&sample,frame,&descriptor));
        assert(!InteractionDiagnostics::ReadHit(&sample,frame,nullptr)&&!sample.readable);
    }
    assert(SettingsMenu::ApplyAngle(45,degrees,counts)&&degrees==45&&counts==450);
    assert(SettingsMenu::ApplyAngle(30,degrees,counts)&&counts==300);
    assert(!SettingsMenu::ApplyAngle(181,degrees,counts));assert(!SettingsMenu::ApplyAngle(std::numeric_limits<float>::quiet_NaN(),degrees,counts));
    SettingsMenu::InputGate gate;gate.latest.hostTick=100;gate.latest.blocked=1;
    assert(gate.Blocked(599));assert(!gate.Blocked(600)); // cached lock contention/focus expiry
    XINPUT_GAMEPAD physical{};physical.wButtons=XINPUT_GAMEPAD_A;
    assert(gate.Physical(true,physical));assert(gate.Physical(false,physical));
    physical={};assert(!gate.Physical(false,physical));
    {   // The panel sits 1 cm in front of the game screen, toward the viewer, whatever the screen's heading (Nov 18).
        for(float yaw:{0.f,.7f,-2.f,3.1f}){
            Transport::Pose screen{{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{-2.f*std::sin(yaw),1.6f,-2.f*std::cos(yaw)}};
            auto p=SettingsMenu::PanelPose(screen);
            const float dx=p.position.x-screen.position.x,dy=p.position.y-screen.position.y,dz=p.position.z-screen.position.z;
            assert(std::abs(std::hypot(dx,dy,dz)-SettingsMenu::PanelLift)<1e-5f&&std::abs(dy)<1e-6f);
            assert(std::hypot(p.position.x,p.position.z)<std::hypot(screen.position.x,screen.position.z)); // nearer the viewer at the origin
            assert(p.orientation.y==screen.orientation.y&&p.orientation.w==screen.orientation.w);
            Transport::Pose eye{{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{0,1.6f,0}};float px{},py{};
            assert(SettingsMenu::Hit(eye,p,px,py)&&std::abs(px-800)<.01f&&std::abs(py-450)<.01f); // still centred on the lifted plane
        }
    }
    Transport::Pose panel{{0,0,0,1},{0,0,-2}},aim{{0,0,0,1},{0,0,0}};float x{},y{};
    assert(SettingsMenu::Hit(aim,panel,x,y));assert(std::abs(x-800)<.01&&std::abs(y-450)<.01);
    aim.position.x=.8f;assert(SettingsMenu::Hit(aim,panel,x,y));assert(std::abs(x-1200)<.01);
    aim.orientation={0,1,0,0};assert(!SettingsMenu::Hit(aim,panel,x,y));aim.orientation={0,0,0,1};aim.position.z=-3;assert(!SettingsMenu::Hit(aim,panel,x,y));
    SettingsMenu::State s;GamepadMapping::Input in{};in.active=true;Transport::MenuState game{};game.snapEnabled=1;game.degrees=30;
    auto update=[&](float px,float py){s.Update(true,true,true,px,py,in,game);};
    update(EX,EY);assert(!s.open&&!s.blocked);
    in.rightTrigger=.1f;update(EX,EY);assert(s.blocked&&!s.open);
    in.rightTrigger=.8f;update(EX,EY);assert(s.open&&s.blocked);
    // Edits stay local, including rapid clicks, until explicit save.
    auto click=[&](float px,float py){in.rightTrigger=0;update(px,py);in.rightTrigger=.8f;update(px,py);};
    update(PX,PY);assert(!s.request);
    for(int n=0;n<4;++n)click(PX,PY);
    assert(s.draft==90&&s.request==0&&game.degrees==30);
    click(700,820);assert(!s.open&&s.request==0); // discard
    click(EX,EY);assert(s.open&&s.draft==30);
    click(PX,PY);click(1100,820);
    assert(s.open&&s.saving&&s.request==1&&s.requested==45);
    click(PX,PY);assert(s.request==1&&s.draft==45); // pending blocks edits
    game.acknowledged=game.saved=1;game.result=0;game.degrees=45;
    in.rightTrigger=0;update(1100,820);assert(!s.open&&!s.saving);
    click(EX,EY);click(PX,PY);click(1100,820);
    game.acknowledged=2;game.result=3;update(1100,820);
    assert(s.open&&!s.saving&&s.committed); // failure stays open, no false success
    click(1100,820);assert(s.request==3&&s.saving); // explicit retry
    game.acknowledged=game.saved=3;game.result=0;game.degrees=60;
    in.rightTrigger=0;update(1100,820);assert(!s.open);
    click(EX,EY);click(PX,PY);
    s.Update(false,false,false,0,0,in,game);assert(!s.open&&s.draft==s.original&&s.request==3);
    update(EX,EY);assert(!s.open); // held trigger on refocus
    click(EX,EY);assert(s.open&&s.draft==60);
    in.rightTrigger=0;update(1000,PY);in.rightX=.8f;update(1000,PY);
    assert(s.draft==75&&s.request==3);update(1000,PY);assert(s.draft==75);
    in.rightX=0;update(1000,PY);in.rightX=-.8f;update(1000,PY);assert(s.draft==60);
    // Another tab shown: the stick does not edit the angle.
    in.rightX=0;click(TX(SettingsMenu::TabView),TabY());in.rightTrigger=0;update(1000,PY);in.rightX=.8f;update(1000,PY);assert(s.draft==60);
    in.rightX=0;click(TX(SettingsMenu::TabMovement),TabY());in.rightTrigger=0;update(1000,PY);
    // Angle shortcut cannot accidentally edit while pointing at another setting.
    in.rightX=0;update(1000,float(SettingsMenu::MainRowY(4)+25));in.rightX=.8f;update(1000,float(SettingsMenu::MainRowY(4)+25));assert(s.draft==60);
    // The centered overlay blocks the native menu even outside its panel.
    in.rightX=0;in.rightTrigger=0;update(80,450);in.rightTrigger=.8f;update(80,450);
    assert(s.open&&s.blocked&&!s.nativeDown);
    // Mode edits are drafts; smooth preserves angle and disables angle edits.
    s={};in={};in.active=true;game={};game.snapEnabled=1;game.degrees=30;
    click(EX,EY);click(1000,TY);assert(!s.draftSnap&&s.request==0);
    click(PX,PY);assert(s.draft==30);click(700,820);assert(!s.open&&s.request==0);
    click(EX,EY);assert(s.draftSnap);click(1000,TY);click(1100,820);
    assert(s.saving&&s.request==1&&!s.requestedSnap&&s.requested==30);
    game.acknowledged=game.saved=1;game.snapEnabled=0;in.rightTrigger=0;update(1100,820);
    click(EX,EY);assert(!s.draftSnap);click(1000,TY);click(PX,PY);
    assert(s.draftSnap&&s.draft==45);click(1100,820);assert(s.requestedSnap&&s.requested==45);
    // Movement choices never offer native mode; discard and save remain explicit.
    s={};in={};in.active=true;game={};game.snapEnabled=1;game.degrees=30;game.movement=1;
    click(EX,EY);click(1000,MY);assert(s.draftMovement==3&&s.request==0);
    click(700,820);click(EX,EY);assert(s.draftMovement==1);
    click(1000,MY);click(1100,820);assert(s.requestedMovement==3&&s.saving);
    game.acknowledged=game.saved=s.request;game.movement=3;in.rightTrigger=0;update(1100,820);
    click(EX,EY);click(1000,MY);assert(s.draftMovement==2);click(1000,MY);assert(s.draftMovement==1);
    Transport::Tracking directions{};directions.leftController.valid=1;directions.leftController.aim.position.x=-1;
    directions.rightController.valid=1;directions.rightController.aim.position.x=1;
    assert(DirectionConfig::Aim(DirectionConfig::Source::LeftController,directions)->aim.position.x==-1);
    assert(DirectionConfig::Aim(DirectionConfig::Source::Controller,directions)->aim.position.x==1);
    directions.leftController.valid=0;assert(!DirectionConfig::Aim(DirectionConfig::Source::LeftController,directions));
    assert(DirectionConfig::Aim(DirectionConfig::Source::Controller,directions));
    assert(DirectionConfig::Parse(L"LeftController",DirectionConfig::Source::Mouse)==DirectionConfig::Source::LeftController);
    // Action list: selection click is not captured; capture requires release,
    // confirmation is a draft, and occupied controls report the displaced action.
    s={};in={};in.active=true;game={};game.degrees=30;game.snapEnabled=1;
    game.bindings[0]=XINPUT_GAMEPAD_RIGHT_SHOULDER;game.holdMs[0]=300;game.bindings[10]=1u<<19;
    game.bindings[2]=XINPUT_GAMEPAD_START;game.holdMs[2]=500;game.bindings[12]=1u<<18;
    game.bindings[4]=game.bindings[3]=XINPUT_GAMEPAD_Y;game.bindings[18]=XINPUT_GAMEPAD_A;
    click(EX,EY);click(TX(SettingsMenu::TabControls),TabY());click(1000,float(SettingsMenu::Remap.y+25));assert(s.remapping&&s.Area().x==180);
    click(1000,240);assert(s.listening&&!s.captureReady&&!s.confirming); // Jump row
    update(1000,240);assert(s.listening);in.rightTrigger=0;update(1000,240);assert(s.captureReady);
    in.b=true;update(1000,240);assert(s.pressedSlot==1);in.b=false;update(1000,240);
    assert(s.confirming&&s.candidate==1&&s.draftBindings==game.bindings&&!s.request);
    click(1100,820);assert(!s.confirming&&s.draftBindings[1]==XINPUT_GAMEPAD_A&&!s.draftBindings[18]);
    assert(!s.nativeDown&&s.blocked);click(700,820);assert(!s.open&&!s.request); // discard
    click(EX,EY);click(TX(SettingsMenu::TabControls),TabY());click(1000,float(SettingsMenu::Remap.y+25));assert(s.draftBindings==game.bindings);
    // Capturing a configured tap preserves its hold assignment.
    click(1000,240);in.rightTrigger=0;s.Update(true,true,true,1000,240,in,game,1000);
    in.x=true;s.Update(true,true,true,1000,240,in,game,1100);
    in.x=false;s.Update(true,true,true,1000,240,in,game,1200);
    assert(s.confirming&&s.candidate==12);click(1100,820);
    assert(s.draftBindings[12]==XINPUT_GAMEPAD_A&&s.draftBindings[2]==XINPUT_GAMEPAD_START);
    click(1100,820);assert(s.saving&&(s.requestedBindingMask&(1u<<12))&&(s.requestedBindingMask&(1u<<18)));
    game.acknowledged=game.saved=s.request;in.rightTrigger=0;update(1100,820);assert(!s.open);
    // Existing duplicate assignments are visible and unchanged until confirmed.
    s={};in={};in.active=true;click(EX,EY);click(TX(SettingsMenu::TabControls),TabY());click(1000,float(SettingsMenu::Remap.y+25));
    auto duplicates=SettingsMenu::Assigned(XINPUT_GAMEPAD_Y,s.draftBindings,game);
    assert(duplicates.find(L"LEFT Y")!=std::wstring::npos&&duplicates.find(L"LEFT GRIP")!=std::wstring::npos);
    click(1000,240);in.rightTrigger=0;update(1000,240);in.a=true;
    s.Update(true,true,true,1000,240,in,game,1000);in.a=false;s.Update(true,true,true,1000,240,in,game,1400);
    assert(s.confirming&&s.candidate==0);click(300,820);assert(!s.confirming&&s.draftBindings==game.bindings);
    // The pointer trigger can itself be captured, but cannot click through.
    click(1000,240);in.rightTrigger=0;update(1000,240);in.rightTrigger=.8f;update(1000,240);
    in.rightTrigger=0;update(1000,240);assert(s.confirming&&s.candidate==9&&!s.nativeDown);
    click(300,820);click(1000,240);in.rightTrigger=0;update(1000,240);
    in.leftClick=in.rightClick=true;update(1000,240);assert(!s.captureReady&&!s.confirming);
    in.leftClick=in.rightClick=false;update(1000,240);in.menu=true;update(1000,240);assert(!s.listening&&!s.confirming);
    in.menu=false;update(1000,240);
    click(1000,240);in.rightTrigger=0;update(1000,240);in.a=true;update(1000,240);
    in.a=false;in.active=false;update(1000,240);assert(!s.listening&&!s.confirming); // tracking/input loss cannot bind
    in.active=true;update(1000,240);
    click(1000,240);in.rightTrigger=0;update(1000,240);in.rightY=.8f;update(1000,240);
    in.rightY=0;update(1000,240);assert(s.confirming&&s.candidate==18);click(300,820);
    for(int i=0;i<20;++i)click(1350,650);assert(s.page==SettingsMenu::Actions-SettingsMenu::Rows);
    click(1000,240);s.Update(false,false,false,0,0,in,game);assert(!s.open&&!s.listening&&!s.confirming);
    // Native-menu hover and click route independently of the VR panel.
    s={};in={};in.active=true;update(800,450);assert(s.nativePointer&&!s.nativeDown&&!s.blocked);
    in.rightTrigger=.1f;update(800,450);assert(s.blocked&&!s.nativeDown);
    in.rightTrigger=.8f;update(800,450);assert(s.nativeDown&&s.blocked&&!s.open);
    update(EX,EY);assert(!s.open&&!s.nativeDown&&s.blocked); // drag cannot open VR panel
    in.rightTrigger=0;update(800,450);update(800,450);assert(!s.blocked&&s.nativePointer);
    s.Update(false,false,false,0,0,in,game);in.rightTrigger=.8f;update(800,450);assert(!s.nativeDown);
    in.rightTrigger=0;update(800,450);in.rightTrigger=.8f;update(800,450);assert(s.nativeDown);
    s={};in={};in.active=true;update(800,450);
    in.leftY=.8f;update(800,450);assert(!s.nativePointer&&!s.blocked);
    in.leftY=0;update(805,452);assert(!s.nativePointer); // tremor cannot steal focus
    update(850,450);assert(s.nativePointer); // deliberate aim resumes mouse
    in.leftY=.8f;update(850,450);in.leftY=0;in.rightTrigger=.1f;update(850,450);assert(s.nativePointer);
    in.rightTrigger=0;update(EX,EY);in.rightTrigger=.8f;update(EX,EY);
    assert(s.open&&s.nativePointer&&!s.nativeDown); // arrow moves over VR, click remains isolated
    // Shortcut in the top-right corner (Nov 15).
    static_assert(SettingsMenu::Entry.x+SettingsMenu::Entry.w<=SettingsMenu::Width-20&&SettingsMenu::Entry.y>=20); // inside the screen, with a margin
    static_assert(SettingsMenu::Entry.x>=SettingsMenu::Width*3/4&&SettingsMenu::Entry.y+SettingsMenu::Entry.h<=SettingsMenu::Height/10); // top-right corner
    // Clear of the game's Save/Load details column (authored at stage 805-1181 x 264-549 of 1280x720 = x1.25 here).
    static_assert(SettingsMenu::Entry.y+SettingsMenu::Entry.h<264*5/4);
    {   // Hit at the corner and still a normal game-menu click just below it.
        SettingsMenu::State st;GamepadMapping::Input pi{};pi.active=true;Transport::MenuState g{};g.degrees=30;
        auto at=[&](float px,float py){st.Update(true,true,true,px,py,pi,g);};
        at(EX,SettingsMenu::Entry.y+SettingsMenu::Entry.h+5.f);assert(st.nativePointer);
        pi.rightTrigger=.8f;at(EX,SettingsMenu::Entry.y+SettingsMenu::Entry.h+5.f);assert(st.nativeDown&&!st.open);
        pi.rightTrigger=0;at(EX,EY);at(EX,EY);pi.rightTrigger=.8f;at(EX,EY);assert(st.open&&!st.nativeDown);
        // Old spot is now an ordinary game-menu point.
        st={};pi={};pi.active=true;at(1200,450);pi.rightTrigger=.8f;at(1200,450);assert(st.nativeDown&&!st.open);
    }
    LONG dx{},dy{};
    assert(NativeMenuPointer::Position(.5f,.5f,{0,0,1920,1080},{0,0},{-1920,0,1920,1080},dx,dy));
    assert(dx>49000&&dx<49200&&dy>32700&&dy<32800);
    assert(!NativeMenuPointer::Position(1,0,{0,0,1920,1080},{0,0},{0,0,1920,1080},dx,dy));
    NativeMenuPointer::State mouse;DWORD flags{};bool success=true;
    auto send=[&](LONG,LONG,DWORD f){flags=f;return success;};
    mouse.Update(true,true,0,0,send);assert(!mouse.down); // held at entry
    mouse.Update(true,false,0,0,send);mouse.Update(true,true,0,0,send);
    assert(mouse.down&&(flags&MOUSEEVENTF_LEFTDOWN));
    flags=0;mouse.Update(true,true,0,0,send);assert(flags==0); // no redundant mouse movement
    success=false;mouse.Update(false,false,0,0,send);assert(mouse.down); // retry failed release
    success=true;mouse.Update(false,false,0,0,send);assert(!mouse.down&&(flags&MOUSEEVENTF_LEFTUP));
    mouse.Update(true,true,0,0,send);assert(!mouse.down); // no refocus click
    // Existing mappings retain their calibration and menu chords.
    GamepadMapping::Mapper mapper;in={};in.active=true;in.leftClick=in.rightClick=true;auto pad=mapper.Update(in,1);assert(!pad.buttons&&!pad.leftTrigger&&!pad.rightTrigger);
    mapper.Reset();in={};in.active=true;in.rightClick=in.x=true;pad=mapper.Update(in,1);assert(pad.buttons&XINPUT_GAMEPAD_BACK);
    auto ini=out/L"settings-test.ini";
    {std::ofstream f(ini,std::ios::binary);f<<"[VR]\r\n; preserve comment\r\nSnapTurnDegrees=30\r\nSnapTurnMouseCounts=300\r\n[Buttons]\r\nLeftX=Start\r\n";}
    assert(SettingsPersistence::Save(ini.wstring(),true,45,450));
    std::ifstream f(ini,std::ios::binary);std::string contents((std::istreambuf_iterator<char>(f)),{});
    assert(contents.starts_with("[VR]"));assert(contents.find("preserve comment")!=std::string::npos);assert(contents.find("LeftX=Start")!=std::string::npos);f.close();
    auto bom=out/L"bom-test.ini";{std::ofstream b(bom,std::ios::binary);b<<"\xef\xbb\xbf[VR]\r\n";}
    assert(!SettingsPersistence::Save(bom.wstring(),true,45,450));
    assert(!SettingsPersistence::Save((out/L"missing.ini").wstring(),true,45,450));
    assert(SettingsPersistence::Save(ini.wstring(),true,22.5f,225));
    // Rapid requests apply elsewhere immediately, but only the latest is persisted.
    auto& writer=SettingsWriter();
    writer.Queue(ini.c_str(),450,100,45);
    writer.Queue(ini.c_str(),600,101,60);
    assert(writer.completed.load()==0);
    assert(GetPrivateProfileIntW(L"VR",L"SnapTurnDegrees",0,ini.c_str())==22);
    for(int n=0;n<200&&writer.completed.load()!=101;++n)Sleep(10);
    assert(writer.completed.load()==101&&writer.failed.load()!=101);
    assert(GetPrivateProfileIntW(L"VR",L"SnapTurnDegrees",0,ini.c_str())==60);
    assert(GetPrivateProfileIntW(L"VR",L"SnapTurnMouseCounts",0,ini.c_str())==600);
    ButtonBindings::Layout bindings{};bindings[2]=XINPUT_GAMEPAD_A;
    writer.Queue(ini.c_str(),600,102,60,1,1,bindings,1u<<2);
    writer.Queue(ini.c_str(),601); // calibration cannot drop the pending layout
    for(int n=0;n<200&&writer.completed.load()!=102;++n)Sleep(10);
    assert(writer.completed.load()==102&&writer.failed.load()!=102);
    wchar_t target[32];GetPrivateProfileStringW(L"Buttons",L"LeftX",L"",target,32,ini.c_str());assert(!wcscmp(target,L"A"));
    assert(GetPrivateProfileIntW(L"VR",L"SnapTurnMouseCounts",0,ini.c_str())==601);
    SettingsMenuDraw draw;s={};s.visible=true;s.draftMovement=s.originalMovement=1;s.draftSnap=s.originalSnap=true;s.draft=s.original=30;game={};game.snapEnabled=1;game.degrees=30;
    assert(draw.Save((out/L"menu-entry.bmp").c_str(),s,game));
    auto entryPixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    assert(entryPixels[(SettingsMenu::Entry.y+SettingsMenu::Entry.h-2)*SettingsMenu::Width+SettingsMenu::Entry.x+2]==0);
    {   // Closed, not pointed at: only the corner button is drawn; its plate is translucent like
        // the game's PCButton base (alpha x0.75), its label and edge solid; the rest is see-through.
        const SettingsMenu::Rect around{SettingsMenu::Entry.x-2,SettingsMenu::Entry.y-2,SettingsMenu::Entry.w+4,SettingsMenu::Entry.h+4};
        static_assert(SettingsMenuDraw::ShortcutOpacity==.75f);
        auto lin=[](float f){return f<=.04045f?f/12.92f:std::pow((f+.055f)/1.055f,2.4f);};
        // sRGB swapchain alpha: the plate over a mid-grey background shows as in the game's encoded-value blend.
        assert(SettingsMenuDraw::ShortcutAlphaSrgb==unsigned((1.f-(lin((26.f+.25f*127.5f)/255.f)-lin(26.f/255.f))/lin(.5f))*255.f+.5f));
        assert(SettingsMenuDraw::ShortcutAlphaLinear==unsigned(.75f*255.f+.5f));
        static_assert(SettingsMenuDraw::Premultiply(34)==26&&SettingsMenuDraw::Premultiply(28)==21&&SettingsMenuDraw::Premultiply(15)==11);
        static_assert(SettingsMenuDraw::Premultiply(255)==191&&SettingsMenuDraw::Premultiply(0)==0);
        for(bool srgb : {true,false}){
            draw.srgb=srgb;entryPixels=static_cast<const uint32_t*>(draw.Draw(s,game));
            const uint32_t plateAlpha=srgb?SettingsMenuDraw::ShortcutAlphaSrgb:SettingsMenuDraw::ShortcutAlphaLinear;
            unsigned solid=0,translucent=0,label=0,edge=0;
            for(int py=0;py<SettingsMenu::Height;++py)for(int px=0;px<SettingsMenu::Width;++px){
                const auto v=entryPixels[py*SettingsMenu::Width+px];if(!v)continue;
                assert(around.Has(float(px),float(py)));
                const uint32_t a=v>>24;assert(a==255||a==plateAlpha);
                // Premultiplied: no colour channel above alpha (in linear light for sRGB).
                auto c=[&](int shift){float f=((v>>shift)&255)/255.f;return srgb?lin(f):f;};
                assert(c(16)<=a/255.f+1e-3f&&c(8)<=a/255.f+1e-3f&&c(0)<=a/255.f+1e-3f);
                if(a==255){++solid;if(((v>>16)&255)>180)++label;if((v&0xffffffu)==0x735922u)++edge;}else ++translucent;
            }
            assert(translucent>unsigned(SettingsMenu::Entry.w*SettingsMenu::Entry.h/2));
            assert(label>300&&edge>500&&solid<translucent);
            // Plain plate point (right of the label, clear of the hatch): RGB(34,28,15) x 0.75.
            assert(entryPixels[(SettingsMenu::Entry.y+SettingsMenu::Entry.h/2)*SettingsMenu::Width+SettingsMenu::Entry.x+SettingsMenu::Entry.w-40]==(plateAlpha<<24|26u<<16|21u<<8|11u));
            for(int py=420;py<490;++py)for(int px=1080;px<1490;++px)assert(entryPixels[py*SettingsMenu::Width+px]==0);
        }
        draw.srgb=true;entryPixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    }
    {   // Pointed at: the whole button is solid again, so the game's cursor under it stays hidden
        // behind the gold arrow exactly as before. No partly transparent pixel anywhere.
        auto pointed=s;pointed.pointer=true;pointed.x=SettingsMenu::Entry.x+SettingsMenu::Entry.w*.5f;pointed.y=SettingsMenu::Entry.y+SettingsMenu::Entry.h*.5f;
        auto p=static_cast<const uint32_t*>(draw.Draw(pointed,game));assert(p);
        unsigned solid=0;
        for(int i=0;i<SettingsMenu::Width*SettingsMenu::Height;++i){const uint32_t a=p[i]>>24;assert(a==0||a==255);if(a==255)++solid;}
        assert(solid>unsigned(SettingsMenu::Entry.w*SettingsMenu::Entry.h/2));
        assert(draw.Save((out/L"menu-entry-pointed.bmp").c_str(),pointed,game));
        entryPixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    }
    draw.Draw(s,game);
    assert(SettingsMenu::Panel.x==SettingsMenu::ListPanel.x&&SettingsMenu::Panel.w==SettingsMenu::ListPanel.w);s.open=true;s.pointer=true;s.x=PX;s.y=PY;
    game.dialogueAvailable=1;assert(draw.Save((out/L"menu-panel.bmp").c_str(),s,game));
    s.draftResponse=2;assert(draw.Save((out/L"menu-gentler.bmp").c_str(),s,game));s.draftResponse=0;
    s.draftDialogue=true;assert(draw.Save((out/L"menu-dialogue.bmp").c_str(),s,game));s.draftDialogue=false;
    auto pixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    assert(pixels[0]==0x90000000u);assert((pixels[300*SettingsMenu::Width+1050]>>24)==255);
    // Pointer remains visible during the approach, outside either clickable area.
    for(bool panelOpen : {false,true}){
        s.open=panelOpen;s.x=80;s.y=450;
        pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
        assert(pixels[450*SettingsMenu::Width+80]==(panelOpen?0x90000000u:0u)); // native arrow owns regular menu
        assert(pixels[440*SettingsMenu::Width+80]==(panelOpen?0x90000000u:0u));
    }
    s.x=PX;s.y=PY;
    // A successful GDI API call alone does not prove the labels reached the DIB.
    auto bright=[&](SettingsMenu::Rect rect){
        unsigned count=0;
        for(int py=rect.y;py<rect.y+rect.h;++py)for(int px=rect.x;px<rect.x+rect.w;++px)
            if(((pixels[py*SettingsMenu::Width+px]>>16)&255)>180)++count;
        return count;
    };
    for(int frame=0;frame<20;++frame){
        s.open=(frame%2)==0;pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
        if(s.open){
            assert(bright({240,60,1120,55})>500); // title text
            assert(bright({870,SettingsMenu::MainRowY(1),310,52})>100); // numeric value
            for(int i=0;i<SettingsMenu::Tabs;++i)assert(bright(SettingsMenu::Tab(i))>100); // tab labels
            assert(bright(SettingsMenu::Back)>100); // visible button and label
        }else assert(bright(SettingsMenu::Entry)>500);
    }
    s.open=true;s.draftScale=s.originalScale=game.worldScale=game.activeWorldScale=300;s.draftInteraction=s.originalInteraction=2;game.dialogueAvailable=1;assert(draw.Save((out/L"menu-panel.bmp").c_str(),s,game));
    s.draftResponse=2;assert(draw.Save((out/L"menu-gentler.bmp").c_str(),s,game));s.draftResponse=0;
    s.draftDialogue=true;assert(draw.Save((out/L"menu-dialogue.bmp").c_str(),s,game));s.draftDialogue=false;
    s.scalePage=true;assert(draw.Save((out/L"menu-world-scale.bmp").c_str(),s,game));s.scalePage=false;
    s.tab=SettingsMenu::TabView;game.scopeZoom=4;s.draftZoom=s.originalZoom=3;assert(draw.Save((out/L"menu-scope-zoom.bmp").c_str(),s,game));
    pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    assert(bright({870,SettingsMenu::MainRowY(2),310,52})>100&&bright(SettingsMenu::ZoomMinus)>5&&bright(SettingsMenu::ZoomPlus)>20); // "3x", "-", "+" (measured 299/14/52)
    game.handHud=game.activeHandHud=2;s.handHudAvailable=true;s.draftHandHud=s.originalHandHud=true;
    assert(draw.Save((out/L"menu-hand-hud.bmp").c_str(),s,game));pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    assert(bright(SettingsMenu::HandHud)>100&&bright({240,SettingsMenu::MainRowY(0),470,52})>100); // "HANDS" and its label
    s.draftHandHud=s.originalHandHud=false;game.handHud=1;
    assert(draw.Save((out/L"menu-hand-hud-restart.bmp").c_str(),s,game));pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    assert(bright({240,SettingsMenu::StatusY,1120,40})>100&&SettingsMenu::StatusY+40<SettingsMenu::ListBack.y); // restart note above the buttons
    // Previews of the three tabs with the new rows (Nov 17), as the headset would show them.
    game.handHud=game.activeHandHud=2;s.handHudAvailable=true;s.draftHandHud=s.originalHandHud=true;
    game.throwAim=3;game.throwMarker=2;game.coverFollow=2;game.immersiveScope=2;game.dialogueAvailable=1;
    s.LoadTabDrafts(game);s.x=80;s.y=450;
    s.tab=SettingsMenu::TabMovement;assert(draw.Save((out/L"menu-tab-movement.bmp").c_str(),s,game));
    s.tab=SettingsMenu::TabControls;assert(draw.Save((out/L"menu-tab-controls.bmp").c_str(),s,game));pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    for(auto r:{SettingsMenu::Interaction,SettingsMenu::ThrowAimRow,SettingsMenu::ThrowMarkerRow,SettingsMenu::Remap})assert(bright(r)>100);
    for(int i=0;i<4;++i)assert(bright({240,SettingsMenu::MainRowY(i),470,52})>100);
    s.tab=SettingsMenu::TabView;s.draftCrosshair=s.originalCrosshair=CoverCrosshair::Third;assert(draw.Save((out/L"menu-tab-view.bmp").c_str(),s,game));pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    for(auto r:{SettingsMenu::HandHud,SettingsMenu::ScopeRow,SettingsMenu::CoverFollowRow,SettingsMenu::Dialogue})assert(bright(r)>100);
    for(int i=0;i<5;++i)assert(bright({240,SettingsMenu::MainRowY(i),470,52})>100);
    s.draftThrowAim=1;s.tab=SettingsMenu::TabControls;assert(draw.Save((out/L"menu-tab-controls-default-throw.bmp").c_str(),s,game));
    game.throwAim=game.throwMarker=game.coverFollow=game.immersiveScope=0;s.LoadTabDrafts(game);s.tab=SettingsMenu::TabMovement;s.x=PX;s.y=PY;
    game.handHud=1;game.activeHandHud=2;s.draftHandHud=s.originalHandHud=false;
    game.handHud=game.activeHandHud=0;s.handHudAvailable=false;s.draftHandHud=s.originalHandHud=false;
    game.scopeZoom=0;s.draftZoom=s.originalZoom=0;
    s.draft=90;s.draftMovement=3;pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    assert(bright(SettingsMenu::Save)>500&&bright(SettingsMenu::Discard)>500);
    assert(draw.Save((out/L"menu-dirty.bmp").c_str(),s,game));
    s.recenterAt=3000;s.recenterSeconds=3;
    assert(draw.Save((out/L"menu-recenter.bmp").c_str(),s,game));s.recenterAt=0;
    s.remapping=true;s.selected=0;s.draftBindings=game.bindings;s.draftBindings[0]=XINPUT_GAMEPAD_RIGHT_SHOULDER;
    game.holdMs[0]=300;game.holdMs[2]=500;
    s.draftBindings[1]=XINPUT_GAMEPAD_B;s.draftBindings[2]=XINPUT_GAMEPAD_START;
    s.draftBindings[3]=s.draftBindings[4]=XINPUT_GAMEPAD_Y;s.draftBindings[5]=XINPUT_GAMEPAD_X;
    s.draftBindings[6]=XINPUT_GAMEPAD_LEFT_SHOULDER;s.draftBindings[7]=XINPUT_GAMEPAD_RIGHT_THUMB;
    s.draftBindings[8]=1u<<16;s.draftBindings[9]=1u<<17;s.draftBindings[10]=1u<<19;s.draftBindings[12]=1u<<18;
    s.draftBindings[18]=XINPUT_GAMEPAD_A;s.draftBindings[19]=XINPUT_GAMEPAD_LEFT_THUMB;
    assert(draw.Save((out/L"menu-remap.bmp").c_str(),s,game));
    s.resetConfirm=true;assert(draw.Save((out/L"menu-reset.bmp").c_str(),s,game));s.resetConfirm=false;
    s.page=10;assert(draw.Save((out/L"menu-remap-bottom.bmp").c_str(),s,game));s.page=0;
    s.listening=true;assert(draw.Save((out/L"menu-listen.bmp").c_str(),s,game));
    s.listening=false;s.confirming=true;s.candidate=0;
    assert(draw.Save((out/L"menu-confirm.bmp").c_str(),s,game));
    printf("PASS %zu-bit: ABI, stale channel, panel 1 cm in front of the game screen, menu tabs (rows per tab, one save, new rows, older-DLL rows inert), pointer geometry, click/release/focus, pending requests, bounds, existing chords, persistence and native UI previews, shortcut in the top-right corner, see-through like the game's buttons\n",sizeof(void*)*8);
}
