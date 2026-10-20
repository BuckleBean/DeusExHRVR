#include "SettingsMenuDraw.h"
#include "SettingsPersistence.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

int wmain(int argc,wchar_t** argv) {
    assert(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    Transport::Header h{};h.magic=Transport::Magic;h.version=6;h.pid=42;
    assert(!Transport::Compatible(&h,42));h.version=7;assert(Transport::Compatible(&h,42));
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
    // Holding the opening trigger cannot also change a setting.
    update(1450,465);assert(!s.request);
    in.rightTrigger=0;update(1450,465);in.rightTrigger=.8f;update(1450,465);assert(s.request==1&&s.requested==35);
    in.rightTrigger=0;update(1450,465);in.rightTrigger=.8f;update(1450,465);assert(s.request==1); // pending request
    game.acknowledged=1;game.degrees=180;in.rightTrigger=0;update(1450,465);in.rightTrigger=.8f;update(1450,465);assert(s.requested==180);
    in.rightTrigger=0;update(1200,680);in.rightTrigger=.8f;update(1200,680);assert(!s.open&&s.blocked);
    in.a=true;in.rightTrigger=0;update(1200,680);assert(s.blocked);in.a=false;update(1200,680);assert(!s.blocked);
    in.rightTrigger=.8f;s.Update(false,false,false,0,0,in,game);assert(!s.visible&&!s.open);
    // Held trigger at focus regain must not open the menu.
    update(1200,450);assert(!s.open);in.rightTrigger=0;update(1200,450);in.rightTrigger=.8f;update(1200,450);assert(s.open);
    s.Update(false,true,false,0,0,in,game);assert(!s.open&&s.blocked);
    in={};s.Update(false,false,false,0,0,in,game);assert(s.blocked); // inactive is not observed release
    in.active=true;s.Update(false,true,false,0,0,in,game);assert(!s.blocked);
    s={};s.open=true;game.acknowledged=0;game.degrees=30;
    update(1250,450);in.rightX=.8f;update(1250,450);assert(s.request==1&&s.requested==35);
    game.acknowledged=1;game.degrees=35;update(1250,450);assert(s.request==1);
    in.rightX=0;update(1250,450);in.rightX=-.8f;update(1250,450);assert(s.request==2&&s.requested==30);
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
    SettingsMenuDraw draw;s={};s.visible=true;game={};game.snapEnabled=1;game.degrees=30;
    assert(draw.Save((out/L"menu-entry.bmp").c_str(),s,game));s.open=true;s.pointer=true;s.x=1450;s.y=465;
    assert(draw.Save((out/L"menu-panel.bmp").c_str(),s,game));
    auto pixels=static_cast<const uint32_t*>(draw.Draw(s,game));
    assert(pixels[0]==0);assert((pixels[300*SettingsMenu::Width+1050]>>24)==255);
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
    printf("PASS %zu-bit: ABI, stale channel, pointer geometry, click/release/focus, pending requests, bounds, existing chords, persistence and native UI previews\n",sizeof(void*)*8);
}
