/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
__import ShaderCommon;
__import Shading;
__import Helpers;
__import BRDF;


Texture2D gPosTex;
Texture2D gNormTex;
Texture2D gBitangentTex;
Texture2D gTexCoordTex;
Texture2D gDiffuseOpacityTex;
Texture2D gSpecRoughTex;
Texture2D gEmissiveTex;
Texture2D gMatlExtraTex;

Texture2D visibilityBuffer;

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
    float4 pixelCrd = pIn.pos;

    // Load g-buffer data
    float4 posW = gPosTex[pixelCrd.xy];
    float3 normW = gNormTex[pixelCrd.xy].xyz * 2 - 1;
    float4 diffuse = gDiffuseOpacityTex[pixelCrd.xy];
    float4 specRough = gSpecRoughTex[pixelCrd.xy];
    float4 emissive = gEmissiveTex[pixelCrd.xy];
    float3 geomN = gMatlExtraTex[pixelCrd.xy].xyz * 2 - 1;

    // Does this g-buffer pixel contain a valid piece of geometry?  (0 in pos.w for invalid)
    bool isGeometryValid = (posW.w != 0.0f);
    if (!isGeometryValid)
        return float4(0, 0, 0, 1);

    // Extract and compute some material and geometric parameters
    float roughness = specRough.a * specRough.a;
    float3 V = normalize(gCamera.posW - posW.xyz);

    // Make sure our normal is pointed the right direction
    if (dot(normW, V) <= 0.0f) normW = -normW;

    // Make sure out geometric normal is pointed in the right direction
    if (dot(geomN, V) <= 0.0f) geomN = -geomN;

    // Fill out ShadingData struct with GBuffer data
    ShadingData sd = initShadingData();
    sd.posW = posW.xyz;
    sd.N = normW;
    sd.V = V;
    sd.NdotV = dot(sd.N, sd.V);
    sd.diffuse = diffuse.rgb;
    sd.specular = specRough.rgb;
    sd.linearRoughness = specRough.a;
    sd.roughness = roughness;
    sd.emissive = emissive.xyz;

    float4 finalColor = float4(0, 0, 0, 1);

    for (uint l = 0; l < gLightsCount; l++)
    {
        float shadowFactor = 1;
        if (l == 0)
        {
            shadowFactor = visibilityBuffer.Load(int3(pixelCrd.xy, 0)).r;
            shadowFactor *= sd.opacity;
        }
        finalColor.rgb += evalMaterial(sd, gLights[l], shadowFactor).color.rgb;
    }

    // Add the emissive component
    finalColor.rgb += sd.emissive;
    finalColor.a = sd.opacity;
    finalColor.rgb += evalMaterial(sd, gLightProbe).color.rgb;

    // Add light-map
    finalColor.rgb += sd.diffuse * sd.lightMap.rgb;

    return finalColor;
}
