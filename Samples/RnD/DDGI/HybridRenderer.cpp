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
#include "HybridRenderer.h"

const std::string HybridRenderer::skDefaultScene = "Arcade/Arcade.fscene";

Gui::DropdownList kSampleCountList =
{
    { 2, "2" },
    { 4, "4" },
    { 8, "8" },
};

const Gui::DropdownList aaModeList =
{
    { 0, "None"},
    { 1, "TAA" },
    { 2, "FXAA" }
};

const Gui::DropdownList renderPathList =
{
    { 0, "Deferred"},
    { 1, "Forward"},
};

void HybridRenderer::initLightingPass()
{
    mLightingPass.pProgram = GraphicsProgram::createFromFile("ForwardRenderer.slang", "vs", "ps");
    mLightingPass.pProgram->addDefine("_LIGHT_COUNT", std::to_string(mpSceneRenderer->getScene()->getLightCount()));
    initControls();
    mLightingPass.pVars = GraphicsVars::create(mLightingPass.pProgram->getReflector());
    
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(true).setStencilTest(false)./*setDepthWriteMask(false).*/setDepthFunc(DepthStencilState::Func::LessEqual);
    mLightingPass.pDsState = DepthStencilState::create(dsDesc);

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    mLightingPass.pNoCullRS = RasterizerState::create(rsDesc);

    BlendState::Desc bsDesc;
    bsDesc.setRtBlend(0, true).setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
    mLightingPass.pAlphaBlendBS = BlendState::create(bsDesc);
}

void HybridRenderer::initShadowPass(uint32_t windowWidth, uint32_t windowHeight)
{
    mShadowPass.pCsm = CascadedShadowMaps::create(mpSceneRenderer->getScene()->getLight(0), 2048, 2048, windowWidth, windowHeight, mpSceneRenderer->getScene()->shared_from_this());
    mShadowPass.pCsm->setFilterMode(CsmFilterEvsm4);
    mShadowPass.pCsm->setVsmLightBleedReduction(0.3f);
    mShadowPass.pCsm->setVsmMaxAnisotropy(4);
    mShadowPass.pCsm->setEvsmBlur(7, 3);
}

void HybridRenderer::initSSAO()
{
    mSSAO.pSSAO = SSAO::create(uvec2(1024));
    mSSAO.pApplySSAOPass = FullScreenPass::create("ApplyAO.ps.slang");
    mSSAO.pVars = GraphicsVars::create(mSSAO.pApplySSAOPass->getProgram()->getReflector());

    Sampler::Desc desc;
    desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mSSAO.pVars->setSampler("gSampler", Sampler::create(desc));
}

void HybridRenderer::setSceneSampler(uint32_t maxAniso)
{
    Scene* pScene = mpSceneRenderer->getScene().get();
    Sampler::Desc samplerDesc;
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(maxAniso);
    mpSceneSampler = Sampler::create(samplerDesc);
    pScene->bindSampler(mpSceneSampler);
}

void HybridRenderer::applyCustomSceneVars(const Scene* pScene, const std::string& filename)
{
    std::string folder = getDirectoryFromFile(filename);

    Scene::UserVariable var = pScene->getUserVariable("sky_box");
    if (var.type == Scene::UserVariable::Type::String) initSkyBox(folder + '/' + var.str);

    var = pScene->getUserVariable("opacity_scale");
    if (var.type == Scene::UserVariable::Type::Double) mOpacityScale = (float)var.d64;
}

