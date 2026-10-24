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
struct Definition {std::array<V,16> pivots{};std::array<Q,15> target{};bool valid{};};
inline Definition Build(const std::array<Segment,223>& segments,unsigned count){
    Definition d;Matrix frame{};if(!BuildRightFrame(segments,frame,count))return d;
    std::array<V,223> p{};
    for(unsigned i=0;i<count;i++)p[i]=i?Add(p[segments[i].parent],segments[i].pivot):segments[i].pivot;
    for(unsigned i=130;i<=145;i++)d.pivots[i-130]=p[i];
    // Match the approved offline mesh preview's inward direction.
    V inward{-frame.m[0],-frame.m[1],-frame.m[2]};
    for(unsigned root:{131u,134u,137u,140u,143u}){
        auto axis=Cross(Sub(p[root+2],p[root]),inward);if(!Normalize(axis))return {};
        for(unsigned j=0;j<3;j++){
            const float degrees=root==143?(j==1?6.f:4.f):(j==1?25.f:15.f);
            const float half=degrees*.00872664626f,s=std::sin(half);
            d.target[root+j-131]={float(axis[0])*s,float(axis[1])*s,float(axis[2])*s,std::cos(half)};
        }
    }
    d.valid=true;return d;
}
inline bool Apply(Matrix* palette,unsigned count,const Definition& d,float amount){
    if(!palette||!d.valid||(count!=162&&count!=223)||!std::isfinite(amount)||amount<0||amount>1)return false;
    if(amount==0)return true; // byte-identical native pose
    std::array<Matrix,16> native{},out{};
    for(unsigned i=0;i<16;i++){
        auto skin=Affine(palette[130+i]);if(!NativeHandWeapon::Rigid(skin))return false;
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
        for(unsigned j:{3u,7u,11u,15u})skin.m[j]=palette[130+i].m[j];
        result[i-1]=skin;
    }
    for(unsigned i=0;i<15;i++)palette[131+i]=result[i];
    return true;
}
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
