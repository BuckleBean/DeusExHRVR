#pragma once
#include "NativeHandWeapon.h"

// Visual-only finger pose. No game action, calibration, file I/O or pose capture.
namespace InteractionCurl {
using namespace ModelHandAlignment;
using Q=CameraMath::Quaternion;
inline Matrix Translation(V v){auto m=CameraMath::Rotation({0,0,0,1});for(int j=0;j<3;j++)m.m[12+j]=float(v[j]);return m;}
inline Q NormalizeQ(Q q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
inline Q RotationQ(const Matrix& row){
    // Standard column-matrix conversion, transposed for CameraMath's row vectors.
    float a[3][3];for(int i=0;i<3;i++)for(int j=0;j<3;j++)a[i][j]=row.m[j*4+i];
    Q q{};float tr=a[0][0]+a[1][1]+a[2][2];
    if(tr>0){float s=2*std::sqrt(tr+1);q={ (a[2][1]-a[1][2])/s,(a[0][2]-a[2][0])/s,(a[1][0]-a[0][1])/s,s/4};}
    else {int i=a[1][1]>a[0][0]?1:0;if(a[2][2]>a[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;
        float s=2*std::sqrt(1+a[i][i]-a[j][j]-a[k][k]);float v[3]{};
        v[i]=s/4;v[j]=(a[i][j]+a[j][i])/s;v[k]=(a[i][k]+a[k][i])/s;
        q={v[0],v[1],v[2],(a[k][j]-a[j][k])/s};}
    return NormalizeQ(q);
}
inline Q Blend(Q a,Q b,float t){
    if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)b={-b.x,-b.y,-b.z,-b.w};
    return NormalizeQ({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
}
struct Definition {std::array<V,16> pivots{};std::array<Q,15> target{};unsigned palm{130};bool valid{};};
inline Q Axis(V axis,double degrees) {
    if(!Normalize(axis))return {0,0,0,1};
    const double half=degrees*.00872664625997,s=std::sin(half);
    return {float(axis[0]*s),float(axis[1]*s),float(axis[2]*s),float(std::cos(half))};
}
inline Q Mul(Q a,Q b) {
    return NormalizeQ({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                       a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
inline V Turn(Q q,V v) {
    const V u{q.x,q.y,q.z};const auto t=Scale(Cross(u,v),2);
    return Add(Add(v,Scale(t,q.w)),Cross(u,t));
}
// closed=false: the original gentle curl. closed=true: the "closed grip" chosen from
// the November 21 preview. left=true builds the same pose on the left hand's bones.
inline Definition Build(const std::array<Segment,223>& segments,unsigned count,bool left=false,bool closed=false){
    Definition d;Matrix frame{};
    if(!(left?BuildLeftFrame(segments,frame,count):BuildRightFrame(segments,frame,count)))return d;
    const unsigned palm=left?101:130;d.palm=palm;
    std::array<V,223> p{};
    for(unsigned i=0;i<count;i++)p[i]=i?Add(p[segments[i].parent],segments[i].pivot):segments[i].pivot;
    for(unsigned i=0;i<16;i++)d.pivots[i]=p[palm+i];
    // Toward the palm: the side the resting fingers already curl to, on either hand.
    V inward{frame.m[0],frame.m[1],frame.m[2]},curlDirection{};
    for(unsigned k:{1u,4u,7u,10u})curlDirection=Add(curlDirection,Sub(p[palm+k+2],p[palm+k]));
    if(Dot(inward,curlDirection)<0)inward=Scale(inward,-1);
    static constexpr double gentle[3]{15,25,15},gentleThumb[3]{4,6,4},grip[3]{58,82,48},gripThumb[3]{14,28,26};
    for(unsigned k:{1u,4u,7u,10u}){
        auto axis=Cross(Sub(p[palm+k+2],p[palm+k]),inward);if(!Normalize(axis))return {};
        for(unsigned j=0;j<3;j++)d.target[k+j-1]=Axis(axis,(closed?grip:gentle)[j]);
    }
    const unsigned thumb=palm+13;
    if(!closed) {
        auto axis=Cross(Sub(p[thumb+2],p[thumb]),inward);if(!Normalize(axis))return {};
        for(unsigned j=0;j<3;j++)d.target[12+j]=Axis(axis,gentleThumb[j]);
    } else {
        // Swing the thumb across the palm, then bend it toward the palm's centre.
        const V along{frame.m[4],frame.m[5],frame.m[6]};
        auto centre=Scale(Add(Add(Add(Add(p[palm],p[palm+1]),p[palm+4]),p[palm+7]),p[palm+10]),.2);
        centre=Add(centre,Scale(inward,12));
        const V tip=Sub(p[thumb+2],p[thumb]);
        const auto plus=Axis(along,28),minus=Axis(along,-28);
        const V a=Sub(Add(p[thumb],Turn(plus,tip)),centre),b=Sub(Add(p[thumb],Turn(minus,tip)),centre);
        const auto swing=Dot(a,a)<=Dot(b,b)?plus:minus;
        const auto bend=Cross(Turn(swing,tip),Sub(centre,p[thumb]));
        auto total=Mul(Axis(bend,gripThumb[0]),swing);
        d.target[12]=total;
        for(unsigned j=1;j<3;j++) {
            const auto joint=Axis(Turn({-total.x,-total.y,-total.z,total.w},bend),gripThumb[j]);
            d.target[12+j]=joint;total=Mul(total,joint);
        }
    }
    d.valid=true;return d;
}
inline bool Apply(Matrix* palette,unsigned count,const Definition& d,float amount){
    if(!palette||!d.valid||(count!=162&&count!=223)||!std::isfinite(amount)||amount<0||amount>1)return false;
    const unsigned palm=d.palm;
    if(amount==0)return true; // byte-identical native pose
    std::array<Matrix,16> native{},out{};
    for(unsigned i=0;i<16;i++){
        auto skin=Affine(palette[palm+i]);if(!NativeHandWeapon::Rigid(skin))return false;
        native[i]=CameraMath::Multiply(Translation(d.pivots[i]),skin);
    }
    out[0]=native[0];
    std::array<Matrix,15> result{};
    for(unsigned i=1;i<16;i++){
        unsigned parent=(i-1)%3==0?0:i-1;
        auto local=CameraMath::Multiply(native[i],CameraMath::InverseRigid(native[parent]));
        auto rot=CameraMath::Rotation(Blend(RotationQ(local),d.target[i-1],amount));
        // Preserve native joint translations (bone lengths), blend rotation only.
        for(int j=0;j<3;j++)rot.m[12+j]=local.m[12+j];
        out[i]=CameraMath::Multiply(rot,out[parent]);
        auto skin=CameraMath::Multiply(Translation(Scale(d.pivots[i],-1)),out[i]);
        if(!NativeHandWeapon::Rigid(skin))return false;
        for(unsigned j:{3u,7u,11u,15u})skin.m[j]=palette[palm+i].m[j];
        result[i-1]=skin;
    }
    for(unsigned i=0;i<15;i++)palette[palm+1+i]=result[i];
    return true;
}
// How closed a hand should be: its grip, with small dead zones at both ends. A press
// of Interact on a non-grip button closes the hand fully.
inline float GripTarget(float grip,bool pressed) {
    if(!std::isfinite(grip))grip=0;
    if(grip>=.08f){const float v=(grip-.05f)/.9f;return v<1?v:1.f;}
    return pressed?1.f:0.f;
}
// Follows the target quickly, without jumping if a reading steps.
struct GripEnvelope {
    uint64_t tick{},frame{};uintptr_t identity{};float value{};bool initialized{};
    float Update(uint64_t now,uint64_t scene,uintptr_t owner,bool eligible,float target) {
        if(!eligible||!owner){*this={};return 0;}
        if(!initialized||owner!=identity||now<tick||now-tick>250){*this={now,scene,owner,0,true};return 0;}
        if(scene==frame)return value;
        const float dt=float(now-tick);tick=now;frame=scene;
        if(!std::isfinite(target))target=0;
        target=target<0?0:target>1?1:target;
        if(target>value){value+=dt/70.f;if(value>target)value=target;}
        else {value-=dt/110.f;if(value<target)value=target;}
        return value;
    }
};
// Exact stable state only. Transition/read failure never means empty hand.
inline bool Eligible(bool fresh,bool supported,uint32_t flags,uint32_t pending){return fresh&&supported&&flags==4&&pending==0;}
struct Envelope {
    uint64_t tick{},frame{};uintptr_t identity{};float value{};bool initialized{};
    float Update(uint64_t now,uint64_t scene,uintptr_t owner,bool eligible,bool pressed){
        if(!eligible||!owner){*this={};return 0;}
        if(!initialized||owner!=identity||now<tick||now-tick>250){*this={now,scene,owner,0,true};return 0;}
        if(scene==frame)return value*value*(3-2*value); // repeated draw / stereo eye
        auto dt=now-tick;tick=now;frame=scene;
        float step=float(dt)/(pressed?160.f:220.f);
        value=pressed?(std::min)(1.f,value+step):(std::max)(0.f,value-step);
        return value*value*(3-2*value);
    }
};
}
