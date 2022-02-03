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

#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/ConstantBuffer.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

using namespace Urho3D;

void ConstantBuffer::OnDeviceReset()
{
    // No-op on Direct3D11
}

void ConstantBuffer::Release()
{
    URHO3D_SAFE_RELEASE(object_.ptr_);

    shadowData.reset();
    size = 0;
}

bool ConstantBuffer::SetSize(uint32_t size_)
{
    Release();

    if (!size_)
    {
        URHO3D_LOGERROR("Can not create zero-sized constant buffer");
        return false;
    }

    // Round up to next 16 bytes
    size_ += 15;
    size_ &= 0xfffffff0;

    size = size_;
    dirty = false;
    shadowData.reset(new uint8_t[size]);
    memset(shadowData.get(), 0, size);

    if (graphics_)
    {
        D3D11_BUFFER_DESC bufferDesc = {};

        bufferDesc.ByteWidth = size;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = 0;

        HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateBuffer(&bufferDesc, 0, (ID3D11Buffer**)&object_.ptr_);
        if (FAILED(hr))
        {
            URHO3D_SAFE_RELEASE(object_.ptr_);
            URHO3D_LOGD3DERROR("Failed to create constant buffer", hr);
            return false;
        }
    }

    return true;
}

void ConstantBuffer::Apply()
{
    if (dirty && object_.ptr_)
    {
        graphics_->GetImpl()->GetDeviceContext()->UpdateSubresource((ID3D11Buffer*)object_.ptr_, 0, 0, shadowData.get(), 0, 0);
        dirty = false;
    }
}
