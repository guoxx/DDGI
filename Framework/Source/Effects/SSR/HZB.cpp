/***************************************************************************
* Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
***************************************************************************/
#include "Framework.h"
#include "HZB.h"
#include "Utils/Gui.h"
#include "API/RenderContext.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/FboHelper.h"

namespace Falcor
{
    const char* HierarchicalZBuffer::kDesc = "Build Hierarchical Z Buffer";

    static const char* kShaderFilename = "Effects/HierarchicalZBuffer.slang";

    HierarchicalZBuffer::~HierarchicalZBuffer() = default;

    HierarchicalZBuffer::HierarchicalZBuffer() : RenderPass("HierarchicalZBuffer")
    {
        init();
        createGraphicsResources();
    }

    HierarchicalZBuffer::SharedPtr HierarchicalZBuffer::create(const Dictionary& dict)
    {
        HierarchicalZBuffer* pHZB = new HierarchicalZBuffer();
        return HierarchicalZBuffer::SharedPtr(pHZB);
    }

    Texture::SharedPtr HierarchicalZBuffer::createHZBTexture(int32_t w, int32_t h)
    {
        return Texture::create2D(getNextPowerOf2(w), getNextPowerOf2(h),
                                 ResourceFormat::RG32Float, 1, Texture::kMaxPossible,
                                 nullptr, Texture::BindFlags::ShaderResource | Texture::BindFlags::RenderTarget);
    }

    void HierarchicalZBuffer::init()
    {
        DepthStencilState::Desc dsDesc;
        dsDesc.setDepthTest(false).setDepthWriteMask(false);
        DepthStencilState::SharedPtr pDepthStencilState = DepthStencilState::create(dsDesc);

        const uint32_t indices[] = { 0, 1, 2 };
        Buffer::SharedPtr pIB = Buffer::create(sizeof(indices), Buffer::BindFlags::Index, Buffer::CpuAccess::None, (void*)indices);
        Vao::SharedPtr pVao = Vao::create(Vao::Topology::TriangleStrip, nullptr, Vao::BufferVec(), pIB, ResourceFormat::R32Uint);

        mpPipelineState = GraphicsState::create();
        mpPipelineState->setDepthStencilState(pDepthStencilState);
        mpPipelineState->setVao(pVao);
    }

    void HierarchicalZBuffer::createGraphicsResources()
    {
        GraphicsProgram::Desc d;
        d.addShaderLibrary(kShaderFilename).vsEntry("VSMain").psEntry("PSMain");
        mpFirstIterProgram = GraphicsProgram::create(d, Program::DefineList{{"_FIRST_ITER", ""}});
        mpReductionProgram = GraphicsProgram::create(d, Program::DefineList());

        ProgramReflection::SharedConstPtr pReflector = mpFirstIterProgram->getReflector();
        mpProgVars = GraphicsVars::create(pReflector);

        Sampler::Desc samplerDesc;
        samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
        samplerDesc.setBorderColor(glm::vec4(1));
        samplerDesc.setAddressingMode(Sampler::AddressMode::Border, Sampler::AddressMode::Border, Sampler::AddressMode::Border);
        mpPointSampler = Sampler::create(samplerDesc);

        const ParameterBlockReflection* pParamBlockReflector = pReflector->getDefaultParameterBlock().get();
        mBindLocations.perFrameCB = pParamBlockReflector->getResourceBinding("PerFrameCB");
        mBindLocations.pointSampler = pParamBlockReflector->getResourceBinding("gPointSampler");
        mBindLocations.depthTex = pParamBlockReflector->getResourceBinding("gDepthTex");
    }

    void HierarchicalZBuffer::execute(RenderContext* pRenderContext,
        const Camera* pCamera,
        const Texture::SharedPtr& pDepthTexture,
        const Texture::SharedPtr& pHiZ)
    {
        assert(getNextPowerOf2(pDepthTexture->getWidth()) == pHiZ->getWidth());
        assert(getNextPowerOf2(pDepthTexture->getHeight()) == pHiZ->getHeight());

        pRenderContext->pushGraphicsState(mpPipelineState);
        pRenderContext->pushGraphicsVars(mpProgVars);

        for (uint32_t mipmap = 0; mipmap < pHiZ->getMipCount(); ++mipmap)
        {
            uint32_t w = pHiZ->getWidth(mipmap);
            uint32_t h = pHiZ->getHeight(mipmap);
            assert(isPowerOf2(w) && isPowerOf2(h));

            // graphics state setup
            if (mipmap == 0)
                mpPipelineState->setProgram(mpFirstIterProgram);
            else
                mpPipelineState->setProgram(mpReductionProgram);

            Fbo::SharedPtr pFbo = Fbo::create();
            pFbo->attachColorTarget(pHiZ, 0, mipmap);
            mpPipelineState->setFbo(pFbo, true);

            // graphics program setup
            ParameterBlock* pDefaultBlock = mpProgVars->getDefaultBlock().get();
            pDefaultBlock->setSampler(mBindLocations.pointSampler, 0, mpPointSampler);
            if (mipmap == 0)
                pDefaultBlock->setSrv(mBindLocations.depthTex, 0, pDepthTexture->getSRV());
            else
                pDefaultBlock->setSrv(mBindLocations.depthTex, 0, pHiZ->getSRV(mipmap - 1, 1));

            ConstantBuffer* pCB = pDefaultBlock->getConstantBuffer(mBindLocations.perFrameCB, 0).get();
            pCamera->setIntoConstantBuffer(pCB, "gCamera");
            if (mipmap == 0)
            {
                pCB->setVariable("gUvScaleOffset", glm::vec4(float(pHiZ->getWidth()) / pDepthTexture->getWidth(),
                                                             float(pHiZ->getHeight()) / pDepthTexture->getHeight(),
                                                             0, 0));
            }
            else
            {
                pCB->setVariable("gUvScaleOffset", glm::vec4(1, 1, -0.25f/w, -0.25f/h));
            }

            pRenderContext->drawIndexed(3, 0, 0);
        }

        pRenderContext->popGraphicsVars();
        pRenderContext->popGraphicsState();
    }

    // TODO: implementation
    RenderPassReflection HierarchicalZBuffer::reflect() const
    {
        should_not_get_here();
        RenderPassReflection reflection;
        return reflection;
    }

    void HierarchicalZBuffer::execute(RenderContext* pContext, const RenderData* pData)
    {
        should_not_get_here();
    }
}
