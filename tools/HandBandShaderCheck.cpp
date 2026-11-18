// Runs the three compiled wrist-band vertex shaders (src/HandBandBlobs.h) on
// Windows' software graphics device (WARP) and compares every output with the
// CPU mirror in CameraMath (BandClip, BandFacing) and with what the game's
// original HUD shaders compute for texture coordinates and colour. Also draws
// a band through a pixel shader that takes the game's HUD inputs. This checks
// the shader code itself, not the game: the constants are stand-ins.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "CameraMath.h"
#include "ModelWristHud.h"
#include "HandBandBlobs.h"
#include "HandBandConstants.h"
using Microsoft::WRL::ComPtr;

static int failures=0;
static void Check(bool ok,const char* what){if(!ok){fprintf(stderr,"FAIL %s\n",what);++failures;}}
static bool Near(float a,float b,float tolerance){return std::abs(a-b)<=tolerance*(1+std::abs(b));}

struct Vertex { float color[4]; float texcoord[4]; float position[3]; };
static_assert(sizeof(Vertex)==44);

static ComPtr<ID3D11Buffer> Constant(ID3D11Device* d,const void* data,UINT bytes) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=(bytes+15)&~15u;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    std::vector<unsigned char> padded(desc.ByteWidth);memcpy(padded.data(),data,bytes);
    D3D11_SUBRESOURCE_DATA init{padded.data(),0,0};ComPtr<ID3D11Buffer> b;
    if(FAILED(d->CreateBuffer(&desc,&init,&b))){fprintf(stderr,"FAIL constant buffer\n");exit(1);}
    return b;
}

