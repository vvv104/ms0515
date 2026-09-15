/*
 * Audio.hpp - the machine's sound as PCM: the speaker, and the mechanical
 * sounds a host can play from recordings - the drive's spindle motor and
 * head seeks, the keyboard's click and bell.
 *
 * One renderer for every front-end (the SDL one, the browser's): each
 * emulated frame the host feeds it the speaker's level transitions and
 * the mechanical events the core reports, each with its CPU-cycle
 * position in the frame, and takes the frame's samples out at the
 * host's rate.  The recordings come as WAV files the host reads
 * (parseWav); their rate need not match the output's.
 */

#ifndef MS0515_AUDIO_HPP
#define MS0515_AUDIO_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace ms0515 {

/* A mono recording. */
struct Pcm {
    int                  rate = 0;      /* samples per second */
    std::vector<int16_t> samples;
    [[nodiscard]] bool empty() const noexcept { return samples.empty(); }
};

/* A RIFF/WAVE file's PCM as mono: 8- or 16-bit samples, one or two
 * channels (averaged).  Anything else - compressed, 24-bit, no data
 * chunk - is nullopt. */
[[nodiscard]] std::optional<Pcm> parseWav(const uint8_t *data, std::size_t size);

/* One drive model's recordings.  A seek is played from the recording of
 * a seek of that many tracks when there is one ("in" = toward the hub,
 * higher track numbers), else from the nearest length recorded, else
 * from one step pulse per track at the command's step rate. */
struct DriveSounds {
    Pcm motorStart;
    Pcm motorLoop;
    Pcm motorStop;
    /* The head's moves, each named by the track it went from and the one it
     * went to.  Not by distance: the head hangs on a steel band the stepper
     * pulls it along, and near the last track it sits close to the motor with
     * a short free tail, so the note is high, while back at track 0 the tail
     * is long and the note is low.  Crossing tracks 0 to 30 and crossing 30 to
     * 60 are different sounds, and a name that says only "thirty tracks"
     * cannot tell them apart. */
    struct Move {
        int from;
        int to;
        Pcm pcm;
    };
    std::vector<Move> moves;
    /* One step of the head, by the track it left.  A formatting run walks the
     * head out a track at a time, so that is where these come from, and a step
     * at track 3 is a different recording from a step at track 60 rather than
     * one recording pretending. */
    std::map<int, Pcm> steps;
    /* By distance alone, for a set that does not say where its moves ran. */
    std::map<int, Pcm> seekIn;
    std::map<int, Pcm> seekOut;
    Pcm stepIn;
    Pcm stepOut;
};

/* The keyboard's own sounds. */
struct KeyboardSounds {
    Pcm click;
    Pcm bell;
};

/* The MS7004's sounds as its firmware drives the piezo element on port P1
 * bit 3 (Alex's listing of the 8035 ROM, 4.608 MHz, 15 clocks a cycle):
 * the click one pulse of 258 cycles (~0.84 ms), the bell 254 half-periods
 * of ~80 cycles (~1.92 kHz for ~66 ms).  The element is taken as a
 * first-order high-pass with a ~0.3 ms time constant: it sounds its
 * edges, not the level.  Its own resonance is not known and not modelled;
 * a recording in assets/sounds/kbd/ms7004/ takes precedence. */
[[nodiscard]] KeyboardSounds ms7004KeyboardSounds(int rate);

class AudioRenderer {
public:
    /* Tracks of the drive, for placing a move by where it ran. */
    static constexpr int kTracks = 80;

    static constexpr int kCpuHz            = 7500000;
    /* The beeper's square wave at full volume - a quarter of what it
     * first was: against the drive and the keyboard it shouted the
     * machine down.  The volume goes to 200 % for anyone who wants it
     * nearer the old level. */
    static constexpr int kSpeakerAmplitude = 1500;

    explicit AudioRenderer(int rate = 48000);

    /* The recordings; null clears them.  Volumes are linear gains, 1 = as recorded. */
    void setDriveSounds(std::shared_ptr<const DriveSounds> sounds);
    void setKeyboardSounds(std::shared_ptr<const KeyboardSounds> sounds);
    void setSpeakerVolume(float gain) noexcept { speakerGain_ = gain; }
    void setDriveVolume(float gain) noexcept { driveGain_ = gain; }
    void setKeyboardVolume(float gain) noexcept { keyboardGain_ = gain; }

