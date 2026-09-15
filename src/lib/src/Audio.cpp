/*
 * Audio.cpp - the machine's sound as PCM: the speaker's square wave and
 * the recordings of the drive and the keyboard, mixed frame by frame.
 * See Audio.hpp.
 */

#include "ms0515/Audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

namespace ms0515 {

/* ---- WAV ------------------------------------------------------------------ */

namespace {

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

struct WavFormat {
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    bool     pcm = false;
};

bool readFormat(const uint8_t *p, uint32_t size, WavFormat &f)
{
    if (size < 16) return false;
    const uint16_t tag = le16(p);
    f.channels = le16(p + 2);
    f.rate     = le32(p + 4);
    f.bits     = le16(p + 14);
    /* 1 = PCM; 0xFFFE = extensible, whose sub-format's first word says PCM. */
    f.pcm = tag == 1 || (tag == 0xFFFE && size >= 26 && le16(p + 24) == 1);
    return f.pcm && (f.channels == 1 || f.channels == 2) && (f.bits == 8 || f.bits == 16) && f.rate > 0;
}

void convert(const uint8_t *p, uint32_t size, const WavFormat &f, std::vector<int16_t> &out)
{
    const uint32_t bytesPerSample = f.bits / 8;
    const uint32_t frameBytes = bytesPerSample * f.channels;
    const uint32_t frames = size / frameBytes;
    out.reserve(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        int sum = 0;
        for (uint16_t c = 0; c < f.channels; ++c) {
            const uint8_t *s = p + i * frameBytes + c * bytesPerSample;
            sum += f.bits == 8 ? (static_cast<int>(s[0]) - 128) << 8 : static_cast<int16_t>(le16(s));
        }
        out.push_back(static_cast<int16_t>(sum / f.channels));
    }
}

} // namespace

std::optional<Pcm> parseWav(const uint8_t *data, std::size_t size)
{
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return std::nullopt;
    WavFormat fmt;
    bool haveFormat = false;
    Pcm pcm;
    std::size_t at = 12;
    while (at + 8 <= size) {
        const uint8_t *chunk = data + at;
        const uint32_t chunkSize = le32(chunk + 4);
        const std::size_t body = at + 8;
        const std::size_t avail = size - body;
        const uint32_t take = chunkSize < avail ? chunkSize : static_cast<uint32_t>(avail);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (!readFormat(data + body, take, fmt)) return std::nullopt;
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) return std::nullopt;
            convert(data + body, take, fmt, pcm.samples);
            pcm.rate = static_cast<int>(fmt.rate);
            return pcm.samples.empty() ? std::nullopt : std::optional<Pcm>(std::move(pcm));
        }
        at = body + chunkSize + (chunkSize & 1);      /* chunks are word-aligned */
    }
    return std::nullopt;
}

/* ---- the MS7004's piezo, from the firmware ---------------------------------- */