void HybridRenderer::initScene(SampleCallbacks* pSample, Scene::SharedPtr pScene)
{
    if (pScene->getCameraCount() == 0)
    {
        // Place the camera above the center, looking slightly downwards
        const Model* pModel = pScene->getModel(0).get();
        Camera::SharedPtr pCamera = Camera::create();

        vec3 position = pModel->getCenter();
        float radius = pModel->getRadius();
        position.y += 0.1f * radius;
        pScene->setCameraSpeed(radius * 0.03f);

        pCamera->setPosition(position);
        pCamera->setTarget(position + vec3(0, -0.3f, -radius));
        pCamera->setDepthRange(0.1f, radius * 10);

        pScene->addCamera(pCamera);
    }

    if (pScene->getLightCount() == 0)
    {
        // Create a directional light
        DirectionalLight::SharedPtr pDirLight = DirectionalLight::create();
        pDirLight->setWorldDirection(vec3(-0.189f, -0.861f, -0.471f));
        pDirLight->setIntensity(vec3(1, 1, 0.985f) * 10.0f);
        pDirLight->setName("DirLight");
        pScene->addLight(pDirLight);
    }

    if (pScene->getLightProbeCount() > 0)
    {
        const LightProbe::SharedPtr& pProbe = pScene->getLightProbe(0);
        pProbe->setRadius(pScene->getRadius());
        pProbe->setPosW(pScene->getCenter());
        pProbe->setSampler(mpSceneSampler);
    }

    mpSceneRenderer = SceneRenderer::create(pScene);
    mpSceneRenderer->setCameraControllerType(SceneRenderer::CameraControllerType::FirstPerson);
    mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
    setSceneSampler(mpSceneSampler ? mpSceneSampler->getMaxAnisotropy() : 4);
    setActiveCameraAspectRatio(pSample->getCurrentFbo()->getWidth(), pSample->getCurrentFbo()->getHeight());

    mpDepthPass = DepthPass::create();
    mpDepthPass->setScene(pScene);

    initLightingPass();
    auto pTargetFbo = pSample->getCurrentFbo();
    initShadowPass(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    initSSAO();
    initAA(pSample);
    initHZB(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    initSSR(pTargetFbo->getWidth(), pTargetFbo->getHeight());

    mpGBufferRaster = GBufferRaster::create();
    mpGBufferRaster->setScene(pScene);

    mpGBufferLightingPass = GBufferLightingPass::create();
    mpGBufferLightingPass->setScene(pScene);

    mpBlitPass = BlitPass::create();

    mControls[EnableReflections].enabled = pScene->getLightProbeCount() > 0;
    applyLightingProgramControl(ControlID::EnableReflections);
    
    pSample->setCurrentTime(0);
}

void HybridRenderer::resetScene()
{
    mpSceneRenderer = nullptr;
    mpSkyPass = nullptr;

    if (mpGBufferRaster)
    {
        mpGBufferRaster->setScene(nullptr);
    }
}

void HybridRenderer::loadModel(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Model");
    }

    Model::SharedPtr pModel = Model::createFromFile(filename.c_str());
    if (!pModel) return;
    Scene::SharedPtr pScene = Scene::create();
    pScene->addModelInstance(pModel, "instance");

    initScene(pSample, pScene);
}

void HybridRenderer::loadScene(SampleCallbacks* pSample, const std::string& filename, bool showProgressBar)
{
    Mesh::resetGlobalIdCounter();
    resetScene();

    ProgressBar::SharedPtr pBar;
    if (showProgressBar)
    {
        pBar = ProgressBar::create("Loading Scene", 100);
    }

    Scene::SharedPtr pScene = Scene::loadFromFile(filename);

    if (pScene != nullptr)
    {
        initScene(pSample, pScene);
        applyCustomSceneVars(pScene.get(), filename);
        applyCsSkinningMode();
    }
}

void HybridRenderer::initSkyBox(const std::string& name)
{
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    Sampler::SharedPtr pSampler = Sampler::create(samplerDesc);
    mpSkyPass = SkyBox::create(name, true, pSampler);
}

void HybridRenderer::updateLightProbe(const LightProbe::SharedPtr& pLight)
{
    Scene::SharedPtr pScene = mpSceneRenderer->getScene();

    // Remove existing light probes
    while (pScene->getLightProbeCount() > 0)
    {
        pScene->deleteLightProbe(0);
    }

    pLight->setRadius(pScene->getRadius());
    pLight->setPosW(pScene->getCenter());
    pLight->setSampler(mpSceneSampler);
    pScene->addLightProbe(pLight);

    mControls[EnableReflections].enabled = true;
    applyLightingProgramControl(ControlID::EnableReflections);
}

void HybridRenderer::initAA(SampleCallbacks* pSample)
{
    mTAA.pTAA = TemporalAA::create();
    mpFXAA = FXAA::create();
    applyAaMode(pSample);
}

void HybridRenderer::initSSR(uint32_t windowWidth, uint32_t windowHeight)
{
    mSSR.pSSREffect = ScreenSpaceReflection::create();

    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA32Float);
    mSSR.pSSRFbo = FboHelper::create2D(windowWidth, windowHeight, fboDesc);
}

