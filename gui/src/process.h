#pragma once

#include "platform.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace mg {

// Run on a worker thread; Cancel may be called by the owning UI thread.
// Cancellation is permanent so shutdown cannot race a subsequent spawn.
class Process {
public:
    ~Process() { Cancel(); }
    int Run(const std::vector<std::string>& args, const std::filesystem::path& log);
    void Cancel();
private:
    std::mutex mutex_;
    bool cancelled_ = false;
#if PLATFORM_WINDOWS
    void* child_ = nullptr;
    void* job_ = nullptr;
#else
    int pid_ = -1;
#endif
};

// Configure once before starting threads, including Finder's minimal PATH.
void ConfigureMarpEnvironment(const std::filesystem::path& resourceRoot);
std::vector<std::string> MarpCommand();

} // namespace mg
