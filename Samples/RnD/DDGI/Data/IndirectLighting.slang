#include "HostDeviceData.h"

__import BRDF;
__import Helpers;


cbuffer PerFrameCB
{
    float4x4 gInvViewProjMat;
    float4 gCameraPos;
};

Texture2D gLightingTex;
Texture2D gNormalTex;
Texture2D gDiffuseOpacity;
Texture2D gSpecRoughTex;
Texture2D gDepthTex;

SamplerState gPointSampler;
SamplerState gLinearSampler;

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
    if (gDepthTex.SampleLevel(gPointSampler, pIn.texC, 0).r >= 1.0)
        return 0;

#if defined(_DIFFUSE)
    //return gLightingTex.SampleLevel(gPointSampler, pIn.texC, 0);
    float3 irradiance = gLightingTex.SampleLevel(gPointSampler, pIn.texC, 0).rgb;
    float3 diffuseBrdf = gDiffuseOpacity.SampleLevel(gPointSampler, pIn.texC, 0).rgb / M_PI;
    return float4(irradiance * diffuseBrdf.rgb, 1);
#elif defined(_SPECULAR)
    // TODO
    float3 P = reconstructPositionFromDepth(gDepthTex, gPointSampler, pIn.texC, gInvViewProjMat).xyz;
    float3 Wo = normalize(gCameraPos.xyz - P);

    float3 N = decodeUnitVector(gNormalTex.SampleLevel(gPointSampler, pIn.texC, 0).xy);

    float4 specRough = gSpecRoughTex.SampleLevel(gPointSampler, pIn.texC, 0);
    float3 specularColor = specRough.rgb;
    float linearRoughness = specRough.a;
    float alpha = linearRoughness * linearRoughness;

    return gLightingTex.SampleLevel(gPointSampler, pIn.texC, 0);
#endif
}