void HybridRenderer::initHZB(uint32_t windowWidth, uint32_t windowHeight)
{
    mHZB.pHZBEffect = HierarchicalZBuffer::create();
    mHZB.pHZBTex = HierarchicalZBuffer::createHZBTexture(windowWidth, windowHeight);
}

void HybridRenderer::initPostProcess()
{
    mpToneMapper = ToneMapping::create(ToneMapping::Operator::Fixed);
    mpToneMapper->setExposureValue(-1);
}

void HybridRenderer::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    mpState = GraphicsState::create();    
    initPostProcess();
    loadScene(pSample, skDefaultScene, true);
}

void HybridRenderer::renderSkyBox(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    if (mpSkyPass)
    {
        PROFILE("skyBox");
        GPU_EVENT(pContext, "skybox");
        mpSkyPass->render(pContext, mpSceneRenderer->getScene()->getActiveCamera().get(), pTargetFbo);
    }
}

void HybridRenderer::beginFrame(RenderContext* pContext, Fbo* pTargetFbo, uint64_t frameId)
{
    GPU_EVENT(pContext, "beginFrame");
    pContext->pushGraphicsState(mpState);
    pContext->clearFbo(mpMainFbo.get(), glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), 1, 0, FboAttachmentType::All);
    pContext->clearFbo(mpPostProcessFbo.get(), glm::vec4(), 1, 0, FboAttachmentType::Color);

    if (mAAMode == AAMode::TAA)
    {
        glm::vec2 targetResolution = glm::vec2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
        pContext->clearRtv(mpMainFbo->getColorTexture(2)->getRTV().get(), vec4(0));

        //  Select the sample pattern and set the camera jitter

    }
}

void HybridRenderer::endFrame(RenderContext* pContext)
{
    GPU_EVENT(pContext, "endFrame");
    pContext->popGraphicsState();
}

void HybridRenderer::buildHZB(RenderContext* pContext)
{
    PROFILE("HZB");
    GPU_EVENT(pContext, "HZB");
    const Texture::SharedPtr pDepthTexture = mpResolveFbo->getDepthStencilTexture();
    const Camera* pCamera = mpSceneRenderer->getScene()->getActiveCamera().get();
    mHZB.pHZBEffect->execute(pContext, pCamera, pDepthTexture, mHZB.pHZBTex);
}

void HybridRenderer::screenSpaceReflection(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    if (mSSR.bEnableSSR)
    {
        PROFILE("SSR");
        GPU_EVENT(pContext, "SSR");
        Texture::SharedPtr pColorIn = mpResolveFbo->getColorTexture(0);
        Texture::SharedPtr pNormal = mpResolveFbo->getColorTexture(1);
        Texture::SharedPtr pDepth = mpResolveFbo->getDepthStencilTexture();
        const Camera* pCamera = mpSceneRenderer->getScene()->getActiveCamera().get();
        mSSR.pSSREffect->execute(pContext, pCamera, pColorIn, pDepth, mHZB.pHZBTex, pNormal, mSSR.pSSRFbo);

        pContext->blit(mSSR.pSSRFbo->getColorTexture(0)->getSRV(), pColorIn->getRTV());
    }
}

void HybridRenderer::toneMapping(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("toneMapping");
    GPU_EVENT(pContext, "toneMapping");
    mpToneMapper->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
}

void HybridRenderer::GBufferPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("GBuffer");
    GPU_EVENT(pContext, "GBuffer");
    mpGBufferRaster->execute(pContext, pTargetFbo);
}

void HybridRenderer::deferredLightingPass(RenderContext* pContext, Fbo::SharedPtr pGBufferFbo, Texture::SharedPtr visibilityTexture, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("Lighting");
    GPU_EVENT(pContext, "Lighting");
    mpGBufferLightingPass->execute(pContext, pGBufferFbo, visibilityTexture, pTargetFbo);
}