namespace {

/* The element as what it is: a piezo disc that rings.  The firmware
 * drives P1.3 with edges; each edge kicks the disc, which answers with a
 * decaying tone at its own resonance - that ring is the click's voice.
 * A second-order resonator driven by the edges, then normalised.
 *
 * `stretches` are (machine cycles, level) of the drive signal.
 */
Pcm piezo(int rate, const std::vector<std::pair<int, int>> &stretches, double tailSeconds)
{
    constexpr double kCycle = 15.0 / 4608000.0;    /* one 8035 machine cycle */
    constexpr double kResonanceHz = 4000.0;        /* the disc's ring */
    constexpr double kDecaySeconds = 0.00035;      /* and how fast it dies away */
    constexpr double kPeak = 11000.0;

    double length = tailSeconds;
    for (const auto &s : stretches) length += s.first * kCycle;
    const auto n = static_cast<std::size_t>(length * rate);
    if (n == 0) return {};

    const double dt = 1.0 / rate;
    const double r = std::exp(-dt / kDecaySeconds);
    const double w = 2.0 * 3.14159265358979323846 * kResonanceHz * dt;
    const double a1 = 2.0 * r * std::cos(w), a2 = -r * r;

    std::vector<double> y(n, 0.0);
    std::size_t si = 0;
    double stretchEnd = stretches.empty() ? 0.0 : stretches[0].first * kCycle;
    int level = stretches.empty() ? 0 : stretches[0].second;
    double peak = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double kick = 0.0;
        const double t = i * dt;
        while (si + 1 < stretches.size() && t >= stretchEnd) {
            ++si;
            stretchEnd += stretches[si].first * kCycle;
            if (stretches[si].second != level) {       /* an edge: kick the disc */
                kick += stretches[si].second ? 1.0 : -1.0;
                level = stretches[si].second;
            }
        }
        if (i == 0 && level) kick += 1.0;              /* the first edge, into the drive */
        const double prev1 = i >= 1 ? y[i - 1] : 0.0;
        const double prev2 = i >= 2 ? y[i - 2] : 0.0;
        y[i] = a1 * prev1 + a2 * prev2 + kick;
        const double m = y[i] < 0 ? -y[i] : y[i];
        if (m > peak) peak = m;
    }

    Pcm p;
    p.rate = rate;
    p.samples.reserve(n);
    const double gain = peak > 0.0 ? kPeak / peak : 0.0;
    for (double v : y) p.samples.push_back(static_cast<int16_t>(v * gain));
    return p;
}

} // namespace

KeyboardSounds ms7004KeyboardSounds(int rate)
{
    KeyboardSounds k;
    /* L_0C7: high for 258 machine cycles, then low - two kicks 0.84 ms
     * apart, and 1.2 ms for the ring to die: a tick of about 2 ms, well
     * clear of the 30 ms the typematic leaves between repeats. */
    k.click = piezo(rate, {{258, 1}, {1, 0}}, 0.0012);
    /* L_0E4: 254 toggles some 80 cycles apart - 1.9 kHz for 66 ms. */
    std::vector<std::pair<int, int>> bell;
    for (int i = 0; i < 254; ++i) bell.emplace_back(80, (i & 1) ? 0 : 1);
    k.bell = piezo(rate, bell, 0.003);
    return k;
}

/* ---- the renderer ---------------------------------------------------------- */

AudioRenderer::AudioRenderer(int rate) : rate_(rate > 0 ? rate : 48000) {}

void AudioRenderer::setDriveSounds(std::shared_ptr<const DriveSounds> sounds)
{
    /* The voices point into the old recordings: drop them; a motor that
     * runs goes on with the new loop. */
    dropVoices(Tag::motor0);
    dropVoices(Tag::motor1);
    dropVoices(Tag::seek);
    drive_ = std::move(sounds);
    for (int d = 0; d < 2; ++d) {
        if (motor_[d] == Motor::off || motor_[d] == Motor::stopping) { motor_[d] = Motor::off; continue; }
        motor_[d] = Motor::running;
        if (drive_ && !drive_->motorLoop.empty()) play(d ? Tag::motor1 : Tag::motor0, drive_->motorLoop, 0, driveGain_, true);
    }
}

void AudioRenderer::setKeyboardSounds(std::shared_ptr<const KeyboardSounds> sounds)
{
    dropVoices(Tag::click);
    dropVoices(Tag::bell);
    keyboard_ = std::move(sounds);
}

void AudioRenderer::speaker(uint32_t cycle, int level) { events_.push_back({Event::Kind::speaker, cycle, level ? 1 : 0, 0}); }
void AudioRenderer::motor(uint32_t cycle, int drive, bool on) { events_.push_back({Event::Kind::motor, cycle, drive & 1, on ? 1 : 0}); }
void AudioRenderer::seek(uint32_t cycle, int tracks, uint32_t stepCycles) { events_.push_back({Event::Kind::seek, cycle, tracks, static_cast<int>(stepCycles)}); }
void AudioRenderer::keyClick(uint32_t cycle) { events_.push_back({Event::Kind::click, cycle, 0, 0}); }
void AudioRenderer::bell(uint32_t cycle) { events_.push_back({Event::Kind::bell, cycle, 0, 0}); }

