// Mithril-Wrapper - MG_State/Config.cpp
#include "Config.h"

#include "../MG_Impl/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace {

std::unordered_map<std::string, std::string>& table() {
    static std::unordered_map<std::string, std::string> t;
    return t;
}

void trim(std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { s.clear(); return; }
    const size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
}

// key=value lines, '#' starts a comment. Unknown keys are kept but ignored;
// a malformed line is skipped rather than aborting the whole file.
void parse_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        std::string k(p), v(eq + 1);
        trim(k); trim(v);
        if (!k.empty()) table()[k] = v;
    }
    std::fclose(f);
}

std::string join(const char* a, const char* b) {
    std::string s(a ? a : "");
    if (!s.empty() && s.back() != '/') s += '/';
    s += b;
    return s;
}

std::vector<std::string> candidates() {
    std::vector<std::string> v;
    if (const char* e = std::getenv("MITHRIL_CONFIG")) v.push_back(e);
    const char* poj = std::getenv("POJAV_HOME");
    if (poj) v.push_back(join(poj, "mithril/mithril.conf"));
    const char* home = std::getenv("HOME");
    if (home) {
        v.push_back(join(home, "Documents/mithril/mithril.conf"));
        v.push_back(join(home, "mithril/mithril.conf"));
    }
    return v;
}

bool loaded = false;

} // namespace

void config_load(void) {
    if (loaded) return;
    loaded = true;
    const std::vector<std::string> paths = candidates();
    for (const std::string& p : paths) {
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) continue;
        std::fclose(f);
        parse_file(p.c_str());
        MITHRIL_LOG_INFO("config", "loaded %s (%zu keys)", p.c_str(), table().size());
        return;
    }
    if (!paths.empty())
        MITHRIL_LOG_INFO("config", "no config file found; tried %s",
                         paths.front().c_str());
}

const char* config_get(const char* key) {
    if (!key) return nullptr;
    config_load();
    auto it = table().find(key);
    return it == table().end() ? nullptr : it->second.c_str();
}

} // namespace mithril
