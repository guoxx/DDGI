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
import Shading;
import DefaultVS;
import GBuffer;
import Helpers;

cbuffer PerFrameCB
{
    float2 gRenderTargetDim;
};

/** Entry point for G-buffer rasterization pixel shader.
*/
GBufferOut ps(VertexOut vsOut, float4 pixelCrd : SV_POSITION)
{
    ShadingData sd = prepareShadingData(vsOut, gMaterial, gCamera.posW);

    GBufferData data;
    data.posW = sd.posW;
    data.depth = pixelCrd.z;
    data.normW = sd.N;
    data.bitangentW = sd.B;
    data.diffuse = sd.diffuse;
    data.opacity = sd.opacity;
    data.specular = sd.specular;
    data.linearRoughness = sd.linearRoughness;
    data.emissive = sd.emissive;

    // Remove jitter from motion vector. See "Temporal Antialiasing in Uncharted 4" P27
    // http://advances.realtimerendering.com/s2016/index.html
#if 1
    float4 prevPosH = vsOut.prevPosH;
    prevPosH.xy += prevPosH.w * 2 * float2(gCamera.jitterX, gCamera.jitterY);
    data.motionVec = calcMotionVector(pixelCrd.xy, prevPosH, gRenderTargetDim);
#else
    float2 jitterOffset = float2(gCamera.jitterX, gCamera.jitterY);
    #ifdef FALCOR_D3D
        jitterOffset.y *= -1;
    #endif
    data.motionVec = calcMotionVector(pixelCrd.xy, vsOut.prevPosH, gRenderTargetDim) + jitterOffset;
#endif

    GBufferOut psOut = GBufferEncode(data);

    float2 posNormFWidth = float2(length(fwidth(sd.posW)), length(fwidth(sd.N))); 
    psOut.svgfMotionVec = float4(data.motionVec.xy, posNormFWidth);

    // The 'linearZ' buffer
    float linearZ    = vsOut.posH.z * vsOut.posH.w;
#ifdef FALCOR_VK
    float prevZBuf   = vsOut.prevPosH.z / vsOut.prevPosH.w;
    float prevLinearZ = prevZBuf / vsOut.prevPosH.w;
#else
    float prevLinearZ = vsOut.prevPosH.z;
#endif
    float maxChangeZ = max(abs(ddx(linearZ)), abs(ddy(linearZ)));
    float objNorm    = asfloat(DirToOct(normalize(vsOut.normalW))); // world normal instead of object normal
    psOut.svgfLinearZ = float4(linearZ, maxChangeZ, prevLinearZ, objNorm);

    // A compacted buffer containing discretizied normal, depth, depth derivative
    psOut.svgfCompactNormDepth = float4(asfloat(DirToOct(sd.N)), linearZ, maxChangeZ, 0.0f);

    return psOut;
}
