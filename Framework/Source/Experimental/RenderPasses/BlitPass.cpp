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
#include "BlitPass.h"
#include "API/RenderContext.h"
#include "API/BlendState.h"
#include "Utils/Gui.h"

namespace Falcor
{
    const char* BlitPass::kDesc = "Blit a texture into a different texture";

    static const std::string kDst = "dst";
    static const std::string kSrc = "src";
    static const std::string kFilter = "filter";

    RenderPassReflection BlitPass::reflect() const
    {
        RenderPassReflection reflector;

        reflector.addOutput(kDst, "The destination texture");
        reflector.addInput(kSrc, "The source texture");

        return reflector;
    }

    static bool parseDictionary(BlitPass* pPass, const Dictionary& dict)
    {
        for (const auto& v : dict)
        {
            if (v.key() == kFilter)
            {
                Sampler::Filter f = (Sampler::Filter)v.val();
                pPass->setFilter(f);
            }
            else
            {
                logWarning("Unknown field `" + v.key() + "` in a BlitPass dictionary");
            }
        }
        return true;
    }

    BlitPass::SharedPtr BlitPass::create(const Dictionary& dict)
    {
        SharedPtr pPass = SharedPtr(new BlitPass);
        parseDictionary(pPass.get(), dict);
        pPass->init();
        return pPass;
    }

    Dictionary BlitPass::getScriptingDictionary() const
    {
        Dictionary dict;
        dict[kFilter] = mFilter;
        return dict;
    }

    void BlitPass::init()
    {
        mpPass = FullScreenPass::create("Framework/Shaders/Blit.vs.slang", "Framework/Shaders/Blit.ps.slang");
        mpVars = GraphicsVars::create(mpPass->getProgram()->getReflector());
        mpState = GraphicsState::create();

        mpSrcRectBuffer = mpVars->getConstantBuffer("SrcRectCB");
        mOffsetVarOffset = (uint32_t)mpSrcRectBuffer->getVariableOffset("gOffset");
        mScaleVarOffset = (uint32_t)mpSrcRectBuffer->getVariableOffset("gScale");

        Sampler::Desc desc;
        desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Point);
        desc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
        mpLinearSampler = Sampler::create(desc);
        desc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
        desc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
        mpPointSampler = Sampler::create(desc);

        const auto& pDefaultBlockReflection = mpPass->getProgram()->getReflector()->getDefaultParameterBlock();
        mTexBindLoc = pDefaultBlockReflection->getResourceBinding("gTex");
        mSamplerBindLoc = pDefaultBlockReflection->getResourceBinding("gSampler");
    }

    BlitPass::BlitPass() : RenderPass("BlitPass")
    {
    }

    void BlitPass::execute(RenderContext* pContext, Texture::SharedPtr pSrcTex, Texture::SharedPtr pDstTex)
    {
        if (pSrcTex && pDstTex)
        {
            if (mFilter == Sampler::Filter::Linear)
            {
                mpVars->getDefaultBlock()->setSampler(mSamplerBindLoc, 0, mpLinearSampler);
            }
            else
            {
                mpVars->getDefaultBlock()->setSampler(mSamplerBindLoc, 0, mpPointSampler);
            }

            ShaderResourceView::SharedPtr pSrc = pSrcTex->getSRV();
            ShaderResourceView::SharedPtr pDst = pDstTex->getSRV();
            assert(pSrc->getViewInfo().arraySize == 1 && pSrc->getViewInfo().mipCount == 1);
            assert(pDst->getViewInfo().arraySize == 1 && pDst->getViewInfo().mipCount == 1);

            const Texture* pSrcTexture = dynamic_cast<const Texture*>(pSrc->getResource());
            const Texture* pDstTexture = dynamic_cast<const Texture*>(pDst->getResource());
            assert(pSrcTexture != nullptr && pDstTexture != nullptr);

            vec2 srcRectOffset(0.0f);
            vec2 srcRectScale(1.0f);
            uint32_t srcMipLevel = pSrc->getViewInfo().mostDetailedMip;
            uint32_t dstMipLevel = pDst->getViewInfo().mostDetailedMip;
            GraphicsState::Viewport dstViewport(0.0f, 0.0f, (float)pDstTexture->getWidth(dstMipLevel), (float)pDstTexture->getHeight(dstMipLevel), 0.0f, 1.0f);

            // Update buffer/state
            mpSrcRectBuffer->setVariable(mOffsetVarOffset, srcRectOffset);
            mpSrcRectBuffer->setVariable(mScaleVarOffset, srcRectScale);

            mpState->setViewport(0, dstViewport);

            pContext->pushGraphicsState(mpState);
            pContext->pushGraphicsVars(mpVars);

            if (pSrcTexture->getSampleCount() > 1)
            {
                mpPass->getProgram()->addDefine("SAMPLE_COUNT", std::to_string(pSrcTexture->getSampleCount()));
            }
            else
            {
                mpPass->getProgram()->removeDefine("SAMPLE_COUNT");
            }

            Fbo::SharedPtr pFbo = Fbo::create();
            Texture::SharedPtr pSharedTex = std::const_pointer_cast<Texture>(pDstTexture->shared_from_this());
            pFbo->attachColorTarget(pSharedTex, 0, pDst->getViewInfo().mostDetailedMip, pDst->getViewInfo().firstArraySlice, pDst->getViewInfo().arraySize);
            mpState->pushFbo(pFbo, false);
            mpVars->getDefaultBlock()->setSrv(mTexBindLoc, 0, pSrc);
            mpPass->execute(pContext, nullptr, mpBlendState);

            // Release the resources we bound
            mpVars->getDefaultBlock()->setSrv(mTexBindLoc, 0, nullptr);
            mpState->popFbo(false);
            pContext->popGraphicsState();
            pContext->popGraphicsVars();

        }
        else
        {
            logWarning("BlitPass::execute() - missing an input or output resource");
        }
    }

    void BlitPass::execute(RenderContext* pContext, const RenderData* pRenderData)
    {
        const auto& pSrcTex = pRenderData->getTexture(kSrc);
        const auto& pDstTex = pRenderData->getTexture(kDst);

        execute(pContext, pSrcTex, pDstTex);
    }

    void BlitPass::renderUI(Gui* pGui, const char* uiGroup)
    {
        if (!uiGroup || pGui->beginGroup(uiGroup))
        {
            static const Gui::DropdownList kFilterList = 
            {
                { (uint32_t)Sampler::Filter::Linear, "Linear" },
                { (uint32_t)Sampler::Filter::Point, "Point" },
            };

            uint32_t f = (uint32_t)mFilter;
            if (pGui->addDropdown("Filter", kFilterList, f)) setFilter((Sampler::Filter)f);

            if (uiGroup) pGui->endGroup();
        }
    }

    void BlitPass::setBlendParams(uint32_t rtIndex,
                                  BlendState::BlendOp rgbOp, BlendState::BlendOp alphaOp,
                                  BlendState::BlendFunc srcRgbFunc, BlendState::BlendFunc dstRgbFunc,
                                  BlendState::BlendFunc srcAlphaFunc, BlendState::BlendFunc dstAlphaFunc)
    {
        
        mBlendStateDesc.setRtParams(rtIndex, rgbOp, alphaOp, srcRgbFunc, dstRgbFunc, srcAlphaFunc, dstAlphaFunc);
        mpBlendState = BlendState::create(mBlendStateDesc);
    }

    void BlitPass::setEnableBlend(uint32_t rtIndex, bool enable)
    {
        mBlendStateDesc.setRtBlend(rtIndex, enable);
        mpBlendState = BlendState::create(mBlendStateDesc);
    }
}
