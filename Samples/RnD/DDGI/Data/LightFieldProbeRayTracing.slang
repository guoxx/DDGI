#include "HostDeviceData.h"

__import BRDF;
__import Helpers;
__import LightFieldProbe;

cbuffer PerFrameCB
{
    float4x4 gViewProjMat;
    float4x4 gInvViewProjMat;
    float4 gCameraPos;

    int3 gProbesCount;
    float gDummy0;
    float3 gProbeStartPosition;
    float gDummy1;
    float3 gProbeStep;
    float gFrameCount;
    float2 gSizeHighRes;
    float2 gSizeLowRes;
};

Texture2DArray gOctRadianceTex;
Texture2DArray gOctNormalTex;
Texture2DArray gOctDistanceTex;
Texture2DArray gOctLowResDistanceTex;
Texture2DArray gIrradianceTex;
Texture2DArray gDistanceMomentsTex;
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

    float3 P = reconstructPositionFromDepth(gDepthTex, gPointSampler, pIn.texC, gInvViewProjMat).xyz;
    float3 Wo = normalize(gCameraPos.xyz - P);

    float3 N = decodeUnitVector(gNormalTex.SampleLevel(gPointSampler, pIn.texC, 0).xy);

    float4 specRough = gSpecRoughTex.SampleLevel(gPointSampler, pIn.texC, 0);
    float3 specularColor = specRough.rgb;
    float linearRoughness = specRough.a;
    float alpha = linearRoughness * linearRoughness;

    uint rndSeed = rand_init(asuint(pIn.pos.x * gFrameCount), asuint(pIn.pos.y * gFrameCount));
    float3 result = 0;
    int sampleCnt = 1;
    for (int i = 0; i < sampleCnt; ++i)
    {

    float2 rndVal = float2(rand_next(rndSeed), rand_next(rndSeed));

#if defined(_SPECULAR)
    float3 Wh = getGGXMicrofacet(rndVal, N, alpha);
    const float3 Wi = reflect(-Wo, Wh);
#elif defined(_DIFFUSE)
    const float3 Wi = getCosHemisphereSample(rndVal, N, getPerpendicularStark(N));
#endif

    if (dot(Wi, N) <= 0)
    {
        continue;
    }

    //float3 Wi = reflect(-V, N);
    Ray reflectRay = {P+Wi*0.02, Wi};

    LightFieldSurface lightFieldSurf;
    lightFieldSurf.radianceProbeGrid = gOctRadianceTex;
    lightFieldSurf.normalProbeGrid = gOctNormalTex;
    lightFieldSurf.distanceProbeGrid = gOctDistanceTex;
    lightFieldSurf.lowResolutionDistanceProbeGrid = gOctLowResDistanceTex;
    lightFieldSurf.pointSampler = gPointSampler;
    lightFieldSurf.linearSampler = gLinearSampler;
    lightFieldSurf.probeCounts = gProbesCount;
    lightFieldSurf.probeStartPosition = gProbeStartPosition;
    lightFieldSurf.probeStep = gProbeStep;
    lightFieldSurf.sizeHighRes = gSizeHighRes;
    lightFieldSurf.sizeLowRes = gSizeLowRes;
    lightFieldSurf.invSizeHighRes = 1.0/lightFieldSurf.sizeHighRes;
    lightFieldSurf.invSizeLowRes = 1.0/lightFieldSurf.sizeLowRes;

    lightFieldSurf.irradianceProbeGrid = gIrradianceTex;
    lightFieldSurf.meanDistProbeGrid = gDistanceMomentsTex;

    Ray worldSpaceRay = reflectRay;

    float3 Li = 0;

#if 1
    bool fillHoles = false;
    float hitDistance = 10000;
    float2 hitProbeTexCoord = 0;
    int probeIndex;
    if (trace(lightFieldSurf, worldSpaceRay, hitDistance, hitProbeTexCoord, probeIndex, fillHoles))
    {
        Li = lightFieldSurf.radianceProbeGrid.SampleLevel(gLinearSampler, float3(hitProbeTexCoord, probeIndex), 0).rgb;
    }
#else
    int probeIdx = 1;
    float tMin = 0;
    float tMax = 10000;
    float2 hitProbeTexCoord = 0;
    float3 probeOrigin = probeLocation(lightFieldSurf, probeIdx);
    float3 dir = normalize(P - probeOrigin);
    //return lightFieldSurf.radianceProbeGrid.SampleLevel(gPointSampler, float3(OctToUv(octEncode(dir)), probeIdx), 0);

    //return float4(probeOrigin, 1);
    int traceResult = traceOneProbeOct(lightFieldSurf, probeIdx, worldSpaceRay, tMin, tMax, hitProbeTexCoord);
    if (traceResult == 1)
    {
        Li = lightFieldSurf.radianceProbeGrid.SampleLevel(gPointSampler, float3(hitProbeTexCoord, probeIdx), 0).rgb;
    }
#endif

    float NdotL = saturate(dot(N, Wi));
    float NdotV = saturate(dot(N, Wo));

#if defined(_SPECULAR)
    float NdotH = saturate(dot(N, Wh));
    float LdotH = saturate(dot(Wi, Wh));

    // Probability in terms of Wi vector, NDF term was cancelled by specular BRDF
    float pdf = NdotH / (4 * LdotH);

    float G = evalSmithGGX(NdotL, NdotV, alpha);
    float3 F = fresnelSchlick(specularColor, 1, LdotH);

    // 4 * NdotL * NdotV cancelled by G term
    // NDF cancelled by pdf
    float3 specularBrdf = G * F;

    result += Li * specularBrdf * NdotL / pdf;
#elif defined(_DIFFUSE)
#if 1
    // NdotL cancelled by pdf
    result += Li * M_PI;
#else
    float3 tmp = computePrefilteredIrradiance(lightFieldSurf, P, N);
    result += tmp/2/M_PI;
#endif
#endif
    //result += Li * NdotL;
    }

    return float4(result/sampleCnt, 1.0);
}
