#include "veyra/Log.h"
#include "veyra/diagnostics/Redaction.h"
#include <sstream>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <share.h>

namespace veyra {

namespace {
thread_local diagnostics::DiagnosticEvent threadDiagnosticContext;

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
    // Diagnostics may read a running session without stopping capture. Keep
    // concurrent writers excluded so another instance cannot truncate this log.
    file_ = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
    if (file_ == nullptr) {
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
    if(level==LogLevel::Warn||level==LogLevel::Error){auto event=threadDiagnosticContext;event.timestamp=timestampUtc();event.severity=levelTag(level);event.component=component;
        auto extract=[&](const char* pattern)->std::optional<uint64_t>{std::smatch m;if(std::regex_search(message,m,std::regex(pattern,std::regex::icase))){try{return std::stoull(m[1].str(),nullptr,m[1].str().starts_with("0x")?16:10);}catch(...){}}return {};};
        if(auto c=extract("(?:HRESULT|hr)[=: ]+(0x[0-9a-f]+|[0-9]+)"))event.hresult=c;
        if(auto c=extract("seh[=: ]+(0x[0-9a-f]+|[0-9]+)"))event.seh=c;
        if(event.component.find("nvof")!=std::string::npos){if(auto c=extract("(?:status|st)[=: ]+(0x[0-9a-f]+|[0-9]+)"))event.nvof=c;}
        if(event.component=="ngx"||message.find("evaluate")!=std::string::npos){if(auto c=extract("(?:result[=: ]+|failed +)(0x[0-9a-f]+|[0-9]+)"))event.ngx=c;}
        event.stage=event.stage.empty()?"runtime":event.stage;event.message=message;event.fingerprint=event.component+"|"+event.stage+"|"+diagnostics::redact(message);latestProblem_=event.component+": "+event.message;diagnostics_.add(std::move(event));}
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


void Logger::diagnosticContext(diagnostics::DiagnosticEvent event){threadDiagnosticContext=std::move(event);}
std::string Logger::latestProblem(){std::lock_guard lock(mutex_);return diagnostics::redact(latestProblem_);}
std::string Logger::diagnosticReport(){std::lock_guard lock(mutex_);std::ostringstream o;o<<"Veyra 本地诊断（复制前脱敏预览；不会上传）\n";
    for(size_t i=0;i<diagnostics_.size();++i){const auto& e=diagnostics_.events()[i];const auto& r=e.resolution;
        auto code=[](std::optional<uint64_t> v){return v?std::format("0x{:X}",*v):std::string("未提供");};
        o<<"\n时间="<<e.timestamp<<" 严重级别="<<e.severity<<"\n组件="<<e.component<<" 阶段="<<e.stage<<" 次数="<<e.occurrenceCount<<"\n"<<e.message<<"\nHRESULT="<<code(e.hresult)<<" NGX="<<code(e.ngx)<<" NVOF="<<code(e.nvof)<<" SEH="<<code(e.seh)<<"\n";
        o<<"source="<<r.source.width<<'x'<<r.source.height<<" base="<<r.base.width<<'x'<<r.base.height<<" NR="<<r.nr.width<<'x'<<r.nr.height<<" flow="<<r.flow.width<<'x'<<r.flow.height<<" FG="<<r.fg.width<<'x'<<r.fg.height<<" output="<<r.output.width<<'x'<<r.output.height<<"\nepoch="<<e.identity.epoch<<" frame="<<e.identity.sourceFrameId<<" batch="<<e.batch<<" subframe="<<e.subframe<<" revision="<<e.identity.settingsRevision<<"\nruntime="<<e.runtimeHash<<" flow="<<e.flowApplied<<" fallback="<<e.fallbackReason<<"\n";
    }return diagnostics::redact(o.str());
}
void Logger::flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fflush(file_);
    }
}
} // namespace veyra
