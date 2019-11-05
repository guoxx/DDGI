#pragma once

#include "Falcor.h"

// Take from https://github.com/philcn/RaysRenderer
class SVGFPass
{
public:
    using SharedPtr = std::shared_ptr<SVGFPass>;

    static SharedPtr create(uint32_t width, uint32_t height);
    SVGFPass(uint32_t width, uint32_t height);
    ~SVGFPass();

    Falcor::Texture::SharedPtr Execute(
        Falcor::RenderContext* renderContext,
        Falcor::Texture::SharedPtr inputSignal,
        Falcor::Texture::SharedPtr motionVec,
        Falcor::Texture::SharedPtr linearZ,
        Falcor::Texture::SharedPtr normalDepth);

    void RenderGui(Falcor::Gui* gui, const char* group = "");

private:
    void TemporalReprojection(Falcor::RenderContext* renderContext);
    void SpatialVarianceEstimation(Falcor::RenderContext* renderContext);
    void AtrousFilter(Falcor::RenderContext* renderContext, uint32_t iteration, Falcor::Fbo::SharedPtr input, Falcor::Fbo::SharedPtr output);

    Falcor::FullScreenPass::UniquePtr mReprojectionPass;
    Falcor::GraphicsVars::SharedPtr mReprojectionVars;
    Falcor::GraphicsState::SharedPtr mReprojectionState;

    Falcor::FullScreenPass::UniquePtr mVarianceEstimationPass;
    Falcor::GraphicsVars::SharedPtr mVarianceEstimationVars;
    Falcor::GraphicsState::SharedPtr mVarianceEstimationState;

    Falcor::FullScreenPass::UniquePtr mAtrousPass;
    Falcor::GraphicsVars::SharedPtr mAtrousVars;
    Falcor::GraphicsState::SharedPtr mAtrousState;

    Falcor::Fbo::SharedPtr mAtrousPingFbo;
    Falcor::Fbo::SharedPtr mAtrousPongFbo;

    Falcor::Fbo::SharedPtr mLastFilteredFbo;

    Falcor::Fbo::SharedPtr mCurrReprojFbo;
    Falcor::Fbo::SharedPtr mPrevReprojFbo;

    Falcor::Fbo::SharedPtr mOutputFbo;

    Falcor::Texture::SharedPtr mPrevLinearZTexture;

    uint32_t mAtrousIterations;
    uint32_t mFeedbackTap;
    uint32_t mAtrousRadius;
    float mAlpha;
    float mMomentsAlpha;
    float mPhiColor;
    float mPhiNormal;
    bool mEnableTemporalReprojection;
    bool mEnableSpatialVarianceEstimation;

    struct
    {
        Falcor::Texture::SharedPtr inputSignal;
        Falcor::Texture::SharedPtr linearZ;
        Falcor::Texture::SharedPtr motionVec;
        Falcor::Texture::SharedPtr compactNormalDepth;
    } mGBufferInput;
};
