#include "TraceLog.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdio>

namespace ms0515_frontend {

bool TraceLog::open(const std::string &path)
{
    try {
        /* 8192-slot queue, single background thread — more than enough
         * for emulator-rate messages; if the queue fills the caller
         * (the CPU step) blocks briefly rather than dropping lines. */
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path, /*truncate=*/true);
        logger_ = std::make_shared<spdlog::async_logger>(
            "trace", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger_->set_pattern("%v");
        logger_->set_level(spdlog::level::trace);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "trace: failed to open '%s': %s\n",
                     path.c_str(), ex.what());
        logger_.reset();
        return false;
    }
    return true;
}

void TraceLog::close()
{
    if (logger_) {
        logger_->flush();
        logger_.reset();
    }
    spdlog::shutdown();
}

void TraceLog::write(const char *line)
{
    if (logger_)
        logger_->trace(line);
}

void TraceLog::traceCallback(void *userdata, const char *line)
{
    auto *self = static_cast<TraceLog *>(userdata);
    self->write(line);
}

} /* namespace ms0515_frontend */
