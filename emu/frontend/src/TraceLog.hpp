/*
 * TraceLog.hpp — Async file-backed trace sink for BOARD_TRACE.
 *
 * Wraps spdlog's async logger so that emulator trace callbacks queue
 * messages to a background thread instead of blocking the CPU step
 * loop on disk I/O.
 */

#pragma once

#include <memory>
#include <string>

namespace spdlog { class logger; }

namespace ms0515_frontend {

class TraceLog {
public:
    /* Open `path` for writing and spin up the async logging thread.
     * Returns true on success. */
    bool open(const std::string &path);

    /* Shut down the async thread and flush pending messages. */
    void close();

    /* Emit one line to the trace log (thread-safe, non-blocking). */
    void write(const char *line);

    static void traceCallback(void *userdata, const char *line);

private:
    std::shared_ptr<spdlog::logger> logger_;
};

} /* namespace ms0515_frontend */
