/***************************************************************************
* Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
***************************************************************************/
#include "Framework.h"
#include "SSR.h"
#include "Utils/Gui.h"
#include "API/RenderContext.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/FboHelper.h"

namespace Falcor
{
    const char* ScreenSpaceReflection::kDesc = "Screen Space Raytrace Reflection";

    static const char* kShaderFilename = "Effects/ScreenSpaceReflection.slang";

#ifdef DEBUG_SSR
    const Gui::DropdownList ScreenSpaceReflection::kDebugModeDropdown = {
        { (uint32_t)DebugMode::None, "None" },
        { (uint32_t)DebugMode::DebugIntersectionResult, "Debug Intersection Result" },
        { (uint32_t)DebugMode::DebugRaymarchCount, "Debug Raymarch Count" },
        { (uint32_t)DebugMode::DebugRayPath, "Debug Ray Path" },
    };
#endif

    ScreenSpaceReflection::~ScreenSpaceReflection() = default;

    ScreenSpaceReflection::ScreenSpaceReflection(int32_t width, int32_t height) : RenderPass("ScreenSpaceReflection")
    {
        init(width, height);
        createGraphicsResources();
    }

    ScreenSpaceReflection::SharedPtr ScreenSpaceReflection::create(const Dictionary& dict)
    {
        return create(dict["width"], dict["height"]);
    }

    ScreenSpaceReflection::SharedPtr ScreenSpaceReflection::create(uint32_t width, uint32_t height)
    {
        ScreenSpaceReflection* pSSR = new ScreenSpaceReflection(width, height);
        return ScreenSpaceReflection::SharedPtr(pSSR);
    }

    void ScreenSpaceReflection::renderUI(Gui* pGui, const char* uiGroup)
    {
        if(!uiGroup || pGui->beginGroup(uiGroup))
        {
            if (pGui->addCheckBox("HiZ", mHiZRayTrace))
            {
                createGraphicsResources();
            }

#ifdef DEBUG_SSR
            if (pGui->addDropdown("Debug Mode", kDebugModeDropdown, (uint32_t&)mDebugMode))
            {
                createGraphicsResources();
            }

            if (mDebugMode == DebugMode::DebugRayPath)
            {
                pGui->addInt2Var("Debug Pixel", mDebugPixel);
                mDebugPixel.x = std::max<int>(0, std::min<int>(mDebugFbo->getWidth() - 1, mDebugPixel.x));
                mDebugPixel.y = std::max<int>(0, std::min<int>(mDebugFbo->getHeight() - 1, mDebugPixel.y));
            }
#endif

            pGui->addFloatSlider("Z Thickness", mConstantData.gZThickness, 0, 1);
            pGui->addTooltip("Camera space thickness to ascribe to each pixel in the depth buffer");

            int rayStrideDDA = (int)mConstantData.gRayStrideDDA;
            pGui->addIntSlider("DDA Ray Stride", rayStrideDDA, 1, 16);
            mConstantData.gRayStrideDDA = (float)rayStrideDDA;
            pGui->addTooltip("Step in horizontal or vertical pixels between samples. "
                "This is a float because integer math is slow on GPUs, but should be set to an integer >= 1");

            pGui->addFloatSlider("Jitter Fraction", mConstantData.gJitterFraction, 0, 2);
            pGui->addTooltip("Number between 0 and 1 for how far to bump the ray "
                "in stride units ""to conceal banding artifacts, plus the stride "
                "ray offset. It is recommended to set this to at least 1.0 to avoid "
                "self-intersection artifacts.");

            pGui->addFloatVar("Max Ray Distance", mConstantData.gMaxRayTraceDistance, 0);
            pGui->addTooltip("Maximum camera-space distance to trace before returning a miss");

            pGui->addFloatVar("Max Steps HZB", mConstantData.gMaxStepsHZB, 0);
            pGui->addTooltip("Maximum number of iterations. Higher gives better images but may be slow");
            pGui->addFloatVar("Max Steps DDA", mConstantData.gMaxStepsDDA, 0);
            pGui->addTooltip("Maximum number of iterations. Higher gives better images but may be slow");

            if (uiGroup) pGui->endGroup();
        }
    }

    void ScreenSpaceReflection::onResize(uint32_t width, uint32_t height)
    {
        Fbo::Desc tempFboDesc;
        tempFboDesc.setColorTarget(0, ResourceFormat::RGBA16Float);
        mpTempFbo = FboHelper::create2D(width, height, tempFboDesc);

        mpHistoryTex = Texture::create2D(width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr,
                                         ResourceBindFlags::ShaderResource | ResourceBindFlags::RenderTarget);

#ifdef DEBUG_SSR
        Fbo::Desc desc;
        desc.setColorTarget(0, ResourceFormat::RGBA16Float, true);
        mDebugFbo = FboHelper::create2D(width, height, desc);

        if (mDebugPixel.x < 0)
        {
            // initialize to screen center
            mDebugPixel.x = mDebugFbo->getWidth() / 2;
            mDebugPixel.y = mDebugFbo->getHeight() / 2;
        }
        mDebugPixel.x = std::max<int>(0, std::min<int>(mDebugFbo->getWidth() - 1, mDebugPixel.x));
        mDebugPixel.y = std::max<int>(0, std::min<int>(mDebugFbo->getHeight() - 1, mDebugPixel.y));
#endif
    }

