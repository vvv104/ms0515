/*
 * Audio.cpp — SDL2 audio output for the MS0515 1-bit speaker.
 *
 * Each emulated frame (~20 ms at 50 Hz) produces ~882 PCM samples.
 * Transitions are logged with their CPU-cycle position within the
 * frame, then converted to sample offsets for accurate waveform
 * reconstruction.  SDL_QueueAudio feeds them to the output device.
 */

#include "Audio.hpp"
#include <cstdio>

namespace ms0515_frontend {

Audio::~Audio()
{
    shutdown();
}

bool Audio::init()
{
    SDL_AudioSpec want{};
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = nullptr;   /* push mode via SDL_QueueAudio */

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(device_, 0);   /* start playback */
    return true;
}

void Audio::beginFrame()
{
    transitions_.clear();
    startLevel_ = currentLevel_;
}

void Audio::addTransition(int cyclePos, int level)
{
    transitions_.push_back({cyclePos, level});
    currentLevel_ = level;
}

void Audio::endFrame(int totalCycles)
{
    if (device_ == 0 || totalCycles <= 0)
        return;

    /* Prevent audio buffer from growing unboundedly if the emulator
     * runs faster than real time (e.g. high-refresh-rate monitors).
     * If more than ~80 ms of audio is already queued, skip this frame. */
    uint32_t queued = SDL_GetQueuedAudioSize(device_);
    if (queued > kSampleRate * 2 * 4 / 50)   /* ~4 frames worth */
        return;

    int numSamples = (int64_t)kSampleRate * totalCycles / 7500000;
    if (numSamples <= 0) numSamples = 1;

    std::vector<int16_t> buf(numSamples);

    /* Walk through samples, looking up the speaker level at each
     * sample's corresponding CPU cycle offset. */
    int tIdx = 0;
    int level = startLevel_;

    for (int i = 0; i < numSamples; i++) {
        /* CPU cycle corresponding to this audio sample */
        int cycle = (int)((int64_t)i * totalCycles / numSamples);

        /* Advance through transitions up to this cycle */
        while (tIdx < (int)transitions_.size() &&
               transitions_[tIdx].cycle <= cycle) {
            level = transitions_[tIdx].level;
            tIdx++;
        }

        buf[i] = level ? kAmplitude : -kAmplitude;
    }

    SDL_QueueAudio(device_, buf.data(), numSamples * sizeof(int16_t));
}

void Audio::shutdown()
{
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

} /* namespace ms0515_frontend */
