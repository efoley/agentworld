#include "status.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string home() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

bool pidAlive(int pid) {
    return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
}

// Claude Code stores transcripts under ~/.claude/projects/<cwd with every
// non-alphanumeric character replaced by '-'>/<sessionId>.jsonl.
fs::path transcriptPath(const std::string& cwd, const std::string& sessionId) {
    std::string enc = cwd;
    for (char& c : enc)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '-';
    return fs::path(home()) / ".claude" / "projects" / enc / (sessionId + ".jsonl");
}

// Transcripts run to tens of MB, so only the tail is read.
std::vector<std::string> tailLines(const fs::path& p, std::streamoff maxBytes = 512 * 1024) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    std::streamoff start = std::max<std::streamoff>(0, size - maxBytes);
    f.seekg(start);
    std::string buf(static_cast<size_t>(size - start), '\0');
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));

    std::vector<std::string> lines;
    std::istringstream ss(buf);
    std::string line;
    if (start > 0) std::getline(ss, line);  // drop the partial first line
    while (std::getline(ss, line))
        if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Leads close a turn with a short block ("Still waiting on you:" and a few
// bullets), so a real question mark there means they want Eric's answer.
// A '?' followed by a letter or digit is a query string, and `code` spans
// don't count.
bool asksSomething(const std::string& block) {
    bool inCode = false;
    for (size_t i = 0; i < block.size(); ++i) {
        char c = block[i];
        if (c == '`') inCode = !inCode;
        if (c != '?' || inCode) continue;
        if (i + 1 == block.size() || !std::isalnum(static_cast<unsigned char>(block[i + 1]))) return true;
    }
    return false;
}

struct Ending {
    bool question = false;
    std::string text;
};

// Looks at the last conversational entry of an idle session. It is a question
// when the assistant's turn ended in an AskUserQuestion call, or its closing
// block asks something.
Ending lastTurn(const fs::path& transcript) {
    auto lines = tailLines(transcript);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        json e = json::parse(*it, nullptr, false);
        if (e.is_discarded()) continue;
        std::string type = e.value("type", "");
        if (type == "user") {
            // A human message after the last reply means nothing is pending;
            // a tool result means we're mid-turn, so keep looking back.
            auto& c = e["message"]["content"];
            bool toolResult = c.is_array() && !c.empty() && c[0].value("type", "") == "tool_result";
            if (!toolResult) return {};
            continue;
        }
        if (type != "assistant") continue;
        auto& content = e["message"]["content"];
        if (!content.is_array()) continue;
        for (auto b = content.rbegin(); b != content.rend(); ++b) {
            std::string bt = b->value("type", "");
            if (bt == "tool_use" && b->value("name", "") == "AskUserQuestion") {
                std::string q;
                auto& qs = (*b)["input"]["questions"];
                if (qs.is_array() && !qs.empty()) q = qs[0].value("question", "");
                return {true, q};
            }
            if (bt == "text") {
                std::string t = trim(b->value("text", ""));
                if (t.empty()) continue;
                size_t cut = t.rfind("\n\n");
                std::string block = trim(cut == std::string::npos ? t : t.substr(cut + 2));
                return {asksSomething(block), block.substr(0, 600)};
            }
        }
    }
    return {};
}

struct CachedEnding {
    fs::file_time_type mtime;
    uintmax_t size = 0;
    Ending ending;
};
std::map<std::string, CachedEnding> g_cache;

Ending lastTurnCached(const fs::path& p) {
    std::error_code ec;
    auto mtime = fs::last_write_time(p, ec);
    auto size = fs::file_size(p, ec);
    if (ec) return {};
    auto& c = g_cache[p.string()];
    if (c.mtime != mtime || c.size != size) c = {mtime, size, lastTurn(p)};
    return c.ending;
}

}  // namespace

const char* stateName(AgentState s) {
    switch (s) {
        case AgentState::Working: return "working";
        case AgentState::Question: return "has a question";
        default: return "idle";
    }
}

std::vector<Agent> readAgents(const std::string& root) {
    struct Found {
        Agent agent;
        int64_t updatedAt = 0;
    };
    std::map<std::string, Found> byName;

    std::error_code ec;
    fs::path dir = fs::path(home()) / ".claude" / "sessions";
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        json s = json::parse(f, nullptr, false);
        if (s.is_discarded()) continue;

        std::string cwd = s.value("cwd", "");
        if (cwd.rfind(root, 0) != 0) continue;
        if (s.value("nameSource", "") != "user") continue;  // leads are named sessions
        if (!pidAlive(s.value("pid", 0))) continue;

        Agent a;
        a.name = s.value("name", "");
        a.status = s.value("status", "");
        a.cwd = cwd;
        a.pid = s.value("pid", 0);
        a.bridgeSessionId = s.value("bridgeSessionId", "");
        int64_t updated = s.value("updatedAt", int64_t{0});

        if (a.status == "busy" || a.status == "shell") {
            a.state = AgentState::Working;
        } else if (a.status == "idle" || a.status.empty()) {
            Ending end = lastTurnCached(transcriptPath(cwd, s.value("sessionId", "")));
            a.state = end.question ? AgentState::Question : AgentState::Idle;
            a.detail = end.text;
        } else {
            // Anything else (a permission prompt, say) is waiting on Eric.
            a.state = AgentState::Question;
            a.detail = a.status;
        }

        auto& slot = byName[a.name];
        if (updated >= slot.updatedAt) slot = {std::move(a), updated};
    }

    std::vector<Agent> out;
    for (auto& [_, f] : byName) out.push_back(std::move(f.agent));
    return out;
}
