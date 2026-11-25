// Included inside EngineCamera's private namespace. Game-thread attachment state.
bool carriedAttachment{},carriedOpaque{},carriedAttachmentReady{};
CarryAttachment::Attachment carryAttachment;
uintptr_t attachmentManager{},attachmentBody{},attachmentOwner{};
bool attachmentLeft{};
thread_local uintptr_t attachmentUpdate{};
thread_local bool attachmentTargetApplied{};
using AttachmentGrab=uintptr_t(__thiscall*)(void*);
using AttachmentUpdate=uintptr_t(__thiscall*)(void*,uint32_t);
using AttachmentProduct=float*(__cdecl*)(float*,const float*,const float*);
using AttachmentFade=void(__cdecl*)(void*,int,float,int,int);
AttachmentGrab originalAttachmentGrab{};
AttachmentUpdate originalAttachmentUpdate{};
AttachmentProduct originalAttachmentProduct{};
AttachmentFade originalAttachmentFade{};

// Read actual contact on the selected physics body, not the pickup proxy's centre.
bool AttachmentContactRaw(void* player,uintptr_t selected,const float* palm,float scale,float* point) {
    uintptr_t world{},saved{};void* shape{};bool changed{},foundPoint{};
    alignas(16) unsigned char hits[32*0x80]{};
    alignas(16) float p[4]{palm[0],palm[1],palm[2],1},q[4]{0,0,0,1};
    alignas(16) uintptr_t ignore[4]{reinterpret_cast<uintptr_t>(player)};
    float best=.15f*.15f*scale*scale;
    __try {
        world=*reinterpret_cast<uintptr_t*>(VA(0x1c7bf64));if(!world||!player)return false;
        shape=reinterpret_cast<void*(__thiscall*)(uintptr_t)>(VA(0x50f020))(world+0x10);if(!shape)return false;
        reinterpret_cast<void(__thiscall*)(void*,int)>(VA(0x4fb620))(shape,8);
        reinterpret_cast<void(__thiscall*)(void*,void*)>(VA(0x4fb5e0))(shape,ignore);
        reinterpret_cast<void(__thiscall*)(void*,float,float,const float*,const float*)>(VA(0x4fcd50))(shape,.15f*scale,.001f*scale,p,q);
        reinterpret_cast<void(__thiscall*)(void*,int)>(VA(0x4fb550))(shape,0x2b);
        reinterpret_cast<void(__thiscall*)(void*,const float*)>(VA(0x4fd100))(shape,p);
        reinterpret_cast<void(__thiscall*)(void*,const float*)>(VA(0x4fd140))(shape,q);
        saved=*reinterpret_cast<uintptr_t*>(world+0x2330);changed=true;
        reinterpret_cast<void(__thiscall*)(uintptr_t,void*)>(VA(0x4c7680))(world,nullptr);
        const int count=reinterpret_cast<int(__thiscall*)(uintptr_t,void*,int,void*,int)>(VA(0x50f1c0))(world+0x10,hits,32,shape,1);
        for(int i=0;i<count&&i<32;i++) {
            auto* h=hits+i*0x80;void* a=*reinterpret_cast<void**>(h+0x58);void* b=*reinterpret_cast<void**>(h+0x5c);
            void* other=a==shape?b:b==shape?a:nullptr;if(!other)continue;
            if(reinterpret_cast<uintptr_t(__thiscall*)(void*)>(VA(0x5b2900))(other)!=selected)continue;
            auto* hit=reinterpret_cast<float*>(h);float d2=0;
            for(int j=0;j<3;j++){float d=hit[j]-palm[j];d2+=d*d;}
            if(std::isfinite(d2)&&d2<best){best=d2;memcpy(point,hit,12);foundPoint=true;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){foundPoint=false;}
    __try {
        if(changed)reinterpret_cast<void(__thiscall*)(uintptr_t,void*)>(VA(0x4c7680))(world,reinterpret_cast<void*>(saved));
        if(shape)reinterpret_cast<void(__thiscall*)(uintptr_t,void*)>(VA(0x50ef00))(world+0x10,shape);
    } __except(EXCEPTION_EXECUTE_HANDLER){foundPoint=false;}
    return foundPoint;
}

bool AttachmentHand(bool left,CameraMath::Matrix& hand,float& scale) {
    std::lock_guard lock(stateMutex);
    const auto now=GetTickCount64();
    const auto& grip=left?current.tracking.leftGrip:current.tracking.rightGrip;
    if(!requested||!motionControls||!current.active||!referenceValid||screenReasons||gameMenuOpen||
       !grip.valid||now<current.tracking.tick||now-current.tracking.tick>100)return false;
    auto frame=(current.nativeHeading||coverCamera.held)?current.controllerBase:RenderBase(current.originalWorld);
    hand=CameraMath::HeadWorld(frame,reference,grip.pose,current.scale);scale=current.scale;
    return CarryAttachment::Rigid(hand)&&std::isfinite(scale)&&scale>0;
}
void AttachmentClear(){carryAttachment.Reset();attachmentManager=attachmentBody=attachmentOwner=0;}
bool AttachmentObjectRaw(uintptr_t manager,bool beforeGrab,uintptr_t& owner,uintptr_t& body,CameraMath::Matrix& object) {
    __try {
        const auto playerHandle=*reinterpret_cast<uint32_t*>(manager+0x10);
        const auto handle=*reinterpret_cast<uint32_t*>(manager+0x14);
        if(handle==0x7fffffff||playerHandle==0x7fffffff)return false;
        auto resolve=reinterpret_cast<uintptr_t(__cdecl*)(uint32_t)>(VA(0x6082a0));
        if(resolve(playerHandle)!=forgivingPlayer.load())return false;
        owner=resolve(handle);if(!owner)return false;
        body=*reinterpret_cast<uintptr_t*>(beforeGrab?owner+0x284:manager+0xa0);
        if(!body)return false;
        const auto* q=reinterpret_cast<const float*>(body+0x90);
        float norm=0;for(int i=0;i<4;i++){if(!std::isfinite(q[i]))return false;norm+=q[i]*q[i];}
        if(std::abs(norm-1)>.02f)return false;
        object=CameraMath::Rotation({q[0],q[1],q[2],q[3]});
        for(int i=0;i<3;i++)object.m[12+i]=reinterpret_cast<const float*>(body+0x80)[i];
        return CarryAttachment::Rigid(object);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AttachmentStillRaw(uintptr_t manager) {
    __try {
        const int state=*reinterpret_cast<int*>(manager+0x128);
        return carryAttachment.valid&&manager==attachmentManager&&CarriedHand::BoxState(state)&&
            *reinterpret_cast<uintptr_t*>(manager+0xa0)==attachmentBody;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AttachmentPullRaw(uintptr_t manager,const CameraMath::Matrix& target,const float* q,const float* native,float* out) {
    __try {
        if(!AttachmentStillRaw(manager))return false;
        const auto constraint=*reinterpret_cast<uintptr_t*>(manager+0x9c);
        if(!constraint||*reinterpret_cast<uintptr_t*>(constraint+0x160)!=attachmentBody)return false;
        float anchor[3]{};
        if(!CarriedRotateRaw(q,reinterpret_cast<const float*>(constraint+0x10),anchor))return false;
        float d2=0;for(int i=0;i<3;i++) {
            out[i]=target.m[12+i]+anchor[i];
            const float d=out[i]-native[i];if(!std::isfinite(d))return false;d2+=d*d;
        }
        const float scale=liveWorldScale.Active();
        if(!std::isfinite(scale)||scale<=0||d2>4.f*scale*scale)return false;
        reinterpret_cast<void*(__thiscall*)(void*,const float*)>(VA(0x4f8de0))(reinterpret_cast<void*>(attachmentBody),q);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uintptr_t __fastcall AttachmentGrabHook(void* self,void*) {
    const auto manager=reinterpret_cast<uintptr_t>(self);
    CameraMath::Matrix hand{},object{};float scale{};uintptr_t owner{},body{};
    const bool localObject=carriedAttachmentReady&&AttachmentObjectRaw(manager,true,owner,body,object);
    if(localObject) {
        AttachmentClear();attachmentLeft=GamepadBridge::LastInteractWasLeft();
        float surface[3]{};bool contact=false;
        if(AttachmentHand(attachmentLeft,hand,scale)) {
            contact=AttachmentContactRaw(reinterpret_cast<void*>(forgivingPlayer.load()),body,hand.m+12,scale,surface);
            if(!contact&&owner==carrySurfaceOwner&&CarriedHand::Fresh(GetTickCount64(),carrySurfaceTick)) {
                memcpy(surface,carrySurfacePoint,12);contact=true;
            }
        }
        if(contact&&carryAttachment.Capture(hand,object,surface,scale)) {
            attachmentManager=manager;attachmentOwner=owner;attachmentBody=body;
            if(interactionTimingLog)TimingLine("attachment captured before native pickup");
        } else if(interactionTimingLog)TimingLine("attachment capture unavailable; existing hold retained");
    }
    const auto result=originalAttachmentGrab(self);
    if(localObject&&!AttachmentStillRaw(manager))AttachmentClear();
    return result;
}
bool AttachmentPose(uintptr_t manager,CameraMath::Matrix& target,float* q) {
    if(!AttachmentStillRaw(manager))return false;
    CameraMath::Matrix hand{};float scale{};
    return AttachmentHand(attachmentLeft,hand,scale)&&carryAttachment.Target(hand,target)&&CarryAttachment::Quat(target,q);
}
bool AttachmentWriteRaw(uintptr_t manager,float* position,float* rotation,const CameraMath::Matrix& target,const float* q) {
    __try {
        if(!AttachmentStillRaw(manager))return false;
        // Reject huge differences from the native target, rather than teleporting.
        float d2=0;for(int j=0;j<3;j++){if(!std::isfinite(position[j]))return false;const float d=position[j]-target.m[12+j];d2+=d*d;}
        const float scale=liveWorldScale.Active();
        if(!std::isfinite(scale)||scale<=0||d2>4.f*scale*scale)return false;
        memcpy(rotation,q,16);for(int j=0;j<3;j++)position[j]=target.m[12+j];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
float* __cdecl AttachmentProductHook(float* out,const float* a,const float* b) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-base+0x400000;
    auto* callerReturn=static_cast<unsigned char*>(_AddressOfReturnAddress());
    auto* result=originalAttachmentProduct(out,a,b);
    if(carriedAttachmentReady&&attachmentUpdate&&caller==0x6604e6&&result) {
        CameraMath::Matrix target{};alignas(16) float q[4]{};
        if(AttachmentPose(attachmentUpdate,target,q))
            attachmentTargetApplied=AttachmentWriteRaw(attachmentUpdate,reinterpret_cast<float*>(callerReturn+0x70),result,target,q);
    }
    return result;
}
uintptr_t __fastcall AttachmentUpdateHook(void* self,void*,uint32_t arg) {
    const auto previous=attachmentUpdate;const bool applied=attachmentTargetApplied;
    attachmentUpdate=carriedAttachmentReady?reinterpret_cast<uintptr_t>(self):0;attachmentTargetApplied=false;
    const auto result=originalAttachmentUpdate(self,arg);
    attachmentUpdate=previous;attachmentTargetApplied=applied;return result;
}
void __cdecl AttachmentFadeHook(void* owner,int mode,float amount,int duration,int flags) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-base+0x400000;
    if(carriedAttachmentReady&&carriedOpaque&&carryAttachment.valid&&reinterpret_cast<uintptr_t>(owner)==attachmentOwner&&
       (caller==0x663cef||caller==0x6600b3||caller==0x66012b)) {
        // Native release uses zero: this field is fading, not conventional opacity.
        amount=0;duration=0;
    }
    originalAttachmentFade(owner,mode,amount,duration,flags);
}
void InstallCarryAttachment(const wchar_t* config) {
    carriedAttachment=GetPrivateProfileIntW(L"VR",L"CarriedAttachment",0,config)!=0;
    carriedOpaque=GetPrivateProfileIntW(L"VR",L"CarriedOpaque",1,config)!=0;
    if(!carriedAttachment||!carriedItemAtHand)return;
    if(!CarryAttachmentNative::Matches(base)){if(interactionTimingLog)TimingLine("attachment native signatures unavailable");return;}
    struct Hook{uintptr_t address;void* hook;void** original;};
    Hook hooks[]{
        {0x6624b0,(void*)&AttachmentGrabHook,(void**)&originalAttachmentGrab},
        {0x660380,(void*)&AttachmentUpdateHook,(void**)&originalAttachmentUpdate},
        {0x4ce860,(void*)&AttachmentProductHook,(void**)&originalAttachmentProduct},
        {0x58faf0,(void*)&AttachmentFadeHook,(void**)&originalAttachmentFade}
    };
    unsigned made=0;bool ok=true;
    for(auto& h:hooks){if(MH_CreateHook((void*)VA(h.address),h.hook,h.original)!=MH_OK){ok=false;break;}++made;}
    if(ok)for(auto& h:hooks)if(MH_EnableHook((void*)VA(h.address))!=MH_OK){ok=false;break;}
    if(!ok)for(unsigned i=0;i<made;i++){MH_DisableHook((void*)VA(hooks[i].address));MH_RemoveHook((void*)VA(hooks[i].address));}
    carriedAttachmentReady=ok;
    if(interactionTimingLog)TimingLine(ok?"attachment hooks ready":"attachment hooks unavailable");
}
