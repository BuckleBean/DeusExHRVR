// Offline checks for the grenade landing marker's pure parts (no game, no
// headset): hold-to-throw input, the stepped path, launch velocity, the
// world-to-headset conversion against CameraMath::HeadWorld, the ring texture
// and its flat orientation, and the transport layout. Runs as 32- and 64-bit.
#include "ThrowAim.h"
#include "ThrowMarker.h"
#include "CameraMath.h"
#include "SharedPair.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace {
void Check(bool ok,const char* why){if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
bool Near(float a,float b,float e){return std::fabs(a-b)<=e;}
}

int main() {
    // 1. Hold to aim, release to throw.
    {
        // Tap throws (RightATap=RB): yesterday's layout.
        ThrowAim::HoldThrow h;
        auto r=h.Update(true,1000,300,true);Check(!r.aiming && !r.throwPulse,"press: not aiming yet");
        r=h.Update(true,1200,300,true);Check(!r.aiming,"200 ms: still a tap");
        r=h.Update(false,1250,300,true);Check(!r.aiming && r.throwPulse && r.heldMs==250,"tap release throws once, held 250 ms");
        r=h.Update(false,1360,300,true);Check(r.throwPulse,"pulse lasts 120 ms");
        r=h.Update(false,1371,300,true);Check(!r.throwPulse,"pulse ends");
        r=h.Update(true,2000,300,true);r=h.Update(true,2300,300,true);Check(r.aiming && !r.throwPulse,"held 300 ms: aiming, no throw yet");
        r=h.Update(true,4000,300,true);Check(r.aiming,"still aiming while held");
        r=h.Update(false,4010,300,true);Check(!r.aiming && r.throwPulse && r.heldMs==2010,"long hold release throws");
        // Tap does nothing (RightA=RB with a hold delay): Mark's layout since Nov 15.
        ThrowAim::HoldThrow m;
        r=m.Update(true,1000,300,false);r=m.Update(false,1250,300,false);
        Check(!r.throwPulse && r.heldMs==0,"tap before the hold delay does not throw");
        r=m.Update(false,1300,300,false);Check(!r.throwPulse,"and nothing follows");
        r=m.Update(true,2000,300,false);r=m.Update(true,2299,300,false);Check(!r.aiming,"299 ms: no ring yet");
        r=m.Update(true,2300,300,false);Check(r.aiming && !r.throwPulse,"300 ms: ring, no throw yet");
        r=m.Update(false,2900,300,false);Check(r.throwPulse && r.heldMs==900,"release after the delay throws");
        ThrowAim::HoldThrow d;r=d.Update(true,10,0,false);Check(r.aiming,"no hold delay: aiming at once");
        r=d.Update(false,15,0,false);Check(r.throwPulse,"no hold delay: any release throws");
        d.Reset();r=d.Update(false,20,0,false);Check(!r.throwPulse,"reset drops a held press without throwing");
        std::printf("PASS hold to aim: a tap throws only where the layout says so (RightATap=RB), a hold past the delay aims and throws on release, reset is silent\n");
    }
    // 2. Path against a flat floor at z=0, same t loop and gravity as 0x75C370.
    {
        alignas(16) float start[4]{0,0,100,1},v[4]{10,0,0,0},landing[3]{};
        unsigned segments=0,calls=0;
        auto floor=[&](const float* p0,const float* p1,float* hit) {
            ++calls;
            if(p1[2]>0)return false;
            float f=p0[2]/(p0[2]-p1[2]);
            for(int i=0;i<3;i++)hit[i]=p0[i]+(p1[i]-p0[i])*f;
            return true;
        };
        Check(ThrowAim::PredictLanding(start,v,floor,landing,&segments),"floor is hit");
        // Exact contact: 100 = 1.72 t^2 -> t = 7.625, x = 76.25. Chords between
        // t=5 and t=10 land a little short of it.
        Check(segments==2 && calls==2,"second segment (t 5..10) reaches the floor");
        Check(landing[0]>70 && landing[0]<77 && Near(landing[2],0,1e-3f),"contact near the exact landing");
        alignas(16) float up[4]{0,0,0,0};
        auto never=[](const float*,const float*,float*){return false;};
        unsigned all=0;Check(!ThrowAim::PredictLanding(start,up,never,landing,&all) && all==60,"no contact: 60 segments to t=300, no marker");
        std::printf("PASS path: segments of 5 to t=300, gravity -3.44, first contact returned\n");
    }
    // 3. Launch velocity from a level aim frame (right, down, forward rows).
    {
        float frame[16]{0,-1,0,0, 0,0,-1,0, 1,0,0,0, 0,0,0,1};
        float v[4];ThrowAim::LaunchVelocity(frame,30,2,v);
        Check(Near(v[0],2*std::cos(0.5235988f),1e-4f) && Near(v[1],0,1e-6f) && Near(v[2],2*std::sin(0.5235988f),1e-4f),"30 degrees tilts forward toward up");
        ThrowAim::LaunchVelocity(frame,0,3,v);Check(Near(v[0],3,1e-6f) && Near(v[2],0,1e-6f),"0 degrees follows the aim");
        std::printf("PASS launch velocity: aim forward tilted up by the grenade's angle, times its speed\n");
    }
    // 4. World -> headset position inverts CameraMath::HeadWorld for points.
    {
        using namespace CameraMath;
        Matrix game{{0.8f,-0.6f,0,0, 0,0,-1,0, 0.6f,0.8f,0,0, 1200,-340,85,1}};
        Pose reference{{0,0.258819f,0,0.9659258f},{0.1f,1.6f,-0.2f}};
        Pose head{{0.0436f,0.3420f,-0.0151f,0.9386f},{0.35f,1.55f,0.10f}};
        float n=std::sqrt(head.orientation.x*head.orientation.x+head.orientation.y*head.orientation.y+head.orientation.z*head.orientation.z+head.orientation.w*head.orientation.w);
        head.orientation={head.orientation.x/n,head.orientation.y/n,head.orientation.z/n,head.orientation.w/n};
        const float scale=300;
        auto headWorld=HeadWorld(game,reference,head,scale);
        Vector targets[]={{0.35f,1.55f,0.10f},{2.0f,-0.3f,-5.0f},{-3.5f,0.2f,1.25f}};
        for(auto x:targets) {
            Pose at{head.orientation,x};
            auto world=HeadWorld(game,reference,at,scale);
            auto back=ThrowAim::WorldToTracking(world.m+12,headWorld.m,{head.position.x,head.position.y,head.position.z},
                {head.orientation.x,head.orientation.y,head.orientation.z,head.orientation.w},scale);
            Check(Near(back.x,x.x,2e-3f) && Near(back.y,x.y,2e-3f) && Near(back.z,x.z,2e-3f),"round trip through HeadWorld");
        }
        std::printf("PASS world to headset: three points round-trip through HeadWorld within 2 mm\n");
    }
    // 5. Ring texture and flat orientation.
    {
        auto p=ThrowMarker::Pixels();
        Check(p.size()==ThrowMarker::Size*ThrowMarker::Size,"texture size");
        auto alpha=[&](uint32_t x,uint32_t y){return p[y*ThrowMarker::Size+x]>>24;};
        const uint32_t c=ThrowMarker::Size/2;
        Check(alpha(0,0)==0 && alpha(ThrowMarker::Size-1,0)==0,"corners transparent");
        Check(alpha(c,c)>200,"centre dot");
        Check(alpha(c+uint32_t(0.82f*(c-0.5f)),c)>240,"ring opaque at 82% radius");
        Check(alpha(c+uint32_t(0.5f*(c-0.5f)),c)==0,"clear between dot and ring");
        bool premultiplied=true;for(auto v:p){uint32_t a=v>>24;if((v&0xff)>a||((v>>8)&0xff)>a||((v>>16)&0xff)>a)premultiplied=false;}
        Check(premultiplied,"colour premultiplied by alpha");
        auto o=ThrowMarker::Flat();
        auto n=CameraMath::Rotate({o.x,o.y,o.z,o.w},{0,0,1});
        Check(Near(n.x,0,1e-5f) && Near(n.y,1,1e-5f) && Near(n.z,0,1e-5f),"quad normal points up");
        std::printf("PASS ring texture (transparent corners, dot, ring, premultiplied) and flat quad facing up\n");
    }
    // 6. Surface facing from two parallel probe segments, and the ring turned to it.
    {
        // A plane through q with normal m (unit): segment p0->p1 meets it where the sign changes.
        auto plane=[](float qx,float qy,float qz,float mx,float my,float mz) {
            return [=](const float* p0,const float* p1,float* hit) {
                float a=(p0[0]-qx)*mx+(p0[1]-qy)*my+(p0[2]-qz)*mz,b=(p1[0]-qx)*mx+(p1[1]-qy)*my+(p1[2]-qz)*mz;
                if(a*b>0||a==b)return false;
                float f=a/(a-b);for(int i=0;i<3;i++)hit[i]=p0[i]+(p1[i]-p0[i])*f;return true;
            };
        };
        auto surface=[&](auto collide,const float* start,const float* v,float* n,float* landing) {
            alignas(16) float from[4],to[4];
            Check(ThrowAim::PredictLanding(start,v,collide,landing,nullptr,from,to),"path reaches the surface");
            return ThrowAim::SurfaceNormal(from,to,landing,collide,n);
        };
        alignas(16) float start[4]{0,0,100,1},v[4]{10,0,0,0};float n[3],landing[3];
        Check(surface(plane(0,0,0,0,0,1),start,v,n,landing) && Near(n[2],1,1e-4f),"floor: facing up");
        Check(surface(plane(60,0,0,1,0,0),start,v,n,landing) && Near(n[0],-1,1e-4f) && Near(landing[0],60,1e-3f),"wall ahead: facing back at the thrower");
        const float s=0.70710678f;
        Check(surface(plane(80,0,0,-s,0,s),start,v,n,landing) && Near(n[0],-s,1e-3f) && Near(n[2],s,1e-3f),"45-degree slope: tilted to match");
        // A wall that ends right at the path: one probe misses, so no facing (ring lies flat).
        auto edge=[](const float* p0,const float* p1,float* hit) {
            float a=p0[0]-60,b=p1[0]-60;if(a*b>0||a==b)return false;
            float f=a/(a-b);for(int i=0;i<3;i++)hit[i]=p0[i]+(p1[i]-p0[i])*f;
            return hit[1]>-1.f; // wall only for y > -1: the probe offset to -y misses
        };
        alignas(16) float level[4]{0,0,100,1},flat[4]{10,0,0,0};
        alignas(16) float from[4],to[4];
        Check(ThrowAim::PredictLanding(level,flat,edge,landing,nullptr,from,to),"edge: first contact found");
        // The path descends along +x, so one probe runs 8 units toward -y: it misses this wall.
        Check(!ThrowAim::SurfaceNormal(from,to,landing,edge,n),"edge: a probe off the wall gives no facing (ring lies flat)");
        // A probe hitting a different, far surface is refused.
        int calls=0;
        auto distant=[&](const float* p0,const float* p1,float* hit){++calls;
            if(!(p0[0]<60&&p1[0]>=60))return false;
            for(int i=0;i<3;i++)hit[i]=p0[i];hit[0]=calls==1?60.f:5000.f;return true;};
        alignas(16) float a[4]{50,0,50,1},b2[4]{70,0,50,1},c[3]{60,0,50};
        Check(!ThrowAim::SurfaceNormal(a,b2,c,distant,n),"probe on another surface far away: refused");
        // The ring's turn: face (+Z) to the normal; floor equals the flat ring.
        auto f=ThrowMarker::Facing(0,1,0),g=ThrowMarker::Flat();
        Check(Near(f.x,g.x,1e-5f)&&Near(f.y,g.y,1e-5f)&&Near(f.z,g.z,1e-5f)&&Near(f.w,g.w,1e-5f),"floor facing is the flat ring");
        float targets[][3]{{1,0,0},{0,0,1},{-0.6f,0.8f,0},{0,-1,0},{0,0,-1},{0.3f,0.4f,-0.866f}};
        for(auto& t:targets) {
            auto q=ThrowMarker::Facing(t[0],t[1],t[2]);
            auto r=CameraMath::Rotate({q.x,q.y,q.z,q.w},{0,0,1});
            float l=std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
            Check(Near(r.x,t[0]/l,1e-4f)&&Near(r.y,t[1]/l,1e-4f)&&Near(r.z,t[2]/l,1e-4f),"ring face turned to the surface normal");
        }
        std::printf("PASS surface facing: floor up, wall toward the thrower, slope tilted, edge and far probe refused; ring face turned to six normals\n");
    }
    // 7. Directions convert with the same head mapping as points.
    {
        using namespace CameraMath;
        Matrix game{{0.8f,-0.6f,0,0, 0,0,-1,0, 0.6f,0.8f,0,0, 1200,-340,85,1}};
        Pose reference{{0,0.258819f,0,0.9659258f},{0.1f,1.6f,-0.2f}};
        Pose head{{0.0436f,0.3420f,-0.0151f,0.9386f},{0.35f,1.55f,0.10f}};
        float n=std::sqrt(head.orientation.x*head.orientation.x+head.orientation.y*head.orientation.y+head.orientation.z*head.orientation.z+head.orientation.w*head.orientation.w);
        head.orientation={head.orientation.x/n,head.orientation.y/n,head.orientation.z/n,head.orientation.w/n};
        auto headWorld=HeadWorld(game,reference,head,300);
        Vector A{1,0.5f,-2},B{1.6f,1.3f,-3.1f};
        auto wa=HeadWorld(game,reference,{head.orientation,A},300),wb=HeadWorld(game,reference,{head.orientation,B},300);
        float d[3];for(int i=0;i<3;i++)d[i]=wb.m[12+i]-wa.m[12+i];
        auto x=ThrowAim::DirectionToTracking(d,headWorld.m,{head.orientation.x,head.orientation.y,head.orientation.z,head.orientation.w});
        float ex=B.x-A.x,ey=B.y-A.y,ez=B.z-A.z,el=std::sqrt(ex*ex+ey*ey+ez*ez),xl=std::sqrt(x.x*x.x+x.y*x.y+x.z*x.z);
        Check(Near(x.x/xl,ex/el,1e-4f)&&Near(x.y/xl,ey/el,1e-4f)&&Near(x.z/xl,ez/el,1e-4f),"direction round trip");
        std::printf("PASS world direction to headset direction matches the point mapping\n");
    }
    // 8. Transport 22 added the cover target; 23 adds the hand HUD setting to the
    // menu state at the END of the header, so RenderInfo and its offsets are unchanged.
    static_assert(Transport::Version==26);
    static_assert(sizeof(Transport::MenuState)==424 && offsetof(Transport::MenuState,handHud)==372 && offsetof(Transport::MenuState,throwAim)==388 && offsetof(Transport::Header,menu)==712);
    static_assert(sizeof(Transport::RenderInfo)==344 && sizeof(Transport::Header)==1136);
    static_assert(offsetof(Transport::RenderInfo,markerPosition)==296 && offsetof(Transport::RenderInfo,markerState)==308 &&
                  offsetof(Transport::RenderInfo,markerNormal)==312);
    static_assert(offsetof(Transport::RenderInfo,coverPosition)==328 && offsetof(Transport::RenderInfo,coverState)==340);
    std::printf("PASS %zu-bit transport 24: RenderInfo 344 bytes, marker at 296, surface at 312, cover target at 328, menu hand HUD at 372, menu tabs settings at 388, header 1136\n",sizeof(void*)*8);
    return 0;
}