void HybridRenderer::depthPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("depthPass");
    GPU_EVENT(pContext, "depthPass");
    mpDepthPass->execute(pContext, pTargetFbo);
}

void HybridRenderer::lightingPass(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("lightingPass");
    GPU_EVENT(pContext, "lightingPass");
    mpState->setFbo(mpMainFbo);
    mpState->setProgram(mLightingPass.pProgram);
    mpState->setDepthStencilState(mLightingPass.pDsState);
    pContext->setGraphicsVars(mLightingPass.pVars);
    ConstantBuffer::SharedPtr pCB = mLightingPass.pVars->getConstantBuffer("PerFrameCB");
    pCB["gOpacityScale"] = mOpacityScale;

    if (mControls[ControlID::EnableShadows].enabled)
    {
        pCB["camVpAtLastCsmUpdate"] = mShadowPass.camVpAtLastCsmUpdate;
        mLightingPass.pVars->setTexture("gVisibilityBuffer", mShadowPass.pVisibilityBuffer);
    }

    if (mAAMode == AAMode::TAA)
    {
        pContext->clearFbo(mTAA.getActiveFbo().get(), vec4(0.0, 0.0, 0.0, 0.0), 1, 0, FboAttachmentType::Color);
        pCB["gRenderTargetDim"] = glm::vec2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    }

    mpSceneRenderer->renderScene(pContext);
    pContext->flush();
    mpState->setDepthStencilState(nullptr);
}

void HybridRenderer::shadowPass(RenderContext* pContext, Texture::SharedPtr pDepthTexture, Texture::SharedPtr* visibilityTexture)
{
    PROFILE("shadowPass");
    GPU_EVENT(pContext, "shadowPass");
    if (mControls[EnableShadows].enabled && mShadowPass.updateShadowMap)
    {
        const Camera* pCamera = mpSceneRenderer->getScene()->getActiveCamera().get();
        mShadowPass.camVpAtLastCsmUpdate = pCamera->getViewProjMatrix();
        *visibilityTexture = mShadowPass.pCsm->generateVisibilityBuffer(pContext, pCamera, pDepthTexture);
        pContext->flush();
    }
}

void HybridRenderer::runTAA(RenderContext* pContext, Fbo::SharedPtr pColorFbo)
{
    if(mAAMode == AAMode::TAA)
    {
        PROFILE("TAA");
        GPU_EVENT(pContext, "TAA");
        //  Get the Current Color and Motion Vectors
        const Texture::SharedPtr pCurColor = pColorFbo->getColorTexture(0);
        const Texture::SharedPtr pMotionVec = mpMainFbo->getColorTexture(2);

        //  Get the Previous Color
        const Texture::SharedPtr pPrevColor = mTAA.getInactiveFbo()->getColorTexture(0);

        //  Execute the Temporal Anti-Aliasing
        pContext->getGraphicsState()->pushFbo(mTAA.getActiveFbo());
        mTAA.pTAA->execute(pContext, pCurColor, pPrevColor, pMotionVec);
        pContext->getGraphicsState()->popFbo();

        //  Copy over the Anti-Aliased Color Texture
        pContext->blit(mTAA.getActiveFbo()->getColorTexture(0)->getSRV(0, 1), pColorFbo->getColorTexture(0)->getRTV());

        //  Swap the Fbos
        mTAA.switchFbos();
    }
}

void HybridRenderer::ambientOcclusion(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("SSAO");
    GPU_EVENT(pContext, "SSAO");
    if (mControls[EnableSSAO].enabled)
    {
        Texture::SharedPtr pDepth = mpResolveFbo->getDepthStencilTexture();
        Texture::SharedPtr pAOMap = mSSAO.pSSAO->generateAOMap(pContext, mpSceneRenderer->getScene()->getActiveCamera().get(), pDepth, mpResolveFbo->getColorTexture(1));
        mSSAO.pVars->setTexture("gColor", mpPostProcessFbo->getColorTexture(0));
        mSSAO.pVars->setTexture("gAOMap", pAOMap);

        pContext->getGraphicsState()->setFbo(pTargetFbo);
        pContext->setGraphicsVars(mSSAO.pVars);

        mSSAO.pApplySSAOPass->execute(pContext);
    }
}

