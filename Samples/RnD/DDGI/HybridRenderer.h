/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
#include "Experimental/RenderPasses/BlitPass.h"
#include "Experimental/RenderPasses/DepthPass.h"
#include "Experimental/RenderPasses/GBuffer.h"
#include "Experimental/RenderPasses/GBufferRaster.h"
#include "Experimental/RenderPasses/GBufferLightingPass.h"
#include "Experimental/RenderPasses/ForwardLightingPass.h"
#include "LightFieldProbeVolume.h"
#include "LightFieldProbeRayTracing.h"
#include "SVGFPass.h"

using namespace Falcor;

class HybridRenderer : public Renderer
{
public:
    void onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext) override;
    void onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent) override;
    void onGuiRender(SampleCallbacks* pSample, Gui* pGui) override;
    void onDroppedFile(SampleCallbacks* pSample, const std::string& filename) override;

private:
    Fbo::SharedPtr mpGBufferFbo;
    Fbo::SharedPtr mpMainFbo;
    Fbo::SharedPtr mpDepthPassFbo;
    Fbo::SharedPtr mpResolveFbo;
    Fbo::SharedPtr mpPostProcessFbo;
    Fbo::SharedPtr mpTempFP16Fbo;
    Texture::SharedPtr mpHZBTexture;

    ForwardLightingPass::SharedPtr mpForwardPass;
    CascadedShadowMaps::SharedPtr mpShadowPass;
    SkyBox::SharedPtr mpSkyPass;
    DepthPass::SharedPtr mpDepthPass;
    GBufferRaster::SharedPtr mpGBufferRaster;
    GBufferLightingPass::SharedPtr mpGBufferLightingPass;
    HierarchicalZBuffer::SharedPtr mpHZBPass;
    ScreenSpaceReflection::SharedPtr mpSSRPass;
    ToneMapping::SharedPtr mpToneMapper;
    SSAO::SharedPtr mpSSAO;
    FXAA::SharedPtr mpFXAA;

    SVGFPass::SharedPtr mpLightFieldRTDenoiser;
    SVGFPass::SharedPtr mpSSRDenoiser;

    LightFieldProbeVolume::SharedPtr mpLightProbeVolume;
    LightFieldProbeRayTracing::SharedPtr mpLightProbeRayTracer;

    BlitPass::SharedPtr mpBlitPass;
    BlitPass::SharedPtr mpAdditiveBlitPass;

    //  The Temporal Anti-Aliasing Pass.
    class
    {
    public:
        TemporalAA::SharedPtr pTAA;
        Fbo::SharedPtr getActiveFbo() { return pTAAFbos[activeFboIndex]; }
        Fbo::SharedPtr getInactiveFbo()  { return pTAAFbos[1 - activeFboIndex]; }
        void createFbos(uint32_t width, uint32_t height, const Fbo::Desc & fboDesc)
        {
            pTAAFbos[0] = FboHelper::create2D(width, height, fboDesc);
            pTAAFbos[1] = FboHelper::create2D(width, height, fboDesc);
        }

        void switchFbos() { activeFboIndex = 1 - activeFboIndex; }
        void resetFbos()
        {
            activeFboIndex = 0;
            pTAAFbos[0] = nullptr;
            pTAAFbos[1] = nullptr;
        }

        void resetFboActiveIndex() { activeFboIndex = 0;}

    private:
        Fbo::SharedPtr pTAAFbos[2];
        uint32_t activeFboIndex = 0;
    } mTAA;

    void beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId);
    void endFrame(RenderContext* pContext);
    void renderGBuffer(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void deferredLighting(RenderContext* pContext, Fbo::SharedPtr pGBufferFbo, Texture::SharedPtr visibilityTexture, Fbo::SharedPtr pTargetFbo);
    void depthPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void shadowPass(RenderContext* pContext, Texture::SharedPtr pDepthTexture);
    void renderSkyBox(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void forwardLightingPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void runTAA(RenderContext* pContext, Fbo::SharedPtr pColorFbo);
    void toneMapping(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void buildHZB(RenderContext* pContext);
    void screenSpaceReflection(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);
    void postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo);

    void initSkyBox(const std::string& name);
    void initShadowPass(uint32_t windowWidth, uint32_t windowHeight);
    void initAA(SampleCallbacks* pSample);
    void initLightFieldProbes(const Scene::SharedPtr& pScene);
    void updateLightProbe(const LightProbe::SharedPtr& pLight);

	SceneRenderer::SharedPtr mpSceneRenderer;
    void loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar);
    void loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar);
    void initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene);
    void applyCustomSceneVars(const Scene* pScene, const std::string& filename);
    void resetScene();

    void setActiveCameraAspectRatio(uint32_t w, uint32_t h);
    void setSceneSampler(uint32_t maxAniso);

    Sampler::SharedPtr mpSceneSampler;

    enum class SamplePattern : uint32_t
    {
        Halton,
        DX11
    };

    enum class AAMode
    {
        None,
        TAA,
        FXAA
    };

    enum class RenderPath
    {
        Deferred = 0,
        Forward
    };

    float mOpacityScale = 0.5f;
    AAMode mAAMode = AAMode::None;
    SamplePattern mTAASamplePattern = SamplePattern::Halton;
    void applyAaMode(SampleCallbacks* pSample);

    RenderPath mRenderPath = RenderPath::Deferred;
    bool mUseCameraPath = true;
    void applyCameraPathState();
    bool mPerMaterialShader = false;
    bool mUseCsSkinning = false;
    bool mVisualizeCascades = false;
    bool mEnableSSAO = false;
    bool mEnableSSR = false;
    bool mEnableSSRDenoiser = false;
    bool mEnableLightFieldProbeRayTracing = false;
    bool mEnableLightFieldProbeDenoise = false;

    bool mDebugDisplayLightFieldRT = false;
    bool mDebugDisplaySSR = false;

    // TODO
    bool mEnableTransparent = false;
    bool mEnableAlphaTest = false;
    void applyCsSkinningMode();
    static const std::string skDefaultScene;

    void createTaaPatternGenerator(uint32_t fboWidth, uint32_t fboHeight);
};
