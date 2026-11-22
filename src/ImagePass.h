#pragma once
#include "ImageAdjust.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
// Draws one eye of the game's image into the headset image with brightness and
// contrast applied. Used only when a setting is not 0; otherwise the plain copy runs.
class ImagePass {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Target {ID3D11Texture2D* texture{};Ptr<ID3D11RenderTargetView> views[2];bool linear{};};
    struct Constants {float exponent,contrast,linearOut;int yOffset;float visibility,pad[3];};
    Ptr<ID3D11Device> device;
    Ptr<ID3D11VertexShader> vs;
    Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11Texture2D> source;
    Ptr<ID3D11ShaderResourceView> srv;
    std::vector<Target> targets;
    bool failed{};
    bool Init(ID3D11Device* dev) {
        static constexpr char shader[]=R"(
Texture2D<float4> image:register(t0);
cbuffer Settings:register(b0){float exponent;float contrast;float linearOut;int yOffset;float visibility;float3 pad;};
float4 VS(uint id:SV_VertexID):SV_POSITION{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float3 ToLinear(float3 c){float3 low=c/12.92;float3 high=pow((c+0.055)/1.055,2.4);return lerp(high,low,step(c,0.04045));}
float4 PS(float4 position:SV_POSITION):SV_TARGET{
    float4 s=image.Load(int3(int(position.x),int(position.y)+yOffset,0));
    float3 c=pow(saturate(s.rgb),exponent);
    c=saturate((c-0.5)*contrast+0.5)*visibility;
    if(linearOut>0.5)c=ToLinear(c);
    return float4(c,s.a);
}
)";
        Ptr<ID3DBlob> v,p,error;
        if(FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_4_0",0,0,&v,&error))||
           FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_4_0",0,0,&p,&error)))return false;
        if(FAILED(dev->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs))||
           FAILED(dev->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DYNAMIC;
        b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(dev->CreateBuffer(&b,nullptr,&constants)))return false;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        if(FAILED(dev->CreateRasterizerState(&r,&raster)))return false;
        device=dev;return true;
    }
    // The headset image is written as display-encoded values. An sRGB view would
    // encode again, so the shader converts to linear first when only that view exists.
    Target* Find(ID3D11Device* dev,ID3D11Texture2D* texture) {
        for(auto& t:targets)if(t.texture==texture)return &t;
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        DXGI_FORMAT format=d.Format;bool linear=false;
        switch(d.Format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:format=DXGI_FORMAT_R8G8B8A8_UNORM;break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:format=DXGI_FORMAT_B8G8R8A8_UNORM;break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM:break;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:linear=true;break;
        default:return nullptr;
        }
        if(d.ArraySize<2||d.SampleDesc.Count!=1||!(d.BindFlags&D3D11_BIND_RENDER_TARGET))return nullptr;
        Target t{};t.texture=texture;t.linear=linear;
        for(UINT eye=0;eye<2;++eye) {
            D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=format;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rv.Texture2DArray.FirstArraySlice=eye;rv.Texture2DArray.ArraySize=1;
            if(FAILED(dev->CreateRenderTargetView(texture,&rv,&t.views[eye])))return nullptr;
        }
        targets.push_back(std::move(t));return &targets.back();
    }
public:
    bool Failed() const {return failed;}
    // Release before the headset images are destroyed: the views keep them alive.
    void Reset() {
        targets.clear();srv.Reset();source.Reset();raster.Reset();constants.Reset();
        ps.Reset();vs.Reset();device.Reset();failed=false;
    }
    bool Draw(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* pair,ID3D11Texture2D* texture,
              UINT eye,UINT sourceY,UINT width,UINT height,const ImageAdjust::Settings& settings,float visibility=1.f) {
        if(failed||eye>1)return false;
        if(device.Get()!=dev){Reset();if(!Init(dev)){Reset();failed=true;return false;}}
        if(source.Get()!=pair) {
            srv.Reset();source.Reset();
            D3D11_TEXTURE2D_DESC d{};pair->GetDesc(&d);
            if((d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)||
               !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)||FAILED(dev->CreateShaderResourceView(pair,nullptr,&srv))){failed=true;return false;}
            source=pair;
        }
        auto* target=Find(dev,texture);
        if(!target){failed=true;return false;}
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(ctx->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map))){failed=true;return false;}
        *static_cast<Constants*>(map.pData)={ImageAdjust::Exponent(settings.brightness),ImageAdjust::ContrastFactor(settings.contrast),target->linear?1.f:0.f,int(sourceY),visibility<0?0.f:visibility>1?1.f:visibility,{0,0,0}};
        ctx->Unmap(constants.Get(),0);
        auto* output=target->views[eye].Get();auto* input=srv.Get();auto* buffer=constants.Get();
        ctx->OMSetRenderTargets(1,&output,nullptr);ctx->OMSetBlendState(nullptr,nullptr,0xffffffff);ctx->OMSetDepthStencilState(nullptr,0);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};ctx->RSSetViewports(1,&viewport);ctx->RSSetScissorRects(0,nullptr);ctx->RSSetState(raster.Get());
        ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
        ctx->PSSetShader(ps.Get(),nullptr,0);ctx->PSSetShaderResources(0,1,&input);ctx->PSSetConstantBuffers(0,1,&buffer);
        ctx->Draw(3,0);
        input=nullptr;ctx->PSSetShaderResources(0,1,&input);ctx->OMSetRenderTargets(0,nullptr,nullptr);
        return true;
    }
};
