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
#include "LightFieldProbeVolume.h"

//#pragma optimize("", off)

namespace Falcor
{
    const Gui::DropdownList LightFieldProbeVolume::sLightFieldDebugDisplayModeList =
    {
        { LightFieldDebugDisplay::Radiance, "Radiance" },
        { LightFieldDebugDisplay::Irradiance, "Irradiance" },
        { LightFieldDebugDisplay::ProbeColor, "Probe Color" }
    };

    class ProbesRenderer : public SceneRenderer
    {
    public:
        using SceneRenderer::SceneRenderer;

        static ProbesRenderer::SharedPtr create(const Scene::SharedPtr& pScene)
        {
            return SharedPtr(new ProbesRenderer(pScene));
        }

        bool setPerModelInstanceData(const CurrentWorkingData& currentData, const Scene::ModelInstance* pModelInstance, uint32_t instanceID) override final
        {
            currentData.pVars->getConstantBuffer("PerInstanceData")["gProbeIdx"] = (int)instanceID;
            return true;
        }
    };

    LightFieldProbeVolume::LightFieldProbeVolume()
    {
        mpRaster = GBufferRaster::create(RasterizerState::CullMode::None);
        mpShading = LightFieldProbeShading::create();
        mpOctMapping = OctahedralMapping::create();
        mpFiltering = LightFieldProbeFiltering::create();
        mpDownscalePass = DownscalePass::create();

        loadDebugResources();

        onProbesCountChanged();
    }

