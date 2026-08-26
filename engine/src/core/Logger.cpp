#include "core/Logger.hpp"

#include <cstdio>
#include <vector>

namespace engine::core {

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

Logger::Logger() : startTime_(std::chrono::steady_clock::now()) {}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

LogLevel Logger::minLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return minLevel_;
}

bool Logger::enableFileLogging(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    fileSink_.open(path, std::ios::app);
    return fileSink_.is_open();
}

void Logger::log(LogLevel level, std::string category, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < minLevel_) return;

    float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();
    LogEntry entry{level, std::move(category), std::move(message), elapsed};

    // Warn/Error to stderr, Debug/Info to stdout -- matches this
    // engine's existing fprintf() convention.
    char line[1024];
    std::snprintf(line, sizeof(line), "[%8.3fs][%-5s][%s] %s", entry.timestampSeconds, logLevelName(entry.level),
                  entry.category.c_str(), entry.message.c_str());
    std::FILE* stream = (entry.level >= LogLevel::Warn) ? stderr : stdout;
    std::fprintf(stream, "%s\n", line);

    // Flushed immediately -- a fatal error can kill the process moments later.
    if (fileSink_.is_open()) {
        fileSink_ << line << "\n";
        fileSink_.flush();
    }

    ring_.push_back(std::move(entry));
    while (ring_.size() > kMaxRingEntries) ring_.pop_front();
}

void Logger::logvf(LogLevel level, const char* category, const char* format, va_list args) {
    // Real, standard two-pass vsnprintf sizing -- the same technique
    // every allocation-free-until-necessary C-string formatter uses:
    // measure with a copy of the va_list (va_copy is required here,
    // reusing `args` a second time after vsnprintf already consumed it
    // is undefined behavior), then format into a right-sized buffer.
    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = std::vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);
    if (needed < 0) return; // real, honest bail on a genuinely malformed format string

    std::vector<char> buffer(static_cast<size_t>(needed) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    log(level, category, std::string(buffer.data(), static_cast<size_t>(needed)));
}

void Logger::logf(LogLevel level, const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logvf(level, category, format, args);
    va_end(args);
}

std::vector<LogEntry> Logger::recentEntries(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.size() <= maxCount) return std::vector<LogEntry>(ring_.begin(), ring_.end());
    return std::vector<LogEntry>(ring_.end() - static_cast<long>(maxCount), ring_.end());
}

void Logger::clearRingBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

void logDebug(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Logger::instance().logvf(LogLevel::Debug, category, format, args);
    va_end(args);
}

void logInfo(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Logger::instance().logvf(LogLevel::Info, category, format, args);
    va_end(args);
}

void logWarn(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Logger::instance().logvf(LogLevel::Warn, category, format, args);
    va_end(args);
}

void logError(const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Logger::instance().logvf(LogLevel::Error, category, format, args);
    va_end(args);
}

} // namespace engine::core
