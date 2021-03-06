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

#include "../../Precompiled.h"

#include "../../Core/Context.h"
#include "../../Core/ProcessUtils.h"
#include "../../Core/Profiler.h"
#include "../../Graphics/ConstantBuffer.h"
#include "../../Graphics/Geometry.h"
#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsEvents.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/IndexBuffer.h"
#include "../../Graphics/Renderer.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/ShaderPrecache.h"
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/Texture2D.h"
#include "../../Graphics/TextureCube.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/File.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>

#include "../../DebugNew.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

// Prefer the high-performance GPU on switchable GPU systems
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

using Microsoft::WRL::ComPtr;

namespace Urho3D
{
#if defined(_DEBUG)
    // Check for SDK Layer support.
    inline bool SdkLayersAvailable() noexcept
    {
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_NULL,       // There is no need to create a real hardware device.
            nullptr,
            D3D11_CREATE_DEVICE_DEBUG,  // Check for the SDK layers.
            nullptr,                    // Any feature level will do.
            0,
            D3D11_SDK_VERSION,
            nullptr,                    // No need to keep the D3D device reference.
            nullptr,                    // No need to know the feature level.
            nullptr                     // No need to keep the D3D device context reference.
        );

        return SUCCEEDED(hr);
    }
#endif

    static const D3D11_COMPARISON_FUNC d3dCmpFunc[] =
    {
        D3D11_COMPARISON_ALWAYS,
        D3D11_COMPARISON_EQUAL,
        D3D11_COMPARISON_NOT_EQUAL,
        D3D11_COMPARISON_LESS,
        D3D11_COMPARISON_LESS_EQUAL,
        D3D11_COMPARISON_GREATER,
        D3D11_COMPARISON_GREATER_EQUAL
    };

    static const DWORD d3dBlendEnable[] =
    {
        FALSE,
        TRUE,
        TRUE,
        TRUE,
        TRUE,
        TRUE,
        TRUE,
        TRUE,
        TRUE
    };

    static const D3D11_BLEND d3dSrcBlend[] =
    {
        D3D11_BLEND_ONE,
        D3D11_BLEND_ONE,
        D3D11_BLEND_DEST_COLOR,
        D3D11_BLEND_SRC_ALPHA,
        D3D11_BLEND_SRC_ALPHA,
        D3D11_BLEND_ONE,
        D3D11_BLEND_INV_DEST_ALPHA,
        D3D11_BLEND_ONE,
        D3D11_BLEND_SRC_ALPHA,
    };

    static const D3D11_BLEND d3dDestBlend[] =
    {
        D3D11_BLEND_ZERO,
        D3D11_BLEND_ONE,
        D3D11_BLEND_ZERO,
        D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_ONE,
        D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_DEST_ALPHA,
        D3D11_BLEND_ONE,
        D3D11_BLEND_ONE
    };

    static const D3D11_BLEND_OP d3dBlendOp[] =
    {
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_REV_SUBTRACT,
        D3D11_BLEND_OP_REV_SUBTRACT
    };

    static const D3D11_STENCIL_OP d3dStencilOp[] =
    {
        D3D11_STENCIL_OP_KEEP,
        D3D11_STENCIL_OP_ZERO,
        D3D11_STENCIL_OP_REPLACE,
        D3D11_STENCIL_OP_INCR,
        D3D11_STENCIL_OP_DECR
    };

    static const D3D11_CULL_MODE d3dCullMode[] =
    {
        D3D11_CULL_NONE,
        D3D11_CULL_BACK,
        D3D11_CULL_FRONT
    };

    static const D3D11_FILL_MODE d3dFillMode[] =
    {
        D3D11_FILL_SOLID,
        D3D11_FILL_WIREFRAME,
        D3D11_FILL_WIREFRAME // Point fill mode not supported
    };

    static D3D_PRIMITIVE_TOPOLOGY GetD3DPrimitiveType(uint32_t elementCount, PrimitiveType type, uint32_t& primitiveCount)
    {
        switch (type)
        {
            case PrimitiveType::TriangleList:
                primitiveCount = elementCount / 3;
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

            case PrimitiveType::LineList:
                primitiveCount = elementCount / 2;
                return D3D_PRIMITIVE_TOPOLOGY_LINELIST;

            case PrimitiveType::PointList:
                primitiveCount = elementCount;
                return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

            case PrimitiveType::TriangleStrip:
                primitiveCount = elementCount - 2;
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

            case PrimitiveType::LineStrip:
                primitiveCount = elementCount - 1;
                return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;

            default:
                primitiveCount = 0;
                return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    static HWND GetWindowHandle(SDL_Window* window)
    {
        SDL_SysWMinfo sysInfo;

        SDL_VERSION(&sysInfo.version);
        SDL_GetWindowWMInfo(window, &sysInfo);
        return sysInfo.info.win.window;
    }

    Graphics::Graphics(Context* context)
        : Object(context)
        , impl_(new GraphicsImpl())
        , position_(SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED)
        , shaderPath_("Shaders/HLSL/")
        , shaderExtension_(".hlsl")
        , orientations_("LandscapeLeft LandscapeRight")
        , apiName_("D3D11")
    {
        SetTextureUnitMappings();
        ResetCachedState();

        context_->RequireSDL(SDL_INIT_VIDEO);

        // Register Graphics library object factories
        RegisterGraphicsLibrary(context_);
    }

    Graphics::~Graphics()
    {
        {
            std::lock_guard<std::mutex> lock(gpuObjectMutex_);

            // Release all GPU objects that still exist
            for (GPUObject* object : gpuObjects)
                object->Release();
            gpuObjects.clear();
        }

        impl_->vertexDeclarations_.clear();
        impl_->allConstantBuffers_.clear();

        impl_->blendStates.clear();
        impl_->depthStates.clear();
        impl_->rasterizerStates.clear();

        URHO3D_SAFE_RELEASE(impl_->defaultRenderTargetView_);
        URHO3D_SAFE_RELEASE(impl_->defaultDepthStencilView_);
        URHO3D_SAFE_RELEASE(impl_->defaultDepthTexture_);
        URHO3D_SAFE_RELEASE(impl_->resolveTexture_);
        URHO3D_SAFE_RELEASE(impl_->swapChain_);
        URHO3D_SAFE_RELEASE(impl_->deviceContext_);
        URHO3D_SAFE_RELEASE(impl_->device_);

        if (window_)
        {
            SDL_ShowCursor(SDL_TRUE);
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        delete impl_;
        impl_ = nullptr;

        context_->ReleaseSDL();
    }

    bool Graphics::SetScreenMode(int width, int height, const ScreenModeParams& params, bool maximize)
    {
        URHO3D_PROFILE(SetScreenMode);

        // Ensure that parameters are properly filled
        ScreenModeParams newParams = params;
        AdjustScreenMode(width, height, newParams, maximize);

        // Find out the full screen mode display format (match desktop color depth)
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(newParams.monitor_, &mode);
        const DXGI_FORMAT fullscreenFormat = SDL_BITSPERPIXEL(mode.format) == 16 ? DXGI_FORMAT_B5G6R5_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;

        // If nothing changes, do not reset the device
        if (width == width_ && height == height_ && newParams == screenParams_)
            return true;

        SDL_SetHint(SDL_HINT_ORIENTATIONS, orientations_.CString());

        if (!window_)
        {
            if (!OpenWindow(width, height, newParams.resizable_, newParams.borderless_))
                return false;
        }

        AdjustWindow(width, height, newParams.fullscreen_, newParams.borderless_, newParams.monitor_);

        if (maximize)
        {
            Maximize();
            SDL_GetWindowSize(window_, &width, &height);
        }

        const int oldMultiSample = screenParams_.multiSample_;
        screenParams_ = newParams;

        if (!impl_->device_ || screenParams_.multiSample_ != oldMultiSample)
            CreateDevice(width, height);
        UpdateSwapChain(width, height);

        // Clear the initial window contents to black
        Clear(ClearTargetFlags::Color);
        impl_->swapChain_->Present(0, 0);

        OnScreenModeChanged();
        return true;
    }

    void Graphics::SetSRGB(bool enable)
    {
        bool newEnable = enable && sRGBWriteSupport_;
        if (newEnable != sRGB_)
        {
            sRGB_ = newEnable;
            if (impl_->swapChain_)
            {
                // Recreate swap chain for the new backbuffer format
                CreateDevice(width_, height_);
                UpdateSwapChain(width_, height_);
            }
        }
    }

    void Graphics::SetFlushGPU(bool enable)
    {
        flushGPU_ = enable;

        if (impl_->device_)
        {
            IDXGIDevice1* dxgiDevice;
            impl_->device_->QueryInterface(IID_IDXGIDevice1, (void**)&dxgiDevice);
            if (dxgiDevice)
            {
                dxgiDevice->SetMaximumFrameLatency(enable ? 1 : 3);
                dxgiDevice->Release();
            }
        }
    }

    void Graphics::Close()
    {
        if (window_)
        {
            SDL_ShowCursor(SDL_TRUE);
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool Graphics::TakeScreenShot(Image& destImage)
    {
        URHO3D_PROFILE(TakeScreenShot);

        if (!impl_->device_)
            return false;

        D3D11_TEXTURE2D_DESC textureDesc;
        memset(&textureDesc, 0, sizeof textureDesc);
        textureDesc.Width = (UINT)width_;
        textureDesc.Height = (UINT)height_;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* stagingTexture = nullptr;
        HRESULT hr = impl_->device_->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(stagingTexture);
            URHO3D_LOGD3DERROR("Could not create staging texture for screenshot", hr);
            return false;
        }

        ID3D11Resource* source = nullptr;
        impl_->defaultRenderTargetView_->GetResource(&source);

        if (screenParams_.multiSample_ > 1)
        {
            // If backbuffer is multisampled, need another DEFAULT usage texture to resolve the data to first
            impl_->CreateResolveTexture(width_, height_);

            if (!impl_->resolveTexture_)
            {
                stagingTexture->Release();
                source->Release();
                return false;
            }

            impl_->deviceContext_->ResolveSubresource(impl_->resolveTexture_, 0, source, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            impl_->deviceContext_->CopyResource(stagingTexture, impl_->resolveTexture_);
        }
        else
            impl_->deviceContext_->CopyResource(stagingTexture, source);

        source->Release();

        D3D11_MAPPED_SUBRESOURCE mappedData;
        mappedData.pData = nullptr;
        hr = impl_->deviceContext_->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedData);
        if (FAILED(hr) || !mappedData.pData)
        {
            URHO3D_LOGD3DERROR("Could not map staging texture for screenshot", hr);
            stagingTexture->Release();
            return false;
        }

        destImage.SetSize(width_, height_, 3);
        unsigned char* destData = destImage.GetData();
        for (int y = 0; y < height_; ++y)
        {
            unsigned char* src = (unsigned char*)mappedData.pData + y * mappedData.RowPitch;
            for (int x = 0; x < width_; ++x)
            {
                *destData++ = *src++;
                *destData++ = *src++;
                *destData++ = *src++;
                ++src;
            }
        }

        impl_->deviceContext_->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return true;
    }

    bool Graphics::BeginFrame()
    {
        if (!IsInitialized())
            return false;

        // If using an external window, check it for size changes, and reset screen mode if necessary
        if (externalWindow_)
        {
            int width, height;

            SDL_GetWindowSize(window_, &width, &height);
            if (width != width_ || height != height_)
                SetMode(width, height);
        }
        else
        {
            // To prevent a loop of endless device loss and flicker, do not attempt to render when in fullscreen
            // and the window is minimized
            if (screenParams_.fullscreen_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED))
                return false;
        }

        // Set default rendertarget and depth buffer
        ResetRenderTargets();

        // Cleanup textures from previous frame
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
            SetTexture(i, nullptr);

        numPrimitives_ = 0;
        numBatches_ = 0;

        SendEvent(E_BEGINRENDERING);
        return true;
    }

    void Graphics::EndFrame()
    {
        if (!IsInitialized())
            return;

        {
            URHO3D_PROFILE(Present);

            SendEvent(E_ENDRENDERING);
            impl_->swapChain_->Present(screenParams_.vsync_ ? 1 : 0, 0);
        }

        // Clean up too large scratch buffers
        CleanupScratchBuffers();
    }

    void Graphics::Clear(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
    {
        IntVector2 rtSize = GetRenderTargetDimensions();

        bool oldColorWrite = colorWrite_;
        bool oldDepthWrite = depthWrite_;

        // D3D11 clear always clears the whole target regardless of viewport or scissor test settings
        // Emulate partial clear by rendering a quad
        if (!viewport_.left_ && !viewport_.top_ && viewport_.right_ == rtSize.x && viewport_.bottom_ == rtSize.y)
        {
            // Make sure we use the read-write version of the depth stencil
            SetDepthWrite(true);
            PrepareDraw();

            if (((flags & ClearTargetFlags::Color) != 0) && impl_->renderTargetViews_[0])
                impl_->deviceContext_->ClearRenderTargetView(impl_->renderTargetViews_[0], color.Data());

            if ((flags & (ClearTargetFlags::DepthStencil)) != 0 && impl_->depthStencilView_)
            {
                UINT depthClearFlags = 0;
                if ((flags & ClearTargetFlags::Depth) != 0)
                    depthClearFlags |= D3D11_CLEAR_DEPTH;
                if ((flags & ClearTargetFlags::Stencil) != 0)
                    depthClearFlags |= D3D11_CLEAR_STENCIL;
                impl_->deviceContext_->ClearDepthStencilView(impl_->depthStencilView_, depthClearFlags, depth, (UINT8)stencil);
            }
        }
        else
        {
            Renderer* renderer = GetSubsystem<Renderer>();
            if (!renderer)
                return;

            Geometry* geometry = renderer->GetQuadGeometry();

            Matrix3x4 model = Matrix3x4::IDENTITY;
            Matrix4 projection = Matrix4::IDENTITY;
            model.m23_ = Clamp(depth, 0.0f, 1.0f);

            SetBlendMode(BLEND_REPLACE);
            SetColorWrite((flags & ClearTargetFlags::Color) != 0);
            SetCullMode(CullMode::None);
            SetDepthTest(CMP_ALWAYS);
            SetDepthWrite((flags & ClearTargetFlags::Depth) != 0);
            SetFillMode(FillMode::Solid);
            SetScissorTest(false);
            SetStencilTest((flags & ClearTargetFlags::Stencil) != 0, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, stencil);
            SetShaders(GetShader(VS, "ClearFramebuffer"), GetShader(PS, "ClearFramebuffer"));
            SetShaderParameter(VSP_MODEL, model);
            SetShaderParameter(VSP_VIEWPROJ, projection);
            SetShaderParameter(PSP_MATDIFFCOLOR, color);

            geometry->Draw(this);

            SetStencilTest(false);
            ClearParameterSources();
        }

        // Restore color & depth write state now
        SetColorWrite(oldColorWrite);
        SetDepthWrite(oldDepthWrite);
    }

    bool Graphics::ResolveToTexture(Texture2D* destination, const IntRect& viewport)
    {
        if (!destination || !destination->GetRenderSurface())
            return false;

        URHO3D_PROFILE(ResolveToTexture);

        IntRect vpCopy = viewport;
        if (vpCopy.right_ <= vpCopy.left_)
            vpCopy.right_ = vpCopy.left_ + 1;
        if (vpCopy.bottom_ <= vpCopy.top_)
            vpCopy.bottom_ = vpCopy.top_ + 1;

        D3D11_BOX srcBox;
        srcBox.left = Clamp(vpCopy.left_, 0, width_);
        srcBox.top = Clamp(vpCopy.top_, 0, height_);
        srcBox.right = Clamp(vpCopy.right_, 0, width_);
        srcBox.bottom = Clamp(vpCopy.bottom_, 0, height_);
        srcBox.front = 0;
        srcBox.back = 1;

        ID3D11Resource* source = nullptr;
        const bool resolve = screenParams_.multiSample_ > 1;
        impl_->defaultRenderTargetView_->GetResource(&source);

        if (!resolve)
        {
            if (!srcBox.left && !srcBox.top && srcBox.right == width_ && srcBox.bottom == height_)
                impl_->deviceContext_->CopyResource((ID3D11Resource*)destination->GetGPUObject(), source);
            else
                impl_->deviceContext_->CopySubresourceRegion((ID3D11Resource*)destination->GetGPUObject(), 0, 0, 0, 0, source, 0, &srcBox);
        }
        else
        {
            if (!srcBox.left && !srcBox.top && srcBox.right == width_ && srcBox.bottom == height_)
            {
                impl_->deviceContext_->ResolveSubresource((ID3D11Resource*)destination->GetGPUObject(), 0, source, 0, (DXGI_FORMAT)
                    destination->GetFormat());
            }
            else
            {
                impl_->CreateResolveTexture(width_, height_);

                if (impl_->resolveTexture_)
                {
                    impl_->deviceContext_->ResolveSubresource(impl_->resolveTexture_, 0, source, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                    impl_->deviceContext_->CopySubresourceRegion((ID3D11Resource*)destination->GetGPUObject(), 0, 0, 0, 0, impl_->resolveTexture_, 0, &srcBox);
                }
            }
        }

        source->Release();

        return true;
    }

    bool Graphics::ResolveToTexture(Texture2D* texture)
    {
        if (!texture)
            return false;
        RenderSurface* surface = texture->GetRenderSurface();
        if (!surface)
            return false;

        texture->SetResolveDirty(false);
        surface->SetResolveDirty(false);
        ID3D11Resource* source = (ID3D11Resource*)texture->GetGPUObject();
        ID3D11Resource* dest = (ID3D11Resource*)texture->GetResolveTexture();
        if (!source || !dest)
            return false;

        impl_->deviceContext_->ResolveSubresource(dest, 0, source, 0, (DXGI_FORMAT)texture->GetFormat());
        return true;
    }

    bool Graphics::ResolveToTexture(TextureCube* texture)
    {
        if (!texture)
            return false;

        texture->SetResolveDirty(false);
        ID3D11Resource* source = (ID3D11Resource*)texture->GetGPUObject();
        ID3D11Resource* dest = (ID3D11Resource*)texture->GetResolveTexture();
        if (!source || !dest)
            return false;

        for (unsigned i = 0; i < MAX_CUBEMAP_FACES; ++i)
        {
            // Resolve only the surface(s) that were actually rendered to
            RenderSurface* surface = texture->GetRenderSurface((CubeMapFace)i);
            if (!surface->IsResolveDirty())
                continue;

            surface->SetResolveDirty(false);
            unsigned subResource = D3D11CalcSubresource(0, i, texture->GetLevels());
            impl_->deviceContext_->ResolveSubresource(dest, subResource, source, subResource, (DXGI_FORMAT)texture->GetFormat());
        }

        return true;
    }


    void Graphics::Draw(PrimitiveType type, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex)
    {
        if (!vertexCount || !impl_->shaderProgram_)
            return;

        PrepareDraw();

        unsigned primitiveCount;
        D3D_PRIMITIVE_TOPOLOGY d3dPrimitiveType = GetD3DPrimitiveType(vertexCount, type, primitiveCount);
        if (d3dPrimitiveType != primitiveType_)
        {
            impl_->deviceContext_->IASetPrimitiveTopology(d3dPrimitiveType);
            primitiveType_ = d3dPrimitiveType;
        }

        if (instanceCount > 1)
        {
            impl_->deviceContext_->DrawInstanced(vertexCount, instanceCount, firstVertex, 0);
        }
        else
        {
            impl_->deviceContext_->Draw(vertexCount, firstVertex);
        }

        numPrimitives_ += primitiveCount;
        ++numBatches_;
    }

    void Graphics::DrawIndexed(PrimitiveType type, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex)
    {
        if (!indexCount || !indexBuffer_ || !impl_->shaderProgram_)
            return;

        PrepareDraw();

        uint32_t primitiveCount;
        D3D_PRIMITIVE_TOPOLOGY d3dPrimitiveType = GetD3DPrimitiveType(indexCount, type, primitiveCount);
        if (d3dPrimitiveType != primitiveType_)
        {
            impl_->deviceContext_->IASetPrimitiveTopology(d3dPrimitiveType);
            primitiveType_ = d3dPrimitiveType;
        }

        if (instanceCount > 1)
        {
            impl_->deviceContext_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, 0);
        }
        else
        {
            impl_->deviceContext_->DrawIndexed(indexCount, firstIndex, baseVertex);
        }

        numPrimitives_ += primitiveCount;
        ++numBatches_;
    }

    void Graphics::SetVertexBuffer(VertexBuffer* buffer)
    {
        // Note: this is not multi-instance safe
        static PODVector<VertexBuffer*> vertexBuffers(1);
        vertexBuffers[0] = buffer;
        SetVertexBuffers(vertexBuffers);
    }

    bool Graphics::SetVertexBuffers(const PODVector<VertexBuffer*>& buffers, unsigned instanceOffset)
    {
        if (buffers.Size() > kMaxVertexBufferBindings)
        {
            URHO3D_LOGERROR("Too many vertex buffers");
            return false;
        }

        for (uint32_t i = 0; i < kMaxVertexBufferBindings; ++i)
        {
            VertexBuffer* buffer = nullptr;
            bool changed = false;

            buffer = i < buffers.Size() ? buffers[i] : nullptr;
            if (buffer)
            {
                const PODVector<VertexElement>& elements = buffer->GetElements();
                // Check if buffer has per-instance data
                bool hasInstanceData = elements.Size() && elements[0].perInstance_;
                unsigned offset = hasInstanceData ? instanceOffset * buffer->GetVertexSize() : 0;

                if (buffer != vertexBuffers_[i] || offset != impl_->vertexOffsets_[i])
                {
                    vertexBuffers_[i] = buffer;
                    impl_->vertexBuffers_[i] = (ID3D11Buffer*)buffer->GetGPUObject();
                    impl_->vertexSizes_[i] = buffer->GetVertexSize();
                    impl_->vertexOffsets_[i] = offset;
                    changed = true;
                }
            }
            else if (vertexBuffers_[i])
            {
                vertexBuffers_[i] = nullptr;
                impl_->vertexBuffers_[i] = nullptr;
                impl_->vertexSizes_[i] = 0;
                impl_->vertexOffsets_[i] = 0;
                changed = true;
            }

            if (changed)
            {
                impl_->vertexDeclarationDirty_ = true;

                if (impl_->firstDirtyVB_ == M_MAX_UNSIGNED)
                    impl_->firstDirtyVB_ = impl_->lastDirtyVB_ = i;
                else
                {
                    if (i < impl_->firstDirtyVB_)
                        impl_->firstDirtyVB_ = i;
                    if (i > impl_->lastDirtyVB_)
                        impl_->lastDirtyVB_ = i;
                }
            }
        }

        return true;
    }

    bool Graphics::SetVertexBuffers(const Vector<SharedPtr<VertexBuffer> >& buffers, unsigned instanceOffset)
    {
        return SetVertexBuffers(reinterpret_cast<const PODVector<VertexBuffer*>&>(buffers), instanceOffset);
    }

    void Graphics::SetIndexBuffer(IndexBuffer* buffer)
    {
        if (buffer != indexBuffer_)
        {
            if (buffer)
                impl_->deviceContext_->IASetIndexBuffer((ID3D11Buffer*)buffer->GetGPUObject(),
                    buffer->GetIndexSize() == sizeof(unsigned short) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
            else
                impl_->deviceContext_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            indexBuffer_ = buffer;
        }
    }

    void Graphics::SetShaders(ShaderVariation* vs, ShaderVariation* ps)
    {
        // Switch to the clip plane variations if necessary
        if (useClipPlane_)
        {
            if (vs)
                vs = vs->GetOwner()->GetVariation(VS, vs->GetDefinesClipPlane());
            if (ps)
                ps = ps->GetOwner()->GetVariation(PS, ps->GetDefinesClipPlane());
        }

        if (vs == vertexShader_ && ps == pixelShader_)
            return;

        if (vs != vertexShader_)
        {
            // Create the shader now if not yet created. If already attempted, do not retry
            if (vs && !vs->GetGPUObject())
            {
                if (vs->GetCompilerOutput().Empty())
                {
                    URHO3D_PROFILE(CompileVertexShader);

                    bool success = vs->Create();
                    if (!success)
                    {
                        URHO3D_LOGERROR("Failed to compile vertex shader " + vs->GetFullName() + ":\n" + vs->GetCompilerOutput());
                        vs = nullptr;
                    }
                }
                else
                    vs = nullptr;
            }

            impl_->deviceContext_->VSSetShader((ID3D11VertexShader*)(vs ? vs->GetGPUObject() : nullptr), nullptr, 0);
            vertexShader_ = vs;
            impl_->vertexDeclarationDirty_ = true;
        }

        if (ps != pixelShader_)
        {
            if (ps && !ps->GetGPUObject())
            {
                if (ps->GetCompilerOutput().Empty())
                {
                    URHO3D_PROFILE(CompilePixelShader);

                    bool success = ps->Create();
                    if (!success)
                    {
                        URHO3D_LOGERROR("Failed to compile pixel shader " + ps->GetFullName() + ":\n" + ps->GetCompilerOutput());
                        ps = nullptr;
                    }
                }
                else
                    ps = nullptr;
            }

            impl_->deviceContext_->PSSetShader((ID3D11PixelShader*)(ps ? ps->GetGPUObject() : nullptr), nullptr, 0);
            pixelShader_ = ps;
        }

        // Update current shader parameters & constant buffers
        if (vertexShader_ && pixelShader_)
        {
            std::pair<ShaderVariation*, ShaderVariation*> key = std::make_pair(vertexShader_, pixelShader_);
            ShaderProgramMap::iterator i = impl_->shaderPrograms_.find(key);
            if (i != impl_->shaderPrograms_.end())
            {
                impl_->shaderProgram_ = i->second.Get();
            }
            else
            {
                ShaderProgram* newProgram = impl_->shaderPrograms_[key] = new ShaderProgram(this, vertexShader_, pixelShader_);
                impl_->shaderProgram_ = newProgram;
            }

            bool vsBuffersChanged = false;
            bool psBuffersChanged = false;

            for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
            {
                ID3D11Buffer* vsBuffer = impl_->shaderProgram_->vsConstantBuffers_[i] ? (ID3D11Buffer*)impl_->shaderProgram_->vsConstantBuffers_[i]->
                    GetGPUObject() : nullptr;
                if (vsBuffer != impl_->constantBuffers_[VS][i])
                {
                    impl_->constantBuffers_[VS][i] = vsBuffer;
                    shaderParameterSources_[i] = (const void*)M_MAX_UNSIGNED;
                    vsBuffersChanged = true;
                }

                ID3D11Buffer* psBuffer = impl_->shaderProgram_->psConstantBuffers_[i] ? (ID3D11Buffer*)impl_->shaderProgram_->psConstantBuffers_[i]->
                    GetGPUObject() : nullptr;
                if (psBuffer != impl_->constantBuffers_[PS][i])
                {
                    impl_->constantBuffers_[PS][i] = psBuffer;
                    shaderParameterSources_[i] = (const void*)M_MAX_UNSIGNED;
                    psBuffersChanged = true;
                }
            }

            if (vsBuffersChanged)
                impl_->deviceContext_->VSSetConstantBuffers(0, MAX_SHADER_PARAMETER_GROUPS, &impl_->constantBuffers_[VS][0]);
            if (psBuffersChanged)
                impl_->deviceContext_->PSSetConstantBuffers(0, MAX_SHADER_PARAMETER_GROUPS, &impl_->constantBuffers_[PS][0]);
        }
        else
            impl_->shaderProgram_ = nullptr;

        // Store shader combination if shader dumping in progress
        if (shaderPrecache_)
            shaderPrecache_->StoreShaders(vertexShader_, pixelShader_);

        // Update clip plane parameter if necessary
        if (useClipPlane_)
            SetShaderParameter(VSP_CLIPPLANE, clipPlane_);
    }

    void Graphics::SetShaderParameter(StringHash param, const float* data, unsigned count)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, (unsigned)(count * sizeof(float)), data);
    }

    void Graphics::SetShaderParameter(StringHash param, float value)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(float), &value);
    }

    void Graphics::SetShaderParameter(StringHash param, int value)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(int), &value);
    }

    void Graphics::SetShaderParameter(StringHash param, bool value)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(bool), &value);
    }

    void Graphics::SetShaderParameter(StringHash param, const Color& color)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Color), &color);
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector2& vector)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Vector2), &vector);
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix3& matrix)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetVector3ArrayParameter(i->second_.offset_, 3, &matrix);
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector3& vector)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Vector3), &vector);
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix4& matrix)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Matrix4), &matrix);
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector4& vector)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Vector4), &vector);
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix3x4& matrix)
    {
        HashMap<StringHash, ShaderParameter>::Iterator i;
        if (!impl_->shaderProgram_ || (i = impl_->shaderProgram_->parameters_.Find(param)) == impl_->shaderProgram_->parameters_.End())
            return;

        ConstantBuffer* buffer = i->second_.bufferPtr_;
        if (!buffer->IsDirty())
            impl_->dirtyConstantBuffers_.Push(buffer);
        buffer->SetParameter(i->second_.offset_, sizeof(Matrix3x4), &matrix);
    }

    bool Graphics::NeedParameterUpdate(ShaderParameterGroup group, const void* source)
    {
        if ((unsigned)(size_t)shaderParameterSources_[group] == M_MAX_UNSIGNED || shaderParameterSources_[group] != source)
        {
            shaderParameterSources_[group] = source;
            return true;
        }

        return false;
    }

    bool Graphics::HasShaderParameter(StringHash param)
    {
        return impl_->shaderProgram_ && impl_->shaderProgram_->parameters_.Find(param) != impl_->shaderProgram_->parameters_.End();
    }

    bool Graphics::HasTextureUnit(TextureUnit unit)
    {
        return (vertexShader_ && vertexShader_->HasTextureUnit(unit)) || (pixelShader_ && pixelShader_->HasTextureUnit(unit));
    }

    void Graphics::ClearParameterSource(ShaderParameterGroup group)
    {
        shaderParameterSources_[group] = (const void*)M_MAX_UNSIGNED;
    }

    void Graphics::ClearParameterSources()
    {
        for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
            shaderParameterSources_[i] = (const void*)M_MAX_UNSIGNED;
    }

    void Graphics::ClearTransformSources()
    {
        shaderParameterSources_[SP_CAMERA] = (const void*)M_MAX_UNSIGNED;
        shaderParameterSources_[SP_OBJECT] = (const void*)M_MAX_UNSIGNED;
    }

    void Graphics::SetTexture(unsigned index, Texture* texture)
    {
        if (index >= MAX_TEXTURE_UNITS)
            return;

        // Check if texture is currently bound as a rendertarget. In that case, use its backup texture, or blank if not defined
        if (texture)
        {
            if (renderTargets_[0] && renderTargets_[0]->GetParentTexture() == texture)
                texture = texture->GetBackupTexture();
            else
            {
                // Resolve multisampled texture now as necessary
                if (texture->GetMultiSample() > 1 && texture->GetAutoResolve() && texture->IsResolveDirty())
                {
                    if (texture->GetType() == Texture2D::GetTypeStatic())
                        ResolveToTexture(static_cast<Texture2D*>(texture));
                    if (texture->GetType() == TextureCube::GetTypeStatic())
                        ResolveToTexture(static_cast<TextureCube*>(texture));
                }
            }

            if (texture && texture->GetLevelsDirty())
                texture->RegenerateLevels();
        }

        if (texture && texture->GetParametersDirty())
        {
            texture->UpdateParameters();
            textures_[index] = nullptr; // Force reassign
        }

        if (texture != textures_[index])
        {
            if (impl_->firstDirtyTexture_ == M_MAX_UNSIGNED)
                impl_->firstDirtyTexture_ = impl_->lastDirtyTexture_ = index;
            else
            {
                if (index < impl_->firstDirtyTexture_)
                    impl_->firstDirtyTexture_ = index;
                if (index > impl_->lastDirtyTexture_)
                    impl_->lastDirtyTexture_ = index;
            }

            textures_[index] = texture;
            impl_->shaderResourceViews_[index] = texture ? (ID3D11ShaderResourceView*)texture->GetShaderResourceView() : nullptr;
            impl_->samplers_[index] = texture ? (ID3D11SamplerState*)texture->GetSampler() : nullptr;
            impl_->texturesDirty_ = true;
        }
    }

    void SetTextureForUpdate(Texture* texture)
    {
        // No-op on Direct3D11
    }

    void Graphics::SetDefaultTextureFilterMode(TextureFilterMode mode)
    {
        if (mode != defaultTextureFilterMode_)
        {
            defaultTextureFilterMode_ = mode;
            SetTextureParametersDirty();
        }
    }

    void Graphics::SetDefaultTextureAnisotropy(unsigned level)
    {
        level = Max(level, 1U);

        if (level != defaultTextureAnisotropy_)
        {
            defaultTextureAnisotropy_ = level;
            SetTextureParametersDirty();
        }
    }

    void Graphics::Restore()
    {
        // No-op on Direct3D11
    }

    void Graphics::SetTextureParametersDirty()
    {
        std::lock_guard<std::mutex> lock(gpuObjectMutex_);

        for (GPUObject* object : gpuObjects)
        {
            Texture* texture = dynamic_cast<Texture*>(object);
            if (texture)
                texture->SetParametersDirty();
        }
    }

    void Graphics::ResetRenderTargets()
    {
        for (unsigned i = 0; i < kMaxColorAttachments; ++i)
            SetRenderTarget(i, (RenderSurface*)nullptr);
        SetDepthStencil((RenderSurface*)nullptr);
        SetViewport(IntRect(0, 0, width_, height_));
    }

    void Graphics::ResetRenderTarget(unsigned index)
    {
        SetRenderTarget(index, (RenderSurface*)nullptr);
    }

    void Graphics::ResetDepthStencil()
    {
        SetDepthStencil((RenderSurface*)nullptr);
    }

    void Graphics::SetRenderTarget(unsigned index, RenderSurface* renderTarget)
    {
        if (index >= kMaxColorAttachments)
            return;

        if (renderTarget != renderTargets_[index])
        {
            renderTargets_[index] = renderTarget;
            impl_->renderTargetsDirty_ = true;

            // If the rendertarget is also bound as a texture, replace with backup texture or null
            if (renderTarget)
            {
                Texture* parentTexture = renderTarget->GetParentTexture();

                for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
                {
                    if (textures_[i] == parentTexture)
                        SetTexture(i, textures_[i]->GetBackupTexture());
                }

                // If multisampled, mark the texture & surface needing resolve
                if (parentTexture->GetMultiSample() > 1 && parentTexture->GetAutoResolve())
                {
                    parentTexture->SetResolveDirty(true);
                    renderTarget->SetResolveDirty(true);
                }

                // If mipmapped, mark the levels needing regeneration
                if (parentTexture->GetLevels() > 1)
                    parentTexture->SetLevelsDirty();
            }
        }
    }

    void Graphics::SetRenderTarget(unsigned index, Texture2D* texture)
    {
        RenderSurface* renderTarget = nullptr;
        if (texture)
            renderTarget = texture->GetRenderSurface();

        SetRenderTarget(index, renderTarget);
    }

    void Graphics::SetDepthStencil(RenderSurface* depthStencil)
    {
        if (depthStencil != depthStencil_)
        {
            depthStencil_ = depthStencil;
            impl_->renderTargetsDirty_ = true;
        }
    }

    void Graphics::SetDepthStencil(Texture2D* texture)
    {
        RenderSurface* depthStencil = nullptr;
        if (texture)
            depthStencil = texture->GetRenderSurface();

        SetDepthStencil(depthStencil);
        // Constant depth bias depends on the bitdepth
        impl_->rasterizerStateDirty_ = true;
    }

    void Graphics::SetViewport(const IntRect& rect)
    {
        IntVector2 size = GetRenderTargetDimensions();

        IntRect rectCopy = rect;

        if (rectCopy.right_ <= rectCopy.left_)
            rectCopy.right_ = rectCopy.left_ + 1;
        if (rectCopy.bottom_ <= rectCopy.top_)
            rectCopy.bottom_ = rectCopy.top_ + 1;
        rectCopy.left_ = Clamp(rectCopy.left_, 0, size.x);
        rectCopy.top_ = Clamp(rectCopy.top_, 0, size.y);
        rectCopy.right_ = Clamp(rectCopy.right_, 0, size.x);
        rectCopy.bottom_ = Clamp(rectCopy.bottom_, 0, size.y);

        static D3D11_VIEWPORT d3dViewport;
        d3dViewport.TopLeftX = (float)rectCopy.left_;
        d3dViewport.TopLeftY = (float)rectCopy.top_;
        d3dViewport.Width = (float)(rectCopy.right_ - rectCopy.left_);
        d3dViewport.Height = (float)(rectCopy.bottom_ - rectCopy.top_);
        d3dViewport.MinDepth = 0.0f;
        d3dViewport.MaxDepth = 1.0f;

        impl_->deviceContext_->RSSetViewports(1, &d3dViewport);

        viewport_ = rectCopy;

        // Disable scissor test, needs to be re-enabled by the user
        SetScissorTest(false);
    }

    void Graphics::SetBlendMode(BlendMode mode, bool alphaToCoverage)
    {
        if (mode != blendMode_ || alphaToCoverage != alphaToCoverage_)
        {
            blendMode_ = mode;
            alphaToCoverage_ = alphaToCoverage;
            impl_->blendStateDirty_ = true;
        }
    }

    void Graphics::SetColorWrite(bool enable)
    {
        if (enable != colorWrite_)
        {
            colorWrite_ = enable;
            impl_->blendStateDirty_ = true;
        }
    }

    void Graphics::SetCullMode(CullMode mode)
    {
        if (mode != cullMode_)
        {
            cullMode_ = mode;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetDepthBias(float constantBias, float slopeScaledBias)
    {
        if (constantBias != constantDepthBias_ || slopeScaledBias != slopeScaledDepthBias_)
        {
            constantDepthBias_ = constantBias;
            slopeScaledDepthBias_ = slopeScaledBias;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetDepthTest(CompareMode mode)
    {
        if (mode != depthTestMode_)
        {
            depthTestMode_ = mode;
            impl_->depthStateDirty_ = true;
        }
    }

    void Graphics::SetDepthWrite(bool enable)
    {
        if (enable != depthWrite_)
        {
            depthWrite_ = enable;
            impl_->depthStateDirty_ = true;
            // Also affects whether a read-only version of depth-stencil should be bound, to allow sampling
            impl_->renderTargetsDirty_ = true;
        }
    }

    void Graphics::SetFillMode(FillMode mode)
    {
        if (mode != fillMode_)
        {
            fillMode_ = mode;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetLineAntiAlias(bool enable)
    {
        if (enable != lineAntiAlias_)
        {
            lineAntiAlias_ = enable;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetScissorTest(bool enable, const Rect& rect, bool borderInclusive)
    {
        // During some light rendering loops, a full rect is toggled on/off repeatedly.
        // Disable scissor in that case to reduce state changes
        if (rect.min_.x_ <= 0.0f && rect.min_.y_ <= 0.0f && rect.max_.x_ >= 1.0f && rect.max_.y_ >= 1.0f)
            enable = false;

        if (enable)
        {
            IntVector2 rtSize(GetRenderTargetDimensions());
            IntVector2 viewSize(viewport_.Size());
            IntVector2 viewPos(viewport_.left_, viewport_.top_);
            IntRect intRect;
            int expand = borderInclusive ? 1 : 0;

            intRect.left_ = Clamp((int)((rect.min_.x_ + 1.0f) * 0.5f * viewSize.x) + viewPos.x, 0, rtSize.x - 1);
            intRect.top_ = Clamp((int)((-rect.max_.y_ + 1.0f) * 0.5f * viewSize.y) + viewPos.y, 0, rtSize.y - 1);
            intRect.right_ = Clamp((int)((rect.max_.x_ + 1.0f) * 0.5f * viewSize.x) + viewPos.x + expand, 0, rtSize.x);
            intRect.bottom_ = Clamp((int)((-rect.min_.y_ + 1.0f) * 0.5f * viewSize.y) + viewPos.y + expand, 0, rtSize.y);

            if (intRect.right_ == intRect.left_)
                intRect.right_++;
            if (intRect.bottom_ == intRect.top_)
                intRect.bottom_++;

            if (intRect.right_ < intRect.left_ || intRect.bottom_ < intRect.top_)
                enable = false;

            if (enable && intRect != scissorRect_)
            {
                scissorRect_ = intRect;
                impl_->scissorRectDirty_ = true;
            }
        }

        if (enable != scissorTest_)
        {
            scissorTest_ = enable;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetScissorTest(bool enable, const IntRect& rect)
    {
        IntVector2 rtSize(GetRenderTargetDimensions());
        IntVector2 viewPos(viewport_.left_, viewport_.top_);

        if (enable)
        {
            IntRect intRect;
            intRect.left_ = Clamp(rect.left_ + viewPos.x, 0, rtSize.x - 1);
            intRect.top_ = Clamp(rect.top_ + viewPos.y, 0, rtSize.y - 1);
            intRect.right_ = Clamp(rect.right_ + viewPos.x, 0, rtSize.x);
            intRect.bottom_ = Clamp(rect.bottom_ + viewPos.y, 0, rtSize.y);

            if (intRect.right_ == intRect.left_)
                intRect.right_++;
            if (intRect.bottom_ == intRect.top_)
                intRect.bottom_++;

            if (intRect.right_ < intRect.left_ || intRect.bottom_ < intRect.top_)
                enable = false;

            if (enable && intRect != scissorRect_)
            {
                scissorRect_ = intRect;
                impl_->scissorRectDirty_ = true;
            }
        }

        if (enable != scissorTest_)
        {
            scissorTest_ = enable;
            impl_->rasterizerStateDirty_ = true;
        }
    }

    void Graphics::SetStencilTest(bool enable, CompareMode mode, StencilOp pass, StencilOp fail, StencilOp zFail, unsigned stencilRef,
        unsigned compareMask, unsigned writeMask)
    {
        if (enable != stencilTest_)
        {
            stencilTest_ = enable;
            impl_->depthStateDirty_ = true;
        }

        if (enable)
        {
            if (mode != stencilTestMode_)
            {
                stencilTestMode_ = mode;
                impl_->depthStateDirty_ = true;
            }
            if (pass != stencilPass_)
            {
                stencilPass_ = pass;
                impl_->depthStateDirty_ = true;
            }
            if (fail != stencilFail_)
            {
                stencilFail_ = fail;
                impl_->depthStateDirty_ = true;
            }
            if (zFail != stencilZFail_)
            {
                stencilZFail_ = zFail;
                impl_->depthStateDirty_ = true;
            }
            if (compareMask != stencilCompareMask_)
            {
                stencilCompareMask_ = compareMask;
                impl_->depthStateDirty_ = true;
            }
            if (writeMask != stencilWriteMask_)
            {
                stencilWriteMask_ = writeMask;
                impl_->depthStateDirty_ = true;
            }
            if (stencilRef != stencilRef_)
            {
                stencilRef_ = stencilRef;
                impl_->stencilRefDirty_ = true;
                impl_->depthStateDirty_ = true;
            }
        }
    }

    void Graphics::SetClipPlane(bool enable, const Plane& clipPlane, const Matrix3x4& view, const Matrix4& projection)
    {
        useClipPlane_ = enable;

        if (enable)
        {
            Matrix4 viewProj = projection * view;
            clipPlane_ = clipPlane.Transformed(viewProj).ToVector4();
            SetShaderParameter(VSP_CLIPPLANE, clipPlane_);
        }
    }

    bool Graphics::IsInitialized() const
    {
        return window_ != nullptr && impl_->GetDevice() != nullptr;
    }

    PODVector<int> Graphics::GetMultiSampleLevels() const
    {
        PODVector<int> ret;
        ret.Push(1);

        if (impl_->device_)
        {
            for (unsigned i = 2; i <= 16; ++i)
            {
                if (impl_->CheckMultiSampleSupport(sRGB_ ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, i))
                    ret.Push(i);
            }
        }

        return ret;
    }

    unsigned Graphics::GetFormat(CompressedFormat format) const
    {
        switch (format)
        {
            case CF_RGBA:
                return DXGI_FORMAT_R8G8B8A8_UNORM;

            case CF_DXT1:
                return DXGI_FORMAT_BC1_UNORM;

            case CF_DXT3:
                return DXGI_FORMAT_BC2_UNORM;

            case CF_DXT5:
                return DXGI_FORMAT_BC3_UNORM;

            default:
                return 0;
        }
    }

    ShaderVariation* Graphics::GetShader(ShaderType type, const String& name, const String& defines) const
    {
        return GetShader(type, name.CString(), defines.CString());
    }

    ShaderVariation* Graphics::GetShader(ShaderType type, const char* name, const char* defines) const
    {
        if (lastShaderName_ != name || !lastShader_)
        {
            ResourceCache* cache = GetSubsystem<ResourceCache>();

            String fullShaderName = shaderPath_ + name + shaderExtension_;
            // Try to reduce repeated error log prints because of missing shaders
            if (lastShaderName_ == name && !cache->Exists(fullShaderName))
                return nullptr;

            lastShader_ = cache->GetResource<Shader>(fullShaderName);
            lastShaderName_ = name;
        }

        return lastShader_ ? lastShader_->GetVariation(type, defines) : nullptr;
    }

    VertexBuffer* Graphics::GetVertexBuffer(unsigned index) const
    {
        return index < kMaxVertexBufferBindings ? vertexBuffers_[index] : nullptr;
    }

    ShaderProgram* Graphics::GetShaderProgram() const
    {
        return impl_->shaderProgram_;
    }

    TextureUnit Graphics::GetTextureUnit(const String& name)
    {
        auto it = textureUnits_.find(name);
        if (it != textureUnits_.end())
            return it->second;

        return MAX_TEXTURE_UNITS;
    }

    const String& Graphics::GetTextureUnitName(TextureUnit unit)
    {
        for (auto i = textureUnits_.begin(); i != textureUnits_.end(); ++i)
        {
            if (i->second == unit)
                return i->first;
        }

        return String::EMPTY;
    }

    Texture* Graphics::GetTexture(unsigned index) const
    {
        return index < MAX_TEXTURE_UNITS ? textures_[index] : nullptr;
    }

    RenderSurface* Graphics::GetRenderTarget(unsigned index) const
    {
        return index < kMaxColorAttachments ? renderTargets_[index] : nullptr;
    }

    IntVector2 Graphics::GetRenderTargetDimensions() const
    {
        int width, height;

        if (renderTargets_[0])
        {
            width = renderTargets_[0]->GetWidth();
            height = renderTargets_[0]->GetHeight();
        }
        else if (depthStencil_) // Depth-only rendering
        {
            width = depthStencil_->GetWidth();
            height = depthStencil_->GetHeight();
        }
        else
        {
            width = width_;
            height = height_;
        }

        return IntVector2(width, height);
    }

    bool Graphics::IsDeviceLost() const
    {
        // Direct3D11 graphics context is never considered lost
        /// \todo The device could be lost in case of graphics adapters getting disabled during runtime. This is not currently handled
        return false;
    }

    void Graphics::OnWindowResized()
    {
        if (!impl_->device_ || !window_)
            return;

        int newWidth, newHeight;

        SDL_GetWindowSize(window_, &newWidth, &newHeight);
        if (newWidth == width_ && newHeight == height_)
            return;

        UpdateSwapChain(newWidth, newHeight);

        // Reset rendertargets and viewport for the new screen size
        ResetRenderTargets();

        URHO3D_LOGDEBUGF("Window was resized to %dx%d", width_, height_);

        using namespace ScreenMode;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_WIDTH] = width_;
        eventData[P_HEIGHT] = height_;
        eventData[P_FULLSCREEN] = screenParams_.fullscreen_;
        eventData[P_RESIZABLE] = screenParams_.resizable_;
        eventData[P_BORDERLESS] = screenParams_.borderless_;
        eventData[P_HIGHDPI] = screenParams_.highDPI_;
        SendEvent(E_SCREENMODE, eventData);
    }

    void Graphics::OnWindowMoved()
    {
        if (!impl_->device_ || !window_ || screenParams_.fullscreen_)
            return;

        int newX, newY;

        SDL_GetWindowPosition(window_, &newX, &newY);
        if (newX == position_.x && newY == position_.y)
            return;

        position_.x = newX;
        position_.y = newY;

        URHO3D_LOGTRACEF("Window was moved to %d,%d", position_.x, position_.y);

        using namespace WindowPos;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_X] = position_.x;
        eventData[P_Y] = position_.y;
        SendEvent(E_WINDOWPOS, eventData);
    }

    void Graphics::CleanupShaderPrograms(ShaderVariation* variation)
    {
        for (ShaderProgramMap::iterator i = impl_->shaderPrograms_.begin(); i != impl_->shaderPrograms_.end();)
        {
            if (i->first.first == variation || i->first.second == variation)
                i = impl_->shaderPrograms_.erase(i);
            else
                ++i;
        }

        if (vertexShader_ == variation || pixelShader_ == variation)
            impl_->shaderProgram_ = nullptr;
    }

    void Graphics::CleanupRenderSurface(RenderSurface* surface)
    {
        // No-op on Direct3D11
    }

    ConstantBuffer* Graphics::GetOrCreateConstantBuffer(ShaderType type, unsigned index, unsigned size)
    {
        // Ensure that different shader types and index slots get unique buffers, even if the size is same
        size_t key = type;
        HashCombine(key, index);
        HashCombine(key, size);
        ConstantBufferMap::iterator i = impl_->allConstantBuffers_.find(key);
        if (i != impl_->allConstantBuffers_.end())
        {
            return i->second.Get();
        }
        else
        {
            SharedPtr<ConstantBuffer> newConstantBuffer(new ConstantBuffer(context_));
            newConstantBuffer->SetSize(size);
            impl_->allConstantBuffers_[key] = newConstantBuffer;
            return newConstantBuffer.Get();
        }
    }

    unsigned Graphics::GetAlphaFormat()
    {
        return DXGI_FORMAT_A8_UNORM;
    }

    unsigned Graphics::GetLuminanceFormat()
    {
        // Note: not same sampling behavior as on D3D9; need to sample the R channel only
        return DXGI_FORMAT_R8_UNORM;
    }

    unsigned Graphics::GetLuminanceAlphaFormat()
    {
        // Note: not same sampling behavior as on D3D9; need to sample the RG channels
        return DXGI_FORMAT_R8G8_UNORM;
    }

    unsigned Graphics::GetRGBFormat()
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    unsigned Graphics::GetRGBAFormat()
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    unsigned Graphics::GetRGBA16Format()
    {
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    }

    unsigned Graphics::GetRGBAFloat16Format()
    {
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    unsigned Graphics::GetRGBAFloat32Format()
    {
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }

    unsigned Graphics::GetRG16Format()
    {
        return DXGI_FORMAT_R16G16_UNORM;
    }

    unsigned Graphics::GetRGFloat16Format()
    {
        return DXGI_FORMAT_R16G16_FLOAT;
    }

    unsigned Graphics::GetRGFloat32Format()
    {
        return DXGI_FORMAT_R32G32_FLOAT;
    }

    unsigned Graphics::GetFloat16Format()
    {
        return DXGI_FORMAT_R16_FLOAT;
    }

    unsigned Graphics::GetFloat32Format()
    {
        return DXGI_FORMAT_R32_FLOAT;
    }

    unsigned Graphics::GetLinearDepthFormat()
    {
        return DXGI_FORMAT_R32_FLOAT;
    }

    unsigned Graphics::GetDepthStencilFormat()
    {
        return DXGI_FORMAT_R24G8_TYPELESS;
    }

    unsigned Graphics::GetReadableDepthFormat()
    {
        return DXGI_FORMAT_R24G8_TYPELESS;
    }

    unsigned Graphics::GetFormat(const String& formatName)
    {
        String nameLower = formatName.ToLower().Trimmed();

        if (nameLower == "a")
            return GetAlphaFormat();
        if (nameLower == "l")
            return GetLuminanceFormat();
        if (nameLower == "la")
            return GetLuminanceAlphaFormat();
        if (nameLower == "rgb")
            return GetRGBFormat();
        if (nameLower == "rgba")
            return GetRGBAFormat();
        if (nameLower == "rgba16")
            return GetRGBA16Format();
        if (nameLower == "rgba16f")
            return GetRGBAFloat16Format();
        if (nameLower == "rgba32f")
            return GetRGBAFloat32Format();
        if (nameLower == "rg16")
            return GetRG16Format();
        if (nameLower == "rg16f")
            return GetRGFloat16Format();
        if (nameLower == "rg32f")
            return GetRGFloat32Format();
        if (nameLower == "r16f")
            return GetFloat16Format();
        if (nameLower == "r32f" || nameLower == "float")
            return GetFloat32Format();
        if (nameLower == "lineardepth" || nameLower == "depth")
            return GetLinearDepthFormat();
        if (nameLower == "d24s8")
            return GetDepthStencilFormat();
        if (nameLower == "readabledepth" || nameLower == "hwdepth")
            return GetReadableDepthFormat();

        return GetRGBFormat();
    }

    uint32_t Graphics::GetMaxBones()
    {
        return 128;
    }

    bool Graphics::OpenWindow(int width, int height, bool resizable, bool borderless)
    {
        if (!externalWindow_)
        {
            unsigned flags = 0;
            if (resizable)
                flags |= SDL_WINDOW_RESIZABLE;
            if (borderless)
                flags |= SDL_WINDOW_BORDERLESS;

            window_ = SDL_CreateWindow(windowTitle_.CString(), position_.x, position_.y, width, height, flags);
        }
        else
            window_ = SDL_CreateWindowFrom(externalWindow_, 0);

        if (!window_)
        {
            URHO3D_LOGERRORF("Could not create window, root cause: '%s'", SDL_GetError());
            return false;
        }

        SDL_GetWindowPosition(window_, &position_.x, &position_.y);

        CreateWindowIcon();

        return true;
    }

    void Graphics::AdjustWindow(int& newWidth, int& newHeight, bool& newFullscreen, bool& newBorderless, int& monitor)
    {
        if (!externalWindow_)
        {
            // Keep current window position because it may change in intermediate callbacks
            const IntVector2 oldPosition = position_;
            bool reposition = false;
            bool resizePostponed = false;
            if (!newWidth || !newHeight)
            {
                SDL_MaximizeWindow(window_);
                SDL_GetWindowSize(window_, &newWidth, &newHeight);
            }
            else
            {
                SDL_Rect display_rect;
                SDL_GetDisplayBounds(monitor, &display_rect);

                reposition = newFullscreen || (newBorderless && newWidth >= display_rect.w && newHeight >= display_rect.h);
                if (reposition)
                {
                    // Reposition the window on the specified monitor if it's supposed to cover the entire monitor
                    SDL_SetWindowPosition(window_, display_rect.x, display_rect.y);
                }

                // Postpone window resize if exiting fullscreen to avoid redundant resolution change
                if (!newFullscreen && screenParams_.fullscreen_)
                    resizePostponed = true;
                else
                    SDL_SetWindowSize(window_, newWidth, newHeight);
            }

            // Turn off window fullscreen mode so it gets repositioned to the correct monitor
            SDL_SetWindowFullscreen(window_, SDL_FALSE);
            // Hack fix: on SDL 2.0.4 a fullscreen->windowed transition results in a maximized window when the D3D device is reset, so hide before
            if (!newFullscreen) SDL_HideWindow(window_);
            SDL_SetWindowFullscreen(window_, newFullscreen ? SDL_WINDOW_FULLSCREEN : 0);
            SDL_SetWindowBordered(window_, newBorderless ? SDL_FALSE : SDL_TRUE);
            if (!newFullscreen) SDL_ShowWindow(window_);

            // Resize now if was postponed
            if (resizePostponed)
                SDL_SetWindowSize(window_, newWidth, newHeight);

            // Ensure that window keeps its position
            if (!reposition)
                SDL_SetWindowPosition(window_, oldPosition.x, oldPosition.y);
            else
                position_ = oldPosition;
        }
        else
        {
            // If external window, must ask its dimensions instead of trying to set them
            SDL_GetWindowSize(window_, &newWidth, &newHeight);
            newFullscreen = false;
        }
    }

    bool Graphics::CreateDevice(int width, int height)
    {
        // Device needs only to be created once
        if (!impl_->device_)
        {
            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
            if (SdkLayersAvailable())
            {
                // If the project is in a debug build, enable debugging via SDK Layers with this flag.
                creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
            }
            else
            {
                OutputDebugStringA("WARNING: Direct3D Debug Device is not available\n");
            }
#endif

            static const D3D_FEATURE_LEVEL s_featureLevels[] =
            {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };

            HRESULT hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                creationFlags,
                s_featureLevels,
                _countof(s_featureLevels),
                D3D11_SDK_VERSION,
                &impl_->device_,
                &impl_->featureLevel,
                &impl_->deviceContext_
            );

            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(impl_->device_);
                URHO3D_SAFE_RELEASE(impl_->deviceContext_);
                URHO3D_LOGD3DERROR("Failed to create D3D11 device", hr);
                return false;
            }

            CheckFeatureSupport();
            // Set the flush mode now as the device has been created
            SetFlushGPU(flushGPU_);
        }

        // Check that multisample level is supported
        PODVector<int> multiSampleLevels = GetMultiSampleLevels();
        if (!multiSampleLevels.Contains(screenParams_.multiSample_))
            screenParams_.multiSample_ = 1;

        // Create swap chain. Release old if necessary
        if (impl_->swapChain_)
        {
            impl_->swapChain_->Release();
            impl_->swapChain_ = nullptr;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        impl_->device_->QueryInterface(IID_IDXGIDevice, (void**)&dxgiDevice);
        IDXGIAdapter* dxgiAdapter = nullptr;
        dxgiDevice->GetParent(IID_IDXGIAdapter, (void**)&dxgiAdapter);
        IDXGIFactory* dxgiFactory = nullptr;
        dxgiAdapter->GetParent(IID_IDXGIFactory, (void**)&dxgiFactory);

        DXGI_RATIONAL refreshRateRational = {};
        IDXGIOutput* dxgiOutput = nullptr;
        UINT numModes = 0;
        dxgiAdapter->EnumOutputs(screenParams_.monitor_, &dxgiOutput);
        dxgiOutput->GetDisplayModeList(sRGB_ ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, 0);

        // find the best matching refresh rate with the specified resolution
        if (numModes > 0)
        {
            DXGI_MODE_DESC* modes = new DXGI_MODE_DESC[numModes];
            dxgiOutput->GetDisplayModeList(sRGB_ ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, modes);
            unsigned bestMatchingRateIndex = -1;
            unsigned bestError = M_MAX_UNSIGNED;
            for (unsigned i = 0; i < numModes; ++i)
            {
                if (width != modes[i].Width || height != modes[i].Height)
                    continue;

                float rate = (float)modes[i].RefreshRate.Numerator / modes[i].RefreshRate.Denominator;
                unsigned error = (unsigned)(Abs(rate - screenParams_.refreshRate_));
                if (error < bestError)
                {
                    bestMatchingRateIndex = i;
                    bestError = error;
                }
            }
            if (bestMatchingRateIndex != -1)
            {
                refreshRateRational.Numerator = modes[bestMatchingRateIndex].RefreshRate.Numerator;
                refreshRateRational.Denominator = modes[bestMatchingRateIndex].RefreshRate.Denominator;
            }
            delete[] modes;
        }

        dxgiOutput->Release();

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Width = (UINT)width;
        swapChainDesc.BufferDesc.Height = (UINT)height;
        swapChainDesc.BufferDesc.Format = sRGB_ ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = refreshRateRational.Numerator;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = refreshRateRational.Denominator;
        swapChainDesc.OutputWindow = GetWindowHandle(window_);
        swapChainDesc.SampleDesc.Count = static_cast<UINT>(screenParams_.multiSample_);
        swapChainDesc.SampleDesc.Quality = impl_->GetMultiSampleQuality(swapChainDesc.BufferDesc.Format, screenParams_.multiSample_);
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        HRESULT hr = dxgiFactory->CreateSwapChain(impl_->device_, &swapChainDesc, &impl_->swapChain_);
        // After creating the swap chain, disable automatic Alt-Enter fullscreen/windowed switching
        // (the application will switch manually if it wants to)
        ThrowIfFailed(dxgiFactory->MakeWindowAssociation(GetWindowHandle(window_), DXGI_MWA_NO_ALT_ENTER));

#ifdef URHO3D_LOGGING
        DXGI_ADAPTER_DESC desc;
        dxgiAdapter->GetDesc(&desc);
        String adapterDesc(desc.Description);
        URHO3D_LOGINFO("Adapter used " + adapterDesc);
#endif

        dxgiFactory->Release();
        dxgiAdapter->Release();
        dxgiDevice->Release();

        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(impl_->swapChain_);
            URHO3D_LOGD3DERROR("Failed to create D3D11 swap chain", hr);
            return false;
        }

        return true;
    }

    bool Graphics::UpdateSwapChain(int width, int height)
    {
        bool success = true;

        ID3D11RenderTargetView* nullView = nullptr;
        impl_->deviceContext_->OMSetRenderTargets(1, &nullView, nullptr);
        if (impl_->defaultRenderTargetView_)
        {
            impl_->defaultRenderTargetView_->Release();
            impl_->defaultRenderTargetView_ = nullptr;
        }
        if (impl_->defaultDepthStencilView_)
        {
            impl_->defaultDepthStencilView_->Release();
            impl_->defaultDepthStencilView_ = nullptr;
        }
        if (impl_->defaultDepthTexture_)
        {
            impl_->defaultDepthTexture_->Release();
            impl_->defaultDepthTexture_ = nullptr;
        }
        if (impl_->resolveTexture_)
        {
            impl_->resolveTexture_->Release();
            impl_->resolveTexture_ = nullptr;
        }

        impl_->depthStencilView_ = nullptr;
        for (unsigned i = 0; i < kMaxColorAttachments; ++i)
            impl_->renderTargetViews_[i] = nullptr;
        impl_->renderTargetsDirty_ = true;

        impl_->swapChain_->ResizeBuffers(1, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

        // Create default rendertarget view representing the backbuffer
        ID3D11Texture2D* backbufferTexture;
        HRESULT hr = impl_->swapChain_->GetBuffer(0, IID_ID3D11Texture2D, (void**)&backbufferTexture);
        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(backbufferTexture);
            URHO3D_LOGD3DERROR("Failed to get backbuffer texture", hr);
            success = false;
        }
        else
        {
            hr = impl_->device_->CreateRenderTargetView(backbufferTexture, nullptr, &impl_->defaultRenderTargetView_);
            backbufferTexture->Release();
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(impl_->defaultRenderTargetView_);
                URHO3D_LOGD3DERROR("Failed to create backbuffer rendertarget view", hr);
                success = false;
            }
        }

        // Create default depth-stencil texture and view
        D3D11_TEXTURE2D_DESC depthDesc;
        memset(&depthDesc, 0, sizeof depthDesc);
        depthDesc.Width = (UINT)width;
        depthDesc.Height = (UINT)height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = static_cast<UINT>(screenParams_.multiSample_);
        depthDesc.SampleDesc.Quality = impl_->GetMultiSampleQuality(depthDesc.Format, screenParams_.multiSample_);
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.CPUAccessFlags = 0;
        depthDesc.MiscFlags = 0;
        hr = impl_->device_->CreateTexture2D(&depthDesc, nullptr, &impl_->defaultDepthTexture_);
        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(impl_->defaultDepthTexture_);
            URHO3D_LOGD3DERROR("Failed to create backbuffer depth-stencil texture", hr);
            success = false;
        }
        else
        {
            hr = impl_->device_->CreateDepthStencilView(impl_->defaultDepthTexture_, nullptr, &impl_->defaultDepthStencilView_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(impl_->defaultDepthStencilView_);
                URHO3D_LOGD3DERROR("Failed to create backbuffer depth-stencil view", hr);
                success = false;
            }
        }

        // Update internally held backbuffer size
        width_ = width;
        height_ = height;

        ResetRenderTargets();
        return success;
    }

    void Graphics::CheckFeatureSupport()
    {
        anisotropySupport_ = true;
        dxtTextureSupport_ = true;
        lightPrepassSupport_ = true;
        deferredSupport_ = true;
        hardwareShadowSupport_ = true;
        shadowMapFormat_ = DXGI_FORMAT_R16_TYPELESS;
        hiresShadowMapFormat_ = DXGI_FORMAT_R32_TYPELESS;
        dummyColorFormat_ = DXGI_FORMAT_UNKNOWN;
        sRGBSupport_ = true;
        sRGBWriteSupport_ = true;
    }

    void Graphics::ResetCachedState()
    {
        for (uint32_t i = 0; i < kMaxVertexBufferBindings; ++i)
        {
            vertexBuffers_[i] = nullptr;
            impl_->vertexBuffers_[i] = nullptr;
            impl_->vertexSizes_[i] = 0;
            impl_->vertexOffsets_[i] = 0;
        }

        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            textures_[i] = nullptr;
            impl_->shaderResourceViews_[i] = nullptr;
            impl_->samplers_[i] = nullptr;
        }

        for (unsigned i = 0; i < kMaxColorAttachments; ++i)
        {
            renderTargets_[i] = nullptr;
            impl_->renderTargetViews_[i] = nullptr;
        }

        for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        {
            impl_->constantBuffers_[VS][i] = nullptr;
            impl_->constantBuffers_[PS][i] = nullptr;
        }

        depthStencil_ = nullptr;
        impl_->depthStencilView_ = nullptr;
        viewport_ = IntRect(0, 0, width_, height_);

        indexBuffer_ = nullptr;
        vertexDeclarationHash_ = 0;
        primitiveType_ = 0;
        vertexShader_ = nullptr;
        pixelShader_ = nullptr;
        blendMode_ = BLEND_REPLACE;
        alphaToCoverage_ = false;
        colorWrite_ = true;
        cullMode_ = CullMode::CounterClockwise;
        constantDepthBias_ = 0.0f;
        slopeScaledDepthBias_ = 0.0f;
        depthTestMode_ = CMP_LESSEQUAL;
        depthWrite_ = true;
        fillMode_ = FillMode::Solid;
        lineAntiAlias_ = false;
        scissorTest_ = false;
        scissorRect_ = IntRect::ZERO;
        stencilTest_ = false;
        stencilTestMode_ = CMP_ALWAYS;
        stencilPass_ = OP_KEEP;
        stencilFail_ = OP_KEEP;
        stencilZFail_ = OP_KEEP;
        stencilRef_ = 0;
        stencilCompareMask_ = M_MAX_UNSIGNED;
        stencilWriteMask_ = M_MAX_UNSIGNED;
        useClipPlane_ = false;
        impl_->shaderProgram_ = nullptr;
        impl_->renderTargetsDirty_ = true;
        impl_->texturesDirty_ = true;
        impl_->vertexDeclarationDirty_ = true;
        impl_->blendStateDirty_ = true;
        impl_->depthStateDirty_ = true;
        impl_->rasterizerStateDirty_ = true;
        impl_->scissorRectDirty_ = true;
        impl_->stencilRefDirty_ = true;
        impl_->blendStateHash = M_MAX_UNSIGNED;
        impl_->depthStateHash = M_MAX_UNSIGNED;
        impl_->rasterizerStateHash = M_MAX_UNSIGNED;
        impl_->firstDirtyTexture_ = impl_->lastDirtyTexture_ = M_MAX_UNSIGNED;
        impl_->firstDirtyVB_ = impl_->lastDirtyVB_ = M_MAX_UNSIGNED;
        impl_->dirtyConstantBuffers_.Clear();
    }

    void Graphics::PrepareDraw()
    {
        if (impl_->renderTargetsDirty_)
        {
            impl_->depthStencilView_ =
                (depthStencil_ && depthStencil_->GetUsage() == TEXTURE_DEPTHSTENCIL) ?
                (ID3D11DepthStencilView*)depthStencil_->GetRenderTargetView() : impl_->defaultDepthStencilView_;

            // If possible, bind a read-only depth stencil view to allow reading depth in shader
            if (!depthWrite_ && depthStencil_ && depthStencil_->GetReadOnlyView())
                impl_->depthStencilView_ = (ID3D11DepthStencilView*)depthStencil_->GetReadOnlyView();

            for (uint32_t i = 0; i < kMaxColorAttachments; ++i)
                impl_->renderTargetViews_[i] =
                (renderTargets_[i] && renderTargets_[i]->GetUsage() == TEXTURE_RENDERTARGET) ?
                (ID3D11RenderTargetView*)renderTargets_[i]->GetRenderTargetView() : nullptr;
            // If rendertarget 0 is null and not doing depth-only rendering, render to the backbuffer
            // Special case: if rendertarget 0 is null and depth stencil has same size as backbuffer, assume the intention is to do
            // backbuffer rendering with a custom depth stencil
            if (!renderTargets_[0] &&
                (!depthStencil_ || (depthStencil_ && depthStencil_->GetWidth() == width_ && depthStencil_->GetHeight() == height_)))
                impl_->renderTargetViews_[0] = impl_->defaultRenderTargetView_;

            impl_->deviceContext_->OMSetRenderTargets(kMaxColorAttachments, &impl_->renderTargetViews_[0], impl_->depthStencilView_);
            impl_->renderTargetsDirty_ = false;
        }

        if (impl_->texturesDirty_ && impl_->firstDirtyTexture_ < M_MAX_UNSIGNED)
        {
            // Set also VS textures to enable vertex texture fetch to work the same way as on OpenGL
            impl_->deviceContext_->VSSetShaderResources(impl_->firstDirtyTexture_, impl_->lastDirtyTexture_ - impl_->firstDirtyTexture_ + 1,
                &impl_->shaderResourceViews_[impl_->firstDirtyTexture_]);
            impl_->deviceContext_->VSSetSamplers(impl_->firstDirtyTexture_, impl_->lastDirtyTexture_ - impl_->firstDirtyTexture_ + 1,
                &impl_->samplers_[impl_->firstDirtyTexture_]);
            impl_->deviceContext_->PSSetShaderResources(impl_->firstDirtyTexture_, impl_->lastDirtyTexture_ - impl_->firstDirtyTexture_ + 1,
                &impl_->shaderResourceViews_[impl_->firstDirtyTexture_]);
            impl_->deviceContext_->PSSetSamplers(impl_->firstDirtyTexture_, impl_->lastDirtyTexture_ - impl_->firstDirtyTexture_ + 1,
                &impl_->samplers_[impl_->firstDirtyTexture_]);

            impl_->firstDirtyTexture_ = impl_->lastDirtyTexture_ = M_MAX_UNSIGNED;
            impl_->texturesDirty_ = false;
        }

        if (impl_->vertexDeclarationDirty_ && vertexShader_ && vertexShader_->GetByteCode().Size())
        {
            if (impl_->firstDirtyVB_ < M_MAX_UNSIGNED)
            {
                impl_->deviceContext_->IASetVertexBuffers(impl_->firstDirtyVB_, impl_->lastDirtyVB_ - impl_->firstDirtyVB_ + 1,
                    &impl_->vertexBuffers_[impl_->firstDirtyVB_], &impl_->vertexSizes_[impl_->firstDirtyVB_], &impl_->vertexOffsets_[impl_->firstDirtyVB_]);

                impl_->firstDirtyVB_ = impl_->lastDirtyVB_ = M_MAX_UNSIGNED;
            }

            size_t newVertexDeclarationHash = 0;
            for (uint32_t i = 0; i < kMaxVertexBufferBindings; ++i)
            {
                if (vertexBuffers_[i])
                {
                    HashCombine(newVertexDeclarationHash, vertexBuffers_[i]->GetBufferHash(i));
                }
            }

            // Do not create input layout if no vertex buffers / elements
            if (newVertexDeclarationHash)
            {
                /// \todo Using a 64bit total hash for vertex shader and vertex buffer elements hash may not guarantee uniqueness
                newVertexDeclarationHash += vertexShader_->GetElementHash();
                if (newVertexDeclarationHash != vertexDeclarationHash_)
                {
                    VertexDeclarationMap::iterator i = impl_->vertexDeclarations_.find(newVertexDeclarationHash);
                    if (i == impl_->vertexDeclarations_.end())
                    {
                        SharedPtr<VertexDeclaration> newVertexDeclaration(new VertexDeclaration(this, vertexShader_, vertexBuffers_));
                        i = impl_->vertexDeclarations_.insert(std::make_pair(newVertexDeclarationHash, newVertexDeclaration)).first;
                    }
                    impl_->deviceContext_->IASetInputLayout(i->second->GetHandle());
                    vertexDeclarationHash_ = newVertexDeclarationHash;
                }
            }

            impl_->vertexDeclarationDirty_ = false;
        }

        if (impl_->blendStateDirty_)
        {
            size_t newBlendStateHash = 0;
            HashCombine(newBlendStateHash, colorWrite_);
            HashCombine(newBlendStateHash, alphaToCoverage_);
            HashCombine(newBlendStateHash, static_cast<uint32_t>(blendMode_));
            if (newBlendStateHash != impl_->blendStateHash)
            {
                auto it = impl_->blendStates.find(newBlendStateHash);
                if (it == impl_->blendStates.end())
                {
                    URHO3D_PROFILE(CreateBlendState);

                    D3D11_BLEND_DESC stateDesc = {};
                    stateDesc.AlphaToCoverageEnable = alphaToCoverage_ ? TRUE : FALSE;
                    stateDesc.IndependentBlendEnable = false;
                    stateDesc.RenderTarget[0].BlendEnable = d3dBlendEnable[blendMode_];
                    stateDesc.RenderTarget[0].SrcBlend = d3dSrcBlend[blendMode_];
                    stateDesc.RenderTarget[0].DestBlend = d3dDestBlend[blendMode_];
                    stateDesc.RenderTarget[0].BlendOp = d3dBlendOp[blendMode_];
                    stateDesc.RenderTarget[0].SrcBlendAlpha = d3dSrcBlend[blendMode_];
                    stateDesc.RenderTarget[0].DestBlendAlpha = d3dDestBlend[blendMode_];
                    stateDesc.RenderTarget[0].BlendOpAlpha = d3dBlendOp[blendMode_];
                    stateDesc.RenderTarget[0].RenderTargetWriteMask = colorWrite_ ? D3D11_COLOR_WRITE_ENABLE_ALL : 0x0;

                    ComPtr<ID3D11BlendState> newBlendState = nullptr;
                    HRESULT hr = impl_->device_->CreateBlendState(&stateDesc, newBlendState.GetAddressOf());
                    if (FAILED(hr))
                    {
                        URHO3D_LOGD3DERROR("Failed to create blend state", hr);
                    }

                    impl_->blendStates[newBlendStateHash] = newBlendState;
                    impl_->deviceContext_->OMSetBlendState(newBlendState.Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
                }
                else
                {
                    impl_->deviceContext_->OMSetBlendState(it->second.Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
                }

                impl_->blendStateHash = newBlendStateHash;
            }

            impl_->blendStateDirty_ = false;
        }

        if (impl_->depthStateDirty_)
        {
            size_t newDepthStateHash = 0;
            HashCombine(newDepthStateHash, depthWrite_);
            HashCombine(newDepthStateHash, stencilTest_);
            HashCombine(newDepthStateHash, depthTestMode_);
            HashCombine(newDepthStateHash, stencilCompareMask_ & 0xff);
            HashCombine(newDepthStateHash, stencilWriteMask_ & 0xff);
            HashCombine(newDepthStateHash, stencilTestMode_);
            HashCombine(newDepthStateHash, stencilFail_);
            HashCombine(newDepthStateHash, stencilZFail_);
            HashCombine(newDepthStateHash, stencilPass_);

            if (newDepthStateHash != impl_->depthStateHash || impl_->stencilRefDirty_)
            {
                auto it = impl_->depthStates.find(newDepthStateHash);
                if (it == impl_->depthStates.end())
                {
                    URHO3D_PROFILE(CreateDepthState);

                    D3D11_DEPTH_STENCIL_DESC stateDesc = {};
                    stateDesc.DepthEnable = TRUE;
                    stateDesc.DepthWriteMask = depthWrite_ ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                    stateDesc.DepthFunc = d3dCmpFunc[depthTestMode_];
                    stateDesc.StencilEnable = stencilTest_ ? TRUE : FALSE;
                    stateDesc.StencilReadMask = (unsigned char)stencilCompareMask_;
                    stateDesc.StencilWriteMask = (unsigned char)stencilWriteMask_;
                    stateDesc.FrontFace.StencilFailOp = d3dStencilOp[stencilFail_];
                    stateDesc.FrontFace.StencilDepthFailOp = d3dStencilOp[stencilZFail_];
                    stateDesc.FrontFace.StencilPassOp = d3dStencilOp[stencilPass_];
                    stateDesc.FrontFace.StencilFunc = d3dCmpFunc[stencilTestMode_];
                    stateDesc.BackFace.StencilFailOp = d3dStencilOp[stencilFail_];
                    stateDesc.BackFace.StencilDepthFailOp = d3dStencilOp[stencilZFail_];
                    stateDesc.BackFace.StencilPassOp = d3dStencilOp[stencilPass_];
                    stateDesc.BackFace.StencilFunc = d3dCmpFunc[stencilTestMode_];

                    ComPtr<ID3D11DepthStencilState> newDepthState;
                    HRESULT hr = impl_->device_->CreateDepthStencilState(&stateDesc, newDepthState.GetAddressOf());
                    if (FAILED(hr))
                    {
                        URHO3D_LOGD3DERROR("Failed to create depth state", hr);
                    }

                    it = impl_->depthStates.insert(std::make_pair(newDepthStateHash, newDepthState)).first;
                }

                impl_->deviceContext_->OMSetDepthStencilState(it->second.Get(), stencilRef_);
                impl_->depthStateHash = newDepthStateHash;
            }

            impl_->depthStateDirty_ = false;
            impl_->stencilRefDirty_ = false;
        }

        if (impl_->rasterizerStateDirty_)
        {
            uint32_t depthBits = 24;
            if (depthStencil_ && depthStencil_->GetParentTexture()->GetFormat() == DXGI_FORMAT_R16_TYPELESS)
                depthBits = 16;
            int scaledDepthBias = (int)(constantDepthBias_ * (1 << depthBits));

            size_t newRasterizerStateHash = 0;
            HashCombine(newRasterizerStateHash, scissorTest_);
            HashCombine(newRasterizerStateHash, lineAntiAlias_);
            HashCombine(newRasterizerStateHash, (uint32_t)fillMode_);
            HashCombine(newRasterizerStateHash, (uint32_t)cullMode_);
            HashCombine(newRasterizerStateHash, scaledDepthBias & 0x1fff);
            HashCombine(newRasterizerStateHash, ((int)(slopeScaledDepthBias_ * 100.0f) & 0x1fff));

            if (newRasterizerStateHash != impl_->rasterizerStateHash)
            {
                auto it = impl_->rasterizerStates.find(newRasterizerStateHash);
                if (it == impl_->rasterizerStates.end())
                {
                    URHO3D_PROFILE(CreateRasterizerState);

                    D3D11_RASTERIZER_DESC stateDesc = {};
                    stateDesc.FillMode = d3dFillMode[(uint32_t)fillMode_];
                    stateDesc.CullMode = d3dCullMode[(uint32_t)cullMode_];
                    stateDesc.FrontCounterClockwise = FALSE;
                    stateDesc.DepthBias = scaledDepthBias;
                    stateDesc.DepthBiasClamp = M_INFINITY;
                    stateDesc.SlopeScaledDepthBias = slopeScaledDepthBias_;
                    stateDesc.DepthClipEnable = TRUE;
                    stateDesc.ScissorEnable = scissorTest_ ? TRUE : FALSE;
                    stateDesc.MultisampleEnable = lineAntiAlias_ ? FALSE : TRUE;
                    stateDesc.AntialiasedLineEnable = lineAntiAlias_ ? TRUE : FALSE;

                    ComPtr<ID3D11RasterizerState> newRasterizerState;
                    HRESULT hr = impl_->device_->CreateRasterizerState(&stateDesc, newRasterizerState.GetAddressOf());
                    if (FAILED(hr))
                    {
                        URHO3D_LOGD3DERROR("Failed to create rasterizer state", hr);
                    }

                    it = impl_->rasterizerStates.insert(std::make_pair(newRasterizerStateHash, newRasterizerState)).first;
                }

                impl_->deviceContext_->RSSetState(it->second.Get());
                impl_->rasterizerStateHash = newRasterizerStateHash;
            }

            impl_->rasterizerStateDirty_ = false;
        }

        if (impl_->scissorRectDirty_)
        {
            D3D11_RECT d3dRect;
            d3dRect.left = scissorRect_.left_;
            d3dRect.top = scissorRect_.top_;
            d3dRect.right = scissorRect_.right_;
            d3dRect.bottom = scissorRect_.bottom_;
            impl_->deviceContext_->RSSetScissorRects(1, &d3dRect);
            impl_->scissorRectDirty_ = false;
        }

        for (unsigned i = 0; i < impl_->dirtyConstantBuffers_.Size(); ++i)
            impl_->dirtyConstantBuffers_[i]->Apply();
        impl_->dirtyConstantBuffers_.Clear();
    }

    void GraphicsImpl::CreateResolveTexture(uint32_t width, uint32_t height)
    {
        if (resolveTexture_)
            return;

        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.CPUAccessFlags = 0;

        HRESULT hr = device_->CreateTexture2D(&textureDesc, nullptr, &resolveTexture_);
        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(resolveTexture_);
            URHO3D_LOGD3DERROR("Could not create resolve texture", hr);
        }
    }

    void Graphics::SetTextureUnitMappings()
    {
        textureUnits_["DiffMap"] = TU_DIFFUSE;
        textureUnits_["DiffCubeMap"] = TU_DIFFUSE;
        textureUnits_["NormalMap"] = TU_NORMAL;
        textureUnits_["SpecMap"] = TU_SPECULAR;
        textureUnits_["EmissiveMap"] = TU_EMISSIVE;
        textureUnits_["EnvMap"] = TU_ENVIRONMENT;
        textureUnits_["EnvCubeMap"] = TU_ENVIRONMENT;
        textureUnits_["LightRampMap"] = TU_LIGHTRAMP;
        textureUnits_["LightSpotMap"] = TU_LIGHTSHAPE;
        textureUnits_["LightCubeMap"] = TU_LIGHTSHAPE;
        textureUnits_["ShadowMap"] = TU_SHADOWMAP;
        textureUnits_["FaceSelectCubeMap"] = TU_FACESELECT;
        textureUnits_["IndirectionCubeMap"] = TU_INDIRECTION;
        textureUnits_["VolumeMap"] = TU_VOLUMEMAP;
        textureUnits_["ZoneCubeMap"] = TU_ZONE;
        textureUnits_["ZoneVolumeMap"] = TU_ZONE;
    }

}
