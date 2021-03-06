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
#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsEvents.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/IndexBuffer.h"
#include "../../Graphics/RenderSurface.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/ShaderPrecache.h"
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/ShaderVariation.h"
#include "../../Graphics/Texture2D.h"
#include "../../Graphics/TextureCube.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/File.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include <SDL/SDL.h>

#include "../../DebugNew.h"

#if ALIMER_OPENGLES
#define glClearDepth glClearDepthf
#endif

#ifdef __EMSCRIPTEN__
#include "../../Input/Input.h"
#include "../../UI/Cursor.h"
#include "../../UI/UI.h"
#include <emscripten/emscripten.h>
#include <emscripten/bind.h>

// Helper functions to support emscripten canvas resolution change
static const Urho3D::Context* appContext;

static void JSCanvasSize(int width, int height, bool fullscreen, float scale)
{
    URHO3D_LOGINFOF("JSCanvasSize: width=%d height=%d fullscreen=%d ui scale=%f", width, height, fullscreen, scale);

    using namespace Urho3D;

    if (appContext)
    {
        bool uiCursorVisible = false;
        bool systemCursorVisible = false;
        MouseMode mouseMode{};

        // Detect current system pointer state
        Input* input = appContext->GetSubsystem<Input>();
        if (input)
        {
            systemCursorVisible = input->IsMouseVisible();
            mouseMode = input->GetMouseMode();
        }

        UI* ui = appContext->GetSubsystem<UI>();
        if (ui)
        {
            ui->SetScale(scale);

            // Detect current UI pointer state
            Cursor* cursor = ui->GetCursor();
            if (cursor)
                uiCursorVisible = cursor->IsVisible();
        }

        // Apply new resolution
        appContext->GetSubsystem<Graphics>()->SetMode(width, height);

        // Reset the pointer state as it was before resolution change
        if (input)
        {
            if (uiCursorVisible)
                input->SetMouseVisible(false);
            else
                input->SetMouseVisible(systemCursorVisible);

            input->SetMouseMode(mouseMode);
        }

        if (ui)
        {
            Cursor* cursor = ui->GetCursor();
            if (cursor)
            {
                cursor->SetVisible(uiCursorVisible);

                IntVector2 pos = input->GetMousePosition();
                pos = ui->ConvertSystemToUI(pos);

                cursor->SetPosition(pos);
            }
        }
    }
}

using namespace emscripten;
EMSCRIPTEN_BINDINGS(Module) {
    function("JSCanvasSize", &JSCanvasSize);
}
#endif

#ifdef _WIN32
// Prefer the high-performance GPU on switchable GPU systems
#include <windows.h>
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace Urho3D
{
    static const GLenum glCmpFunc[] =
    {
        GL_ALWAYS,
        GL_EQUAL,
        GL_NOTEQUAL,
        GL_LESS,
        GL_LEQUAL,
        GL_GREATER,
        GL_GEQUAL
    };

    static const GLenum glSrcBlend[] =
    {
        GL_ONE,
        GL_ONE,
        GL_DST_COLOR,
        GL_SRC_ALPHA,
        GL_SRC_ALPHA,
        GL_ONE,
        GL_ONE_MINUS_DST_ALPHA,
        GL_ONE,
        GL_SRC_ALPHA
    };

    static const GLenum glDestBlend[] =
    {
        GL_ZERO,
        GL_ONE,
        GL_ZERO,
        GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE,
        GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA,
        GL_ONE,
        GL_ONE
    };

    static const GLenum glBlendOp[] =
    {
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_ADD,
        GL_FUNC_REVERSE_SUBTRACT,
        GL_FUNC_REVERSE_SUBTRACT
    };

#if !ALIMER_OPENGLES
    static const GLenum glFillMode[] =
    {
        GL_FILL,
        GL_LINE,
        GL_POINT
    };
#endif

    static const GLenum glStencilOps[] =
    {
        GL_KEEP,
        GL_ZERO,
        GL_REPLACE,
        GL_INCR_WRAP,
        GL_DECR_WRAP
    };

    static const GLenum glElementTypes[] =
    {
        GL_INT,
        GL_FLOAT,
        GL_FLOAT,
        GL_FLOAT,
        GL_FLOAT,
        GL_UNSIGNED_BYTE,
        GL_UNSIGNED_BYTE
    };

    static const GLenum glElementComponents[] =
    {
        1,
        1,
        2,
        3,
        4,
        4,
        4
    };

#if ALIMER_OPENGLES
    static constexpr GLenum glesDepthStencilFormat = GL_DEPTH_COMPONENT16;
#endif

    static String extensions;

    bool CheckExtension(const String& name)
    {
        if (extensions.Empty())
            extensions = (const char*)glGetString(GL_EXTENSIONS);
        return extensions.Contains(name);
    }

    static void GetGLPrimitiveType(uint32_t elementCount, PrimitiveType type, uint32_t& primitiveCount, GLenum& glPrimitiveType)
    {
        switch (type)
        {
            case PrimitiveType::TriangleList:
                primitiveCount = elementCount / 3;
                glPrimitiveType = GL_TRIANGLES;
                break;

            case PrimitiveType::LineList:
                primitiveCount = elementCount / 2;
                glPrimitiveType = GL_LINES;
                break;

            case PrimitiveType::PointList:
                primitiveCount = elementCount;
                glPrimitiveType = GL_POINTS;
                break;

            case PrimitiveType::TriangleStrip:
                primitiveCount = elementCount - 2;
                glPrimitiveType = GL_TRIANGLE_STRIP;
                break;

            case PrimitiveType::LineStrip:
                primitiveCount = elementCount - 1;
                glPrimitiveType = GL_LINE_STRIP;
                break;
        }
    }

    Graphics::Graphics(Context* context)
        : Object(context)
        , impl_(new GraphicsImpl())
        , position_(SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED)
        , shadowMapFormat_(GL_DEPTH_COMPONENT16)
        , hiresShadowMapFormat_(GL_DEPTH_COMPONENT24)
        , shaderPath_("Shaders/GLSL/")
        , shaderExtension_(".glsl")
        , orientations_("LandscapeLeft LandscapeRight")
#if ALIMER_OPENGLES
        , apiName_("GLES3")
#else
        , apiName_("GL3")
#endif
    {
        SetTextureUnitMappings();
        ResetCachedState();

        context_->RequireSDL(SDL_INIT_VIDEO);

        // Register Graphics library object factories
        RegisterGraphicsLibrary(context_);

#ifdef __EMSCRIPTEN__
        appContext = context_;
#endif
    }

    Graphics::~Graphics()
    {
        Close();

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

        if (IsInitialized() && width == width_ && height == height_ && screenParams_ == newParams)
            return true;

        // If only vsync changes, do not destroy/recreate the context
        if (IsInitialized() && width == width_ && height == height_
            && screenParams_.EqualsExceptVSync(newParams) && screenParams_.vsync_ != newParams.vsync_)
        {
            SDL_GL_SetSwapInterval(newParams.vsync_ ? 1 : 0);
            screenParams_.vsync_ = newParams.vsync_;
            return true;
        }

        // Track if the window was repositioned and don't update window position in this case
        bool reposition = false;

        // With an external window, only the size can change after initial setup, so do not recreate context
        if (!externalWindow_ || !impl_->context_)
        {
            // Close the existing window and OpenGL context, mark GPU objects as lost
            Release(false, true);

            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

#if !ALIMER_OPENGLES
            SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

            if (externalWindow_)
                SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
            else
                SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

            SDL_Rect display_rect;
            SDL_GetDisplayBounds(newParams.monitor_, &display_rect);
            reposition = newParams.fullscreen_ || (newParams.borderless_ && width >= display_rect.w && height >= display_rect.h);

            const int x = reposition ? display_rect.x : position_.x;
            const int y = reposition ? display_rect.y : position_.y;

            unsigned flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
            if (newParams.fullscreen_)
                flags |= SDL_WINDOW_FULLSCREEN;
            if (newParams.borderless_)
                flags |= SDL_WINDOW_BORDERLESS;
            if (newParams.resizable_)
                flags |= SDL_WINDOW_RESIZABLE;

#ifndef __EMSCRIPTEN__
            if (newParams.highDPI_)
                flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif

            SDL_SetHint(SDL_HINT_ORIENTATIONS, orientations_.CString());

            // Try 24-bit depth first, fallback to 16-bit
            for (const int depthSize : { 24, 16 })
            {
                SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthSize);

                // Try requested multisample level first, fallback to lower levels and no multisample
                for (int multiSample = newParams.multiSample_; multiSample > 0; multiSample /= 2)
                {
                    if (multiSample > 1)
                    {
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multiSample);
                    }
                    else
                    {
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
                    }

                    if (!externalWindow_)
                        window_ = SDL_CreateWindow(windowTitle_.CString(), x, y, width, height, flags);
                    else
                    {
#ifndef __EMSCRIPTEN__
                        if (!window_)
                            window_ = SDL_CreateWindowFrom(externalWindow_, SDL_WINDOW_OPENGL);
                        newParams.fullscreen_ = false;
#endif
                    }

                    if (window_)
                    {
                        // TODO: We probably want to keep depthSize as well
                        newParams.multiSample_ = multiSample;
                        break;
                    }
                }

                if (window_)
                    break;
            }

            if (!window_)
            {
                URHO3D_LOGERRORF("Could not create window, root cause: '%s'", SDL_GetError());
                return false;
            }

            // Reposition the window on the specified monitor
            if (reposition)
                SDL_SetWindowPosition(window_, display_rect.x, display_rect.y);

            CreateWindowIcon();

            if (maximize)
            {
                Maximize();
                SDL_GL_GetDrawableSize(window_, &width, &height);
            }

            // Create/restore context and GPU objects and set initial renderstate
            Restore();

            // Specific error message is already logged by Restore() when context creation or OpenGL extensions check fails
            if (!impl_->context_)
                return false;
        }

        // Set vsync
        SDL_GL_SetSwapInterval(newParams.vsync_ ? 1 : 0);

        // Store the system FBO on iOS/tvOS now
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&impl_->systemFBO_);

        screenParams_ = newParams;

        SDL_GL_GetDrawableSize(window_, &width_, &height_);
        if (!reposition)
            SDL_GetWindowPosition(window_, &position_.x, &position_.y);

        int logicalWidth, logicalHeight;
        SDL_GetWindowSize(window_, &logicalWidth, &logicalHeight);
        screenParams_.highDPI_ = (width_ != logicalWidth) || (height_ != logicalHeight);

        // Reset rendertargets and viewport for the new screen mode
        ResetRenderTargets();

        // Clear the initial window contents to black
        Clear(ClearTargetFlags::Color);
        SDL_GL_SwapWindow(window_);

        CheckFeatureSupport();

