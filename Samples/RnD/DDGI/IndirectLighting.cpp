#include "IndirectLighting.h"
#include "Experimental/RenderPasses/GBuffer.h"

using namespace Falcor;

IndirectLighting::SharedPtr IndirectLighting::create(Type type)
{
    auto ptr = new IndirectLighting(type);
    return SharedPtr(ptr);
}

IndirectLighting::IndirectLighting(Type type) : RenderPass("IndirectLighting")
{
    GraphicsProgram::Desc d;
    d.setOptimizationLevel(Shader::OptimizationLevel::Maximal);
    //d.setCompilerFlags(Shader::CompilerFlags::EmitDebugInfo);
    //d.setCompilerFlags(Shader::CompilerFlags::HLSLShader);
    d.addShaderLibrary("IndirectLighting.slang").vsEntry("VSMain").psEntry("PSMain");
    Program::DefineList definesList;
    if (type == IndirectLighting::Diffuse)
    {
        definesList.add("_DIFFUSE", "1");
    }
    else if (type == IndirectLighting::Specular)
    {
        definesList.add("_SPECULAR", "1");
    }
    mpProgram = GraphicsProgram::create(d, definesList);

    ProgramReflection::SharedConstPtr pReflector = mpProgram->getReflector();
    mpVars = GraphicsVars::create(pReflector);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap);
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

void IndirectLighting::setVarsData(Camera::SharedPtr& pCamera, Texture::SharedPtr& pLightingTex, Fbo::SharedPtr& pSceneGBufferFbo)
{
    mpVars->setTexture("gLightingTex", pLightingTex);
    mpVars->setTexture("gNormalTex", pSceneGBufferFbo->getColorTexture(GBufferRT::NORMAL_BITANGENT));
    mpVars->setTexture("gDiffuseOpacity", pSceneGBufferFbo->getColorTexture(GBufferRT::DIFFUSE_OPACITY));
    mpVars->setTexture("gSpecRoughTex", pSceneGBufferFbo->getColorTexture(GBufferRT::SPECULAR_ROUGHNESS));
    mpVars->setTexture("gDepthTex", pSceneGBufferFbo->getDepthStencilTexture());

    ParameterBlock* pDefaultBlock = mpVars->getDefaultBlock().get();
    // Update value of constants
    mConstantData.gInvViewProjMat = pCamera->getInvViewProjMatrix();
    mConstantData.gCameraPos = float4(pCamera->getPosition(), 1);
    // Update to GPU
    ConstantBuffer* pCB = pDefaultBlock->getConstantBuffer("PerFrameCB").get();
    assert(sizeof(mConstantData) <= pCB->getSize());
    pCB->setBlob(&mConstantData, 0, sizeof(mConstantData));
}

void IndirectLighting::execute(RenderContext* pContext,
    Camera::SharedPtr& pCamera,
    Texture::SharedPtr& pLightingTex,
    Fbo::SharedPtr& pSceneGBufferFbo,
    Fbo::SharedPtr& pTargetFbo)
{
    setVarsData(pCamera, pLightingTex, pSceneGBufferFbo);

    mpState->pushFbo(pTargetFbo);

    pContext->pushGraphicsState(mpState);
    pContext->pushGraphicsVars(mpVars);

    pContext->drawIndexed(3, 0, 0);

    pContext->popGraphicsVars();
    pContext->popGraphicsState();

    mpState->popFbo();    
}

// TODO: implementation
RenderPassReflection IndirectLighting::reflect() const
{
    should_not_get_here();
    RenderPassReflection reflector;
    return reflector;
}

void IndirectLighting::execute(RenderContext* pContext, const RenderData* pRenderData)
{
    should_not_get_here();
}
