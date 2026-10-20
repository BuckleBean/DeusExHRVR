#include "SettingsMenuDraw.h"
#include "SettingsPersistence.h"
#include "NativeMenuPointer.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

int wmain(int argc,wchar_t** argv) {
    assert(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    Transport::Header h{};h.magic=Transport::Magic;h.version=9;h.pid=42;
    assert(!Transport::Compatible(&h,42));h.version=Transport::Version;assert(Transport::Compatible(&h,42));
    assert(Transport::LockMenu(&h));Transport::MenuState read{};assert(!Transport::ReadMenu(&h,read));Transport::UnlockMenu(&h);
    assert(Transport::ReadMenu(&h,read));assert(Transport::Fresh(100,599));assert(!Transport::Fresh(100,600));assert(!Transport::Fresh(100,99));
    assert(SettingsMenu::Eligible(2));assert(SettingsMenu::Eligible(2|32));
    for(unsigned reason:{0u,1u,4u,8u,16u,32u,64u,128u,2u|4u,2u|16u})assert(!SettingsMenu::Eligible(reason));
    float degrees=30;double counts=300;
    assert(SettingsMenu::ApplyAngle(45,degrees,counts)&&degrees==45&&counts==450);
    assert(SettingsMenu::ApplyAngle(30,degrees,counts)&&counts==300);
    assert(!SettingsMenu::ApplyAngle(181,degrees,counts));assert(!SettingsMenu::ApplyAngle(std::numeric_limits<float>::quiet_NaN(),degrees,counts));
    SettingsMenu::InputGate gate;gate.latest.hostTick=100;gate.latest.blocked=1;
    assert(gate.Blocked(599));assert(!gate.Blocked(600)); // cached lock contention/focus expiry
    XINPUT_GAMEPAD physical{};physical.wButtons=XINPUT_GAMEPAD_A;
    assert(gate.Physical(true,physical));assert(gate.Physical(false,physical));
    physical={};assert(!gate.Physical(false,physical));
    Transport::Pose panel{{0,0,0,1},{0,0,-2}},aim{{0,0,0,1},{0,0,0}};float x{},y{};
    assert(SettingsMenu::Hit(aim,panel,x,y));assert(std::abs(x-800)<.01&&std::abs(y-450)<.01);
    aim.position.x=.8f;assert(SettingsMenu::Hit(aim,panel,x,y));assert(std::abs(x-1200)<.01);
    aim.orientation={0,1,0,0};assert(!SettingsMenu::Hit(aim,panel,x,y));aim.orientation={0,0,0,1};aim.position.z=-3;assert(!SettingsMenu::Hit(aim,panel,x,y));
    SettingsMenu::State s;GamepadMapping::Input in{};in.active=true;Transport::MenuState game{};game.snapEnabled=1;game.degrees=30;
    auto update=[&](float px,float py){s.Update(true,true,true,px,py,in,game);};
    update(1200,450);assert(!s.open&&!s.blocked);
    in.rightTrigger=.1f;update(1200,450);assert(s.blocked&&!s.open);
    in.rightTrigger=.8f;update(1200,450);assert(s.open&&s.blocked);
    // Edits stay local, including rapid clicks, until explicit save.
    auto click=[&](float px,float py){in.rightTrigger=0;update(px,py);in.rightTrigger=.8f;update(px,py);};
    update(1450,465);assert(!s.request);
    for(int n=0;n<4;++n)click(1450,465);
    assert(s.draft==90&&s.request==0&&game.degrees==30);
    click(1200,730);assert(!s.open&&s.request==0); // discard
    click(1200,450);assert(s.open&&s.draft==30);
    click(1450,465);click(1200,650);
    assert(s.open&&s.saving&&s.request==1&&s.requested==45);
    click(1450,465);assert(s.request==1&&s.draft==45); // pending blocks edits
    game.acknowledged=game.saved=1;game.result=0;game.degrees=45;
    in.rightTrigger=0;update(1200,650);assert(!s.open&&!s.saving);
    click(1200,450);click(1450,465);click(1200,650);
    game.acknowledged=2;game.result=3;update(1200,650);
    assert(s.open&&!s.saving&&s.committed); // failure stays open, no false success
    click(1200,650);assert(s.request==3&&s.saving); // explicit retry
    game.acknowledged=game.saved=3;game.result=0;game.degrees=60;
    in.rightTrigger=0;update(1200,650);assert(!s.open);
    click(1200,450);click(1450,465);
    s.Update(false,false,false,0,0,in,game);assert(!s.open&&s.draft==s.original&&s.request==3);
    update(1200,450);assert(!s.open); // held trigger on refocus
    click(1200,450);assert(s.open&&s.draft==60);
    in.rightTrigger=0;update(1250,450);in.rightX=.8f;update(1250,450);
    assert(s.draft==75&&s.request==3);update(1250,450);assert(s.draft==75);
    in.rightX=0;update(1250,450);in.rightX=-.8f;update(1250,450);assert(s.draft==60);
    // Mode edits are drafts; smooth preserves angle and disables angle edits.
    s={};in={};in.active=true;game={};game.snapEnabled=1;game.degrees=30;
    click(1200,450);click(1200,350);assert(!s.draftSnap&&s.request==0);
    click(1450,465);assert(s.draft==30);click(1200,730);assert(!s.open&&s.request==0);
    click(1200,450);assert(s.draftSnap);click(1200,350);click(1200,650);
    assert(s.saving&&s.request==1&&!s.requestedSnap&&s.requested==30);
    game.acknowledged=game.saved=1;game.snapEnabled=0;in.rightTrigger=0;update(1200,650);
    click(1200,450);assert(!s.draftSnap);click(1200,350);click(1450,465);
    assert(s.draftSnap&&s.draft==45);click(1200,650);assert(s.requestedSnap&&s.requested==45);
    // Movement choices never offer native mode; discard and save remain explicit.
    s={};in={};in.active=true;game={};game.snapEnabled=1;game.degrees=30;game.movement=1;
    click(1200,450);click(1200,540);assert(s.draftMovement==3&&s.request==0);
    click(1200,730);click(1200,450);assert(s.draftMovement==1);
    click(1200,540);click(1200,650);assert(s.requestedMovement==3&&s.saving);
    game.acknowledged=game.saved=s.request;game.movement=3;in.rightTrigger=0;update(1200,650);
    click(1200,450);click(1200,540);assert(s.draftMovement==2);click(1200,540);assert(s.draftMovement==1);
    Transport::Tracking directions{};directions.leftController.valid=1;directions.leftController.aim.position.x=-1;
    directions.rightController.valid=1;directions.rightController.aim.position.x=1;
    assert(DirectionConfig::Aim(DirectionConfig::Source::LeftController,directions)->aim.position.x==-1);
    assert(DirectionConfig::Aim(DirectionConfig::Source::Controller,directions)->aim.position.x==1);
    directions.leftController.valid=0;assert(!DirectionConfig::Aim(DirectionConfig::Source::LeftController,directions));
    assert(DirectionConfig::Aim(DirectionConfig::Source::Controller,directions));
    assert(DirectionConfig::Parse(L"LeftController",DirectionConfig::Source::Mouse)==DirectionConfig::Source::LeftController);
    // Native-menu hover and click route independently of the VR panel.
    s={};in={};in.active=true;update(800,450);assert(s.nativePointer&&!s.nativeDown&&!s.blocked);
    in.rightTrigger=.1f;update(800,450);assert(s.blocked&&!s.nativeDown);
    in.rightTrigger=.8f;update(800,450);assert(s.nativeDown&&s.blocked&&!s.open);
    update(1200,450);assert(!s.open&&!s.nativeDown&&s.blocked); // drag cannot open VR panel
    in.rightTrigger=0;update(800,450);update(800,450);assert(!s.blocked&&s.nativePointer);
    s.Update(false,false,false,0,0,in,game);in.rightTrigger=.8f;update(800,450);assert(!s.nativeDown);
    in.rightTrigger=0;update(800,450);in.rightTrigger=.8f;update(800,450);assert(s.nativeDown);
    s={};in={};in.active=true;update(800,450);
    in.leftY=.8f;update(800,450);assert(!s.nativePointer&&!s.blocked);
    in.leftY=0;update(805,452);assert(!s.nativePointer); // tremor cannot steal focus
    update(850,450);assert(s.nativePointer); // deliberate aim resumes mouse
    in.leftY=.8f;update(850,450);in.leftY=0;in.rightTrigger=.1f;update(850,450);assert(s.nativePointer);
    in.rightTrigger=0;update(1200,450);in.rightTrigger=.8f;update(1200,450);
    assert(s.open&&s.nativePointer&&!s.nativeDown); // arrow moves over VR, click remains isolated
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
    SettingsMenuDraw draw;s={};s.visible=true;s.draftMovement=s.originalMovement=1;s.draftSnap=s.originalSnap=true;s.draft=s.original=30;game={};game.snapEnabled=1;game.degrees=30;
    assert(draw.Save((out/L"menu-entry.bmp").c_str(),s,game));s.open=true;s.pointer=true;s.x=1450;s.y=465;
    assert(draw.Save((out/L"menu-panel.bmp").c_str(),s,game));
    auto pixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    assert(pixels[0]==0);assert((pixels[300*SettingsMenu::Width+1050]>>24)==255);
    // Pointer remains visible during the approach, outside either clickable area.
    for(bool panelOpen : {false,true}){
        s.open=panelOpen;s.x=800;s.y=450;
        pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
        assert(pixels[450*SettingsMenu::Width+800]==0); // native arrow owns regular menu
        assert(pixels[440*SettingsMenu::Width+800]==0);
    }
    s.x=1450;s.y=465;
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
            assert(bright({1040,255,490,60})>500); // title text
            assert(bright({1180,430,210,80})>100); // numeric value
            assert(bright(SettingsMenu::Back)>500); // visible button and label
        }else assert(bright(SettingsMenu::Entry)>500);
    }
    s.open=true;assert(draw.Save((out/L"menu-panel.bmp").c_str(),s,game));
    s.draft=90;s.draftMovement=3;pixels=static_cast<const uint32_t*>(draw.Draw(s,game));assert(pixels);
    assert(bright(SettingsMenu::Save)>500&&bright(SettingsMenu::Discard)>500);
    assert(draw.Save((out/L"menu-dirty.bmp").c_str(),s,game));
    printf("PASS %zu-bit: ABI, stale channel, pointer geometry, click/release/focus, pending requests, bounds, existing chords, persistence and native UI previews\n",sizeof(void*)*8);
}
