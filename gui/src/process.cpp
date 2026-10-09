#include "process.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#if PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mg {
namespace fs = std::filesystem;

void ConfigureMarpEnvironment(const fs::path& root) {
    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
#if PLATFORM_WINDOWS
    path = (root / "node_modules" / ".bin").string() + ";" + path;
    _putenv_s("PATH", path.c_str());
#else
    // Search ancestors as well: Resources -> Contents -> .app -> build -> repo.
    for (fs::path p = root; !p.empty(); p = p.parent_path()) {
        if (fs::is_directory(p / "node_modules" / ".bin"))
            path = (p / "node_modules" / ".bin").string() + ":" + path;
        if (p == p.parent_path()) break;
    }
#if PLATFORM_MACOS
    path += ":/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin";
    if (const char* home = std::getenv("HOME"))
        path += ":" + (fs::path(home) / ".local" / "bin").string();
#endif
    setenv("PATH", path.c_str(), 1);
#endif
}

std::vector<std::string> MarpCommand() {
    // Prefer an installed Marp. npx fallback never downloads dependencies.
    const char* env = std::getenv("PATH");
    std::string path = env ? env : "";
#if PLATFORM_WINDOWS
    const char separator = ';';
    const char* executable = "marp.cmd";
#else
    const char separator = ':';
    const char* executable = "marp";
#endif
    size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find(separator, start);
        fs::path candidate = fs::path(path.substr(start, end - start)) / executable;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)
#if !PLATFORM_WINDOWS
            && access(candidate.c_str(), X_OK) == 0
#endif
        ) return {candidate.string()};
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {"npx", "--no-install", "marp"};
}

int Process::Run(const std::vector<std::string>& args, const fs::path& log) {
    if (args.empty()) return -1;
#if PLATFORM_WINDOWS
    // npm shims are .cmd files; pass quoted literal paths through cmd.exe.
    auto wide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
        out.pop_back();
        return out;
    };
    std::string command = "cmd.exe /d /s /c \"";
    for (auto& arg : args) {
        // A literal quote cannot occur in a Windows path.
        if (arg.find('"') != std::string::npos || arg.find('\n') != std::string::npos) return -1;
        command += "\"" + arg + "\" ";
    }
    command += "\"";
    std::wstring cmd = wide(command);
    PROCESS_INFORMATION pi{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_) return -1;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return -1;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = output;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        bool ok = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                                  &limits, sizeof(limits));
        if (ok) ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                    CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
        CloseHandle(output);
        if (!ok) { if (job) CloseHandle(job); return -1; }
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job);
            return -1;
        }
        child_ = pi.hProcess;
        job_ = job;
        ResumeThread(pi.hThread);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    std::lock_guard<std::mutex> lock(mutex_);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle((HANDLE)job_);
    child_ = job_ = nullptr;
    return cancelled_ ? -1 : (int)code;
#else
    pid_t child;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_) return -1;
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        posix_spawn_file_actions_init(&actions);
        posix_spawnattr_init(&attributes);
        int error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                                    O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (!error) error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        if (!error) error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        if (!error) error = posix_spawnattr_setpgroup(&attributes, 0);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        if (!error) error = posix_spawnp(&child, argv[0], &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (error) return -error;
        pid_ = child;
    }
    // Reap under the same lock as cancellation, preventing PID reuse races.
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            int status = 0;
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child || (result == -1 && errno != EINTR)) {
                pid_ = -1;
                if (result == -1 || cancelled_ || !WIFEXITED(status)) return -1;
                return WEXITSTATUS(status);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
}

void Process::Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
#if PLATFORM_WINDOWS
    if (job_) TerminateJobObject((HANDLE)job_, 1);
#else
    if (pid_ > 0) kill(-pid_, SIGKILL);
#endif
}

} // namespace mg
