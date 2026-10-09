// gui/src/log.h — thread-safe stream logger.
//
// plog-inspired: FURYD << "msg " << value;  builds a Record and flushes it on
// destruction. Console output is always on; file output is toggled at runtime
// (Settings "Log to file"). An optional extra sink mirrors lines (unused now,
// could feed an in-app console later).
//
// plog-inspired: console always on, file output toggled at runtime.

#pragma once

#include "platform.h"

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>

namespace mg { // marp-gui

enum class LogLevel : int { EROR = 0, WARN = 1, INFO = 2, DBUG = 3 };

class Record {
public:
    std::string level;
    std::string func;
    std::string file;
    size_t line = 0;
    std::stringstream stream;

    Record(LogLevel level, const char *function, const char *file, int line)
        : func(function), file(file), line(line) {
        switch (level) {
        case LogLevel::DBUG: this->level = "DBUG"; break;
        case LogLevel::INFO: this->level = "INFO"; break;
        case LogLevel::WARN: this->level = "WARN"; break;
        case LogLevel::EROR: this->level = "EROR"; break;
        }
        auto start = func.find(' ') + 1;
        auto end = func.find('(');
        if (start != std::string::npos && end != std::string::npos && end > start)
            func = func.substr(start, end - start);
        auto s = this->file.find_last_of("\\/");
        if (s != std::string::npos) this->file = this->file.substr(s + 1);
    }

    template <typename T>
    Record &operator<<(const T &data) {
        stream << data;
        return *this;
    }
};

struct Formatter {
    static void Default(std::ostream &stream, const Record &record) {
        // timestamp
        using namespace std::chrono;
        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        tm tmv;
#if PLATFORM_WINDOWS
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char tb[32];
        snprintf(tb, sizeof(tb), "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min,
                 tmv.tm_sec, (int)ms.count());
        stream << "[" << tb << "]";
        stream << "[" << record.level << "]";
        stream << "[" << record.file << ":" << record.line << "][" << record.func << "]: ";
    }
};

using LogFormatter = std::function<void(std::ostream &, const Record &)>;
using LogSink = std::function<void(const Record &)>;

// Thread-safe logger. Console always on; file toggled via SetFileOutput.
class Log {
public:
    static Log &Instance() {
        static Log inst;
        return inst;
    }

    void SetLevel(LogLevel level) { m_LogLevel = level; }
    LogLevel GetLevel() const { return m_LogLevel; }

    // Toggle file output (Settings checkbox). Opens truncating on enable.
    void SetFileOutput(const std::string &path, bool append = false) {
        std::lock_guard<std::mutex> lock(m_StreamMutex);
        if (m_FileStream.is_open()) m_FileStream.close();
        m_FileOutput = false;
        if (!path.empty()) {
            m_FileStream.open(path, std::ofstream::out |
                                        (append ? std::ofstream::app : std::ofstream::trunc));
            m_FileOutput = m_FileStream.good();
        }
    }
    bool FileOutputEnabled() const { return m_FileOutput; }

    void SetExtraSink(LogSink sink) {
        std::lock_guard<std::mutex> lock(m_StreamMutex);
        m_ExtraSink = std::move(sink);
    }

    void operator+=(const Record &record) {
        std::lock_guard<std::mutex> lock(m_StreamMutex);
        // console always on
        m_Formatter(std::cout, record);
        std::cout << record.stream.str() << "\n";
        std::cout.flush();

        if (m_FileOutput) {
            m_Formatter(m_FileStream, record);
            m_FileStream << record.stream.str() << "\n";
            m_FileStream.flush();
        }

        if (m_ExtraSink) m_ExtraSink(record);
    }

private:
    Log() = default;
    std::mutex m_StreamMutex;
    std::ofstream m_FileStream;
    bool m_FileOutput = false;
    LogFormatter m_Formatter = Formatter::Default;
    // Default INFO: DBUG (verbose, e.g. shutdown timings) hidden unless enabled.
    LogLevel m_LogLevel = LogLevel::INFO;
    LogSink m_ExtraSink;
};

} // namespace mg

#ifdef _MSC_VER
#define MG_FUNC_NAME __FUNCTION__
#else
#define MG_FUNC_NAME __PRETTY_FUNCTION__
#endif

#define MG_LOG(level) \
    if (level <= mg::Log::Instance().GetLevel()) \
    mg::Log::Instance() += mg::Record(level, MG_FUNC_NAME, __FILE__, __LINE__)

#define LOGD MG_LOG(mg::LogLevel::DBUG)
#define LOGI MG_LOG(mg::LogLevel::INFO)
#define LOGW MG_LOG(mg::LogLevel::WARN)
#define LOGE MG_LOG(mg::LogLevel::EROR)
