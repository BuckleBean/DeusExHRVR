#pragma once
#include "InteractionCurl.h"

// Experimental full arms. All math is row-vector, in game world units. Final
// accepted palm/finger palettes are authoritative. Forearm helpers100/129 may move.
namespace FullArmIK {
using namespace ModelHandAlignment;
struct Rig {std::array<V,223> p{};std::array<uint32_t,223> parent{};unsigned count{};bool valid{};};
inline Rig Build(const std::array<Segment,223>& s,unsigned count){
    Rig r;Matrix check{};
    if(!BuildRightFrame(s,check,count)||!BuildLeftFrame(s,check,count))return r;
    r.count=count;
    for(unsigned i=0;i<count;i++){r.parent[i]=s[i].parent;r.p[i]=i?Add(r.p[s[i].parent],s[i].pivot):s[i].pivot;}
    for(auto sh:{96u,125u}){
        unsigned el=sh+3,hand=sh+5;
        if(s[el].parent!=sh||s[hand].parent!=el)return {};
        double a=std::sqrt(Dot(Sub(r.p[el],r.p[sh]),Sub(r.p[el],r.p[sh])));
        double b=std::sqrt(Dot(Sub(r.p[hand],r.p[el]),Sub(r.p[hand],r.p[el])));
        if(std::abs(a-87.223)>.1||std::abs(b-85.694)>.1)return {};
    }
    r.valid=true;return r;
}
inline V Point(V p,const Matrix& m){V v{};for(int j=0;j<3;j++){v[j]=m.m[12+j];for(int k=0;k<3;k++)v[j]+=p[k]*m.m[k*4+j];}return v;}
inline Matrix Frame(V axis,V normal){
    Normalize(axis);normal=Sub(normal,Scale(axis,Dot(axis,normal)));Normalize(normal);
    auto third=Cross(axis,normal);auto m=CameraMath::Rotation({0,0,0,1});
    for(int j=0;j<3;j++){m.m[j]=float(axis[j]);m.m[4+j]=float(normal[j]);m.m[8+j]=float(third[j]);}return m;
}
inline Matrix Map(const Matrix& rotation,V from,V to){
    auto m=rotation;auto moved=Point(from,m);
    for(int j=0;j<3;j++)m.m[12+j]=float(to[j]-moved[j]);return m;
}
inline Matrix SegmentMap(V rest,V target,const Matrix& rotation,V origin,V destination){
    double oldLength=std::sqrt(Dot(rest,rest)),newLength=std::sqrt(Dot(target,target));
    auto axis=Scale(rest,1/oldLength);double stretch=newLength/oldLength;
    auto scale=CameraMath::Rotation({0,0,0,1});
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)scale.m[i*4+j]+=float((stretch-1)*axis[i]*axis[j]);
    return Map(CameraMath::Multiply(scale,rotation),origin,destination);
}
inline bool ValidSkin(const Matrix& m){
    if(!Finite(m))return false;
    // Output permits bounded positive axial scaling, unlike a rigid hand pose.
    double determinant= m.m[0]*(m.m[5]*m.m[10]-m.m[6]*m.m[9])
        -m.m[1]*(m.m[4]*m.m[10]-m.m[6]*m.m[8])
        +m.m[2]*(m.m[4]*m.m[9]-m.m[5]*m.m[8]);
    return determinant>.95 && determinant<1.151;
}
constexpr double FoldRegionFraction=.15;
struct FoldAnchor {bool active{};V upper{},lower{};};
struct History { std::array<V,2> bend{}; std::array<bool,2> valid{}; std::array<FoldAnchor,2> fold{}; };
struct Joints {V shoulder{},elbow{},wrist{};};
inline bool Solve(V shoulder,V wrist,double upper,double lower,V pole,Joints& out,const V* previous=nullptr,V* solvedBend=nullptr,const FoldAnchor* previousFold=nullptr,FoldAnchor* nextFold=nullptr){
    if(!std::isfinite(upper)||!std::isfinite(lower)||upper<=0||lower<=0)return false;
    auto dir=Sub(wrist,shoulder);double distance=std::sqrt(Dot(dir,dir));
    constexpr double MaxStretch=1.15;
    const double reach=upper+lower;
    if(!std::isfinite(distance)||distance>reach*.995*MaxStretch)return false;
    // Keep the shoulder anchored. Limited axial stretch preserves the tracked
    // hand at extended reach; never translate the shoulder to conceal mismatch.
    double stretch=std::max(1.,distance/(reach*.995));upper*=stretch;lower*=stretch;
    if(distance<1e-5)dir={0,0,-1};else dir=Scale(dir,1/distance);
    // Near a fully folded arm, equalize the almost-equal segment lengths so
    // wrist and shoulder can coincide without moving either endpoint.
    if(distance<std::abs(upper-lower)*2){
        double mean=(upper+lower)*.5;
        double t=distance/(std::abs(upper-lower)*2);
        upper=mean+(upper-mean)*t;lower=mean+(lower-mean)*t;
    }
    auto d=distance;
    auto root=shoulder;
    auto bend=Sub(pole,Scale(dir,Dot(pole,dir)));
    if(previous){
        auto transported=Sub(*previous,Scale(dir,Dot(*previous,dir)));
        if(Normalize(transported)){
            double strength=std::sqrt(Dot(bend,bend)/(std::max)(1e-12,Dot(pole,pole)));
            if(strength<=.1)bend=transported;
            else {
                Normalize(bend);
                if(Dot(bend,transported)<0)bend=Scale(bend,-1);
                double weight=std::clamp((strength-.1)/.15,0.,1.);
                weight=weight*weight*(3-2*weight);
                bend=Add(Scale(transported,1-weight),Scale(bend,weight));
            }
        }
    }
    if(!Normalize(bend)){
        V fallback=std::abs(dir[2])<.8?V{0,0,-1}:V{1,0,0};
        bend=Sub(fallback,Scale(dir,Dot(fallback,dir)));if(!Normalize(bend))return false;
    }
    double along=d>1e-5?(upper*upper-lower*lower+d*d)/(2*d):0.;
    double height=std::sqrt((std::max)(0.,upper*upper-along*along));
    Joints result{root,Add(Add(root,Scale(dir,along)),Scale(bend,height)),wrist};
    FoldAnchor fold{};
    (void)previousFold;
    out=result;
    if(solvedBend)*solvedBend=bend;
    if(nextFold)*nextFold=fold;
    return true;
}
inline bool Hand(unsigned i){return (i>=101&&i<=116)||(i>=130&&i<=145);}
inline bool Descendant(const Rig& r,unsigned i,unsigned ancestor){
    for(unsigned n=0;n<r.count && i<r.count;n++){if(i==ancestor)return true;i=r.parent[i];}return false;
}
inline bool Apply(Matrix* palette,unsigned count,const Rig& r,const Matrix& head,
                  const std::array<bool,223>& used,const History* previous=nullptr,History* next=nullptr){
    if(!palette||!r.valid||count!=r.count||!NativeHandWeapon::Rigid(Affine(head)))return false;
    auto horizontal=CameraMath::HorizontalDirection(head,head);
    V right{horizontal.m[0],horizontal.m[1],0},forward{horizontal.m[8],horizontal.m[9],0};
    // Reference mesh: -X is right, -Y is forward, +Z is up. Anchor its
    // estimated eye height to the tracked head; do not scale arm geometry
    // independently of the existing hands when world scale changes.
    auto body=CameraMath::Rotation({0,0,0,1});
    for(int j=0;j<3;j++){body.m[j]=float(-right[j]);body.m[4+j]=float(-forward[j]);body.m[8+j]=j==2?1.f:0.f;}
    V headPosition{head.m[12],head.m[13],head.m[14]};
    body=Map(body,{0,0,587.326},headPosition);
    History pending{};
    std::array<Matrix,223> result{};
    for(unsigned i=0;i<count;i++){
        result[i]=palette[i];
        if(used[i]&&!Hand(i)){if(!Finite(palette[i]))return false;result[i]=body;}
    }
    for(unsigned sh:{96u,125u}){
        unsigned el=sh+3,hand=sh+5;bool left=sh==96;
        auto palm=Affine(palette[hand]);if(!NativeHandWeapon::Rigid(palm))return false;
        auto shoulder=Point(r.p[sh],body),wrist=Point(r.p[hand],palm);
        auto restUpper=Sub(r.p[el],r.p[sh]),restLower=Sub(r.p[hand],r.p[el]);
        double upper=std::sqrt(Dot(restUpper,restUpper)),lower=std::sqrt(Dot(restLower,restLower));
        auto pole=Add(Add(V{0,0,-1},Scale(right,left?-.4:.4)),Scale(forward,-.25));
        unsigned side=left?0:1; const V* prior=previous&&previous->valid[side]?&previous->bend[side]:nullptr;
        const FoldAnchor* oldFold=previous&&previous->valid[side]?&previous->fold[side]:nullptr;
        Joints joints;if(!Solve(shoulder,wrist,upper,lower,pole,joints,prior,&pending.bend[side],oldFold,&pending.fold[side]))return false;
        pending.valid[side]=true;
        auto a=Sub(joints.elbow,joints.shoulder),b=Sub(wrist,joints.elbow);
        auto normal=Cross(a,b),nativeNormal=Cross(restUpper,restLower);
        if(!Normalize(normal)){normal=Cross(a,pole);if(!Normalize(normal)){normal=Cross(a,forward);if(!Normalize(normal))return false;}}
        if(!Normalize(nativeNormal))return false;
        auto upperR=CameraMath::Multiply(CameraMath::InverseRigid(Frame(restUpper,nativeNormal)),Frame(a,normal));
        auto lowerR=CameraMath::Multiply(CameraMath::InverseRigid(Frame(restLower,nativeNormal)),Frame(b,normal));
        auto upperM=SegmentMap(restUpper,a,upperR,r.p[sh],joints.shoulder);
        // Helpers100/129 follow the forearm; palm and finger entries stay fixed.
        for(unsigned i=0;i<count;i++)if(used[i]&&!Hand(i)){
            if(Descendant(r,i,sh)){
                if(Descendant(r,i,hand)){result[i]=palm;continue;}
                if(Descendant(r,i,el)){
                    result[i]=SegmentMap(restLower,b,lowerR,r.p[el],joints.elbow);
                }else result[i]=upperM;
            }
        }
    }
    for(unsigned i=0;i<count;i++)if(used[i]&&!Hand(i)){
        if(!ValidSkin(result[i]))return false;
        for(unsigned j:{3u,7u,11u,15u})result[i].m[j]=palette[i].m[j];
    }
    for(unsigned i=0;i<count;i++)if(used[i]&&!Hand(i))palette[i]=result[i];
    if(next)*next=pending;
    return true;
}
}
