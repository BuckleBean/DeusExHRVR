#pragma once
#include "SettingsMenu.h"
#include <vector>
#include <cstdio>

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
        Text(label,r.x,r.y,r.w,r.h);
    }
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
        if(!s.open)Button(SettingsMenu::Entry,L"VR SETTINGS",hovered(SettingsMenu::Entry));
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
            Text(L"Changes take effect after saving and restarting the game.",240,260,1120,45);
            wchar_t value[40];swprintf_s(value,L"%.6g",double(s.draftScale));Text(value,560,330,480,70,true);
            Button(SettingsMenu::ScaleMinus,L"-",!s.saving&&hovered(SettingsMenu::ScaleMinus));
            Button(SettingsMenu::ScalePlus,L"+",!s.saving&&hovered(SettingsMenu::ScalePlus));
            Text(L"Adjust in steps of 10",240,410,1120,40);
            Button(SettingsMenu::ScaleOriginal,L"ORIGINAL DEFAULT: 100",!s.saving&&hovered(SettingsMenu::ScaleOriginal));
            Button(SettingsMenu::ScaleTested,L"TESTED PRESET: 300",!s.saving&&hovered(SettingsMenu::ScaleTested));
            wchar_t active[96];swprintf_s(active,L"Active this session: %.6g",double(game.activeWorldScale));Text(active,240,565,1120,45);
            Text(s.saving?L"Saving...":s.committed&&game.result==3?L"Save failed. Choose Save & Back to retry.":L"Your current scale stays active until you restart.",240,685,1120,40);
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
                RECT dialog{210,170,1390,815};auto brush=CreateSolidBrush(RGB(16,17,14));FillRect(dc,&dialog,brush);DeleteObject(brush);
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
            Text(heading,240,110,1120,55,true,true);
            Text(L"Turning",240,210,470,50,false,true);
            Text(s.draftSnap?L"Snap-turn angle":L"Snap-turn angle (inactive)",240,267,470,50,false,true);
            Text(L"Movement direction",240,324,470,50,false,true);
            Text(L"Walking response",240,438,470,50,false,true);
            Text(L"Interaction aim",240,381,470,50,false,true);
            Text(L"World scale",240,495,470,50,false,true);
            wchar_t scaleLabel[64];swprintf_s(scaleLabel,L"%.6g / ADJUST",double(s.draftScale));Button(SettingsMenu::WorldScale,scaleLabel,!s.saving&&hovered(SettingsMenu::WorldScale));
            Text(L"Gameplay controls",240,552,470,50,false,true);
            Text(L"Recenter view",240,609,470,50,false,true);
            Button(SettingsMenu::TurnMode,s.draftSnap?L"SNAP":L"SMOOTH",!s.saving&&hovered(SettingsMenu::TurnMode));
            wchar_t value[40];swprintf_s(value,L"%.6g\u00b0",double(s.draft));Text(value,870,267,310,50,true);
            Button(SettingsMenu::Minus,L"-",s.draftSnap&&!s.saving&&hovered(SettingsMenu::Minus));Button(SettingsMenu::Plus,L"+",s.draftSnap&&!s.saving&&hovered(SettingsMenu::Plus));
            Button(SettingsMenu::Movement,s.draftMovement==1?L"HEAD":s.draftMovement==3?L"LEFT HAND":s.draftMovement==2?L"RIGHT HAND":L"FROM CONFIG",!s.saving&&hovered(SettingsMenu::Movement));
            Button(SettingsMenu::Response,s.draftResponse?L"GENTLE":L"NORMAL",!s.saving&&hovered(SettingsMenu::Response));
            Button(SettingsMenu::Interaction,s.draftInteraction==1?L"HEAD":s.draftInteraction==2?L"RIGHT HAND":L"FROM CONFIG",!s.saving&&hovered(SettingsMenu::Interaction));
            Button(SettingsMenu::Remap,L"BUTTON REMAPPING",!s.saving&&hovered(SettingsMenu::Remap));
            Button(SettingsMenu::Recenter,L"RECENTER (3 SECONDS)",!s.saving&&hovered(SettingsMenu::Recenter));
            Text(s.Dirty()?L"Changes apply when you choose Save & Back.":game.worldScale!=game.activeWorldScale?L"World scale saved. Restart the game to apply it.":L"Select a value to change it.",240,685,1120,40);
            if(s.Dirty())Button(SettingsMenu::Discard,s.committed?L"BACK":L"DISCARD & BACK",!s.saving&&hovered(SettingsMenu::Discard));
            else Button(SettingsMenu::Back,L"BACK",hovered(SettingsMenu::Back));
            Button(SettingsMenu::Save,s.saving?L"SAVING...":s.Dirty()?L"SAVE & BACK":L"NO CHANGES",!s.saving&&s.Dirty()&&hovered(SettingsMenu::Save));
        }
        if(!GdiFlush()||drawError)return nullptr;
        auto data=static_cast<uint32_t*>(pixels);
        for(int y=std::max(0,area.y-2);y<std::min(SettingsMenu::Height,area.y+area.h+2);++y)for(int x=std::max(0,area.x-2);x<std::min(SettingsMenu::Width,area.x+area.w+2);++x){
            auto& pixel=data[y*SettingsMenu::Width+x];
            if(s.open&&area.Has(float(x),float(y)))pixel|=0xff000000;
            else if(!s.open&&pixel)pixel|=0xff000000; // only the shortcut silhouette is opaque
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