void HybridRenderer::postProcess(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("postProcess");
    GPU_EVENT(pContext, "postProcess");

    Fbo::SharedPtr pPostProcessDst = mControls[EnableSSAO].enabled ? mpPostProcessFbo : pTargetFbo;
    buildHZB(pContext);
    screenSpaceReflection(pContext, pPostProcessDst);
    toneMapping(pContext, pPostProcessDst);
    runTAA(pContext, pPostProcessDst); // This will only run if we are in TAA mode
    ambientOcclusion(pContext, pTargetFbo);
    executeFXAA(pContext, pTargetFbo);
}

void HybridRenderer::executeFXAA(RenderContext* pContext, Fbo::SharedPtr pTargetFbo)
{
    PROFILE("FXAA");
    GPU_EVENT(pContext, "FXAA");
    if(mAAMode == AAMode::FXAA)
    {
        pContext->blit(pTargetFbo->getColorTexture(0)->getSRV(), mpResolveFbo->getRenderTargetView(0));
        mpFXAA->execute(pContext, mpResolveFbo->getColorTexture(0), pTargetFbo);
    }
}

void HybridRenderer::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    if (mpSceneRenderer)
    {
        beginFrame(pRenderContext, pTargetFbo.get(), pSample->getFrameID());
        {
            PROFILE("updateScene");
            GPU_EVENT(pRenderContext, "updateScene");
            mpSceneRenderer->update(pSample->getCurrentTime());
        }

        depthPass(pRenderContext, mpDepthPassFbo);
        shadowPass(pRenderContext, mpDepthPassFbo->getDepthStencilTexture(), &mShadowPass.pVisibilityBuffer);
        if (mRenderPath == RenderPath::Deferred)
        {
            GBufferPass(pRenderContext, mpGBufferFbo);
            deferredLightingPass(pRenderContext, mpGBufferFbo, mShadowPass.pVisibilityBuffer, mpMainFbo);
            mpBlitPass->execute(pRenderContext, mpGBufferFbo->getColorTexture(1), mpMainFbo->getColorTexture(1));
        }
        else
        {
            lightingPass(pRenderContext, mpMainFbo);
        }
        renderSkyBox(pRenderContext, mpMainFbo);
        postProcess(pRenderContext, pTargetFbo);

        endFrame(pRenderContext);
    }
    else
    {
        pRenderContext->clearFbo(pTargetFbo.get(), vec4(0.2f, 0.4f, 0.5f, 1), 1, 0);
    }

}

void HybridRenderer::applyCameraPathState()
{
    const Scene* pScene = mpSceneRenderer->getScene().get();
    if(pScene->getPathCount())
    {
        mUseCameraPath = mUseCameraPath;
        if (mUseCameraPath)
        {
            pScene->getPath(0)->attachObject(pScene->getActiveCamera());
        }
        else
        {
            pScene->getPath(0)->detachObject(pScene->getActiveCamera());
        }
    }
}

bool HybridRenderer::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    if (mpSceneRenderer && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        switch (keyEvent.key)
        {
        case KeyboardEvent::Key::Minus:
            mUseCameraPath = !mUseCameraPath;
            applyCameraPathState();
            return true;
        case KeyboardEvent::Key::O:
            mPerMaterialShader = !mPerMaterialShader;
            mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            return true;
        }
    }

    return mpSceneRenderer ? mpSceneRenderer->onKeyEvent(keyEvent) : false;
}

void HybridRenderer::onDroppedFile(SampleCallbacks* pSample, const std::string& filename)
{
    if (hasSuffix(filename, ".fscene", false) == false)
    {
        msgBox("You can only drop a scene file into the window");
        return;
    }
    loadScene(pSample, filename, true);
}

bool HybridRenderer::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mpSceneRenderer ? mpSceneRenderer->onMouseEvent(mouseEvent) : true;
}

