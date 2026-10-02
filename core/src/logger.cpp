#include "soundsim/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace soundsim {
namespace {

const char* toString(LogLevel level) {
    switch (level) {
        case LogLevel::trace: return "TRACE";
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info: return "INFO";
        case LogLevel::warn: return "WARN";
        case LogLevel::error: return "ERROR";
        case LogLevel::critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

std::string timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string fileTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return ss.str();
}

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    shutdown();
}

void Logger::initialize(const std::filesystem::path& logDirectory, std::string_view sessionName) {
    std::scoped_lock lock(mutex_);
    if (initialized_) {
        return;
    }

    std::filesystem::create_directories(logDirectory);
    path_ = logDirectory / (std::string(sessionName) + "-" + fileTimestamp() + ".log");
    file_.open(path_, std::ios::out | std::ios::app);
    initialized_ = file_.is_open();

    const std::string line = "[" + timestampNow() + "] [INFO] [logger] session start: " + path_.string();
    std::cout << line << '\n';
    if (file_) {
        file_ << line << '\n';
        file_.flush();
    }
}

void Logger::shutdown() {
    std::scoped_lock lock(mutex_);
    if (!initialized_) {
        return;
    }

    const std::string line = "[" + timestampNow() + "] [INFO] [logger] session end";
    std::cout << line << '\n';
    if (file_) {
        file_ << line << '\n';
        file_.flush();
        file_.close();
    }
    initialized_ = false;
}

void Logger::log(LogLevel level, std::string_view subsystem, std::string_view message) {
    std::scoped_lock lock(mutex_);
    const std::string line = "[" + timestampNow() + "] [" + toString(level) + "] [" +
                             std::string(subsystem) + "] " + std::string(message);
    std::cout << line << '\n';
    if (file_) {
        file_ << line << '\n';
        file_.flush();
    }
}

bool Logger::initialized() const noexcept {
    std::scoped_lock lock(mutex_);
    return initialized_;
}

std::filesystem::path Logger::logPath() const {
    std::scoped_lock lock(mutex_);
    return path_;
}

} // namespace soundsim