    void LightFieldProbeVolume::loadDebugResources()
    {
        // Initialize an empty scene
        mDebugger.pScene = Scene::create();
        mDebugger.pRenderer = ProbesRenderer::create(mDebugger.pScene);

        // Initialize graphics resources
        mDebugger.pPipelineState = GraphicsState::create();

        Program::DefineList definesList;
        if (mDebugger.debugDisplayMode == ProbeColor)
            definesList.add("_PROBE_COLOR", "1");
        mDebugger.pProgram = GraphicsProgram::createFromFile("LightFieldProbeViewer.ps.slang", "", "main", definesList);

        auto pRasterizerState = RasterizerState::create(RasterizerState::Desc().setCullMode(RasterizerState::CullMode::Back));
        auto pDepthState = DepthStencilState::create(DepthStencilState::Desc().setDepthTest(true));
        mDebugger.pPipelineState->setRasterizerState(pRasterizerState);
        mDebugger.pPipelineState->setDepthStencilState(pDepthState);

        mDebugger.pProgVars = GraphicsVars::create(mDebugger.pProgram->getActiveVersion()->getReflector());
        auto pSampler = Sampler::create(Sampler::Desc().setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point));
        mDebugger.pProgVars->setSampler("gSampler", pSampler);
    }

    void LightFieldProbeVolume::unloadDebugResources()
    {
        // TODO
    }

    void LightFieldProbeVolume::debugDraw(RenderContext* pContext, Camera::SharedConstPtr pCamera, Fbo::SharedPtr pTargetFbo)
    {
        if (std::find_if(mProbes.cbegin(), mProbes.cend(), [](const LightFieldProbe& p) { return p.mVisible; }) == mProbes.cend())
        {
            // No visible probes
            return;
        }

        if (mDebugger.debugDisplayMode == ProbeColor)
        {
        }
        else if (mDebugger.debugDisplayMode == Radiance)
        {
            mDebugger.pProgVars->setTexture("gColorTex", mpRadianceFbo->getColorTexture(0));
        }
        else if (mDebugger.debugDisplayMode == Irradiance)
        {
            mDebugger.pProgVars->setTexture("gColorTex", mpFilteredFbo->getColorTexture(0));
        }
        mDebugger.pProgVars["PerInstanceData"]["gProbeCounts"] = mProbesCount;

        mDebugger.pPipelineState->setProgram(mDebugger.pProgram);
        mDebugger.pPipelineState->setFbo(pTargetFbo);

        pContext->pushGraphicsState(mDebugger.pPipelineState);
        pContext->pushGraphicsVars(mDebugger.pProgVars);
        mDebugger.pRenderer->renderScene(pContext, pCamera.get());
        pContext->popGraphicsVars();
        pContext->popGraphicsState();
    }

    LightFieldProbeVolume::~LightFieldProbeVolume()
    {
    }

    LightFieldProbeVolume::SharedPtr LightFieldProbeVolume::create()
    {
        return SharedPtr(new LightFieldProbeVolume());
    }

    void LightFieldProbeVolume::setScene(const Scene::SharedPtr& pScene)
    {
        mSceneBounds = pScene->getBoundingBox();
        onSceneBoundsChanged();

        mpScene = pScene;
        mpRaster->setScene(pScene);
        mpShading->setScene(pScene);

        if (!mpShadowPass)
        {
            mpShadowPass = CascadedShadowMaps::create(pScene->getLight(0), 512, 512, CubemapResolution, CubemapResolution, pScene);
            mpShadowPass->setFilterMode(CsmFilterPoint);
            mpShadowPass->toggleMinMaxSdsm(false);
        }
    }

    void LightFieldProbeVolume::renderUI(Gui* pGui, const char* group)
    {
        if (pGui->beginGroup(group))
        {
            if (pGui->addButton("Update All Probes"))
            {
                onProbesCountChanged();
            }

            if (pGui->addCheckBox("Visualize All Probes", mVisualizeProbes))
            {
                for (auto& p: mProbes)
                {
                    p.mVisible = mVisualizeProbes;
                    mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setVisible(p.mVisible);
                }
            }

            if (pGui->addDropdown("Probe Display Mode (DEBUG)", sLightFieldDebugDisplayModeList, ((uint32_t&)mDebugger.debugDisplayMode)))
            {
                if (mDebugger.debugDisplayMode == ProbeColor)
                {
                    mDebugger.pProgram->addDefine("_PROBE_COLOR", "1");
                }
                else
                {
                    mDebugger.pProgram->removeDefine("_PROBE_COLOR");
                }
            }

            if (pGui->addInt3Var("Probes Count", mProbesCount, 1, 1024))
            {
                mProbesCount.x = getNextPowerOf2(mProbesCount.x);
                mProbesCount.y = getNextPowerOf2(mProbesCount.y);
                mProbesCount.z = getNextPowerOf2(mProbesCount.z);
            }

            if (pGui->addFloatSlider("Probe Size", mProbeSize, 0.01f, 1.0f))
            {
                float scaling = mProbeSize*0.5f/mDebugger.pScene->getModel(0)->getRadius();
                for (const auto& p : mProbes)    
                {
                    mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setScaling(float3(scaling));
                }
            }

            if (pGui->beginGroup("Light Field Probes"))
            {
                for (auto& p : mProbes)
                {
                    if (pGui->beginGroup(std::string("Probe Index ") + std::to_string(p.mProbeIdx), true))
                    {
                        float3 tmpPos = p.mProbePosition;
                        pGui->addFloat3Var("Position", tmpPos);
                        if (pGui->addCheckBox("Visible", p.mVisible))
                        {
                            mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setVisible(p.mVisible);
                        }
                        pGui->addCheckBox("Update Every Frame", p.mUpdateEveryFrame);
                        pGui->endGroup();
                    }
                }

                pGui->endGroup();
            }

            pGui->endGroup();
        }
    }

    void LightFieldProbeVolume::onProbesCountChanged()
    {
        createFBOs();
        updateProbesAllocation();
        setProbesNeedToUpdate();
    }

    void LightFieldProbeVolume::onSceneBoundsChanged()
    {
        updateProbesAllocation();
        setProbesNeedToUpdate();
    }

    void LightFieldProbeVolume::setProbesNeedToUpdate()
    {
        for (auto& p : mProbes)
        {
            p.mUpdated = false;
        }
    }

    void LightFieldProbeVolume::createFBOs()
    {
        uint32_t numProbes = mProbesCount.x*mProbesCount.y*mProbesCount.z;
        Fbo::Desc fboDesc;
        fboDesc.setColorTarget(0, ResourceFormat::R11G11B10Float);
        mpRadianceFbo = FboHelper::create2D(OctahedralResolution, OctahedralResolution, fboDesc, numProbes);
        fboDesc.setColorTarget(0, ResourceFormat::RGBA8Unorm);
        mpNormalFbo = FboHelper::create2D(OctahedralResolution, OctahedralResolution, fboDesc, numProbes);
        fboDesc.setColorTarget(0, ResourceFormat::R16Float);
        mpDistanceFbo = FboHelper::create2D(OctahedralResolution, OctahedralResolution, fboDesc, numProbes);
        mpLowResDistanceFbo = FboHelper::create2D(OctahedralResolutionLowRes, OctahedralResolutionLowRes, fboDesc, numProbes);

        Fbo::Desc filterredFboDesc;
        filterredFboDesc.setColorTarget(0, ResourceFormat::R11G11B10Float);
        filterredFboDesc.setColorTarget(1, ResourceFormat::RG16Float);
        mpFilteredFbo = FboHelper::create2D(FilteredFboResolution, FilteredFboResolution, filterredFboDesc, numProbes);

        mpTempGBufferFbo = GBufferRaster::createGBufferFbo(CubemapResolution, CubemapResolution, true);;
        Fbo::Desc lfFboDesc;
        lfFboDesc.setColorTarget(0, ResourceFormat::RGBA16Float);
        lfFboDesc.setColorTarget(1, ResourceFormat::RGBA16Float);
        lfFboDesc.setColorTarget(2, ResourceFormat::R32Float);
        mpTempLightFieldFbo = FboHelper::createCubemap(CubemapResolution, CubemapResolution, lfFboDesc);
    }

    void LightFieldProbeVolume::updateProbesAllocation()
    {
        const BoundingBox& bbox = mSceneBounds;
        const float3 minPos = bbox.getMinPos();
        const float3 maxPos = bbox.getMaxPos();

        mProbeStep = (maxPos - minPos) / float3(mProbesCount + 1);
        mProbeStartPosition = minPos + mProbeStep;

        mProbes.clear();
        mProbes.reserve(mProbesCount.x*mProbesCount.y*mProbesCount.z);
        for (int iy = 0; iy < mProbesCount.y; ++iy)
        {
            for (int iz = 0; iz < mProbesCount.z; ++iz)
            {
                for (int ix = 0; ix < mProbesCount.x; ++ix)
                {
                    LightFieldProbe p;
                    p.mUpdated = false;
                    p.mVisible = mVisualizeProbes;
                    p.mProbeIdx = ix + iy * mProbesCount.x + iz * mProbesCount.x * mProbesCount.y;
                    p.mProbePosition = mProbeStartPosition + mProbeStep * float3(ix, iy, iz);
                    mProbes.push_back(p);
                }
            }
        }

        // Add sphere to the scene
        int sphereModelId = 0;
        Model::SharedPtr pModel = nullptr;
        if (mDebugger.pScene->getModelCount() > 0)
        {
            pModel = mDebugger.pScene->getModel(sphereModelId);
        }
        else 
        {
            pModel = Model::createFromFile("UnitSphere.fbx");
        }

        int numProbes = (int)mProbes.size();
        int numSpheres = (mDebugger.pScene->getModelCount() > 0) ? mDebugger.pScene->getModelInstanceCount(sphereModelId) : 0;
        for (int i = numSpheres; i < numProbes; ++i)
        {
            mDebugger.pScene->addModelInstance(pModel, "");
        }
        for (int i = numSpheres-1; i > numProbes-1; --i)
        {
            mDebugger.pScene->deleteModelInstance(sphereModelId, i);
        }
        assert(mProbes.size() == mDebugger.pScene->getModelInstanceCount(sphereModelId));

        for (const auto& p : mProbes) {
            mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setTranslation(p.mProbePosition, false);
            mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setVisible(p.mVisible);
            mDebugger.pScene->getModelInstance(0, p.mProbeIdx)->setScaling(vec3(mProbeSize*0.5f/pModel->getRadius()));
        }
    }

    void LightFieldProbeVolume::update(RenderContext* pContext)
    {
        int cnt = NumProbesUpdatePerFrame;
        for (auto& probe : mProbes)
        {
            bool updateProbe = probe.mUpdateEveryFrame || !probe.mUpdated;
            updateProbe = updateProbe && cnt > 0;
            if (!updateProbe) { continue; }

            GPU_EVENT(pContext, "UpdateProbe");
            probe.mUpdated = true;
            --cnt;

            glm::vec3 targetVec[6] = {
                glm::vec3(1, 0, 0),
                glm::vec3(-1, 0, 0),
                glm::vec3(0, 1, 0),
                glm::vec3(0, -1, 0),
                glm::vec3(0, 0, -1),
                glm::vec3(0, 0, 1),
            };

            glm::vec3 upVec[6] = {
                glm::vec3(0, 1, 0),
                glm::vec3(0, 1, 0),
                glm::vec3(0, 0, 1),
                glm::vec3(0, 0, -1),
                glm::vec3(0, 1, 0),
                glm::vec3(0, 1, 0),
            };

            for (int i = 0; i < ARRAYSIZE(targetVec); ++i)
            {
                pContext->clearFbo(mpTempGBufferFbo.get(), vec4(0), 1.f, 0, FboAttachmentType::All);

                Camera::SharedPtr pCamera = Camera::create();
                pCamera->setPosition(probe.mProbePosition);
                pCamera->setUpVector(upVec[i]);
                pCamera->setTarget(probe.mProbePosition + targetVec[i]);
                pCamera->setAspectRatio(1.0);
                pCamera->setDepthRange(0.01f, 10);
                pCamera->setFocalLength(fovYToFocalLength((float)M_PI_2, Camera::kDefaultFrameHeight));

                mpRaster->execute(pContext, mpTempGBufferFbo, pCamera);

                mpShadowPass->generateVisibilityBuffer(pContext, pCamera.get(), mpTempGBufferFbo->getDepthStencilTexture());

                Fbo::SharedPtr tmpTempLightFieldFbo = Fbo::create();
                tmpTempLightFieldFbo->attachColorTarget(mpTempLightFieldFbo->getColorTexture(0), 0, 0, i);
                tmpTempLightFieldFbo->attachColorTarget(mpTempLightFieldFbo->getColorTexture(1), 1, 0, i);
                tmpTempLightFieldFbo->attachColorTarget(mpTempLightFieldFbo->getColorTexture(2), 2, 0, i);
                mpShading->setCamera(pCamera);
                mpShading->execute(pContext, mpTempGBufferFbo, mpShadowPass->getVisibilityBuffer(), tmpTempLightFieldFbo);
            }

            Fbo::SharedPtr tmpRadianceFbo = Fbo::create();
            Fbo::SharedPtr tmpNormalFbo = Fbo::create();
            Fbo::SharedPtr tmpDistanceFbo = Fbo::create();
            Fbo::SharedPtr tmpLowResDistanceFbo = Fbo::create();
            tmpRadianceFbo->attachColorTarget(mpRadianceFbo->getColorTexture(0), 0, 0, probe.mProbeIdx);
            tmpNormalFbo->attachColorTarget(mpNormalFbo->getColorTexture(0), 0, 0, probe.mProbeIdx);
            tmpDistanceFbo->attachColorTarget(mpDistanceFbo->getColorTexture(0), 0, 0, probe.mProbeIdx);
            tmpLowResDistanceFbo->attachColorTarget(mpLowResDistanceFbo->getColorTexture(0), 0, 0, probe.mProbeIdx);

            Fbo::SharedPtr pTargetFbos[3] = {tmpRadianceFbo, tmpNormalFbo, tmpDistanceFbo};
            for (int i = 0; i < 3; ++i)
            {
                mpOctMapping->execute(pContext, mpTempLightFieldFbo->getColorTexture(i), pTargetFbos[i]);
            }

            Fbo::SharedPtr tmpFilteredFbo = Fbo::create();
            tmpFilteredFbo->attachColorTarget(mpFilteredFbo->getColorTexture(0), 0, 0, probe.mProbeIdx);
            tmpFilteredFbo->attachColorTarget(mpFilteredFbo->getColorTexture(1), 1, 0, probe.mProbeIdx);
            mpFiltering->execute(pContext, tmpRadianceFbo->getColorTexture(0), tmpDistanceFbo->getColorTexture(0), probe.mProbeIdx, tmpFilteredFbo);

            mpDownscalePass->execute(pContext, tmpDistanceFbo->getColorTexture(0), probe.mProbeIdx, tmpLowResDistanceFbo);
        }
    }
}