void HybridRenderer::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    // Create the post-process FBO and AA resolve Fbo
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
    mpPostProcessFbo = FboHelper::create2D(width, height, fboDesc);

    Fbo::Desc ssrFboDesc = mSSR.pSSRFbo->getDesc();
    mSSR.pSSRFbo = FboHelper::create2D(width, height, ssrFboDesc);

    mHZB.pHZBTex = HierarchicalZBuffer::createHZBTexture(width, height);

    applyAaMode(pSample);
    mShadowPass.pCsm->onResize(width, height);

    if(mpSceneRenderer)
    {
        setActiveCameraAspectRatio(width, height);
    }
}

void HybridRenderer::applyCsSkinningMode()
{
    if(mpSceneRenderer)
    {
        SkinningCache::SharedPtr pCache = mUseCsSkinning ? SkinningCache::create() : nullptr;
        mpSceneRenderer->getScene()->attachSkinningCacheToModels(pCache);
    }    
}

void HybridRenderer::setActiveCameraAspectRatio(uint32_t w, uint32_t h)
{
    mpSceneRenderer->getScene()->getActiveCamera()->setAspectRatio((float)w / (float)h);
}

void HybridRenderer::initControls()
{
    mControls.resize(ControlID::Count);
    mControls[ControlID::SuperSampling] = { false, false, "INTERPOLATION_MODE", "sample" };
    mControls[ControlID::EnableShadows] = { true, false, "_ENABLE_SHADOWS" };
    mControls[ControlID::EnableReflections] = { false, false, "_ENABLE_REFLECTIONS" };
    mControls[ControlID::EnableHashedAlpha] = { true, true, "_DEFAULT_ALPHA_TEST" };
    mControls[ControlID::EnableTransparency] = { false, false, "_ENABLE_TRANSPARENCY" };
    mControls[ControlID::EnableSSAO] = { true, false, "" };
    mControls[ControlID::VisualizeCascades] = { false, false, "_VISUALIZE_CASCADES" };

    for (uint32_t i = 0; i < ControlID::Count; i++)
    {
        applyLightingProgramControl((ControlID)i);
    }
}

void HybridRenderer::applyLightingProgramControl(ControlID controlId)
{
    const ProgramControl control = mControls[controlId];
    if (control.define.size())
    {
        bool add = control.unsetOnEnabled ? !control.enabled : control.enabled;
        if (add)
        {
            mLightingPass.pProgram->addDefine(control.define, control.value);
        }
        else
        {
            mLightingPass.pProgram->removeDefine(control.define);
        }
    }
}

void HybridRenderer::createTaaPatternGenerator(uint32_t fboWidth, uint32_t fboHeight)
{
    PatternGenerator::SharedPtr pGenerator;
    switch (mTAASamplePattern)
    {
    case SamplePattern::Halton:
        pGenerator = HaltonSamplePattern::create();
        break;
    case SamplePattern::DX11:
        pGenerator = DxSamplePattern::create();
        break;
    default:
        should_not_get_here();
        pGenerator = nullptr;
    }

    mpSceneRenderer->getScene()->getActiveCamera()->setPatternGenerator(pGenerator, 1.0f/vec2(fboWidth, fboHeight));
}

void HybridRenderer::applyAaMode(SampleCallbacks* pSample)
{
    if (mLightingPass.pProgram == nullptr) return;

    uint32_t w = pSample->getCurrentFbo()->getWidth();
    uint32_t h = pSample->getCurrentFbo()->getHeight();

    // Common Depth FBO, shared by depth pass, GBuffer pass and forward pass
    Fbo::Desc depthFboDesc;
    depthFboDesc.setDepthStencilTarget(ResourceFormat::D32Float);
    mpDepthPassFbo = FboHelper::create2D(w, h, depthFboDesc);

    // Common FBO desc (2 color outputs - color and normal)
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA32Float).setColorTarget(1, ResourceFormat::RGBA8Unorm);

    // Release the TAA FBOs
    mTAA.resetFbos();

    if (mAAMode == AAMode::TAA)
    {
        mLightingPass.pProgram->removeDefine("INTERPOLATION_MODE");
        mLightingPass.pProgram->addDefine("_OUTPUT_MOTION_VECTORS");
        fboDesc.setColorTarget(2, ResourceFormat::RG16Float);

        Fbo::Desc taaFboDesc;
        taaFboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
        mTAA.createFbos(w, h, taaFboDesc);
        createTaaPatternGenerator(w, h);
    }
    else
    {
        mpSceneRenderer->getScene()->getActiveCamera()->setPatternGenerator(nullptr);
        mLightingPass.pProgram->removeDefine("_OUTPUT_MOTION_VECTORS");
        applyLightingProgramControl(SuperSampling);

        if (mAAMode == AAMode::FXAA)
        {
            Fbo::Desc resolveDesc;
            resolveDesc.setColorTarget(0, pSample->getCurrentFbo()->getColorTexture(0)->getFormat());
            mpResolveFbo = FboHelper::create2D(w, h, resolveDesc);
        }
    }

    mpMainFbo = FboHelper::create2D(w, h, fboDesc);
    mpMainFbo->attachDepthStencilTarget(mpDepthPassFbo->getDepthStencilTexture());

    mpGBufferFbo = GBufferRaster::createGBufferFbo(w, h, false);
    mpGBufferFbo->attachDepthStencilTarget(mpDepthPassFbo->getDepthStencilTexture());

    mpResolveFbo = mpMainFbo;
}

