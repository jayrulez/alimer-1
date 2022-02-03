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

#include "../Audio/Audio.h"
#include "../Audio/Sound.h"
#include "../Audio/SoundListener.h"
#include "../Audio/SoundSource3D.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../IO/Log.h"

#include <SDL/SDL.h>

#include "../DebugNew.h"

#ifdef _MSC_VER
#   pragma warning(disable:6293)
#endif

namespace Urho3D
{
    const char* AUDIO_CATEGORY = "Audio";

    static const int MIN_BUFFERLENGTH = 20;
    static const int MIN_MIXRATE = 11025;
    static const int MAX_MIXRATE = 48000;
    static const StringHash SOUND_MASTER_HASH("Master");

    static void SDLAudioCallback(void* userdata, Uint8* stream, int len);

    Audio::Audio(Context* context) :
        Object(context)
    {
        context_->RequireSDL(SDL_INIT_AUDIO);

        // Set the master to the default value
        masterGain[SOUND_MASTER_HASH] = 1.0f;

        // Register Audio library object factories
        RegisterAudioLibrary(context_);

        SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(Audio, HandleRenderUpdate));
    }

    Audio::~Audio()
    {
        Release();
        context_->ReleaseSDL();
    }

    bool Audio::SetMode(int bufferLengthMSec, int mixRate, bool stereo, bool interpolation)
    {
        Release();

        bufferLengthMSec = Max(bufferLengthMSec, MIN_BUFFERLENGTH);
        mixRate = Clamp(mixRate, MIN_MIXRATE, MAX_MIXRATE);

        SDL_AudioSpec desired;
        SDL_AudioSpec obtained;

        desired.freq = mixRate;

        desired.format = AUDIO_S16;
        desired.callback = SDLAudioCallback;
        desired.userdata = this;

        // SDL uses power of two audio fragments. Determine the closest match
        int bufferSamples = mixRate * bufferLengthMSec / 1000;
        desired.samples = (Uint16)NextPowerOfTwo((unsigned)bufferSamples);
        if (Abs((int)desired.samples / 2 - bufferSamples) < Abs((int)desired.samples - bufferSamples))
            desired.samples /= 2;

        // Intentionally disallow format change so that the obtained format will always be the desired format, even though that format
        // is not matching the device format, however in doing it will enable the SDL's internal audio stream with audio conversion.
        // Also disallow channels change to avoid issues on multichannel audio device (5.1, 7.1, etc)
        int allowedChanges = SDL_AUDIO_ALLOW_ANY_CHANGE & ~SDL_AUDIO_ALLOW_FORMAT_CHANGE & ~SDL_AUDIO_ALLOW_CHANNELS_CHANGE;

        if (stereo)
        {
            desired.channels = 2;
            deviceID_ = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, &obtained, allowedChanges);
        }

        // If stereo requested but not available then fall back into mono
        if (!stereo || !deviceID_)
        {
            desired.channels = 1;
            deviceID_ = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, &obtained, allowedChanges);

            if (!deviceID_)
            {
                URHO3D_LOGERROR("Could not initialize audio output");
                return false;
            }
        }

        if (obtained.format != AUDIO_S16)
        {
            URHO3D_LOGERROR("Could not initialize audio output, 16-bit buffer format not supported");
            SDL_CloseAudioDevice(deviceID_);
            deviceID_ = 0;
            return false;
        }

        stereo_ = obtained.channels == 2;
        sampleSize_ = (unsigned)(stereo_ ? sizeof(int) : sizeof(short));
        // Guarantee a fragment size that is low enough so that Vorbis decoding buffers do not wrap
        fragmentSize_ = Min(NextPowerOfTwo((unsigned)mixRate >> 6u), (unsigned)obtained.samples);
        mixRate_ = obtained.freq;
        interpolation_ = interpolation;
        clipBuffer_ = new int[stereo ? fragmentSize_ << 1u : fragmentSize_];

        URHO3D_LOGINFO("Set audio mode " + String(mixRate_) + " Hz " + (stereo_ ? "stereo" : "mono") + (interpolation_ ? " interpolated" : ""));

        return Play();
    }

    void Audio::Update(float timeStep)
    {
        if (!playing_)
            return;

        UpdateInternal(timeStep);
    }

    bool Audio::Play()
    {
        if (playing_)
            return true;

        if (!deviceID_)
        {
            URHO3D_LOGERROR("No audio mode set, can not start playback");
            return false;
        }

        SDL_PauseAudioDevice(deviceID_, 0);

        // Update sound sources before resuming playback to make sure 3D positions are up to date
        UpdateInternal(0.0f);

        playing_ = true;
        return true;
    }

    void Audio::Stop()
    {
        playing_ = false;
    }

    void Audio::SetMasterGain(const String& type, float gain)
    {
        masterGain[type] = Clamp(gain, 0.0f, 1.0f);

        for (SoundSource* source : soundSources)
            source->UpdateMasterGain();
    }

    void Audio::PauseSoundType(const String& type)
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        pausedSoundTypes.insert(type);
    }

    void Audio::ResumeSoundType(const String& type)
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        pausedSoundTypes.erase(type);
        // Update sound sources before resuming playback to make sure 3D positions are up to date
        // Done under mutex to ensure no mixing happens before we are ready
        UpdateInternal(0.0f);
    }

    void Audio::ResumeAll()
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        pausedSoundTypes.clear();
        UpdateInternal(0.0f);
    }

    void Audio::SetListener(SoundListener* listener)
    {
        listener_ = listener;
    }

    void Audio::StopSound(Sound* sound)
    {
        for (SoundSource* source : soundSources)
        {
            if (source->GetSound() == sound)
            {
                source->Stop();
            }
        }
    }

    float Audio::GetMasterGain(const String& type) const
    {
        // By definition previously unknown types return full volume
        auto it = masterGain.find(type);
        if (it == masterGain.end())
            return 1.0f;

        return it->second.GetFloat();
    }

    bool Audio::IsSoundTypePaused(const String& type) const
    {
        return pausedSoundTypes.find(type) != pausedSoundTypes.end();
    }

    SoundListener* Audio::GetListener() const
    {
        return listener_;
    }

    void Audio::AddSoundSource(SoundSource* soundSource)
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        soundSources.push_back(soundSource);
    }

    void Audio::RemoveSoundSource(SoundSource* soundSource)
    {
        std::vector<SoundSource*>::iterator i = std::find( soundSources.begin(), soundSources.end(), soundSource);
        if (i != soundSources.end())
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            soundSources.erase(i);
        }
    }

    float Audio::GetSoundSourceMasterGain(StringHash typeHash) const
    {
        auto masterIt = masterGain.find(SOUND_MASTER_HASH);

        if (!typeHash)
            return masterIt->second.GetFloat();

        auto typeIt = masterGain.find(typeHash);

        if (typeIt == masterGain.end() || typeIt == masterIt)
            return masterIt->second.GetFloat();

        return masterIt->second.GetFloat() * typeIt->second.GetFloat();
    }

    void SDLAudioCallback(void* userdata, Uint8* stream, int len)
    {
        auto* audio = static_cast<Audio*>(userdata);
        {
            std::lock_guard<std::mutex> Lock(audio->GetMutex());
            audio->MixOutput(stream, len / audio->GetSampleSize());
        }
    }

    void Audio::MixOutput(void* dest, unsigned samples)
    {
        if (!playing_ || !clipBuffer_)
        {
            memset(dest, 0, samples * (size_t)sampleSize_);
            return;
        }

        while (samples)
        {
            // If sample count exceeds the fragment (clip buffer) size, split the work
            unsigned workSamples = Min(samples, fragmentSize_);
            unsigned clipSamples = workSamples;
            if (stereo_)
                clipSamples <<= 1;

            // Clear clip buffer
            int* clipPtr = clipBuffer_.Get();
            memset(clipPtr, 0, clipSamples * sizeof(int));

            // Mix samples to clip buffer
            for (SoundSource* source : soundSources)
            {
                // Check for pause if necessary
                if (!pausedSoundTypes.empty())
                {
                    if (pausedSoundTypes.find(source->GetSoundType()) != pausedSoundTypes.end())
                        continue;
                }

                source->Mix(clipPtr, workSamples, mixRate_, stereo_, interpolation_);
            }
            // Copy output from clip buffer to destination
            auto* destPtr = (short*)dest;
            while (clipSamples--)
                *destPtr++ = (short)Clamp(*clipPtr++, -32768, 32767);
            samples -= workSamples;
            ((unsigned char*&)dest) += sampleSize_ * workSamples;
        }
    }

    void Audio::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
    {
        using namespace RenderUpdate;

        Update(eventData[P_TIMESTEP].GetFloat());
    }

    void Audio::Release()
    {
        Stop();

        if (deviceID_)
        {
            SDL_CloseAudioDevice(deviceID_);
            deviceID_ = 0;
            clipBuffer_.Reset();
        }
    }

    void Audio::UpdateInternal(float timeStep)
    {
        URHO3D_PROFILE(UpdateAudio);

        // Update in reverse order, because sound sources might remove themselves
        for (size_t i = soundSources.size() - 1; i < soundSources.size(); --i)
        {
            SoundSource* source = soundSources[i];

            // Check for pause if necessary; do not update paused sound sources
            if (!pausedSoundTypes.empty())
            {
                if (pausedSoundTypes.find(source->GetSoundType()) != pausedSoundTypes.end())
                    continue;
            }

            source->Update(timeStep);
        }
    }

    void RegisterAudioLibrary(Context* context)
    {
        Sound::RegisterObject(context);
        SoundSource::RegisterObject(context);
        SoundSource3D::RegisterObject(context);
        SoundListener::RegisterObject(context);
    }
}
