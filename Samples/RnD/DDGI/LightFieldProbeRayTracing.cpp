#include "LightFieldProbeRayTracing.h"
#include "Experimental/RenderPasses/GBuffer.h"

using namespace Falcor;

LightFieldProbeRayTracing::SharedPtr LightFieldProbeRayTracing::create(const Dictionary& dictionary)
{
    auto ptr = new LightFieldProbeRayTracing();
    return SharedPtr(ptr);
}

LightFieldProbeRayTracing::LightFieldProbeRayTracing() : RenderPass("LightFieldProbeRayTracing")
{
    GraphicsProgram::Desc d;
    d.setOptimizationLevel(Shader::OptimizationLevel::Maximal);
    //d.setCompilerFlags(Shader::CompilerFlags::EmitDebugInfo);
    //d.setCompilerFlags(Shader::CompilerFlags::HLSLShader);
    d.addShaderLibrary("LightFieldProbeRayTracing.slang").vsEntry("VSMain").psEntry("PSMain");
    mpProgram = GraphicsProgram::create(d);

    ProgramReflection::SharedConstPtr pReflector = mpProgram->getReflector();
    mpVars = GraphicsVars::create(pReflector);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
    Sampler::SharedPtr pPointSampler = Sampler::create(samplerDesc);
    mpVars->setSampler("gPointSampler", pPointSampler);
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
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

void LightFieldProbeRayTracing::setVarsData(Camera::SharedPtr& pCamera, LightFieldProbeVolume::SharedPtr& pProbe, Fbo::SharedPtr& pSceneGBufferFbo)
{
    mpVars->setTexture("gOctRadianceTex", pProbe->getRadianceTexture());
    mpVars->setTexture("gOctNormalTex", pProbe->getNormalTexture());
    mpVars->setTexture("gOctDistanceTex", pProbe->getDistanceTexture());
    mpVars->setTexture("gOctLowResDistanceTex", pProbe->getLowResDistanceTexture());
    mpVars->setTexture("gNormalTex", pSceneGBufferFbo->getColorTexture(GBufferRT::NORMAL_BITANGENT));
    mpVars->setTexture("gDiffuseOpacity", pSceneGBufferFbo->getColorTexture(GBufferRT::DIFFUSE_OPACITY));
    mpVars->setTexture("gSpecRoughTex", pSceneGBufferFbo->getColorTexture(GBufferRT::SPECULAR_ROUGHNESS));
    mpVars->setTexture("gDepthTex", pSceneGBufferFbo->getDepthStencilTexture());

    ParameterBlock* pDefaultBlock = mpVars->getDefaultBlock().get();
    // Update value of constants
    mConstantData.gViewProjMat = pCamera->getViewProjMatrix();
    mConstantData.gInvViewProjMat = pCamera->getInvViewProjMatrix();
    mConstantData.gCameraPos = float4(pCamera->getPosition(), 1);
    mConstantData.gProbeStep = pProbe->getProbeStep();
    mConstantData.gFrameCount += 1.0;
    mConstantData.gProbesCount = pProbe->getProbesCount();
    mConstantData.gProbeStartPosition = pProbe->getProbeStartPosition();
    mConstantData.gSizeHighRes = float2(pProbe->getDistanceTexture()->getWidth(), pProbe->getDistanceTexture()->getHeight());
    mConstantData.gSizeLowRes = float2(pProbe->getLowResDistanceTexture()->getWidth(), pProbe->getLowResDistanceTexture()->getHeight());
    // Update to GPU
    ConstantBuffer* pCB = pDefaultBlock->getConstantBuffer("PerFrameCB").get();
    assert(sizeof(mConstantData) <= pCB->getSize());
    pCB->setBlob(&mConstantData, 0, sizeof(mConstantData));
}

void LightFieldProbeRayTracing::execute(RenderContext* pContext,
    Camera::SharedPtr& pCamera,
    LightFieldProbeVolume::SharedPtr& pProbe,
    Fbo::SharedPtr& pSceneGBufferFbo,
    Fbo::SharedPtr& pTargetFbo)
{
    setVarsData(pCamera, pProbe, pSceneGBufferFbo);

    mpState->pushFbo(pTargetFbo);

    pContext->pushGraphicsState(mpState);
    pContext->pushGraphicsVars(mpVars);

    pContext->drawIndexed(3, 0, 0);

    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    mpState->popFbo();    
}

// TODO: implementation
RenderPassReflection LightFieldProbeRayTracing::reflect() const
{
    should_not_get_here();
    RenderPassReflection reflector;
    return reflector;
}

void LightFieldProbeRayTracing::execute(RenderContext* pContext, const RenderData* pRenderData)
{
    should_not_get_here();
}
