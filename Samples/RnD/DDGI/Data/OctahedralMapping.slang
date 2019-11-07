#include "HostDeviceData.h"

__import Helpers;

TextureCube gCubemap;
SamplerState gLinearSampler;

float3 sphericalMapping(float2 uv)
{
    float phi = M_PI * uv.y;
    float theta = M_PI2 * uv.x - M_PI/2;

    float3 dir;
    dir.x = sin(phi) * cos(theta);
    dir.z = sin(phi) * sin(theta);
    dir.y = cos(phi);
    return normalize(dir);
}

float4 sampleCubemap(float3 dir)
{
    // We are using right hand coordinate
    dir.z = -dir.z;
    return gCubemap.SampleLevel(gLinearSampler, dir, 0);
}

struct VsOut
{
    float2 texC : TEXCOORD;
    float4 pos : SV_POSITION;
};

VsOut VSMain(uint id: SV_VertexID)
{
    VsOut vOut; 
	vOut.texC = float2((id & 0x02) * 1.0, (id & 0x01) * 2.0);
	vOut.pos = float4(vOut.texC * float2(2, -2) + float2(-1, 1), 0, 1);
    return vOut;
}

float4 PSMain(VsOut pIn) : SV_TARGET0
{
    float3 dir = octDecode(UvToOct(pIn.texC));
    //float3 dir = sphericalMapping(pIn.texC);
    float4 ret = sampleCubemap(dir);
    return ret;
}
