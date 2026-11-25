#pragma once
#include "CameraMath.h"
#include <algorithm>

namespace CarryAttachment {
using CameraMath::Matrix;
using CameraMath::Quaternion;
inline bool Rigid(const Matrix& m) {
    for(float x:m.m)if(!std::isfinite(x))return false;
    for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
        float dot=0;for(int k=0;k<3;k++)dot+=m.m[i*4+k]*m.m[j*4+k];
        if(std::abs(dot-(i==j?1.f:0.f))>.01f)return false;
    }
    const float det=m.m[0]*(m.m[5]*m.m[10]-m.m[6]*m.m[9])-m.m[1]*(m.m[4]*m.m[10]-m.m[6]*m.m[8])+m.m[2]*(m.m[4]*m.m[9]-m.m[5]*m.m[8]);
    return std::abs(det-1)<.01f;
}
inline bool Quat(const Matrix& m,float* q) {
    if(!Rigid(m))return false;
    // CameraMath uses row-vector matrices; transpose for the usual conversion.
    float a[3][3];for(int i=0;i<3;i++)for(int j=0;j<3;j++)a[i][j]=m.m[j*4+i];
    const float trace=a[0][0]+a[1][1]+a[2][2];
    if(trace>0) {
        const float s=std::sqrt(trace+1)*2;
        q[3]=s*.25f;q[0]=(a[2][1]-a[1][2])/s;q[1]=(a[0][2]-a[2][0])/s;q[2]=(a[1][0]-a[0][1])/s;
    } else {
        int i=0;if(a[1][1]>a[i][i])i=1;if(a[2][2]>a[i][i])i=2;
        const int j=(i+1)%3,k=(i+2)%3;
        const float s=std::sqrt(1+a[i][i]-a[j][j]-a[k][k])*2;
        if(!(s>0))return false;
        q[i]=s*.25f;q[j]=(a[j][i]+a[i][j])/s;q[k]=(a[k][i]+a[i][k])/s;q[3]=(a[k][j]-a[j][k])/s;
    }
    float norm=0;for(int i=0;i<4;i++)norm+=q[i]*q[i];
    if(!std::isfinite(norm)||norm<.9f||norm>1.1f)return false;
    for(int i=0;i<4;i++)q[i]/=std::sqrt(norm);return true;
}
struct Attachment {
    Matrix relative{};float contact[3]{};bool valid{};
    void Reset(){*this={};}
    bool Capture(const Matrix& hand,const Matrix& object,const float* surface,float scale) {
        Reset();if(!Rigid(hand)||!Rigid(object)||!std::isfinite(scale)||scale<=0)return false;
        float d2=0;for(int i=0;i<3;i++) {
            if(!std::isfinite(surface[i]))return false;
            const float d=surface[i]-object.m[12+i];d2+=d*d;
        }
        if(d2>4*scale*scale)return false;
        for(int i=0;i<3;i++)for(int j=0;j<3;j++)contact[i]+=(surface[j]-object.m[12+j])*object.m[i*4+j];
        relative=CameraMath::Multiply(object,CameraMath::InverseRigid(hand));
        relative.m[12]=relative.m[13]=relative.m[14]=0;valid=true;return true;
    }
    bool Target(const Matrix& hand,Matrix& object)const {
        if(!valid||!Rigid(hand))return false;
        object=CameraMath::Multiply(relative,hand);
        for(int j=0;j<3;j++) {
            object.m[12+j]=hand.m[12+j];
            for(int i=0;i<3;i++)object.m[12+j]-=contact[i]*object.m[i*4+j];
        }
        return Rigid(object);
    }
};
}
