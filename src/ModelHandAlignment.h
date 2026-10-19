#pragma once
#include "CameraMath.h"
#include <array>
#include <cmath>
#include <algorithm>

// Row-vector transforms. F is a landmark frame IN model space, not a skin
// matrix translation and not the learned handBind calibration.
namespace ModelHandAlignment {
using CameraMath::Matrix;
using V=std::array<double,3>;
struct Segment {V pivot{};uint32_t parent{};};
inline V Add(V a,V b){for(int j=0;j<3;j++)a[j]+=b[j];return a;}
inline V Sub(V a,V b){for(int j=0;j<3;j++)a[j]-=b[j];return a;}
inline V Scale(V a,double s){for(auto& x:a)x*=s;return a;}
inline double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline bool Normalize(V& v){double n=std::sqrt(Dot(v,v));if(!std::isfinite(n)||n<1e-6)return false;v=Scale(v,1/n);return true;}
inline bool Finite(const Matrix& m){for(float v:m.m)if(!std::isfinite(v))return false;return true;}
inline Matrix Affine(Matrix m){m.m[3]=m.m[7]=m.m[11]=0;m.m[15]=1;return m;}
inline bool BuildRightFrame(const std::array<Segment,223>& segments,Matrix& result,unsigned count=223){
    if(count!=223 && count!=162)return false;
    std::array<V,223> p{};
    for(unsigned i=0;i<count;i++){
        for(auto v:segments[i].pivot)if(!std::isfinite(v)||std::abs(v)>10000)return false;
        auto parent=segments[i].parent;
        if(i==0){if(parent!=UINT32_MAX)return false;p[i]=segments[i].pivot;}
        else {if(parent>=i)return false;p[i]=Add(p[parent],segments[i].pivot);}
    }
    // Reject a differently numbered or proportioned skeleton, even if its mesh
    // happens to have matching counts. Values measured from installed resource87370.
    if(segments[129].parent!=128 || segments[130].parent!=128)return false;
    V expected{-209.548951,-14.734818,412.863129};
    if(Dot(Sub(p[130],expected),Sub(p[130],expected))>.01)return false;
    constexpr unsigned roots[]{131,134,137,140};
    V mean{},curl{};
    for(unsigned i:roots){
        if(segments[i].parent!=130 || segments[i+1].parent!=i || segments[i+2].parent!=i+1)return false;
        mean=Add(mean,Scale(p[i],.25));curl=Add(curl,Scale(Sub(p[i+2],p[i]),.25));
    }
    if(segments[143].parent!=130 || segments[144].parent!=143 || segments[145].parent!=144)return false;
    unsigned index=Dot(Sub(p[131],p[145]),Sub(p[131],p[145]))<Dot(Sub(p[140],p[145]),Sub(p[140],p[145]))?131:140;
    unsigned little=index==131?140:131;
    V z=Sub(p[little],p[index]),length=Sub(mean,p[130]);
    if(!Normalize(z)||!Normalize(length))return false;
    V x=Cross(length,z);if(!Normalize(x)||std::abs(Dot(x,curl))<1e-5)return false;
    if(Dot(x,curl)>0)x=Scale(x,-1); // right +X points into the palm
    V y=Cross(z,x);if(!Normalize(y))return false;x=Cross(y,z);if(!Normalize(x))return false;
    Matrix f{};auto center=Scale(Add(p[130],mean),.5);
    for(int j=0;j<3;j++){f.m[j]=float(x[j]);f.m[4+j]=float(y[j]);f.m[8+j]=float(z[j]);f.m[12+j]=float(center[j]);}
    f.m[15]=1;result=f;return true;
}
inline bool BuildLeftFrame(const std::array<Segment,223>& segments,Matrix& result,unsigned count=223){
    if(count!=223 && count!=162)return false;
    std::array<V,223> p{};
    for(unsigned i=0;i<count;i++){
        for(auto v:segments[i].pivot)if(!std::isfinite(v)||std::abs(v)>10000)return false;
        auto parent=segments[i].parent;
        if(i==0){if(parent!=UINT32_MAX)return false;p[i]=segments[i].pivot;}
        else {if(parent>=i)return false;p[i]=Add(p[parent],segments[i].pivot);}
    }
    // Reject a differently numbered or proportioned skeleton, even if its mesh
    // happens to have matching counts. Values measured from installed resource87370.
    if(segments[100].parent!=99 || segments[101].parent!=99)return false;
    V expected{209.548951,-14.734818,412.863129};
    if(Dot(Sub(p[101],expected),Sub(p[101],expected))>.01)return false;
    constexpr unsigned roots[]{102,105,108,111};
    V mean{},curl{};
    for(unsigned i:roots){
        if(segments[i].parent!=101 || segments[i+1].parent!=i || segments[i+2].parent!=i+1)return false;
        mean=Add(mean,Scale(p[i],.25));curl=Add(curl,Scale(Sub(p[i+2],p[i]),.25));
    }
    if(segments[114].parent!=101 || segments[115].parent!=114 || segments[116].parent!=115)return false;
    unsigned index=Dot(Sub(p[102],p[116]),Sub(p[102],p[116]))<Dot(Sub(p[111],p[116]),Sub(p[111],p[116]))?102:111;
    unsigned little=index==102?111:102;
    V z=Sub(p[little],p[index]),length=Sub(mean,p[101]);
    if(!Normalize(z)||!Normalize(length))return false;
    V x=Cross(length,z);if(!Normalize(x)||std::abs(Dot(x,curl))<1e-5)return false;
    if(Dot(x,curl)<0)x=Scale(x,-1); // left +X points out of the palm
    V y=Cross(z,x);if(!Normalize(y))return false;x=Cross(y,z);if(!Normalize(x))return false;
    Matrix f{};auto center=Scale(Add(p[101],mean),.5);
    for(int j=0;j<3;j++){f.m[j]=float(x[j]);f.m[4+j]=float(y[j]);f.m[8+j]=float(z[j]);f.m[12+j]=float(center[j]);}
    f.m[15]=1;result=f;return true;
}
inline bool GripTarget(const Transport::Tracking& t,uint64_t now,const Matrix& base,
                       const Transport::Pose& reference,float scale,Matrix& result,bool left=false){
    if(!t.valid||!(left?t.leftGrip.valid:t.rightGrip.valid)||!t.tick||now<t.tick||now-t.tick>=250||!std::isfinite(scale)||scale<=0||!Finite(base))return false;
    auto grip=left?t.leftGrip.pose:t.rightGrip.pose;
    auto validPose=[](const Transport::Pose& p){
        const auto& q=p.orientation;
        double n=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
        return std::isfinite(n)&&std::abs(n-1)<.01&&std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z);
    };
    if(!validPose(grip)||!validPose(reference))return false;
    auto target=CameraMath::HeadWorld(base,reference,grip,scale);
    // HeadWorld converts local OpenXR axes to camera right/down/forward.
    // F uses original grip axes, so U=diag(1,-1,-1,1) restores those axes.
    for(int j=0;j<3;j++){target.m[4+j]*=-1;target.m[8+j]*=-1;}
    if(!Finite(target))return false;result=target;return true;
}
inline bool Invert(const Matrix& m,Matrix& out){
    double a[4][8]{};
    if(!Finite(m))return false;
    for(int i=0;i<4;i++){for(int j=0;j<4;j++)a[i][j]=m.m[i*4+j];a[i][i+4]=1;}
    for(int col=0;col<4;col++){
        int pivot=col;for(int i=col+1;i<4;i++)if(std::abs(a[i][col])>std::abs(a[pivot][col]))pivot=i;
        if(std::abs(a[pivot][col])<1e-8)return false;
        if(pivot!=col)for(int j=0;j<8;j++)std::swap(a[pivot][j],a[col][j]);
        double d=a[col][col];for(int j=0;j<8;j++)a[col][j]/=d;
        for(int i=0;i<4;i++)if(i!=col){double f=a[i][col];for(int j=0;j<8;j++)a[i][j]-=f*a[col][j];}
    }
    for(int i=0;i<4;i++)for(int j=0;j<4;j++)out.m[i*4+j]=float(a[i][j+4]);
    return Finite(out);
}
inline bool MoveWithDelta(Matrix* palette,unsigned count,const std::array<bool,223>& selected,
                          const Matrix& delta){
    if(!palette||(count!=223 && count!=162)||!selected[130]||!Finite(delta))return false;
    // Reject scale/shear/reflection rather than distorting a hand. The native
    // skin palettes observed in this model are rigid. Small float drift is allowed.
    for(int i=0;i<3;i++)for(int j=0;j<3;j++){
        double dot=0;for(int k=0;k<3;k++)dot+=double(delta.m[i*4+k])*delta.m[j*4+k];
        if(!std::isfinite(dot)||std::abs(dot-(i==j?1.:0.))>.02)return false;
    }
    V x{delta.m[0],delta.m[1],delta.m[2]},y{delta.m[4],delta.m[5],delta.m[6]},z{delta.m[8],delta.m[9],delta.m[10]};
    if(Dot(Cross(x,y),z)<.98)return false;
    std::array<Matrix,17> moved{};
    for(unsigned i=0;i<223;i++)if(selected[i]){
        if(i<129||i>145||!Finite(palette[i]))return false;
        moved[i-129]=CameraMath::MoveSkinMatrix(palette[i],delta);
        if(!Finite(moved[i-129]))return false;
    }
    // Commit only once every selected bone has passed validation.
    for(unsigned i=129;i<=145;i++)if(selected[i])palette[i]=moved[i-129];
    return true;
}
inline bool Move(Matrix* palette,unsigned count,const std::array<bool,223>& selected,
                 const Matrix& frame,const Matrix& target){
    if(!palette||(count!=223 && count!=162)||!Finite(frame)||!Finite(target))return false;
    Matrix inverse{};
    auto world=CameraMath::Multiply(frame,Affine(palette[130]));
    if(!Invert(world,inverse))return false;
    return MoveWithDelta(palette,count,selected,CameraMath::Multiply(inverse,target));
}
// Neutral right wrist. After placement, 129 (wrist) and 130 (palm) still carry
// their native RELATIVE animation - which Mark saw in VR as the wrist wiggling
// during sprint and sitting at a different angle per weapon. These are
// model-space skin matrices, so giving 129 the same spatial transform as 130
// restores the mesh's resting wrist/palm relationship and makes every vertex
// weighted across the pair rigid. Slots 3/7/11/15 are the shader's w-column and
// must survive, so 129 keeps its own - the same idiom RestLeft uses below.
// Applied AFTER the move, so the motion diagnostic's "after" sample is honest.
inline bool NeutralWrist(Matrix* palette,unsigned count,const std::array<bool,223>& selected){
    if(!palette||(count!=223 && count!=162)||!selected[129]||!selected[130])return false;
    if(!Finite(palette[129])||!Finite(palette[130]))return false;
    auto old=palette[129];palette[129]=palette[130];
    for(unsigned j:{3u,7u,11u,15u})palette[129].m[j]=old.m[j];
    return Finite(palette[129]);
}
inline bool RestLeft(Matrix* palette,unsigned count,const std::array<bool,223>& selected,
                     const Matrix& frame,const Matrix& target){
    if(!palette || (count!=223 && count!=162) || !selected[101] || !Finite(target))return false;
    Matrix inverse{};if(!Invert(frame,inverse))return false;
    auto delta=CameraMath::Multiply(inverse,target);
    if(!Finite(delta))return false;
    for(unsigned i=0;i<223;i++)if(selected[i] && (i<100 || i>116 || !Finite(palette[i])))return false;
    for(unsigned i=100;i<=116;i++)if(selected[i]){
        auto old=palette[i];palette[i]=delta;
        for(unsigned j: {3u,7u,11u,15u})palette[i].m[j]=old.m[j];
    }
    return true;
}

}
