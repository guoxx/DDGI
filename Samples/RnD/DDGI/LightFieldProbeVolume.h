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
#include "Experimental/RenderPasses/GBufferRaster.h"
#include "LightFieldProbeShading.h"
#include "LightFieldProbeFiltering.h"
#include "OctahedralMapping.h"
#include "DownscalePass.h"

namespace Falcor
{
    class ProgramVars;
    class ConstantBuffer;
    class Gui;

    class LightFieldProbeVolume : public std::enable_shared_from_this<LightFieldProbeVolume>
    {
    public:
        using SharedPtr = std::shared_ptr<LightFieldProbeVolume>;
        using SharedConstPtr = std::shared_ptr<const LightFieldProbeVolume>;

        static SharedPtr create();

        ~LightFieldProbeVolume();

        void setScene(const Scene::SharedPtr& pScene);

        void renderUI(Gui* pGui, const char* group = nullptr);

        void setProbesCount(const glm::vec3 probesCount) { mProbesCount = probesCount; }

        int3 getProbesCount() const { return mProbesCount; }
        float3 getProbeStep() const { return mProbeStep; }
        float3 getProbeStartPosition() const { return mProbeStartPosition; }

        void update(RenderContext* pContext);

        Texture::SharedPtr getRadianceTexture() const { return mpRadianceFbo->getColorTexture(0); }
        Texture::SharedPtr getNormalTexture() const { return mpNormalFbo->getColorTexture(0); }
        Texture::SharedPtr getDistanceTexture() const { return mpDistanceFbo->getColorTexture(0); }
        Texture::SharedPtr getLowResDistanceTexture() const { return mpLowResDistanceFbo->getColorTexture(0); }
        Texture::SharedPtr getIrradianceTexture() const { return mpFilteredFbo->getColorTexture(0); }
        Texture::SharedPtr getDistanceMomentsTexture() const { return mpFilteredFbo->getColorTexture(1); }

        void debugDraw(RenderContext* pContext, Camera::SharedConstPtr pCamera, Fbo::SharedPtr pTargetFbo);

    private:
        LightFieldProbeVolume();

        void onProbesCountChanged();
        void onSceneBoundsChanged();

        void setProbesNeedToUpdate();

        void createFBOs();
        void updateProbesAllocation();

        void loadDebugResources();
        void unloadDebugResources();

        enum LightFieldDebugDisplay
        {
            Radiance,
            Irradiance,
            ProbeColor,
        };
        static const Gui::DropdownList sLightFieldDebugDisplayModeList;

        struct
        {
            LightFieldDebugDisplay debugDisplayMode = Radiance;
            Scene::SharedPtr pScene;
            SceneRenderer::SharedPtr pRenderer;

            GraphicsProgram::SharedPtr pProgram;
            GraphicsVars::SharedPtr pProgVars;
            GraphicsState::SharedPtr pPipelineState;
        } mDebugger;

        bool mVisualizeProbes = false;

        Scene::SharedPtr mpScene;
        BoundingBox mSceneBounds;

        int3 mProbesCount{2, 1, 1};
        float3 mProbeStep;
        float3 mProbeStartPosition;

        enum 
        {
            CubemapResolution = 1024,
            OctahedralResolution = 1024,
            OctahedralResolutionLowRes = 1024/32,
            FilteredFboResolution = 128,
            NumProbesUpdatePerFrame = 1,
        };

        Fbo::SharedPtr mpTempGBufferFbo;
        Fbo::SharedPtr mpTempLightFieldFbo;

        Fbo::SharedPtr mpRadianceFbo;
        Fbo::SharedPtr mpNormalFbo;
        Fbo::SharedPtr mpDistanceFbo;
        Fbo::SharedPtr mpLowResDistanceFbo;
        Fbo::SharedPtr mpFilteredFbo;

        CascadedShadowMaps::SharedPtr mpShadowPass;
        GBufferRaster::SharedPtr mpRaster;
        LightFieldProbeShading::SharedPtr mpShading;
        OctahedralMapping::SharedPtr mpOctMapping;
        LightFieldProbeFiltering::SharedPtr mpFiltering;
        DownscalePass::SharedPtr mpDownscalePass;

        struct LightFieldProbe
        {
            bool mUpdated = false;
            bool mVisible = false;
            bool mUpdateEveryFrame = false;
            int mProbeIdx;
            float3 mProbePosition;
        };
        std::vector<LightFieldProbe> mProbes;
        float mProbeSize = 1.0;
    };
}
