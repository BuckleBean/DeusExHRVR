// Runs the brightness and contrast shader on Windows' software renderer (WARP) and
// compares every headset format with its CPU mirror. No game or headset involved.
#include <cstdio>
#include <array>
#include "ImagePass.h"
using Microsoft::WRL::ComPtr;
static int failures=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static constexpr UINT W=4,H=4;
static std::array<unsigned char,3> Read(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* texture,UINT eye) {
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.ArraySize=1;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;std::array<unsigned char,3> out{};
    if(FAILED(dev->CreateTexture2D(&d,nullptr,&staging)))return out;
    ctx->CopySubresourceRegion(staging.Get(),0,0,0,0,texture,eye,nullptr);
    D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return out;
    auto* p=static_cast<const unsigned char*>(map.pData)+map.RowPitch*2+2*4;out={p[0],p[1],p[2]};ctx->Unmap(staging.Get(),0);return out;
}
static bool Close(unsigned char got,unsigned char source,const ImageAdjust::Settings& s) {
    const int want=int(ImageAdjust::Apply(source/255.f,s)*255.f+.5f);return std::abs(int(got)-want)<=2;
}
int main() {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx))){puts("FAIL no WARP device");return 1;}
    static constexpr unsigned char top[4]{40,120,200,255},bottom[4]{200,60,20,255};
    std::array<unsigned char,W*H*2*4> pixels{};
    for(UINT y=0;y<H*2;++y)for(UINT x=0;x<W;++x)memcpy(&pixels[(y*W+x)*4],y<H?top:bottom,4);
    D3D11_TEXTURE2D_DESC pd{};pd.Width=W;pd.Height=H*2;pd.MipLevels=pd.ArraySize=1;pd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;pd.SampleDesc.Count=1;pd.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{pixels.data(),W*4,0};ComPtr<ID3D11Texture2D> pair;
    if(FAILED(dev->CreateTexture2D(&pd,&init,&pair))){puts("FAIL no source");return 1;}
    const ImageAdjust::Settings settings{25,-20};
    struct Case {DXGI_FORMAT format;const char* name;};
    for(auto c:{Case{DXGI_FORMAT_R8G8B8A8_TYPELESS,"typeless"},Case{DXGI_FORMAT_R8G8B8A8_UNORM,"plain"},Case{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"sRGB"}}) {
        D3D11_TEXTURE2D_DESC td=pd;td.Height=H;td.ArraySize=2;td.Format=c.format;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> target;if(FAILED(dev->CreateTexture2D(&td,nullptr,&target))){Check(false,c.name);continue;}
        ImagePass pass;
        const bool drew=pass.Draw(dev.Get(),ctx.Get(),pair.Get(),target.Get(),0,0,W,H,settings)&&pass.Draw(dev.Get(),ctx.Get(),pair.Get(),target.Get(),1,H,W,H,settings);
        auto left=Read(dev.Get(),ctx.Get(),target.Get(),0),right=Read(dev.Get(),ctx.Get(),target.Get(),1);
        bool ok=drew;for(int i=0;i<3;++i)ok=ok&&Close(left[i],top[i],settings)&&Close(right[i],bottom[i],settings);
        char name[80];sprintf_s(name,"shader matches its mirror on both eyes: %s",c.name);Check(ok,name);
        pass.Reset();
    }
    {
        D3D11_TEXTURE2D_DESC fd=pd;fd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;ComPtr<ID3D11Texture2D> unsupported,target;
        D3D11_TEXTURE2D_DESC td=pd;td.Height=H;td.ArraySize=2;dev->CreateTexture2D(&fd,nullptr,&unsupported);dev->CreateTexture2D(&td,nullptr,&target);
        ImagePass pass;const bool drew=pass.Draw(dev.Get(),ctx.Get(),unsupported.Get(),target.Get(),0,0,W,H,settings);
        Check(!drew&&pass.Failed(),"an unsupported game image falls back to the plain copy");
    }
    {
        D3D11_TEXTURE2D_DESC td=pd;td.Height=H;td.ArraySize=2;ComPtr<ID3D11Texture2D> target;dev->CreateTexture2D(&td,nullptr,&target);
        ImagePass pass;const ImageAdjust::Settings neutral{};
        const bool half=pass.Draw(dev.Get(),ctx.Get(),pair.Get(),target.Get(),0,0,W,H,neutral,.5f);auto a=Read(dev.Get(),ctx.Get(),target.Get(),0);
        bool ok=half;for(int i=0;i<3;++i)ok=ok&&std::abs(int(a[i])-int(top[i])/2)<=2;Check(ok,"a half fade darkens the image by half");
        const bool black=pass.Draw(dev.Get(),ctx.Get(),pair.Get(),target.Get(),0,0,W,H,neutral,0.f);auto b=Read(dev.Get(),ctx.Get(),target.Get(),0);
        Check(black&&!b[0]&&!b[1]&&!b[2],"a full fade is black");
        const bool none=pass.Draw(dev.Get(),ctx.Get(),pair.Get(),target.Get(),0,0,W,H,neutral,1.f);auto c=Read(dev.Get(),ctx.Get(),target.Get(),0);
        ok=none;for(int i=0;i<3;++i)ok=ok&&std::abs(int(c[i])-int(top[i]))<=2;Check(ok,"no fade leaves the image alone");
    }
    return failures?1:0;
}
