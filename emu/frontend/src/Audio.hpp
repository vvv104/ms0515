/*
 * Audio.hpp — SDL2 audio output for the MS0515 1-bit speaker.
 *
 * The MS0515 speaker is driven by timer channel 2 (square wave),
 * gated by System Register C bits 5-7.  The board core tracks
 * transitions and calls a callback on each edge.  This module
 * records those transitions with sub-frame timing and renders
 * them into PCM samples queued to SDL each frame.
 */

#ifndef MS0515_FRONTEND_AUDIO_HPP
#define MS0515_FRONTEND_AUDIO_HPP

#include <SDL.h>
#include <cstdint>
#include <vector>

namespace ms0515_frontend {

class Audio {
public:
    static constexpr int kSampleRate  = 44100;
    static constexpr int kAmplitude   = 6000;   /* 16-bit amplitude (~18%) */

    Audio() = default;
    ~Audio();

    Audio(const Audio &)            = delete;
    Audio &operator=(const Audio &) = delete;

    /* Open the SDL audio device.  Returns false on failure. */
    [[nodiscard]] bool init();

    /* Call at the start of each emulated frame. */
    void beginFrame();

    /* Record a speaker level change.  `cyclePos` is the CPU cycle
     * offset within the current frame (from board.frame_cycle_pos). */
    void addTransition(int cyclePos, int level);

    /* Call at the end of each emulated frame.  Renders accumulated
     * transitions into PCM samples and queues them to SDL.
     * `totalCycles` is the frame length in CPU cycles. */
    void endFrame(int totalCycles);

    void shutdown();

private:
    SDL_AudioDeviceID device_ = 0;

    struct Transition {
        int cycle;
        int level;
    };
    std::vector<Transition> transitions_;
    int startLevel_ = 0;   /* speaker level at frame start */
    int currentLevel_ = 0;
};

} /* namespace ms0515_frontend */

#endif /* MS0515_FRONTEND_AUDIO_HPP */
