#include "config.h"

#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace uiedit {

Config g_config;
std::string g_scriptsDir;

static std::string ReadString(const char* section, const char* key, const char* def, const std::string& ini) {
    char buf[256];
    GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), ini.c_str());
    char* semi = strchr(buf, ';');
    if (semi) *semi = 0;
    size_t n = strlen(buf);
    while (n && isspace((unsigned char)buf[n - 1])) buf[--n] = 0;
    char* p = buf;
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}
// "F6", "0x75", "117"
static int ParseKey(const std::string& s, int def) {
    if (s.empty()) return def;
    if ((s[0] == 'F' || s[0] == 'f') && s.size() > 1 && isdigit((unsigned char)s[1])) {
        int n = atoi(s.c_str() + 1);
        return n >= 1 && n <= 24 ? VK_F1 + n - 1 : def;
    }
    int v = (int)strtol(s.c_str(), nullptr, 0);
    return v > 0 && v < 256 ? v : def;
}

void LoadConfig(const std::string& ini) {
    g_config.panelKey = ParseKey(ReadString("General", "PanelKey", "F6", ini), VK_F6);
    g_config.menuKey = ParseKey(ReadString("General", "MenuKey", "F7", ini), VK_F7);
    std::string v = ReadString("General", "LogFile", "1", ini);
    g_config.logFile = v.empty() || v[0] != '0';
    v = ReadString("Hud", "ReplaceGameHud", "1", ini);
    g_config.replaceHud = v.empty() || v[0] != '0';
}

void IniSet(const char* section, const char* key, const std::string& value) {
    std::string path = g_scriptsDir + "NFSU2UIEditor.ini";
    std::vector<std::string> lines;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        std::string cur; int c;
        while ((c = fgetc(f)) != EOF) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += (char)c; }
        if (!cur.empty()) lines.push_back(cur);
        fclose(f);
    }
    auto trim = [](std::string s) {
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        size_t i = 0; while (i < s.size() && isspace((unsigned char)s[i])) ++i;
        return s.substr(i);
    };
    std::string want = std::string("[") + section + "]";
    int sec = -1, end = (int)lines.size();
    for (int i = 0; i < (int)lines.size(); ++i) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (sec >= 0) { end = i; break; }
            size_t close = t.find(']');   // "[Rumble]  ; comment"
            if (close != std::string::npos && !_stricmp(t.substr(0, close + 1).c_str(), want.c_str())) sec = i;
        }
    }
    bool cr = !lines.empty() && !lines[0].empty() && lines[0].back() == '\r';
    if (sec < 0) {
        lines.push_back(cr ? "\r" : "");
        lines.push_back(want + (cr ? "\r" : ""));
        lines.push_back(std::string(key) + " = " + value + (cr ? "\r" : ""));
    } else {
        bool done = false;
        for (int i = sec + 1; i < end && !done; ++i) {
            std::string& l = lines[i];
            size_t eq = l.find('=');
            if (eq == std::string::npos || trim(l)[0] == ';') continue;
            if (_stricmp(trim(l.substr(0, eq)).c_str(), key)) continue;
            // keep "key = " as written and the comment, at its column when the new value fits
            size_t vs = eq + 1; while (vs < l.size() && l[vs] == ' ') ++vs;
            size_t semi = l.find(';', vs);
            std::string comment = semi == std::string::npos ? "" : l.substr(semi);
            if (!comment.empty() && comment.back() == '\r') comment.pop_back();
            std::string nl = l.substr(0, vs) + value;
            while (!nl.empty() && nl.back() == ' ') nl.pop_back();   // an empty value: no padding piling up
            if (!comment.empty()) {
                size_t col = semi;
                nl += nl.size() + 1 < col ? std::string(col - nl.size(), ' ') : std::string(1, ' ');
                nl += comment;
            }
            l = nl + (cr ? "\r" : "");
            done = true;
        }
        if (!done) {
            int at = end;
            while (at > sec + 1 && trim(lines[at - 1]).empty()) --at;   // after the section's last line
            lines.insert(lines.begin() + at, std::string(key) + " = " + value + (cr ? "\r" : ""));
        }
    }
    std::string tmp = path + ".tmp";   // written whole, then swapped in
    if (FILE* f = fopen(tmp.c_str(), "wb")) {
        for (size_t i = 0; i < lines.size(); ++i) { fputs(lines[i].c_str(), f); fputc('\n', f); }
        bool ok = fclose(f) == 0;
        if (ok) MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        else DeleteFileA(tmp.c_str());
    }
}

} // namespace uiedit
