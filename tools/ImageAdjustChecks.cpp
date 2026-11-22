// Offline checks of the image settings' values and the experimental render scale.
#include <cstdio>
#include "ImageAdjust.h"
#include "RenderScale.h"
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static bool Near(float a,float b){return std::abs(a-b)<.0005f;}
int main() {
    using namespace ImageAdjust;
    Check(Settings{}.Neutral(),"image: 0 and 0 is the plain copy");
    bool same=true;for(int i=0;i<=255;++i)same=same&&Near(Apply(i/255.f,{}),i/255.f);
    Check(same,"image: neutral values change no shade");
    Check(Near(Apply(0,{50,0}),0)&&Near(Apply(1,{50,0}),1),"image: brightness keeps black and white");
    Check(Apply(.25f,{25,0})>.25f&&Apply(.25f,{-25,0})<.25f,"image: brightness raises and lowers shadows");
    Check(Apply(.25f,{0,25})<.25f&&Apply(.75f,{0,25})>.75f,"image: contrast spreads shades from the middle");
    Check(Near(Apply(.5f,{0,50}),.5f),"image: contrast keeps mid grey");
    Check(Parse(L"15")==15&&Parse(L"-10")==-10,"image: values read from the file");
    Check(Parse(L"7")==5&&Parse(L"999")==50&&Parse(L"-999")==-50,"image: values snap to steps and limits");
    Check(Parse(L"junk")==0&&Parse(L"")==0&&Parse(L"5x")==0,"image: bad text means neutral");
    Check(Decode(Encode(-50))==-50&&Decode(Encode(50))==50&&Decode(Encode(0))==0,"image: values survive the game-to-companion link");
    Check(ValidWire(0)&&ValidWire(Encode(-50))&&!ValidWire(50)&&!ValidWire(152)&&!ValidWire(108),"image: invalid link values refused");
    using namespace RenderScale;
    Check(RenderScale::Parse(L"1")==1.f&&RenderScale::Parse(L"junk")==1.f&&RenderScale::Parse(L"")==1.f,"scale: absent or bad text is off");
    Check(RenderScale::Parse(L".5")==1.f&&RenderScale::Parse(L"9")==2.f,"scale: limited to 1.0 through 2.0");
    {auto s=RenderScale::Apply(3048,3264,16384,8192,1.f);Check(s.width==3048&&s.height==3264,"scale: 1.0 leaves the size unchanged");}
    {auto s=RenderScale::Apply(3048,3264,16384,8192,1.25f);Check(s.width==3810&&s.height==4080,"scale: 1.25 multiplies both sides");}
    {auto s=RenderScale::Apply(3048,3264,16384,8192,1.33f);Check(s.width%2==0&&s.height%2==0,"scale: sizes stay even");}
    {auto s=RenderScale::Apply(3048,3264,4096,4096,2.f);Check(s.width<=4096&&s.height<=4096&&s.height>=4094,"scale: stays inside the runtime's limit");}
    {auto s=RenderScale::Apply(5000,5000,4096,4096,2.f);Check(s.width==5000&&s.height==5000,"scale: never shrinks the recommended size");}
    return failures?1:0;
}
