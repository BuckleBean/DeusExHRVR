#pragma once
// Hand HUD (the HUD on the hand/wrist): on when the HandHud setting is on
// (default); the desktop and isolation checks stay marker-gated test modes.
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace HudIsolationCheck {
#if defined(_M_IX86)
inline uintptr_t image{};
inline uintptr_t VA(uintptr_t p) { return image+p-0x400000; }
struct Value { void* interfacePtr{}; uint32_t type{}; double number{}; };
static_assert(sizeof(Value)==16 && offsetof(Value,number)==8);
struct Slot {
    uintptr_t vtable;
    const char* path;
    double offset;
    void* controller{};
    void* movie{};
    unsigned attempts{}, observations{}, generation{};
    bool applied{}, finished{}, described{};
    double original{}, target{};
    // Hand HUD mode: times the game put the element back and it was parked
    // again, and times its clip could not be found after it had been.
    unsigned reparks{}, lost{};
    bool missing{};
};
inline Slot slots[]={
    {0xABBD3C,"_root.mainHealthBar_mc.healthBar_mc._x",240},
    {0xABB3AC,"_root.mainHealthBar_mc.energyBar_mc._x",240},
    {0xABF81C,"_root.mainWeaponIndicator_mc.weaponInfo_mc._x",-240},
    {0xABF63C,"_root.mainWeaponIndicator_mc.grenadeInfo_mc._x",-240},
    {0xABDCFC,"_root.mainRadar_mc.radar_mc._x",240},
    // November 15 (Mark: "Let's also get the augs and mark and track added to
    // the wrist"). Hand HUD mode only. Controllers found from the game's RTTI
    // (the same method gives 0xABBD3C and 0xABDCFC above); both updates call
    // the hooked base update 0x7C06C0 first (0x7D9740, 0x80F7F0).
    // The aug shortcut cross and passive-aug icons: NsAugmentationWheelMovieController,
    // movie "augmentationbars" (87481), _root.mainAugmentationBar_mc.
    {0xAB8C4C,"_root.mainAugmentationBar_mc._x",-240},
    // The Mark & Track counter: the interface database lists MARK&TRACK and
    // HOLDTOMARK under "StealthEnhancer" (NsStealthEnhancerMovieController);
    // movie 87503 holds _root.Main.markingCounter_mc.
    {0xABED1C,"_root.Main.markingCounter_mc._x",240}
};
inline bool desktopMode{}, handHudMode{}, compareRequested{};
// Hand HUD mode (HandHud setting, [VR], default 1) runs in VR. It addresses
// and parks the elements exactly as the desktop check does; EngineCamera then
// redraws their draws on a hand. handHudActive is set only once this header's
// hooks are in place, and is what EngineCamera tests.
inline std::atomic<bool> handHudActive{};
// Live switch (Nov 17): in VR the hand HUD hooks are always installed and the
// HUD setting only chooses what they do. Shown: park and hide as above, and
// EngineCamera redraws on the hand (handHudActive). Not shown: every change is
// put back once (each element's own recorded x, each hidden plate's recorded
// value) and the game's HUD is then left exactly as the game draws it.
// Written by the present thread, read by the game thread.
inline std::atomic<bool> handHudShown{true}, handHooksReady{};
inline void SetHandHudShown(bool shown);
inline bool Requesting() { return desktopMode || handHudMode; }
inline const char* LogName() {
    return desktopMode?"DeusExHRVR-desktop-hud.log":handHudMode?"DeusExHRVR-hand-hud-quiet.log":"DeusExHRVR-hud-isolation.log";
}
// Desktop check: the offset wanted for each element, in movie units, and a
// counter that changes whenever any of them does. Written by the present
// thread, read by the game thread. Each slot re-arms itself when it sees a new
// generation, so slot state is only ever changed on the game thread.
inline constexpr int slotCount=7;
inline std::atomic<int> requested[slotCount]{};
inline std::atomic<unsigned> generation{1};
inline std::atomic<unsigned> appliedGeneration[slotCount]{};
inline std::atomic<bool> resolved[slotCount]{};
inline void Request(int health,int energy,int ammo) {
    requested[0]=health;requested[1]=energy;requested[2]=ammo;++generation;
}
inline const char* status[slotCount]={"not seen","not seen","not seen","not seen","not seen","not seen","not seen"};
// Scale between each element's clip and the HUD's tilted plane: its parent
// clip's _xscale times its wrapper movie's _xscale, read from the game once per
// controller reset. 0 = not measured. The radar's parking signal is scaled by
// its value relative to health's (health measured exact on the desktop).
inline std::atomic<float> chainScale[slotCount]{};
inline bool chainTried[slotCount]{};
inline std::atomic<float> radarScale{};
inline char censusText[200]="waiting for the HUD";
inline HWND window{};
inline void Title() {
#if !defined(HUD_ISOLATION_TEST)
    if(!desktopMode || !window)return;
    char title[512];snprintf(title,sizeof(title),
        "DXHR HUD RECOVERY | %s | health: %s | energy: %s | ammo: %s | consumable: %s | radar: %s",
        censusText,status[0],status[1],status[2],status[3],status[4]);
    SetWindowTextA(window,title);
#endif
}
// Switches all three between original and moved positions.
inline void Toggle() {
    compareRequested=!compareRequested;
    Request(compareRequested?480:0,compareRequested?480:0,compareRequested?-480:0);
    Title();
}
// Logging is opt-in in hand HUD mode (Mark, November 13: "I don't like having
// extraneous stuff running"). Off unless DeusExHRVR-hand-hud-log.enable is in
// the game folder; set once at startup (Install). When off, only problems are
// written (lines whose text says FAIL or NOT FOUND), and the diagnostics that
// only feed the log (watcher, summaries, census, samples) do not run. The
// desktop check and the isolation check always log.
inline bool verbose{true};
inline bool Problem(const char* text) { return strstr(text,"FAIL") || strstr(text,"NOT FOUND"); }
inline unsigned logLines{};
inline void Log(const char* event,const Slot* s=nullptr,double value=0) {
    if(Requesting() && s) {
        status[s-slots]=event;Title();
    }
    if(!verbose && !Problem(event))return;
    if(logLines++>=64)return;
    FILE* f{};
    if(!fopen_s(&f,LogName(),"a")) {
        fprintf(f,"%s path=%s original=%.3f target=%.3f observed=%.3f\n",
            event,s?s->path:"-",s?s->original:0,s?s->target:0,value);
        fclose(f);
    }
}
// Bounded one-off diagnostics for the desktop check. Never per frame.
inline unsigned detailLines{};
inline void Detail(const char* format,...) {
#if !defined(HUD_ISOLATION_TEST)
    if(!verbose && !Problem(format))return;
    if(detailLines++>=160)return;
    FILE* f{};
    if(fopen_s(&f,LogName(),"a"))return;
    va_list arguments;va_start(arguments,format);
    vfprintf(f,format,arguments);va_end(arguments);
    fputc(10,f);fclose(f);
#else
    (void)format;
#endif
}
// Hand HUD mode events with their time since the first one, on their own
// budget. Log() and Detail() are spent within the first minutes of a session
// (wristfix run, November 13: the log stops at 12:00 PM), so a later revert of
// the layout (after hacking, Mark) left no trace. Never per frame.
// HUD rebuilds seen by the slots (each element's first sight of a new clip).
inline std::atomic<unsigned> hudRebuilds{};
inline unsigned noteLines{};
inline uint64_t noteStart{};
inline void Note(const char* format,...) {
#if !defined(HUD_ISOLATION_TEST)
    // About 300 bytes a line: 6,000 lines is under 2 MB, and at the steady
    // rate (about 6 lines every 10 s) lasts about 2.8 hours. Sized so the log
    // does not run out in a normal session; Mark should not have to time play
    // around it (November 13).
    if(!verbose && !Problem(format))return;
    if(noteLines++>=6000)return;
    const uint64_t now=GetTickCount64();
    if(!noteStart)noteStart=now;
    FILE* f{};
    if(fopen_s(&f,LogName(),"a"))return;
    fprintf(f,"[t=%.1fs] ",double(now-noteStart)/1000.0);
    va_list arguments;va_start(arguments,format);
    vfprintf(f,format,arguments);va_end(arguments);
    fputc(10,f);fclose(f);
#else
    ++noteLines;(void)format;
#endif
}
// Diagnostics read game-owned pointers that this check has not validated.
// A bad one must cost a log line, not the session.
inline void* SafePointer(void* object,size_t offset) {
    if(!object)return nullptr;
    __try { return *reinterpret_cast<void**>(static_cast<char*>(object)+offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
inline void SafeText(const void* source,char* out,size_t size) {
    if(!source){strcpy_s(out,size,"(null)");return;}
    __try {
        auto text=static_cast<const unsigned char*>(source);
        size_t n=0;
        for(;n+1<size && text[n];++n)out[n]=(text[n]>=32 && text[n]<127)?char(text[n]):char(63);
        out[n]=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { strcpy_s(out,size,"(unreadable)"); }
}
using Init=void(__thiscall*)(void*,void*,void*);
using Update=void(__thiscall*)(void*,float);
inline Init originalInit{};
inline Update originalUpdate{};
inline Slot* Match(void* self) {
    const auto vt=*reinterpret_cast<uintptr_t*>(self);
    // WeaponIndicator update calls this base at 0x816382. The earlier
    // WeaponInfo-only selection produced no observation in the headset session.
    // Select the live parent controller, but still address only weaponInfo.
    for(auto& s:slots) {
        if(&s>=slots+3 && !handHudMode)continue;
        if(vt==VA(Requesting() && &s==&slots[2]?0xABF63C:s.vtable))return &s;
    }
    return nullptr;
}
inline void Reset(Slot& s,void* controller,void* movie) {
    s.controller=controller;s.movie=movie;s.attempts=s.observations=s.generation=0;
    s.applied=s.finished=s.described=false;s.original=s.target=0;
    resolved[&s-slots]=false;appliedGeneration[&s-slots]=0;chainTried[&s-slots]=false;
}
// A controller holds one of two movie wrappers at +0x20 and always addresses it
// through that wrapper's vtable (0x7C0C50, 0x7C0D50, 0x7C07B0).
// Slot 23 (+0x5C) is the getter and slot 24 (+0x60) the setter on both.
//   Root  0xAB022C: 0x72F8E0 / 0x72F8F0 forward straight to the GFx movie (+0x4C).
//   Child 0xAAFDCC: 0x729DC0 / 0x729E20 first call 0x7298F0, which rewrites the
//         path with the prefix held at child+0x50: "_root." is dropped and the
//         rest is joined as "%s.%s"; then they forward to the parent (+0x40).
// The November 11/12 checks unwrapped a child to its parent and called the
// parent getter with the authored "_root..." path, skipping that rewrite.
// This version never unwraps: it calls the live wrapper through its vtable,
// exactly as the game does, so the path is qualified whenever the game would.
// Confirmed live on November 12: all three controllers hold prefixed children.
enum class Kind { None, Root, Child };
inline uintptr_t rootVtable{}, childVtable{}, rootGet{}, rootSet{}, childGet{}, childSet{};
inline void BindNative() {
    rootVtable=VA(0xAB022C);childVtable=VA(0xAAFDCC);
    rootGet=VA(0x72F8E0);rootSet=VA(0x72F8F0);
    childGet=VA(0x729DC0);childSet=VA(0x729E20);
}
inline Kind KindOf(void* movie) {
    const auto vt=reinterpret_cast<uintptr_t>(SafePointer(movie,0));
    if(!vt)return Kind::None;
    if(vt!=rootVtable && vt!=childVtable)return Kind::None;
    const auto table=reinterpret_cast<const uintptr_t*>(vt);
    if(vt==rootVtable)return table[23]==rootGet && table[24]==rootSet?Kind::Root:Kind::None;
    return table[23]==childGet && table[24]==childSet?Kind::Child:Kind::None;
}
inline uintptr_t rejectedVtable{};
inline void* LiveWrapper(void* movie) {
    rejectedVtable=0;
    if(KindOf(movie)!=Kind::None)return movie;
    rejectedVtable=reinterpret_cast<uintptr_t>(SafePointer(movie,0));
    return nullptr;
}
using Getter=bool(__thiscall*)(void*,Value*,const char*);
using Setter=bool(__thiscall*)(void*,const char*,const Value*);
inline unsigned readType{};inline bool readSucceeded{};
// `wrapper` must have passed LiveWrapper in this same update.
inline bool Read(void* wrapper,const char* path,double& number) {
    Value value{};
    const auto table=*reinterpret_cast<const uintptr_t* const*>(wrapper);
    bool ok=reinterpret_cast<Getter>(table[23])(wrapper,&value,path);
    readType=value.type;readSucceeded=ok;
    ok=ok && value.type==3 && std::isfinite(value.number) && std::abs(value.number)<100000;
    if(ok)number=value.number;
#if !defined(HUD_ISOLATION_TEST)
    // Unexpected object/string results must also release their managed
    // reference: the native value assignment (0x7C0A10) tests bit 0x40 and
    // calls 0x858AB0 on the value interface with the value and its data.
    if((value.type&0x40) && value.interfacePtr) {
        void* data{};memcpy(&data,&value.number,sizeof(data));
        reinterpret_cast<void(__thiscall*)(void*,Value*,void*)>(VA(0x858AB0))(
            value.interfacePtr,&value,data);
    }
#endif
    return ok;
}
// A true/false property such as _visible. The game returns it as a GFx
// boolean (type 2, the flag in the value's first byte), which Read refuses:
// Mark's revision 3 and 5 runs logged "unnamed-plate FAILED write ...
// instance391._visible" 16 times per session although the three dots were
// hidden in his screenshots, and the hide was retried every update. A number
// (type 3) is also accepted, as 0 or not.
inline bool ReadFlag(void* wrapper,const char* path,bool& flag) {
    Value value{};
    const auto table=*reinterpret_cast<const uintptr_t* const*>(wrapper);
    bool ok=reinterpret_cast<Getter>(table[23])(wrapper,&value,path);
    readType=value.type;readSucceeded=ok;
    if(ok && value.type==2){unsigned char b{};memcpy(&b,&value.number,1);flag=b!=0;return true;}
    if(ok && value.type==3 && std::isfinite(value.number)){flag=value.number!=0;return true;}
    return false;
}
inline bool Write(void* wrapper,const char* path,double number) {
    Value value{};value.type=3;value.number=number;
    const auto table=*reinterpret_cast<const uintptr_t* const*>(wrapper);
    return reinterpret_cast<Setter>(table[24])(wrapper,path,&value);
}
// Records what the live objects actually are: wrapper type, prefix, parent,
// GFx movie, and the component path the game configured.
inline void Describe(const Slot& s,void* controller,void* liveMovie) {
    void* node=liveMovie;
    for(int depth=0;node && depth<4;++depth) {
        const Kind kind=KindOf(node);
        if(kind==Kind::Child) {
            char prefix[160];SafeText(SafePointer(node,0x50),prefix,sizeof(prefix));
            void* parent=SafePointer(node,0x40);
            Detail("wrapper path=%s depth=%d kind=child object=%p parent=%p prefix=\"%s\"",
                s.path,depth,node,parent,prefix);
            node=parent;
        } else if(kind==Kind::Root) {
            Detail("wrapper path=%s depth=%d kind=root object=%p gfx=%p",
                s.path,depth,node,SafePointer(node,0x4C));
            break;
        } else {
            const auto vt=reinterpret_cast<uintptr_t>(SafePointer(node,0));
            Detail("wrapper path=%s depth=%d kind=unknown object=%p vtable-preferred=%08x",
                s.path,depth,node,unsigned(vt?vt-image+0x400000:0));
            break;
        }
    }
    if(void* config=SafePointer(controller,0x28)) {
        char component[200],prefix[80];
        SafeText(SafePointer(config,8),component,sizeof(component));
        SafeText(SafePointer(config,0xC),prefix,sizeof(prefix));
        Detail("config path=%s component=\"%s\" function-prefix=\"%s\"",s.path,component,prefix);
    } else Detail("config path=%s none",s.path);
}
// After a failed lookup: which part of the authored path is the missing one?
// Reads `_x` on each ancestor, nearest first, through the same live wrapper.
inline void ProbeAncestors(const Slot& s,void* wrapper) {
    char path[192]{};
    strncpy_s(path,s.path,strlen(s.path)-3); // without the trailing "._x"
    for(int level=0;level<4;++level) {
        char* dot=strrchr(path,46);
        if(!dot)break;
        *dot=0;
        char probe[200];snprintf(probe,sizeof(probe),"%s._x",path);
        double x{};const bool numeric=Read(wrapper,probe,x);
        Detail("probe path=%s numeric=%d getter-success=%d value-type=%u value=%.3f",
            probe,int(numeric),int(readSucceeded),readType,numeric?x:0.0);
    }
}
// After a successful lookup: where the clip sits and how large it is.
inline void Geometry(const Slot& s,void* wrapper,double x) {
    static const char* const names[]={"_y","_width","_height","_xscale","_yscale"};
    double values[5]{};bool ok[5]{};
    for(int i=0;i<5;++i) {
        char path[200];snprintf(path,sizeof(path),"%.*s.%s",int(strlen(s.path)-3),s.path,names[i]);
        ok[i]=Read(wrapper,path,values[i]);
    }
    Detail("geometry path=%s _x=%.3f _y=%.3f(%d) _width=%.3f(%d) _height=%.3f(%d) _xscale=%.3f(%d) _yscale=%.3f(%d)",
        s.path,x,values[0],int(ok[0]),values[1],int(ok[1]),values[2],int(ok[2]),
        values[3],int(ok[3]),values[4],int(ok[4]));
}
// Product of the _xscale of a slot's parent clip and of the wrapper movie
// holding it, over 100*100. Logs each factor and the wrapper's ancestors with
// their rotations, once. Returns 0 when any factor cannot be read.
inline float ChainScale(const Slot& s,void* wrapper) {
    char parent[192]{};
    strncpy_s(parent,s.path,strlen(s.path)-3); // without the trailing "._x"
    char* dot=strrchr(parent,46);if(!dot)return 0;
    *dot=0;                                     // without the clip itself
    char path[240];double own=100,outer=100;
    bool ok=true;
    if(strchr(parent,46)) {                     // "_root" alone is the wrapper itself
        snprintf(path,sizeof(path),"%s._xscale",parent);
        ok=Read(wrapper,path,own);
    }
    char prefix[160]="(root wrapper)";
    if(ok && KindOf(wrapper)==Kind::Child) {
        SafeText(SafePointer(wrapper,0x50),prefix,sizeof(prefix));
        void* top=SafePointer(wrapper,0x40);
        if(!top || KindOf(top)!=Kind::Root || !prefix[0] || prefix[0]==40)ok=false;
        else {
            snprintf(path,sizeof(path),"%s._xscale",prefix);
            ok=Read(top,path,outer);
            // Context for the log only: the wrapper and its ancestors.
            char node[160];strcpy_s(node,prefix);
            static const char* const names[]={"_xscale","_yscale","_rotation","_xrotation","_yrotation","_zrotation"};
            for(int level=0;level<4 && node[0];++level) {
                double v[6]{};int got[6]{};
                for(int k=0;k<6;++k){snprintf(path,sizeof(path),"%s.%s",node,names[k]);got[k]=Read(top,path,v[k]);}
                Detail("chain-node slot=%s node=%s xscale=%.3f(%d) yscale=%.3f(%d) rotation=%.3f(%d) xrotation=%.3f(%d) yrotation=%.3f(%d) zrotation=%.3f(%d)",
                    s.path,node,v[0],got[0],v[1],got[1],v[2],got[2],v[3],got[3],v[4],got[4],v[5],got[5]);
                char* cut=strrchr(node,46);if(!cut)break;*cut=0;
            }
        }
    }
    const float product=ok && own>0 && outer>0?float(own*outer/10000.0):0.f;
    Detail("chain-scale slot=%s parent=%s parent-xscale=%.3f wrapper=%s wrapper-xscale=%.3f product=%.4f ok=%d",
        s.path,parent,own,prefix,outer,product,int(ok));
    return product;
}
inline void MeasureChain(int index,void* wrapper) {
    if(chainTried[index])return;
    chainTried[index]=true;
    chainScale[index]=ChainScale(slots[index],wrapper);
    const float health=chainScale[0].load(),radar=chainScale[4].load();
    if(health>0 && radar>0 && !(radarScale.load()>0)) {
        radarScale=radar/health;
        Detail("radar-scale health=%.4f radar=%.4f ratio=%.4f",health,radar,radarScale.load());
    }
}
inline void __fastcall InitHook(void* self,void*,void* movie,void* config) {
    originalInit(self,movie,config);
    if(auto s=Match(self)) {
        Reset(*s,self,movie);
        if(handHudMode && s==&slots[2])Reset(slots[3],self,movie);
    }
}
inline void UpdateSlot(void* self,Slot* s) {
    const int index=int(s-slots);
    auto liveMovie=*reinterpret_cast<void**>(static_cast<char*>(self)+0x20);
    if(s->controller!=self || s->movie!=liveMovie)Reset(*s,self,liveMovie);
    // A new request re-arms the one-write rule for this element.
    const unsigned wanted=generation.load();
    if(Requesting() && s->generation!=wanted){s->generation=wanted;s->finished=false;s->observations=0;}
    if(s->finished)return;
    // Retry misses at a bounded cadence rather than exhausting the whole probe
    // before the gameplay movie exists.
    if(Requesting() && s->attempts && (s->attempts++ % 60)!=0)return;
    if(Requesting() && !s->described){s->described=true;Describe(*s,self,liveMovie);}
    auto movie=LiveWrapper(liveMovie);
    double x{};
    if(!movie || !Read(movie,s->path,x)) {
        if(handHudMode && s->applied && !s->missing) {
            s->missing=true;++s->lost;
            Note("slot-lost path=%s (the HUD clip is gone; looking for it again every 60 updates, never giving up)",s->path);
        }
        if(Requesting()) {
            if(!s->attempts) {
                Log(!movie?"UNRESOLVED movie wrapper":"UNRESOLVED property",s);
                Detail("lookup-detail path=%s wrapper=%p rejected-vtable-preferred=%08x getter-success=%d value-type=%u",
                    s->path,liveMovie,unsigned(rejectedVtable?rejectedVtable-image+0x400000:0),
                    int(movie && readSucceeded),movie?readType:0u);
                if(movie)ProbeAncestors(*s,movie);
                s->attempts=1;
            } else if(s->attempts>3600 && !handHudMode)s->finished=true;
            // Hand HUD mode never gives up: hacking or a long menu can keep
            // the HUD away for minutes (Mark, November 13: the layout reverted
            // after hacking and stayed so after a save and reload).
        } else if(++s->attempts==120){s->finished=true;Log("lookup-failed",s);}
        return;
    }
    if(Requesting()) {
        if(handHudMode) {
            // Found: check every update from now on, not every 60th.
            if(s->missing){s->missing=false;Note("slot-found-again path=%s x=%.3f",s->path,x);}
            s->attempts=0;
        }
        char originalPath[192]{};
        snprintf(originalPath,sizeof(originalPath),"%.*s.__dxhrvrHudIsolationOriginalX",int(strlen(s->path)-3),s->path);
        double saved{};
        if(!Read(movie,originalPath,saved)) {
            // First sight of this clip: record its untouched layout before any write.
            if(handHudMode){++hudRebuilds;Note("slot-first-sight path=%s x=%.3f (new or rebuilt clip)",s->path,x);}
            Geometry(*s,movie,x);
            if(!Write(movie,originalPath,x) || !Read(movie,originalPath,saved) || std::abs(saved-x)>0.05) {
                s->finished=true;Log("FAILED original capture",s);return;
            }
        }
        s->original=saved;resolved[index]=true;
        if(handHudMode)MeasureChain(index,movie);
        const int offset=requested[index].load();
        s->target=saved+offset;
        if(!s->applied || std::abs(s->target-x)>0.05) {
            // Desktop check: only one write per request; subsequent native
            // overwrites are failures, not a reason to force the layout.
            // Hand HUD mode: the element belongs on the wrist for the whole
            // session, so whenever the game puts it back it is parked again.
            if(s->observations && !handHudMode){s->finished=true;Log("FAILED native override",s,x);return;}
            if(s->observations && handHudMode) {
                ++s->reparks;
                if(s->reparks<=40 || s->reparks%500==0)
                    Note("repark path=%s game-set x=%.3f target=%.3f count=%u",s->path,x,s->target,s->reparks);
            }
            double back{};
            if(!Write(movie,s->path,s->target) || !Read(movie,s->path,back) || std::abs(back-s->target)>0.05) {
                bool restored=Write(movie,s->path,saved) && Read(movie,s->path,back) && std::abs(back-saved)<=0.05;
                if(handHudMode) {
                    // Try again in 60 updates rather than leave the element on screen.
                    Note("park-write FAILED path=%s restored=%d (retrying every 60 updates)",s->path,int(restored));
                    s->applied=false;s->observations=0;s->attempts=1;return;
                }
                s->finished=true;Log(restored?"FAILED write; restored":"FAILED restore",s);return;
            }
            s->applied=true;
        }
        if(!s->observations) {
            Log(offset?"AFTER readback OK":"BEFORE readback OK",s,s->target);
            appliedGeneration[index]=wanted;
        }
        if(handHudMode){if(s->observations<1000000)++s->observations;return;}
        if(++s->observations==120)s->finished=true;
        return;
    }
    if(!s->applied) {
        // Keep the original on the clip itself, whose lifetime is the correct
        // one. Controller reinitialization/pointer reuse must not add another
        // offset to an already shifted movie.
        char originalPath[192]{};
        snprintf(originalPath,sizeof(originalPath),"%.*s.__dxhrvrHudIsolationOriginalX",
            int(strlen(s->path)-3),s->path);
        double saved{};
        if(!Read(movie,originalPath,saved)) {
            if(!Write(movie,originalPath,x) || !Read(movie,originalPath,saved) || std::abs(saved-x)>0.05) {
                s->finished=true;Log("original-capture-failed",s);return;
            }
        }
        s->original=saved;s->target=saved+s->offset;
        bool wrote=Write(movie,s->path,s->target);
        double readback{};
        if(!wrote || !Read(movie,s->path,readback) || std::abs(readback-s->target)>0.05) {
            // Restore the known original on any failed write/readback; report
            // restoration failure explicitly. Never retry and accumulate offsets.
            bool restored=Write(movie,s->path,s->original);
            double restoredX{};
            restored=restored && Read(movie,s->path,restoredX) && std::abs(restoredX-s->original)<=0.05;
            s->finished=true;Log(restored?"write-failed-restored":"RESTORE-FAILED",s);
            return;
        }
        s->applied=true;Log("moved-readback-confirmed",s,readback);
    } else if(std::abs(x-s->target)>0.05) {
        // Native animation/layout owns a changed value: do not fight it.
        s->finished=true;Log("native-layout-overrode-offset",s,x);
    } else if(++s->observations==120) {
        s->finished=true;Log("offset-persisted-120-updates",s,x);
    }
}

// ---- Backplates (hand HUD mode) ------------------------------------------
// The black/gold plates behind health, energy, ammo and the consumable look
// like a separate layer on the wrist (Mark, November 13). They are their own
// authored clips (2026-11-13-hand-hud-fit-candidate/backplate-findings.md,
// movie-87489.gfx / movie-87505.gfx), so they are hidden by setting their own
// _alpha to 0, leaving numbers, icons, cells, outlines of the cells, and the
// radar untouched.
// The health background (sprite 50, 74 frames) is also the damage cue: frame 1
// stops (healthy); frames 2-38, 39-61 and 62-74 are labelled onDamageLevel1-3
// and each ends with a jump back to its own label, so at a damage level the
// plate keeps pulsing until the game sends it back to frame 1 (read from the
// movie's DoAction tags, November 13). It is hidden only while it rests on
// frame 1 and shown, pulsing, whenever it plays a damage level.
struct Plate { int slot; const char* path; bool damageCue; int written; unsigned calls, logs;
                double original{}; bool haveOriginal{}; }; // value before the first hide (live switch)
inline Plate plates[]={
    // Health plate: hidden always (Mark, November 13: its damage pulse "is
    // not needed and looks bad"; the game's red screen already warns).
    {0,"_root.mainHealthBar_mc.healthBar_mc.background_mc",false,-1,0,0},
    {1,"_root.mainHealthBar_mc.energyBar_mc.outline_mc",false,-1,0,0},
    {1,"_root.mainHealthBar_mc.energyBar_mc.background_mc",false,-1,0,0},
    {2,"_root.mainWeaponIndicator_mc.weaponInfo_mc.outline_mc",false,-1,0,0},
    {2,"_root.mainWeaponIndicator_mc.grenadeInfo_mc.outline_mc",false,-1,0,0},
    // Revision 9: the dark plate behind "MARK&TRACK 3.0" (markingCounter_mc's
    // named bg_outline_mc, movie 87503), as for the plates above.
    {6,"_root.Main.markingCounter_mc.bg_outline_mc",false,-1,0,0},
};
inline std::atomic<unsigned> plateWrites{}, plateFailures{}, damageShows{};
// The black backgrounds behind ammo and the consumable (Mark, November 13:
// "Black background that should be hidden appears behind HUD elements on right
// hand"). They have no instance names in movie-87505.gfx: weaponInfo_mc
// (sprite 85) places sprite 65 at depth 1, x -5.75, y -10.65, and grenadeInfo_mc
// (sprite 100) places sprite 89 at depth 1, x 4.0, y 5.75, each with a colour
// transform of black at 192/256 alpha (read from their PlaceObject tags,
// November 13). Their shapes are 217.6 x 54.4 and 128 x 64, the first the same
// size as the gold outline already hidden.
// Scaleform names unnamed instances "instance<N>" (the pattern instance{0} is in
// DXHRDC.exe at 0x6ccf38; that it is the AS2 naming is an inference from
// Scaleform's Flash-compatible behaviour, not traced). N comes from a counter
// that is not known here, so the names are searched upward from 1, a bounded
// number per update, and a candidate is accepted only at the authored position.
struct UnnamedPlate {
    const char* parent; double x, y;
    char found[24]; unsigned next, pass, wait, calls, logs; int written; bool announced;
    const char* property="_alpha"; // set to 0
    int slot=2;                    // the controller whose movie holds it
    double original{}; bool haveOriginal{}; // value before the first hide (live switch)
};
// November 15: also the three dots right of the ammo count (Mark: "I'm not sure
// if those 3 dots next to the ammo are necessary. If that is limiting how large
// you can display the weapon/ammo, it might be worth removing that"). They are
// weaponInfo_mc's unnamed depth 20 (sprite 80: three copies of sprite 79) at
// x 185.85, y -8.65 (movie-87505.gfx PlaceObject tags). What the game uses them
// for is not identified. Hidden with _visible, not _alpha, so the game does not
// draw them and their origin does not enter the ammo's measured middle.
inline UnnamedPlate unnamedPlates[]={
    {"_root.mainWeaponIndicator_mc.weaponInfo_mc",-5.75,-10.65,{},1,0,0,0,0,-1,false},
    {"_root.mainWeaponIndicator_mc.grenadeInfo_mc",4.0,5.75,{},1,0,0,0,0,-1,false},
    {"_root.mainWeaponIndicator_mc.weaponInfo_mc",185.85,-8.65,{},1,0,0,0,0,-1,false,"_visible"},
    // Revision 9 (Mark, revision 8 run: the aug cross "has the black background
    // embedded in the hand"; "Feels like we're not applying the lessons we've
    // learned"): the aug cross's background, augmentationWheel_mc's unnamed
    // depth 1 (sprite 12, the conversation-wheel background texture, 256 x 128
    // at x -128, y -64; movie 87481), hidden like the dots: _visible, so it is
    // not drawn and stays out of the aug cross's measured positions.
    {"_root.mainAugmentationBar_mc.augmentationWheel_mc",-128.0,-64.0,{},1,0,0,0,0,-1,false,"_visible",5},
};
inline constexpr unsigned unnamedLimit=2000000;   // highest N tried in one pass
inline constexpr unsigned unnamedPerUpdate=128;   // names tried per plate per update
inline std::atomic<unsigned> unnamedLookups{};
inline bool AtPlate(void* movie,const UnnamedPlate& p,const char* name) {
    char path[200];double x{},y{};
    snprintf(path,sizeof(path),"%s.%s._x",p.parent,name);
    if(!Read(movie,path,x) || std::abs(x-p.x)>0.06)return false;
    snprintf(path,sizeof(path),"%s.%s._y",p.parent,name);
    return Read(movie,path,y) && std::abs(y-p.y)<=0.06;
}
inline void UnnamedPlates(void* movie,int slot) {
    // Never more than half a millisecond of searching per update, all of them together.
    LARGE_INTEGER start{},now{},frequency{};
    QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    for(auto& p:unnamedPlates) {
        if(p.slot!=slot)continue;
        const bool due=(p.calls++%30)==0;
        if(p.found[0] && due && !AtPlate(movie,p,p.found)) {
            Note("unnamed-plate lost parent=%s name=%s (searching again)",p.parent,p.found);
            p.found[0]=0;p.next=1;p.written=-1;p.haveOriginal=false;
        }
        if(!p.found[0]) {
            if(p.wait){--p.wait;continue;}
            for(unsigned n=0;n<unnamedPerUpdate && !p.found[0];++n) {
                char name[24];snprintf(name,sizeof(name),"instance%u",p.next);
                ++unnamedLookups;
                if(AtPlate(movie,p,name)) {
                    strcpy_s(p.found,name);
                    Note("unnamed-plate found parent=%s name=%s pass=%u lookups-so-far=%u",p.parent,name,p.pass,unnamedLookups.load());
                    break;
                }
                if(++p.next>unnamedLimit) {
                    p.next=1;++p.pass;p.wait=3600; // about a minute before another pass
                    Note("unnamed-plate NOT FOUND parent=%s after instance1..instance%u (pass %u); retrying in 3600 updates",p.parent,unnamedLimit,p.pass);
                    break;
                }
                QueryPerformanceCounter(&now);
                if(frequency.QuadPart && (now.QuadPart-start.QuadPart)*2000>frequency.QuadPart)break;
            }
            if(!p.found[0])continue;
        }
        if(!due && p.written==0)continue;
        char path[200];snprintf(path,sizeof(path),"%s.%s.%s",p.parent,p.found,p.property);
        const bool flag=!strcmp(p.property,"_visible");
        double back{};bool shown{};
        if(!p.haveOriginal) {
            // Record the value to put back if the HUD is switched to DEFAULT.
            // Unreadable: the authored default (visible, full alpha).
            bool was{};double value{};
            p.original=flag?(ReadFlag(movie,path,was)?(was?1.0:0.0):1.0):(Read(movie,path,value)?value:100.0);
            p.haveOriginal=true;
        }
        if(!Write(movie,path,0) || (flag?!ReadFlag(movie,path,shown) || shown:!Read(movie,path,back) || std::abs(back)>=1.0)) {
            ++plateFailures;
            if(p.logs<16){++p.logs;Note("unnamed-plate FAILED write path=%s",path);}
            p.written=-1;continue;
        }
        if(p.written!=0 && !p.announced){p.announced=true;Note("unnamed-plate hidden path=%s",path);}
        p.written=0;++plateWrites;
    }
}
// Animations that must not move a piece on the wrist (Mark, November 15:
// "Reloading moves the HUD and then it goes back"). On the wrist ends each
// piece is centred from the spread of its own draws' origins over a rolling
// window (BandSample). weaponInfo_mc's timeline (sprite 85, movie 87505) rests
// on frame 1 "Normal" or 2 "Laser"; "Reload" (frame 3 on), "loopReload",
// "EndReload" and "Empty"/"loopEmpty"/"EndEmpty" place an extra part (depth 26,
// characters 82/84, at x 128.4, y -1.55), whose draws widened the spread and
// slid the ammo until the window passed them. grenadeInfo_mc (sprite 100) rests
// on frame 1 "Normal"; its "Empty"/"EmptyThrow" frames move a child 33 px.
// Each update the two frames are read; while a piece is off its resting
// frames, and for 30 updates after, its draws are not sampled, so its centre
// stays where it was. Unreadable: sampled as before.
inline constexpr int kSteadyAfter=30;
inline std::atomic<int> ammoQuiet{kSteadyAfter}, consumableQuiet{kSteadyAfter};
inline unsigned frameNotes{};
inline double lastFrames[2]{-1,-1};
inline void ReadAnimating(void* movie) {
    int which=0;
    auto one=[&](const char* path,double lastRestFrame,std::atomic<int>& quiet) {
        double frame{};
        const bool ok=Read(movie,path,frame);
        // Diagnostics (logging switched on): every change of frame, and a
        // failed read once.
        double& last=lastFrames[which++];
        const double seen=ok?frame:-2;
        if(seen!=last && frameNotes<400){++frameNotes;Note("%s frame=%g%s",path,seen,ok?"":" (read failed)");}
        last=seen;
        if(!ok){quiet=kSteadyAfter;return;}
        if(frame>lastRestFrame+.5)quiet=0;
        else if(quiet<kSteadyAfter)++quiet;
    };
    one("_root.mainWeaponIndicator_mc.weaponInfo_mc._currentframe",2,ammoQuiet);
    one("_root.mainWeaponIndicator_mc.grenadeInfo_mc._currentframe",1,consumableQuiet);
}
// Whether a band's draws may be sampled for its middle now (bands as above:
// 1 ammo, 3 consumable; the others always).
inline bool BandSteady(int band) {
    return band==1?ammoQuiet.load()>=kSteadyAfter:band==3?consumableQuiet.load()>=kSteadyAfter:true;
}
// Called on the game thread after a controller's update. Static plates are
// set, then re-set every 30 updates (a rebuilt HUD movie starts them visible
// again). The health background's frame is read every update; its alpha is
// written when that changes, and re-set every 30 updates.
inline void Backplates(void* controller,int slot) {
    if(!handHudMode)return;
    void* movie=LiveWrapper(SafePointer(controller,0x20));
    if(!movie)return;
    UnnamedPlates(movie,slot);
    if(slot==2)ReadAnimating(movie);
    for(auto& p:plates) {
        if(p.slot!=slot)continue;
        const bool due=(p.calls++%30)==0;
        char path[200];
        int alpha=0;
        if(p.damageCue) {
            double frame{};
            snprintf(path,sizeof(path),"%s._currentframe",p.path);
            if(!Read(movie,path,frame)) {
                ++plateFailures;
                if(p.logs<16){++p.logs;Detail("backplate FAILED read path=%s",path);}
                continue;
            }
            alpha=frame>1.5?100:0;
        }
        if(!due && alpha==p.written)continue;
        snprintf(path,sizeof(path),"%s._alpha",p.path);
        double back{};
        if(!p.haveOriginal){double value{};p.original=Read(movie,path,value)?value:100.0;p.haveOriginal=true;}
        const bool ok=Write(movie,path,alpha) && Read(movie,path,back) && std::abs(back-alpha)<1.0;
        if(!ok) {
            ++plateFailures;
            if(p.logs<16){++p.logs;Detail("backplate FAILED write path=%s alpha=%d",path,alpha);}
            continue;
        }
        if(alpha!=p.written && p.logs<16) {
            ++p.logs;
            Detail("backplate path=%s alpha=%d%s",p.path,alpha,
                p.damageCue?(alpha?" (damage animation playing)":" (resting, healthy)"):"");
        }
        if(p.damageCue && alpha && p.written!=alpha)++damageShows;
        p.written=alpha;++plateWrites;
    }
}
// HUD switched to DEFAULT: put back what the hand HUD changed on this
// controller's movie, once; afterwards nothing is written. Game thread.
inline void Unpark(void* self,Slot* s) {
    const int index=int(s-slots);
    auto liveMovie=*reinterpret_cast<void**>(static_cast<char*>(self)+0x20);
    void* movie=LiveWrapper(liveMovie);
    if(s->controller!=self || s->movie!=liveMovie)Reset(*s,self,liveMovie); // a new movie starts as authored
    else if(movie && s->applied && resolved[index])Write(movie,s->path,s->original);
    s->applied=false;s->observations=0;s->generation=0;s->finished=false;
    if(!movie)return;
    char path[200];
    for(auto& p:plates)if(p.slot==index && p.haveOriginal) {
        snprintf(path,sizeof(path),"%s._alpha",p.path);
        Write(movie,path,p.original);p.haveOriginal=false;p.written=-1;
    }
    for(auto& p:unnamedPlates)if(p.slot==index && p.haveOriginal && p.found[0]) {
        snprintf(path,sizeof(path),"%s.%s.%s",p.parent,p.found,p.property);
        Write(movie,path,p.original);p.haveOriginal=false;p.written=-1;
    }
}
inline void __fastcall UpdateHook(void* self,void*,float elapsed) {
    originalUpdate(self,elapsed);
    auto s=Match(self);if(!s)return;
    if(handHudMode && !handHudShown.load()) {
        Unpark(self,s);
        if(s==&slots[2])Unpark(self,&slots[3]);
        return;
    }
    UpdateSlot(self,s);
    if(handHudMode)Backplates(self,int(s-slots));
    // Ammo and consumable are siblings serviced by the same native controller.
    // Run the native update once, then process both independent clip requests.
    if(handHudMode && s==&slots[2])UpdateSlot(self,&slots[3]);
}

// ---- Recovery preview (desktop only) ---------------------------------------
// The census (2026-11-12-hud-draw-census-candidate) showed that an element
// parked far off screen is still drawn, and that its draws can be recognised
// from the Scaleform constants block each draw carries. This build asks the
// next question: can the mod put those draws back on screen itself, exactly,
// without touching anything else?
//
// What the game computes for a HUD vertex (vertex shaders 5412c91c6c582d9a,
// 0e624d71c99a22c3 and b22b03cd660e6c41, read from their disassembly):
//     X = dot(xRow, v)   Y = dot(yRow, v)   Z = dot(depthRow, v)
//     clip = X*WVP[0] + Y*WVP[1] + Z*WVP[2] + WVP[3]
// depthRow is the block at primitiveState+0x20 that the census recorded.
// 0x550930 builds WVP as World * View * Projection (World at state+0x500).
// Moving an element by D movie units adds D*20 to X and sine*D*20 to Z, where
// sine is the region's tilt (measured: 6.84 per unit = 20*sin 20 degrees, and
// the block's third number is the matching cosine). Because clip is linear in
// X, Y and Z, a World matrix that translates by the opposite of the move
// undoes it exactly, and any further translation places the element elsewhere.
//
// How far X moves was measured by the first run of this preview (3:09 PM,
// November 12), which assumed D*20 and was wrong. In the engine's own buffers
// only two numbers change when an element is parked: the X row's translation
// and the depth row's translation. Within a screen region the X row is the
// depth row times a constant `ratio` (plus a region constant): 3.57169 top
// left, -2.74748 bottom right, to five decimals over every draw. So
//     X moves by ratio * (sine*D*20)      Z moves by sine*D*20
// The ratio differs per region (it contains that region's HUD scale), so it
// is not assumed: it is read from the engine's buffer after each tilted draw,
// as xRow[0]/depthRow[0], and used for the draws that follow in that region.
// Until a region's ratio has been seen, its parked draws are left alone.
// World is identity for HUD draws (census), so it is set for one draw and put
// back. A draw whose World is not identity is left alone and counted.
//
// The run cycles three modes, five seconds each, shown in the window title:
//   NORMAL    nothing parked, nothing changed.
//   RESTORED  all three parked off screen; their draws translated back.
//             Should look the same as NORMAL.
//   SHIFTED   as RESTORED, plus a visible offset toward the screen centre.
// For the first two cycles one frame per mode is recorded together with the
// engine's own constant buffers after the draw, so the result can be checked
// numerically as well as by eye.
enum class Mode : int { Normal=0, Restored=1, Shifted=2 };
inline constexpr int modeCount=3;
inline constexpr const char* modeText[modeCount]={
    "1 of 3: NORMAL",
    "2 of 3: RESTORED - should look the same as NORMAL",
    "3 of 3: SHIFTED - the three blocks sit nearer the middle"};
// Parking distance for health, energy and ammo, in movie units. These are the
// distances the census measured and ChatGPT's replay verified.
// Aug cross and Mark & Track (slots 6 and 7): 12,000, outside the bands the
// backup recogniser uses (5-7k, 8-10k), so it never claims them; right side
// negative as for ammo, left side positive as for health.
inline constexpr int park[slotCount]={6000,9000,-6000,-9000,6000,-12000,12000};
inline constexpr int shiftX[slotCount]={600,600,-600,0,0,0,0};
inline constexpr int shiftY[slotCount]={300,300,-300,0,0,0,0};
inline constexpr float twips=20.f;
inline std::atomic<int> mode{0};
inline std::atomic<unsigned> fixFaults{}, recordFaults{}, nonIdentityWorld{}, fixedDraws{}, noRatio{}, learnFaults{};
using Matrices=void(__thiscall*)(void*);
inline Matrices matrices{};

// 0 = not one of the three parked elements; 1 health, 2 energy, 3 ammo.
// `sine` receives the signed tilt factor that ties Z to X for this draw.
inline int Classify(const float* p,const float* c,float& sine) {
    for(int i=0;i<12;++i)if(!std::isfinite(c[i]))return 0;
    if(!std::isfinite(p[8]) || !std::isfinite(p[9]))return 0;
    // The second vector is (0,0,0,1) in every HUD draw seen; anything else is not this layout.
    // c[8..11] are not part of the test: after a hack they hold
    // -8.34, 0, 95.34, 95.7 on every HUD draw, in the normal corners and
    // tilts (hackdiag run, November 13, 155.8 s), and requiring zero there
    // rejected the whole HUD. What the game uses them for is not known; the
    // wrist band shader does not read them (b3, b5, b11 only).
    if(c[4]!=0 || c[5]!=0 || c[6]!=0 || c[7]!=1)return 0;
    // An untilted draw carries no position in this block. Reject, do not guess.
    if(!(c[2]>0.5f && c[2]<0.9999f))return 0;
    const float magnitude=std::sqrt(1.f-c[2]*c[2]);
    const float units=c[3]/magnitude/twips;
    const bool topLeft=p[8]>0.3f && p[9]<-0.3f;
    const bool bottomRight=p[8]<-0.3f && p[9]>0.3f;
    int tag=0;
    if(topLeft && units>5000 && units<7000)tag=1;
    else if(topLeft && units>8000 && units<10000)tag=2;
    else if(bottomRight && units>5000 && units<7000)tag=3;
    else if(handHudMode && bottomRight && units>8000 && units<10000)tag=4;
    // Radar: everything tilted in the bottom-left corner. The band run (10:03
    // PM) recognised the radar by tilt 0.95514-0.95517 for its first seconds
    // only (2,912 draws, then none), so its tilt changes after the HUD
    // appears; the corner's other tilt seen was 0.95630, the desktop census's
    // value there (0.9563) for the self-animating group. No other element
    // lives in that corner. Not parked: recognised where it is.
    else if(handHudMode && p[8]>0.3f && p[9]>0.3f)tag=5;
    // November 15: the aug cross (right middle, region 3) and the Mark & Track
    // counter (left middle, region 5). Mark's hackdiag log (November 13) shows
    // both regions drawing only tilted draws at home (right cos 0.93969, left
    // 0.90257; units -29..128 and 4..8), none parked. Parked (12,000 movie
    // units times an unmeasured parent scale) they are the only draws there
    // beyond 1,000 units, so any such draw is theirs.
    // Revision 7 (Mark's revision 6 run, 5:36 PM log): the parked aug cross was
    // drawn in the CENTRE region (p8 = p9 = 0) at tilt cos 0.99587, units ~2,000,
    // layout x 23,034-24,501, y 8,654-9,237 (eight samples, t=13.3 s), then not
    // at all: from the next 10 s on no parked draw remained anywhere (the game
    // stops drawing it that far out; health and ammo, parked, keep being
    // drawn). During play the centre region drew no other tilted draws. So the
    // aug cross is no longer parked: like the radar it is recognised where it
    // is, by that region and tilt, and carried to the wrist.
    // Revision 8 (Mark's revision 7 run, 6:15 PM log: every unrecognised tilted
    // draw grouped by region and tilt, Mark & Track parked and at home in turn):
    // during play the RIGHT-MIDDLE region (region 3) draws only the aug cross,
    // at home, tilt cos 0.9389-0.9404 (it sways a little), layout x
    // 21,431-24,083, y 6,761-8,775, ~20,000 draws per 10 s; the LEFT-MIDDLE
    // region (5) only Mark & Track, cos 0.9014-0.9038, x 129-2,941 at home and
    // ~240,000 when parked (parking moves its X row but barely its depth row,
    // which is why the parked test above never saw it), y 9,346-10,436. (The
    // revision 7 centre-region test, cos 0.99587, came from the parked aug's
    // first draws and never matched it at home.) So, like the radar, both are
    // recognised where they are, by region alone, and neither is parked.
    else if(handHudMode && p[8]<-0.3f && std::abs(p[9])<=0.3f)tag=6;
    else if(handHudMode && p[8]>0.3f && std::abs(p[9])<=0.3f)tag=7;
    if(!tag)return 0;
    // The parked value is positive in both regions, so the tilt has the sign
    // of the parking distance.
    sine=(park[tag-1]>0?magnitude:-magnitude)*(units<0?-1.f:1.f);
    return tag;
}
inline bool Identity(const float* m) {
    for(int i=0;i<16;++i)if(m[i]!=((i%5)?0.f:1.f))return false;
    return true;
}
// One ratio per screen region of the 3x3 HUD grid, with the tilt cosine it was
// seen with. Touched only by the thread that submits draws.
// value: X row = value * depth row, per region (desktop measurement). offset:
// the region's constant in X translation = value * depth translation + offset.
struct Ratio { float value; float cosine; bool known; float offset; bool offsetKnown; };
inline Ratio ratios[9]{};
inline int Region(const float* p) {
    const int x=p[8]>0.3f?2:(p[8]<-0.3f?0:1);
    const int y=p[9]>0.3f?2:(p[9]<-0.3f?0:1);
    return y*3+x;
}
struct Fix { unsigned char* state; float saved[16]; float d[3]; float ratio; int tag; bool applied; bool backup{}; };
inline std::atomic<unsigned> recognisedTags[slotCount]{},placedTags[slotCount]{},hiddenTags[slotCount]{};
// Why a recognised piece was hidden instead of placed (EngineCamera).
inline std::atomic<unsigned> pieceNoGrip[slotCount]{},pieceNoLayout[slotCount]{},pieceNoFrame[slotCount]{},pieceNoRenderer[slotCount]{};
inline std::atomic<unsigned> noScale{}, scaleMismatch{}, unownedParked{};
inline std::atomic<unsigned> radarSamples{}, unownedSamples{};
// Smooth band (HandBand.h): draws given the bending vertex shader, draws whose
// game shader was not one of the three known HUD shaders, and failures.
inline std::atomic<unsigned> bandApplied{}, bandUnknownShader{}, bandFailed{};
// Owned draws given a band but never swapped (the engine bound no shader for
// them through the hooked render-state call), and HUD rebuilds seen by the
// slots; HandBand drops its remembered shader identities on each rebuild.
inline std::atomic<unsigned> bandMissed{};
// Wrist curvature: where each owned draw's origin came from, and how pieces were laid.
inline std::atomic<unsigned> rowsFromParams{}, rowsFromBuffer{}, rowsNone{}, curvedPieces{}, flatPieces{};
// Position signal of a tilted Scaleform draw, in movie units; 0 if untilted.
inline float Units(const float* c) {
    if(!(c[2]>0.5f && c[2]<0.9999f))return 0;
    return c[3]/std::sqrt(1.f-c[2]*c[2])/twips;
}
// Hack diagnostics (Mark, November 13: "after hacking the hud disappears").
// The failsafe log showed every HUD draw unrecognised after the hack (542,362
// parked draws in a minute, none placed, radar none) but its samples were spent
// in the first minute. Per minute: a census of unrecognised draws by screen
// region (3x3 by the projection's off-centre terms), tilted or not, parked
// (|units| > 1000) or not, with parked units split into ranges; and up to 8
// samples of unrecognised parked draws and 4 of unrecognised tilted unparked
// draws, each with its projection, tilt block and own X/Y rows. Written by
// PersistStatus. Render thread only.
inline std::atomic<unsigned> censusParked[9]{}, censusTilted[9]{}, censusFlat[9]{};
inline std::atomic<unsigned> parkedRange[6]{};  // <0, 1000-5000, 5000-7000, 7000-8000, 8000-10000, >10000
inline unsigned parkedSampleBudget{8}, tiltedSampleBudget{4}, backupSampleBudget{0};
inline void DiagnoseUnowned(unsigned char* primitiveState,const float* projection,const float* data) {
    const int region=Region(projection);
    const float units=Units(data);
    const bool tilted=data[2]>0.5f && data[2]<0.9999f;
    const bool parked=std::abs(units)>1000;
    if(parked) {
        ++censusParked[region];
        const int range=units<0?0:units<5000?1:units<7000?2:units<8000?3:units<10000?4:5;
        ++parkedRange[range];
    } else if(tilted)++censusTilted[region];
    else ++censusFlat[region];
    unsigned& budget=parked?parkedSampleBudget:tiltedSampleBudget;
    if(!(parked || tilted) || !budget)return;
    --budget;
    float row[8]{};
    if(const auto params=*reinterpret_cast<float**>(primitiveState+0x1c))memcpy(row,params,sizeof(row));
    Note("%s region=%d proj p0=%.4f p5=%.4f p8=%.4f p9=%.4f p10=%.4f p11=%.4f p12=%.4f p13=%.4f p14=%.4f p15=%.4f | tilt c0=%.5f c1=%.5f cos=%.5f c3=%.2f units=%.1f c4-11=%g,%g,%g,%g,%g,%g,%g,%g | x=%.5f,%.5f,%.5f,%.2f y=%.5f,%.5f,%.5f,%.2f",
        parked?"unowned-parked-sample":"unowned-tilted-sample",region,
        projection[0],projection[5],projection[8],projection[9],projection[10],projection[11],projection[12],projection[13],projection[14],projection[15],
        data[0],data[1],data[2],data[3],units,data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],
        row[0],row[1],row[2],row[3],row[4],row[5],row[6],row[7]);
}
// ---- Backup recognition (hack candidate) --------------------------------
// After a hack every HUD draw failed Classify (failsafe run, November 13), whose
// test depends on the screen corner each piece is drawn in (the projection's
// off-centre terms). This backup does not use the corner at all:
//  * parked or not, and which pair: the parking distance in the draw's own
//    depth row (Units), 5000-7000 for health/ammo, 8000-10000 for
//    energy/consumable, the same bands Classify uses;
//  * which wrist: the piece's own layout height (Y translation of its X/Y
//    rows, primitiveState+0x1c), compared with the average height of the
//    health/energy pieces and of the ammo/consumable pieces that Classify has
//    recognised (health/energy sit at the top of the layout, ammo/consumable at
//    the bottom: band-middle Y 590-2335 against 11835-13643 in the wristfix
//    run). Without both averages it does nothing;
//  * the X shift home: from this draw's own relation of X row to depth row
//    (x[0]/c[0], the relation Classify's region learns, exact per draw), or
//    the last one seen for that piece when c[0] is too small to divide by.
// That the X/Y rows keep their layout heights in the post-hack drawing mode is
// an inference (they come from the piece's own parameters, not the projection);
// not measured. Render thread only.
struct GroupHeight { double sum{}; unsigned count{}; float Mean() const { return count?float(sum/count):0.f; } };
inline GroupHeight groupHeights[2];          // 0 health/energy, 1 ammo/consumable
inline float backupRatio[4]{};               // last per-draw ratio per piece
inline std::atomic<unsigned> backupTags[4]{}, backupNoGroups{}, backupNoRatio{}, backupBadRows{};
inline void RecordHeight(int tag,unsigned char* primitiveState) {
    if(tag<1 || tag>4)return;
    auto& g=groupHeights[tag<=2?0:1];
    if(g.count>=100000)return;
    if(const auto params=*reinterpret_cast<float**>(primitiveState+0x1c))
        if(std::isfinite(params[7])){g.sum+=params[7];++g.count;}
}
inline int BackupClassify(unsigned char* primitiveState,const float* c,float& sine,float& ratio) {
    for(int i=0;i<12;++i)if(!std::isfinite(c[i]))return 0;
    // c[8..11] are not part of the test: after a hack they hold
    // -8.34, 0, 95.34, 95.7 on every HUD draw, in the normal corners and
    // tilts (hackdiag run, November 13, 155.8 s), and requiring zero there
    // rejected the whole HUD. What the game uses them for is not known; the
    // wrist band shader does not read them (b3, b5, b11 only).
    if(c[4]!=0 || c[5]!=0 || c[6]!=0 || c[7]!=1)return 0;
    if(!(c[2]>0.5f && c[2]<0.9999f))return 0;
    const float magnitude=std::sqrt(1.f-c[2]*c[2]);
    const float units=c[3]/magnitude/twips;
    const bool nearBand=units>5000 && units<7000, farBand=units>8000 && units<10000;
    if(!nearBand && !farBand)return 0;
    if(groupHeights[0].count<100 || groupHeights[1].count<100){++backupNoGroups;return 0;}
    const auto params=*reinterpret_cast<float**>(primitiveState+0x1c);
    if(!params){++backupBadRows;return 0;}
    float rows[8];memcpy(rows,params,32);
    for(int i=0;i<8;++i)if(!std::isfinite(rows[i])){++backupBadRows;return 0;}
    const float top=groupHeights[0].Mean(),bottom=groupHeights[1].Mean();
    const bool right=std::abs(rows[7]-bottom)<std::abs(rows[7]-top);
    const int tag=right?(nearBand?3:4):(nearBand?1:2);
    float r=std::abs(c[0])>1e-4f?rows[0]/c[0]:0.f;
    if(std::isfinite(r) && std::abs(r)>0.01f && std::abs(r)<1000.f)backupRatio[tag-1]=r;
    else r=backupRatio[tag-1];
    if(!(std::abs(r)>0)){++backupNoRatio;return 0;}
    ratio=r;
    sine=(park[tag-1]>0?magnitude:-magnitude)*(units<0?-1.f:1.f);
    ++backupTags[tag-1];
    return tag;
}
// Classify the draw and, if it is a parked element, set World for it. The
// caller rebuilds the matrices: the desktop check through BeginFix below,
// EngineCamera through its own HUD path in VR.
inline void PrepareFix(void* self,Fix& fix) {
    __try {
        const auto primitive=static_cast<unsigned char*>(self);
        const auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
        if(!primitiveState)return;
        const auto data=*reinterpret_cast<float**>(primitiveState+0x20);
        if(!data)return; // not a Scaleform primitive
        const auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
        const auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        const auto overrideMatrix=*reinterpret_cast<float**>(state+0x540);
        const float* projection=overrideMatrix?overrideMatrix:reinterpret_cast<float*>(state+0x440);
        float sine{};
        fix.tag=Classify(projection,data,sine);
        if(!fix.tag) {
            // Hand mode diagnostics: a draw this far off screen can only be a
            // parked element. If it was not recognised, say where it is.
            if(handHudMode && verbose)DiagnoseUnowned(primitiveState,projection,data);
            float backupRatioValue{};
            if(handHudMode)fix.tag=BackupClassify(primitiveState,data,sine,backupRatioValue);
            if(!fix.tag) {
                if(handHudMode && std::abs(Units(data))>1000) {
                    ++unownedParked;
                    if(unownedSamples++<12)Detail("unowned-parked p8=%.4f p9=%.4f cos=%.5f units=%.1f c0=%.5f c1=%.5f c4-11=%g,%g,%g,%g,%g,%g,%g,%g",
                        projection[8],projection[9],data[2],Units(data),data[0],data[1],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11]);
                }
                return;
            }
            // Recognised by the backup: carried home by this draw's own ratio.
            const auto backupWorld=reinterpret_cast<float*>(state+0x500);
            if(!Identity(backupWorld)){++nonIdentityWorld;fix.tag=0;return;}
            const float move=sine*(-float(park[fix.tag-1]))*twips;
            fix.backup=true;fix.ratio=backupRatioValue;
            fix.d[0]=backupRatioValue*move;fix.d[1]=0;fix.d[2]=move;
            memcpy(fix.saved,backupWorld,64);
            backupWorld[12]=fix.d[0];backupWorld[13]=fix.d[1];backupWorld[14]=fix.d[2];
            state[0x544]=1;fix.state=state;fix.applied=true;
            if(backupSampleBudget) {
                --backupSampleBudget;
                const auto params=*reinterpret_cast<float**>(primitiveState+0x1c);
                Note("backup-sample tag=%d region=%d p8=%.4f p9=%.4f cos=%.5f units=%.1f c0=%.5f c3=%.2f ratio=%.5f shift=%.2f y3=%.2f heights top=%.1f bottom=%.1f",
                    fix.tag,Region(projection),projection[8],projection[9],data[2],Units(data),data[0],data[3],
                    fix.ratio,fix.d[0],params?params[7]:0.f,groupHeights[0].Mean(),groupHeights[1].Mean());
            }
            ++fixedDraws;
            return;
        }
        ++recognisedTags[fix.tag-1];
        if(handHudMode)RecordHeight(fix.tag,primitiveState);
        // Parking moved this element's depth row by its chain scale times the
        // health-measured amount. Radar: use the measured chain ratio, and only
        // when the draw's own signal agrees with it (at-home draws are < 200).
        float scale=1.f;
        if(fix.tag==5 || fix.tag==6 || fix.tag==7) {
            // Not parked: drawn where it is, and only carried to the wrist.
            const auto radarWorld=reinterpret_cast<float*>(state+0x500);
            if(!Identity(radarWorld)){++nonIdentityWorld;return;}
            memcpy(fix.saved,radarWorld,64);
            fix.d[0]=fix.d[1]=fix.d[2]=0;fix.ratio=0;
            fix.state=state;fix.applied=true;++fixedDraws;
            if(radarSamples++<12)Detail("radar-draw p8=%.4f p9=%.4f cos=%.5f c0=%.5f c3=%.2f",projection[8],projection[9],data[2],data[0],data[3]);
            return;
        }
        const auto world=reinterpret_cast<float*>(state+0x500);
        if(!Identity(world)){++nonIdentityWorld;return;}
        const Ratio& known=ratios[Region(projection)];
        if(!known.known || std::abs(known.cosine-data[2])>0.001f){++noRatio;return;}
        const int i=fix.tag-1;
        const bool shifted=mode.load()==int(Mode::Shifted);
        // What a native move of this many movie units does to the depth row,
        // and through the region's ratio to the X row.
        const float depthMove=scale*sine*(float(shifted?shiftX[i]:0)-float(park[i]))*twips;
        fix.ratio=known.value;
        fix.d[0]=known.value*depthMove;
        // The Y row's scale is not measured; the preview offset in Y is raw.
        fix.d[1]=float(shifted?shiftY[i]:0)*twips;
        fix.d[2]=depthMove;
        memcpy(fix.saved,world,64);
        world[12]=fix.d[0];world[13]=fix.d[1];world[14]=fix.d[2];
        state[0x544]=1;fix.state=state;fix.applied=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++fixFaults;fix.applied=false; }
    if(fix.applied)++fixedDraws;
}
inline void RestoreWorld(Fix& fix) {
    if(!fix.applied)return;
    memcpy(fix.state+0x500,fix.saved,64);fix.state[0x544]=1;
}
inline void BeginFix(void* self,Fix& fix) {
    PrepareFix(self,fix);
    if(fix.applied)matrices(fix.state);
}
inline void EndFix(Fix& fix) {
    if(!fix.applied)return;
    RestoreWorld(fix);
    matrices(fix.state);
}
// The engine's CPU copy of one of its constant buffers (same layout that
// EngineShaderTrace reads): object at state+0x5a8+slot*4, data pointer at +8,
// row count at +0xC. Returns the row count, or 0 when it cannot be read.
inline uint32_t Shadow(unsigned char* state,unsigned slot,float* out,unsigned rows) {
    const auto buffer=*reinterpret_cast<uint32_t**>(state+0x5a8+slot*4);
    if(!buffer)return 0;
    const auto data=reinterpret_cast<const float*>(uintptr_t(buffer[2]));
    const uint32_t available=buffer[3];
    if(!data || available<rows)return 0;
    memcpy(out,data,rows*16);
    return available;
}
// After the engine has submitted a tilted HUD draw, its X row is in buffer 4.
inline void Learn(void* self) {
    __try {
        const auto primitive=static_cast<unsigned char*>(self);
        const auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
        if(!primitiveState)return;
        const auto data=*reinterpret_cast<float**>(primitiveState+0x20);
        if(!data)return;
        if(!(data[2]>0.5f && data[2]<0.9999f) || !(std::abs(data[0])>1e-3f))return;
        const auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
        const auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        const auto overrideMatrix=*reinterpret_cast<float**>(state+0x540);
        const float* projection=overrideMatrix?overrideMatrix:reinterpret_cast<float*>(state+0x440);
        float row[4];
        if(!Shadow(state,4,row,1))return;
        const float value=row[0]/data[0];
        if(!std::isfinite(value) || !(std::abs(value)>0.1f && std::abs(value)<100.f))return;
        Ratio& r=ratios[Region(projection)];
        r.value=value;r.cosine=data[2];r.known=true;
        const float offset=row[3]-value*data[3];
        if(std::isfinite(offset)){r.offset=offset;r.offsetKnown=true;}
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++learnFaults; }
}
// Hand HUD mode: a bounded running account of what was done, for the log.
inline std::atomic<unsigned> handPanels[2]{}, handFallbacks[2]{};
inline uint64_t handStatusAt{};
inline unsigned handStatusLines{};
// Once a minute for the whole session: has the game put anything back, and
// were the unnamed plates found (on Note's own budget).
inline uint64_t persistStatusAt{};
inline void PersistStatus() {
#if !defined(HUD_ISOLATION_TEST)
    const uint64_t now=GetTickCount64();
    if(persistStatusAt && now-persistStatusAt<10000)return;
    persistStatusAt=now;
    Note("persist-status reparks health=%u energy=%u ammo=%u consumable=%u radar=%u | lost %u/%u/%u/%u/%u | plates weapon=%s grenade=%s lookups=%u failures=%u",
        slots[0].reparks,slots[1].reparks,slots[2].reparks,slots[3].reparks,slots[4].reparks,
        slots[0].lost,slots[1].lost,slots[2].lost,slots[3].lost,slots[4].lost,
        unnamedPlates[0].found[0]?unnamedPlates[0].found:"(searching)",
        unnamedPlates[1].found[0]?unnamedPlates[1].found:"(searching)",
        unnamedLookups.load(),plateFailures.load());
    // November 15 (revision 6 diagnostics, Mark: "new HUD elements completely
    // gone"): the aug cross (slot/tag 6) and Mark & Track (7), step by step:
    // the clip found and parked (status, game x, target, reparks, lost), its
    // draws recognised, then placed on the wrist or hidden and why.
    // (Revision 7's Mark & Track parking on/off and its tilted-draw signature
    // lines found both pieces; removed in revision 8.)
    for(int i=5;i<7;++i)
        Note("new-piece %s | slot: %s original=%.1f target=%.1f reparks=%u lost=%u | draws recognised=%u placed=%u hidden=%u (no grip=%u, no layout position=%u, wrist frame refused=%u, band not ready=%u)",
            i==5?"aug-cross":"mark-track",status[i],slots[i].original,slots[i].target,slots[i].reparks,slots[i].lost,
            recognisedTags[i].load(),placedTags[i].load(),hiddenTags[i].load(),
            pieceNoGrip[i].load(),pieceNoLayout[i].load(),pieceNoFrame[i].load(),pieceNoRenderer[i].load());
    // Drawing, since the last line: owned draws put on a wrist / hidden (no
    // grip pose or layout), band shader swaps done / missed / refused for an
    // unknown game shader / failed, and parked draws not recognised.
    static unsigned last[12]{};
    const unsigned now12[12]={
        placedTags[0].load()+placedTags[1].load()+placedTags[2].load()+placedTags[3].load()+placedTags[4].load(),
        hiddenTags[0].load()+hiddenTags[1].load()+hiddenTags[2].load()+hiddenTags[3].load()+hiddenTags[4].load(),
        bandApplied.load(),bandMissed.load(),bandUnknownShader.load(),bandFailed.load(),
        unownedParked.load(),noRatio.load(),nonIdentityWorld.load(),fixFaults.load(),
        recognisedTags[4].load(),placedTags[4].load()};
    unsigned d[12];for(int i=0;i<12;++i){d[i]=now12[i]-last[i];last[i]=now12[i];}
    {
        char text[400];int n=0;
        for(int r=0;r<9;++r)n+=snprintf(text+n,sizeof(text)-n," r%d=%u/%u/%u",r,censusParked[r].exchange(0),censusTilted[r].exchange(0),censusFlat[r].exchange(0));
        Note("unrecognised-census last-10s (parked/tilted/flat; r=3*row+col by projection p9/p8: <-0.3, middle, >0.3; health/energy are r2, ammo/consumable r6, radar r8)%s | parked units <0=%u 1-5k=%u 5-7k=%u 7-8k=%u 8-10k=%u >10k=%u",
            text,parkedRange[0].exchange(0),parkedRange[1].exchange(0),parkedRange[2].exchange(0),parkedRange[3].exchange(0),parkedRange[4].exchange(0),parkedRange[5].exchange(0));
    }
    Note("backup-recognition totals health=%u energy=%u ammo=%u consumable=%u | no-heights=%u no-ratio=%u bad-rows=%u | heights top=%.1f(%u) bottom=%.1f(%u)",
        backupTags[0].load(),backupTags[1].load(),backupTags[2].load(),backupTags[3].load(),
        backupNoGroups.load(),backupNoRatio.load(),backupBadRows.load(),
        groupHeights[0].Mean(),groupHeights[0].count,groupHeights[1].Mean(),groupHeights[1].count);
    Note("draw-status last-10s placed=%u hidden=%u | band swapped=%u missed=%u unknown-shader=%u failed=%u | parked-unrecognised=%u noRatio=%u nonIdentityWorld=%u faults=%u | radar recognised=%u placed=%u | rebuilds=%u",
        d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8],d[9],d[10],d[11],hudRebuilds.load());
#endif
}
// Once a second: notice at once when recognition is lost or comes back, or
// the backup starts working, and write samples right then, so a short session
// (into the game, one hack, out) is enough (Mark, November 13: "The difference
// between 10 seconds and 2 minutes is huge"). Each event line carries the last
// second's counts. Samples: 8 parked + 4 tilted unrecognised draws on loss, 6
// backup-recognised draws when the backup starts; at most once every 10 s.
struct Watch { uint64_t at{}, sampledAt{}; unsigned placed{}, unowned{}, backup{}; bool lost{}, backupOn{}; };
inline Watch watch;
inline unsigned SumPlaced() { unsigned t=0;for(auto& v:placedTags)t+=v.load();return t; }
inline unsigned SumBackup() { unsigned t=0;for(auto& v:backupTags)t+=v.load();return t; }
// Pure decision for one second's counts; returns 1 lost, 2 back, 3 backup on, 4 backup off, 0 none.
inline int WatchStep(Watch& w,unsigned dPlaced,unsigned dUnowned,unsigned dBackup) {
    if(!w.lost && dPlaced==0 && dUnowned>=100){w.lost=true;return 1;}
    if(w.lost && dPlaced>0 && dBackup==0){w.lost=false;return 2;}
    if(!w.backupOn && dBackup>0){w.backupOn=true;w.lost=false;return 3;}
    if(w.backupOn && dBackup==0){w.backupOn=false;return 4;}
    return 0;
}
inline void WatchRecognition(uint64_t now) {
    if(watch.at && now-watch.at<1000)return;
    const unsigned placed=SumPlaced(),unowned=unownedParked.load(),backup=SumBackup();
    if(!watch.at){watch.at=now;watch.placed=placed;watch.unowned=unowned;watch.backup=backup;return;}
    const unsigned dPlaced=placed-watch.placed,dUnowned=unowned-watch.unowned,dBackup=backup-watch.backup;
    watch.at=now;watch.placed=placed;watch.unowned=unowned;watch.backup=backup;
    const int event=WatchStep(watch,dPlaced,dUnowned,dBackup);
    if(!event)return;
    static const char* const names[]={"","recognition-LOST","recognition-back","backup-ACTIVE","backup-idle"};
    Note("%s last-second placed=%u unrecognised-parked=%u backup=%u",names[event],dPlaced,dUnowned,dBackup);
    if((event==1 || event==3) && (!watch.sampledAt || now-watch.sampledAt>=10000)) {
        watch.sampledAt=now;
        if(event==1){parkedSampleBudget=8;tiltedSampleBudget=4;}
        else backupSampleBudget=6;
    }
}
inline void HandStatus() {
#if !defined(HUD_ISOLATION_TEST)
    if(!verbose)return;
    WatchRecognition(GetTickCount64());
    PersistStatus();
    if(handStatusLines>=24)return;
    const uint64_t now=GetTickCount64();
    if(now-handStatusAt<5000)return;
    handStatusAt=now;++handStatusLines;
    Detail("hand-status fixed=%u noRatio=%u nonIdentityWorld=%u fixFaults=%u learnFaults=%u panels left=%u right=%u fallbacks left=%u right=%u | health: %s | energy: %s | ammo: %s | consumable: %s | radar: %s",
        fixedDraws.load(),noRatio.load(),nonIdentityWorld.load(),fixFaults.load(),learnFaults.load(),
        handPanels[0].load(),handPanels[1].load(),handFallbacks[0].load(),handFallbacks[1].load(),
        status[0],status[1],status[2],status[3],status[4]);
    Detail("radar-status scale=%.4f noScale=%u scaleMismatch=%u unownedParked=%u",radarScale.load(),noScale.load(),scaleMismatch.load(),unownedParked.load());
    Detail("wrist-status rows params=%u buffer=%u none=%u pieces curved=%u flat=%u band applied=%u unknownShader=%u failed=%u",rowsFromParams.load(),rowsFromBuffer.load(),rowsNone.load(),curvedPieces.load(),flatPieces.load(),bandApplied.load(),bandUnknownShader.load(),bandFailed.load());
    Detail("hand-tags health=%u/%u/%u energy=%u/%u/%u ammo=%u/%u/%u consumable=%u/%u/%u radar=%u/%u/%u (recognised/placed/hidden)",
        recognisedTags[0].load(),placedTags[0].load(),hiddenTags[0].load(),
        recognisedTags[1].load(),placedTags[1].load(),hiddenTags[1].load(),
        recognisedTags[2].load(),placedTags[2].load(),hiddenTags[2].load(),
        recognisedTags[3].load(),placedTags[3].load(),hiddenTags[3].load(),
        recognisedTags[4].load(),placedTags[4].load(),hiddenTags[4].load());
    Detail("backplate-status writes=%u failures=%u damageShows=%u",plateWrites.load(),plateFailures.load(),damageShows.load());
#endif
}
// Before a draw: where its own origin is (X, Y and depth translation), for the
// wrist's curvature. Two places are tried, the primitive's parameter block
// (primitiveState+0x1c) and the engine's buffer 4. A candidate is accepted only
// if its X row is what the region's measured relation predicts from this
// draw's own depth row, so a row left over from another draw is not used.
inline bool RowsMatch(const float* x,const float* depth,const Ratio& r) {
    for(int i=0;i<8;++i)if(!std::isfinite(x[i]))return false;
    const float tolerance=1e-3f*(std::abs(x[0])+std::abs(x[1])+1.f);
    return std::abs(x[0]-r.value*depth[0])<=tolerance && std::abs(x[1]-r.value*depth[1])<=tolerance
        && std::abs(x[3]-(r.value*depth[3]+r.offset))<=2.f;
}
// `trusted`: the radar, which is not parked and has no learned relation; its
// own parameter block is taken as is (that block matched for ~99% of the other
// elements' draws in the wrist run).
inline int DrawOrigin(void* self,float* origin,bool trusted=false) {
    __try {
        const auto primitive=static_cast<unsigned char*>(self);
        const auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
        if(!primitiveState)return 0;
        const auto data=*reinterpret_cast<float**>(primitiveState+0x20);
        if(!data)return 0;
        const auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
        const auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        const auto overrideMatrix=*reinterpret_cast<float**>(state+0x540);
        const float* projection=overrideMatrix?overrideMatrix:reinterpret_cast<float*>(state+0x440);
        float rows[8];
        if(trusted) {
            const auto params=*reinterpret_cast<float**>(primitiveState+0x1c);
            if(!params){++rowsNone;return 0;}
            memcpy(rows,params,32);
            for(int i=0;i<8;++i)if(!std::isfinite(rows[i])){++rowsNone;return 0;}
            ++rowsFromParams;
            origin[0]=rows[3];origin[1]=rows[7];origin[2]=data[3];
            return 1;
        }
        const Ratio& r=ratios[Region(projection)];
        if(!r.known || !r.offsetKnown || std::abs(r.cosine-data[2])>0.001f){++rowsNone;return 0;}
        int source=0;
        if(const auto params=*reinterpret_cast<float**>(primitiveState+0x1c)) {
            memcpy(rows,params,32);
            if(RowsMatch(rows,data,r))source=1;
        }
        if(!source && Shadow(state,4,rows,2) && RowsMatch(rows,data,r))source=2;
        if(!source){++rowsNone;return 0;}
        ++(source==1?rowsFromParams:rowsFromBuffer);
        origin[0]=rows[3];origin[1]=rows[7];origin[2]=data[3];
        return source;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++fixFaults; }
    return 0;
}
// Each band's middle in the HUD's clip rectangle, from its own draws' origins:
// 0 left (health, energy), 1 ammo, 2 radar, 3 consumable (ammo and consumable
// measured apart since November 15, when they were laid out apart on the
// right wrist's end). The band run
// showed the VR HUD laid out differently from the desktop the old fixed
// middles came from (radar corner offset 0.583 in VR, 0.667 on the desktop),
// which turned the left band too far round the wrist. Measured over rolling
// windows of 4,000 pieces; the last full window is used, so a slide-in when
// the HUD appears does not stick. Render thread only.
struct Bounds { float minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f; unsigned samples=0; };
// narrowest: for the ammo and the consumable, the narrowest full window since
// the HUD was last (re)built (rebuild = hudRebuilds when it was taken).
struct BandMiddle { Bounds current, published, narrowest; unsigned logged=0, rebuild=0; };
inline BandMiddle bandMiddles[6];   // 4 aug cross, 5 Mark & Track (November 15)
// Mark, revision 5 (November 15): reloading still moved the ammo on the wrist
// end. Pausing the measurement on weaponInfo_mc's reload frames (revision 4)
// did not stop it, and the reload part found in the game file (depth 26) would
// move it only about 0.4 units against about 3 in his screenshots, so what
// else a reload adds is not known. Whatever it is, a part that appears only
// adds draw origins, so it can only WIDEN a window. For the ammo and the
// consumable the middle is therefore taken from the narrowest full window
// since the HUD was last built: a reload (or anything else that adds draws for
// a while) can no longer move them. A HUD rebuild measures afresh.
// Revision 9: also the aug cross (band 4): its "not enough energy" flash and
// any animation could widen a window the way reload did.
inline bool KeepsNarrowest(int band){return band==1 || band==3 || band==4;}
inline float Spread(const Bounds& b){return (b.maxX-b.minX)+(b.maxY-b.minY);}
inline unsigned bandWindowNotes{};
inline void BandSample(int band,float x,float y) {
    if(band<0 || band>5 || !std::isfinite(x) || !std::isfinite(y))return;
    auto& m=bandMiddles[band];auto& b=m.current;
    b.minX=x<b.minX?x:b.minX;b.maxX=x>b.maxX?x:b.maxX;b.minY=y<b.minY?y:b.minY;b.maxY=y>b.maxY?y:b.maxY;
    if(++b.samples<4000)return;
    m.published=b;b=Bounds{};
    bool kept=false;
    if(KeepsNarrowest(band)) {
        const unsigned rebuilds=hudRebuilds.load();
        if(m.rebuild!=rebuilds || !m.narrowest.samples){m.rebuild=rebuilds;m.narrowest=m.published;kept=true;}
        else if(Spread(m.published)<Spread(m.narrowest)-.5f){m.narrowest=m.published;kept=true;}
    }
    if(m.logged++<6)Detail("band-middle band=%d x=%.4f..%.4f y=%.4f..%.4f",band,m.published.minX,m.published.maxX,m.published.minY,m.published.maxY);
    // Diagnostics (logging switched on): every window of the ammo, the
    // consumable, the aug cross and Mark & Track, to see what a reload does.
    if(band!=0 && band!=2 && bandWindowNotes<800) {
        ++bandWindowNotes;
        Note("band-window band=%d x=%.1f..%.1f y=%.1f..%.1f spread=%.1f%s",band,
            m.published.minX,m.published.maxX,m.published.minY,m.published.maxY,Spread(m.published),
            KeepsNarrowest(band)?(kept?" kept (narrowest)":" not kept"):"");
    }
}
// The band's middle: for the ammo and the consumable the narrowest full window
// since the last HUD build; otherwise the last full window, else the window
// so far; else the given fallback.
inline void BandMiddleOf(int band,float& x,float& y) {
    if(band<0 || band>5)return;
    const auto& m=bandMiddles[band];
    const bool narrow=KeepsNarrowest(band) && m.narrowest.samples && m.rebuild==hudRebuilds.load();
    const Bounds& b=narrow?m.narrowest:m.published.samples?m.published:m.current;
    if(!b.samples)return;
    x=.5f*(b.minX+b.maxX);y=.5f*(b.minY+b.maxY);
}
// The band's largest origin (bottom right of its range), from the last full
// window, else the window so far. Returns false when nothing is measured yet.
inline bool BandMaxOf(int band,float& x,float& y) {
    if(band<0 || band>5)return false;
    const auto& m=bandMiddles[band];
    const Bounds& b=m.published.samples?m.published:m.current;
    if(!b.samples)return false;
    x=b.maxX;y=b.maxY;return true;
}
// The band's smallest origin (top left of its range), from the same window.
// Returns false, leaving x and y, when nothing has been measured yet.
inline bool BandMinOf(int band,float& x,float& y) {
    if(band<0 || band>5)return false;
    const auto& m=bandMiddles[band];
    const Bounds& b=m.published.samples?m.published:m.current;
    if(!b.samples)return false;
    x=b.minX;y=b.minY;return true;
}
// Every UI draw nothing owns whose own origin lands in the bottom-left of
// the HUD's clip rectangle (where the radar sat in Mark's 9:36 PM screenshots,
// about x -0.92..-0.81, y -0.56..-0.69): what it is, for recognising the
// radar next time. Bounded. Render thread only.
inline unsigned radarAreaLines{};
inline void RadarAreaSample(void* self) {
    if(radarAreaLines>=48)return;
    __try {
        const auto primitive=static_cast<unsigned char*>(self);
        const auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
        if(!primitiveState)return;
        const auto data=*reinterpret_cast<float**>(primitiveState+0x20);
        const auto params=*reinterpret_cast<float**>(primitiveState+0x1c);
        if(!data || !params)return;
        const auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
        const auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        const auto overrideMatrix=*reinterpret_cast<float**>(state+0x540);
        const float* p=overrideMatrix?overrideMatrix:reinterpret_cast<float*>(state+0x440);
        const float* world=reinterpret_cast<float*>(state+0x500);
        const float* view=reinterpret_cast<float*>(state+0x480);
        const float origin[4]={params[3],params[7],data[3],1};
        float a[4],b[4],c[4];
        for(int j=0;j<4;++j)a[j]=origin[0]*world[j]+origin[1]*world[4+j]+origin[2]*world[8+j]+origin[3]*world[12+j];
        for(int j=0;j<4;++j)b[j]=a[0]*view[j]+a[1]*view[4+j]+a[2]*view[8+j]+a[3]*view[12+j];
        for(int j=0;j<4;++j)c[j]=b[0]*p[j]+b[1]*p[4+j]+b[2]*p[8+j]+b[3]*p[12+j];
        if(!(c[3]>1e-6f))return;
        const float x=c[0]/c[3],y=c[1]/c[3];
        if(!std::isfinite(x) || !std::isfinite(y) || x>-0.6f || y>-0.35f)return;
        ++radarAreaLines;
        Detail("radar-area ndc=%.4f,%.4f p0=%.4f p8=%.4f p9=%.4f cos=%.5f c0=%.5f c1=%.5f c3=%.2f x0=%.4f x3=%.2f y1=%.4f y3=%.2f flags=%08x indices=%u material=%08x",
            x,y,p[0],p[8],p[9],data[2],data[0],data[1],data[3],params[0],params[3],params[5],params[7],
            *reinterpret_cast<uint32_t*>(primitiveState),*reinterpret_cast<uint32_t*>(primitive+0x28),
            unsigned(*reinterpret_cast<uintptr_t*>(primitiveState+0xc)));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
inline constexpr int captureCount=6; // two cycles of the three modes
inline constexpr unsigned drawCapacity=1536;
struct Draw {
    uint8_t present, eye, tag, applied;
    uint32_t flags, indices, rows[3];
    uintptr_t material;
    float projection[16], constants[12], d[4], wvp[16], xy[8], depth[4];
};
inline Draw draws[captureCount][drawCapacity];
inline std::atomic<unsigned> drawCount[captureCount]{};
inline std::atomic<bool> capturing{};
inline std::atomic<int> captureSlot{};
inline std::atomic<unsigned> capturePresent{};
// Called after the engine has submitted the draw and before World is put back,
// so the buffers hold what this draw used.
inline void Record(void* self,const Fix& fix) {
    __try {
        const auto primitive=static_cast<unsigned char*>(self);
        const auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
        if(!primitiveState)return;
        const auto data=*reinterpret_cast<float**>(primitiveState+0x20);
        if(!data)return;
        const int slot=captureSlot.load();
        const unsigned n=drawCount[slot].fetch_add(1);
        if(n>=drawCapacity){drawCount[slot]=drawCapacity;return;}
        const auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
        const auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        const auto overrideMatrix=*reinterpret_cast<float**>(state+0x540);
        Draw& d=draws[slot][n];
        memset(&d,0,sizeof(d));
        d.present=uint8_t(capturePresent.load());d.eye=state[0x5ea];
        d.tag=uint8_t(fix.tag);d.applied=fix.applied;
        d.flags=*reinterpret_cast<uint32_t*>(primitiveState);
        d.indices=*reinterpret_cast<uint32_t*>(primitive+0x28);
        d.material=*reinterpret_cast<uintptr_t*>(primitiveState+0xc);
        memcpy(d.projection,overrideMatrix?overrideMatrix:reinterpret_cast<float*>(state+0x440),64);
        memcpy(d.constants,data,48);
        if(fix.applied){memcpy(d.d,fix.d,sizeof(fix.d));d.d[3]=fix.ratio;}
        d.rows[0]=Shadow(state,0,d.wvp,4);
        d.rows[1]=Shadow(state,3,d.depth,1);
        d.rows[2]=Shadow(state,4,d.xy,2);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++recordFaults; }
}
using Primitive=void(__thiscall*)(void*,void*,bool,uint32_t);
inline Primitive originalPrimitive{};
inline void __fastcall PrimitiveHook(void* self,void*,void* stream,bool backBeforeFront,uint32_t flags) {
    Fix fix;fix.state=nullptr;fix.tag=0;fix.applied=false;fix.ratio=0;
    if(stream)BeginFix(self,fix);
    originalPrimitive(self,stream,backBeforeFront,flags);
    if(stream) {
        if(capturing.load(std::memory_order_relaxed))Record(self,fix);
        Learn(self);
    }
    EndFix(fix);
}
enum class Phase { WaitHud, Settle, Capture, Hold };
inline Phase phase{Phase::WaitHud};
inline int period{-1}; // counts five-second mode periods from 0
inline uint64_t hudSeen{}, phaseStart{}, settledAt{};
inline unsigned capturedPresents{};
inline int capturesWritten{};
inline bool periodApplied[captureCount][3]{};
inline void WriteCapture(int k) {
    ++capturesWritten;
#if !defined(HUD_ISOLATION_TEST)
    FILE* f{};
    if(fopen_s(&f,"DeusExHRVR-hud-recovery.log",k?"a":"w"))return;
    if(!k) {
        fprintf(f,"recovery captures=%d capacity=%u park=%d,%d,%d shiftX=%d,%d,%d shiftY=%d,%d,%d\n",
            captureCount,drawCapacity,park[0],park[1],park[2],shiftX[0],shiftX[1],shiftX[2],shiftY[0],shiftY[1],shiftY[2]);
        fputs("# d capture index present eye tag applied flags indices material rows0 rows3 rows4 | projection[16] | constants[12] | d[3] ratio | wvp[16] | xy[8] | depth[4]\n",f);
    }
    unsigned n=drawCount[k].load();if(n>drawCapacity)n=drawCapacity;
    fprintf(f,"capture %d mode=%d applied=%d,%d,%d draws=%u fixed=%u fixFaults=%u recordFaults=%u nonIdentityWorld=%u noRatio=%u learnFaults=%u\n",
        k,k%modeCount,int(periodApplied[k][0]),int(periodApplied[k][1]),int(periodApplied[k][2]),n,
        fixedDraws.load(),fixFaults.load(),recordFaults.load(),nonIdentityWorld.load(),noRatio.load(),learnFaults.load());
    for(unsigned i=0;i<n;++i) {
        const Draw& d=draws[k][i];
        fprintf(f,"d %d %u %u %u %u %u %08x %u %08x %u %u %u |",k,i,unsigned(d.present),unsigned(d.eye),
            unsigned(d.tag),unsigned(d.applied),d.flags,d.indices,unsigned(d.material),d.rows[0],d.rows[1],d.rows[2]);
        for(float v:d.projection)fprintf(f," %.9g",v);
        fputs(" |",f);for(float v:d.constants)fprintf(f," %.9g",v);
        fputs(" |",f);for(float v:d.d)fprintf(f," %.9g",v);
        fputs(" |",f);for(float v:d.wvp)fprintf(f," %.9g",v);
        fputs(" |",f);for(float v:d.xy)fprintf(f," %.9g",v);
        fputs(" |",f);for(float v:d.depth)fprintf(f," %.9g",v);
        fputc(10,f);
    }
    fclose(f);
#else
    (void)k;
#endif
}
inline void ShowMode() {
    if(capturesWritten>=captureCount)
        snprintf(censusText,sizeof(censusText),"%s | log written - quit when ready",modeText[period%modeCount]);
    else
        snprintf(censusText,sizeof(censusText),"%s | recorded %d of %d",modeText[period%modeCount],capturesWritten,captureCount);
    Title();
}
inline void BeginMode(int next,uint64_t now) {
    period=next;settledAt=0;phaseStart=now;phase=Phase::Settle;
    const int m=next%modeCount;
    mode=m;
    if(m==int(Mode::Normal))Request(0,0,0);else Request(park[0],park[1],park[2]);
    ShowMode();
}
// Called once per Present. `now` is in milliseconds.
inline void Sequence(uint64_t now) {
    switch(phase) {
    case Phase::WaitHud:
        if(!(resolved[0] && resolved[1])){hudSeen=0;return;}
        if(!hudSeen){hudSeen=now;return;}
        // Give the weapon indicator a chance to appear, but do not wait forever.
        if((resolved[2] && now-hudSeen>=2000) || now-hudSeen>=8000)BeginMode(0,now);
        return;
    case Phase::Settle: {
        const unsigned wanted=generation.load();
        bool all=true;
        for(int i=0;i<3;++i)if(resolved[i] && appliedGeneration[i].load()!=wanted)all=false;
        if(!all)settledAt=0;
        else if(!settledAt)settledAt=now;
        // Half a second after every element confirmed its position; or stop
        // waiting after four seconds and record what was actually applied.
        if((settledAt && now-settledAt>=500) || now-phaseStart>=4000) {
            if(period>=captureCount){phase=Phase::Hold;return;}
            for(int i=0;i<3;++i)periodApplied[period][i]=resolved[i] && appliedGeneration[i].load()==wanted;
            captureSlot=period;capturePresent=0;capturedPresents=0;
            drawCount[period]=0;capturing=true;phase=Phase::Capture;
        }
        return;
    }
    case Phase::Capture:
        // Two present intervals, so at least one holds a whole frame.
        if(++capturedPresents<2){capturePresent=capturedPresents;return;}
        capturing=false;
        WriteCapture(period);
        phase=Phase::Hold;ShowMode();
        return;
    case Phase::Hold:
        if(now-phaseStart>=5000)BeginMode(period+1,now);
        return;
    }
}
inline void DesktopFrame(HWND hwnd) {
    if(!desktopMode)return;
    if(window!=hwnd){window=hwnd;Title();}
    // No key is read: F5 is the quicksave key, and the first desktop check's
    // F5 toggle would have overwritten the quicksave slot.
    Sequence(GetTickCount64());
}
// Live HUD switch; see handHudShown. Present thread.
inline void SetHandHudShown(bool shown) {
    handHudShown=shown;
    handHudActive=shown && handHooksReady.load();
    if(shown)++generation; // re-arm every element to park again
}
inline void Install(uintptr_t base,bool handAvailable,bool shown) {
    // handAvailable: VR (EngineCamera) installs the hand HUD hooks whatever the
    // HUD setting; `shown` is that setting (HandHud in the INI), switchable
    // later with SetHandHudShown. It replaced the old
    // DeusExHRVR-hand-hud-quiet.enable marker, which is no longer read. The
    // desktop and isolation test modes keep their markers.
    static std::once_flag once;
    const bool desktop=GetFileAttributesW(L"DeusExHRVR-desktop-hud.enable")!=INVALID_FILE_ATTRIBUTES;
    const bool hand=!desktop && handAvailable;
    handHudShown=shown;
    if(!desktop && !hand && GetFileAttributesW(L"DeusExHRVR-hud-isolation.enable")==INVALID_FILE_ATTRIBUTES)return;
    std::call_once(once,[&]{
    desktopMode=desktop;handHudMode=hand;
    verbose=!hand || GetFileAttributesW(L"DeusExHRVR-hand-hud-log.enable")!=INVALID_FILE_ATTRIBUTES;
    // Hand HUD mode parks the three elements for the whole session.
    // The radar is not parked (see Classify); its slot still logs its geometry.
    // Revision 8: neither the aug cross (5) nor Mark & Track (6) is parked (see
    // Classify); their slots still report the clips' status.
    if(hand){requested[3]=park[3];requested[4]=0;requested[5]=0;requested[6]=0;Request(park[0],park[1],park[2]);}
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x52840914 || nt->OptionalHeader.SizeOfImage!=0x1c54000)return;
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return;
    image=base;BindNative();
    struct Hook {uintptr_t address;void* hook;void** original;const char* bytes;size_t length;};
    // The primitive hook is desktop-only. In VR, EngineCamera owns 0x532C70;
    // in desktop mode EngineCamera::Install never runs (it is reached only
    // through NativeBridge::Present, which desktop mode skips).
    Hook hooks[]={
        {0x7C05F0,(void*)&InitHook,(void**)&originalInit,"\x55\x8b\xec\x51\x89\x4d\xfc",7},
        {0x7C06C0,(void*)&UpdateHook,(void**)&originalUpdate,"\x55\x8b\xec\x83\xec\x08\x89\x4d\xf8",9},
        {0x532C70,(void*)&PrimitiveHook,(void**)&originalPrimitive,"\x83\xec\x20\x53\x8b\x5c\x24\x28",8}
    };
    const int hookCount=desktop?3:2;
    const char getBytes[]="\x8b\x49\x4c\x8b\x01\x8b\x40\x44\xff\xe0";
    const char setBytes[]="\x8b\x54\x24\x08\x8b\x49\x4c\x8b\x01\x8b\x40\x40\x6a\x01";
    // Child getter and setter both open with: sub esp,104h / mov eax,[cookie].
    const char childBytes[]="\x81\xec\x04\x01\x00\x00\xa1";
    // The path rewrite they share: push edi / mov edi,ecx / mov eax,[edi+50h].
    const char qualifyBytes[]="\x57\x8b\xf9\x8b\x47\x50\x85\xc0";
    if(memcmp((void*)VA(0x72F8E0),getBytes,sizeof(getBytes)-1) ||
       memcmp((void*)VA(0x72F8F0),setBytes,sizeof(setBytes)-1) ||
       memcmp((void*)VA(0x729DC0),childBytes,7) ||
       memcmp((void*)VA(0x729E20),childBytes,7) ||
       memcmp((void*)VA(0x7298F0),qualifyBytes,sizeof(qualifyBytes)-1)){Log("ABI-guard-failed");return;}
    // 0x550930 rebuilds the engine matrices; EngineCamera hooks it in VR with
    // this same byte guard. Here it is only called, never hooked.
    if(desktop && memcmp((void*)VA(0x550930),"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x84\x00\x00\x00",12)){Log("matrices-guard-failed");return;}
    matrices=reinterpret_cast<Matrices>(VA(0x550930));
    for(int i=0;i<hookCount;++i)if(memcmp((void*)VA(hooks[i].address),hooks[i].bytes,hooks[i].length)){Log("hook-guard-failed");return;}
    int created=0;bool ok=true;
    for(int i=0;i<hookCount;++i){if(MH_CreateHook((void*)VA(hooks[i].address),hooks[i].hook,hooks[i].original)!=MH_OK){ok=false;break;}++created;}
    if(ok)for(int i=0;i<hookCount;++i)if(MH_QueueEnableHook((void*)VA(hooks[i].address))!=MH_OK)ok=false;
    if(ok)ok=MH_ApplyQueued()==MH_OK;
    if(!ok)for(int i=0;i<created;++i){MH_DisableHook((void*)VA(hooks[i].address));MH_RemoveHook((void*)VA(hooks[i].address));}
    Log(ok?"isolation-check-enabled":"isolation-check-hook-failed");
    if(ok && hand){handHooksReady=true;handHudActive=handHudShown.load();}
    });
}
#else
inline bool desktopMode=false;
inline void DesktopFrame(HWND) {}
inline void Install(uintptr_t,bool,bool) {}
inline void SetHandHudShown(bool) {}
#endif
}
