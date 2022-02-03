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

#include "../Container/ArrayPtr.h"
#include "../Core/Object.h"
#include "../Graphics/GPUObject.h"
#include "../Graphics/GraphicsDefs.h"

namespace Urho3D
{
    /// GPU constant buffer.
    class URHO3D_API ConstantBuffer : public Object, public GPUObject
    {
        URHO3D_OBJECT(ConstantBuffer, Object);

    public:
        /// Construct.
        explicit ConstantBuffer(Context* context);
        /// Destruct.
        ~ConstantBuffer() override;

        /// Recreate the GPU resource and restore data if applicable.
        void OnDeviceReset() override;
        /// Release the buffer.
        void Release() override;

        /// Set size and create GPU-side buffer. Return true on success.
        bool SetSize(uint32_t size);
        /// Set a generic parameter and mark buffer dirty.
        void SetParameter(uint32_t offset, uint32_t size, const void* data);
        /// Set a Vector3 array parameter and mark buffer dirty.
        void SetVector3ArrayParameter(uint32_t offset, uint32_t rows, const void* data);
        /// Apply to GPU.
        void Apply();

        /// Return size.
        constexpr uint32_t GetSize() const { return size; }

        /// Return whether has unapplied data.
        constexpr bool IsDirty() const { return dirty; }

    private:
        /// Shadow data.
        std::unique_ptr<uint8_t[]> shadowData;
        /// Buffer byte size.
        uint32_t size{0u};
        /// Dirty flag.
        bool dirty{false};
    };
}
