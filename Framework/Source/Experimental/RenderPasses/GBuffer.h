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
#pragma once
#include "Falcor.h"

// GBuffer channel metadata
struct GBufferChannelDesc
{
    const char*   name;     // Canonical channel name
    const char*   desc;     // Human-readable channel description
    const char*   texname;  // Name of corresponding ITexture2D in GBufferRT shader code
};

// Keep same order as channel description showed below
enum GBufferRT
{
    DIFFUSE_OPACITY     = 0,
    SPECULAR_ROUGHNESS  = 1,
    EMISSIVE            = 2,
    NORMAL_BITANGENT    = 3,
    MOTION_VECTOR       = 4,

    SVGF_MotionVec          = 5,
    SVGF_LinearZ            = 6,
    SVGF_CompactNormDepth   = 7,
};

// Note that channel order should correspond to SV_TARGET index order used in
// GBufferRaster's primary fragment shader.
static const std::vector<GBufferChannelDesc> kGBufferChannelDesc({
        {"rt0",             "Render target 0",          ""},
        {"rt1",             "Render target 1",          ""},
        {"rt2",             "Render target 2",          ""},
        {"rt3",             "Render target 3",          ""},
        {"rt4",             "Render target 4",          ""},
        {"svgfMotionVec",           "Render target 5",          ""},
        {"svgfLinearZ",             "Render target 6",          ""},
        {"svgfCompactNormDepth",    "Render target 7",          ""},
        });


// Culling dictionary key and dropdown mode selection
static const std::string kCull = "cull";

static const Falcor::Gui::DropdownList kCullModeList =
{
    { (uint32_t)Falcor::RasterizerState::CullMode::None, "None" },
    { (uint32_t)Falcor::RasterizerState::CullMode::Back, "Back" },
    { (uint32_t)Falcor::RasterizerState::CullMode::Front, "Front" },
};

