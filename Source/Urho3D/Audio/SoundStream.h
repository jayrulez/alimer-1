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

#include "../Container/RefCounted.h"

namespace Urho3D
{

    /// Base class for sound streams.
    class URHO3D_API SoundStream : public RefCounted
    {
    public:
        /// Destruct.
        ~SoundStream() override = default;

        /// Seek to sample number. Return true on success. Need not be implemented by all streams.
        virtual bool Seek(uint32_t sample_number) { return false; }

        /// Produce sound data into destination. Return number of bytes produced. Called by SoundSource from the mixing thread.
        virtual uint32_t GetData(int8_t* dest, uint32_t numBytes) = 0;

        /// Set sound data format.
        void SetFormat(uint32_t frequency, bool sixteenBit, bool stereo);
        /// Set whether playback should stop when no more data. Default false.
        void SetStopAtEnd(bool enable);

        /// Return sample size.
        uint32_t GetSampleSize() const;

        /// Return default frequency as a float.
        float GetFrequency() const { return (float)frequency_; }

        /// Return default frequency as an integer.
        uint32_t GetIntFrequency() const { return frequency_; }

        /// Return whether playback should stop when no more data.
        bool GetStopAtEnd() const { return stopAtEnd_; }

        /// Return whether data is sixteen bit.
        bool IsSixteenBit() const { return sixteenBit_; }

        /// Return whether data is stereo.
        bool IsStereo() const { return stereo_; }

    protected:
        /// Constructor.
        SoundStream();

        /// Default frequency.
        uint32_t frequency_{ 44100u };
        /// Stop when no more data flag.
        bool stopAtEnd_{ false };
        /// Sixteen bit flag.
        bool sixteenBit_{ false };
        /// Stereo flag.
        bool stereo_{ false };
    };

}
