#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include "veyra/diagnostics/DiagnosticEvent.h"

namespace veyra {

enum class LogLevel : uint8_t {
    Trace,
    Info,
    Warn,
    Error,
};

// Process-wide structured logger (Playbook section 17.1).
// Line shape:
//   <UTC ISO8601 ms> t=<tid> [LEVEL] [component] message
// Thread-safe; writes to stdout and, when openFile() was called, to a file.
class Logger {
public:
    static Logger& instance();

    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Creates parent directories; returns false and logs to console on failure.
    bool openFile(const std::wstring& path);
    void closeFile();
    void setConsoleEnabled(bool enabled);

    void write(LogLevel level, const char* component, const std::string& message);

    // Flush the file sink immediately (staged-teardown markers must survive
    // a crash on the very next call).
    void flush();
    std::string diagnosticReport();
    std::string latestProblem();
    static void diagnosticContext(diagnostics::DiagnosticEvent);

private:
    std::mutex mutex_;
    std::FILE* file_ = nullptr;
    bool consoleEnabled_ = true;
    uint64_t lastFileFlushTick_=0;
    size_t bufferedLogBytes_=0;
    diagnostics::DiagnosticHistory diagnostics_;std::string latestProblem_;
};

namespace log {

// Flush the log sink to disk immediately (teardown markers).

void trace(const char* component, const std::string& message);
void info(const char* component, const std::string& message);
void warn(const char* component, const std::string& message);
void error(const char* component, const std::string& message);
bool verboseFrameLogs();

// Key=value helper used by call sites that need structured fields.
inline std::string kv(const char* key, uint64_t value)
{
    return std::string(key) + "=" + std::to_string(value);
}

inline std::string kvHex(const char* key, uint64_t value)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return std::string(key) + "=" + buffer;
}

inline std::string kv(const char* key, const std::string& value)
{
    return std::string(key) + "=" + value;
}

} // namespace log

} // namespace veyra
