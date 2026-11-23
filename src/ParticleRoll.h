#pragma once
#include <cmath>
#include <cstring>

namespace ParticleRoll {
// Native particle-job params: +0x90 is the scene viewport's right/up/forward
// matrix. +0x10/+0x50 are separate position/projection transforms. Z is up.
// Change only the two billboard axes, retaining forward, position and w lanes.
inline bool Level(float* basis) {
    for(int i=0;i<12;i++)if(!std::isfinite(basis[i]))return false;
    for(int row=0;row<3;row++) {
        float n=0;for(int j=0;j<3;j++)n+=basis[row*4+j]*basis[row*4+j];
        if(std::abs(n-1.f)>.02f)return false;
        for(int other=0;other<row;other++) {
            float d=0;for(int j=0;j<3;j++)d+=basis[row*4+j]*basis[other*4+j];
            if(std::abs(d)>.02f)return false;
        }
    }
    float x=basis[8],y=basis[9],z=basis[10],h=std::hypot(x,y);
    // Heading becomes undefined at vertical view. Keep native axes there.
    if(h<.1f)return false;
    float right[3]={y/h,-x/h,0};
    float up[3]={-x*z/h,-y*z/h,h};
    // Require native viewport handedness, not a player-camera down axis.
    float cross[3]={basis[1]*basis[10]-basis[2]*basis[9],
        basis[2]*basis[8]-basis[0]*basis[10],basis[0]*basis[9]-basis[1]*basis[8]};
    float handed=0;for(int j=0;j<3;j++)handed+=cross[j]*basis[4+j];
    if(handed<.98f)return false;
    std::memcpy(basis,right,sizeof(right));std::memcpy(basis+4,up,sizeof(up));
    return true;
}
// For the log: the camera's right axis leaves level by more than about 5 degrees.
inline bool Tilted(const float* basis){return std::abs(basis[2])>.087f;}
// For the log: the largest difference between the job's axes and the player camera,
// in the viewport's layout (its second row is the camera's, negated).
inline void MatchGap(const float* viewport,const float* player,float& rotation,float& position) {
    rotation=position=0;
    for(int i=0;i<16;i++) {
        const float value=(i>=4&&i<7)?-player[i]:player[i];
        const float gap=std::abs(viewport[i]-value);
        if(i>=12){if(gap>position)position=gap;}
        else if(gap>rotation)rotation=gap;
    }
}
}
