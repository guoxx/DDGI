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
#include "Framework.h"
#include "Experimental/RenderPasses/GBuffer.h"
#include "LightFieldProbeShading.h"

namespace
{
    const char kShaderFilename[] = "LightFieldProbeShading.slang";
}

RenderPassReflection LightFieldProbeShading::reflect() const
{
    RenderPassReflection r;
    for (int i = 0; i < kGBufferChannelDesc.size(); ++i)
    {
        r.addInput(kGBufferChannelDesc[i].name, kGBufferChannelDesc[i].desc);
    }
    r.addInput("depthStencil", "depth and stencil");

    r.addOutput("output", "").format(ResourceFormat::RGBA16Float).bindFlags(Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess | Resource::BindFlags::RenderTarget);

    return r;
}

bool LightFieldProbeShading::parseDictionary(const Dictionary& dict)
{
    for (const auto& v : dict)
    {
        logWarning("Unknown field `" + v.key() + "` in a GBufferShading dictionary");
    }
    return true;
}

LightFieldProbeShading::SharedPtr LightFieldProbeShading::create(const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new LightFieldProbeShading);
    return pPass->parseDictionary(dict) ? pPass : nullptr;
}

Dictionary LightFieldProbeShading::getScriptingDictionary() const
{
    Dictionary dict;
    return dict;
}

LightFieldProbeShading::LightFieldProbeShading() : RenderPass("GBufferShading")
{
    GraphicsProgram::Desc d;
    d.addShaderLibrary(kShaderFilename).vsEntry("VSMain").psEntry("PSMain");
    mpProgram = GraphicsProgram::create(d);

    ProgramReflection::SharedConstPtr pReflector = mpProgram->getReflector();
    mpVars = GraphicsVars::create(pReflector);
    mpInternalPerFrameCB = mpVars->getConstantBuffer("InternalPerFrameCB");

    const ReflectionVar* pCbReflVar = pReflector->getResource("InternalPerFrameCB").get();
    const ReflectionType* pCbReflType = pCbReflVar->getType().get();
    mOffsetInCB.cameraDataOffset = (int32_t)pCbReflType->findMember("gCamera.viewMat")->getOffset();
    const auto& pCountOffset = pCbReflType->findMember("gLightsCount");
    mOffsetInCB.lightCountOffset = pCountOffset ? (int32_t)pCountOffset->getOffset() : ConstantBuffer::kInvalidOffset;
    const auto& pLightOffset = pCbReflType->findMember("gLights");
    mOffsetInCB.lightArrayOffset = pLightOffset ? (int32_t)pLightOffset->getOffset() : ConstantBuffer::kInvalidOffset;

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

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    Sampler::SharedPtr pPointSampler = Sampler::create(samplerDesc);
    mpVars->setSampler("gPointSampler", pPointSampler);

    const ParameterBlockReflection* pParamBlockReflector = pReflector->getDefaultParameterBlock().get();
    mBindLocations.gbufferRT = pParamBlockReflector->getResourceBinding("gGbufferRT");
    mBindLocations.depthTex = pParamBlockReflector->getResourceBinding("gDepthTex");
    mBindLocations.visibilityBuffer = pParamBlockReflector->getResourceBinding("visibilityBuffer");
}

void LightFieldProbeShading::onResize(uint32_t width, uint32_t height)
{
}

void LightFieldProbeShading::setScene(const Scene::SharedPtr& pScene)
{
    mpScene = pScene;
}

void LightFieldProbeShading::renderUI(Gui* pGui, const char* uiGroup)
{
}

void LightFieldProbeShading::execute(RenderContext* pContext, const Fbo::SharedPtr& pGBufferFbo, Texture::SharedPtr visibilityTexture, const Fbo::SharedPtr& pTargetFbo)
{
    setVarsData(pGBufferFbo, visibilityTexture);

    mpState->pushFbo(pTargetFbo);

    pContext->pushGraphicsState(mpState);
    pContext->pushGraphicsVars(mpVars);

    pContext->drawIndexed(3, 0, 0);

    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    mpState->popFbo();    
}

void LightFieldProbeShading::execute(RenderContext* pContext, const RenderData* pRenderData)
{
    Fbo::SharedPtr pGBufferFbo = Fbo::create();
    pGBufferFbo->attachDepthStencilTarget(pRenderData->getTexture("depthStencil"));
    for (int i = 0; i < kGBufferChannelDesc.size(); ++i)
    {
        pGBufferFbo->attachColorTarget(pRenderData->getTexture(kGBufferChannelDesc[i].name), i);
    }

    Fbo::SharedPtr pTargetFbo = Fbo::create();
    pTargetFbo->attachColorTarget(pRenderData->getTexture("output"), 0);
    execute(pContext, pGBufferFbo, pRenderData->getTexture("visibilityBuffer"), pTargetFbo);
}

void LightFieldProbeShading::setVarsData(const Fbo::SharedPtr& pGBufferFbo, Texture::SharedPtr visibilityTexture)
{
    // SRV binding
    ParameterBlock* pDefaultBlock = mpVars->getDefaultBlock().get();
    for (int i = 0; i < kGBufferChannelDesc.size(); ++i)
    {
        pDefaultBlock->setSrv(mBindLocations.gbufferRT, i, pGBufferFbo->getColorTexture(i)->getSRV());
    }
    pDefaultBlock->setSrv(mBindLocations.depthTex, 0, pGBufferFbo->getDepthStencilTexture()->getSRV());
    pDefaultBlock->setSrv(mBindLocations.visibilityBuffer, 0, visibilityTexture->getSRV());

    // InternalPerFrameCB update
    // Set camera
    mpCamera->setIntoConstantBuffer(mpInternalPerFrameCB.get(), mOffsetInCB.cameraDataOffset);

    // Set lights
    if (mOffsetInCB.lightArrayOffset != ConstantBuffer::kInvalidOffset)
    {
        assert(mpScene->getLightCount() <= MAX_LIGHT_SOURCES);  // Max array size in the shader
        for (uint32_t i = 0; i < mpScene->getLightCount(); i++)
        {
            mpScene->getLight(i)->setIntoProgramVars(mpVars.get(),
                mpInternalPerFrameCB.get(),
                mOffsetInCB.lightArrayOffset + (i * Light::getShaderStructSize()));
        }
    }
    if (mOffsetInCB.lightCountOffset != ConstantBuffer::kInvalidOffset)
    {
        mpInternalPerFrameCB->setVariable(mOffsetInCB.lightCountOffset, mpScene->getLightCount());
    }

    if (mpScene->getLightProbeCount() > 0)
    {
        assert(false);
    }
}
