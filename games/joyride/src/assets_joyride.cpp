// See assets_joyride.h.
#include "assets_joyride.h"
#include "assets.h"

#include <windows.h>
#include <string>
#include <vector>

namespace assets_joyride {
namespace {

std::string g_dir;

void AddFont(assets::Index& out, const std::string& name, const std::string& path, const std::string& source) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    for (auto& f : out.fonts) if (f.name == name) return;
    assets::Font f; f.name = name; f.file = path; f.source = source; f.line = 32;
    out.fonts.push_back(f);
}

void Scan(assets::Index& out) {
    // REUI's own fonts folder: <data>\fonts\*.ttf / *.otf (named by the file)
    std::string fonts = g_dir + "fonts\\";
    CreateDirectoryA(fonts.c_str(), nullptr);
    for (const char* pat : {"*.ttf", "*.otf"}) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((fonts + pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string n = fd.cFileName;
            AddFont(out, n.substr(0, n.rfind('.')), fonts + n, "REUI\\fonts");
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    // Windows' own (on every Windows 10 / 11 PC; layouts using them work without shipping a font)
    static const char* kWin[][2] = {
        {"Bahnschrift", "bahnschrift.ttf"}, {"Segoe UI", "segoeui.ttf"}, {"Segoe UI Bold", "segoeuib.ttf"},
        {"Segoe UI Black", "seguibl.ttf"}, {"Arial Black", "ariblk.ttf"}, {"Impact", "impact.ttf"},
    };
    char win[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(win, MAX_PATH);
    for (auto& w : kWin) AddFont(out, w[0], std::string(win) + "\\Fonts\\" + w[1], "Windows");
}

} // namespace

void Install(const std::string& dataDir) {
    g_dir = dataDir;
    assets::source.scan = Scan;
    assets::source.pixels = nullptr;
}

} // namespace assets_joyride
