#include "process.h"

#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

int main() {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / ("marp-process-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    auto log = dir / "output.txt";
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
        failures += !ok;
    };
    {
        mg::Process process;
        const std::string literal = "spaces 中文 'quote' \"double\" $HOME $(echo injected); &";
        check(process.Run({"/bin/sh", "-c", "printf '%s' \"$1\"", "test", literal}, log) == 0,
              "argument process succeeds");
        std::ifstream in(log); std::stringstream text; text << in.rdbuf();
        check(text.str() == literal, "arguments remain literal with Unicode and shell metacharacters");
        check(process.Run({"/bin/sh", "-c", "exit 7"}, log) == 7, "exit status preserved");
        check(process.Run({"/no/such/marp-executable"}, log) < 0, "spawn failure reported");
    }
    {
        mg::Process process;
        auto running = std::async(std::launch::async, [&] {
            return process.Run({"/bin/sh", "-c", "echo started; sleep 30 & wait"}, log);
        });
        for (int i = 0; i < 100 && (!fs::exists(log) || fs::file_size(log) == 0); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto start = std::chrono::steady_clock::now();
        process.Cancel();
        check(running.get() != 0, "in-flight child cancelled");
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "shutdown is prompt");
        check(process.Run({"/bin/echo", "must not start"}, log) == -1, "spawn after cancellation prevented");
    }
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