#ifdef URHO3D_LOGGING
        URHO3D_LOGINFOF("Adapter used %s %s", (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER));
#endif

        OnScreenModeChanged();
        return true;
    }

    void Graphics::SetSRGB(bool enable)
    {
        enable &= sRGBWriteSupport_;

        if (enable != sRGB_)
        {
            sRGB_ = enable;
            impl_->fboDirty_ = true;
        }
    }

    void Graphics::SetFlushGPU(bool enable)
    {
        // Currently unimplemented on OpenGL
    }

    void Graphics::Close()
    {
        if (!IsInitialized())
            return;

        // Actually close the window
        Release(true, true);
    }

    bool Graphics::TakeScreenShot(Image& destImage)
    {
        URHO3D_PROFILE(TakeScreenShot);

        if (!IsInitialized())
            return false;

        if (IsDeviceLost())
        {
            URHO3D_LOGERROR("Can not take screenshot while device is lost");
            return false;
        }

        ResetRenderTargets();

#if !ALIMER_OPENGLES
        destImage.SetSize(width_, height_, 3);
        glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, destImage.GetData());
#else
        // Use RGBA format on OpenGL ES, as otherwise (at least on Android) the produced image is all black
        destImage.SetSize(width_, height_, 4);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, destImage.GetData());
