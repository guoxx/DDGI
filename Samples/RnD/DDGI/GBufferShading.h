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

using namespace Falcor;

class GBufferShading : public RenderPass, inherit_shared_from_this<RenderPass, GBufferShading>
{
public:
    using SharedPtr = std::shared_ptr<GBufferShading>;

    static SharedPtr create(const Dictionary& dict = {});

    void setCamera(Camera::SharedConstPtr pCamera) { mpCamera = pCamera; }

    void execute(RenderContext* pContext, const Fbo::SharedPtr& pGBufferFbo, Texture::SharedPtr visibilityTexture, const Fbo::SharedPtr& pTargetFbo);

    RenderPassReflection reflect() const override;
    void execute(RenderContext* pContext, const RenderData* pRenderData) override;
    void renderUI(Gui* pGui, const char* uiGroup) override;
    Dictionary getScriptingDictionary() const override;
    void onResize(uint32_t width, uint32_t height) override;
    void setScene(const Scene::SharedPtr& pScene) override;
    std::string getDesc(void) override { return "GBuffer lighting"; }
private:
    GBufferShading();
    bool parseDictionary(const Dictionary& dict);

    void setVarsData(const Fbo::SharedPtr& pGBufferFbo, Texture::SharedPtr visibilityTexture);

    Camera::SharedConstPtr mpCamera;
    Scene::SharedConstPtr mpScene;
    ConstantBuffer::SharedPtr mpInternalPerFrameCB;
    struct
    {
        int32_t cameraDataOffset;
        int32_t lightArrayOffset;
        int32_t lightCountOffset;
    } mOffsetInCB;

    GraphicsState::SharedPtr mpState;
    GraphicsProgram::SharedPtr mpProgram;
    GraphicsVars::SharedPtr mpVars;
};
