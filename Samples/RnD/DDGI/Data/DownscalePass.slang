Texture2D gSrcTex;
SamplerState gPointSampler;

cbuffer PerFrameCB
{
    float2 gSizeHighRes;
    float2 gSizeLowRes;
    float2 gDownsampleFactor;
};

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
    float2 stLowRes = floor(pIn.texC * gSizeLowRes);
    float2 stHighRes = stLowRes * gDownsampleFactor;

    float4 ret = 1e6;
    for (int dT = 0; dT < gDownsampleFactor.y; ++dT)
    {
        for (int dS = 0; dS < gDownsampleFactor.x; ++dS)
        {
            float2 uv = (stHighRes + float2(dS, dT)) / gSizeHighRes;
            float4 tmp = gSrcTex.SampleLevel(gPointSampler, uv, 0);
            ret = min(ret, tmp);
        }
    }
    return ret;
}
