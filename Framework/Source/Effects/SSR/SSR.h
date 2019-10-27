#pragma once
#include "Experimental/RenderGraph/RenderPass.h"
#include "API/FBO.h"
#include "Graphics/FullScreenPass.h"
#include "Graphics/Program/ProgramVars.h"
#include "Effects/SSR/HZB.h"
#include "Effects/TAA/TAA.h"
#include "Utils/Gui.h"

#define DEBUG_SSR

namespace Falcor
{
    class Gui;
    class Camera;

    // TODO: full stochastic screen space reflection implementation
    // https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
    class ScreenSpaceReflection : public RenderPass, public inherit_shared_from_this<RenderPass, ScreenSpaceReflection>
    {
    public:
        using SharedPtr = std::shared_ptr<ScreenSpaceReflection>;
        static const char* kDesc;

        /** Destructor
        */
        ~ScreenSpaceReflection();

        /** Create a new instance
        */
        static SharedPtr create(const Dictionary& dict = {});
        static SharedPtr create(uint32_t width, uint32_t height);

        /** Render UI controls for this effect.
            \param[in] pGui GUI object to render UI elements with
        */
        virtual void renderUI(Gui* pGui, const char* uiGroup = "") override;

        /** Run the effect
        */
        void execute(RenderContext* pRenderContext,
                     const Camera* pCamera,
                     const Texture::SharedPtr& pColorIn,
                     const Texture::SharedPtr& pHZBTexture,
                     const Fbo::SharedPtr& pGBufferFbo,
                     const Fbo::SharedPtr& pFbo);

        virtual void onResize(uint32_t width, uint32_t height) override;

        // Render-pass stuff
        virtual void execute(RenderContext* pContext, const RenderData* pData) override;
        virtual RenderPassReflection reflect() const override;
        std::string getDesc() override { return kDesc; }

    private:
        ScreenSpaceReflection(int32_t width, int32_t height);

        void init(int32_t width, int32_t height);

        void createGraphicsResources();

        //  Set the Variable Data needed for Rendering.
        void setVarsData(const Camera* pCamera,
                         const Texture::SharedPtr& pColorIn,
                         const Texture::SharedPtr& pHZBTexture,
                         const Fbo::SharedPtr& pGBufferFbo);

        struct
        {
            ProgramReflection::BindLocation perFrameCB;
#ifdef DEBUG_SSR
            ProgramReflection::BindLocation debugCB;
            ProgramReflection::BindLocation debugTex;
            ProgramReflection::BindLocation debugData;
#endif
            ProgramReflection::BindLocation pointSampler;
            ProgramReflection::BindLocation HZBTex;
            ProgramReflection::BindLocation normalTex;
            ProgramReflection::BindLocation specRoughTex;
            ProgramReflection::BindLocation colorTex;
        } mBindLocations;

        struct
        {
            glm::mat4x4 gInvProjMat;
            glm::mat4x4 gViewMat;
            glm::mat4x4 gProjToPixelMat;

            glm::vec4 gZBufferSize;
            glm::vec4 gHZBSize;

            float gNearZ;
            float gFarZ;
            float gHZBMipCount;
            float gFrameCount = 0;
            float gZThickness = 0.05f;
            float gRayStrideDDA = 1;
            float gJitterFraction = 1;
            float gMaxRayTraceDistance = 100;
            float gMaxStepsHZB = 64;
            float gMaxStepsDDA = 2048;
        } mConstantData;

        bool mHiZRayTrace = true;
        GraphicsProgram::SharedPtr mpProgram;
        GraphicsVars::SharedPtr mpProgVars;
        GraphicsState::SharedPtr mpPipelineState;

        Sampler::SharedPtr mpPointSampler;

#ifdef DEBUG_SSR
        static const Gui::DropdownList kDebugModeDropdown;

        enum DebugMode
        {
            None,
            DebugIntersectionResult,
            DebugRaymarchCount,
            DebugRayPath,
        } mDebugMode = None;
        glm::ivec2 mDebugPixel{-1, -1};

        TypedBuffer<glm::vec4>::SharedPtr mpDebugData;
        Fbo::SharedPtr mDebugFbo;
#endif
    };
}
