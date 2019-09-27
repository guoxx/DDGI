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
#include "LightFieldProbe.h"

namespace Falcor
{
    LightFieldProbe::LightFieldProbe()
    {
        mpRaster = GBufferRaster::create();
        mpShading = GBufferShading::create();
        mpOctMapping = OctahedralMapping::create();

        Fbo::Desc fboDesc;
        fboDesc.setColorTarget(0, ResourceFormat::RGBA16Float);
        mpRadianceFbo = FboHelper::create2D(mOctahedralResolution, mOctahedralResolution, fboDesc);
        mpNormalFbo = FboHelper::create2D(mOctahedralResolution, mOctahedralResolution, fboDesc);
        fboDesc.setColorTarget(0, ResourceFormat::R32Float);
        mpDistanceFbo = FboHelper::create2D(mOctahedralResolution, mOctahedralResolution, fboDesc);

        loadDebugResources();
    }

    void LightFieldProbe::loadDebugResources()
    {
        // Initialize an empty scene
        mDebugger.pScene = Scene::create();
        mDebugger.pRenderer = SceneRenderer::create(mDebugger.pScene);

        // Add sphere to the scene
        Model::SharedPtr pModel = Model::createFromFile("UnitSphere.fbx");
        // Scale to about unit size
        float scaling = 0.5f / pModel->getRadius();
        mDebugger.pScene->addModelInstance(pModel, "", mPosition, vec3(), vec3(scaling));

        // Initialize graphics resources
        mDebugger.pPipelineState = GraphicsState::create();

        mDebugger.pProgram = GraphicsProgram::createFromFile("LightFieldProbeViewer.ps.slang", "", "main");
        mDebugger.pPipelineState->setProgram(mDebugger.pProgram);

        auto pRasterizerState = RasterizerState::create(RasterizerState::Desc().setCullMode(RasterizerState::CullMode::Back));
        auto pDepthState = DepthStencilState::create(DepthStencilState::Desc().setDepthTest(true));
        mDebugger.pPipelineState->setRasterizerState(pRasterizerState);
        mDebugger.pPipelineState->setDepthStencilState(pDepthState);

        mDebugger.pProgVars = GraphicsVars::create(mDebugger.pProgram->getActiveVersion()->getReflector());
        auto pLinearSampler = Sampler::create(Sampler::Desc().setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear));
        mDebugger.pProgVars->setSampler("gLinearSampler", pLinearSampler);
        mDebugger.pProgVars->setTexture("gColorTex", mpRadianceFbo->getColorTexture(0));
    }

    void LightFieldProbe::unloadDebugResources()
    {
        // TODO
    }

    void LightFieldProbe::debugDraw(RenderContext* pContext, Camera::SharedConstPtr pCamera, Fbo::SharedPtr pTargetFbo)
    {
        mDebugger.pPipelineState->setFbo(pTargetFbo);

        pContext->pushGraphicsState(mDebugger.pPipelineState);
        pContext->pushGraphicsVars(mDebugger.pProgVars);
        mDebugger.pRenderer->renderScene(pContext, pCamera.get());
        pContext->popGraphicsVars();
        pContext->popGraphicsState();
    }

    LightFieldProbe::~LightFieldProbe()
    {
    }

    LightFieldProbe::SharedPtr LightFieldProbe::create()
    {
        return SharedPtr(new LightFieldProbe());
    }

    void LightFieldProbe::setScene(const Scene::SharedPtr& pScene)
    {
        mpScene = pScene;
        mpRaster->setScene(pScene);
        mpShading->setScene(pScene);

        if (!mpShadowPass)
        {
            mpShadowPass = CascadedShadowMaps::create(pScene->getLight(0), 512, 512, mCubemapResolution, mCubemapResolution, pScene);
            mpShadowPass->setFilterMode(CsmFilterPoint);
            mpShadowPass->toggleMinMaxSdsm(false);
        }
    }

    void LightFieldProbe::setPosition(const glm::vec3& p)
    {
        mPosition = p;

        if (mDebugger.pScene)
        {
            mDebugger.pScene->getModelInstance(0, 0)->setTranslation(mPosition, false);
        }
    }

    void LightFieldProbe::update(RenderContext* pContext)
    {
        glm::vec3 targetVec[6] = {
            glm::vec3( 1,  0,  0),
            glm::vec3(-1,  0,  0),
            glm::vec3( 0,  1,  0),
            glm::vec3( 0, -1,  0),
            glm::vec3( 0,  0,  1),
            glm::vec3( 0,  0, -1),
        };

        glm::vec3 upVec[6] = {
            glm::vec3( 0,  1,  0),
            glm::vec3( 0,  1,  0),
            glm::vec3( 0,  0,  1),
            glm::vec3( 0,  0, -1),
            glm::vec3( 0,  1,  0),
            glm::vec3( 0,  1,  0),
        };

        Fbo::SharedPtr pGBufferFbo = GBufferRaster::createGBufferFbo(mCubemapResolution, mCubemapResolution, true);;
        Fbo::Desc lfFboDesc;
        lfFboDesc.setColorTarget(0, ResourceFormat::RGBA16Float);
        lfFboDesc.setColorTarget(1, ResourceFormat::RGBA16Float);
        lfFboDesc.setColorTarget(2, ResourceFormat::R32Float);
        Fbo::SharedPtr pLightFieldFbos[6];
        for (int i = 0; i < ARRAYSIZE(pLightFieldFbos); ++i)
        {
            pLightFieldFbos[i] = FboHelper::create2D(mCubemapResolution, mCubemapResolution, lfFboDesc);
        }

        for (int i = 0; i < ARRAYSIZE(targetVec); ++i)
        {
            pContext->clearFbo(pGBufferFbo.get(), vec4(0), 1.f, 0, FboAttachmentType::All);
            pContext->clearFbo(pLightFieldFbos[i].get(), vec4(0), 1.f, 0, FboAttachmentType::All);

            Camera::SharedPtr pCamera = Camera::create();
            pCamera->setPosition(mPosition);
            pCamera->setUpVector(upVec[i]);
            pCamera->setTarget(mPosition + targetVec[i]);
            pCamera->setAspectRatio(1.0);
            pCamera->setDepthRange(0.01f, 10);
            pCamera->setFocalLength(fovYToFocalLength((float)M_PI_2, Camera::kDefaultFrameHeight));

            mpRaster->execute(pContext, pGBufferFbo, pCamera);

            mpShadowPass->generateVisibilityBuffer(pContext, pCamera.get(), pGBufferFbo->getDepthStencilTexture());

            mpShading->setCamera(pCamera);
            mpShading->execute(pContext, pGBufferFbo, mpShadowPass->getVisibilityBuffer(), pLightFieldFbos[i]);
        }

        Fbo::SharedPtr pTargetFbos[3] = { mpRadianceFbo, mpNormalFbo, mpDistanceFbo };
        for (int i = 0; i < 3; ++i)
        {
            mpOctMapping->execute(pContext,
                pLightFieldFbos[0]->getColorTexture(i), pLightFieldFbos[1]->getColorTexture(i),
                pLightFieldFbos[2]->getColorTexture(i), pLightFieldFbos[3]->getColorTexture(i),
                pLightFieldFbos[4]->getColorTexture(i), pLightFieldFbos[5]->getColorTexture(i),
                pTargetFbos[i]);
        }
    }

    void LightFieldProbe::move(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up)
    {
        logWarning("Light probes don't support paths. Expect absolutely nothing to happen");
    }

}