void AudioRenderer::reset()
{
    voices_.clear();
    events_.clear();
    motor_[0] = motor_[1] = Motor::off;
    track_ = 0;
}

bool AudioRenderer::motorRunning(int drive) const noexcept
{
    const Motor m = motor_[drive & 1];
    return m == Motor::starting || m == Motor::running;
}

int AudioRenderer::cyclesToSamples(uint32_t cycles, uint32_t frameCycles, int n) const noexcept
{
    if (frameCycles == 0) return 0;
    const auto at = static_cast<int>(static_cast<int64_t>(cycles) * n / frameCycles);
    return at < 0 ? 0 : at >= n ? n - 1 : at;
}

void AudioRenderer::play(Tag tag, const Pcm &pcm, int at, float gain, bool loop)
{
    if (pcm.empty()) return;
    voices_.push_back({tag, &pcm, 0.0, at, gain, loop});
}

void AudioRenderer::dropVoices(Tag tag)
{
    std::erase_if(voices_, [tag](const Voice &v) { return v.tag == tag; });
}

/* The keyboard has one element and a firmware that sits in a delay loop
 * while it sounds: it cannot click over a click, or ring twice at once.
 * So a new keyboard sound starts where the one sounding ends - the two
 * bells of power-on come out as the pair they are, and a click can never
 * pile onto the one before it. */
int AudioRenderer::keyboardFreeAt(int at) const
{
    for (const auto &v : voices_) {
        if ((v.tag != Tag::click && v.tag != Tag::bell) || v.pcm == nullptr) continue;
        const double left = static_cast<double>(v.pcm->samples.size()) - v.pos;
        if (left <= 0) continue;
        const int ends = v.startAt + static_cast<int>(left * rate_ / v.pcm->rate);
        if (ends > at) at = ends;
    }
    return at;
}

void AudioRenderer::startMotor(int drive, int at)
{
    if (motorRunning(drive)) return;
    const Tag tag = drive ? Tag::motor1 : Tag::motor0;
    dropVoices(tag);                                   /* a stop still sounding */
    if (!drive_) { motor_[drive] = Motor::running; return; }
    if (!drive_->motorStart.empty()) {
        play(tag, drive_->motorStart, at, driveGain_, false);
        motor_[drive] = Motor::starting;
    } else {
        play(tag, drive_->motorLoop, at, driveGain_, true);
        motor_[drive] = Motor::running;
    }
}

void AudioRenderer::stopMotor(int drive, int at)
{
    if (!motorRunning(drive)) return;
    const Tag tag = drive ? Tag::motor1 : Tag::motor0;
    dropVoices(tag);
    if (drive_ && !drive_->motorStop.empty()) {
        play(tag, drive_->motorStop, at, driveGain_, false);
        motor_[drive] = Motor::stopping;
    } else {
        motor_[drive] = Motor::off;
    }
}

/* A move of the head, from where it stands to where the controller sends it.
 *
 * The recording that ran nearest the same way is used - nearest in both ends,
 * not in distance alone, because the note the head makes depends on where it
 * is: near the last track it sits close to the stepper with a short free
 * length of band to ring, and back at track 0 the tail is long and the note
 * low.  A set that names its moves only by distance falls back to that, and
 * one with no moves at all to a step per track at the rate the command asks.
 */
