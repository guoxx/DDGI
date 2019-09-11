#pragma once
#include "Experimental/RenderGraph/RenderPass.h"
#include "Graphics/GraphicsState.h"
#include "Graphics/Program/GraphicsProgram.h"
#include "Graphics/Program/ProgramVars.h"

namespace Falcor
{
    class Camera;

    class HierarchicalZBuffer : public RenderPass, public inherit_shared_from_this<RenderPass, HierarchicalZBuffer>
    {
    public:
        using SharedPtr = std::shared_ptr<HierarchicalZBuffer>;
        static const char* kDesc;

        /** Destructor
        */
        ~HierarchicalZBuffer();

        /** Create a new instance
        */
        static SharedPtr create(const Dictionary& dict = {});

        /** Helper function to create a HiZ texture
        */
        static Texture::SharedPtr createHZBTexture(int32_t w, int32_t h);

        /** Run the effect
        */
        void execute(RenderContext* pRenderContext,
                     const Camera* pCamera,
                     const Texture::SharedPtr& pDepthTexture,
                     const Texture::SharedPtr& pHiZ);

        // Render-pass stuff
        virtual void execute(RenderContext* pContext, const RenderData* pData) override;
        virtual RenderPassReflection reflect() const override;
        std::string getDesc() override { return kDesc; }

    private:
        HierarchicalZBuffer();

        void init();
        void createGraphicsResources();

        struct
        {
            ProgramReflection::BindLocation perFrameCB;
            ProgramReflection::BindLocation pointSampler;
            ProgramReflection::BindLocation depthTex;
        } mBindLocations;

        GraphicsProgram::SharedPtr mpFirstIterProgram;
        GraphicsProgram::SharedPtr mpReductionProgram;
        GraphicsVars::SharedPtr mpProgVars;
        GraphicsState::SharedPtr mpPipelineState;

        Sampler::SharedPtr mpPointSampler;
    };
}
