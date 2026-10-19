#pragma once
#include "ModelHandAlignment.h"

// No learned offset or history: reconstruct the native skin transform from the
// same animation matrix and accumulated bind pivot used by DXHRDC 0x60c200.
namespace NativeHandWeapon {
using namespace ModelHandAlignment;
inline bool Rigid(const Matrix& m){
    if(!Finite(m)||std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])>.001f||std::abs(m.m[15]-1)>.001f)return false;
    for(int i=0;i<3;i++)for(int j=0;j<3;j++){
        double d=0;for(int k=0;k<3;k++)d+=double(m.m[i*4+k])*m.m[j*4+k];
        if(std::abs(d-(i==j?1.:0.))>.02)return false;
    }
    return Dot(Cross({m.m[0],m.m[1],m.m[2]},{m.m[4],m.m[5],m.m[6]}),{m.m[8],m.m[9],m.m[10]})>.98;
}
inline bool Frame(const std::array<Segment,223>& segments,Matrix& frame,V& pivot,unsigned count=223){
    if(!BuildRightFrame(segments,frame,count))return false;
    std::array<V,223> p{};p[0]=segments[0].pivot;
    for(unsigned i=1;i<count;i++)p[i]=Add(p[segments[i].parent],segments[i].pivot);
    pivot=p[130];return true;
}
inline bool Skin(const Matrix& bone,V pivot,Matrix& skin){
    auto b=Affine(bone);if(!Rigid(b))return false;
    auto s=b;
    for(int j=0;j<3;j++){
        double v=b.m[12+j];for(int k=0;k<3;k++)v-=pivot[k]*b.m[k*4+j];
        s.m[12+j]=float(v);
    }
    if(!Rigid(s))return false;skin=s;return true;
}
inline bool Delta(const Matrix& skin,const Matrix& frame,const Matrix& target,Matrix& delta){
    auto palm=CameraMath::Multiply(frame,Affine(skin));Matrix inverse{};
    if(!Rigid(palm)||!Rigid(target)||!Invert(palm,inverse))return false;
    auto d=CameraMath::Multiply(inverse,target);if(!Rigid(d))return false;delta=d;return true;
}
// Reproduce the model-table lookup in 0x60c9b0 / 0x607480 using bounded reads,
// never calling a virtual method or keeping a borrowed pointer between frames.
template<class Reader> bool OwnerModel(uintptr_t owner,Reader read,uint32_t& model,uint32_t& bones){
    uint32_t object{},table{},nested{};int8_t index{};
    if(!owner || !read(owner+0x21c,&object,4)||!object ||
       !read(owner+0x220,&index,1)||index<0||index>31 ||
       !read(owner+0xb0,&bones,4)||!bones || !read(object+0x5c,&table,4))return false;
    if(!table){
        if(!read(object+0x6c,&nested,4))return false;
        if(!nested && !read(object+8,&nested,4))return false;
        if(!nested || !read(nested+0x40,&table,4))return false;
    }
    return table && read(table+uint32_t(index)*4,&model,4) && model;
}
inline float Difference(const Matrix& a,const Matrix& b){
    float e=0;for(int i=0;i<16;i++)e=(std::max)(e,std::abs(a.m[i]-b.m[i]));return e;
}
}