    void ScreenSpaceReflection::init(int32_t width, int32_t height)
    {
        mpTAA = TemporalAA::create();

        DepthStencilState::Desc dsDesc;
        dsDesc.setDepthTest(false).setDepthWriteMask(false);
        DepthStencilState::SharedPtr pDepthStencilState = DepthStencilState::create(dsDesc);

        const uint32_t indices[] = {0, 1, 2};
        Buffer::SharedPtr pIB = Buffer::create(sizeof(indices), Buffer::BindFlags::Index, Buffer::CpuAccess::None, (void*)indices);
        Vao::SharedPtr pVao = Vao::create(Vao::Topology::TriangleStrip, nullptr, Vao::BufferVec(), pIB, ResourceFormat::R32Uint);

        mpPipelineState = GraphicsState::create();
        mpPipelineState->setDepthStencilState(pDepthStencilState);
        mpPipelineState->setVao(pVao);    

        onResize(width, height);
    }

    void ScreenSpaceReflection::createGraphicsResources()
    {
        Program::DefineList defines;
        if (mHiZRayTrace)
            defines.add("HIZ_RAY_TRACE");
#ifdef DEBUG_SSR
        if (mDebugMode == DebugMode::DebugIntersectionResult)
            defines.add("DEBUG_INTERSECTION_RESULT");
        else if (mDebugMode == DebugMode::DebugRaymarchCount)
            defines.add("DEBUG_RAYMARCH_CNT");
        else if (mDebugMode == DebugMode::DebugRayPath)
            defines.add("DEBUG_PIXEL");
#endif

        GraphicsProgram::Desc programDesc;
        programDesc.setCompilerFlags(Shader::CompilerFlags::EmitDebugInfo);
        programDesc.setOptimizationLevel(Shader::OptimizationLevel::Maximal);
        programDesc.addShaderLibrary(kShaderFilename).vsEntry("vsMain").psEntry("psMain");
        mpProgram = GraphicsProgram::create(programDesc, defines);
        mpPipelineState->setProgram(mpProgram);

        ProgramReflection::SharedConstPtr pReflector = mpProgram->getReflector();
        mpProgVars = GraphicsVars::create(pReflector);

        Sampler::Desc samplerDesc;
        samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
        mpPointSampler = Sampler::create(samplerDesc);

        const ParameterBlockReflection* pParamBlockReflector = pReflector->getDefaultParameterBlock().get();
        mBindLocations.perFrameCB = pParamBlockReflector->getResourceBinding("PerFrameCB");
#ifdef DEBUG_SSR
        mBindLocations.debugCB = pParamBlockReflector->getResourceBinding("DebugCB");
        mBindLocations.debugTex = pParamBlockReflector->getResourceBinding("gDebugTex");
        mBindLocations.debugData = pParamBlockReflector->getResourceBinding("gDebugData");
#endif
        mBindLocations.pointSampler = pParamBlockReflector->getResourceBinding("gPointSampler");
        mBindLocations.HZBTex = pParamBlockReflector->getResourceBinding("gHZBTex");
        mBindLocations.normalTex = pParamBlockReflector->getResourceBinding("gNormalTex");
        mBindLocations.diffuseOpacityTex = pParamBlockReflector->getResourceBinding("gDiffuseOpacityTex");
        mBindLocations.specRoughTex = pParamBlockReflector->getResourceBinding("gSpecRoughTex");
        mBindLocations.colorTex = pParamBlockReflector->getResourceBinding("gColorTex");
        mBindLocations.historyTex = pParamBlockReflector->getResourceBinding("gHistoryTex");

#ifdef DEBUG_SSR
        mpDebugData = TypedBuffer<glm::vec4>::create(4096, ResourceBindFlags::UnorderedAccess);
#endif
    }

