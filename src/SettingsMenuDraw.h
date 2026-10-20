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
    void Text(const wchar_t* text,int x,int y,int w,int h,bool heading=false) {
        auto old=SelectObject(dc,heading?title:font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,heading?RGB(255,190,55):RGB(244,227,191));
        RECT r{x,y,x+w,y+h};if(!DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE))drawError=true;SelectObject(dc,old);
    }
    void Button(SettingsMenu::Rect r,const wchar_t* label,bool hover) {
        auto brush=CreateSolidBrush(hover?RGB(119,78,12):RGB(35,28,16));auto pen=CreatePen(PS_SOLID,2,RGB(210,150,30));
        auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
        POINT p[]={{r.x,r.y},{r.x+r.w-20,r.y},{r.x+r.w,r.y+20},{r.x+r.w,r.y+r.h},{r.x+20,r.y+r.h},{r.x,r.y+r.h-20}};
        if(!Polygon(dc,p,6))drawError=true;SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
        Text(label,r.x,r.y,r.w,r.h);
    }
    const void* Draw(const SettingsMenu::State& s,const Transport::MenuState& game) {
        if(!Init())return nullptr;
        drawError=false;
        // Complete any pending GDI work before direct writes to the DIB.
        if(!GdiFlush())return nullptr;
        memset(pixels,0,SettingsMenu::Width*SettingsMenu::Height*4);
        if(!s.visible)return pixels;
        auto area=s.open?SettingsMenu::Panel:SettingsMenu::Entry;
        RECT r{area.x,area.y,area.x+area.w,area.y+area.h};auto b=CreateSolidBrush(RGB(16,17,14));FillRect(dc,&r,b);DeleteObject(b);
        auto hovered=[&](SettingsMenu::Rect rect){return s.pointer&&rect.Has(s.x,s.y);};
        if(!s.open)Button(SettingsMenu::Entry,L"VR SETTINGS",hovered(SettingsMenu::Entry));
        else {
            Text(L"DEUS EX / VR SETTINGS",1040,255,490,60,true);
            Text(L"CONTROLS",1070,335,430,40,true);
            Text(L"Snap-turn angle",1070,380,430,40);
            wchar_t value[40];swprintf_s(value,L"%.6g\u00b0",double(game.degrees));Text(value,1180,430,210,80,true);
            Button(SettingsMenu::Minus,L"-",hovered(SettingsMenu::Minus));Button(SettingsMenu::Plus,L"+",hovered(SettingsMenu::Plus));
            const wchar_t* status=!game.snapEnabled?L"Snap turning is disabled":s.request!=game.acknowledged?L"Applying...":
                game.result==2?L"Change refused":game.result==3?L"Applied - could not save":game.saved!=game.acknowledged?L"Applied - saving...":game.acknowledged?L"Saved":L"Current setting";
            Text(status,1040,535,490,45);Text(L"Trigger selects / stick adjusts",1040,585,490,40);
            Button(SettingsMenu::Back,L"BACK",hovered(SettingsMenu::Back));
        }
        if(!GdiFlush()||drawError)return nullptr;
        auto data=static_cast<uint32_t*>(pixels);
        for(int y=area.y;y<area.y+area.h;++y)for(int x=area.x;x<area.x+area.w;++x)data[y*SettingsMenu::Width+x]|=0xff000000;
        if(s.pointer) {
            int px=int(s.x),py=int(s.y);
            for(int y=std::max(0,py-5);y<std::min(SettingsMenu::Height,py+6);++y)
                for(int x=std::max(0,px-5);x<std::min(SettingsMenu::Width,px+6);++x)data[y*SettingsMenu::Width+x]=0xffffffff;
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
