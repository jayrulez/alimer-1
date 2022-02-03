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

#include "../Audio/SoundStream.h"
#include "../Container/ArrayPtr.h"
#include "../Container/Pair.h"
#include <list>
#include <mutex>

namespace Urho3D
{

    /// %Sound stream that supports manual buffering of data from the main thread.
    class URHO3D_API BufferedSoundStream : public SoundStream
    {
    public:
        /// Construct.
        BufferedSoundStream() = default;

        /// Produce sound data into destination. Return number of bytes produced. Called by SoundSource from the mixing thread.
        uint32_t GetData(int8_t* dest, uint32_t numBytes) override;

        /// Buffer sound data. Makes a copy of it.
        void AddData(void* data, uint32_t numBytes);
        /// Buffer sound data by taking ownership of it.
        void AddData(const SharedArrayPtr<int8_t>& data, uint32_t numBytes);
        /// Buffer sound data by taking ownership of it.
        void AddData(const SharedArrayPtr<int16_t>& data, uint32_t numBytes);
        /// Remove all buffered audio data.
        void Clear();

        /// Return amount of buffered (unplayed) sound data in bytes.
        uint32_t GetBufferNumBytes() const;
        /// Return length of buffered (unplayed) sound data in seconds.
        float GetBufferLength() const;

    private:
        /// Buffers and their sizes.
        std::list<std::pair<SharedArrayPtr<int8_t>, uint32_t> > buffers_;
        /// Byte position in the front most buffer.
        uint32_t position_{ 0u };
        /// Mutex for buffer data.
        mutable std::mutex bufferMutex_;
    };
}