    /* A frame: the events with their cycle positions, then render.  An
     * event may also arrive between two renders and waits for the next. */
    void speaker(uint32_t cycle, int level);
    void motor(uint32_t cycle, int drive, bool on);
    void seek(uint32_t cycle, int tracks, uint32_t stepCycles);
    void keyClick(uint32_t cycle);
    void bell(uint32_t cycle);
    /* The frame's samples into `out` (up to `max`); returns the count -
     * `frameCycles` at the CPU clock decides how many.  What is still
     * sounding carries over to the next frame. */
    int render(int16_t *out, int max, uint32_t frameCycles);

    /* Silence: every voice dropped, the speaker level kept. */
    void reset();

    /* What is sounding: for the tests and the page's diagnostics. */
    [[nodiscard]] bool motorRunning(int drive) const noexcept;
    [[nodiscard]] int voices() const noexcept { return static_cast<int>(voices_.size()); }

private:
    enum class Tag : uint8_t { motor0, motor1, seek, click, bell };
    enum class Motor : uint8_t { off, starting, running, stopping };

    struct Voice {
        Tag         tag;
        const Pcm  *pcm;
        double      pos;        /* in the recording's samples */
        int         startAt;    /* output sample of this frame it begins at; 0 = already sounding */
        float       gain;
        bool        loop;
    };
    struct Event {
        enum class Kind : uint8_t { speaker, motor, seek, click, bell } kind;
        uint32_t cycle;
        int      a;             /* level / drive / tracks */
        int      b;             /* motor on / step cycles */
    };

    void mixVoice(Voice &v, std::vector<float> &mix, int from, int to);
    void apply(const Event &e, int at);
    void startMotor(int drive, int at);
    void stopMotor(int drive, int at);
    void startSeek(int tracks, uint32_t stepCycles, int at);
    void dropVoices(Tag tag);
    [[nodiscard]] int keyboardFreeAt(int at) const;
    void play(Tag tag, const Pcm &pcm, int at, float gain, bool loop);
    [[nodiscard]] int cyclesToSamples(uint32_t cycles, uint32_t frameCycles, int n) const noexcept;

    int   rate_;
    float speakerGain_  = 1.0f;
    float driveGain_    = 1.0f;
    float keyboardGain_ = 1.0f;
    std::shared_ptr<const DriveSounds>    drive_;
    std::shared_ptr<const KeyboardSounds> keyboard_;
    std::vector<Event> events_;
    std::vector<Voice> voices_;
    Motor motor_[2] = {Motor::off, Motor::off};
    /* Where the head stands, as the moves that went by say.  The controller
     * never reports it, but every move arrives with its signed length and a
     * restore always drives to track 0, so following the moves keeps this
     * right and any restore puts it right again. */
    int   track_ = 0;
    int   level_ = 0;           /* the speaker's level carried between frames */
};

/* What a loudspeaker does to a steady level: nothing.
 *
 * The machine's speaker is a 1-bit line, and the renderer holds it at plus or
 * minus its amplitude for as long as the machine holds the level - so a silent
 * machine still renders a steady offset the size of a beep.  Nothing is heard
 * while it lasts, but every break in the stream steps between that offset and
 * silence, and the step is heard as a click louder than anything the drive
 * makes: the audio device opening, a queue being cut, a page's buffer running
 * dry.  A real cone cannot hold a level either; it returns.
 *
 * One pole at 20 Hz takes the offset out and leaves every edge of the square
 * wave where it was - at a kilohertz a half period is half a millisecond
 * against the pole's eight, so the beep itself is untouched.  It belongs to
 * whatever hands the samples to a device, not to the rendering, so the host
 * layers apply it and the renderer's own output stays exactly what it says.
 */
class DcBlocker {
public:
    explicit DcBlocker(int rate, float cornerHz = 20.0f) noexcept;

    /* In place, carrying the pole between calls. */
    void apply(int16_t *samples, int count) noexcept;
    void reset() noexcept { in_ = out_ = 0.0f; }

private:
    float pole_;
    float in_  = 0.0f;
    float out_ = 0.0f;
};

} /* namespace ms0515 */

#endif /* MS0515_AUDIO_HPP */
