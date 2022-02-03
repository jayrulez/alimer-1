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

#include "../Precompiled.h"

#include "../Graphics/Graphics.h"
#include "../Graphics/ConstantBuffer.h"
#include "../IO/Log.h"

#include "../DebugNew.h"

using namespace Urho3D;

ConstantBuffer::ConstantBuffer(Context* context) :
    Object(context),
    GPUObject(GetSubsystem<Graphics>())
{
}

ConstantBuffer::~ConstantBuffer()
{
    Release();
}

void ConstantBuffer::SetParameter(uint32_t offset, uint32_t size_, const void* data)
{
    if (offset + size_ > size)
        return; // Would overflow the buffer

    memcpy(&shadowData[offset], data, size_);
    dirty = true;
}

void ConstantBuffer::SetVector3ArrayParameter(uint32_t offset, uint32_t rows, const void* data)
{
    if (offset + rows * 4 * sizeof(float) > size)
        return; // Would overflow the buffer

    float* dest = (float*)&shadowData[offset];
    const auto* src = (const float*)data;

    while (rows--)
    {
        *dest++ = *src++;
        *dest++ = *src++;
        *dest++ = *src++;
        ++dest; // Skip over the w coordinate
    }

    dirty = true;
}
