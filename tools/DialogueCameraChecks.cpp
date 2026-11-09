#include "DialogueCamera.h"
#include "SettingsMenu.h"
#include "SettingsPersistence.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int wmain(int argc,wchar_t** argv){
    using DialogueCamera::State;
    Transport::Vector body{0,0,80.59f};
    auto pose=[&](float height){auto m=CameraMath::Rotation({0,0,0,1});m.m[14]=body.z+height;return m;};
    State s;auto v=pose(500);
    assert(!s.Update(true,false,true,1,body,300,100,v));
    v=pose(0);s.Update(true,false,true,1,body,300,110,v); // fresh bad transition cannot overwrite
    assert(s.Update(true,true,false,1,body,300,125,v)&&std::abs(v.m[14]-580.59f)<.001f);
    v=pose(0);assert(s.Update(true,false,false,1,body,300,200,v));
    v=pose(500);assert(!s.Update(true,false,true,1,body,300,210,v));
    // No sample: do not acquire midway even if gameplay flags briefly reappear.
    s.Reset();v=pose(0);assert(!s.Update(true,true,false,1,body,300,100,v));
    v=pose(500);assert(!s.Update(true,true,true,1,body,300,120,v));
    s.Reset();v=pose(500);s.Update(true,false,true,1,body,300,100,v);
    v=pose(0);assert(!s.Update(true,true,false,1,body,300,1100,v)); // stale
    s.Reset();v=pose(500);s.Update(true,false,true,1,body,300,100,v);
    v=pose(0);assert(!s.Update(true,true,false,2,body,300,120,v)); // new player
    s.Reset();v=pose(250);s.Update(true,false,true,1,body,300,100,v);
    v=pose(0);assert(s.Update(true,true,false,1,body,300,120,v)&&std::abs(v.m[14]-330.59f)<.001f); // crouch
    s.Reset(true);v=pose(500);assert(!s.Update(true,true,true,1,body,300,140,v)); // recenter/load/scale
    s.Reset();v=pose(500);s.Update(true,false,true,1,body,300,100,v);
    v=pose(0);assert(!s.Update(false,true,false,1,body,300,120,v));
    assert(!s.Update(true,true,false,1,body,300,140,v)); // enable during dialogue stays native
    s.Reset();v=pose(500);s.Update(true,false,true,1,body,300,100,v);
    s.Update(true,true,false,1,body,300,120,v);
    for(uint64_t t=200;t<=2200;t+=100){v=pose(0);s.Update(true,false,false,1,body,300,t,v);}
    assert(!s.holding); // bounded exit

    Transport::MenuState game{};game.worldScale=300;game.degrees=30;game.dialogueAvailable=1;
    GamepadMapping::Input input{};input.active=true;
    SettingsMenu::State ui;
    auto click=[&](SettingsMenu::Rect r){input.rightTrigger=0;ui.Update(true,true,true,float(r.x+5),float(r.y+5),input,game,100);input.rightTrigger=1;ui.Update(true,true,true,float(r.x+5),float(r.y+5),input,game,110);};
    click(SettingsMenu::Entry);click(SettingsMenu::Dialogue);assert(ui.draftDialogue&&ui.Dirty());
    click(SettingsMenu::Discard);assert(!ui.open&&ui.request==0&&game.dialogue==0);
    click(SettingsMenu::Entry);assert(!ui.draftDialogue);
    click(SettingsMenu::Dialogue);click(SettingsMenu::Save);assert(ui.requestedDialogue&&ui.request==1);
    ui={};game.dialogueAvailable=0;click(SettingsMenu::Entry);click(SettingsMenu::Dialogue);assert(!ui.draftDialogue&&!ui.Dirty());

    assert(argc==2);auto file=std::filesystem::absolute(std::filesystem::path(argv[1])/L"dialogue.ini");
    std::filesystem::create_directories(file.parent_path());
    {std::ofstream out(file);out<<"[VR]\nFixedDialogueCamera=0\nUnrelated=preserved\n";}
    assert(SettingsPersistence::Save(file.wstring(),true,30,68,-1,-1,{},0,-1,-1,false,-1,1));
    assert(GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",0,file.c_str())==1);
    assert(SettingsPersistence::Save(file.wstring(),false,0,69)); // calibration write preserves option
    assert(GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",0,file.c_str())==1);
    assert(SettingsPersistence::Save(file.wstring(),true,30,69,-1,-1,{},0,-1,-1,false,-1,0));
    assert(GetPrivateProfileIntW(L"VR",L"FixedDialogueCamera",1,file.c_str())==0);
    wchar_t text[40];GetPrivateProfileStringW(L"VR",L"Unrelated",L"",text,40,file.c_str());assert(wcscmp(text,L"preserved")==0);
    puts("PASS: dialogue admission, zero-height transition, fallback, identity, crouch, invalidation, exit, menu Save/Discard/unavailable, persistence");
}