    void ScreenSpaceReflection::setVarsData(const Camera* pCamera,
        const Texture::SharedPtr& pColorIn,
        const Texture::SharedPtr& pDepthTexture,
        const Texture::SharedPtr& pHZBTexture,
        const Texture::SharedPtr& pNormalTexture,
        const Texture::SharedPtr& pDiffuseOpacityTexture,
        const Texture::SharedPtr& pSpecRoughTexture)
    {
        ParameterBlock* pDefaultBlock = mpProgVars->getDefaultBlock().get();
        pDefaultBlock->setSampler(mBindLocations.pointSampler, 0, mpPointSampler);
        pDefaultBlock->setSrv(mBindLocations.colorTex, 0, pColorIn->getSRV());
        pDefaultBlock->setSrv(mBindLocations.HZBTex, 0, pHZBTexture->getSRV());
        pDefaultBlock->setSrv(mBindLocations.normalTex, 0, pNormalTexture->getSRV());
        pDefaultBlock->setSrv(mBindLocations.diffuseOpacityTex, 0, pDiffuseOpacityTexture->getSRV());
        pDefaultBlock->setSrv(mBindLocations.specRoughTex, 0, pSpecRoughTexture->getSRV());
        pDefaultBlock->setSrv(mBindLocations.historyTex, 0, mpHistoryTex->getSRV());

        // Update value of constants
        mConstantData.gFrameCount += 1;
        mConstantData.gInvProjMat = glm::inverse(pCamera->getProjMatrix());
        mConstantData.gViewMat = pCamera->getViewMatrix();
        const float w = float(pDepthTexture->getWidth()), h = float(pDepthTexture->getHeight());
        glm::mat4x4 mNDCToPixel = { 0.5f*w,       0,   0, 0,
                                         0, -0.5f*h,   0, 0,
                                         0,       0,   1, 0,
                                    0.5f*w,  0.5f*h,   0, 1 };
        mConstantData.gProjToPixelMat = mNDCToPixel * pCamera->getProjMatrix();
        mConstantData.gZBufferSize = glm::vec4(w, h, 1.0f/w, 1.0f/h);
        const float HZBWidth = float(pHZBTexture->getWidth()), HZBHeight = float(pHZBTexture->getHeight());
        mConstantData.gHZBSize = glm::vec4(HZBWidth, HZBHeight, 1.0f/HZBWidth, 1.0f/HZBHeight);
        // Negative value in camera space
        mConstantData.gNearZ = -pCamera->getNearPlane();
        mConstantData.gFarZ = -pCamera->getFarPlane();
        mConstantData.gHZBMipCount = float(pHZBTexture->getMipCount());
        // Update to GPU
        ConstantBuffer* pCB = pDefaultBlock->getConstantBuffer(mBindLocations.perFrameCB, 0).get();
        assert(sizeof(mConstantData) <= pCB->getSize());
        pCB->setBlob(&mConstantData, 0, sizeof(mConstantData));

#ifdef DEBUG_SSR
        if (mDebugMode == DebugMode::DebugRayPath)
        {
            pDefaultBlock->setUav(mBindLocations.debugTex, 0, mDebugFbo->getColorTexture(0)->getUAV());
            pDefaultBlock->setUav(mBindLocations.debugData, 0, mpDebugData->getUAV());

            ConstantBuffer* pDebugCB = pDefaultBlock->getConstantBuffer(mBindLocations.debugCB, 0).get();
            pDebugCB->setVariable("gDebugCoord", mDebugPixel);
        }
#endif
    }

    void ScreenSpaceReflection::execute(RenderContext* pRenderContext,
        const Camera* pCamera,
        const Texture::SharedPtr& pColorIn,
        const Texture::SharedPtr& pDepthTexture,
        const Texture::SharedPtr& pHZBTexture,
        const Texture::SharedPtr& pNormalTexture,
        const Texture::SharedPtr& pDiffuseOpacityTexture,
        const Texture::SharedPtr& pSpecRoughTexture,
        const Texture::SharedPtr& pMotionVecTexture,
        const Fbo::SharedPtr& pFbo)
    {
#ifdef DEBUG_SSR
        pRenderContext->clearUAV(mDebugFbo->getColorTexture(0)->getUAV().get(), glm::vec4(0.0f));
        pRenderContext->clearUAV(mpDebugData->getUAV().get(), glm::vec4(0.0f));
#endif

        setVarsData(pCamera, pColorIn, pDepthTexture, pHZBTexture, pNormalTexture, pDiffuseOpacityTexture, pSpecRoughTexture);

        mpPipelineState->setProgram(mpProgram);
        mpPipelineState->setFbo(mpTempFbo, true);

        pRenderContext->pushGraphicsState(mpPipelineState);
        pRenderContext->pushGraphicsVars(mpProgVars);
        pRenderContext->drawIndexed(3, 0, 0);
        pRenderContext->popGraphicsVars();
        pRenderContext->popGraphicsState();

#ifdef DEBUG_SSR
        if (mDebugMode == DebugMode::DebugRayPath)
        {
            pRenderContext->blit(mDebugFbo->getColorTexture(0)->getSRV(), pFbo->getColorTexture(0)->getRTV());
        }
#endif

        pRenderContext->getGraphicsState()->pushFbo(pFbo);
        mpTAA->execute(pRenderContext, mpTempFbo->getColorTexture(0), mpHistoryTex, pMotionVecTexture);
        pRenderContext->getGraphicsState()->popFbo();

        pRenderContext->blit(pFbo->getColorTexture(0)->getSRV(), mpHistoryTex->getRTV());
    }

    // TODO: implementation
    RenderPassReflection ScreenSpaceReflection::reflect() const
    {
        should_not_get_here();
        RenderPassReflection reflection;
        return reflection;
    }

    void ScreenSpaceReflection::execute(RenderContext* pContext, const RenderData* pData)
    {
        should_not_get_here();
    }
}
