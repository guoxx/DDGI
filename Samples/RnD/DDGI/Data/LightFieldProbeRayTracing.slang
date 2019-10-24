#include "HostDeviceData.h"

__import Octahedral;
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
    float gDummy2;
    float2 gSizeHighRes;
    float2 gSizeLowRes;
};

Texture2DArray gOctRadianceTex;
Texture2DArray gOctNormalTex;
Texture2DArray gOctDistanceTex;
Texture2DArray gOctLowResDistanceTex;
Texture2D gNormalTex;
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

struct BoundaryBox
{
    float3 bbMin;
    float3 bbMax;
    float3 bbCenter;
};

struct Intersection
{
    bool hit;
    float3 P;
};

Intersection boxIntersect(in Ray ray, in BoundaryBox bbox)
{
    float3 t0 = (bbox.bbMin - ray.origin) / ray.direction;
    float3 t1 = (bbox.bbMax - ray.origin) / ray.direction;
    float3 tmp = max(t0, t1);
    float t = min(min(tmp.x, tmp.y), tmp.z);

    Intersection its;
    its.hit = t > 0;
    its.P = ray.origin + ray.direction * t;
    return its;
}

float4 PSMain(VsOut pIn) : SV_TARGET0
{
    if (gDepthTex.SampleLevel(gPointSampler, pIn.texC, 0).r >= 1.0)
        return 0;

    float3 P = reconstructPositionFromDepth(gDepthTex, gPointSampler, pIn.texC, gInvViewProjMat).xyz;
    float3 V = normalize(gCameraPos.xyz - P);

    float3 N = gNormalTex.SampleLevel(gPointSampler, pIn.texC, 0).xyz * 2 - 1;

    float3 Wi = reflect(-V, N);
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

    Ray worldSpaceRay = reflectRay;

#if 1
    bool fillHoles = false;
    float hitDistance = 10000;
    float2 hitProbeTexCoord = 0;
    int probeIndex;
    if (trace(lightFieldSurf, worldSpaceRay, hitDistance, hitProbeTexCoord, probeIndex, fillHoles))
    {
        return lightFieldSurf.radianceProbeGrid.SampleLevel(gLinearSampler, float3(hitProbeTexCoord, probeIndex), 0);
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
        return lightFieldSurf.radianceProbeGrid.SampleLevel(gPointSampler, float3(hitProbeTexCoord, probeIdx), 0);
    }
#endif
    return float4(0, 0,0,1);

    BoundaryBox bbox;
    float3 center = {-0.875000000, 1.22500002, 0.875000000 };
    float3 extent = {1.87500000, 1.27499998, 1.87500000};
    bbox.bbMin = center - extent;
    bbox.bbMax = center + extent;
    bbox.bbCenter = (bbox.bbMin + bbox.bbMax) / 2;

    Intersection its = boxIntersect(reflectRay, bbox);
    if (its.hit)
    {
        float4 projP = mul(float4(its.P, 1), gViewProjMat);
        projP /= projP.w;
        float3 N_prime = gNormalTex.SampleLevel(gPointSampler, NdcToUv(projP.xy), 0).xyz * 2 - 1;

        float3 dir = its.P - bbox.bbCenter;
        dir = normalize(dir);
        float2 uv = OctToUv(octEncode(dir));
        //if (dot(N_prime, reflectRay.dir) < 0)
        {
            float3 result = gOctRadianceTex.SampleLevel(gPointSampler, float3(uv, 0), 0).xyz;
            return float4(result, 1);
        }
    }

    return float4(1, 0, 0, 1);
}
