/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
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
import ShaderCommon;
import Shading;
import Helpers;
import GGXGICommon;
import GBuffer;

[shader("raygeneration")]
void GGXGlobalIllumRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // Initialize our random number generator
    uint randSeed = rand_init(launchIndex.x + launchIndex.y * launchDim.x, gFrameCount, 16);

    // Load g-buffer data
    GBufferData data = GBufferDecode(gGbufferRT, gDepthTex, gSampler, float2(launchIndex)/launchDim, gCamera.invViewProj);
    float4 posW = float4(data.posW, 1);
    float3 normW = data.normW;
    float3 diffuse = data.diffuse;
    float4 specRough = float4(data.specular, data.linearRoughness);
    float3 emissive = data.emissive;

    // Does this g-buffer pixel contain a valid piece of geometry?  (0 in pos.w for invalid)
    bool isGeometryValid = (posW.w != 0.0f);

    // Extract and compute some material and geometric parameters
    float roughness = specRough.a * specRough.a;
    float3 V = normalize(gCamera.posW - posW.xyz);

    // Make sure our normal is pointed the right direction
    if (dot(normW, V) <= 0.0f) normW = -normW;

    float3 shadeColor = float3(0, 0, 0);

    // Do shading, if we have geometry here (otherwise, output the background color)
    if (isGeometryValid)
    {
        // Add any emissive color from primary rays
        shadeColor = gEmitMult * emissive.rgb;

        // Fill out ShadingData struct with GBuffer data
        ShadingData sd = initShadingData();
        sd.posW = posW.xyz;
        sd.N = normW;
        sd.V = V;
        sd.diffuse = diffuse.rgb;
        sd.specular = specRough.rgb;
        sd.linearRoughness = specRough.a;
        sd.roughness = roughness;

        // Direct lighting to a random light in the scene
        if (gDoDirectGI) shadeColor += ggxDirect(randSeed, sd);

        // Indirect lighting for global illumination
        if (gDoIndirectGI && (gMaxDepth > 0)) shadeColor += ggxIndirect(randSeed, sd, normW);

        // Zero out any bad samples
        bool colorsNan = any(isnan(shadeColor));
        shadeColor = colorsNan ? float3(0, 0, 0) : shadeColor;
    }
    
    gOutput[launchIndex] = float4(shadeColor, 1.0f);
}
