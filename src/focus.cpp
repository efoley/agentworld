#include "focus.h"

#include <cstdlib>
#include <filesystem>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/stat.h>
#endif

extern char** environ;

namespace {

std::string ttyOf(int pid) {
#ifdef __APPLE__
    proc_bsdinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) != sizeof info) return "";
    if (info.e_tdev == static_cast<uint32_t>(NODEV)) return "";
    const char* name = devname(info.e_tdev, S_IFCHR);
    return name ? std::string("/dev/") + name : "";
#else
    std::error_code ec;
    auto target = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/fd/0", ec);
    return ec ? "" : target.string();
#endif
}

int run(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t child;
    if (posix_spawnp(&child, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return -1;
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#ifdef __APPLE__
// Claude Code titles its terminal "<spinner glyph> <session name>".
const char* kGhosttyScript = R"(
on run argv
    set wanted to item 1 of argv
    set cwd to item 2 of argv
    tell application "Ghostty"
        set fallback to missing value
        repeat with t in terminals
            set n to name of t
            if n is wanted or n ends with (" " & wanted) then
                focus t
                activate
                return "title"
            end if
            if fallback is missing value and working directory of t is cwd then set fallback to t
        end repeat
        if fallback is not missing value then
            focus fallback
            activate
            return "cwd"
        end if
    end tell
    error "no terminal for " & wanted
end run
)";
#endif

}  // namespace

void focusAgent(const Agent& agent) {
    Agent a = agent;
    std::thread([a] {
        const char* home = std::getenv("HOME");
        std::string hook = std::string(home ? home : "") + "/.config/agentworld/focus";
        if (access(hook.c_str(), X_OK) == 0 &&
            run({hook, a.name, a.cwd, std::to_string(a.pid), ttyOf(a.pid)}) == 0)
            return;
#ifdef __APPLE__
        run({"osascript", "-e", kGhosttyScript, a.name, a.cwd});
#endif
    }).detach();
}

void openInClaudeDesktop(const Agent& agent) {
    if (agent.bridgeSessionId.empty()) return;
    std::string url = "claude://code/" + agent.bridgeSessionId;
    std::thread([url] {
#ifdef __APPLE__
        run({"open", url});
#else
        run({"xdg-open", url});
#endif
    }).detach();
}
