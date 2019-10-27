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
    data.motionVec = calcMotionVector(pixelCrd.xy, vsOut.prevPosH, gRenderTargetDim);
    return GBufferEncode(data);
}
