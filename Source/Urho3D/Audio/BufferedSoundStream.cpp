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

#include "../Precompiled.h"

#include "../Audio/BufferedSoundStream.h"

#include "../DebugNew.h"

namespace Urho3D
{
    uint32_t BufferedSoundStream::GetData(int8_t* dest, uint32_t numBytes)
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);

        uint32_t outBytes = 0;

        while (numBytes && buffers_.size())
        {
            // Copy as much from the front buffer as possible, then discard it and move to the next
            auto front = buffers_.begin();

            uint32_t copySize = front->second - position_;
            if (copySize > numBytes)
                copySize = numBytes;

            memcpy(dest, front->first.Get() + position_, copySize);
            position_ += copySize;
            if (position_ >= front->second)
            {
                buffers_.pop_front();
                position_ = 0;
            }

            dest += copySize;
            outBytes += copySize;
            numBytes -= copySize;
        }

        return outBytes;
    }

    void BufferedSoundStream::AddData(void* data, uint32_t numBytes)
    {
        if (data && numBytes)
        {
            std::lock_guard<std::mutex> lock(bufferMutex_);

            SharedArrayPtr<int8_t> newBuffer(new int8_t[numBytes]);
            memcpy(newBuffer.Get(), data, numBytes);
            buffers_.push_back(std::make_pair(newBuffer, numBytes));
        }
    }

    void BufferedSoundStream::AddData(const SharedArrayPtr<int8_t>& data, uint32_t numBytes)
    {
        if (data && numBytes)
        {
            std::lock_guard<std::mutex> lock(bufferMutex_);

            buffers_.push_back(std::make_pair(data, numBytes));
        }
    }

    void BufferedSoundStream::AddData(const SharedArrayPtr<int16_t>& data, uint32_t numBytes)
    {
        if (data && numBytes)
        {
            std::lock_guard<std::mutex> lock(bufferMutex_);

            buffers_.push_back(std::make_pair(ReinterpretCast<int8_t>(data), numBytes));
        }
    }

    void BufferedSoundStream::Clear()
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);

        buffers_.clear();
        position_ = 0;
    }

    uint32_t BufferedSoundStream::GetBufferNumBytes() const
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);

        uint32_t ret = 0;
        for (auto i = buffers_.begin(); i != buffers_.end(); ++i)
        {
            ret += i->second;
        }
        // Subtract amount of sound data played from the front buffer
        ret -= position_;

        return ret;
    }

    float BufferedSoundStream::GetBufferLength() const
    {
        return (float)GetBufferNumBytes() / (GetFrequency() * (float)GetSampleSize());
    }

}