#endif

        // On OpenGL we need to flip the image vertically after reading
        destImage.FlipVertical();

        return true;
    }

    bool Graphics::BeginFrame()
    {
        if (!IsInitialized() || IsDeviceLost())
            return false;

        // If using an external window, check it for size changes, and reset screen mode if necessary
        if (externalWindow_)
        {
            int width, height;

            SDL_GL_GetDrawableSize(window_, &width, &height);
            if (width != width_ || height != height_)
                SetMode(width, height);
        }

        // Re-enable depth test and depth func in case a third party program has modified it
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(glCmpFunc[depthTestMode_]);

        // Set default rendertarget and depth buffer
        ResetRenderTargets();

        // Cleanup textures from previous frame
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
            SetTexture(i, nullptr);

        // Enable color and depth write
        SetColorWrite(true);
        SetDepthWrite(true);

        numPrimitives_ = 0;
        numBatches_ = 0;

        SendEvent(E_BEGINRENDERING);

        return true;
    }

    void Graphics::EndFrame()
    {
        if (!IsInitialized())
            return;

        URHO3D_PROFILE(Present);

        SendEvent(E_ENDRENDERING);

        SDL_GL_SwapWindow(window_);

        // Clean up too large scratch buffers
        CleanupScratchBuffers();
    }

    void Graphics::Clear(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
    {
        PrepareDraw();

#if ALIMER_OPENGLES
        flags &= ~CLEAR_STENCIL;
#endif

        bool oldColorWrite = colorWrite_;
        bool oldDepthWrite = depthWrite_;

        if ((flags & ClearTargetFlags::Color) != 0 && !oldColorWrite)
            SetColorWrite(true);
        if ((flags & ClearTargetFlags::Depth) != 0 && !oldDepthWrite)
            SetDepthWrite(true);
        if ((flags & ClearTargetFlags::Stencil) != 0 && stencilWriteMask_ != M_MAX_UNSIGNED)
            glStencilMask(M_MAX_UNSIGNED);

        GLbitfield glFlags = 0;
        if ((flags & ClearTargetFlags::Color) != 0)
        {
            glFlags |= GL_COLOR_BUFFER_BIT;
            glClearColor(color.r_, color.g_, color.b_, color.a_);
        }
        if ((flags & ClearTargetFlags::Depth) != 0)
        {
            glFlags |= GL_DEPTH_BUFFER_BIT;
            glClearDepth(depth);
        }
        if ((flags & ClearTargetFlags::Stencil) != 0)
        {
            glFlags |= GL_STENCIL_BUFFER_BIT;
            glClearStencil(stencil);
        }

        // If viewport is less than full screen, set a scissor to limit the clear
        /// \todo Any user-set scissor test will be lost
        IntVector2 viewSize = GetRenderTargetDimensions();
        if (viewport_.left_ != 0 || viewport_.top_ != 0 || viewport_.right_ != viewSize.x || viewport_.bottom_ != viewSize.y)
            SetScissorTest(true, IntRect(0, 0, viewport_.Width(), viewport_.Height()));
        else
            SetScissorTest(false);

        glClear(glFlags);

        SetScissorTest(false);
        SetColorWrite(oldColorWrite);
        SetDepthWrite(oldDepthWrite);
        if ((flags & ClearTargetFlags::Stencil) != 0
            && stencilWriteMask_ != M_MAX_UNSIGNED)
        {
            glStencilMask(stencilWriteMask_);
        }
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
        vpCopy.left_ = Clamp(vpCopy.left_, 0, width_);
        vpCopy.top_ = Clamp(vpCopy.top_, 0, height_);
        vpCopy.right_ = Clamp(vpCopy.right_, 0, width_);
        vpCopy.bottom_ = Clamp(vpCopy.bottom_, 0, height_);

        // Make sure the FBO is not in use
        ResetRenderTargets();

        // Use Direct3D convention with the vertical coordinates ie. 0 is top
        SetTextureForUpdate(destination);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vpCopy.left_, height_ - vpCopy.bottom_, vpCopy.Width(), vpCopy.Height());
        SetTexture(0, nullptr);

        return true;
    }

    bool Graphics::ResolveToTexture(Texture2D* texture)
    {
        if (!texture)
            return false;
        RenderSurface* surface = texture->GetRenderSurface();
        if (!surface || !surface->GetRenderBuffer())
            return false;

        URHO3D_PROFILE(ResolveToTexture);

        texture->SetResolveDirty(false);
        surface->SetResolveDirty(false);

        // Use separate FBOs for resolve to not disturb the currently set rendertarget(s)
        if (!impl_->resolveSrcFBO_)
            glGenFramebuffers(1, &impl_->resolveSrcFBO_);
        if (!impl_->resolveDestFBO_)
            glGenFramebuffers(1, &impl_->resolveDestFBO_);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, impl_->resolveSrcFBO_);
        glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, surface->GetRenderBuffer());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, impl_->resolveDestFBO_);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->GetGPUObjectName(), 0);
        glBlitFramebuffer(0, 0, texture->GetWidth(), texture->GetHeight(), 0, 0, texture->GetWidth(), texture->GetHeight(),
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Restore previously bound FBO
        glBindFramebuffer(GL_FRAMEBUFFER, impl_->boundFBO_);
        return true;
    }

    bool Graphics::ResolveToTexture(TextureCube* texture)
    {
        if (!texture)
            return false;

        URHO3D_PROFILE(ResolveToTexture);

        texture->SetResolveDirty(false);

        // Use separate FBOs for resolve to not disturb the currently set rendertarget(s)
        if (!impl_->resolveSrcFBO_)
            glGenFramebuffers(1, &impl_->resolveSrcFBO_);
        if (!impl_->resolveDestFBO_)
            glGenFramebuffers(1, &impl_->resolveDestFBO_);

        for (uint32_t i = 0; i < MAX_CUBEMAP_FACES; ++i)
        {
            RenderSurface* surface = texture->GetRenderSurface((CubeMapFace)i);
            if (!surface->IsResolveDirty())
                continue;

            surface->SetResolveDirty(false);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, impl_->resolveSrcFBO_);
            glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, surface->GetRenderBuffer());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, impl_->resolveDestFBO_);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                texture->GetGPUObjectName(), 0);
            glBlitFramebuffer(0, 0, texture->GetWidth(), texture->GetHeight(), 0, 0, texture->GetWidth(), texture->GetHeight(),
                GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Restore previously bound FBO
        glBindFramebuffer(GL_FRAMEBUFFER, impl_->boundFBO_);
        return true;
    }

    void Graphics::Draw(PrimitiveType type, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex)
    {
        if (!vertexCount)
            return;

        PrepareDraw();

        uint32_t primitiveCount;
        GLenum glPrimitiveType;

        GetGLPrimitiveType(vertexCount, type, primitiveCount, glPrimitiveType);
        if (instanceCount > 1)
        {
            glDrawArraysInstanced(glPrimitiveType, firstVertex, vertexCount, instanceCount);
        }
        else
        {
            glDrawArrays(glPrimitiveType, firstVertex, vertexCount);
        }

        numPrimitives_ += primitiveCount;
        ++numBatches_;
    }

    void Graphics::DrawIndexed(PrimitiveType type, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex)
    {
        if (!indexCount || !indexBuffer_ || !indexBuffer_->GetGPUObjectName())
            return;

        PrepareDraw();

        uint32_t indexSize = indexBuffer_->GetIndexSize();
        uint32_t primitiveCount;
        GLenum glPrimitiveType;

        GetGLPrimitiveType(indexCount, type, primitiveCount, glPrimitiveType);
        GLenum indexType = indexSize == sizeof(uint16_t) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        if (instanceCount > 1)
        {
#if !ALIMER_OPENGLES
            if (baseVertex != 0)
            {
                glDrawElementsInstancedBaseVertex(glPrimitiveType,
                    indexCount,
                    indexType,
                    (const GLvoid*)(GLintptr)(firstIndex * indexSize),
                    instanceCount,
                    baseVertex);
            }
            else
#endif
            {
                glDrawElementsInstanced(glPrimitiveType,
                    indexCount,
                    indexType,
                    (const GLvoid*)(GLintptr)(firstIndex * indexSize),
                    instanceCount);
            }
        }
        else
        {
#if !ALIMER_OPENGLES
            if (baseVertex != 0)
            {
                glDrawElementsBaseVertex(glPrimitiveType,
                    indexCount,
                    indexType,
                    (const GLvoid*)(GLintptr)(firstIndex * indexSize),
                    baseVertex);
            }
            else
#endif
            {
                glDrawElements(glPrimitiveType,
                    indexCount,
                    indexType,
                    (const GLvoid*)(GLintptr)(firstIndex * indexSize)
                );
            }
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

        if (instanceOffset != impl_->lastInstanceOffset_)
        {
            impl_->lastInstanceOffset_ = instanceOffset;
            impl_->vertexBuffersDirty_ = true;
        }

        for (uint32_t i = 0; i < kMaxVertexBufferBindings; ++i)
        {
            VertexBuffer* buffer = nullptr;
            if (i < buffers.Size())
                buffer = buffers[i];
            if (buffer != vertexBuffers_[i])
            {
                vertexBuffers_[i] = buffer;
                impl_->vertexBuffersDirty_ = true;
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
        if (indexBuffer_ == buffer)
            return;

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer ? buffer->GetGPUObjectName() : 0);
        indexBuffer_ = buffer;
    }

    void Graphics::SetShaders(ShaderVariation* vs, ShaderVariation* ps)
    {
        if (vs == vertexShader_ && ps == pixelShader_)
            return;

        // Compile the shaders now if not yet compiled. If already attempted, do not retry
        if (vs && !vs->GetGPUObjectName())
        {
            if (vs->GetCompilerOutput().Empty())
            {
                URHO3D_PROFILE(CompileVertexShader);

                bool success = vs->Create();
                if (success)
                    URHO3D_LOGDEBUG("Compiled vertex shader " + vs->GetFullName());
                else
                {
                    URHO3D_LOGERROR("Failed to compile vertex shader " + vs->GetFullName() + ":\n" + vs->GetCompilerOutput());
                    vs = nullptr;
                }
            }
            else
                vs = nullptr;
        }

        if (ps && !ps->GetGPUObjectName())
        {
            if (ps->GetCompilerOutput().Empty())
            {
                URHO3D_PROFILE(CompilePixelShader);

                bool success = ps->Create();
                if (success)
                    URHO3D_LOGDEBUG("Compiled pixel shader " + ps->GetFullName());
                else
                {
                    URHO3D_LOGERROR("Failed to compile pixel shader " + ps->GetFullName() + ":\n" + ps->GetCompilerOutput());
                    ps = nullptr;
                }
            }
            else
                ps = nullptr;
        }

        if (!vs || !ps)
        {
            glUseProgram(0);
            vertexShader_ = nullptr;
            pixelShader_ = nullptr;
            impl_->shaderProgram_ = nullptr;
        }
        else
        {
            vertexShader_ = vs;
            pixelShader_ = ps;

            std::pair<ShaderVariation*, ShaderVariation*> combination(vs, ps);
            ShaderProgramMap::iterator i = impl_->shaderPrograms_.find(combination);

            if (i != impl_->shaderPrograms_.end())
            {
                // Use the existing linked program
                if (i->second->GetGPUObjectName())
                {
                    glUseProgram(i->second->GetGPUObjectName());
                    impl_->shaderProgram_ = i->second;
                }
                else
                {
                    glUseProgram(0);
                    impl_->shaderProgram_ = nullptr;
                }
            }
            else
            {
                // Link a new combination
                URHO3D_PROFILE(LinkShaders);

                SharedPtr<ShaderProgram> newProgram(new ShaderProgram(this, vs, ps));
                if (newProgram->Link())
                {
                    URHO3D_LOGDEBUG("Linked vertex shader " + vs->GetFullName() + " and pixel shader " + ps->GetFullName());
                    // Note: Link() calls glUseProgram() to set the texture sampler uniforms,
                    // so it is not necessary to call it again
                    impl_->shaderProgram_ = newProgram;
                }
                else
                {
                    URHO3D_LOGERROR("Failed to link vertex shader " + vs->GetFullName() + " and pixel shader " + ps->GetFullName() + ":\n" +
                        newProgram->GetLinkerOutput());
                    glUseProgram(0);
                    impl_->shaderProgram_ = nullptr;
                }

                impl_->shaderPrograms_[combination] = newProgram;
            }
        }

        // Update the clip plane uniform on GL3, and set constant buffers
        if (impl_->shaderProgram_)
        {
            const SharedPtr<ConstantBuffer>* constantBuffers = impl_->shaderProgram_->GetConstantBuffers();
            for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS * 2; ++i)
            {
                ConstantBuffer* buffer = constantBuffers[i].Get();
                if (buffer != impl_->constantBuffers_[i])
                {
                    unsigned object = buffer ? buffer->GetGPUObjectName() : 0;
                    glBindBufferBase(GL_UNIFORM_BUFFER, i, object);
                    // Calling glBindBufferBase also affects the generic buffer binding point
                    impl_->boundUBO_ = object;
                    impl_->constantBuffers_[i] = buffer;
                    ShaderProgram::ClearGlobalParameterSource((ShaderParameterGroup)(i % MAX_SHADER_PARAMETER_GROUPS));
                }
            }

            SetShaderParameter(VSP_CLIPPLANE, useClipPlane_ ? clipPlane_ : Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        // Store shader combination if shader dumping in progress
        if (shaderPrecache_)
            shaderPrecache_->StoreShaders(vertexShader_, pixelShader_);

        if (impl_->shaderProgram_)
        {
            impl_->usedVertexAttributes_ = impl_->shaderProgram_->GetUsedVertexAttributes();
            impl_->vertexAttributes_ = &impl_->shaderProgram_->GetVertexAttributes();
        }
        else
        {
            impl_->usedVertexAttributes_ = 0;
            impl_->vertexAttributes_ = nullptr;
        }

        impl_->vertexBuffersDirty_ = true;
    }

    void Graphics::SetShaderParameter(StringHash param, const float* data, unsigned count)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                    {
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    }

                    buffer->SetParameter(info->offset_, (unsigned)(count * sizeof(float)), data);
                    return;
                }

                switch (info->glType_)
                {
                    case GL_FLOAT:
                        glUniform1fv(info->location_, count, data);
                        break;

                    case GL_FLOAT_VEC2:
                        glUniform2fv(info->location_, count / 2, data);
                        break;

                    case GL_FLOAT_VEC3:
                        glUniform3fv(info->location_, count / 3, data);
                        break;

                    case GL_FLOAT_VEC4:
                        glUniform4fv(info->location_, count / 4, data);
                        break;

                    case GL_FLOAT_MAT3:
                        glUniformMatrix3fv(info->location_, count / 9, GL_FALSE, data);
                        break;

                    case GL_FLOAT_MAT4:
                        glUniformMatrix4fv(info->location_, count / 16, GL_FALSE, data);
                        break;

                    default: break;
                }
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, float value)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                    {
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    }
                    buffer->SetParameter(info->offset_, sizeof(float), &value);
                    return;
                }

                glUniform1fv(info->location_, 1, &value);
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, int value)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(int), &value);
                    return;
                }

                glUniform1i(info->location_, value);
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, bool value)
    {
        // \todo Not tested
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(bool), &value);
                    return;
                }

                glUniform1i(info->location_, (int)value);
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Color& color)
    {
        SetShaderParameter(param, color.Data(), 4);
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector2& vector)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(Vector2), &vector);
                    return;
                }

                // Check the uniform type to avoid mismatch
                switch (info->glType_)
                {
                    case GL_FLOAT:
                        glUniform1fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC2:
                        glUniform2fv(info->location_, 1, vector.Data());
                        break;

                    default: break;
                }
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix3& matrix)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetVector3ArrayParameter(info->offset_, 3, &matrix);
                    return;
                }

                glUniformMatrix3fv(info->location_, 1, GL_FALSE, matrix.Data());
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector3& vector)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(Vector3), &vector);
                    return;
                }

                // Check the uniform type to avoid mismatch
                switch (info->glType_)
                {
                    case GL_FLOAT:
                        glUniform1fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC2:
                        glUniform2fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC3:
                        glUniform3fv(info->location_, 1, vector.Data());
                        break;

                    default: break;
                }
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix4& matrix)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(Matrix4), &matrix);
                    return;
                }

                glUniformMatrix4fv(info->location_, 1, GL_FALSE, matrix.Data());
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Vector4& vector)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(Vector4), &vector);
                    return;
                }

                // Check the uniform type to avoid mismatch
                switch (info->glType_)
                {
                    case GL_FLOAT:
                        glUniform1fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC2:
                        glUniform2fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC3:
                        glUniform3fv(info->location_, 1, vector.Data());
                        break;

                    case GL_FLOAT_VEC4:
                        glUniform4fv(info->location_, 1, vector.Data());
                        break;

                    default: break;
                }
            }
        }
    }

    void Graphics::SetShaderParameter(StringHash param, const Matrix3x4& matrix)
    {
        if (impl_->shaderProgram_)
        {
            const ShaderParameter* info = impl_->shaderProgram_->GetParameter(param);
            if (info)
            {
                // Expand to a full Matrix4
                static Matrix4 fullMatrix;
                fullMatrix.m00_ = matrix.m00_;
                fullMatrix.m01_ = matrix.m01_;
                fullMatrix.m02_ = matrix.m02_;
                fullMatrix.m03_ = matrix.m03_;
                fullMatrix.m10_ = matrix.m10_;
                fullMatrix.m11_ = matrix.m11_;
                fullMatrix.m12_ = matrix.m12_;
                fullMatrix.m13_ = matrix.m13_;
                fullMatrix.m20_ = matrix.m20_;
                fullMatrix.m21_ = matrix.m21_;
                fullMatrix.m22_ = matrix.m22_;
                fullMatrix.m23_ = matrix.m23_;

                if (info->bufferPtr_)
                {
                    ConstantBuffer* buffer = info->bufferPtr_;
                    if (!buffer->IsDirty())
                        impl_->dirtyConstantBuffers.push_back(buffer);
                    buffer->SetParameter(info->offset_, sizeof(Matrix4), &fullMatrix);
                    return;
                }

                glUniformMatrix4fv(info->location_, 1, GL_FALSE, fullMatrix.Data());
            }
        }
    }

    bool Graphics::NeedParameterUpdate(ShaderParameterGroup group, const void* source)
    {
        return impl_->shaderProgram_ ? impl_->shaderProgram_->NeedParameterUpdate(group, source) : false;
    }

    bool Graphics::HasShaderParameter(StringHash param)
    {
        return impl_->shaderProgram_ && impl_->shaderProgram_->HasParameter(param);
    }

    bool Graphics::HasTextureUnit(TextureUnit unit)
    {
        return impl_->shaderProgram_ && impl_->shaderProgram_->HasTextureUnit(unit);
    }

    void Graphics::ClearParameterSource(ShaderParameterGroup group)
    {
        if (impl_->shaderProgram_)
            impl_->shaderProgram_->ClearParameterSource(group);
    }

    void Graphics::ClearParameterSources()
    {
        ShaderProgram::ClearParameterSources();
    }

    void Graphics::ClearTransformSources()
    {
        if (impl_->shaderProgram_)
        {
            impl_->shaderProgram_->ClearParameterSource(SP_CAMERA);
            impl_->shaderProgram_->ClearParameterSource(SP_OBJECT);
        }
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
        }

        if (textures_[index] != texture)
        {
            if (impl_->activeTexture_ != index)
            {
                glActiveTexture(GL_TEXTURE0 + index);
                impl_->activeTexture_ = index;
            }

            if (texture)
            {
                unsigned glType = texture->GetTarget();
                // Unbind old texture type if necessary
                if (impl_->textureTypes_[index] && impl_->textureTypes_[index] != glType)
                    glBindTexture(impl_->textureTypes_[index], 0);
                glBindTexture(glType, texture->GetGPUObjectName());
                impl_->textureTypes_[index] = glType;

                if (texture->GetParametersDirty())
                    texture->UpdateParameters();
                if (texture->GetLevelsDirty())
                    texture->RegenerateLevels();
            }
            else if (impl_->textureTypes_[index])
            {
                glBindTexture(impl_->textureTypes_[index], 0);
                impl_->textureTypes_[index] = 0;
            }

            textures_[index] = texture;
        }
        else
        {
            if (texture && (texture->GetParametersDirty() || texture->GetLevelsDirty()))
            {
                if (impl_->activeTexture_ != index)
                {
                    glActiveTexture(GL_TEXTURE0 + index);
                    impl_->activeTexture_ = index;
                }

                glBindTexture(texture->GetTarget(), texture->GetGPUObjectName());
                if (texture->GetParametersDirty())
                    texture->UpdateParameters();
                if (texture->GetLevelsDirty())
                    texture->RegenerateLevels();
            }
        }
    }

    void Graphics::SetTextureForUpdate(Texture* texture)
    {
        if (impl_->activeTexture_ != 0)
        {
            glActiveTexture(GL_TEXTURE0);
            impl_->activeTexture_ = 0;
        }

        unsigned glType = texture->GetTarget();
        // Unbind old texture type if necessary
        if (impl_->textureTypes_[0] && impl_->textureTypes_[0] != glType)
            glBindTexture(impl_->textureTypes_[0], 0);
        glBindTexture(glType, texture->GetGPUObjectName());
        impl_->textureTypes_[0] = glType;
        textures_[0] = texture;
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

    void Graphics::SetTextureParametersDirty()
    {
        std::lock_guard<std::mutex> lock(gpuObjectMutex_);

        for (GPUObject* object : gpuObjects)
        {
            auto* texture = dynamic_cast<Texture*>(object);
            if (texture)
                texture->SetParametersDirty();
        }
    }

    void Graphics::ResetRenderTargets()
    {
        for (uint32_t i = 0; i < kMaxColorAttachments; ++i)
        {
            SetRenderTarget(i, (RenderSurface*)nullptr);
        }

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

    void Graphics::SetRenderTarget(uint32_t index, RenderSurface* renderTarget)
    {
        if (index >= kMaxColorAttachments)
            return;

        if (renderTarget != renderTargets_[index])
        {
            renderTargets_[index] = renderTarget;

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

            impl_->fboDirty_ = true;
        }
    }

    void Graphics::SetRenderTarget(uint32_t index, Texture2D* texture)
    {
        RenderSurface* renderTarget = nullptr;
        if (texture)
            renderTarget = texture->GetRenderSurface();

        SetRenderTarget(index, renderTarget);
    }

    void Graphics::SetDepthStencil(RenderSurface* depthStencil)
    {
        // If we are using a rendertarget texture, it is required in OpenGL to also have an own depth-stencil
        // Create a new depth-stencil texture as necessary to be able to provide similar behaviour as Direct3D9
        // Only do this for non-multisampled rendertargets; when using multisampled target a similarly multisampled
        // depth-stencil should also be provided (backbuffer depth isn't compatible)
        if (renderTargets_[0] && renderTargets_[0]->GetMultiSample() == 1 && !depthStencil)
        {
            int width = renderTargets_[0]->GetWidth();
            int height = renderTargets_[0]->GetHeight();

            // Direct3D9 default depth-stencil can not be used when rendertarget is larger than the window.
            // Check size similarly
            if (width <= width_ && height <= height_)
            {
                unsigned searchKey = (width << 16u) | height;

                auto i = impl_->depthTextures_.find(searchKey);
                if (i != impl_->depthTextures_.end())
                {
                    depthStencil = i->second->GetRenderSurface();
                }
                else
                {
                    SharedPtr<Texture2D> newDepthTexture(new Texture2D(context_));
                    newDepthTexture->SetSize(width, height, GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL);
                    impl_->depthTextures_[searchKey] = newDepthTexture;
                    depthStencil = newDepthTexture->GetRenderSurface();
                }
            }
        }

        if (depthStencil != depthStencil_)
        {
            depthStencil_ = depthStencil;
            impl_->fboDirty_ = true;
        }
    }

    void Graphics::SetDepthStencil(Texture2D* texture)
    {
        RenderSurface* depthStencil = nullptr;
        if (texture)
            depthStencil = texture->GetRenderSurface();

        SetDepthStencil(depthStencil);
    }

    void Graphics::SetViewport(const IntRect& rect)
    {
        PrepareDraw();

        IntVector2 rtSize = GetRenderTargetDimensions();

        IntRect rectCopy = rect;

        if (rectCopy.right_ <= rectCopy.left_)
            rectCopy.right_ = rectCopy.left_ + 1;
        if (rectCopy.bottom_ <= rectCopy.top_)
            rectCopy.bottom_ = rectCopy.top_ + 1;
        rectCopy.left_ = Clamp(rectCopy.left_, 0, rtSize.x);
        rectCopy.top_ = Clamp(rectCopy.top_, 0, rtSize.y);
        rectCopy.right_ = Clamp(rectCopy.right_, 0, rtSize.x);
        rectCopy.bottom_ = Clamp(rectCopy.bottom_, 0, rtSize.y);

        // Use Direct3D convention with the vertical coordinates ie. 0 is top
        glViewport(rectCopy.left_, rtSize.y - rectCopy.bottom_, rectCopy.Width(), rectCopy.Height());
        viewport_ = rectCopy;

        // Disable scissor test, needs to be re-enabled by the user
        SetScissorTest(false);
    }

    void Graphics::SetBlendMode(BlendMode mode, bool alphaToCoverage)
    {
        if (mode != blendMode_)
        {
            if (mode == BLEND_REPLACE)
                glDisable(GL_BLEND);
            else
            {
                glEnable(GL_BLEND);
                glBlendFunc(glSrcBlend[mode], glDestBlend[mode]);
                glBlendEquation(glBlendOp[mode]);
            }

            blendMode_ = mode;
        }

        if (alphaToCoverage != alphaToCoverage_)
        {
            if (alphaToCoverage)
                glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            else
                glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

            alphaToCoverage_ = alphaToCoverage;
        }
    }

    void Graphics::SetColorWrite(bool enable)
    {
        if (enable != colorWrite_)
        {
            if (enable)
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            else
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

            colorWrite_ = enable;
        }
    }

    void Graphics::SetCullMode(CullMode mode)
    {
        if (mode != cullMode_)
        {
            if (mode == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                // Use Direct3D convention, ie. clockwise vertices define a front face
                glEnable(GL_CULL_FACE);
                glCullFace((mode == CullMode::CounterClockwise) ? GL_FRONT : GL_BACK);
            }

            cullMode_ = mode;
        }
    }

    void Graphics::SetDepthBias(float constantBias, float slopeScaledBias)
    {
        if (constantBias != constantDepthBias_ || slopeScaledBias != slopeScaledDepthBias_)
        {
#if !ALIMER_OPENGLES
            if (slopeScaledBias != 0.0f)
            {
                // OpenGL constant bias is unreliable and dependent on depth buffer bitdepth, apply in the projection matrix instead
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(slopeScaledBias, 0.0f);
            }
            else
            {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
#endif

            constantDepthBias_ = constantBias;
            slopeScaledDepthBias_ = slopeScaledBias;
            // Force update of the projection matrix shader parameter
            ClearParameterSource(SP_CAMERA);
        }
    }

    void Graphics::SetDepthTest(CompareMode mode)
    {
        if (mode != depthTestMode_)
        {
            glDepthFunc(glCmpFunc[mode]);
            depthTestMode_ = mode;
        }
    }

    void Graphics::SetDepthWrite(bool enable)
    {
        if (enable != depthWrite_)
        {
            glDepthMask(enable ? GL_TRUE : GL_FALSE);
            depthWrite_ = enable;
        }
    }

    void Graphics::SetFillMode(FillMode mode)
    {
#if !ALIMER_OPENGLES
        if (mode != fillMode_)
        {
            glPolygonMode(GL_FRONT_AND_BACK, glFillMode[(uint32_t)mode]);
            fillMode_ = mode;
        }
#endif
    }

    void Graphics::SetLineAntiAlias(bool enable)
    {
#if !ALIMER_OPENGLES
        if (enable != lineAntiAlias_)
        {
            if (enable)
                glEnable(GL_LINE_SMOOTH);
            else
                glDisable(GL_LINE_SMOOTH);
            lineAntiAlias_ = enable;
        }
#endif
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

            if (enable && scissorRect_ != intRect)
            {
                // Use Direct3D convention with the vertical coordinates ie. 0 is top
                glScissor(intRect.left_, rtSize.y - intRect.bottom_, intRect.Width(), intRect.Height());
                scissorRect_ = intRect;
            }
        }
        else
            scissorRect_ = IntRect::ZERO;

        if (enable != scissorTest_)
        {
            if (enable)
                glEnable(GL_SCISSOR_TEST);
            else
                glDisable(GL_SCISSOR_TEST);
            scissorTest_ = enable;
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

            if (enable && scissorRect_ != intRect)
            {
                // Use Direct3D convention with the vertical coordinates ie. 0 is top
                glScissor(intRect.left_, rtSize.y - intRect.bottom_, intRect.Width(), intRect.Height());
                scissorRect_ = intRect;
            }
        }
        else
            scissorRect_ = IntRect::ZERO;

        if (enable != scissorTest_)
        {
            if (enable)
                glEnable(GL_SCISSOR_TEST);
            else
                glDisable(GL_SCISSOR_TEST);
            scissorTest_ = enable;
        }
    }

    void Graphics::SetClipPlane(bool enable, const Plane& clipPlane, const Matrix3x4& view, const Matrix4& projection)
    {
#if !ALIMER_OPENGLES && defined(GL_CLIP_PLANE_TODO)
        if (enable != useClipPlane_)
        {
            if (enable)
                glEnable(GL_CLIP_PLANE0);
            else
                glDisable(GL_CLIP_PLANE0);

            useClipPlane_ = enable;
        }

        if (enable)
        {
            Matrix4 viewProj = projection * view;
            clipPlane_ = clipPlane.Transformed(viewProj).ToVector4();

            if (!gl3Support)
            {
                GLdouble planeData[4];
                planeData[0] = clipPlane_.x_;
                planeData[1] = clipPlane_.y_;
                planeData[2] = clipPlane_.z_;
                planeData[3] = clipPlane_.w_;

                glClipPlane(GL_CLIP_PLANE0, &planeData[0]);
            }
        }
#endif
    }

    void Graphics::SetStencilTest(bool enable, CompareMode mode, StencilOp pass, StencilOp fail, StencilOp zFail, unsigned stencilRef,
        unsigned compareMask, unsigned writeMask)
    {
        if (enable != stencilTest_)
        {
            if (enable)
                glEnable(GL_STENCIL_TEST);
            else
                glDisable(GL_STENCIL_TEST);
            stencilTest_ = enable;
        }

        if (enable)
        {
            if (mode != stencilTestMode_ || stencilRef != stencilRef_ || compareMask != stencilCompareMask_)
            {
                glStencilFunc(glCmpFunc[mode], stencilRef, compareMask);
                stencilTestMode_ = mode;
                stencilRef_ = stencilRef;
                stencilCompareMask_ = compareMask;
            }
            if (writeMask != stencilWriteMask_)
            {
                glStencilMask(writeMask);
                stencilWriteMask_ = writeMask;
            }
            if (pass != stencilPass_ || fail != stencilFail_ || zFail != stencilZFail_)
            {
                glStencilOp(glStencilOps[fail], glStencilOps[zFail], glStencilOps[pass]);
                stencilPass_ = pass;
                stencilFail_ = fail;
                stencilZFail_ = zFail;
            }
        }
    }

    bool Graphics::IsInitialized() const
    {
        return window_ != nullptr;
    }

    bool Graphics::IsDeviceLost() const
    {
        // On iOS and tvOS treat window minimization as device loss, as it is forbidden to access OpenGL when minimized
#if defined(IOS) || defined(TVOS)
        if (window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0)
            return true;
#endif

        return impl_->context_ == nullptr;
    }

    PODVector<int> Graphics::GetMultiSampleLevels() const
    {
        PODVector<int> ret;
        // No multisampling always supported
        ret.Push(1);

        int maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        for (int i = 2; i <= maxSamples && i <= 16; i *= 2)
        {
            ret.Push(i);
        }

        return ret;
    }

    unsigned Graphics::GetFormat(CompressedFormat format) const
    {
        switch (format)
        {
            case CF_RGBA:
                return GL_RGBA;

            case CF_DXT1:
                return dxtTextureSupport_ ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : 0;

            case CF_DXT3:
                return dxtTextureSupport_ ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : 0;

            case CF_DXT5:
                return dxtTextureSupport_ ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : 0;

#if ALIMER_OPENGLES
            case CF_ETC1:
                return etcTextureSupport_ ? GL_ETC1_RGB8_OES : 0;

            case CF_ETC2_RGB:
                return etc2TextureSupport_ ? GL_ETC2_RGB8_OES : 0;

            case CF_ETC2_RGBA:
                return etc2TextureSupport_ ? GL_ETC2_RGBA8_OES : 0;

            case CF_PVRTC_RGB_2BPP:
                return pvrtcTextureSupport_ ? COMPRESSED_RGB_PVRTC_2BPPV1_IMG : 0;

            case CF_PVRTC_RGB_4BPP:
                return pvrtcTextureSupport_ ? COMPRESSED_RGB_PVRTC_4BPPV1_IMG : 0;

            case CF_PVRTC_RGBA_2BPP:
                return pvrtcTextureSupport_ ? COMPRESSED_RGBA_PVRTC_2BPPV1_IMG : 0;

            case CF_PVRTC_RGBA_4BPP:
                return pvrtcTextureSupport_ ? COMPRESSED_RGBA_PVRTC_4BPPV1_IMG : 0;
#endif

            default:
                return 0;
        }
    }

    uint32_t Graphics::GetMaxBones()
    {
        return 128u;
    }

    ShaderVariation* Graphics::GetShader(ShaderType type, const String& name, const String& defines) const
    {
        return GetShader(type, name.CString(), defines.CString());
    }

    ShaderVariation* Graphics::GetShader(ShaderType type, const char* name, const char* defines) const
    {
        if (lastShaderName_ != name || !lastShader_)
        {
            auto* cache = GetSubsystem<ResourceCache>();

            String fullShaderName = shaderPath_ + name + shaderExtension_;
            // Try to reduce repeated error log prints because of missing shaders
            if (lastShaderName_ == name && !cache->Exists(fullShaderName))
                return nullptr;

            lastShader_ = cache->GetResource<Shader>(fullShaderName);
            lastShaderName_ = name;
        }

        return lastShader_ ? lastShader_->GetVariation(type, defines) : nullptr;
    }

    VertexBuffer* Graphics::GetVertexBuffer(uint32_t index) const
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
        else if (depthStencil_)
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

    void Graphics::OnWindowResized()
    {
        if (!window_)
            return;

        int newWidth, newHeight;

        SDL_GL_GetDrawableSize(window_, &newWidth, &newHeight);
        if (newWidth == width_ && newHeight == height_)
            return;

        width_ = newWidth;
        height_ = newHeight;

        int logicalWidth, logicalHeight;
        SDL_GetWindowSize(window_, &logicalWidth, &logicalHeight);
        screenParams_.highDPI_ = (width_ != logicalWidth) || (height_ != logicalHeight);

        // Reset rendertargets and viewport for the new screen size. Also clean up any FBO's, as they may be screen size dependent
        impl_->CleanupFramebuffers(IsDeviceLost());
        ResetRenderTargets();

        URHO3D_LOGDEBUGF("Window was resized to %dx%d", width_, height_);

#ifdef __EMSCRIPTEN__
        EM_ASM({
            Module.SetRendererSize($0, $1);
            }, width_, height_);
#endif

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
        if (!window_ || screenParams_.fullscreen_)
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

    void Graphics::CleanupRenderSurface(RenderSurface* surface)
    {
        if (!surface)
            return;

        // Flush pending FBO changes first if any
        PrepareDraw();

        GLuint currentFBO = impl_->boundFBO_;

        // Go through all FBOs and clean up the surface from them
        for (auto i = impl_->frameBuffers_.begin(); i != impl_->frameBuffers_.end(); ++i)
        {
            for (uint32_t j = 0; j < kMaxColorAttachments; ++j)
            {
                if (i->second.colorAttachments_[j] == surface)
                {
                    if (currentFBO != i->second.fbo_)
                    {
                        glBindFramebuffer(GL_FRAMEBUFFER, i->second.fbo_);
                        currentFBO = i->second.fbo_;
                    }
                    impl_->BindColorAttachment(j, GL_TEXTURE_2D, 0, false);
                    i->second.colorAttachments_[j] = nullptr;
                    // Mark drawbuffer bits to need recalculation
                    i->second.drawBuffers_ = M_MAX_UNSIGNED;
                }
            }
            if (i->second.depthAttachment_ == surface)
            {
                if (currentFBO != i->second.fbo_)
                {
                    glBindFramebuffer(GL_FRAMEBUFFER, i->second.fbo_);
                    currentFBO = i->second.fbo_;
                }
                impl_->BindDepthAttachment(0, false);
                impl_->BindStencilAttachment(0, false);
                i->second.depthAttachment_ = nullptr;
            }
        }

        // Restore previously bound FBO now if needed
        if (currentFBO != impl_->boundFBO_)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, impl_->boundFBO_);
        }
    }

    void Graphics::CleanupShaderPrograms(ShaderVariation* variation)
    {
        for (ShaderProgramMap::iterator i = impl_->shaderPrograms_.begin(); i != impl_->shaderPrograms_.end();)
        {
            if (i->second->GetVertexShader() == variation || i->second->GetPixelShader() == variation)
                i = impl_->shaderPrograms_.erase(i);
            else
                ++i;
        }

        if (vertexShader_ == variation || pixelShader_ == variation)
            impl_->shaderProgram_ = nullptr;
    }

    ConstantBuffer* Graphics::GetOrCreateConstantBuffer(ShaderType /*type*/, unsigned index, unsigned size)
    {
        // Note: shaderType parameter is not used on OpenGL, instead binding index should already use the PS range
        // for PS constant buffers

        size_t key = 0;
        HashCombine(key, index);
        HashCombine(key, size);

        auto it = impl_->allConstantBuffers_.find(key);
        if (it == impl_->allConstantBuffers_.end())
        {
            it = impl_->allConstantBuffers_.insert(std::make_pair(key, SharedPtr<ConstantBuffer>(new ConstantBuffer(context_)))).first;
            it->second->SetSize(size);
        }

        return it->second.Get();
    }

    void Graphics::Release(bool clearGPUObjects, bool closeWindow)
    {
        if (!window_)
            return;

        if (clearGPUObjects)
        {
            // Shutting down: release all GPU objects that still exist
            // Shader programs are also GPU objects; clear them first to avoid list modification during iteration
            impl_->shaderPrograms_.clear();

            {
                std::lock_guard<std::mutex> lock(gpuObjectMutex_);

                for (GPUObject* object : gpuObjects)
                {
                    object->Release();
                }
            }

            gpuObjects.clear();
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(gpuObjectMutex_);

                // We are not shutting down, but recreating the context: mark GPU objects lost
                for (GPUObject* object : gpuObjects)
                {
                    object->OnDeviceLost();
                }
            }

            // In this case clear shader programs last so that they do not attempt to delete their OpenGL program
            // from a context that may no longer exist
            impl_->shaderPrograms_.clear();

            SendEvent(E_DEVICELOST);
        }

        impl_->CleanupFramebuffers(IsDeviceLost());
        impl_->depthTextures_.clear();

        // End fullscreen mode first to counteract transition and getting stuck problems on OS X
#if defined(__APPLE__) && !defined(IOS) && !defined(TVOS)
        if (closeWindow && screenParams_.fullscreen_ && !externalWindow_)
            SDL_SetWindowFullscreen(window_, 0);
#endif

        if (impl_->context_)
        {
            // Do not log this message if we are exiting
            if (!clearGPUObjects)
                URHO3D_LOGINFO("OpenGL context lost");

            SDL_GL_DeleteContext(impl_->context_);
            impl_->context_ = nullptr;
        }

        if (closeWindow)
        {
            SDL_ShowCursor(SDL_TRUE);

            // Do not destroy external window except when shutting down
            if (!externalWindow_ || clearGPUObjects)
            {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
        }
    }

    void Graphics::Restore()
    {
        if (!window_)
            return;

#ifdef __ANDROID__
        // On Android the context may be lost behind the scenes as the application is minimized
        if (impl_->context_ && !SDL_GL_GetCurrentContext())
        {
            impl_->context_ = 0;
            // Mark GPU objects lost without a current context. In this case they just mark their internal state lost
            // but do not perform OpenGL commands to delete the GL objects
            Release(false, false);
        }
#endif

        // Ensure first that the context exists
        if (!impl_->context_)
        {
            impl_->context_ = SDL_GL_CreateContext(window_);

            if (!impl_->context_)
            {
                URHO3D_LOGERRORF("Could not create OpenGL context, root cause '%s'", SDL_GetError());
                return;
            }

            // Clear cached extensions string from the previous context
            extensions.Clear();

            // Initialize OpenGL extensions library (desktop only)
#if !ALIMER_OPENGLES
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
            {
                URHO3D_LOGERROR("Could not initialize GLAD");
                return;
            }

            apiName_ = "GL3";
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&impl_->systemFBO_);


            // Create and bind a vertex array object that will stay in use throughout
            GLuint vertexArrayObject;
            glGenVertexArrays(1, &vertexArrayObject);
            glBindVertexArray(vertexArrayObject);

            // Enable seamless cubemap if possible
            // Note: even though we check the extension, this can lead to software fallback on some old GPU's
            // See https://github.com/urho3d/Urho3D/issues/1380 or
            // http://distrustsimplicity.net/articles/gl_texture_cube_map_seamless-on-os-x/
            // In case of trouble or for wanting maximum compatibility, simply remove the glEnable below.
            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#else
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&impl_->systemFBO_);
#endif

            // Set up texture data read/write alignment. It is important that this is done before uploading any texture data
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            ResetCachedState();
        }

        {
            std::lock_guard<std::mutex> lock(gpuObjectMutex_);

            for (GPUObject* object : gpuObjects)
            {
                object->OnDeviceReset();
            }
        }

        SendEvent(E_DEVICERESET);
            }

    void Graphics::MarkFBODirty()
    {
        impl_->fboDirty_ = true;
    }

    void Graphics::SetVBO(unsigned object)
    {
        if (impl_->boundVBO_ != object)
        {
            if (object)
                glBindBuffer(GL_ARRAY_BUFFER, object);
            impl_->boundVBO_ = object;
        }
    }

    void Graphics::SetUBO(unsigned object)
    {
        if (impl_->boundUBO_ != object)
        {
            if (object)
                glBindBuffer(GL_UNIFORM_BUFFER, object);
            impl_->boundUBO_ = object;
        }
    }

    unsigned Graphics::GetAlphaFormat()
    {
        // Alpha format is deprecated on OpenGL 3+
        return GL_R8;
    }

    unsigned Graphics::GetLuminanceFormat()
    {
        // Luminance format is deprecated on OpenGL 3+
        return GL_R8;
    }

    unsigned Graphics::GetLuminanceAlphaFormat()
    {
        // Luminance alpha format is deprecated on OpenGL 3+
        return GL_RG8;
    }

    unsigned Graphics::GetRGBFormat()
    {
        return GL_RGB;
    }

    unsigned Graphics::GetRGBAFormat()
    {
        return GL_RGBA;
    }

    unsigned Graphics::GetRGBA16Format()
    {
        return GL_RGBA16;
    }

    unsigned Graphics::GetRGBAFloat16Format()
    {
        return GL_RGBA16F;
    }

    unsigned Graphics::GetRGBAFloat32Format()
    {
        return GL_RGBA32F;
    }

    unsigned Graphics::GetRG16Format()
    {
        return GL_RG16;
    }

    unsigned Graphics::GetRGFloat16Format()
    {
        return GL_RG16F;
    }

    unsigned Graphics::GetRGFloat32Format()
    {
        return GL_RG32F;
    }

    unsigned Graphics::GetFloat16Format()
    {
        return GL_R16F;
    }

    unsigned Graphics::GetFloat32Format()
    {
        return GL_R32F;
    }

    unsigned Graphics::GetLinearDepthFormat()
    {
        // OpenGL 3 can use different color attachment formats
        return GL_R32F;
    }

    unsigned Graphics::GetDepthStencilFormat()
    {
#if !ALIMER_OPENGLES
        return GL_DEPTH24_STENCIL8;
#else
        return glesDepthStencilFormat;
#endif
    }

    unsigned Graphics::GetReadableDepthFormat()
    {
        return GL_DEPTH_COMPONENT24;
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

    void Graphics::CheckFeatureSupport()
    {
        // Check supported features: light pre-pass, deferred rendering and hardware depth texture
        lightPrepassSupport_ = false;
        deferredSupport_ = false;

#if !ALIMER_OPENGLES
        int numSupportedRTs = 1;
        dxtTextureSupport_ = true;
        anisotropySupport_ = true;
        sRGBSupport_ = true;
        sRGBWriteSupport_ = true;

        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &numSupportedRTs);

        // Must support 2 rendertargets for light pre-pass, and 4 for deferred
        if (numSupportedRTs >= 2)
            lightPrepassSupport_ = true;
        if (numSupportedRTs >= 4)
            deferredSupport_ = true;

#if defined(__APPLE__) && !defined(IOS) && !defined(TVOS)
        // On macOS check for an Intel driver and use shadow map RGBA dummy color textures, because mixing
        // depth-only FBO rendering and backbuffer rendering will bug, resulting in a black screen in full
        // screen mode, and incomplete shadow maps in windowed mode
        String renderer((const char*)glGetString(GL_RENDERER));
        if (renderer.Contains("Intel", false))
            dummyColorFormat_ = GetRGBAFormat();
#endif
#else
        // Check for supported compressed texture formats
#ifdef __EMSCRIPTEN__
        dxtTextureSupport_ = CheckExtension("WEBGL_compressed_texture_s3tc"); // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_s3tc/
        etcTextureSupport_ = CheckExtension("WEBGL_compressed_texture_etc1"); // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_etc1/
        pvrtcTextureSupport_ = CheckExtension("WEBGL_compressed_texture_pvrtc"); // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_pvrtc/
        etc2TextureSupport_ = gl3Support || CheckExtension("WEBGL_compressed_texture_etc"); // https://www.khronos.org/registry/webgl/extensions/WEBGL_compressed_texture_etc/
#else
        dxtTextureSupport_ = CheckExtension("EXT_texture_compression_dxt1");
        etcTextureSupport_ = CheckExtension("OES_compressed_ETC1_RGB8_texture");
        etc2TextureSupport_ = gl3Support || CheckExtension("OES_compressed_ETC2_RGBA8_texture");
        pvrtcTextureSupport_ = CheckExtension("IMG_texture_compression_pvrtc");
#endif

        // Check for best supported depth renderbuffer format for GLES2
        if (CheckExtension("GL_OES_depth24"))
            glesDepthStencilFormat = GL_DEPTH_COMPONENT24;
        if (CheckExtension("GL_OES_packed_depth_stencil"))
            glesDepthStencilFormat = GL_DEPTH24_STENCIL8;
#ifdef __EMSCRIPTEN__
        if (!CheckExtension("WEBGL_depth_texture"))
#else
        if (!CheckExtension("GL_OES_depth_texture"))
#endif
        {
            shadowMapFormat_ = 0;
            hiresShadowMapFormat_ = 0;
        }
        else
        {
#if defined(IOS) || defined(TVOS)
            // iOS hack: depth renderbuffer seems to fail, so use depth textures for everything if supported
            glesDepthStencilFormat = GL_DEPTH_COMPONENT;
#endif
            shadowMapFormat_ = GL_DEPTH_COMPONENT;
            hiresShadowMapFormat_ = 0;
            // WebGL shadow map rendering seems to be extremely slow without an attached dummy color texture
#ifdef __EMSCRIPTEN__
            dummyColorFormat_ = GetRGBAFormat();
#endif
        }
#endif

        // Consider OpenGL shadows always hardware sampled, if supported at all
        hardwareShadowSupport_ = shadowMapFormat_ != 0;
    }

    void Graphics::PrepareDraw()
    {
        for (ConstantBuffer* buffer : impl_->dirtyConstantBuffers)
        {
            buffer->Apply();
        }

        impl_->dirtyConstantBuffers.clear();

        if (impl_->fboDirty_)
        {
            impl_->fboDirty_ = false;

            // First check if no framebuffer is needed. In that case simply return to backbuffer rendering
            bool noFbo = !depthStencil_;
            if (noFbo)
            {
                for (auto& renderTarget : renderTargets_)
                {
                    if (renderTarget)
                    {
                        noFbo = false;
                        break;
                    }
                }
            }

            if (noFbo)
            {
                if (impl_->boundFBO_ != impl_->systemFBO_)
                {
                    glBindFramebuffer(GL_FRAMEBUFFER, impl_->systemFBO_);
                    impl_->boundFBO_ = impl_->systemFBO_;
                }

#if !ALIMER_OPENGLES
                // Disable/enable sRGB write
                if (sRGBWriteSupport_)
                {
                    bool sRGBWrite = sRGB_;
                    if (sRGBWrite != impl_->sRGBWrite_)
                    {
                        if (sRGBWrite)
                            glEnable(GL_FRAMEBUFFER_SRGB);
                        else
                            glDisable(GL_FRAMEBUFFER_SRGB);
                        impl_->sRGBWrite_ = sRGBWrite;
                    }
                }
#endif

                return;
            }

            // Search for a new framebuffer based on format & size, or create new
            IntVector2 rtSize = Graphics::GetRenderTargetDimensions();
            unsigned format = 0;
            if (renderTargets_[0])
                format = renderTargets_[0]->GetParentTexture()->GetFormat();
            else if (depthStencil_)
                format = depthStencil_->GetParentTexture()->GetFormat();

            uint64_t fboKey = 0;
            HashCombine(fboKey, format);
            HashCombine(fboKey, rtSize.x);
            HashCombine(fboKey, rtSize.y);

            auto i = impl_->frameBuffers_.find(fboKey);
            if (i == impl_->frameBuffers_.end())
            {
                FrameBufferObject newFbo;
                glGenFramebuffers(1, &newFbo.fbo_);
                i = impl_->frameBuffers_.insert(std::make_pair(fboKey, newFbo)).first;
            }

            if (impl_->boundFBO_ != i->second.fbo_)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, i->second.fbo_);
                impl_->boundFBO_ = i->second.fbo_;
            }

            // Setup readbuffers & drawbuffers if needed
            if (i->second.readBuffers_ != GL_NONE)
            {
                glReadBuffer(GL_NONE);
                i->second.readBuffers_ = GL_NONE;
            }

            // Calculate the bit combination of non-zero color rendertargets to first check if the combination changed
            unsigned newDrawBuffers = 0;
            for (uint32_t j = 0; j < kMaxColorAttachments; ++j)
            {
                if (renderTargets_[j])
                    newDrawBuffers |= 1u << j;
            }

            if (newDrawBuffers != i->second.drawBuffers_)
            {
                // Check for no color rendertargets (depth rendering only)
                if (!newDrawBuffers)
                {
                    glDrawBuffer(GL_NONE);
                }
                else
                {
                    GLenum drawBufferIds[kMaxColorAttachments];
                    GLsizei drawBufferCount = 0;

                    for (uint32_t j = 0; j < kMaxColorAttachments; ++j)
                    {
                        if (renderTargets_[j])
                        {
                            drawBufferIds[drawBufferCount++] = GL_COLOR_ATTACHMENT0 + j;
                        }
                    }
                    glDrawBuffers(drawBufferCount, drawBufferIds);
                }

                i->second.drawBuffers_ = newDrawBuffers;
            }

            for (uint32_t j = 0; j < kMaxColorAttachments; ++j)
            {
                if (renderTargets_[j])
                {
                    Texture* texture = renderTargets_[j]->GetParentTexture();

                    // Bind either a renderbuffer or texture, depending on what is available
                    unsigned renderBufferID = renderTargets_[j]->GetRenderBuffer();
                    if (!renderBufferID)
                    {
                        // If texture's parameters are dirty, update before attaching
                        if (texture->GetParametersDirty())
                        {
                            SetTextureForUpdate(texture);
                            texture->UpdateParameters();
                            SetTexture(0, nullptr);
                        }

                        if (i->second.colorAttachments_[j] != renderTargets_[j])
                        {
                            impl_->BindColorAttachment(j, renderTargets_[j]->GetTarget(), texture->GetGPUObjectName(), false);
                            i->second.colorAttachments_[j] = renderTargets_[j];
                        }
                    }
                    else
                    {
                        if (i->second.colorAttachments_[j] != renderTargets_[j])
                        {
                            impl_->BindColorAttachment(j, renderTargets_[j]->GetTarget(), renderBufferID, true);
                            i->second.colorAttachments_[j] = renderTargets_[j];
                        }
                    }
                }
                else
                {
                    if (i->second.colorAttachments_[j])
                    {
                        impl_->BindColorAttachment(j, GL_TEXTURE_2D, 0, false);
                        i->second.colorAttachments_[j] = nullptr;
                    }
                }
            }

            if (depthStencil_)
            {
                // Bind either a renderbuffer or a depth texture, depending on what is available
                Texture* texture = depthStencil_->GetParentTexture();
                const bool hasStencil = texture->GetFormat() == GL_DEPTH24_STENCIL8;
                unsigned renderBufferID = depthStencil_->GetRenderBuffer();
                if (!renderBufferID)
                {
                    // If texture's parameters are dirty, update before attaching
                    if (texture->GetParametersDirty())
                    {
                        SetTextureForUpdate(texture);
                        texture->UpdateParameters();
                        SetTexture(0, nullptr);
                    }

                    if (i->second.depthAttachment_ != depthStencil_)
                    {
                        impl_->BindDepthAttachment(texture->GetGPUObjectName(), false);
                        impl_->BindStencilAttachment(hasStencil ? texture->GetGPUObjectName() : 0, false);
                        i->second.depthAttachment_ = depthStencil_;
                    }
                }
                else
                {
                    if (i->second.depthAttachment_ != depthStencil_)
                    {
                        impl_->BindDepthAttachment(renderBufferID, true);
                        impl_->BindStencilAttachment(hasStencil ? renderBufferID : 0, true);
                        i->second.depthAttachment_ = depthStencil_;
                    }
                }
            }
            else
            {
                if (i->second.depthAttachment_)
                {
                    impl_->BindDepthAttachment(0, false);
                    impl_->BindStencilAttachment(0, false);
                    i->second.depthAttachment_ = nullptr;
                }
            }

