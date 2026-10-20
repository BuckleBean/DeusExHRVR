#pragma once
#include "SharedPair.h"
#include <algorithm>
#include <cmath>

// Coordinates are normalized on the same full-screen quad as the game image.
// No desktop input is sent unless the foreground window belongs to this game.
namespace NativeMenuPointer {
inline bool Position(float x,float y,RECT client,POINT origin,RECT desktop,LONG& dx,LONG& dy) {
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>=1||y<0||y>=1||
       client.right<=client.left||client.bottom<=client.top||desktop.right-desktop.left<2||desktop.bottom-desktop.top<2)return false;
    double px=origin.x+x*(client.right-client.left-1),py=origin.y+y*(client.bottom-client.top-1);
    dx=LONG(std::clamp((px-desktop.left)*65535./(desktop.right-desktop.left-1),0.,65535.));
    dy=LONG(std::clamp((py-desktop.top)*65535./(desktop.bottom-desktop.top-1),0.,65535.));return true;
}
struct State {
    bool down{},armed{};
    // Losing focus/eligibility/ray releases our press and requires a new release.
    template<class Send> void Update(bool active,bool pressed,LONG x,LONG y,Send send) {
        if(!active){armed=false;if(down&&send(0,0,MOUSEEVENTF_LEFTUP))down=false;return;}
        if(!pressed)armed=true;
        bool want=pressed&&armed;
        DWORD flags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        if(want!=down)flags|=want?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
        if(send(x,y,flags))down=want;
    }
};
inline State state;
inline void Update(const Transport::MenuState& m,bool eligible,uint64_t now) {
    HWND window=GetForegroundWindow();DWORD pid{};GetWindowThreadProcessId(window,&pid);
    bool active=eligible&&Transport::Fresh(m.hostTick,now)&&m.pointerActive&&pid==GetCurrentProcessId();
    RECT client{};POINT origin{};LONG x{},y{};
    if(active){
        active=GetClientRect(window,&client)&&ClientToScreen(window,&origin);
        // Convert both corners to physical pixels for SendInput on scaled displays.
        POINT end{origin.x+client.right,origin.y+client.bottom};
        if(active){LogicalToPhysicalPointForPerMonitorDPI(window,&origin);LogicalToPhysicalPointForPerMonitorDPI(window,&end);client={0,0,end.x-origin.x,end.y-origin.y};}
        RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
        desktop.right=desktop.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);desktop.bottom=desktop.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
        active=active&&Position(m.pointerX,m.pointerY,client,origin,desktop,x,y);
    }
    state.Update(active,m.pointerDown!=0,x,y,[](LONG dx,LONG dy,DWORD flags){
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=dx;input.mi.dy=dy;input.mi.dwFlags=flags;
        return SendInput(1,&input,sizeof(input))==1;
    });
}
}
