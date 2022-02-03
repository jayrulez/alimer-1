//
// Copyright (c) 2008-2022 the Urho3D project.
// Copyright (c) 2022 Amer Koleci and Contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../../Graphics/ConstantBuffer.h"
#include "../../Graphics/GraphicsDefs.h"
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/VertexDeclaration.h"
#include "../../Math/Color.h"
#include "PlatformInclude.h"

#include <wrl/client.h>

#ifndef D3D11_NO_HELPERS
#define D3D11_NO_HELPERS
#endif
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <unordered_map>

namespace Urho3D
{
#define URHO3D_SAFE_RELEASE(p) if (p) { ((IUnknown*)p)->Release();  p = 0; }

#define URHO3D_LOGD3DERROR(msg, hr) URHO3D_LOGERRORF("%s (HRESULT %x)", msg, (uint32_t)hr)

    constexpr inline uint32_t D3D11CalcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t MipLevels)
    {
        return MipSlice + ArraySlice * MipLevels;
    }

    using ShaderProgramMap = std::unordered_map<std::pair<ShaderVariation*, ShaderVariation*>, SharedPtr<ShaderProgram> >;
    using VertexDeclarationMap = std::unordered_map<size_t, SharedPtr<VertexDeclaration> >;
    using ConstantBufferMap = std::unordered_map<size_t, SharedPtr<ConstantBuffer> >;

    /// %Graphics implementation. Holds API-specific objects.
    class URHO3D_API GraphicsImpl
    {
        friend class Graphics;

    public:
        /// Construct.
        GraphicsImpl();

        /// Return Direct3D device.
        ID3D11Device* GetDevice() const { return device_; }

        /// Return Direct3D immediate device context.
        ID3D11DeviceContext* GetDeviceContext() const { return deviceContext_; }

        /// Return swapchain.
        IDXGISwapChain* GetSwapChain() const { return swapChain_; }

        /// Return whether multisampling is supported for a given texture format and sample count.
        bool CheckMultiSampleSupport(DXGI_FORMAT format, unsigned sampleCount) const;

        /// Return multisample quality level for a given texture format and sample count. The sample count must be supported. On D3D feature level 10.1+, uses the standard level. Below that uses the best quality.
        unsigned GetMultiSampleQuality(DXGI_FORMAT format, unsigned sampleCount) const;

        /// Create intermediate texture for multisampled backbuffer resolve. No-op if already exists.
        void CreateResolveTexture(uint32_t width, uint32_t height);

    private:
        /// Graphics device.
        ID3D11Device* device_;
        /// Immediate device context.
        ID3D11DeviceContext* deviceContext_;
        /// Supported feature level.
        D3D_FEATURE_LEVEL featureLevel{ D3D_FEATURE_LEVEL_9_1 };
        /// Swap chain.
        IDXGISwapChain* swapChain_;
        /// Default (backbuffer) rendertarget view.
        ID3D11RenderTargetView* defaultRenderTargetView_;
        /// Default depth-stencil texture.
        ID3D11Texture2D* defaultDepthTexture_;
        /// Default depth-stencil view.
        ID3D11DepthStencilView* defaultDepthStencilView_;
        /// Current color rendertarget views.
        ID3D11RenderTargetView* renderTargetViews_[kMaxColorAttachments];
        /// Current depth-stencil view.
        ID3D11DepthStencilView* depthStencilView_;
        /// Created blend state objects.
        std::unordered_map<size_t, Microsoft::WRL::ComPtr<ID3D11BlendState>> blendStates;
        /// Created depth state objects.
        std::unordered_map<size_t, Microsoft::WRL::ComPtr<ID3D11DepthStencilState>> depthStates;
        /// Created rasterizer state objects.
        std::unordered_map<size_t, Microsoft::WRL::ComPtr<ID3D11RasterizerState>> rasterizerStates;
        /// Intermediate texture for multisampled screenshots and less than whole viewport multisampled resolve, created on demand.
        ID3D11Texture2D* resolveTexture_;
        /// Bound shader resource views.
        ID3D11ShaderResourceView* shaderResourceViews_[MAX_TEXTURE_UNITS];
        /// Bound sampler state objects.
        ID3D11SamplerState* samplers_[MAX_TEXTURE_UNITS];
        /// Bound vertex buffers.
        ID3D11Buffer* vertexBuffers_[kMaxVertexBufferBindings];
        /// Bound constant buffers.
        ID3D11Buffer* constantBuffers_[2][MAX_SHADER_PARAMETER_GROUPS];
        /// Vertex sizes per buffer.
        uint32_t vertexSizes_[kMaxVertexBufferBindings];
        /// Vertex stream offsets per buffer.
        uint32_t vertexOffsets_[kMaxVertexBufferBindings];
        /// Rendertargets dirty flag.
        bool renderTargetsDirty_;
        /// Textures dirty flag.
        bool texturesDirty_;
        /// Vertex declaration dirty flag.
        bool vertexDeclarationDirty_;
        /// Blend state dirty flag.
        bool blendStateDirty_;
        /// Depth state dirty flag.
        bool depthStateDirty_;
        /// Rasterizer state dirty flag.
        bool rasterizerStateDirty_;
        /// Scissor rect dirty flag.
        bool scissorRectDirty_;
        /// Stencil ref dirty flag.
        bool stencilRefDirty_;
        /// Hash of current blend state.
        size_t blendStateHash;
        /// Hash of current depth state.
        size_t depthStateHash;
        /// Hash of current rasterizer state.
        size_t rasterizerStateHash;
        /// First dirtied texture unit.
        uint32_t firstDirtyTexture_;
        /// Last dirtied texture unit.
        uint32_t lastDirtyTexture_;
        /// First dirtied vertex buffer.
        uint32_t firstDirtyVB_;
        /// Last dirtied vertex buffer.
        uint32_t lastDirtyVB_;
        /// Vertex declarations.
        VertexDeclarationMap vertexDeclarations_;
        /// Constant buffer search map.
        ConstantBufferMap allConstantBuffers_;
        /// Currently dirty constant buffers.
        PODVector<ConstantBuffer*> dirtyConstantBuffers_;
        /// Shader programs.
        ShaderProgramMap shaderPrograms_;
        /// Shader program in use.
        ShaderProgram* shaderProgram_;
    };
}
