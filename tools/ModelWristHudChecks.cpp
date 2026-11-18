#include "ModelWristHud.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool b,const char* s){if(!b){printf("FAIL %s\n",s);exit(1);}}
static float Dot(CameraMath::Vector a,CameraMath::Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
using ModelWristHud::Piece;using ModelWristHud::Add;using ModelWristHud::Scale;using ModelWristHud::Cross;
static CameraMath::Vector Sub(CameraMath::Vector a,CameraMath::Vector b){return Add(a,Scale(b,-1));}
int main(){
    Transport::Tracking t{};t.head.orientation.w=1;
    for(auto& e:t.eyes){e.pose.orientation.w=1;e.left=-.7f;e.right=.8f;e.up=.75f;e.down=-.75f;}
    t.eyes[0].pose.position.x=-.032f;t.eyes[1].pose.position.x=.032f;
    Transport::Pose grip{};grip.orientation.w=1;grip.position={0,0,-.5f};
    // Wrapped round a cuff: health and energy (back of the left, unchanged), the
    // aug cross (back of the right) and Mark & Track (palm side of the left),
    // November 15.
    for(Piece cuffPiece:{Piece::HealthEnergy,Piece::AugCross,Piece::MarkTrack})for(float scale:{100.f,300.f,500.f}){
        CameraMath::BandFrame f[2];
        for(unsigned eye=0;eye<2;eye++){
            Check(ModelWristHud::Frame(t,eye,grip,cuffPiece,scale,0,0,f[eye]),"valid cuff frame");
            const auto& b=f[eye];
            Check(std::abs(Dot(b.back,b.up))<1e-5f&&std::abs(Dot(b.back,b.across))<1e-5f,"orthogonal cuff axes");
            Check(std::abs(Dot(b.back,b.back)-1)<1e-5f,"unit cuff normal");
            Check(b.axis.y*scale>20 && b.axis.y*scale<27,"center inside measured cuff extent, not beyond wrist");
            Check(std::abs(b.radius*scale-10.643747f)<1e-4f,"radius follows mesh when world scale changes");
            CameraMath::Vector surface{};
            Check(CameraMath::BandPoint(b,0,0,surface),"band center available");
            auto d=Sub(surface,b.axis);
            Check(std::abs(Dot(d,d)-b.radius*b.radius)<1e-6f,"band stays on cuff cylinder");
        }
        Check(std::abs(f[0].axis.x-f[1].axis.x)<1e-7f&&std::abs(f[0].back.z-f[1].back.z)<1e-7f,"both eyes share one physical band");
    }
    CameraMath::BandFrame health;
    Check(ModelWristHud::Frame(t,0,grip,Piece::HealthEnergy,300,0,0,health),"health frame");
    {
        // Mark's choices (November 15): aug cross on the back of the RIGHT cuff,
        // Mark & Track on the PALM side of the left cuff.
        CameraMath::BandFrame aug,mt;
        Check(ModelWristHud::Frame(t,0,grip,Piece::AugCross,300,0,0,aug) && ModelWristHud::Frame(t,0,grip,Piece::MarkTrack,300,0,0,mt),"aug and Mark & Track frames");
        // Same grip pose for both hands here, so the cuff centres differ only by
        // the mirrored x: the right cuff's is -0.699 units, the left's +0.699.
        Check(std::abs(aug.axis.x*300+.698768f)<1e-3f && std::abs(health.axis.x*300-.698768f)<1e-3f,"aug cross on the right cuff, health on the left");
        Check(std::abs(mt.axis.x-health.axis.x)<1e-7f && std::abs(mt.axis.y-health.axis.y)<1e-7f,"Mark & Track on the left cuff");
        Check(aug.back.x>.99f && health.back.x<-.99f,"each band on the back of its own hand (+x right, -x left)");
        Check(Dot(health.back,mt.back)<-.999f,"Mark & Track faces the opposite way to health: palm side");
        Check(std::abs(Dot(health.up,mt.up)-1)<1e-5f && aug.up.y<-.95f && health.up.y<-.95f,"all upright toward the hand");
        const auto hc=Cross(health.across,health.up),gc=Cross(aug.across,aug.up),mc=Cross(mt.across,mt.up);
        Check(Dot(hc,health.back)*Dot(gc,aug.back)>0 && Dot(hc,health.back)*Dot(mc,mt.back)>0,"neither new band is mirrored");
        Check(std::abs(aug.radius-health.radius)<1e-9f && std::abs(aug.metresX-health.metresX)<1e-9f && std::abs(mt.metresY-health.metresY)<1e-9f,
              "same cuff radius and size per HUD unit as health and energy");
    }

    // The wrist ends (Mark, November 15). Pose: either hand pointing forward
    // and level, thumb up: the grip's wristward axis (+Y) turned to world +Z
    // (back toward the viewer), so the wrist's end faces the head.
    const Transport::Quaternion pointing{.70710678f,0,0,.70710678f},turned{-.70710678f,0,0,.70710678f};
    const float perX=.0012f,perY=-.0012f; // signs as on screen: layout Y runs down
    const float midX=22900,midY=12700;
    // The measured right wrist end (grip units): the rim's fitted circle is
    // centred on the cuff axis, 4.60 along it (revision 3; revisions 1-2 were
    // 0.48 back and 1.08 toward the thumb). Its mirror for the left.
    const CameraMath::Vector rc{-.698768f,23.029006f,1.906744f},ra{-.110770f,.976536f,.184683f},rb{.993846f,.108841f,.020584f};
    const auto rightEnd=Add(rc,Scale(ra,4.60f));
    const CameraMath::Vector leftEnd{-rightEnd.x,rightEnd.y,rightEnd.z};
    // Points of each artwork that must fit (stage px from its centre): the
    // ammo and consumable boxes' corners; the radar's visible parts (map box
    // corners, ALARMED label's top corners, dotted line's lower corners) from
    // (172.5, 597).
    struct Point { float x,y; };
    const Point ammoPts[4]={{-76.15f,-25.05f},{76.15f,-25.05f},{-76.15f,25.05f},{76.15f,25.05f}};
    const Point consumablePts[4]={{-45.2f,-34.35f},{45.2f,-34.35f},{-45.2f,34.35f},{45.2f,34.35f}};
    const Point radarPts[8]={{98-172.5f,548-597.f},{247-172.5f,548-597.f},{98-172.5f,656-597.f},{247-172.5f,656-597.f},
                             {106-172.5f,527-597.f},{190-172.5f,527-597.f},{105-172.5f,667-597.f},{176-172.5f,667-597.f}};
    for(float scale:{100.f,300.f,500.f}){
        struct Expect { Piece piece; bool left; float perPixel,halfH,up,fit,limit; const Point* pts; int count; };
        const Expect pieces[3]={
            {Piece::Ammo,false,.1094f,50.1f/2,1.16f,9.2f,9.83f,ammoPts,4},
            {Piece::Consumable,false,.0821f,68.7f/2,-5.60f,9.2f,9.83f,consumablePts,4},
            {Piece::Radar,true,.0941f,0,0.f,9.15f,9.2f,radarPts,8}};
        CameraMath::BandFrame frames[3];CameraMath::Vector centres[3];
        for(int k=0;k<3;++k){
            const auto& e=pieces[k];
            Transport::Pose hand{};hand.orientation=pointing;hand.position={e.left?-.15f:.15f,-.25f,-.45f};
            CameraMath::BandFrame f[2];
            for(unsigned eye=0;eye<2;eye++)Check(ModelWristHud::Frame(t,eye,hand,e.piece,scale,midX,midY,f[eye],perX,perY,0),"wrist-end frame");
            const auto& b=f[0];frames[k]=b;
            Check(std::abs(f[0].axis.x-f[1].axis.x)<1e-6f&&std::abs(f[0].back.z-f[1].back.z)<1e-7f,"both eyes share one physical face");
            // The measured wrist axis is about 12 degrees off the grip's Y, so 0.977, not 1.
            Check(b.back.z<-.97f,"face looks back along the pointing arm, toward the head");
            Check(b.up.y>.95f,"upright: top toward the thumb, which is up with either hand pointing");
            Check(b.across.x>.95f,"reads left to right, to the viewer's right");
            const auto hc=Cross(health.across,health.up),ac=Cross(b.across,b.up);
            Check(Dot(hc,health.back)*Dot(ac,b.back)>0,"not mirrored");
            // The piece's centre: its wrist end, moved toward the thumb by `up`.
            const auto end=e.left?leftEnd:rightEnd;
            const auto world=Add(hand.position,CameraMath::Rotate(hand.orientation,Scale(end,1/scale)));
            const auto expectEnd=CameraMath::HeadDirection(t,world);
            CameraMath::Vector centre{};
            Check(CameraMath::BandPoint(b,midX,midY,centre),"face middle available");
            centres[k]=centre;
            const auto shift=Sub(centre,expectEnd);
            Check(std::abs(Dot(shift,b.back))<1e-5f && std::abs(Dot(shift,b.across))<1e-5f && std::abs(Dot(shift,b.up)-e.up/scale)<1e-5f,
                  "piece centred on the measured wrist end, shifted only toward the thumb");
            Check(CameraMath::BandFacing(b,midX,midY)>.2f,"shown when pointing");
            // Size: one scale across and down, the solved units per stage pixel.
            CameraMath::Vector right{},down{};
            Check(CameraMath::BandPoint(b,midX+20,midY,right)&&CameraMath::BandPoint(b,midX,midY+20,down),"face points");
            const auto dr=Sub(right,centre),dd=Sub(down,centre);
            const float want=e.perPixel/scale,lr=std::sqrt(Dot(dr,dr)),ld=std::sqrt(Dot(dd,dd));
            // Within 2 micrometres: single-precision rounding on the 10 m flat-face cylinder.
            Check(std::abs(lr-want)<2e-6f && std::abs(ld-want)<2e-6f && std::abs(Dot(dr,dd))<1e-3f*lr*ld,
                  "same size across and down at the solved scale: not stretched");
            Check(Dot(dd,b.up)<0,"layout down goes away from the thumb");
            // The artwork's extreme points: on the face, shown, within the fit
            // circle round the wrist end, and inside the ring's limit; at
            // least one of them reaches the fit circle (the space is used).
            float reach=0;
            for(int i=0;i<e.count;++i){
                const float lx=midX+e.pts[i].x*20,ly=midY+e.pts[i].y*20;
                CameraMath::Vector c{};
                Check(CameraMath::BandPoint(b,lx,ly,c),"corner placed");
                const auto d=Sub(c,expectEnd);
                const float inPlane=std::sqrt(Dot(d,b.across)*Dot(d,b.across)+Dot(d,b.up)*Dot(d,b.up));
                reach=inPlane>reach?inPlane:reach;
                Check(inPlane<=e.fit/scale*1.002f,"artwork inside the fit circle");
                Check(inPlane<e.limit/scale,"artwork inside the ring");
                Check(std::abs(Dot(Sub(c,centre),b.back))<.005f*e.fit/scale,"flat: corners on the face's plane");
                Check(CameraMath::BandFacing(b,lx,ly)>0,"whole face shown");
            }
            Check(reach>e.fit/scale*.99f,"artwork fills out to the fit circle");
            // Turned round (the wrist end facing away): hidden by the shader.
            Transport::Pose away=hand;away.orientation=turned;
            CameraMath::BandFrame a;
            Check(ModelWristHud::Frame(t,0,away,e.piece,scale,midX,midY,a,perX,perY,0),"turned frame");
            Check(CameraMath::BandFacing(a,midX,midY)<0,"hidden when the end faces away");
        }
        // Weapon/ammo first (Mark: "I want the weapon/ammo count to take
        // priority"): larger than the consumable, 40% larger than the first
        // wrist-end build and larger than revision 2; the two apart by 1.2 units.
        Check(pieces[0].perPixel>pieces[1].perPixel && pieces[0].perPixel>=1.4f*.0773f && pieces[0].perPixel>.1007f,"ammo has size priority");
        Check(pieces[2].perPixel>1.1f*.0832f,"radar larger than revision 2");
        const auto& ammo=frames[0];
        const float ammoBottom=Dot(Sub(centres[0],centres[1]),ammo.up)-pieces[0].halfH*pieces[0].perPixel/scale-pieces[1].halfH*pieces[1].perPixel/scale;
        Check(ammoBottom>1.19f/scale && ammoBottom<1.21f/scale,"1.2-unit gap between ammo and consumable");
        Check(std::abs(Dot(Sub(centres[0],centres[1]),ammo.across))<1e-6f,"ammo straight above the consumable");
    }
    CameraMath::BandFrame unused;
    Check(!ModelWristHud::Frame(t,0,grip,Piece::HealthEnergy,0,0,0,unused),"bad scale rejected");
    grip.position.z=.5f;
    Check(!ModelWristHud::Frame(t,0,grip,Piece::HealthEnergy,300,0,0,unused),"behind head stays hidden");
    Check(!ModelWristHud::Frame(t,0,grip,Piece::Radar,300,0,0,unused),"wrist end behind head stays hidden");
    puts("PASS measured cuff extent and health band; wrist ends centred on the rim's fitted circle: ammo above consumable (ammo larger, 1.2 apart), radar on the left end; upright, filling and inside the ring, hidden when turned away; world scale, stereo and hidden placement");
}
