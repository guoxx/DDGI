#pragma once
#include <Falcor.h>
#include "LightFieldProbeVolume.h"

namespace Falcor
{
    class LightFieldProbeRayTracing : public RenderPass, inherit_shared_from_this<RenderPass, LightFieldProbeRayTracing>
    {
    public:
        using SharedPtr = std::shared_ptr<LightFieldProbeRayTracing>;

        static SharedPtr create(const Dictionary& dictionary = {});

        void execute(RenderContext* pContext,
            Camera::SharedPtr& pCamera,
            LightFieldProbeVolume::SharedPtr& pProbe,
            Fbo::SharedPtr& pSceneGBufferFbo,
            Fbo::SharedPtr& pTargetFbo);

        virtual RenderPassReflection reflect() const override;
        virtual void execute(RenderContext* pContext, const RenderData* pRenderData) override;
        virtual std::string getDesc() override { return "Light Field Probe Ray Tracing"; }

    private:
        LightFieldProbeRayTracing();

        void setVarsData(Camera::SharedPtr& pCamera, LightFieldProbeVolume::SharedPtr& pProbe, Fbo::SharedPtr& pSceneGBufferFbo);

        GraphicsState::SharedPtr mpState;
        GraphicsProgram::SharedPtr mpProgram;
        GraphicsVars::SharedPtr mpVars;

        struct
        {
            float4x4 gViewProjMat;
            float4x4 gInvViewProjMat;
            float4 gCameraPos;

            int3 gProbesCount;
            float gDummy0;
            float3 gProbeStartPosition;
            float gDummy1;
            float3 gProbeStep;
            float gDummy2;
            float2 gSizeHighRes;
            float2 gSizeLowRes;
        } mConstantData;
    };
}
