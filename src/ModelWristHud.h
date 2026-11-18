#pragma once
#include "CameraMath.h"

// Geometry measured by measure.py from the game's resource87370 and the same
// palm frames used by ModelHandAlignment. Units are game units, not metres.
// This fit targets model-aligned hands with NeutralWrist, not animated arms.
namespace ModelWristHud {
using CameraMath::Vector;
inline Vector Add(Vector a,Vector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector Scale(Vector a,float s){return {a.x*s,a.y*s,a.z*s};}
inline Vector Cross(Vector a,Vector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}

// What sits where (Mark, November 15):
// - the open end of the RIGHT wrist ("that big circle facing the player where
//   the wrist cuts off ... In particular, the ammo stuff because it's in view
//   when pointing"): weapon/ammo above, consumable below. After the first run:
//   "I would like it to be larger and use the space better. The 2 elements are
//   crowded"; "If there's a size conflict, I want the weapon/ammo count to take
//   priority".
// - the open end of the LEFT wrist: the radar ("I also want to move the radar
//   to the same place on the left hand"; after revision 2: "the radar could
//   be a little larger/better centered").
// - health and energy stay on the back of the left cuff.
// - "Let's also get the augs and mark and track added to the wrist": Mark
//   chose the aug shortcut cross on the back of the RIGHT cuff (empty since the
//   ammo moved to the end), and the Mark & Track counter on the PALM side of the
//   left cuff (empty since the radar moved to the end). Both wrapped round the
//   cuff like health and energy, at the same size per HUD unit.
enum class Piece { HealthEnergy, Ammo, Consumable, Radar, AugCross, MarkTrack };
inline bool OnCuff(Piece p){return p==Piece::HealthEnergy || p==Piece::AugCross || p==Piece::MarkTrack;}
inline bool LeftHand(Piece p){return p==Piece::HealthEnergy || p==Piece::Radar || p==Piece::MarkTrack;}

// The right wrist's open end, measured from the hand mesh
// (2026-11-13-hand-hud-fit-candidate/wrist-mesh.npz). Its rim (the outermost
// vertex in each 15-degree sector round the axis) fits a circle of radius 9.16
// units centred ON the cuff's axis (0.02 back, -0.04 across), 4.49-4.71 units
// past the cuff centre (the cut is slightly slanted); the face sits at the
// middle, 4.60. Revisions 1-2 used the plain average of the end-most vertices,
// which lie on one side of the slanted cut: 0.48 back and 1.08 toward the
// thumb, the offset Mark's revision 2 screenshot shows on both hands. The left
// hand is the mirror image (x negated), as the cuff centre is.
inline constexpr float kEndAlong=4.60f,kEndBack=0.f,kEndAcross=0.f;
inline constexpr float kRingInner=9.09f;

// Sizes on the end face, game units per stage pixel (1 px = 20 layout units),
// solved offline (2026-11-15, revision 3) inside a 9.2-unit circle (the rim):
// - Ammo artwork 152.3 x 50.1 px (weaponInfo_mc: icon and counts; plates and
//   the three dots hidden; the overheating bar, heat weapons only, reaches 4 px
//   further up and left), consumable 90.4 x 68.7 px (grenadeInfo_mc), movie
//   87505. Weapon/ammo first (Mark: "If there's a size conflict, I want the
//   weapon/ammo count to take priority"): consumable at 0.75 of the ammo's
//   scale, 1.2 units apart: ammo 0.1094 (9% larger than revision 2, 42% larger
//   than revision 1), consumable 0.0821; centres 1.16 units above and 5.60
//   below the face's middle, toward the thumb.
// - Radar: its visible parts (ALARMED label, map box, dotted line under it;
//   x 98-247, y 527-667 px) reach at most 97.2 px from (172.5, 597): 0.0941
//   (13% larger than revision 2) fits them in 9.15 units. Its alarm timer (to
//   the right, x 238-361) is not fitted: it shows only during an alarm and
//   will reach past the ring.
inline constexpr float kAmmoPerPixel=.1094f,kAmmoUp=1.16f;
inline constexpr float kConsumablePerPixel=.0821f,kConsumableUp=-5.60f;
inline constexpr float kRadarPerPixel=.0941f,kRadarReach=97.2f;
inline constexpr float kAmmoHalfW=152.3f/2,kAmmoHalfH=50.1f/2,kConsumableHalfW=90.4f/2,kConsumableHalfH=68.7f/2;

// Where each piece's artwork centre is relative to what the mod measures in
// play (layout units). The mod measures the range of the piece's draw
// origins; the game file gives both that range and the artwork (movie
// 87505/87498, frame 1). Check: the radar's origin range in the file is
// 76.7 x 137.4 px, in Mark's grip-run log 80.9 x 137.5 px.
// - ammo, consumable: artwork centre minus origin-range centre (ammo: without
//   the hidden dots, which no longer draw; overheating bar absent, as for
//   most weapons).
// - radar: visible-content centre minus the origins' MINIMUM corner (its
//   outline's top left), so the alarm timer and moving blips, which add
//   origins to the right and below, do not shift it.
inline constexpr float kAmmoFromMiddle[2]{29.0f*20,12.9f*20};
inline constexpr float kConsumableFromMiddle[2]{7.4f*20,12.1f*20};
inline constexpr float kRadarFromMin[2]{(172.5f-95.7f)*20,(597.f-524.5f)*20};
// Revision 9 (Mark: the aug cross "is off center ... Feels like we're not
// applying the lessons we've learned"): the cuff pieces are centred on their
// artwork too, from movies 87481 / 87503 the same way.
// - Aug cross: its pills and icons (background hidden) span x 1047.3-1171.0
//   (1167.2 without the icon placeholders) around origins 1047.3-1130.8: the
//   artwork centre is 18.3-20.1 px right of the origin-range centre (19.2
//   used), 0 down.
// - Mark & Track: the visible counter (three dots, label, version, seven
//   triangles; bg_outline_mc hidden) spans x 98-249, y 487-520, centre
//   (173.5, 503.5). Its label's text box reaches x 479 (mostly empty) and an
//   unnamed 64 px part (sprite 33, at x 85-149, y 470-534) may or may not be
//   drawn, so it is anchored on its LARGEST origins, the last triangle's
//   (x 229.6, y 520.2), which neither the label nor that part reaches.
inline constexpr float kAugFromMiddle[2]{19.2f*20,0};
inline constexpr float kMarkTrackFromMax[2]{(173.5f-229.6f)*20,(503.5f-520.2f)*20};

// A flat face drawn with the band shader: a cylinder this wide (metres) is
// flat to within 0.03 mm over the 6 cm disc, so the shader is unchanged.
inline constexpr float kFlatRadius=10.f;

// middleX/Y: the piece's artwork centre (end face) or the band's middle (cuff)
// in the HUD's flat layout units. perUnitX/Y: how far one layout unit moves on
// the game's normal HUD, in its clip units (signed); the end face uses only
// their signs. xShift: added to layout X to bring a parked element home.
inline bool Frame(const Transport::Tracking& t,unsigned eye,const Transport::Pose& grip,
                  Piece piece,float worldScale,float middleX,float middleY,
                  CameraMath::BandFrame& f,float perUnitX=1,float perUnitY=1,float xShift=0) {
    if(!std::isfinite(worldScale)||worldScale<10||worldScale>1000||eye>1)return false;
    const bool left=LeftHand(piece);
    const Vector center{left?.698768f:-.698768f,23.029006f,1.906744f};
    const Vector axis=left?Vector{.106606f,.978570f,.176168f}:Vector{-.110770f,.976536f,.184683f};
    const Vector back=left?Vector{-.994301f,.104919f,.018888f}:Vector{.993846f,.108841f,.020584f};
    const auto across=Cross(back,axis);
    if(!OnCuff(piece)) {
        // A flat face across the wrist's open end, facing out of the wrist
        // (toward you when you point). Upright to the hand: top toward the
        // thumb side (the top of a held gun); left to right toward the back of
        // the right hand / the palm of the left (to your right with either hand
        // pointing, thumb up). Hidden by the shader when the end faces away,
        // like the bands.
        const Vector rightCentre{-.698768f,23.029006f,1.906744f},rightAxis{-.110770f,.976536f,.184683f},rightBack{.993846f,.108841f,.020584f};
        auto end=Add(Add(Add(rightCentre,Scale(rightAxis,kEndAlong)),Scale(rightBack,kEndBack)),Scale(Cross(rightBack,rightAxis),kEndAcross));
        if(left)end.x=-end.x;
        const auto thumb=left?across:Scale(across,-1);
        const auto reading=left?Scale(back,-1):back;
        const float perPixel=piece==Piece::Ammo?kAmmoPerPixel:piece==Piece::Consumable?kConsumablePerPixel:kRadarPerPixel;
        const float up=piece==Piece::Ammo?kAmmoUp:piece==Piece::Consumable?kConsumableUp:0.f;
        const auto centre=Add(end,Scale(thumb,up));
        const auto p=Add(grip.position,CameraMath::Rotate(grip.orientation,Scale(centre,1/worldScale)));
        const auto normal=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,axis));
        f.back=normal;
        f.across=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,reading));
        f.up=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,thumb));
        const auto face=CameraMath::HeadDirection(t,{p.x-t.head.position.x,p.y-t.head.position.y,p.z-t.head.position.z});
        f.radius=kFlatRadius;
        f.axis=Add(face,Scale(normal,-kFlatRadius));
        // One size for X and Y (no stretching); the signs keep the game's
        // layout directions (layout Y runs down the screen).
        const float metres=perPixel/20/worldScale;
        f.metresX=std::copysign(metres,perUnitX);f.metresY=std::copysign(metres,perUnitY);
        f.middleX=middleX;f.middleY=middleY;f.maxTheta=.01f;f.xShift=xShift;
        const float distance2=face.x*face.x+face.y*face.y+face.z*face.z;
        if(!std::isfinite(distance2)||distance2<.15f*.15f||face.z<=.01f)return false;
        CameraMath::Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
        f.eyeProjection=CameraMath::EyeProjection(depth,t,eye,1);
        return true;
    }
    // Wrapped round the cuff (Mark, November 13), fixed to it: looking around
    // never rotates the layout. Health and energy on the back of the left cuff,
    // the aug cross on the back of the right: both read across the back of the
    // hand, upright toward the fingers (on either hand back x across points
    // out of the back, so neither is mirrored). Mark & Track on the palm side
    // of the left cuff, read with the palm toward you: the back band's
    // direction reversed (as the radar's palm band was, November 13).
    const bool palm=piece==Piece::MarkTrack;
    const auto offset=CameraMath::Rotate(grip.orientation,Scale(center,1/worldScale));
    const auto p=Add(grip.position,offset);
    f.axis=CameraMath::HeadDirection(t,{p.x-t.head.position.x,p.y-t.head.position.y,p.z-t.head.position.z});
    f.back=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,palm?Scale(back,-1):back));
    f.across=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,palm?Scale(across,-1):across));
    f.up=CameraMath::HeadDirection(t,CameraMath::Rotate(grip.orientation,Scale(axis,-1)));
    f.radius=10.643747f/worldScale;
    const float scale=.36f*300/worldScale;
    f.metresX=1.6f*scale*perUnitX;f.metresY=.9f*scale*perUnitY;
    f.middleX=middleX;f.middleY=middleY;f.maxTheta=2.2f;f.xShift=xShift;
    const auto surface=Add(f.axis,Scale(f.back,f.radius));
    const float distance2=surface.x*surface.x+surface.y*surface.y+surface.z*surface.z;
    if(!std::isfinite(distance2)||distance2<.15f*.15f||surface.z<=.01f)return false;
    CameraMath::Matrix depth;depth.m[10]=1;depth.m[14]=-.01f;
    f.eyeProjection=CameraMath::EyeProjection(depth,t,eye,1);
    return true;
}
}
