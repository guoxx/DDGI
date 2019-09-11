__import Helpers;

cbuffer PerFrameCB
{
    CameraData gCamera;
    float4 gUvScaleOffset;
};

Texture2D gDepthTex;
SamplerState gPointSampler;

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
    float2 uvInPrevMip = pIn.texC * gUvScaleOffset.xy + gUvScaleOffset.zw;
#ifdef _FIRST_ITER
    float depthVal = gDepthTex.SampleLevel(gPointSampler, uvInPrevMip, 0).r;
    if (depthVal == 1.0)
        return -gCamera.farZ;
    else
        return depthToLinearZ(depthVal, gCamera.projMat);
#else
    float4 depthMaxVals = gDepthTex.GatherRed(gPointSampler, uvInPrevMip, 0);
    float4 depthMinVals = gDepthTex.GatherGreen(gPointSampler, uvInPrevMip, 0);
    float zMax = max(max(depthMaxVals.x, depthMaxVals.y), max(depthMaxVals.z, depthMaxVals.w));
    float zMin = min(min(depthMinVals.x, depthMinVals.y), min(depthMinVals.z, depthMinVals.w));
    return float4(zMax, zMin, 0, 1);
#endif
}
