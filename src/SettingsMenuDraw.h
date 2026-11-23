#pragma once
#include "SettingsMenu.h"
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Small native raster UI; no game assets. The same pixels feed preview and XR.
class SettingsMenuDraw {
    HDC dc{};HBITMAP bitmap{};HGDIOBJ oldBitmap{};HFONT font{},title{};void* pixels{};
    bool drawError{};
public:
    ~SettingsMenuDraw(){Reset();}
    void Reset(){if(dc&&oldBitmap)SelectObject(dc,oldBitmap);if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);if(title)DeleteObject(title);if(dc)DeleteDC(dc);dc=nullptr;bitmap=nullptr;font=title=nullptr;pixels=nullptr;oldBitmap=nullptr;}
    bool Init() {
        if(dc)return true;
        dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        BITMAPINFO b{};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=SettingsMenu::Width;
        b.bmiHeader.biHeight=-SettingsMenu::Height;b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;b.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&b,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap){Reset();return false;}oldBitmap=SelectObject(dc,bitmap);
        font=CreateFontW(-28,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title=CreateFontW(-34,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font||!title){Reset();return false;}return true;
    }
    void Text(const wchar_t* text,int x,int y,int w,int h,bool heading=false,bool left=false) {
        auto old=SelectObject(dc,heading?title:font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,heading?RGB(255,190,55):RGB(244,227,191));
        RECT r{x,y,x+w,y+h};if(!DrawTextW(dc,text,-1,&r,(left?DT_LEFT:DT_CENTER)|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS))drawError=true;SelectObject(dc,old);
    }
    void Button(SettingsMenu::Rect r,const wchar_t* label,bool hover) {
        auto brush=CreateSolidBrush(hover?RGB(151,106,22):RGB(34,28,15));auto pen=CreatePen(PS_SOLID,2,hover?RGB(241,188,68):RGB(115,89,34));
        auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
        POINT p[]={{r.x,r.y},{r.x+r.w-20,r.y},{r.x+r.w,r.y+20},{r.x+r.w,r.y+r.h},{r.x+20,r.y+r.h},{r.x,r.y+r.h-20}};
        if(!Polygon(dc,p,6))drawError=true;SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
        // Short diagonal gold hatch, fading toward the right like the native UI.
        const int span=std::min(r.w-40,220);
        for(int i=0;i<span;i+=8){
            float fade=1.f-float(i)/span;int v=int((hover?45:28)*fade);
            auto stripe=CreatePen(PS_SOLID,1,hover?RGB(151+v,106+v,22+v/2):RGB(34+v,28+v,15+v/2));
            auto old=SelectObject(dc,stripe);MoveToEx(dc,r.x+10+i,r.y+5,nullptr);LineTo(dc,r.x+18+i,r.y+13);
            SelectObject(dc,old);DeleteObject(stripe);
        }
        if(label)Text(label,r.x,r.y,r.w,r.h);
    }
    // The game's PC menu buttons (ComponentsLibrary.Classes.PCButton) draw their
    // background plate at alpha x0.75 and their label solid (pause-menu movie
    // 87518 sprite 258, also 87474/87475). The closed shortcut copies that.
    // The game blends that 75% on its own (presumably sRGB-encoded) values, so 25%
    // of the background's encoded brightness shows through. The XR layer is
    // premultiplied (BLEND_TEXTURE_SOURCE_ALPHA without UNPREMULTIPLIED): stored
    // colour = 0.75 x colour on either swapchain. A linear swapchain blends like
    // the game (alpha 0.75). An sRGB swapchain blends in linear light, where
    // alpha 0.75 would let far more through; there alpha is chosen so the plate
    // (stored red 26) over a mid-grey background (sRGB 0.5) gives the game's
    // result 26 + 0.25 x 127.5: 1 - (lin(57.875/255) - lin(26/255)) / lin(0.5)
    // = 0.851 -> 217. Darker backgrounds come through a little less than in the
    // game, brighter ones a little more.
    static constexpr float ShortcutOpacity=.75f;
    static constexpr uint32_t ShortcutAlphaLinear=191,ShortcutAlphaSrgb=217;
    static constexpr COLORREF ShortcutEdge=RGB(115,89,34); // Button() border, not hovered
    static constexpr uint8_t Premultiply(uint8_t c){return uint8_t((c*3u+2u)/4u);} // round(0.75 x c)
    bool srgb=true; // set by NativeBridge from the swapchain format
    std::vector<uint32_t> plate; // closed shortcut before its label, Entry+-2
    const void* Draw(const SettingsMenu::State& s,const Transport::MenuState& live) {
        auto game=live;if(s.draftReset)for(auto& hold:game.holdMs)hold=0;
        if(!Init())return nullptr;
        drawError=false;
        // Complete any pending GDI work before direct writes to the DIB.
        if(!GdiFlush())return nullptr;
        memset(pixels,0,SettingsMenu::Width*SettingsMenu::Height*4);
        if(!s.visible)return pixels;
        auto area=s.Area();
        if(s.open){
            auto data=static_cast<uint32_t*>(pixels);std::fill(data,data+SettingsMenu::Width*SettingsMenu::Height,0x90000000u);
            RECT r{area.x,area.y,area.x+area.w,area.y+area.h};auto b=CreateSolidBrush(RGB(19,18,13));FillRect(dc,&r,b);DeleteObject(b);
            auto edge=CreateSolidBrush(RGB(115,89,34));FrameRect(dc,&r,edge);DeleteObject(edge);
        }
        auto hovered=[&](SettingsMenu::Rect rect){return s.pointer&&rect.Has(s.x,s.y);};
        const bool shortcutTranslucent=!s.open&&!hovered(SettingsMenu::Entry);
        if(!s.open){
            Button(SettingsMenu::Entry,nullptr,hovered(SettingsMenu::Entry));
            if(shortcutTranslucent){
                // Remember the plate so pixels the label changes stay solid.
                if(!GdiFlush())return nullptr;
                const auto e=SettingsMenu::Entry;const int w=e.w+4;plate.assign(size_t(w)*(e.h+4),0);
                auto data=static_cast<const uint32_t*>(pixels);
                for(int y=0;y<e.h+4;++y)for(int x=0;x<w;++x){
                    int cx=e.x-2+x,cy=e.y-2+y;
                    if(cx>=0&&cy>=0&&cx<SettingsMenu::Width&&cy<SettingsMenu::Height)plate[size_t(y)*w+x]=data[cy*SettingsMenu::Width+cx]&0xffffffu;
                }
            }
            Text(L"VR SETTINGS",SettingsMenu::Entry.x,SettingsMenu::Entry.y,SettingsMenu::Entry.w,SettingsMenu::Entry.h);
        }
        else if(s.recenterAt){
            Text(L"RECENTER VIEW",240,120,1120,60,true);
            Text(L"Face forward and get comfortable.",240,300,1120,50);
            wchar_t seconds[8];swprintf_s(seconds,L"%d",s.recenterSeconds);
            Text(seconds,240,390,1120,90,true);
            Text(L"Your settings will not change.",240,530,1120,45);
            Button(SettingsMenu::Back,L"CANCEL",hovered(SettingsMenu::Back));
        }
        else if(s.scalePage){
            Text(L"WORLD SCALE",240,110,1120,55,true,true);
            Text(L"Lower values make the world look larger. Higher values make it look smaller.",240,210,1120,45);
            Text(L"Choose Save & Back, then resume to see the new scale.",240,260,1120,45);
            wchar_t value[40];swprintf_s(value,L"%.6g",double(s.draftScale));Text(value,560,330,480,70,true);
            Button(SettingsMenu::ScaleMinus,L"-",!s.saving&&hovered(SettingsMenu::ScaleMinus));
            Button(SettingsMenu::ScalePlus,L"+",!s.saving&&hovered(SettingsMenu::ScalePlus));
            Text(L"Adjust in steps of 10",240,410,1120,40);
            wchar_t resetLabel[48];swprintf_s(resetLabel,L"RESET TO %.6g",double(LiveWorldScale::Default));
            Button(SettingsMenu::ScaleReset,resetLabel,!s.saving&&hovered(SettingsMenu::ScaleReset));
            wchar_t active[96];swprintf_s(active,L"Active this session: %.6g",double(game.activeWorldScale));Text(active,240,565,1120,45);
            Text(s.saving?L"Saving...":s.committed&&game.result==3?L"Save failed. Choose Save & Back to retry.":L"Discard & Back keeps your previous scale.",240,685,1120,40);
            Button(SettingsMenu::Back,L"SETTINGS",!s.saving&&hovered(SettingsMenu::Back));
            if(s.Dirty())Button(SettingsMenu::Discard,s.committed?L"BACK":L"DISCARD & BACK",!s.saving&&hovered(SettingsMenu::Discard));
            Button(SettingsMenu::Save,s.saving?L"SAVING...":s.Dirty()?L"SAVE & BACK":L"NO CHANGES",!s.saving&&s.Dirty()&&hovered(SettingsMenu::Save));
        }
        else if(s.remapping) {
            Text(L"GAMEPLAY BUTTON MAPPING",240,105,770,55,true);
            Button(SettingsMenu::ResetBindings,L"RESTORE DEFAULTS",!s.saving&&hovered(SettingsMenu::ResetBindings));
            Text(L"ACTION",240,170,470,40,true,true);Text(L"ASSIGNED CONTROL",760,170,530,40,true);
            for(int row=0;row<SettingsMenu::Rows;++row){
                auto target=ButtonBindings::Targets[s.page+row];
                Text(target.label,240,220+row*57,470,49,false,true);
                auto label=SettingsMenu::Assigned(target.value,s.draftBindings,game);
                Button(SettingsMenu::Row(row),label.c_str(),!s.saving&&!s.listening&&!s.confirming&&hovered(SettingsMenu::Row(row)));
            }
            Button(SettingsMenu::PageUp,L"^",hovered(SettingsMenu::PageUp));Button(SettingsMenu::PageDown,L"v",hovered(SettingsMenu::PageDown));
            const wchar_t* status=s.saving?L"Saving...":s.committed&&game.result==3?L"Applied; save failed. Select Save to retry.":s.committed&&game.result==2?L"Change refused. Select Save to retry.":L"Select an assignment, then press and release the control you want.";
            Text(status,220,685,1160,40);
            Button(SettingsMenu::ListBack,L"SETTINGS",hovered(SettingsMenu::ListBack));
            Button(SettingsMenu::ListDiscard,s.committed?L"BACK":L"DISCARD & BACK",!s.saving&&hovered(SettingsMenu::ListDiscard));
            Button(SettingsMenu::ListSave,s.saving?L"SAVING...":s.Dirty()?L"SAVE & BACK":L"NO CHANGES",!s.saving&&s.Dirty()&&hovered(SettingsMenu::ListSave));
            if(s.listening||s.confirming||s.resetConfirm){
                RECT dialog{210,170,1390,865};auto brush=CreateSolidBrush(RGB(16,17,14));FillRect(dc,&dialog,brush);DeleteObject(brush);
                brush=CreateSolidBrush(RGB(210,150,30));FrameRect(dc,&dialog,brush);DeleteObject(brush);
                if(s.resetConfirm){
                    Text(L"RESTORE DEFAULT BINDINGS?",300,290,1000,60,true);
                    Text(L"Replaces gameplay assignments and clears custom tap / hold bindings.",240,390,1120,50);
                    Text(L"Choose Save & Back afterward to apply, or Discard to keep your setup.",240,465,1120,50);
                    Button(SettingsMenu::Confirm,L"RESTORE DEFAULTS",hovered(SettingsMenu::Confirm));
                }else {
                Text(ButtonBindings::Targets[s.selected].label,340,290,920,60,true);
                if(s.listening){
                    Text(s.captureReady?L"Press and release a button or trigger":L"Release controls to begin",340,375,920,50,true);
                    Text(L"For an existing tap / hold control, use the gesture you want.",340,445,920,45);
                    Text(L"Left menu button cancels. Right stick up / down also supported.",340,505,920,45);
                }else{
                    auto name=SettingsMenu::ControlName(size_t(s.candidate),game);
                    auto assign=std::wstring(L"Assign to ")+name+L"?";Text(assign.c_str(),340,365,920,50,true);
                    auto old=s.draftBindings[size_t(s.candidate)];
                    auto displaced=std::wstring(L"Replaces: ")+ButtonBindings::Label(old);Text(displaced.c_str(),340,440,920,45);
                    auto previous=std::wstring(L"Previous: ")+SettingsMenu::Assigned(ButtonBindings::Targets[s.selected].value,s.draftBindings,game);
                    Text(previous.c_str(),240,490,1120,45);
                    Text(L"Other custom assignments for this action will be replaced.",340,545,920,40);
                    Button(SettingsMenu::Confirm,L"ASSIGN",hovered(SettingsMenu::Confirm));
                }
                }
                Button(SettingsMenu::Cancel,L"CANCEL",hovered(SettingsMenu::Cancel));
            }
        }
        else {
            const wchar_t* heading=s.saving?L"SAVING...":s.committed&&game.result==3?L"SAVE FAILED - RETRY":s.committed&&game.result==2?L"CHANGE REFUSED":L"VR SETTINGS";
            using SettingsMenu::MainRowY;
            Text(heading,240,60,1120,55,true,true);
            // Tabs: the shown one is drawn lit, with a gold bar under it.
            static const wchar_t* tabNames[SettingsMenu::Tabs]{L"MOVEMENT",L"CONTROLS",L"VIEW",L"IMAGE"};
            for(int i=0;i<SettingsMenu::Tabs;++i){
                const auto r=SettingsMenu::Tab(i);
                Button(r,tabNames[i],s.tab==i||(!s.saving&&hovered(r)));
                if(s.tab==i){RECT bar{r.x+20,r.y+r.h+4,r.x+r.w-20,r.y+r.h+9};auto b=CreateSolidBrush(RGB(241,188,68));FillRect(dc,&bar,b);DeleteObject(b);}
            }
            auto label=[&](int row,const wchar_t* text){Text(text,240,MainRowY(row),470,52,false,true);};
            auto row=[&](SettingsMenu::Rect r,bool available,const wchar_t* text){Button(r,available?text:L"UNAVAILABLE",!s.saving&&available&&hovered(r));};
            if(s.tab==SettingsMenu::TabMovement){
                label(0,L"Turning");
                label(1,s.draftSnap?L"Snap-turn angle":L"Snap-turn angle (inactive)");
                label(2,L"Movement direction");
                label(3,L"Walking response");
                label(4,L"World scale");
                label(5,L"Recenter view");
                Button(SettingsMenu::TurnMode,s.draftSnap?L"SNAP":L"SMOOTH",!s.saving&&hovered(SettingsMenu::TurnMode));
                wchar_t value[40];swprintf_s(value,L"%.6g\u00b0",double(s.draft));Text(value,870,MainRowY(1),310,52,true);
                Button(SettingsMenu::Minus,L"-",s.draftSnap&&!s.saving&&hovered(SettingsMenu::Minus));Button(SettingsMenu::Plus,L"+",s.draftSnap&&!s.saving&&hovered(SettingsMenu::Plus));
                Button(SettingsMenu::Movement,s.draftMovement==1?L"HEAD":s.draftMovement==3?L"LEFT HAND":s.draftMovement==2?L"RIGHT HAND":L"FROM CONFIG",!s.saving&&hovered(SettingsMenu::Movement));
                Button(SettingsMenu::Response,s.draftResponse==2?L"GENTLER":s.draftResponse==1?L"GENTLE":L"NORMAL",!s.saving&&hovered(SettingsMenu::Response));
                wchar_t scaleLabel[64];swprintf_s(scaleLabel,L"%.6g / ADJUST",double(s.draftScale));Button(SettingsMenu::WorldScale,scaleLabel,!s.saving&&hovered(SettingsMenu::WorldScale));
                Button(SettingsMenu::Recenter,L"RECENTER (3 SECONDS)",!s.saving&&hovered(SettingsMenu::Recenter));
            }else if(s.tab==SettingsMenu::TabControls){
                label(0,L"Interaction aim");
                label(1,L"Grenade aim");
                label(2,s.draftThrowAim==1?L"Grenade landing ring (inactive)":L"Grenade landing ring");
                label(3,L"Gameplay controls");
                label(4,L"Quickbar");
                row(SettingsMenu::QuickBarRow,s.originalQuickBar!=0,s.draftQuickBar==2?L"AUTO-HIDE":L"ALWAYS SHOWN");
                label(5,L"Hand pointers");
                row(SettingsMenu::HandPointerRow,s.originalHandPointers!=0,HandPointer::Label(s.draftHandPointers));
                Button(SettingsMenu::Interaction,s.draftInteraction==1?L"HEAD":s.draftInteraction==2?L"RIGHT HAND":L"FROM CONFIG",!s.saving&&hovered(SettingsMenu::Interaction));
                row(SettingsMenu::ThrowAimRow,s.originalThrowAim!=0,s.draftThrowAim==2?L"HEAD":s.draftThrowAim==3?L"RIGHT HAND":s.draftThrowAim==4?L"LEFT HAND":L"DEFAULT");
                row(SettingsMenu::ThrowMarkerRow,s.throwMarkerAvailable,s.draftThrowMarker?L"ON":L"OFF");
                Button(SettingsMenu::Remap,L"BUTTON REMAPPING",!s.saving&&hovered(SettingsMenu::Remap));
            }else if(s.tab==SettingsMenu::TabImage){
                label(0,L"Brightness");
                label(1,L"Contrast");
                label(2,L"Original image");
                auto stepper=[&](SettingsMenu::Rect minus,SettingsMenu::Rect plus,int line,int value){
                    if(!s.imageAvailable){Text(L"UNAVAILABLE",870,MainRowY(line),310,52,true);return;}
                    wchar_t number[16];swprintf_s(number,value>0?L"+%d":L"%d",value);Text(number,870,MainRowY(line),310,52,true);
                    Button(minus,L"-",!s.saving&&value>ImageAdjust::Min&&hovered(minus));
                    Button(plus,L"+",!s.saving&&value<ImageAdjust::Max&&hovered(plus));
                };
                stepper(SettingsMenu::BrightnessMinus,SettingsMenu::BrightnessPlus,0,s.draftBrightness);
                stepper(SettingsMenu::ContrastMinus,SettingsMenu::ContrastPlus,1,s.draftContrast);
                row(SettingsMenu::ImageReset,s.imageAvailable,L"RESET TO 0");
            }else{
                label(0,L"HUD");
                label(1,L"Immersive scope");
                label(2,s.scopeAvailable&&!s.draftScope?L"Scope zoom (inactive)":L"Scope zoom");
                label(3,L"Cover camera");
                label(4,L"Conversation camera");
                label(5,L"Crosshair");
                row(SettingsMenu::HandHud,s.handHudAvailable,s.draftHandHud?L"HANDS":L"DEFAULT");
                row(SettingsMenu::ScopeRow,s.scopeAvailable,s.draftScope?L"ON":L"OFF");
                if(game.scopeZoom>=1){
                    wchar_t zoom[40];swprintf_s(zoom,L"%.6gx",double(s.draftZoom));Text(zoom,870,MainRowY(2),310,52,true);
                    Button(SettingsMenu::ZoomMinus,L"-",!s.saving&&s.draftZoom>SettingsMenu::ZoomMin&&hovered(SettingsMenu::ZoomMinus));
                    Button(SettingsMenu::ZoomPlus,L"+",!s.saving&&s.draftZoom<SettingsMenu::ZoomMax&&hovered(SettingsMenu::ZoomPlus));
                }else Text(L"UNAVAILABLE",870,MainRowY(2),310,52,true);
                row(SettingsMenu::CoverFollowRow,s.coverFollowAvailable,s.draftCoverFollow?L"FOLLOWS AIM":L"STILL");
                row(SettingsMenu::Dialogue,game.dialogueAvailable!=0,s.draftDialogue?L"STAY IN FIRST PERSON":L"GAME CAMERA");
                row(SettingsMenu::CrosshairRow,s.originalCrosshair!=0,CoverCrosshair::Label(s.draftCrosshair));
            }
            Text(s.Dirty()?L"Changes apply when you choose Save & Back.":game.worldScale!=game.activeWorldScale?L"World scale saved. Resume to apply it.":game.handHud&&game.activeHandHud&&game.handHud!=game.activeHandHud?L"HUD saved. Restart the game to apply it.":L"Select a value to change it.",240,SettingsMenu::StatusY,1120,40);
            if(s.Dirty())Button(SettingsMenu::Discard,s.committed?L"BACK":L"DISCARD & BACK",!s.saving&&hovered(SettingsMenu::Discard));
            else Button(SettingsMenu::Back,L"BACK",hovered(SettingsMenu::Back));
            Button(SettingsMenu::Save,s.saving?L"SAVING...":s.Dirty()?L"SAVE & BACK":L"NO CHANGES",!s.saving&&s.Dirty()&&hovered(SettingsMenu::Save));
        }
        if(!GdiFlush()||drawError)return nullptr;
        auto data=static_cast<uint32_t*>(pixels);
        for(int y=std::max(0,area.y-2);y<std::min(SettingsMenu::Height,area.y+area.h+2);++y)for(int x=std::max(0,area.x-2);x<std::min(SettingsMenu::Width,area.x+area.w+2);++x){
            auto& pixel=data[y*SettingsMenu::Width+x];
            if(s.open&&area.Has(float(x),float(y)))pixel|=0xff000000;
            else if(!s.open&&pixel){
                // Only the shortcut silhouette is drawn. Pointed at, it is solid as
                // before (covering the game's cursor beneath); otherwise its plate
                // is translucent like the game's buttons, label and edge solid.
                const uint32_t rgb=pixel&0xffffffu;
                const auto e=SettingsMenu::Entry;
                constexpr uint32_t edge=0x735922u; // ShortcutEdge RGB(115,89,34) as the DIB stores it
                static_assert(ShortcutEdge==0x225973u);
                const bool platePixel=shortcutTranslucent&&rgb!=edge&&
                    plate[size_t(y-(e.y-2))*(e.w+4)+size_t(x-(e.x-2))]==rgb;
                if(!platePixel)pixel=rgb|0xff000000u;
                else pixel=(srgb?ShortcutAlphaSrgb:ShortcutAlphaLinear)<<24|uint32_t(Premultiply(uint8_t(rgb>>16)))<<16|
                           uint32_t(Premultiply(uint8_t(rgb>>8)))<<8|Premultiply(uint8_t(rgb));
            }
        }
        // The native cursor continues moving underneath the XR panel. Draw its
        // gold-arrow counterpart only where this opaque layer would cover it.
        if(s.pointer&&area.Has(s.x,s.y)) {
            int px=int(s.x),py=int(s.y);
            POINT arrow[]={{px,py},{px+42,py+19},{px+25,py+25},{px+19,py+42}};
            auto brush=CreateSolidBrush(RGB(239,171,35));auto pen=CreatePen(PS_SOLID,3,RGB(116,72,16));
            auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
            Polygon(dc,arrow,4);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
            POINT light[]={{px+3,py+3},{px+35,py+18},{px+22,py+23}};
            POINT shade[]={{px+3,py+3},{px+22,py+23},{px+18,py+35}};
            auto facet=[&](POINT* points,COLORREF color){auto fill=CreateSolidBrush(color);auto old=SelectObject(dc,fill);auto line=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,points,3);SelectObject(dc,old);SelectObject(dc,line);DeleteObject(fill);};
            facet(light,RGB(255,209,99));facet(shade,RGB(191,119,18));
            if(!GdiFlush())return nullptr;
            // GDI leaves alpha unset. Cover the cursor's silhouette, including
            // the portion extending past the panel edge, without a square halo.
            POINT pts[]={{px-2,py-2},{px+46,py+19},{px+27,py+27},{px+19,py+46}};
            auto region=CreatePolygonRgn(pts,4,WINDING);
            for(int cy=std::max(0,py-2);cy<std::min(SettingsMenu::Height,py+47);++cy)
                for(int cx=std::max(0,px-2);cx<std::min(SettingsMenu::Width,px+47);++cx)
                    if(PtInRegion(region,cx,cy)&&data[cy*SettingsMenu::Width+cx])data[cy*SettingsMenu::Width+cx]|=0xff000000;
            DeleteObject(region);
        }
        return pixels;
    }
    bool Save(const wchar_t* path,const SettingsMenu::State& s,const Transport::MenuState& game) {
        if(!Draw(s,game))return false;
        FILE* f{};if(_wfopen_s(&f,path,L"wb"))return false;
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+SettingsMenu::Width*SettingsMenu::Height*4;
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=SettingsMenu::Width;info.biHeight=-SettingsMenu::Height;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=SettingsMenu::Width*SettingsMenu::Height*4;
        bool ok=fwrite(&file,sizeof(file),1,f)==1&&fwrite(&info,sizeof(info),1,f)==1&&fwrite(pixels,info.biSizeImage,1,f)==1;fclose(f);return ok;
    }
};
