// gui/src/crash.h — crash logging for marp_gui.
//
// On an unhandled SEH exception or CRT abort: symbolize and log the crashing
// callstack straight into the log system (so a crash is readable without a
// debugger), and write a minidump to <exe>/crash/crash_<pid>.dmp for offline
// analysis. Symbols come from the module's PDB — always generated (see
// CMakeLists), so this resolves function/file/line for our own frames.

#pragma once

#include "log.h"

#if PLATFORM_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace mg {

// One-time dbghelp init for symbolized stack walks.
static bool InitSymbols() {
    static bool done = false, ok = false;
    if (done) return ok;
    done = true;
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES |
                  SYMOPT_UNDNAME | SYMOPT_INCLUDE_32BIT_MODULES);
    ok = SymInitialize(proc, nullptr, TRUE) == TRUE;
    return ok;
}

// Walk the crashing thread's context and log symbolized frames.
static void LogStackTrace(EXCEPTION_POINTERS *ep) {
    if (!InitSymbols()) {
        LOGE << "(symbols unavailable — PDB not found)";
        return;
    }
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    CONTEXT ctx = ep && ep->ContextRecord ? *ep->ContextRecord : CONTEXT{};
    if (!ep || !ep->ContextRecord) {
        RtlCaptureContext(&ctx);
    }

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, &ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 addr = frame.AddrPC.Offset;

        // symbol name
        char symBuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        const char *name = "<no symbol>";
        if (SymFromAddr(proc, addr, &disp, sym)) name = sym->Name;

        // file:line
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        const char *file = nullptr;
        unsigned lineno = 0;
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
            file = line.FileName;
            lineno = line.LineNumber;
        }

        std::stringstream ss;
        ss << "  [" << i << "] " << name;
        if (file) {
            // keep just the tail of the path for readability
            const char *base = strrchr(file, '\\');
            ss << "  (" << (base ? base + 1 : file) << ":" << lineno << ")";
        } else {
            ss << "  @ 0x" << std::hex << addr << std::dec;
        }
        LOGE << ss.str();
    }
}

static LONG WINAPI CrashUnhandledException(EXCEPTION_POINTERS *ep) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void *addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    LOGE << "CRASH: exception 0x" << std::hex << code << " at " << addr;
    LogStackTrace(ep);

    // minidump next to the exe (NOT .gui-build — the build worker wipes it).
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
    LogStackTrace(nullptr);
    std::signal(sig, SIG_DFL);  // don't re-enter on the abort below
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
#include <unistd.h>
namespace mg {
static void CrashAbortHandler(int sig) {
    // Logging takes locks and allocates memory, which is unsafe in a signal
    // handler. Reset SIGABRT before aborting so it cannot recurse.
    const char *message = sig == SIGSEGV ? "marp_gui: fatal SIGSEGV\n" : "marp_gui: fatal signal\n";
    size_t length = sig == SIGSEGV ? 24 : 23;
    (void)!write(STDERR_FILENO, message, length);
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}
inline void InstallCrashHandler() {
    std::signal(SIGABRT, CrashAbortHandler);
    std::signal(SIGSEGV, CrashAbortHandler);
}
} // namespace mg
#endif
