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
#include "Framework.h"
#include "FullScreenPass.h"
#include "API/VAO.h"
#include "API/Buffer.h"
#include "API/DepthStencilState.h"
#include "API/RenderContext.h"
#include "API/VertexLayout.h"
#include "API/Window.h"

namespace Falcor
{
    bool checkForViewportArray2Support()
    {
#ifdef FALCOR_D3D12
        return false;
#elif defined FALCOR_VK
        return false;
#else
#error Unknown API
#endif
    }
    struct Vertex
    {
        glm::vec2 screenPos;
        glm::vec2 texCoord;
    };

#ifdef FALCOR_VK
#define ADJUST_Y(a) (-(a))
#else
#define ADJUST_Y(a) a
#endif

    static const Vertex kVertices[] =
    {
        {glm::vec2(-1, ADJUST_Y( 1)), glm::vec2(0, 0)},
        {glm::vec2(-1, ADJUST_Y(-1)), glm::vec2(0, 1)},
        {glm::vec2( 1, ADJUST_Y( 1)), glm::vec2(1, 0)},
        {glm::vec2( 1, ADJUST_Y(-1)), glm::vec2(1, 1)},
    };
#undef ADJUST_Y

    static void initStaticObjects(Buffer::SharedPtr& pVB, Vao::SharedPtr& pVao)
    {
        // First time we got here. create VB and VAO
        const uint32_t vbSize = (uint32_t)(sizeof(Vertex)*arraysize(kVertices));
        pVB = Buffer::create(vbSize, Buffer::BindFlags::Vertex, Buffer::CpuAccess::Write, (void*)kVertices);

        // create VAO
        VertexLayout::SharedPtr pLayout = VertexLayout::create();
        VertexBufferLayout::SharedPtr pBufLayout = VertexBufferLayout::create();
        pBufLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
        pBufLayout->addElement("TEXCOORD", 8, ResourceFormat::RG32Float, 1, 1);
        pLayout->addBufferLayout(0, pBufLayout);

        Vao::BufferVec buffers{ pVB };
        pVao = Vao::create(Vao::Topology::TriangleStrip, pLayout, buffers);
    }

    FullScreenPass::UniquePtr FullScreenPass::create(const std::string& psFile, const Program::DefineList& programDefines, bool disableDepth, bool disableStencil, uint32_t viewportMask, bool enableSPS, Shader::CompilerFlags compilerFlags, const std::string& shaderModel)
    {
        return create("", psFile, programDefines, disableDepth, disableStencil, viewportMask, enableSPS, compilerFlags, shaderModel);
    }

    FullScreenPass::UniquePtr FullScreenPass::create(const std::string& vsFile, const std::string& psFile, const Program::DefineList& programDefines, bool disableDepth, bool disableStencil, uint32_t viewportMask, bool enableSPS, Shader::CompilerFlags compilerFlags, const std::string& shaderModel)
    {
        UniquePtr pPass = UniquePtr(new FullScreenPass());
        pPass->init(vsFile, psFile, programDefines, disableDepth, disableStencil, viewportMask, enableSPS, compilerFlags, shaderModel);
        return pPass;
    }

    void FullScreenPass::init(const std::string& vsFile, const std::string& psFile, const Program::DefineList& programDefines, bool disableDepth, bool disableStencil, uint32_t viewportMask, bool enableSPS, Shader::CompilerFlags compilerFlags, const std::string& shaderModel)
    {
        mpPipelineState = GraphicsState::create();
        mpPipelineState->toggleSinglePassStereo(enableSPS);

        // create depth stencil state
        DepthStencilState::Desc dsDesc;
        dsDesc.setDepthTest(!disableDepth);
        dsDesc.setDepthWriteMask(!disableDepth);
        dsDesc.setDepthFunc(DepthStencilState::Func::LessEqual);    // Equal is needed to allow overdraw when z is enabled (e.g., background pass etc.)
        dsDesc.setStencilTest(!disableStencil);
        dsDesc.setStencilWriteMask(!disableStencil);
        mpDepthStencilState = DepthStencilState::create(dsDesc);

        Program::DefineList defs = programDefines;
        std::string gs;

        if(viewportMask)
        {
            defs.add("_VIEWPORT_MASK", std::to_string(viewportMask));
            if(checkForViewportArray2Support())
            {
                defs.add("_USE_VP2_EXT");
            }
            else
            {
                defs.add("_OUTPUT_VERTEX_COUNT", std::to_string(3 * popcount(viewportMask)));
                gs = "Framework/Shaders/FullScreenPass.gs.slang";
            }
        }

        GraphicsProgram::Desc d;
        d.setCompilerFlags(compilerFlags);
        d.addShaderLibrary(vsFile.empty() ? "Framework/Shaders/FullScreenPass.vs.slang" : vsFile).vsEntry("main").addShaderLibrary(psFile).psEntry("main");
        if (gs.size()) d.addShaderLibrary(gs).gsEntry("main");
        if (!shaderModel.empty()) d.setShaderModel(shaderModel);
        mpProgram = GraphicsProgram::create(d, defs);
        mpPipelineState->setProgram(mpProgram);

        if (gFullScreenData.pVertexBuffer == nullptr)
        {
            initStaticObjects(gFullScreenData.pVertexBuffer, gFullScreenData.pVao);
        }
        mpPipelineState->setVao(gFullScreenData.pVao);
    }

    void FullScreenPass::execute(RenderContext* pRenderContext, DepthStencilState::SharedPtr pDsState, BlendState::SharedPtr pBlendState) const
    {
        mpPipelineState->pushFbo(pRenderContext->getGraphicsState()->getFbo(), false);
        mpPipelineState->setViewport(0, pRenderContext->getGraphicsState()->getViewport(0), false);
        mpPipelineState->setScissors(0, pRenderContext->getGraphicsState()->getScissors(0));

        mpPipelineState->setVao(gFullScreenData.pVao);
        mpPipelineState->setDepthStencilState(pDsState ? pDsState : mpDepthStencilState);
        mpPipelineState->setBlendState(pBlendState);
        pRenderContext->pushGraphicsState(mpPipelineState);
        pRenderContext->draw(arraysize(kVertices), 0);
        pRenderContext->popGraphicsState();
        mpPipelineState->popFbo(false);
    }
}