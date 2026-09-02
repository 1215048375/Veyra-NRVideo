#include "veyra/Log.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <format>

namespace veyra {

namespace {

const char* levelTag(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    default: return "?????";
    }
}

std::string timestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
    const int milliseconds = static_cast<int>(ms % 1000);
    std::tm utc{};
    gmtime_s(&utc, &seconds);
    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
        utc.tm_hour, utc.tm_min, utc.tm_sec, milliseconds);
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    closeFile();
}

bool Logger::openFile(const std::wstring& path)
{
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    if (_wfopen_s(&file_, path.c_str(), L"wb") != 0 || file_ == nullptr) {
        return false;
    }
    return true;
}

void Logger::closeFile()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::setConsoleEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

void Logger::write(LogLevel level, const char* component, const std::string& message)
{
    const unsigned long threadId = GetCurrentThreadId();
    const std::string line = std::format("{} t={} [{}] [{}] {}",
        timestampUtc(), threadId, levelTag(level), component, message);

    std::lock_guard<std::mutex> lock(mutex_);
    if (consoleEnabled_) {
        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
    }
    if (file_ != nullptr) {
        std::fprintf(file_, "%s\n", line.c_str());
        std::fflush(file_);
    }
}

namespace log {

void trace(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Trace, component, message);
}

void info(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Info, component, message);
}

void warn(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Warn, component, message);
}

void error(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Error, component, message);
}

} // namespace log

} // namespace veyra
