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
__import Helpers;


struct GBufferData
{
    float3 posW;
    float depth;
    float3 normW;
    float3 bitangentW;
    float3 diffuse;
    float opacity;
    float3 specular;
    float linearRoughness;
    float3 emissive;
    float2 motionVec;
};

struct GBufferOut
{
    float4 rt0 : SV_TARGET0;
    float4 rt1 : SV_TARGET1;
    float4 rt2 : SV_TARGET2;
    float4 rt3 : SV_TARGET3;
    float4 rt4 : SV_TARGET4;

    float4 svgfMotionVec        : SV_TARGET5; // .rg motion vector, .b position derivative, .a normal derivative
    float4 svgfLinearZ          : SV_TARGET6; // .r linear Z, .g max Z derivative, .b last frame Z, .a obj space normal
    float4 svgfCompactNormDepth : SV_TARGET7; // .r world normal, .g linear Z, .b Z derivative
};

GBufferData GBufferDecode(Texture2D RTs[8], Texture2D depthTex, SamplerState sampler, float2 uv, float4x4 matInvViewProj)
{
    GBufferData d;

    d.depth = depthTex.SampleLevel(sampler, uv, 0).r;
    d.posW = reconstructPositionFromDepth(d.depth, uv, matInvViewProj).xyz;

    float4 tmp = RTs[3].SampleLevel(sampler, uv, 0);
    d.normW = decodeUnitVector(tmp.xy);
    d.bitangentW = decodeUnitVector(tmp.zw);

    tmp = RTs[0].SampleLevel(sampler, uv, 0);
    d.diffuse = tmp.xyz;
    d.opacity = tmp.w;

    tmp = RTs[1].SampleLevel(sampler, uv, 0);
    d.specular = tmp.xyz;
    d.linearRoughness = tmp.w;

    tmp = RTs[2].SampleLevel(sampler, uv, 0);
    d.emissive = tmp.xyz;

    tmp = RTs[4].SampleLevel(sampler, uv, 0);
    d.motionVec = tmp.xy;

    return d;
}

GBufferOut GBufferEncode(GBufferData d)
{
    GBufferOut gOut;

    gOut.rt3.xy = encodeUnitVector(d.normW);
    gOut.rt3.zw = encodeUnitVector(d.bitangentW);

    gOut.rt0.xyz = d.diffuse;
    gOut.rt0.w = d.opacity;
    gOut.rt1.xyz = d.specular;
    gOut.rt1.w = d.linearRoughness;
    gOut.rt2.xyz = d.emissive;
    gOut.rt2.w = 0;
    gOut.rt4.xy = d.motionVec;
    gOut.rt4.zw = 0;

    return gOut;
}
