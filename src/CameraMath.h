#pragma once
#include "SharedPair.h"
#include <cmath>
#include <cstring>
namespace CameraMath {
using Transport::Pose;using Transport::Vector;using Transport::Quaternion;
struct alignas(16) Matrix {float m[16]{};};
inline Quaternion Inverse(Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
inline Quaternion Multiply(Quaternion a,Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vector Rotate(Quaternion q,Vector v) {
    auto r=Multiply(Multiply(q,{v.x,v.y,v.z,0}),Inverse(q));return {r.x,r.y,r.z};
}
inline Matrix Rotation(Quaternion q) {
    Matrix r;
    Vector axes[3]={{1,0,0},{0,1,0},{0,0,1}};
    for(int i=0;i<3;i++){auto v=Rotate(q,axes[i]);r.m[i*4]=v.x;r.m[i*4+1]=v.y;r.m[i*4+2]=v.z;}
    r.m[15]=1;return r;
}
inline Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix r;for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)r.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j];return r;
}
inline Matrix InverseRigid(const Matrix& a) {
    Matrix r;for(int i=0;i<3;i++)for(int j=0;j<3;j++)r.m[i*4+j]=a.m[j*4+i];
    for(int j=0;j<3;j++)for(int k=0;k<3;k++)r.m[12+j]-=a.m[12+k]*r.m[k*4+j];r.m[15]=1;return r;
}
inline Matrix HeadWorld(const Matrix& game,const Pose& reference,const Pose& head,float scale) {
    auto q=Multiply(Inverse(reference.orientation),head.orientation);
    // PlayerCamera coordinates are right/down/forward; OpenXR is right/up/back.
    q.y=-q.y;q.z=-q.z;
    auto result=Multiply(Rotation(q),game);
    auto delta=Rotate(Inverse(reference.orientation),{head.position.x-reference.position.x,head.position.y-reference.position.y,head.position.z-reference.position.z});
    for(int j=0;j<3;j++)result.m[12+j]+=scale*(delta.x*game.m[j]-delta.y*game.m[4+j]-delta.z*game.m[8+j]);
    return result;
}
inline Matrix EyeWorld(const Transport::Tracking& t,unsigned eye,float scale) {
    const auto& e=t.eyes[eye];
    auto q=Multiply(Inverse(t.head.orientation),e.pose.orientation);
    auto pos=Rotate(Inverse(t.head.orientation),{e.pose.position.x-t.head.position.x,e.pose.position.y-t.head.position.y,e.pose.position.z-t.head.position.z});
    // RenderViewport coordinates are left-handed right/up/forward.
    auto eyeWorld=Rotation({-q.x,-q.y,q.z,q.w});
    eyeWorld.m[12]=pos.x*scale;eyeWorld.m[13]=pos.y*scale;eyeWorld.m[14]=-pos.z*scale;
    return eyeWorld;
}
inline Matrix EyeProjection(const Matrix& original,const Transport::Tracking& t,unsigned eye,float scale) {
    const auto& e=t.eyes[eye];
    float l=std::tan(e.left),r=std::tan(e.right),u=std::tan(e.up),d=std::tan(e.down);
    Matrix p;p.m[0]=2/(r-l);p.m[5]=2/(u-d);p.m[8]=-(r+l)/(r-l);p.m[9]=-(u+d)/(u-d);
    p.m[10]=original.m[10];p.m[11]=1;p.m[14]=original.m[14];
    return Multiply(InverseRigid(EyeWorld(t,eye,scale)),p);
}
// Reconstruct world positions from (textureU * eyeDepth, textureV *
// eyeDepth, eyeDepth, 1). Lighting must invert the same eye frustum as geometry.
inline Matrix DepthToWorld(const Matrix& viewportWorld,const Transport::Tracking& t,unsigned eye,float scale) {
    const auto& e=t.eyes[eye];
    float l=std::tan(e.left),r=std::tan(e.right),u=std::tan(e.up),d=std::tan(e.down);
    Matrix rays;rays.m[0]=r-l;rays.m[5]=d-u;rays.m[8]=l;rays.m[9]=u;rays.m[10]=rays.m[15]=1;
    return Multiply(Multiply(rays,EyeWorld(t,eye,scale)),viewportWorld);
}
inline Matrix Load(const void* p){Matrix m;std::memcpy(m.m,p,64);return m;}
// Map the UI's original clip rectangle to one head-relative plane, then project
// that SAME plane through each eye. Equal eye pixels are not equal headset rays.
inline Matrix HudClipTransform(const Transport::Tracking& t,unsigned eye) {
    Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    auto projection=EyeProjection(depth,t,eye,1.f);
    Matrix plane;plane.m[0]=1.6f;plane.m[5]=.9f;plane.m[14]=2.f;plane.m[15]=1;
    auto result=Multiply(plane,projection);
    // This is an overlay, at the near clip depth. Preserve UI stencil ordering.
    for(int i=0;i<4;i++)result.m[i*4+2]=0;
    return result;
}
}
