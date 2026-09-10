#include "NativeBridge.h"
#include <windows.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <cstring>
using Microsoft::WRL::ComPtr;
int main(int argc,char** argv) {
    D3D11_TEXTURE2D_DESC d{}; d.Width=1024;d.Height=1024;d.ArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(!NativeBridge::ValidatePair(d,512))return 10;
    if(NativeBridge::ValidatePair(d,0)||NativeBridge::ValidatePair(d,1024))return 11;
    d.SampleDesc.Count=2;if(NativeBridge::ValidatePair(d,512))return 12;d.SampleDesc.Count=1;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)){printf("D3D11 device failed: %08lx\n",hr);return 1;}
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    std::vector<unsigned> pixels(1024*1024);
    for(unsigned y=0;y<1024;y++)for(unsigned x=0;x<1024;x++)pixels[y*1024+x]=y<512?0xff3030d0:0xff30d030;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),1024*4,0};ComPtr<ID3D11Texture2D> pair;
    hr=dev->CreateTexture2D(&d,&data,&pair);if(FAILED(hr))return 2;
    // Exercise actual array-slice copies and read them back. Detect eye swap/box errors.
    D3D11_TEXTURE2D_DESC dest=d;dest.Height=512;dest.ArraySize=2;
    ComPtr<ID3D11Texture2D> eyes; if(FAILED(dev->CreateTexture2D(&dest,nullptr,&eyes)))return 3;
    for(UINT e=0;e<2;e++){D3D11_BOX b{0,e*512,0,1024,(e+1)*512,1};ctx->CopySubresourceRegion(eyes.Get(),e,0,0,0,pair.Get(),0,&b);}
    dest.Usage=D3D11_USAGE_STAGING;dest.BindFlags=0;dest.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;if(FAILED(dev->CreateTexture2D(&dest,nullptr,&read)))return 4;
    ctx->CopyResource(read.Get(),eyes.Get());
    for(UINT e=0;e<2;e++){
        D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(ctx->Map(read.Get(),e,D3D11_MAP_READ,0,&m)))return 5;
        bool valid=true;for(UINT y=0;y<512;y++)for(UINT x=0;x<1024;x++)
            if(reinterpret_cast<const unsigned*>(static_cast<const char*>(m.pData)+y*m.RowPitch)[x]!=(e?0xff30d030:0xff3030d0))valid=false;
        ctx->Unmap(read.Get(),e);if(!valid)return 6;
    }
    printf("PASS: %u-bit D3D11 native pair bounds and GPU eye-array copies/readback.\n",unsigned(sizeof(void*)*8));
    if(argc<2||strcmp(argv[1],"--xr"))return 0;
    printf("Testing OpenXR for 20 seconds. Left red, right green.\n");
    ULONGLONG end=GetTickCount64()+20000;
    while(GetTickCount64()<end){NativeBridge::Present(nullptr,pair.Get(),512,false);Sleep(10);}
    auto n=NativeBridge::SubmittedPairs();NativeBridge::Shutdown();
    printf("Submitted pairs: %llu\n",n);return n?0:7;
}
