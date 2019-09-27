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
#include "OctahedralMapping.h"

namespace
{
    const char kShaderFilename[] = "OctahedralMapping.slang";
}

OctahedralMapping::SharedPtr OctahedralMapping::create(const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new OctahedralMapping);
    return pPass;
}

OctahedralMapping::OctahedralMapping() : RenderPass("OctahedralMapping")
{
    GraphicsProgram::Desc d;
    d.setCompilerFlags(Shader::CompilerFlags::EmitDebugInfo);
    d.addShaderLibrary(kShaderFilename).vsEntry("VSMain").psEntry("PSMain");
    mpProgram = GraphicsProgram::create(d);

    ProgramReflection::SharedConstPtr pReflector = mpProgram->getReflector();
    mpVars = GraphicsVars::create(pReflector);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    Sampler::SharedPtr pLinearSampler = Sampler::create(samplerDesc);
    mpVars->setSampler("gLinearSampler", pLinearSampler);

    // Initialize graphics state
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(false).setDepthWriteMask(false);
    DepthStencilState::SharedPtr pDepthStencilState = DepthStencilState::create(dsDesc);

    const uint32_t indices[] = { 0, 1, 2 };
    Buffer::SharedPtr pIB = Buffer::create(sizeof(indices), Buffer::BindFlags::Index, Buffer::CpuAccess::None, (void*)indices);
    Vao::SharedPtr pVao = Vao::create(Vao::Topology::TriangleStrip, nullptr, Vao::BufferVec(), pIB, ResourceFormat::R32Uint);

    mpState = GraphicsState::create();
    mpState->setDepthStencilState(pDepthStencilState);
    mpState->setVao(pVao);
    mpState->setProgram(mpProgram);
}

void OctahedralMapping::execute(RenderContext* pContext,
                                const Texture::SharedPtr& pFacePosX, const Texture::SharedPtr pFaceNegX,
                                const Texture::SharedPtr& pFacePosY, const Texture::SharedPtr pFaceNegY,
                                const Texture::SharedPtr& pFacePosZ, const Texture::SharedPtr pFaceNegZ,
                                const Fbo::SharedPtr& pTargetFbo)
{
    mpVars->setTexture("gFacePosX", pFacePosX);
    mpVars->setTexture("gFaceNegX", pFaceNegX);
    mpVars->setTexture("gFacePosY", pFacePosY);
    mpVars->setTexture("gFaceNegY", pFaceNegY);
    mpVars->setTexture("gFacePosZ", pFacePosZ);
    mpVars->setTexture("gFaceNegZ", pFaceNegZ);

    mpState->pushFbo(pTargetFbo);

    pContext->pushGraphicsState(mpState);
    pContext->pushGraphicsVars(mpVars);

    pContext->drawIndexed(3, 0, 0);

    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    mpState->popFbo();    
}

RenderPassReflection OctahedralMapping::reflect() const
{
    should_not_get_here();
    RenderPassReflection r;
    return r;
}

void OctahedralMapping::execute(RenderContext* pContext, const RenderData* pRenderData)
{
    should_not_get_here();
}

