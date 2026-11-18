// Wrist band vertex shaders for the hand HUD (DeusExHRVR, November 12-13).
//
// The game's three Scaleform HUD vertex shaders (5412c91c6c582d9a,
// 0e624d71c99a22c3, b22b03cd660e6c41; disassembled in
// 2026-11-12-hud-render-recovery/) take a vertex's flat HUD layout position
//     X = dot(cb5[0], v)   Y = dot(cb5[1], v)
// and then tilt and project it (cb1[0] depth row, cb0 matrix). The game's
// HUD panels are turned about 20 degrees in 3D, so their projected text
// slopes a little; Mark saw the bands "off by a small degree" (November 13).
// These replacements start from the FLAT layout position (X, Y) instead,
// before any tilt, and bend every vertex onto a cylinder round the wrist.
//
// Parts of the band on the far side of the wrist from the eye are clipped
// (SV_ClipDistance), so the wrist hides them (Mark: "They can also still be
// seen through the hands/wrists").
//
// Inputs match the originals; outputs match them with one extra clip
// distance after the last original output, which the pixel shader ignores.
// Band parameters come from b11, set by the mod for each owned draw.
// CPU mirror for the offline checks: CameraMath::BandClip / BandFacing.

cbuffer StreamDecl : register(b3) { float4 NormalScaleOffset; float4 TexcoordScales; };
cbuffer InstanceBuffer : register(b5) { float4 InstanceParams[4]; };
cbuffer BandBuffer : register(b11) {
    float4 BandAxis;     // xyz: point on the forearm's centre line (head space, metres); w: radius
    float4 BandBack;     // xyz: from that line to the band's middle;  w: metres per unit of layout X
    float4 BandAcross;   // xyz: across the wrist, left to right;      w: metres per unit of layout Y
    float4 BandUp;       // xyz: along the forearm toward the hand;    w: band middle, layout X
    float4 BandMiddle;   // x: band middle, layout Y; y: largest angle round the wrist; z: X shift home
    row_major float4x4 EyeProjection; // head space -> this eye's clip space
};

void BandPosition(float3 position, out float4 clip, out float facing) {
    float4 p = float4(position, 1);
    float X = dot(InstanceParams[0], p) + BandMiddle.z;
    float Y = dot(InstanceParams[1], p);
    float theta = (X - BandUp.w) * BandBack.w / BandAxis.w;
    if (abs(theta) > BandMiddle.y) { clip = float4(2, 2, 0, 1); facing = -1; return; }
    float3 normal = cos(theta) * BandBack.xyz + sin(theta) * BandAcross.xyz;
    float3 head = BandAxis.xyz + BandAxis.w * normal + (Y - BandMiddle.x) * BandAcross.w * BandUp.xyz;
    clip = mul(float4(head, 1), EyeProjection);
    clip.z = 0; // an overlay like the rest of the HUD
    facing = dot(normal, -head) / max(length(head), 1e-4);
}

// 5412c91c6c582d9a: position only.
void BandPositionOnly(float4 color : COLOR0, float4 texcoord : TEXCOORD0, float3 position : POSITION0,
                      out float4 outPosition : SV_POSITION, out float outFacing : SV_ClipDistance0) {
    BandPosition(position, outPosition, outFacing);
}

// 0e624d71c99a22c3: position, and texture coordinates from cb5[2..3].
void BandGeneratedUv(float4 color : COLOR0, float4 texcoord : TEXCOORD0, float3 position : POSITION0,
                     out float4 outPosition : SV_POSITION, out float4 outTexcoord : TEXCOORD0,
                     out float outFacing : SV_ClipDistance0) {
    BandPosition(position, outPosition, outFacing);
    float4 p = float4(position, 1);
    outTexcoord = float4(dot(p, InstanceParams[2]), dot(p, InstanceParams[3]), 0, 0);
}

// b22b03cd660e6c41: position, vertex colour, scaled texture coordinates.
void BandColorUv(float4 color : COLOR0, float4 texcoord : TEXCOORD0, float3 position : POSITION0,
                 out float4 outPosition : SV_POSITION, out float4 outColor : COLOR0, out float4 outTexcoord : TEXCOORD0,
                 out float outFacing : SV_ClipDistance0) {
    BandPosition(position, outPosition, outFacing);
    outColor = color;
    outTexcoord = float4(texcoord.xy * TexcoordScales.xx, 0, 0);
}
