// gui/src/crash.h — crash logging for marp_gui.
//
// On an unhandled SEH exception or CRT abort, write the exception info and a
// minidump to .gui-build/crash_*.log / .dmp, and flush the log. Lets us get a
// usable report instead of a silent exit.

#pragma once

#include "log.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace mg {

static LONG WINAPI CrashUnhandledException(EXCEPTION_POINTERS *ep) {
    // Write dumps next to the exe (NOT .gui-build — the build worker wipes it).
    std::string dir;
    {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string exeDir(exe);
        auto pos = exeDir.find_last_of("\\/");
        dir = (pos != std::string::npos ? exeDir.substr(0, pos) : ".") + "\\crash";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void *addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    LOGE << "CRASH: exception 0x" << std::hex << code << " at " << addr;

    // minidump
    char dmpPath[MAX_PATH];
    snprintf(dmpPath, sizeof(dmpPath), "%s\\crash_%lu.dmp", dir.c_str(),
             (unsigned long)GetCurrentProcessId());
    HANDLE hFile = CreateFileA(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei{};
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = ep;
        mdei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpNormal, &mdei, nullptr, nullptr);
        CloseHandle(hFile);
        LOGE << "minidump written: " << dmpPath;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void CrashAbortHandler(int sig) {
    LOGE << "CRASH: CRT signal " << sig << " (abort/assert)";
    std::abort();
}

// Install handlers. Call once at startup.
inline void InstallCrashHandler() {
    SetUnhandledExceptionFilter(CrashUnhandledException);
    std::signal(SIGABRT, CrashAbortHandler);
    std::signal(SIGSEGV, CrashAbortHandler);
}

} // namespace mg

#else // non-Windows: minimal signal logging
#include <csignal>
#include <cstdlib>
namespace mg {
static void CrashAbortHandler(int sig) {
    LOGE << "CRASH: signal " << sig;
    std::abort();
}
inline void InstallCrashHandler() {
    std::signal(SIGABRT, CrashAbortHandler);
    std::signal(SIGSEGV, CrashAbortHandler);
}
} // namespace mg
#endif
