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

#include "../Audio/AudioDefs.h"
#include "../Container/ArrayPtr.h"
#include "../Core/Object.h"
#include <mutex>
#include <unordered_set>

namespace Urho3D
{
    class AudioImpl;
    class Sound;
    class SoundListener;
    class SoundSource;

    /// %Audio subsystem.
    class URHO3D_API Audio : public Object
    {
        URHO3D_OBJECT(Audio, Object);

    public:
        /// Construct.
        explicit Audio(Context* context);
        /// Destruct. Terminate the audio thread and free the audio buffer.
        ~Audio() override;

        /// Initialize sound output with specified buffer length and output mode.
        bool SetMode(int bufferLengthMSec, int mixRate, bool stereo, bool interpolation = true);
        /// Run update on sound sources. Not required for continued playback, but frees unused sound sources & sounds and updates 3D positions.
        void Update(float timeStep);
        /// Restart sound output.
        bool Play();
        /// Suspend sound output.
        void Stop();
        /// Set master gain on a specific sound type such as sound effects, music or voice.
        /// @property
        void SetMasterGain(const String& type, float gain);
        /// Pause playback of specific sound type. This allows to suspend e.g. sound effects or voice when the game is paused. By default all sound types are unpaused.
        void PauseSoundType(const String& type);
        /// Resume playback of specific sound type.
        void ResumeSoundType(const String& type);
        /// Resume playback of all sound types.
        void ResumeAll();
        /// Set active sound listener for 3D sounds.
        /// @property
        void SetListener(SoundListener* listener);
        /// Stop any sound source playing a certain sound clip.
        void StopSound(Sound* sound);

        /// Return byte size of one sample.
        /// @property
        uint32_t GetSampleSize() const { return sampleSize_; }

        /// Return mixing rate.
        /// @property
        int32_t GetMixRate() const { return mixRate_; }

        /// Return whether output is interpolated.
        /// @property
        bool GetInterpolation() const { return interpolation_; }

        /// Return whether output is stereo.
        /// @property
        bool IsStereo() const { return stereo_; }

        /// Return whether audio is being output.
        /// @property
        bool IsPlaying() const { return playing_; }

        /// Return whether an audio stream has been reserved.
        /// @property
        bool IsInitialized() const { return deviceID_ != 0; }

        /// Return master gain for a specific sound source type. Unknown sound types will return full gain (1).
        /// @property
        float GetMasterGain(const String& type) const;

        /// Return whether specific sound type has been paused.
        bool IsSoundTypePaused(const String& type) const;

        /// Return active sound listener.
        /// @property
        SoundListener* GetListener() const;

        /// Return all sound sources.
        const std::vector<SoundSource*>& GetSoundSources() const { return soundSources; }

        /// Return whether the specified master gain has been defined.
        bool HasMasterGain(const String& type) const { return masterGain.find(type) != masterGain.end(); }

        /// Add a sound source to keep track of. Called by SoundSource.
        void AddSoundSource(SoundSource* soundSource);
        /// Remove a sound source. Called by SoundSource.
        void RemoveSoundSource(SoundSource* soundSource);

        /// Return audio thread mutex.
        [[nodiscard]] std::mutex& GetMutex() { return audioMutex; }

        /// Return sound type specific gain multiplied by master gain.
        float GetSoundSourceMasterGain(StringHash typeHash) const;

        /// Mix sound sources into the buffer.
        void MixOutput(void* dest, uint32_t samples);

    private:
        /// Handle render update event.
        void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
        /// Stop sound output and release the sound buffer.
        void Release();
        /// Actually update sound sources with the specific timestep. Called internally.
        void UpdateInternal(float timeStep);

        /// Clipping buffer for mixing.
        std::unique_ptr<int32_t[]> clipBuffer_;
        /// Audio thread mutex.
        std::mutex audioMutex;
        /// SDL audio device ID.
        uint32_t deviceID_{};
        /// Sample size.
        uint32_t sampleSize_{};
        /// Clip buffer size in samples.
        uint32_t fragmentSize_{};
        /// Mixing rate.
        int32_t mixRate_{};
        /// Mixing interpolation flag.
        bool interpolation_{};
        /// Stereo flag.
        bool stereo_{};
        /// Playing flag.
        bool playing_{};
        /// Master gain by sound source type.
        std::unordered_map<StringHash, Variant> masterGain;
        /// Paused sound types.
        std::unordered_set<StringHash> pausedSoundTypes;
        /// Sound sources.
        std::vector<SoundSource*> soundSources;
        /// Sound listener.
        WeakPtr<SoundListener> listener_;
    };

    /// Register Audio library objects.
    /// @nobind
    void URHO3D_API RegisterAudioLibrary(Context* context);
}
