#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace soundsim {

enum class LogLevel {
    trace,
    debug,
    info,
    warn,
    error,
    critical
};

class Logger final {
public:
    static Logger& instance();

    void initialize(const std::filesystem::path& logDirectory,
                    std::string_view sessionName = "soundsim");
    void shutdown();

    void log(LogLevel level, std::string_view subsystem, std::string_view message);

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::filesystem::path logPath() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex mutex_;
    std::ofstream file_;
    std::filesystem::path path_;
    bool initialized_{false};
};

#define SS_LOG_TRACE(subsystem, message) ::soundsim::Logger::instance().log(::soundsim::LogLevel::trace, subsystem, message)
#define SS_LOG_DEBUG(subsystem, message) ::soundsim::Logger::instance().log(::soundsim::LogLevel::debug, subsystem, message)
#define SS_LOG_INFO(subsystem, message)  ::soundsim::Logger::instance().log(::soundsim::LogLevel::info, subsystem, message)
#define SS_LOG_WARN(subsystem, message)  ::soundsim::Logger::instance().log(::soundsim::LogLevel::warn, subsystem, message)
#define SS_LOG_ERROR(subsystem, message) ::soundsim::Logger::instance().log(::soundsim::LogLevel::error, subsystem, message)

} // namespace soundsim
