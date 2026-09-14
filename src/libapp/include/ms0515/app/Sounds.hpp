/*
 * Sounds.hpp - the recordings the machine's mechanical sounds are played
 * from, found on disk: assets/sounds/fdd/<set>/ for a drive model and
 * assets/sounds/kbd/<set>/ for a keyboard, under Paths::searchRoots().
 *
 * A set is a folder of WAV files named for what they are:
 *   drive:     motor_start.wav  motor_loop.wav  motor_stop.wav
 *              seek_in_<n>.wav / seek_out_<n>.wav  - a seek of n tracks,
 *              "in" toward the hub (higher track numbers)
 *              step_in.wav / step_out.wav - one pulse, used when no seek
 *              of the length asked for was recorded
 *   keyboard:  click.wav  bell.wav
 * Any file may be missing: what is missing is silent (a seek falls back
 * on the nearest length, then on step pulses).  A file that is not a
 * PCM WAV is skipped.
 */

#ifndef MS0515_APP_SOUNDS_HPP
#define MS0515_APP_SOUNDS_HPP

#include <ms0515/Audio.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ms0515::app {

class Sounds {
public:
    /* The drive sets found: the folder names under assets/sounds/fdd/,
     * sorted, each once (the first search root that has it wins). */
    static std::vector<std::string> driveSets();

    /* A set by name, or null when no root has it. */
    static std::shared_ptr<ms0515::DriveSounds>    loadDrive(const std::string &set);
    static std::shared_ptr<ms0515::KeyboardSounds> loadKeyboard(const std::string &set);

    /* The same from a folder, wherever it is. */
    static std::shared_ptr<ms0515::DriveSounds>    loadDriveDir(const std::filesystem::path &dir);
    static std::shared_ptr<ms0515::KeyboardSounds> loadKeyboardDir(const std::filesystem::path &dir);

    /* The folder of a set under the search roots, or empty. */
    static std::filesystem::path driveDir(const std::string &set);
    static std::filesystem::path keyboardDir(const std::string &set);
};

} /* namespace ms0515::app */

#endif /* MS0515_APP_SOUNDS_HPP */
