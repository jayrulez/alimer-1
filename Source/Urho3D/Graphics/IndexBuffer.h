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

#include "../Core/Object.h"
#include "../Container/ArrayPtr.h"
#include "../Graphics/GPUObject.h"
#include "../Graphics/GraphicsDefs.h"

namespace Urho3D
{
    /// Hardware index buffer.
    class URHO3D_API IndexBuffer : public Object, public GPUObject
    {
        URHO3D_OBJECT(IndexBuffer, Object);

    public:
        /// Construct. Optionally force headless (no GPU-side buffer) operation.
        explicit IndexBuffer(Context* context, bool forceHeadless = false);
        /// Destruct.
        ~IndexBuffer() override;

        /// Mark the buffer destroyed on graphics context destruction. May be a no-op depending on the API.
        void OnDeviceLost() override;
        /// Recreate the buffer and restore data if applicable. May be a no-op depending on the API.
        void OnDeviceReset() override;
        /// Release buffer.
        void Release() override;

        /// Enable shadowing in CPU memory. Shadowing is forced on if the graphics subsystem does not exist.
        /// @property
        void SetShadowed(bool enable);
        /// Set size and vertex elements and dynamic mode. Previous data will be lost.
        bool SetSize(unsigned indexCount, bool largeIndices, bool dynamic = false);
        /// Set all data in the buffer.
        bool SetData(const void* data);
        /// Set a data range in the buffer. Optionally discard data outside the range.
        bool SetDataRange(const void* data, unsigned start, unsigned count, bool discard = false);
        /// Lock the buffer for write-only editing. Return data pointer if successful. Optionally discard data outside the range.
        void* Lock(unsigned start, unsigned count, bool discard = false);
        /// Unlock the buffer and apply changes to the GPU buffer.
        void Unlock();

        /// Return whether CPU memory shadowing is enabled.
        /// @property
        bool IsShadowed() const { return shadowed_; }

        /// Return whether is dynamic.
        /// @property
        bool IsDynamic() const { return dynamic_; }

        /// Return whether is currently locked.
        bool IsLocked() const { return lockState_ != LOCK_NONE; }

        /// Return number of indices.
        /// @property
        uint32_t GetIndexCount() const { return indexCount_; }

        /// Return index size in bytes.
        /// @property
        uint32_t GetIndexSize() const { return indexSize_; }

        /// Return used vertex range from index range.
        bool GetUsedVertexRange(unsigned start, unsigned count, unsigned& minVertex, unsigned& vertexCount);

        /// Return CPU memory shadow data.
        uint8_t* GetShadowData() const { return shadowData_.Get(); }

        /// Return shared array pointer to the CPU memory shadow data.
        SharedArrayPtr<uint8_t> GetShadowDataShared() const { return shadowData_; }

    private:
        /// Create buffer.
        bool Create();
        /// Update the shadow data to the GPU buffer.
        bool UpdateToGPU();
#if !defined(URHO3D_OPENGL)
        /// Map the GPU buffer into CPU memory. Not used on OpenGL.
        void* MapBuffer(unsigned start, unsigned count, bool discard);
        /// Unmap the GPU buffer. Not used on OpenGL.
        void UnmapBuffer();
#endif

        /// Shadow data.
        SharedArrayPtr<uint8_t> shadowData_;
        /// Number of indices.
        uint32_t indexCount_{ 0u };
        /// Index size.
        uint32_t indexSize_{ 0u };
        /// Buffer locking state.
        LockState lockState_{ LOCK_NONE };
        /// Lock start vertex.
        unsigned lockStart_{ 0u };
        /// Lock number of vertices.
        unsigned lockCount_{ 0u };
        /// Scratch buffer for fallback locking.
        void* lockScratchData_{ nullptr };
        /// Dynamic flag.
        bool dynamic_{ false };
        /// Shadowed flag.
        bool shadowed_{ false };
#if defined(URHO3D_OPENGL)
        /// Discard lock flag. Used by OpenGL only.
        bool discardLock_{ false };
#endif
    };
}
