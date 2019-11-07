#pragma once
#include <Falcor.h>
#include "LightFieldProbeVolume.h"

namespace Falcor
{
    class IndirectLighting : public RenderPass, inherit_shared_from_this<RenderPass, IndirectLighting>
    {
    public:
        enum Type
        {
            Diffuse,
            Specular,
        };

        using SharedPtr = std::shared_ptr<IndirectLighting>;

        static SharedPtr create(Type type);

        void execute(RenderContext* pContext,
            Camera::SharedPtr& pCamera,
            Texture::SharedPtr& pLightingTex,
            Fbo::SharedPtr& pSceneGBufferFbo,
            Fbo::SharedPtr& pTargetFbo);

        virtual RenderPassReflection reflect() const override;
        virtual void execute(RenderContext* pContext, const RenderData* pRenderData) override;
        virtual std::string getDesc() override { return "Indirect lighting"; }

    private:
        IndirectLighting(Type type);

        void setVarsData(Camera::SharedPtr& pCamera, Texture::SharedPtr& pLightingTex, Fbo::SharedPtr& pSceneGBufferFbo);

        GraphicsState::SharedPtr mpState;
        GraphicsProgram::SharedPtr mpProgram;
        GraphicsVars::SharedPtr mpVars;

        struct
        {
            float4x4 gInvViewProjMat;
            float4 gCameraPos;
        } mConstantData;
    };
}