void AudioRenderer::startSeek(int tracks, uint32_t stepCycles, int at)
{
    if (!drive_ || tracks == 0) return;
    const int from = track_;
    int to = from + tracks;
    if (to < 0) to = 0;
    if (to > kTracks - 1) to = kTracks - 1;
    track_ = to;

    const int n = tracks < 0 ? -tracks : tracks;

    /* A track or two is a click or two, and no recording of a long move will
     * do for it: it would sound for a tenth of a second where the head took a
     * hundredth, and a formatting run would pile one over the next.  Below
     * this the steps are played one at a time, at the rate the command asks. */
    constexpr int kSingly = 2;
    if (n <= kSingly && !drive_->steps.empty()) {
        /* Each track crossed sounds as the recording made at that track. */
        const auto spacing = static_cast<int>(static_cast<int64_t>(stepCycles) * rate_ / kCpuHz);
        for (int k = 0; k < n; ++k) {
            const int track = tracks > 0 ? from + k : from - k - 1;
            auto it = drive_->steps.lower_bound(track);
            if (it == drive_->steps.end()) --it;
            else if (it != drive_->steps.begin()) {
                auto prev = std::prev(it);
                if (track - prev->first <= it->first - track) it = prev;
            }
            play(Tag::seek, it->second, at + k * spacing, driveGain_, false);
        }
        return;
    }

    if (n > kSingly) {
        const DriveSounds::Move *best = nullptr;
        int closest = 0;
        for (const auto &m : drive_->moves) {
            if ((m.to > m.from) != (tracks > 0) || m.pcm.empty()) continue;
            /* Near in both ends, and of about the right length: a move half
             * again as long as the one asked for is the wrong sound however
             * near it began. */
            const int span = m.to > m.from ? m.to - m.from : m.from - m.to;
            const int slack = 4 + n / 2;
            if (span < n - slack || span > n + slack) continue;
            const int d = std::abs(m.from - from) + std::abs(m.to - to);
            if (!best || d < closest) { best = &m; closest = d; }
        }
        if (best) {
            play(Tag::seek, best->pcm, at, driveGain_, false);
            return;
        }
    }

    const auto &seeks = tracks > 0 ? drive_->seekIn : drive_->seekOut;
    if (!seeks.empty()) {
        /* The recording of this length, else the nearest one. */
        auto hi = seeks.lower_bound(n);
        if (hi == seeks.end()) --hi;
        else if (hi != seeks.begin() && hi->first != n) {
            auto lo = std::prev(hi);
            if (n - lo->first <= hi->first - n) hi = lo;
        }
        play(Tag::seek, hi->second, at, driveGain_, false);
        return;
    }
    const Pcm &step = tracks > 0 ? drive_->stepIn : drive_->stepOut;
    if (step.empty()) return;
    const auto spacing = static_cast<int>(static_cast<int64_t>(stepCycles) * rate_ / kCpuHz);
    for (int k = 0; k < n; ++k) play(Tag::seek, step, at + k * spacing, driveGain_, false);
}

void AudioRenderer::apply(const Event &e, int at)
{
    switch (e.kind) {
    case Event::Kind::speaker: level_ = e.a; break;
    case Event::Kind::motor:   if (e.b) startMotor(e.a, at); else stopMotor(e.a, at); break;
    case Event::Kind::seek:    startSeek(e.a, static_cast<uint32_t>(e.b), at); break;
    case Event::Kind::click:   if (keyboard_) play(Tag::click, keyboard_->click, keyboardFreeAt(at), keyboardGain_, false); break;
    case Event::Kind::bell:    if (keyboard_) play(Tag::bell, keyboard_->bell, keyboardFreeAt(at), keyboardGain_, false); break;
    }
}

/* One voice into the samples [from, to) of the frame, from its own start
 * if that comes later; the recording is resampled by linear interpolation.
 * A motor start that ends goes straight on with the loop; a stop that
 * ends leaves the motor off; any other end is the voice's. */
