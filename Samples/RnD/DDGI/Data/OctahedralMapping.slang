#include "HostDeviceData.h"

__import Octahedral;
__import Helpers;

Texture2D gFacePosX;
Texture2D gFaceNegX;
Texture2D gFacePosY;
Texture2D gFaceNegY;
Texture2D gFacePosZ;
Texture2D gFaceNegZ;

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
    float3 absDir = abs(dir);
    float maxVal = max(max(absDir.x, absDir.y), absDir.z);
    if (maxVal == absDir.x)
    {
        if (dir.x > 0)
        {
            float2 uv = NdcToUv(dir.zy/maxVal);
            return gFacePosX.SampleLevel(gLinearSampler, uv, 0);
        }
        else
        {
            float2 uv = NdcToUv(float2(-dir.z, dir.y)/maxVal);
            return gFaceNegX.SampleLevel(gLinearSampler, uv, 0);
        }
    }
    else if (maxVal == absDir.y)
    {
        if (dir.y > 0)
        {
            float2 uv = NdcToUv(float2(dir.x, dir.z)/maxVal);
            return gFacePosY.SampleLevel(gLinearSampler, uv, 0);
        }
        else
        {
            float2 uv = NdcToUv(float2(dir.x, -dir.z)/maxVal);
            return gFaceNegY.SampleLevel(gLinearSampler, uv, 0);
        }
    }
    else
    {
        if (dir.z > 0)
        {
            float2 uv = NdcToUv(float2(-dir.x, dir.y)/maxVal);
            return gFacePosZ.SampleLevel(gLinearSampler, uv, 0);
        }
        else
        {
            float2 uv = NdcToUv(float2(dir.x, dir.y)/maxVal);
            return gFaceNegZ.SampleLevel(gLinearSampler, uv, 0);
        }
    }
    return 0;
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