#if !ALIMER_OPENGLES
            // Disable/enable sRGB write
            if (sRGBWriteSupport_)
            {
                bool sRGBWrite = renderTargets_[0] ? renderTargets_[0]->GetParentTexture()->GetSRGB() : sRGB_;
                if (sRGBWrite != impl_->sRGBWrite_)
                {
                    if (sRGBWrite)
                        glEnable(GL_FRAMEBUFFER_SRGB);
                    else
                        glDisable(GL_FRAMEBUFFER_SRGB);
                    impl_->sRGBWrite_ = sRGBWrite;
                }
            }
#endif
        }

        if (impl_->vertexBuffersDirty_)
        {
            // Go through currently bound vertex buffers and set the attribute pointers that are available & required
            // Use reverse order so that elements from higher index buffers will override lower index buffers
            unsigned assignedLocations = 0;

            for (uint32_t i = kMaxVertexBufferBindings - 1; i < kMaxVertexBufferBindings; --i)
            {
                VertexBuffer* buffer = vertexBuffers_[i];
                // Beware buffers with missing OpenGL objects, as binding a zero buffer object means accessing CPU memory for vertex data,
                // in which case the pointer will be invalid and cause a crash
                if (!buffer || !buffer->GetGPUObjectName() || !impl_->vertexAttributes_)
                    continue;

                const PODVector<VertexElement>& elements = buffer->GetElements();

                for (PODVector<VertexElement>::ConstIterator j = elements.Begin(); j != elements.End(); ++j)
                {
                    const VertexElement& element = *j;
                    HashMap<Pair<unsigned char, unsigned char>, unsigned>::ConstIterator k =
                        impl_->vertexAttributes_->Find(MakePair((unsigned char)element.semantic_, element.index_));

                    if (k != impl_->vertexAttributes_->End())
                    {
                        unsigned location = k->second_;
                        unsigned locationMask = 1u << location;
                        if (assignedLocations & locationMask)
                            continue; // Already assigned by higher index vertex buffer
                        assignedLocations |= locationMask;

                        // Enable attribute if not enabled yet
                        if (!(impl_->enabledVertexAttributes_ & locationMask))
                        {
                            glEnableVertexAttribArray(location);
                            impl_->enabledVertexAttributes_ |= locationMask;
                        }

                        // Enable/disable instancing divisor as necessary
                        unsigned dataStart = element.offset_;
                        if (element.perInstance_)
                        {
                            dataStart += impl_->lastInstanceOffset_ * buffer->GetVertexSize();
                            if (!(impl_->instancingVertexAttributes_ & locationMask))
                            {
                                glVertexAttribDivisor(location, 1);
                                impl_->instancingVertexAttributes_ |= locationMask;
                            }
                        }
                        else
                        {
                            if (impl_->instancingVertexAttributes_ & locationMask)
                            {
                                glVertexAttribDivisor(location, 0);
                                impl_->instancingVertexAttributes_ &= ~locationMask;
                            }
                        }

                        SetVBO(buffer->GetGPUObjectName());
                        glVertexAttribPointer(location, glElementComponents[element.type_], glElementTypes[element.type_],
                            element.type_ == TYPE_UBYTE4_NORM ? GL_TRUE : GL_FALSE, (unsigned)buffer->GetVertexSize(),
                            (const void*)(size_t)dataStart);
                    }
                }
            }

            // Finally disable unnecessary vertex attributes
            unsigned disableVertexAttributes = impl_->enabledVertexAttributes_ & (~impl_->usedVertexAttributes_);
            unsigned location = 0;
            while (disableVertexAttributes)
            {
                if (disableVertexAttributes & 1u)
                {
                    glDisableVertexAttribArray(location);
                    impl_->enabledVertexAttributes_ &= ~(1u << location);
                }
                ++location;
                disableVertexAttributes >>= 1;
            }

            impl_->vertexBuffersDirty_ = false;
        }
    }

    void GraphicsImpl::CleanupFramebuffers(bool deviceLost)
    {
        if (!deviceLost)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, systemFBO_);
            boundFBO_ = systemFBO_;
            fboDirty_ = true;

            for (auto i = frameBuffers_.begin(); i != frameBuffers_.end(); ++i)
                glDeleteFramebuffers(1, &i->second.fbo_);

            if (resolveSrcFBO_)
                glDeleteFramebuffers(1, &resolveSrcFBO_);
            if (resolveDestFBO_)
                glDeleteFramebuffers(1, &resolveDestFBO_);
        }
        else
        {
            boundFBO_ = 0;
        }

        resolveSrcFBO_ = 0;
        resolveDestFBO_ = 0;

        frameBuffers_.clear();
    }

    void Graphics::ResetCachedState()
    {
        for (auto& vertexBuffer : vertexBuffers_)
            vertexBuffer = nullptr;

        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            textures_[i] = nullptr;
            impl_->textureTypes_[i] = 0;
        }

        for (auto& renderTarget : renderTargets_)
            renderTarget = nullptr;

        depthStencil_ = nullptr;
        viewport_ = IntRect(0, 0, 0, 0);
        indexBuffer_ = nullptr;
        vertexShader_ = nullptr;
        pixelShader_ = nullptr;
        blendMode_ = BLEND_REPLACE;
        alphaToCoverage_ = false;
        colorWrite_ = true;
        cullMode_ = CullMode::None;
        constantDepthBias_ = 0.0f;
        slopeScaledDepthBias_ = 0.0f;
        depthTestMode_ = CMP_ALWAYS;
        depthWrite_ = false;
        lineAntiAlias_ = false;
        fillMode_ = FillMode::Solid;
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
        impl_->lastInstanceOffset_ = 0;
        impl_->activeTexture_ = 0;
        impl_->enabledVertexAttributes_ = 0;
        impl_->usedVertexAttributes_ = 0;
        impl_->instancingVertexAttributes_ = 0;
        impl_->boundFBO_ = impl_->systemFBO_;
        impl_->boundVBO_ = 0;
        impl_->boundUBO_ = 0;
        impl_->sRGBWrite_ = false;

        // Set initial state to match Direct3D
        if (impl_->context_)
        {
            glEnable(GL_DEPTH_TEST);
            SetCullMode(CullMode::CounterClockwise);
            SetDepthTest(CMP_LESSEQUAL);
            SetDepthWrite(true);
        }

        for (auto& constantBuffer : impl_->constantBuffers_)
            constantBuffer = nullptr;
        impl_->dirtyConstantBuffers.clear();
    }

    void Graphics::SetTextureUnitMappings()
    {
        textureUnits_["DiffMap"] = TU_DIFFUSE;
        textureUnits_["DiffCubeMap"] = TU_DIFFUSE;
        textureUnits_["AlbedoBuffer"] = TU_ALBEDOBUFFER;
        textureUnits_["NormalMap"] = TU_NORMAL;
        textureUnits_["NormalBuffer"] = TU_NORMALBUFFER;
        textureUnits_["SpecMap"] = TU_SPECULAR;
        textureUnits_["EmissiveMap"] = TU_EMISSIVE;
        textureUnits_["EnvMap"] = TU_ENVIRONMENT;
        textureUnits_["EnvCubeMap"] = TU_ENVIRONMENT;
        textureUnits_["LightRampMap"] = TU_LIGHTRAMP;
        textureUnits_["LightSpotMap"] = TU_LIGHTSHAPE;
        textureUnits_["LightCubeMap"] = TU_LIGHTSHAPE;
        textureUnits_["ShadowMap"] = TU_SHADOWMAP;
        textureUnits_["VolumeMap"] = TU_VOLUMEMAP;
        textureUnits_["FaceSelectCubeMap"] = TU_FACESELECT;
        textureUnits_["IndirectionCubeMap"] = TU_INDIRECTION;
        textureUnits_["DepthBuffer"] = TU_DEPTHBUFFER;
        textureUnits_["LightBuffer"] = TU_LIGHTBUFFER;
        textureUnits_["ZoneCubeMap"] = TU_ZONE;
        textureUnits_["ZoneVolumeMap"] = TU_ZONE;
    }

    void GraphicsImpl::BindColorAttachment(uint32_t index, GLenum target, GLuint object, bool isRenderBuffer)
    {
        if (!object)
            isRenderBuffer = false;

        if (!isRenderBuffer)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, target, object, 0);
        else
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_RENDERBUFFER, object);
    }

    void GraphicsImpl::BindDepthAttachment(GLuint object, bool isRenderBuffer)
    {
        if (!object)
            isRenderBuffer = false;

        if (!isRenderBuffer)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, object, 0);
        else
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, object);
    }

    void GraphicsImpl::BindStencilAttachment(GLuint object, bool isRenderBuffer)
    {
        if (!object)
            isRenderBuffer = false;

        if (!isRenderBuffer)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, object, 0);
        else
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, object);
    }
    }
