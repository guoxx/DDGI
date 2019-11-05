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
#include "Graphics/FullScreenPass.h"
#include "Graphics/Program/ProgramVars.h"
#include "Experimental/RenderGraph/RenderPass.h"
#include "API/Sampler.h"
#include "API/Texture.h"

namespace Falcor
{
    class BlitPass : public RenderPass, inherit_shared_from_this<RenderPass, BlitPass>
    {
    public:
        using SharedPtr = std::shared_ptr<BlitPass>;
        static const char* kDesc;

        /** Create a new object
        */
        static SharedPtr create(const Dictionary& dict = {});

        void execute(RenderContext* pContext, Texture::SharedPtr pSrcTex, Texture::SharedPtr pDstTex);

        virtual RenderPassReflection reflect() const override;
        virtual void execute(RenderContext* pContext, const RenderData* pRenderData) override;
        virtual void renderUI(Gui* pGui, const char* uiGroup) override;
        virtual Dictionary getScriptingDictionary() const override;
        virtual std::string getDesc() override { return kDesc; }

        void setFilter(Sampler::Filter filter) { mFilter = filter; }

        void setBlendParams(uint32_t rtIndex,
                            BlendState::BlendOp rgbOp, BlendState::BlendOp alphaOp,
                            BlendState::BlendFunc srcRgbFunc, BlendState::BlendFunc dstRgbFunc,
                            BlendState::BlendFunc srcAlphaFunc, BlendState::BlendFunc dstAlphaFunc);
        void setEnableBlend(uint32_t rtIndex, bool enable);

    private:
        BlitPass();
        void init();

        Sampler::Filter mFilter = Sampler::Filter::Linear;
        BlendState::Desc mBlendStateDesc;
        BlendState::SharedPtr mpBlendState;

        FullScreenPass::UniquePtr mpPass;
        GraphicsVars::SharedPtr mpVars;
        GraphicsState::SharedPtr mpState;

        Sampler::SharedPtr mpLinearSampler;
        Sampler::SharedPtr mpPointSampler;

        ConstantBuffer::SharedPtr mpSrcRectBuffer;

        // Variable offsets in constant buffer
        size_t mOffsetVarOffset = ConstantBuffer::kInvalidOffset;
        size_t mScaleVarOffset = ConstantBuffer::kInvalidOffset;;

        ProgramReflection::BindLocation mTexBindLoc;
        ProgramReflection::BindLocation mSamplerBindLoc;
    };
}
