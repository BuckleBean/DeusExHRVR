// Offline checks of the grip-driven hand pose on the game's saved skeletons.
// Run from the workspace root. No game code runs.
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "SharedPair.h"
#include "CameraMath.h"
#include "ModelHandAlignment.h"
#include "InteractionCurl.h"
using namespace ModelHandAlignment;
static int failures=0;
static void Check(bool ok,const char* name,const char* model=""){printf("%s %s%s%s\n",ok?"PASS":"FAIL",name,*model?" - ":"",model);if(!ok)++failures;}
struct Skeleton {std::array<Segment,223> segments{};std::array<V,223> rest{};unsigned count{};};
static bool Load(const char* path,Skeleton& s) {
    std::ifstream f(path);if(!f)return false;
    std::stringstream b;b<<f.rdbuf();const std::string t=b.str();
    size_t at=0;unsigned i=0;
    while((at=t.find("\"parent\":",at))!=std::string::npos&&i<223) {
        at+=9;while(t[at]==' ')++at;
        s.segments[i].parent=t.compare(at,4,"null")==0?UINT32_MAX:uint32_t(std::stoul(t.substr(at,12)));
        size_t l=t.find("\"local\":",at);if(l==std::string::npos)return false;l=t.find('[',l)+1;
        for(int j=0;j<3;j++){size_t used{};s.segments[i].pivot[j]=std::stod(t.substr(l,40),&used);l+=used;if(j<2)l=t.find(',',l)+1;}
        s.rest[i]=i?Add(s.rest[s.segments[i].parent],s.segments[i].pivot):s.segments[i].pivot;
        at=l;++i;
    }
    s.count=i;return i==223||i==162;
}
static V Apply4(const Matrix& m,V q){V o{};for(int j=0;j<3;j++)o[j]=q[0]*m.m[j]+q[1]*m.m[4+j]+q[2]*m.m[8+j]+m.m[12+j];return o;}
static double Dist(V a,V b){auto d=Sub(a,b);return std::sqrt(Dot(d,d));}
static std::array<Matrix,223> Identity(){std::array<Matrix,223> p{};for(auto& m:p){m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;}return p;}
static V PalmPoint(const Skeleton& s,unsigned palm){return Scale(Add(Add(Add(Add(s.rest[palm],s.rest[palm+1]),s.rest[palm+4]),s.rest[palm+7]),s.rest[palm+10]),.2);}
static void Hand(const Skeleton& s,bool left,const char* name) {
    using namespace InteractionCurl;
    const auto d=Build(s.segments,s.count,left,true);
    Check(d.valid,left?"left hand pose builds":"right hand pose builds",name);if(!d.valid)return;
    const unsigned palm=left?101:130;
    auto base=Identity();for(auto& m:base){m.m[3]=.25f;m.m[7]=.5f;m.m[11]=.75f;}
    auto zero=base;Apply(zero.data(),s.count,d,0);
    Check(!memcmp(zero.data(),base.data(),sizeof(Matrix)*223),"open grip leaves the hand byte-identical",name);
    auto half=base,full=base;
    bool ok=Apply(half.data(),s.count,d,.5f)&&Apply(full.data(),s.count,d,1.f);Check(ok,"pose applies at half and full squeeze",name);if(!ok)return;
    bool others=true,meta=true,rigid=true,connected=true,closer=true;
    for(unsigned i=0;i<223;i++)if((i<=palm||i>palm+15)&&memcmp(&full[i],&base[i],sizeof(Matrix)))others=false;
    for(unsigned i=palm+1;i<=palm+15;i++) {
        for(unsigned j:{3u,7u,11u})if(full[i].m[j]!=base[i].m[j])meta=false;
        if(!NativeHandWeapon::Rigid(Affine(full[i])))rigid=false;
        if(Dist(Apply4(full[i],s.rest[i]),Apply4(full[s.segments[i].parent],s.rest[i]))>1e-2)connected=false;
    }
    const V centre=PalmPoint(s,palm);
    for(unsigned k:{3u,6u,9u,12u}) { // the thumb now rests on the fingers, checked below
        const auto tip=s.rest[palm+k];
        const double r=Dist(tip,centre),h=Dist(Apply4(half[palm+k],tip),centre),f=Dist(Apply4(full[palm+k],tip),centre);
        if(!(f<h&&h<r))closer=false;
    }
    Check(others,"only that hand's fingers move",name);Check(meta,"shader data survives",name);
    Check(rigid,"finger transforms stay rigid",name);Check(connected,"finger joints stay connected",name);
    Check(closer,"all four fingertips close steadily with squeeze",name);
    {   // Across the whole squeeze the thumb must never pass underneath the fingers.
        Matrix frame{};(left?BuildLeftFrame(s.segments,frame,s.count):BuildRightFrame(s.segments,frame,s.count));
        V in{frame.m[0],frame.m[1],frame.m[2]},bendWay{};
        for(unsigned k:{1u,4u,7u,10u})bendWay=Add(bendWay,Sub(s.rest[palm+k+2],s.rest[palm+k]));
        if(Dot(in,bendWay)<0)in=Scale(in,-1);
        bool outside=true;double worst=1e9;
        for(int step=1;step<=20;step++) {
            auto q=base;Apply(q.data(),s.count,d,float(step)/20.f);
            const V tip=Apply4(q[palm+15],s.rest[palm+15]);
            for(unsigned f:{1u,4u,7u,10u})for(unsigned j=0;j<3;j++) {
                const V joint=Apply4(q[palm+f+j],s.rest[palm+f+j]);auto gap=Sub(tip,joint);
                const double height=Dot(gap,in);auto flat=Sub(gap,Scale(in,height));
                if(Dot(flat,flat)<36.0){worst=std::min(worst,height);if(height<0)outside=false;}
            }
        }
        Check(outside,"the thumb stays outside the fingers through the whole squeeze",name);
    }
}
int main() {
    Skeleton a,b,c;
    Check(Load("2026-10-15-hand-alignment/skeleton.json",a)&&a.count==223,"223-bone skeleton loads");
    Check(Load("2026-10-18-hand-models/skeleton-87363.json",b)&&b.count==162,"162-bone skeleton 87363 loads");
    Check(Load("2026-10-18-hand-models/skeleton-87362.json",c)&&c.count==162,"162-bone skeleton 87362 loads");
    Hand(a,false,"223 right");Hand(a,true,"223 left");
    Hand(b,false,"87363 right");Hand(c,false,"87362 right");
    for(auto* s:{&b,&c}) {
        Matrix f{};const bool leftOk=BuildLeftFrame(s->segments,f,s->count);
        printf("NOTE %s left frame on this 162-bone model: %s\n",s==&b?"87363":"87362",leftOk?"yes":"no");
        if(leftOk)Hand(*s,true,s==&b?"87363 left":"87362 left");
    }
    {   // The C++ pose must be the pose Mark saw in the preview (report.json, CLOSED GRIP).
        const auto d=InteractionCurl::Build(a.segments,a.count,false,true);auto p=Identity();
        bool ok=InteractionCurl::Apply(p.data(),a.count,d,1.f);const V centre=PalmPoint(a,130);
        const unsigned tips[5]{133,136,139,142,145};const double want[5]{18.3,18.7,19.4,19.4,23.27};
        for(int i=0;i<5;i++){const double got=Dist(Apply4(p[tips[i]],a.rest[tips[i]]),centre);if(std::abs(got-want[i])>.25){ok=false;printf("NOTE tip %u got %.2f want %.1f\n",tips[i],got,want[i]);}}
        Check(ok,"full squeeze matches the previewed closed grip");
        const auto g=InteractionCurl::Build(a.segments,a.count);Matrix frame{};
        bool gentle=g.valid&&BuildRightFrame(a.segments,frame,a.count);
        const V inward{-frame.m[0],-frame.m[1],-frame.m[2]};
        for(unsigned root:{131u,134u,137u,140u,143u}) {
            auto axis=Cross(Sub(a.rest[root+2],a.rest[root]),inward);gentle=gentle&&Normalize(axis);
            for(unsigned j=0;j<3;j++) {
                const float degrees=root==143?(j==1?6.f:4.f):(j==1?25.f:15.f);
                const float half=degrees*.00872664626f,sine=std::sin(half);const auto& t=g.target[root+j-131];
                if(std::abs(t.x-float(axis[0])*sine)>1e-5f||std::abs(t.y-float(axis[1])*sine)>1e-5f||
                   std::abs(t.z-float(axis[2])*sine)>1e-5f||std::abs(t.w-std::cos(half))>1e-5f)gentle=false;
            }
        }
        Check(gentle,"HandGripClose=0 keeps the original gentle curl exactly");
    }
    {
        using InteractionCurl::GripTarget;
        Check(GripTarget(0,false)==0&&GripTarget(.05f,false)==0,"a resting finger on the grip leaves the hand open");
        Check(GripTarget(0,true)==1,"Interact on a non-grip button closes the hand");
        Check(std::abs(GripTarget(.5f,true)-.5f)<1e-5f&&GripTarget(1,true)==1,"grip amount wins when the grip is what is pressed");
        Check(GripTarget(NAN,false)==0,"a bad grip reading is treated as open");
        InteractionCurl::GripEnvelope e;uintptr_t owner=7;
        Check(e.Update(1000,1,owner,true,1)==0,"closing starts from open");
        float v=e.Update(1065,2,owner,true,1);Check(std::abs(v-.5f)<1e-4f,"closes at the slower speed");
        Check(e.Update(1065,2,owner,true,0)==v,"a second eye of the same frame does not advance it");
        v=e.Update(1200,3,owner,false,1);Check(v==0,"losing eligibility opens at once");
        e.Update(2000,4,owner,true,.5f);e.Update(2100,5,owner,true,.5f);Check(e.Update(2120,6,owner,true,.5f)==.5f,"holds a partial squeeze");
    }
    {
        const auto bits=Transport::PackGrips(.5f,1.f)|1u;
        Check((bits&1)==1&&std::abs(Transport::LeftGrip(bits)-.5f)<.005f&&Transport::RightGrip(bits)==1.f,"grip amounts cross the transport without touching the crosshair bit");
        Check(Transport::PackGrips(NAN,-1)==0&&Transport::PackGrips(2,0)==(255u<<8),"bad grip values are clamped");
    }
    return failures?1:0;
}
