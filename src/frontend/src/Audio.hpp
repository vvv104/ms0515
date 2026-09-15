/*
 * Audio.hpp — SDL2 audio output for the machine's sound: the 1-bit
 * speaker and the recordings of the drive and the keyboard.
 *
 * The rendering is the lib's (ms0515::AudioRenderer); this module owns
 * the SDL device and queues the renderer's samples to it each frame.
 * The speaker's transitions and the mechanical events reach the renderer
 * through App's callbacks, with their cycle position in the frame.
 */

#ifndef MS0515_FRONTEND_AUDIO_HPP
#define MS0515_FRONTEND_AUDIO_HPP

#include <ms0515/Audio.hpp>

#include <SDL.h>
#include <cstdint>
#include <vector>

namespace ms0515_frontend {

class Audio {
public:
    static constexpr int kSampleRate = 44100;

    Audio() = default;
    ~Audio();

    Audio(const Audio &)            = delete;
    Audio &operator=(const Audio &) = delete;

    /* Open the SDL audio device.  Returns false on failure. */
    [[nodiscard]] bool init();

    /* Where the frame's events go. */
    [[nodiscard]] ms0515::AudioRenderer &renderer() noexcept { return renderer_; }

    /* Call at the end of each emulated frame: renders the frame's
     * samples and, when `output`, queues them to SDL.  Rendered either
     * way, so the motors and the seeks keep their place while the sound
     * is off. `totalCycles` is the frame length in CPU cycles. */
    void endFrame(int totalCycles, bool output);

    void shutdown();

private:
    SDL_AudioDeviceID     device_ = 0;
    ms0515::AudioRenderer renderer_{kSampleRate};
    ms0515::DcBlocker     speakerCone_{kSampleRate};
    std::vector<int16_t>  buf_;
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_AUDIO_HPP */
