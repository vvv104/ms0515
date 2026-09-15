/*
 * Audio.cpp — SDL2 audio output: the lib's renderer makes each frame's
 * samples (~882 at 44.1 kHz for a 50 Hz frame), SDL_QueueAudio plays
 * them.
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

/* The machine's speaker is a 1-bit line: the renderer holds it at plus or
 * minus its amplitude for as long as the machine holds the level, so a
 * silent machine still renders a steady offset the size of a beep.  A real
 * loudspeaker cannot hold a level - the cone returns - and neither can the
 * stream: every break in it, the device opening or the queue being cut,
 * steps between that offset and silence, and the step is heard as a click
 * louder than anything the drive makes.  One pole at 20 Hz takes the offset
 * out and leaves every edge of the square wave where it was: at a kilohertz
 * a half period is half a millisecond against the pole's eight, so the beep
 * itself is untouched.
 */
void Audio::removeOffset(int n)
{
    constexpr float kPole = 1.0f - 2.0f * 3.14159265f * 20.0f / static_cast<float>(kSampleRate);
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>(buf_[static_cast<std::size_t>(i)]);
        const float y = x - dcIn_ + kPole * dcOut_;
        dcIn_  = x;
        dcOut_ = y;
        buf_[static_cast<std::size_t>(i)] =
            static_cast<int16_t>(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : static_cast<int>(y));
    }
}

void Audio::endFrame(int totalCycles, bool output)
{
    if (totalCycles <= 0)
        return;
    buf_.resize(static_cast<std::size_t>(kSampleRate) / 10);   /* room for a 100 ms frame */
    const int n = renderer_.render(buf_.data(), static_cast<int>(buf_.size()), static_cast<uint32_t>(totalCycles));
    if (n <= 0)
        return;
    removeOffset(n);              /* always, so the pole does not go stale while muted */
    if (!output || device_ == 0)
        return;

    /* The machine can make sound a little faster than the device plays it
     * (a host frame is not an emulated one), so the queue creeps up.  It
     * used to be kept down by dropping the frame - which threw away
     * whatever happened in those 20 ms: a keyclick every third repeat
     * simply never reached the ears.  Now nothing rendered is discarded:
     * when the backlog grows past a fifth of a second the queue is cut
     * once, and this frame - clicks and all - goes in behind it. */
    constexpr uint32_t kMaxQueuedBytes = kSampleRate * sizeof(int16_t) / 5;   /* 200 ms */
    if (SDL_GetQueuedAudioSize(device_) > kMaxQueuedBytes)
        SDL_ClearQueuedAudio(device_);
    SDL_QueueAudio(device_, buf_.data(), static_cast<uint32_t>(n) * sizeof(int16_t));
}

void Audio::shutdown()
{
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

} /* namespace ms0515_frontend */