void AudioRenderer::mixVoice(Voice &v, std::vector<float> &mix, int from, int to)
{
    if (v.pcm == nullptr) return;
    const int drive = v.tag == Tag::motor0 ? 0 : v.tag == Tag::motor1 ? 1 : -1;
    for (int i = v.startAt > from ? v.startAt : from; i < to; ++i) {
        if (v.pos >= static_cast<double>(v.pcm->samples.size())) {
            if (v.loop) { v.pos -= static_cast<double>(v.pcm->samples.size()); }
            else if (drive >= 0 && motor_[drive] == Motor::starting && drive_ && !drive_->motorLoop.empty()) {
                v.pcm = &drive_->motorLoop; v.loop = true; v.pos = 0.0; motor_[drive] = Motor::running;
            } else {
                if (drive >= 0 && motor_[drive] == Motor::stopping) motor_[drive] = Motor::off;
                v.pcm = nullptr;
                return;
            }
        }
        const auto &s = v.pcm->samples;
        const auto k = static_cast<std::size_t>(v.pos);
        const auto f = static_cast<float>(v.pos - static_cast<double>(k));
        const float a = s[k];
        const float b = k + 1 < s.size() ? s[k + 1] : v.loop ? s[0] : a;
        mix[static_cast<std::size_t>(i)] += (a + (b - a) * f) * v.gain;
        v.pos += static_cast<double>(v.pcm->rate) / rate_;
    }
}

/* The frame in segments between its events: each segment takes the speaker
 * at its level and every voice up to the event, then the event is applied
 * (a level change, a voice started or dropped) and the next segment goes
 * on from there. */
int AudioRenderer::render(int16_t *out, int max, uint32_t frameCycles)
{
    if (!out || max <= 0) return 0;
    auto n = static_cast<int>(static_cast<int64_t>(rate_) * frameCycles / kCpuHz);
    if (n <= 0) n = 1;
    if (n > max) n = max;
    std::vector<float> mix(static_cast<std::size_t>(n), 0.0f);
    const float amp = static_cast<float>(kSpeakerAmplitude) * speakerGain_;

    int pos = 0;
    auto segment = [&](int to) {
        for (int i = pos; i < to; ++i) mix[static_cast<std::size_t>(i)] += level_ ? amp : -amp;
        for (std::size_t v = 0; v < voices_.size(); ++v) mixVoice(voices_[v], mix, pos, to);
        pos = to;
    };
    std::stable_sort(events_.begin(), events_.end(), [](const Event &x, const Event &y) { return x.cycle < y.cycle; });
    for (const Event &e : events_) {
        const int at = cyclesToSamples(e.cycle, frameCycles, n);
        if (at > pos) segment(at);
        apply(e, at);
    }
    segment(n);
    events_.clear();

    std::erase_if(voices_, [](const Voice &v) { return v.pcm == nullptr; });
    for (Voice &v : voices_) v.startAt = v.startAt >= n ? v.startAt - n : 0;    /* due later: wait; sounding: go on */

    for (int i = 0; i < n; ++i) {
        const float x = mix[static_cast<std::size_t>(i)];
        out[i] = static_cast<int16_t>(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : static_cast<int>(x));
    }
    return n;
}

/* ---- the loudspeaker's own high pass ---------------------------------------- */

DcBlocker::DcBlocker(int rate, float cornerHz) noexcept
    : pole_(rate > 0 ? 1.0f - 2.0f * 3.14159265f * cornerHz / static_cast<float>(rate) : 0.0f)
{
    if (pole_ < 0.0f) pole_ = 0.0f;
}

void DcBlocker::apply(int16_t *samples, int count) noexcept
{
    if (!samples) return;
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(samples[i]);
        const float y = x - in_ + pole_ * out_;
        in_  = x;
        out_ = y;
        samples[i] = static_cast<int16_t>(y > 32767.0f    ? 32767
                                          : y < -32768.0f ? -32768
                                                          : static_cast<int>(y));
    }
}

} /* namespace ms0515 */