void HybridRenderer::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
    static const FileDialogFilterVec kImageFilesFilter = { {"bmp"}, {"jpg"}, {"dds"}, {"png"}, {"tiff"}, {"tif"}, {"tga"}, {"hdr"}, {"exr"}  };

    if (pGui->addButton("Load Model"))
    {
        std::string filename;
        if (openFileDialog(Model::kFileExtensionFilters, filename))
        {
            loadModel(pSample, filename, true);
        }
    }

    if (pGui->addButton("Load Scene"))
    {
        std::string filename;
        if (openFileDialog(Scene::kFileExtensionFilters, filename))
        {
            loadScene(pSample, filename, true);
        }
    }

    if (mpSceneRenderer)
    {
        if (pGui->addButton("Load SkyBox Texture"))
        {
            std::string filename;
            if (openFileDialog(kImageFilesFilter, filename))
            {
                initSkyBox(filename);
            }
        }

        if (pGui->beginGroup("Scene Settings"))
        {
            Scene* pScene = mpSceneRenderer->getScene().get();
            float camSpeed = pScene->getCameraSpeed();
            if (pGui->addFloatVar("Camera Speed", camSpeed))
            {
                pScene->setCameraSpeed(camSpeed);
            }

            vec2 depthRange(pScene->getActiveCamera()->getNearPlane(), pScene->getActiveCamera()->getFarPlane());
            if (pGui->addFloat2Var("Depth Range", depthRange, 0, FLT_MAX))
            {
                pScene->getActiveCamera()->setDepthRange(depthRange.x, depthRange.y);
            }

            if (pScene->getPathCount() > 0)
            {
                if (pGui->addCheckBox("Camera Path", mUseCameraPath))
                {
                    applyCameraPathState();
                }
            }

            if (pScene->getLightCount() && pGui->beginGroup("Light Sources"))
            {
                for (uint32_t i = 0; i < pScene->getLightCount(); i++)
                {
                    Light* pLight = pScene->getLight(i).get();
                    pLight->renderUI(pGui, pLight->getName().c_str());
                }
                pGui->endGroup();
            }

            if (pGui->addCheckBox("Use CS for Skinning", mUseCsSkinning))
            {
                applyCsSkinningMode();
            }
            pGui->endGroup();
        }

        if (pGui->beginGroup("Renderer Settings"))
        {
            pGui->addDropdown("Render Path", renderPathList, (uint32_t&)mRenderPath);

            if (pGui->addCheckBox("Specialize Material Shaders", mPerMaterialShader))
            {
                mpSceneRenderer->toggleStaticMaterialCompilation(mPerMaterialShader);
            }
            pGui->addTooltip("Create a specialized version of the lighting program for each material in the scene");

            uint32_t maxAniso = mpSceneSampler->getMaxAnisotropy();
            if (pGui->addIntVar("Max Anisotropy", (int&)maxAniso, 1, 16))
            {
                setSceneSampler(maxAniso);
            }

            pGui->endGroup();
        }

        //  Anti-Aliasing Controls.
        if (pGui->beginGroup("Anti-Aliasing"))
        {
            bool reapply = false;
            reapply = reapply || pGui->addDropdown("AA Mode", aaModeList, (uint32_t&)mAAMode);

            //  Temporal Anti-Aliasing.
            if (mAAMode == AAMode::TAA)
            {
                if (pGui->beginGroup("TAA"))
                {
                    //  Render the TAA UI.
                    mTAA.pTAA->renderUI(pGui);

                    //  Choose the Sample Pattern for TAA.
                    Gui::DropdownList samplePatternList;
                    samplePatternList.push_back({ (uint32_t)SamplePattern::Halton, "Halton" });
                    samplePatternList.push_back({ (uint32_t)SamplePattern::DX11, "DX11" });
                    pGui->addDropdown("Sample Pattern", samplePatternList, (uint32_t&)mTAASamplePattern);

                    // Disable super-sampling
                    pGui->endGroup();
                }
            }

            if (mAAMode == AAMode::FXAA)
            {
                mpFXAA->renderUI(pGui, "FXAA");
            }

            if (reapply) applyAaMode(pSample);

            pGui->endGroup();
        }

        if (pGui->beginGroup("Light Probes"))
        {
            if (pGui->addButton("Add/Change Light Probe"))
            {
                std::string filename;
                if (openFileDialog(kImageFilesFilter, filename))
                {
                    updateLightProbe(LightProbe::create(pSample->getRenderContext(), filename, true, ResourceFormat::RGBA16Float));
                }
            }

            Scene::SharedPtr pScene = mpSceneRenderer->getScene();
            if (pScene->getLightProbeCount() > 0)
            {
                if (pGui->addCheckBox("Enable", mControls[ControlID::EnableReflections].enabled))
                {
                    applyLightingProgramControl(ControlID::EnableReflections);
                }
                if (mControls[ControlID::EnableReflections].enabled)
                {
                    pGui->addSeparator();
                    pScene->getLightProbe(0)->renderUI(pGui);
                }
            }

            pGui->endGroup();
        }

        mpToneMapper->renderUI(pGui, "Tone-Mapping");

        if (pGui->beginGroup("SSR"))
        {
            pGui->addCheckBox("Enable SSR", mSSR.bEnableSSR);
            mSSR.pSSREffect->renderUI(pGui);

            pGui->endGroup();
        }

        if (pGui->beginGroup("Shadows"))
        {
            if (pGui->addCheckBox("Enable Shadows", mControls[ControlID::EnableShadows].enabled))
            {
                applyLightingProgramControl(ControlID::EnableShadows);
            }
            if (mControls[ControlID::EnableShadows].enabled)
            {
                pGui->addCheckBox("Update Map", mShadowPass.updateShadowMap);
                mShadowPass.pCsm->renderUI(pGui);
                if (pGui->addCheckBox("Visualize Cascades", mControls[ControlID::VisualizeCascades].enabled))
                {
                    applyLightingProgramControl(ControlID::VisualizeCascades);
                    mShadowPass.pCsm->toggleCascadeVisualization(mControls[ControlID::VisualizeCascades].enabled);
                }
            }
            pGui->endGroup();
        }

        if (pGui->beginGroup("SSAO"))
        {
            if (pGui->addCheckBox("Enable SSAO", mControls[ControlID::EnableSSAO].enabled))
            {
                applyLightingProgramControl(ControlID::EnableSSAO);
            }

            if (mControls[ControlID::EnableSSAO].enabled)
            {
                mSSAO.pSSAO->renderUI(pGui);
            }
            pGui->endGroup();
        }

        if (pGui->beginGroup("Transparency"))
        {
            if (pGui->addCheckBox("Enable Transparency", mControls[ControlID::EnableTransparency].enabled))
            {
                applyLightingProgramControl(ControlID::EnableTransparency);
            }
            pGui->addFloatVar("Opacity Scale", mOpacityScale, 0, 1);
            pGui->endGroup();
        }

        if (pGui->addCheckBox("Hashed-Alpha Test", mControls[ControlID::EnableHashedAlpha].enabled))
        {
            applyLightingProgramControl(ControlID::EnableHashedAlpha);
        }
    }
}