int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,got{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&got,&context))) {
        fprintf(stderr,"FAIL no WARP device\n");return 1;
    }
    // A left wrist raised, back of the hand toward the face, so the band is
    // in view; and the same wrist turned the other way.
    Transport::Tracking h{};
    h.head.orientation.w=1;
    for(auto& eye:h.eyes){eye.pose.orientation.w=1;eye.left=-.7f;eye.right=.8f;eye.up=.75f;eye.down=-.75f;}
    h.eyes[0].pose.position={-.032f,0,0};h.eyes[1].pose.position={.032f,0,0};
    auto fromAxes=[](Transport::Vector x,Transport::Vector y,Transport::Vector z) {
        float m00=x.x,m11=y.y,m22=z.z,trace=m00+m11+m22;Transport::Quaternion q{};
        if(trace>0){float s=std::sqrt(trace+1)*2;q.w=.25f*s;q.x=(y.z-z.y)/s;q.y=(z.x-x.z)/s;q.z=(x.y-y.x)/s;}
        else if(m00>m11&&m00>m22){float s=std::sqrt(1+m00-m11-m22)*2;q.w=(y.z-z.y)/s;q.x=.25f*s;q.y=(y.x+x.y)/s;q.z=(z.x+x.z)/s;}
        else if(m11>m22){float s=std::sqrt(1+m11-m00-m22)*2;q.w=(z.x-x.z)/s;q.x=(y.x+x.y)/s;q.y=.25f*s;q.z=(z.y+y.z)/s;}
        else {float s=std::sqrt(1+m22-m00-m11)*2;q.w=(x.y-y.x)/s;q.x=(z.x+x.z)/s;q.y=(z.y+y.z)/s;q.z=.25f*s;}
        return q;
    };
    auto grip=fromAxes({0,0,-1},{0,-1,0},{-1,0,0});
    auto turned=fromAxes({0,0,1},{0,-1,0},{1,0,0});
    // Flat layout units as the game's rows produce them (logs: X in the
    // thousands, Y in the hundreds), a parked shift of 50, and a size per
    // unit that spreads the band about 40 degrees each way round the wrist.
    const float instance[16]={ .12f,.001f,0,-1200.f,  .002f,.125f,0,-800.f,  .0008f,0,0,.25f,  0,.0009f,0,.5f };
    const float shift=50.f,middleX=.12f*200-1200+shift,middleY=-800.f+.125f*10;
    const float perUnitX=1.13e-3f,perUnitY=-5e-3f;
    CameraMath::BandFrame frame,away,disc;
    // Also the flat face on the open end of the right wrist (ammo, November
    // 15), drawn by the same shader as a 10 m cylinder: right hand pointing
    // forward, thumb up, the wrist end toward the head.
    const Transport::Quaternion pointing{.70710678f,0,0,.70710678f};
    using ModelWristHud::Piece;
    if(!ModelWristHud::Frame(h,0,Transport::Pose{grip,{-.1f,0,-.4f}},Piece::HealthEnergy,300,middleX,middleY,frame,perUnitX,perUnitY,shift) ||
       !ModelWristHud::Frame(h,0,Transport::Pose{turned,{-.1f,0,-.4f}},Piece::HealthEnergy,300,middleX,middleY,away,perUnitX,perUnitY,shift) ||
       !ModelWristHud::Frame(h,0,Transport::Pose{pointing,{.15f,-.25f,-.45f}},Piece::Ammo,300,middleX,middleY,disc,perUnitX,perUnitY,shift)) {
        fprintf(stderr,"FAIL band frame\n");return 1;
    }
    const float texScale[8]={0,0,0,0, .5f,0,0,0};
    auto b3=Constant(device.Get(),texScale,32),b5=Constant(device.Get(),instance,64);
    const HandBand::Constants bandFront=HandBand::Pack(frame),bandAway=HandBand::Pack(away),bandDisc=HandBand::Pack(disc);
    auto b11Front=Constant(device.Get(),&bandFront,sizeof(bandFront)),b11Away=Constant(device.Get(),&bandAway,sizeof(bandAway));
    auto b11Disc=Constant(device.Get(),&bandDisc,sizeof(bandDisc));

    // Vertices spread across the band, some beyond its ends.
    Vertex vertices[7]{};
    const float px[7]={-120,40,200,360,520,-2000,4000},py[7]={-40,0,30,60,-20,0,0};
    for(int i=0;i<7;++i){vertices[i].position[0]=px[i];vertices[i].position[1]=py[i];vertices[i].position[2]=0;
        vertices[i].texcoord[0]=.1f*i;vertices[i].texcoord[1]=.2f;vertices[i].color[0]=.25f;vertices[i].color[3]=1;}
    D3D11_BUFFER_DESC vbDesc{};vbDesc.ByteWidth=sizeof(vertices);vbDesc.Usage=D3D11_USAGE_DEFAULT;vbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbInit{vertices,0,0};ComPtr<ID3D11Buffer> vb;device->CreateBuffer(&vbDesc,&vbInit,&vb);
    D3D11_INPUT_ELEMENT_DESC layoutDesc[3]={
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};

    struct Variant { const char* name; const void* blob; SIZE_T size; int outputs; };
    const Variant variants[3]={
        {"5412 position only",HandBandBlobs::BandPositionOnly,sizeof(HandBandBlobs::BandPositionOnly),1},
        {"0e624 generated UV",HandBandBlobs::BandGeneratedUv,sizeof(HandBandBlobs::BandGeneratedUv),2},
        {"b22b colour and UV",HandBandBlobs::BandColorUv,sizeof(HandBandBlobs::BandColorUv),3}};
    for(int v=0;v<3;++v) for(int pose=0;pose<3;++pose) {
        const auto& var=variants[v];
        const CameraMath::BandFrame& f=pose==2?disc:pose?away:frame;
        ID3D11Buffer* slots[12]{nullptr,nullptr,nullptr,b3.Get(),nullptr,b5.Get(),nullptr,nullptr,nullptr,nullptr,nullptr,(pose==2?b11Disc:pose?b11Away:b11Front).Get()};
        ComPtr<ID3D11VertexShader> vs;
        Check(SUCCEEDED(device->CreateVertexShader(var.blob,var.size,nullptr,&vs)),"vertex shader created");
        ComPtr<ID3D11InputLayout> il;
        Check(SUCCEEDED(device->CreateInputLayout(layoutDesc,3,var.blob,var.size,&il)),"game's input layout accepted");
        // Stream the vertex shader's outputs back, the clip distance included.
        D3D11_SO_DECLARATION_ENTRY so[4]={{0,"SV_POSITION",0,0,4,0},{0,var.outputs==3?"COLOR":"TEXCOORD",0,0,4,0},{0,"TEXCOORD",0,0,4,0},{0,"SV_ClipDistance",0,0,1,0}};
        D3D11_SO_DECLARATION_ENTRY used[4];UINT count=0;
        for(int k=0;k<var.outputs;++k)used[count++]=so[k];
        used[count++]=so[3];
        UINT stride=16*var.outputs+4;
        ComPtr<ID3D11GeometryShader> gs;
        Check(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(var.blob,var.size,used,count,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)),"stream output created");
        D3D11_BUFFER_DESC soDesc{};soDesc.ByteWidth=stride*7;soDesc.Usage=D3D11_USAGE_DEFAULT;soDesc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
        ComPtr<ID3D11Buffer> soBuffer;device->CreateBuffer(&soDesc,nullptr,&soBuffer);
        D3D11_BUFFER_DESC stDesc=soDesc;stDesc.Usage=D3D11_USAGE_STAGING;stDesc.BindFlags=0;stDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;device->CreateBuffer(&stDesc,nullptr,&staging);
        UINT vstride=sizeof(Vertex),offset=0;ID3D11Buffer* vbs=vb.Get();ID3D11Buffer* sos=soBuffer.Get();
        context->IASetInputLayout(il.Get());context->IASetVertexBuffers(0,1,&vbs,&vstride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->VSSetConstantBuffers(0,12,slots);
        context->GSSetShader(gs.Get(),nullptr,0);context->SOSetTargets(1,&sos,&offset);
        context->Draw(7,0);
        ID3D11Buffer* none=nullptr;context->SOSetTargets(1,&none,&offset);context->GSSetShader(nullptr,nullptr,0);
        context->CopyResource(staging.Get(),soBuffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))){fprintf(stderr,"FAIL readback\n");return 1;}
        const float* out=static_cast<const float*>(mapped.pData);
        int bent=0,shown=0;
        for(int i=0;i<7;++i) {
            const float* o=out+i*(stride/4);
            const float facing=o[4*var.outputs];
            const float p[4]={px[i],py[i],0,1};
            float X=0,Y=0;for(int k=0;k<4;++k){X+=instance[k]*p[k];Y+=instance[4+k]*p[k];}
            float expected[4];
            if(CameraMath::BandClip(f,X,Y,expected)) {
                ++bent;
                for(int j=0;j<4;++j)Check(Near(o[j],expected[j],2e-4f),"shader position equals the CPU band from the flat layout (BandClip)");
                Check(Near(facing,CameraMath::BandFacing(f,X,Y),2e-4f),"shader clip distance equals the CPU facing (BandFacing)");
                if(facing>0)++shown;
            } else {
                Check(o[0]==2 && o[1]==2 && o[2]==0 && o[3]==1 && facing<0,"vertex beyond the band's ends is placed outside the view and clipped");
            }
            if(var.outputs==2) {
                Check(Near(o[4],instance[8]*p[0]+instance[9]*p[1]+instance[10]*p[2]+instance[11],1e-5f)
                    && Near(o[5],instance[12]*p[0]+instance[13]*p[1]+instance[14]*p[2]+instance[15],1e-5f)
                    && o[6]==0 && o[7]==0,"generated texture coordinates as the game's 0e624 shader");
            }
            if(var.outputs==3) {
                Check(o[4]==.25f && o[7]==1,"vertex colour passed through as the game's b22b shader");
                Check(Near(o[8],vertices[i].texcoord[0]*.5f,1e-6f) && Near(o[9],.2f*.5f,1e-6f) && o[10]==0 && o[11]==0,
                    "texture coordinates scaled as the game's b22b shader");
            }
        }
        context->Unmap(staging.Get(),0);
        if(pose==2) {
            // The flat face has no wrap-round limit for these offsets: all seven placed and shown.
            Check(bent==7,"wrist end: all seven vertices on the flat face");
            Check(shown==7,"wrist end facing the eye: the whole face is shown");
            printf("%s, right wrist end facing the eye: 7 vertices checked\n",var.name);
            continue;
        }
        Check(bent==5,"five vertices on the band, two beyond its ends");
        Check(pose?shown==0:shown==5,pose?"turned away: the whole band is on the far side and clipped":"facing: the whole band is shown");
        printf("%s, %s: 7 vertices checked\n",var.name,pose?"wrist turned away":"band facing the eye");
    }

    // A real draw through a pixel shader with the game's inputs: the extra
    // clip distance links, and the far side of the wrist draws nothing.
    {
        const char* psSource="float4 main(float4 p:SV_POSITION,float4 c:COLOR0,float4 t:TEXCOORD0):SV_TARGET{return float4(1,1,1,1)+c*0+t*0;}";
        ComPtr<ID3DBlob> psBlob,errors;
        Check(SUCCEEDED(D3DCompile(psSource,strlen(psSource),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&psBlob,&errors)),"test pixel shader compiled");
        ComPtr<ID3D11PixelShader> ps;device->CreatePixelShader(psBlob->GetBufferPointer(),psBlob->GetBufferSize(),nullptr,&ps);
        ComPtr<ID3D11VertexShader> vs;device->CreateVertexShader(HandBandBlobs::BandColorUv,sizeof(HandBandBlobs::BandColorUv),nullptr,&vs);
        ComPtr<ID3D11InputLayout> il;device->CreateInputLayout(layoutDesc,3,HandBandBlobs::BandColorUv,sizeof(HandBandBlobs::BandColorUv),&il);
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
        td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;device->CreateTexture2D(&td,nullptr,&target);
        ComPtr<ID3D11RenderTargetView> rtv;device->CreateRenderTargetView(target.Get(),nullptr,&rtv);
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;device->CreateTexture2D(&td,nullptr,&read);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> rs;device->CreateRasterizerState(&rd,&rs);
        Vertex quad[6]{};
        const float qx[6]={-120,520,520,-120,520,-120},qy[6]={-40,-40,60,-40,60,60};
        for(int i=0;i<6;++i){quad[i].position[0]=qx[i];quad[i].position[1]=qy[i];quad[i].color[3]=1;}
        D3D11_BUFFER_DESC qd{};qd.ByteWidth=sizeof(quad);qd.Usage=D3D11_USAGE_DEFAULT;qd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA qi{quad,0,0};ComPtr<ID3D11Buffer> qb;device->CreateBuffer(&qd,&qi,&qb);
        auto pixels=[&](ID3D11Buffer* band) {
            const float clear[4]{0,0,0,0};context->ClearRenderTargetView(rtv.Get(),clear);
            ID3D11RenderTargetView* r=rtv.Get();context->OMSetRenderTargets(1,&r,nullptr);
            D3D11_VIEWPORT vp{0,0,128,128,0,1};context->RSSetViewports(1,&vp);context->RSSetState(rs.Get());
            UINT st=sizeof(Vertex),of=0;ID3D11Buffer* b=qb.Get();
            context->IASetInputLayout(il.Get());context->IASetVertexBuffers(0,1,&b,&st,&of);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11Buffer* s[12]{nullptr,nullptr,nullptr,b3.Get(),nullptr,b5.Get(),nullptr,nullptr,nullptr,nullptr,nullptr,band};
            context->VSSetConstantBuffers(0,12,s);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
            context->Draw(6,0);
            context->CopyResource(read.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE m{};context->Map(read.Get(),0,D3D11_MAP_READ,0,&m);
            int n=0;for(UINT y=0;y<128;++y){auto row=static_cast<const uint8_t*>(m.pData)+y*m.RowPitch;for(UINT x=0;x<128;++x)if(row[x*4])++n;}
            context->Unmap(read.Get(),0);return n;
        };
        const int front=pixels(b11Front.Get()),back=pixels(b11Away.Get());
        printf("full draw: %d pixels facing, %d pixels turned away\n",front,back);
        Check(front>20,"band facing the eye is drawn through the game-like pixel shader");
        Check(back==0,"band on the far side of the wrist draws nothing");
    }
    // The band is smooth: neighbouring points along it stay on the wrist's
    // surface, the fitted radius from the cuff centre line, with no jumps.
    CameraMath::Vector previous{};bool havePrevious=false;
    for(float x=-1240.f;x<=-1110.f;x+=1.f) {
        CameraMath::Vector p;
        if(!CameraMath::BandPoint(frame,x,middleY,p))continue;
        const float dx=p.x-frame.axis.x,dy=p.y-frame.axis.y,dz=p.z-frame.axis.z;
        const float along=dx*frame.up.x+dy*frame.up.y+dz*frame.up.z;
        const float radial=std::sqrt(dx*dx+dy*dy+dz*dz-along*along);
        Check(std::abs(radial-frame.radius)<1e-4f,"every point of the band is on the wrist's surface");
        if(havePrevious) {
            const float step=std::sqrt((p.x-previous.x)*(p.x-previous.x)+(p.y-previous.y)*(p.y-previous.y)+(p.z-previous.z)*(p.z-previous.z));
            Check(step<std::abs(frame.metresX)*1.01f,"no jumps between neighbouring points");
        }
        previous=p;havePrevious=true;
    }
    // A line of text in the flat layout (constant Y) keeps one distance along
    // the forearm, so it runs parallel to the cuff rim.
    {
        CameraMath::Vector a,b;
        Check(CameraMath::BandPoint(frame,-1200,middleY,a) && CameraMath::BandPoint(frame,-1120,middleY,b),"two points on one text line");
        const float da=(a.x-frame.axis.x)*frame.up.x+(a.y-frame.axis.y)*frame.up.y+(a.z-frame.axis.z)*frame.up.z;
        const float db=(b.x-frame.axis.x)*frame.up.x+(b.y-frame.axis.y)*frame.up.y+(b.z-frame.axis.z)*frame.up.z;
        Check(std::abs(da-db)<1e-6f,"a text line runs parallel to the cuff rim");
    }
    // Reading direction with the hand raised, back toward the face: left to
    // right and upright (layout Y grows downward).
    {
        float c0[4],cr[4],cu[4];
        Check(CameraMath::BandClip(frame,middleX-shift,middleY,c0) && CameraMath::BandClip(frame,middleX-shift+20,middleY,cr) && CameraMath::BandClip(frame,middleX-shift,middleY-4,cu),"band points in view");
        Check(cr[0]/cr[3]>c0[0]/c0[3]+.005f,"band reads left to right");
        Check(cu[1]/cu[3]>c0[1]/c0[3]+.002f,"band is upright");
    }
    if(failures){fprintf(stderr,"%d failures\n",failures);return 1;}
    puts("PASS wrist band shaders on WARP: positions and clip distances equal the CPU band from the flat HUD layout for all three game HUD shader variants, facing and turned away; texture coordinates and colour as the originals; game input layout accepted; a full draw through a game-like pixel shader shows the facing band and nothing of the far side; band smooth, on the wrist's surface, text lines square to the rim");
    return 0;
}
