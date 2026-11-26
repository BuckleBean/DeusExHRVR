#pragma once
#include "SharedPair.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
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
// Remove native look pitch from a rendering copy only. The game's Z-up
// PlayerCamera uses right/down/forward axes; rotation about local right
// preserves its yaw, roll and position. OpenXR pitch is applied afterward.
inline Matrix WithoutLookPitch(const Matrix& game) {
    float pitch=std::atan2(game.m[10],-game.m[6]);
    return Multiply(Rotation({-std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)}),game);
}
inline Matrix HorizontalDirection(const Matrix& source,const Matrix& fallback) {
    float x=source.m[8],y=source.m[9];
    float length=std::hypot(x,y);
    // At a vertical aim, the right axis still supplies a usable heading.
    if(length<.001f){x=-source.m[1];y=source.m[0];length=std::hypot(x,y);}
    if(length<.001f){x=fallback.m[8];y=fallback.m[9];length=std::hypot(x,y);}
    if(length<.001f){x=0;y=1;length=1;}
    x/=length;y/=length;
    Matrix result{{y,-x,0,0, 0,0,-1,0, x,y,0,0, fallback.m[12],fallback.m[13],fallback.m[14],1}};
    return result;
}
inline float HeadingDelta(const Matrix& from,const Matrix& to) {
    auto a=HorizontalDirection(from,from),b=HorizontalDirection(to,from);
    return std::atan2(a.m[8]*b.m[9]-a.m[9]*b.m[8],a.m[8]*b.m[8]+a.m[9]*b.m[9]);
}
struct MovementAxes {float strafe,walk;};
// Shape processed locomotion, after native deadzone handling. Preserve direction,
// full deflection, and out-of-range combined inputs. No temporal smoothing.
inline MovementAxes MovementResponse(float x,float y,unsigned mode){
    float r=std::hypot(x,y);
    if(mode==0||mode>2||!std::isfinite(r)||r>=1.f)return {x,y};
    if(mode==1){float scale=.5f+.5f*r;return {x*scale,y*scale};}
    if(r==0.f)return {x,y};
    // Gentler: broad middle range, then a deliberate outer-stick rise.
    // Hermite joins preserve value and slope. Knots (input,output):
    // (0,0), (.2,.2), (.8,.55), (1,1). Not measured world speeds.
    float lo,hi,a,b,da,db;
    if(r<.2f){lo=0;hi=.2f;a=0;b=.2f;da=1;db=.7f;}
    else if(r<.8f){lo=.2f;hi=.8f;a=.2f;b=.55f;da=.7f;db=1;}
    else {lo=.8f;hi=1;a=.55f;b=1;da=1;db=2.75f;}
    float width=hi-lo,t=(r-lo)/width,t2=t*t,t3=t2*t;
    float output=(2*t3-3*t2+1)*a+(t3-2*t2+t)*width*da
        +(-2*t3+3*t2)*b+(t3-t2)*width*db;
    float scale=output/r;return {x*scale,y*scale};
}
inline MovementAxes ReorientMovement(float strafe,float walk,const Matrix& native,const Matrix& target) {
    // Native locomotion actions use the opposite yaw sense to the camera's
    // right/forward basis. Direct world-vector projection mirrored the turn:
    // mouse north + head west sent W east. Rotate in native input coordinates.
    float yaw=HeadingDelta(native,target),c=std::cos(yaw),s=std::sin(yaw);
    return {c*strafe+s*walk,-s*strafe+c*walk};
}
inline Matrix ControllerMuzzle(const Matrix& base,const Pose& reference,const Pose& aim,float scale,float muzzleForward) {
    auto hand=HeadWorld(base,reference,aim,scale);
    Matrix muzzle=hand;
    // Native muzzle firing direction is -Y (callers at 0x75313c/0x6df718).
    // Its top is +Z. PlayerCamera's second row points DOWN, so the first
    // experiment's [right,-forward,down] basis rolled the gun upside down.
    for(int j=0;j<3;j++) {
        muzzle.m[j]=-hand.m[j];
        muzzle.m[4+j]=-hand.m[8+j];muzzle.m[8+j]=-hand.m[4+j];
        muzzle.m[12+j]+=hand.m[8+j]*muzzleForward*scale;
    }
    return muzzle;
}
inline Matrix FiringFromMuzzle(const Matrix& muzzle) {
    auto firing=muzzle;
    // Native 0x750dc0 converts muzzle -Y to the +Z row consumed by the
    // player's spread/raycast calculation (0x763370 -> 0x762e80).
    for(int j=0;j<3;j++) {
        firing.m[4+j]=muzzle.m[8+j];firing.m[8+j]=-muzzle.m[4+j];
    }
    return firing;
}
inline Matrix MoveSkinMatrix(const Matrix& skin,const Matrix& delta) {
    // 0x538160 stores skinning translation with w=0, despite the shader
    // using it as a POSITION. Ordinary 4x4 multiplication loses delta's
    // translation for these bones, leaving animated weapon parts behind.
    auto affine=skin;affine.m[3]=affine.m[7]=affine.m[11]=0;affine.m[15]=1;
    auto moved=Multiply(affine,delta);
    for(int i:{3,7,11,15})moved.m[i]=skin.m[i];
    return moved;
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
inline Matrix EyeFrustum(const Matrix& original,const Transport::Tracking& t,unsigned eye) {
    const auto& e=t.eyes[eye];
    float l=std::tan(e.left),r=std::tan(e.right),u=std::tan(e.up),d=std::tan(e.down);
    Matrix p;p.m[0]=2/(r-l);p.m[5]=2/(u-d);p.m[8]=-(r+l)/(r-l);p.m[9]=-(u+d)/(u-d);
    p.m[10]=original.m[10];p.m[11]=1;p.m[14]=original.m[14];
    return p;
}
inline Matrix EyeProjection(const Matrix& original,const Transport::Tracking& t,unsigned eye,float scale) {
    return Multiply(InverseRigid(EyeWorld(t,eye,scale)),EyeFrustum(original,t,eye));
}
// A camera-centred sky dome represents directions, not nearby geometry.
// Keep its rotation and native depth range, but remove head/eye translation.
inline Matrix SkyProjection(Matrix world,Matrix viewportWorld,const Matrix& projection,const Transport::Tracking& t,unsigned eye) {
    for(int i=12;i<15;i++){world.m[i]=0;viewportWorld.m[i]=0;}
    return Multiply(Multiply(world,InverseRigid(viewportWorld)),EyeProjection(projection,t,eye,0));
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
// size (HudSize / 100) shrinks the plane about its centre, at the same 2 m; 1 = 3.2 m x 1.8 m.
inline Matrix HudClipTransform(const Transport::Tracking& t,unsigned eye,float size=1.f) {
    Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    auto projection=EyeProjection(depth,t,eye,1.f);
    if(!(size>0&&size<=1))size=1;
    Matrix plane;plane.m[0]=1.6f*size;plane.m[5]=.9f*size;plane.m[14]=2.f;plane.m[15]=1;
    auto result=Multiply(plane,projection);
    // This is an overlay, at the near clip depth. Preserve UI stencil ordering.
    for(int i=0;i<4;i++)result.m[i*4+2]=0;
    return result;
}
// A small flat panel that carries one HUD element to a hand. The element keeps
// the shape it has on the HUD plane, reduced by `scale`. Its point (centreX,
// centreY) of the UI clip rectangle sits at `anchor` plus `offset`, where the
// offset is in head space (right, up, forward, metres). The panel stays
// parallel to the HUD plane, so it faces the head, and it is projected through
// each eye from its real position, so it has the hand's depth. Returns false
// when the panel is too near the face or behind the viewing plane. The caller
// hides it, never returns it to the normal HUD plane.
// Wrist HUD, worn like a watch band (Mark, November 12: "wrapped around the
// wrist"; "like a watch ... is more about how the element is worn on the
// wrist"; text "upright" with the hand raised, back of the hand toward him).
// Each block's long side wraps around the wrist; its text reads across the
// wrist, upright toward the hand. Input: the OpenXR GRIP pose, which the game's
// hand model is aligned to axis for axis (ModelHandAlignment::GripTarget,
// BuildLeftFrame / BuildRightFrame): +Y knuckles toward the wrist, +Z index
// toward little finger, back of the hand -X (left) / +X (right). The band's
// middle is on the back of the wrist, `along` metres from the palm centre
// toward the elbow, `radius` from the forearm's centre line.
struct WristLayout { float centreX,centreY,scale,along,radius; };
inline Vector HeadDirection(const Transport::Tracking& t,Vector v) {
    auto r=Rotate(Inverse(t.head.orientation),v);return {r.x,r.y,-r.z};
}
// `curved`: the piece being drawn sits on the wrist at its own horizontal
// position `drawX` in the HUD's clip rectangle, so the block bends around the
// wrist piece by piece; otherwise at the block's middle. Returns false (hide
// this piece) when it is within 15 cm of the head or behind it.
inline bool WristPanelTransform(const Transport::Tracking& t,unsigned eye,Vector gripPosition,Quaternion grip,
        bool left,const WristLayout& w,bool curved,float drawX,Matrix& result) {
    auto x=Rotate(grip,{1,0,0}),y=Rotate(grip,{0,1,0}),z=Rotate(grip,{0,0,1});
    Vector axis{gripPosition.x+y.x*w.along-t.head.position.x,gripPosition.y+y.y*w.along-t.head.position.y,gripPosition.z+y.z*w.along-t.head.position.z};
    auto a=HeadDirection(t,axis);
    auto back=HeadDirection(t,left?Vector{-x.x,-x.y,-x.z}:x);   // back of the model's hand
    // Left to right across the back of the wrist, seen with the back of the
    // hand toward you and the hand up: from the little finger to the thumb on
    // the left hand, from the thumb to the little finger on the right.
    auto across=HeadDirection(t,left?Vector{-z.x,-z.y,-z.z}:z);
    auto up=HeadDirection(t,{-y.x,-y.y,-y.z});                  // toward the hand
    const float halfWidth=1.6f*w.scale,halfHeight=.9f*w.scale;
    const float xRef=curved?drawX:w.centreX;
    float theta=(xRef-w.centreX)*halfWidth/w.radius;
    if(!std::isfinite(theta) || std::abs(theta)>2.2f)return false;
    const float c=std::cos(theta),s=std::sin(theta);
    Vector normal{c*back.x+s*across.x,c*back.y+s*across.y,c*back.z+s*across.z};
    Vector tangent{-s*back.x+c*across.x,-s*back.y+c*across.y,-s*back.z+c*across.z};
    Vector p{a.x+w.radius*normal.x,a.y+w.radius*normal.y,a.z+w.radius*normal.z};
    const float distance=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    if(!std::isfinite(distance) || distance<.15f || p.z<=.01f)return false;
    Matrix plane;
    plane.m[0]=halfWidth*tangent.x;plane.m[1]=halfWidth*tangent.y;plane.m[2]=halfWidth*tangent.z;
    plane.m[4]=halfHeight*up.x;plane.m[5]=halfHeight*up.y;plane.m[6]=halfHeight*up.z;
    plane.m[12]=p.x-xRef*halfWidth*tangent.x-w.centreY*halfHeight*up.x;
    plane.m[13]=p.y-xRef*halfWidth*tangent.y-w.centreY*halfHeight*up.y;
    plane.m[14]=p.z-xRef*halfWidth*tangent.z-w.centreY*halfHeight*up.z;
    plane.m[15]=1;
    Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    result=Multiply(plane,EyeProjection(depth,t,eye,1.f));
    // An overlay like the HUD plane: preserve UI stencil ordering.
    for(int i=0;i<4;i++)result.m[i*4+2]=0;
    return true;
}
// Smooth wrist band (Mark: the per-piece bend above "peels"). The band's
// geometry for the replacement vertex shader shaders/HandBand.hlsl, which
// bends every vertex. Same placement as WristPanelTransform.
// Since November 13 the band works in the HUD's flat layout units (the X, Y
// rows the game's shader computes before it tilts the HUD panel): middleX/Y
// are in those units, metresX/Y are metres per unit (signed), and xShift is
// added to X to bring a parked element home.
struct BandFrame {
    Vector axis{},back{},across{},up{};   // head space (right, up, forward)
    float radius{},metresX{},metresY{},middleX{},middleY{},maxTheta{},xShift{};
    Matrix eyeProjection;
};
inline bool WristBandFrame(const Transport::Tracking& t,unsigned eye,Vector gripPosition,Quaternion grip,
        bool left,const WristLayout& w,BandFrame& f) {
    auto x=Rotate(grip,{1,0,0}),y=Rotate(grip,{0,1,0}),z=Rotate(grip,{0,0,1});
    Vector axis{gripPosition.x+y.x*w.along-t.head.position.x,gripPosition.y+y.y*w.along-t.head.position.y,gripPosition.z+y.z*w.along-t.head.position.z};
    f.axis=HeadDirection(t,axis);
    f.back=HeadDirection(t,left?Vector{-x.x,-x.y,-x.z}:x);
    f.across=HeadDirection(t,left?Vector{-z.x,-z.y,-z.z}:z);
    f.up=HeadDirection(t,{-y.x,-y.y,-y.z});
    f.radius=w.radius;f.metresX=1.6f*w.scale;f.metresY=.9f*w.scale;f.middleX=w.centreX;f.middleY=w.centreY;f.maxTheta=2.2f;
    Vector p{f.axis.x+f.radius*f.back.x,f.axis.y+f.radius*f.back.y,f.axis.z+f.radius*f.back.z};
    const float distance=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    if(!std::isfinite(distance) || distance<.15f || p.z<=.01f)return false;
    Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    f.eyeProjection=EyeProjection(depth,t,eye,1.f);
    return true;
}
// Where a point of the band's flat HUD layout (X as the game computes it,
// before xShift) lands in head space: the CPU mirror of BandPosition in
// shaders/HandBand.hlsl, for the offline checks.
inline bool BandPoint(const BandFrame& f,float layoutX,float layoutY,Vector& head) {
    const float theta=(layoutX+f.xShift-f.middleX)*f.metresX/f.radius;
    if(!std::isfinite(theta) || std::abs(theta)>f.maxTheta)return false;
    const float c=std::cos(theta),s=std::sin(theta),v=(layoutY-f.middleY)*f.metresY;
    head={f.axis.x+f.radius*(c*f.back.x+s*f.across.x)+v*f.up.x,
          f.axis.y+f.radius*(c*f.back.y+s*f.across.y)+v*f.up.y,
          f.axis.z+f.radius*(c*f.back.z+s*f.across.z)+v*f.up.z};
    return true;
}
// Positive where that point of the band faces the head (shown), negative on
// the far side of the wrist (clipped): the shader's SV_ClipDistance.
inline float BandFacing(const BandFrame& f,float layoutX,float layoutY) {
    Vector h;if(!BandPoint(f,layoutX,layoutY,h))return -1;
    const float theta=(layoutX+f.xShift-f.middleX)*f.metresX/f.radius,c=std::cos(theta),s=std::sin(theta);
    const Vector n{c*f.back.x+s*f.across.x,c*f.back.y+s*f.across.y,c*f.back.z+s*f.across.z};
    const float l=std::sqrt(h.x*h.x+h.y*h.y+h.z*h.z);
    return -(n.x*h.x+n.y*h.y+n.z*h.z)/(l>1e-4f?l:1e-4f);
}
inline bool BandClip(const BandFrame& f,float layoutX,float layoutY,float clip[4]) {
    Vector h;if(!BandPoint(f,layoutX,layoutY,h))return false;
    const auto& m=f.eyeProjection.m;
    for(int j=0;j<4;++j)clip[j]=h.x*m[j]+h.y*m[4+j]+h.z*m[8+j]+m[12+j];
    clip[2]=0;
    return true;
}
// Collapse all vertices outside the clip volume, retaining native draw order.
inline Matrix HiddenHudTransform() {
    Matrix m;m.m[12]=2;m.m[13]=2;m.m[15]=1;return m;
}
inline bool HudPanelTransform(const Transport::Tracking& t,unsigned eye,const Pose& anchor,
        float centreX,float centreY,float scale,Vector offset,Matrix& result) {
    auto p=Rotate(Inverse(t.head.orientation),{anchor.position.x-t.head.position.x,anchor.position.y-t.head.position.y,anchor.position.z-t.head.position.z});
    // Head space as in EyeWorld: right/up/forward.
    float x=p.x+offset.x,y=p.y+offset.y,z=-p.z+offset.z;
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z<=.01f || x*x+y*y+z*z<.15f*.15f)return false;
    Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    auto projection=EyeProjection(depth,t,eye,1.f);
    float halfWidth=1.6f*scale,halfHeight=.9f*scale;
    Matrix plane;plane.m[0]=halfWidth;plane.m[5]=halfHeight;
    plane.m[12]=x-centreX*halfWidth;plane.m[13]=y-centreY*halfHeight;plane.m[14]=z;plane.m[15]=1;
    result=Multiply(plane,projection);
    // An overlay like the HUD plane: preserve UI stencil ordering.
    for(int i=0;i<4;i++)result.m[i*4+2]=0;
    return true;
}
}
