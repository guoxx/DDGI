#include "HostDeviceData.h"

__import Helpers;

Texture2DArray gRadianceTex;
Texture2DArray gDistanceTex;

SamplerState gPointSampler;
SamplerState gLinearSampler;

cbuffer PerPassCB
{
    int gArrayIndex;
    int gSampleCount;
    float gFrameCount;
	// Exponent for depth testing. A high value will rapidly react to depth discontinuities, but risks exhibiting banding.
    float gDepthSharpness;
}

struct VsOut
{
    float2 texC : TEXCOORD;
    float4 pos : SV_POSITION;
};

struct PsOut
{
    float4 irradiance : SV_TARGET0;
    float2 distMoments : SV_TARGET1;
}

VsOut VSMain(uint id: SV_VertexID)
{
    VsOut vOut; 
	vOut.texC = float2((id & 0x02) * 1.0, (id & 0x01) * 2.0);
	vOut.pos = float4(vOut.texC * float2(2, -2) + float2(-1, 1), 0, 1);
    return vOut;
}

PsOut PSMain(VsOut pIn) : SV_TARGET0
{
    PsOut psOut;
    psOut.irradiance = 0;
    psOut.distMoments = 0;

    uint rndSeed = rand_init(asuint(pIn.pos.x * gFrameCount), asuint(pIn.pos.y * gFrameCount));

    //float3 N = sphericalCrdToDir(pIn.texC);
    float3 N = octDecode(UvToOct(pIn.texC));
    float3 T = getPerpendicularStark(N);
    float3 B = normalize(cross(N, T));

    float distWeightSum = 0.0;
#if 1
    int kNumThetaSamples = 16;
    int kNumPhiSamples = 64;
    float kDeltaTheta = M_PI/2/(kNumThetaSamples);
    float kDeltaPhi = M_PI*2/(kNumPhiSamples);

    float theta = 0;
    float phi = 0;
    for (int i = 0; i < kNumThetaSamples; ++i)
    {
        theta = kDeltaTheta*i;

        for (int j = 0; j < kNumPhiSamples; ++j)
        {
            phi = kDeltaPhi*j;

            float cosTheta = cos(theta);
            float sinTheta = sin(theta);
            float3 L = float3(sinTheta * cos(phi),
                              sinTheta * sin(phi),
                              cosTheta);

            float3 Wi = normalize(T * L.x + B * L.y + N * L.z);

            float2 uv = OctToUv(octEncode(Wi));
            float3 radiance = gRadianceTex.SampleLevel(gLinearSampler, float3(uv, gArrayIndex), 0).rgb;
            psOut.irradiance.xyz += radiance*cosTheta*sinTheta;

            float distWeight = pow(cosTheta, gDepthSharpness);
            psOut.distMoments.x += gDistanceTex.SampleLevel(gPointSampler, float3(uv, gArrayIndex), 0).x * distWeight;
            distWeightSum += distWeight;
        }
    }
    psOut.irradiance.xyz *= (kDeltaTheta*kDeltaPhi);

#else
    int sampleCnt = gSampleCount;
    for (int i = 0; i < sampleCnt; ++i)
    {
        float2 rndVal = float2(rand_next(rndSeed), rand_next(rndSeed));
        const float3 Wi = getCosHemisphereSample(rndVal, N, T);
        float2 uv = OctToUv(octEncode(Wi));
        float3 radiance = gRadianceTex.SampleLevel(gLinearSampler, float3(uv, gArrayIndex), 0).rgb;
        psOut.irradiance.xyz += radiance;

        float distWeight = pow(max(0, dot(Wi, N)), gDepthSharpness);
        psOut.distMoments.x += gDistanceTex.SampleLevel(gPointSampler, float3(uv, gArrayIndex), 0).x * distWeight;
        distWeightSum += distWeight;
    }
    // normalization
    psOut.irradiance *= M_PI;
    psOut.irradiance /= sampleCnt;
#endif

    psOut.distMoments.x /= distWeightSum;
    psOut.distMoments.y = psOut.distMoments.x * psOut.distMoments.x;

    return psOut;
}
