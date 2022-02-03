//
// Copyright (c) 2008-2022 the Urho3D project.
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

#include "../../Container/HashMap.h"
#include "../../Core/Timer.h"
#include "../../Graphics/ConstantBuffer.h"
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/Texture2D.h"
#include "../../Math/Color.h"

#if defined(IOS) || defined(TVOS)
#   include <OpenGLES/ES3/gl.h>
#   include <OpenGLES/ES3/glext.h>
#   define ALIMER_OPENGLES 1
#elif defined(__ANDROID__) || defined (__arm__) || defined(__aarch64__) || defined (__EMSCRIPTEN__)
#   include <GLES3/gl3.h>
#   include <GLES3/gl3ext.h>
#   define ALIMER_OPENGLES 1
#else
#   include <glad/glad.h>
#endif

#ifndef ALIMER_OPENGLES
#   define ALIMER_OPENGLES 0
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83f1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83f2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83f3
#endif
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8d64
#endif
#ifndef GL_ETC2_RGB8_OES
#define GL_ETC2_RGB8_OES 0x9274
#endif
#ifndef GL_ETC2_RGBA8_OES
#define GL_ETC2_RGBA8_OES 0x9278
#endif
#ifndef COMPRESSED_RGB_PVRTC_4BPPV1_IMG
#define COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8c00
#endif
#ifndef COMPRESSED_RGB_PVRTC_2BPPV1_IMG
#define COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8c01
#endif
#ifndef COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
#define COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8c02
#endif
#ifndef COMPRESSED_RGBA_PVRTC_2BPPV1_IMG
#define COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8c03
#endif

using SDL_GLContext = void*;

namespace Urho3D
{

    class Context;

    using ConstantBufferMap = HashMap<unsigned, SharedPtr<ConstantBuffer> >;
    using ShaderProgramMap = HashMap<Pair<ShaderVariation*, ShaderVariation*>, SharedPtr<ShaderProgram> >;

    /// Cached state of a frame buffer object.
    struct FrameBufferObject
    {
        /// Frame buffer handle.
        GLuint fbo_{};
        /// Bound color attachment textures.
        RenderSurface* colorAttachments_[kMaxColorAttachments]{};
        /// Bound depth/stencil attachment.
        RenderSurface* depthAttachment_{};
        /// Read buffer bits.
        unsigned readBuffers_{ M_MAX_UNSIGNED };
        /// Draw buffer bits.
        unsigned drawBuffers_{ M_MAX_UNSIGNED };
    };

    /// %Graphics subsystem implementation. Holds API-specific objects.
    class URHO3D_API GraphicsImpl
    {
        friend class Graphics;

    public:
        /// Construct.
        GraphicsImpl() = default;

        /// Clean up all framebuffers. Called when destroying the context. Used only on OpenGL.
        void CleanupFramebuffers(bool deviceLost);

        /// Bind a framebuffer color attachment using either extension or core functionality. Used only on OpenGL.
        void BindColorAttachment(uint32_t index, GLenum target, GLuint object, bool isRenderBuffer);
        /// Bind a framebuffer depth attachment using either extension or core functionality. Used only on OpenGL.
        void BindDepthAttachment(GLuint object, bool isRenderBuffer);
        /// Bind a framebuffer stencil attachment using either extension or core functionality. Used only on OpenGL.
        void BindStencilAttachment(GLuint object, bool isRenderBuffer);

        /// Return the GL Context.
        const SDL_GLContext& GetGLContext() { return context_; }

    private:
        /// SDL OpenGL context.
        SDL_GLContext context_{};
        /// iOS/tvOS system framebuffer handle.
        GLuint systemFBO_{};
        /// Active texture unit.
        GLuint activeTexture_{};
        /// Enabled vertex attributes bitmask.
        GLuint enabledVertexAttributes_{};
        /// Vertex attributes bitmask used by the current shader program.
        GLuint usedVertexAttributes_{};
        /// Vertex attribute instancing bitmask for keeping track of divisors.
        GLuint instancingVertexAttributes_{};
        /// Current mapping of vertex attribute locations by semantic. The map is owned by the shader program, so care must be taken to switch a null shader program when it's destroyed.
        const HashMap<Pair<unsigned char, unsigned char>, unsigned>* vertexAttributes_{};
        /// Currently bound frame buffer object.
        GLuint boundFBO_{};
        /// Currently bound vertex buffer object.
        GLuint boundVBO_{};
        /// Currently bound uniform buffer object.
        GLuint boundUBO_{};
        /// Read frame buffer for multisampled texture resolves.
        GLuint resolveSrcFBO_{};
        /// Write frame buffer for multisampled texture resolves.
        GLuint resolveDestFBO_{};
        /// Current pixel format.
        int pixelFormat_{};
        /// Map for FBO's per resolution and format.
        std::unordered_map<uint64_t, FrameBufferObject> frameBuffers_;
        /// OpenGL texture types in use.
        GLenum textureTypes_[MAX_TEXTURE_UNITS]{};
        /// Constant buffer search map.
        ConstantBufferMap allConstantBuffers_;
        /// Currently bound constant buffers.
        ConstantBuffer* constantBuffers_[MAX_SHADER_PARAMETER_GROUPS * 2]{};
        /// Dirty constant buffers.
        std::vector<ConstantBuffer*> dirtyConstantBuffers;
        /// Last used instance data offset.
        uint32_t lastInstanceOffset_{};
        /// Map for additional depth textures, to emulate Direct3D9 ability to mix render texture and backbuffer rendering.
        std::unordered_map<uint32_t, SharedPtr<Texture2D> > depthTextures_;
        /// Shader program in use.
        ShaderProgram* shaderProgram_{};
        /// Linked shader programs.
        ShaderProgramMap shaderPrograms_;
        /// Need FBO commit flag.
        bool fboDirty_{};
        /// Need vertex attribute pointer update flag.
        bool vertexBuffersDirty_{};
        /// sRGB write mode flag.
        bool sRGBWrite_{};
    };
}
