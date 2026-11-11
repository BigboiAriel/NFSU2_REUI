// See assets_joyride.h.
#include "assets_joyride.h"
#include "assets.h"
#include "bpk.h"
#include "log.h"
#include "swf_fonts.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace assets_joyride {
namespace {

std::string g_dir;
std::filesystem::path g_bpk;
std::atomic<bool> g_stop{false};

// ---------------------------------------------------------------- the menus' pictures

constexpr int kCacheVersion = 2;   // 2: also keeps the menus' font movie (gfxfontlib.gfx: the game's fonts)
struct Img { uint64_t off; uint32_t size; };
std::vector<Img> g_img;                   // by Tex::id (written by the scan before uied sees the index)
std::filesystem::path g_bin;
FILE* g_binFile = nullptr;                // (the game thread's)

uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t Be64(const uint8_t* p) { return (uint64_t)Be32(p) << 32 | Be32(p + 4); }
uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// A "9KPB" pack's directory: header (0x30 bytes, little-endian; file count at 0x10), then 64-byte big-endian entries:
// u32 0x40, u32 0x20000, u64 offset, u64 size, u64, u32, u32 hash, char[4] extension, u32 hash, u64 name offset
// (-> u32 length + path), u64 FILETIME.
struct Entry { std::string name; uint64_t off, size; char ext[5]; };
// false: `d` (n bytes) does not hold the whole directory yet (or is not a pack: bad = true)
bool Directory(const uint8_t* d, size_t n, std::vector<Entry>& out, bool& bad) {
    bad = false;
    if (n < 0x30) return false;
    if (memcmp(d, "9KPB", 4) != 0) { bad = true; return false; }
    const uint32_t count = Le32(d + 0x10);
    if (count > 1000000) { bad = true; return false; }
    if (0x30 + (uint64_t)count * 64 > n) return false;
    out.clear();
    out.reserve(count);
    for (uint32_t k = 0; k < count; ++k) {
        const uint8_t* e = d + 0x30 + 64 * k;
        Entry x;
        x.off = Be64(e + 8); x.size = Be64(e + 16);
        memcpy(x.ext, e + 40, 4); x.ext[4] = 0;
        const uint64_t no = Be64(e + 48);
        if (no + 4 > n) return false;
        const uint32_t len = Be32(d + no);
        if (len > 4096) { bad = true; return false; }
        if (no + 4 + len > n) return false;
        x.name.assign((const char*)d + no + 4, strnlen((const char*)d + no + 4, len));
        out.push_back(std::move(x));
    }
    return true;
}

std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

std::string CacheKey() {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(g_bpk, ec);
    const auto t = std::filesystem::last_write_time(g_bpk, ec).time_since_epoch().count();
    char b[128];
    snprintf(b, sizeof b, "REUI-JRT-IMAGES\t%d\t%llu\t%lld", kCacheVersion, (unsigned long long)size, (long long)t);
    return b;
}

struct Line { uint64_t off; uint32_t size, w, h, fmt; int hud; std::string pack, name; };

bool LoadIndex(const std::filesystem::path& idx, const std::string& key, std::vector<Line>& lines) {
    FILE* f = _wfopen(idx.wstring().c_str(), L"rb");
    if (!f) return false;
    char buf[1024];
    bool ok = fgets(buf, sizeof buf, f) && std::string(buf).substr(0, key.size()) == key &&
              (buf[key.size()] == '\n' || buf[key.size()] == '\r');
    while (ok && fgets(buf, sizeof buf, f)) {
        Line l{};
        char pack[512], name[512];
        unsigned long long off;
        if (sscanf(buf, "%llu\t%u\t%u\t%u\t%x\t%d\t%511[^\t]\t%511[^\t\r\n]", &off, &l.size, &l.w, &l.h, &l.fmt, &l.hud,
                   pack, name) != 8) { ok = false; break; }
        l.off = off; l.pack = pack; l.name = name;
        lines.push_back(std::move(l));
    }
    fclose(f);
    return ok && !lines.empty();
}

// Decodes data.bpk up to the end of the last menu pack, keeping the .dds images' pixels.
bool BuildCache(const std::filesystem::path& bin, const std::filesystem::path& idx, const std::string& key,
                std::vector<Line>& lines) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::filesystem::path binTmp = bin.wstring() + L".tmp", idxTmp = idx.wstring() + L".tmp";
    FILE* out = _wfopen(binTmp.wstring().c_str(), L"wb");
    if (!out) { reui::Log("REUI: cannot write %s", binTmp.string().c_str()); return false; }
    uint64_t outPos = 0;
    int packs = 0, badPacks = 0, badImages = 0;

    std::vector<uint8_t> head;            // the outer directory (the start of the contents)
    std::vector<Entry> all, menus;        // menus: data\fe\**\*.bpk, by offset
    bool haveDir = false;
    size_t cur = 0;                       // the next menu pack to collect
    std::vector<uint8_t> pack;            // its bytes

    auto Pack = [&](const Entry& m) {     // one menu pack collected: keep its images
        std::vector<Entry> files;
        bool bad = false;
        if (!Directory(pack.data(), pack.size(), files, bad)) { ++badPacks; return; }
        ++packs;
        std::string movie = m.name.substr(8, m.name.size() - 8 - 4);   // data\fe\ ... .bpk
        for (auto& c : movie) if (c == '\\') c = '/';
        const bool hud = Lower(movie).rfind("hud/", 0) == 0;
        if (Lower(movie) == "gfxfontlib")   // the menus' fonts: kept whole, made into TrueType files (ScanFonts)
            for (auto& e : files)
                if (Lower(e.ext).compare(0, 3, "gfx") == 0 && e.off + e.size <= pack.size()) {
                    const std::wstring f = (bin.parent_path() / L"gfxfontlib.gfx").wstring();
                    if (FILE* o = _wfopen(f.c_str(), L"wb")) { fwrite(pack.data() + e.off, 1, (size_t)e.size, o); fclose(o); }
                }
        for (auto& e : files) {
            if (Lower(e.ext).compare(0, 3, "dds") != 0) continue;
            if (e.off + e.size > pack.size() || e.size < 128) { ++badImages; continue; }
            const uint8_t* d = pack.data() + e.off;
            if (memcmp(d, "DDS ", 4) != 0) { ++badImages; continue; }
            const uint32_t h = Le32(d + 12), w = Le32(d + 16), fourcc = Le32(d + 84);
            const uint32_t blockBytes = fourcc == 0x31545844 ? 8 : (fourcc == 0x33545844 || fourcc == 0x35545844) ? 16 : 0;
            if (!blockBytes || !w || !h || w > 8192 || h > 8192) { ++badImages; continue; }
            const uint32_t bytes = ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
            if (128 + (uint64_t)bytes > e.size) { ++badImages; continue; }
            if (fwrite(d + 128, 1, bytes, out) != bytes) { ++badImages; continue; }
            std::string name = e.name.substr(e.name.find_last_of('\\') + 1);
            if (Lower(name).rfind("bpk_fe_", 0) == 0) name = name.substr(7);
            if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".dds") name.resize(name.size() - 4);
            lines.push_back({outPos, bytes, w, h, fourcc, hud ? 1 : 0, movie, name});
            outPos += bytes;
        }
    };

    std::string err;
    const bool ok = bpk::Stream(g_bpk, [&](const uint8_t* data, size_t size, uint64_t at) {
        if (g_stop) return false;
        if (!haveDir) {
            if (head.size() < (64u << 20)) head.insert(head.end(), data, data + size);
            bool bad = false;
            if (Directory(head.data(), head.size(), all, bad)) {
                haveDir = true;
                for (auto& e : all)
                    if (Lower(e.name).rfind("data\\fe\\", 0) == 0 && Lower(e.ext).compare(0, 3, "bpk") == 0 && e.size)
                        menus.push_back(e);
                std::sort(menus.begin(), menus.end(), [](const Entry& a, const Entry& b) { return a.off < b.off; });
                head.clear(); head.shrink_to_fit();
                // (menu packs that started before the directory was complete: the cache misses them)
                while (cur < menus.size() && menus[cur].off < at) { ++badPacks; ++cur; }
            } else if (bad || head.size() >= (64u << 20)) {
                err = "data.bpk's contents are not a 9KPB pack";
                return false;
            }
        }
        if (!haveDir) return true;
        const uint64_t end = at + size;
        while (cur < menus.size() && menus[cur].off < end) {
            const Entry& m = menus[cur];
            if (pack.size() != m.size) pack.assign((size_t)m.size, 0);
            const uint64_t a = std::max(m.off, at), b = std::min(m.off + m.size, end);
            memcpy(pack.data() + (a - m.off), data + (a - at), (size_t)(b - a));
            if (m.off + m.size > end) break;   // continues in the next block
            Pack(m);
            pack.clear();
            ++cur;
        }
        return cur < menus.size();             // stop after the last menu pack
    }, err);
    fclose(out);
    if (!ok || g_stop || lines.empty()) {
        _wremove(binTmp.wstring().c_str());
        if (!g_stop) reui::Log("REUI: menu pictures from %s failed: %s", g_bpk.string().c_str(), err.empty() ? "no images" : err.c_str());
        lines.clear();
        return false;
    }
    FILE* fi = _wfopen(idxTmp.wstring().c_str(), L"wb");
    if (!fi) { _wremove(binTmp.wstring().c_str()); lines.clear(); return false; }
    fprintf(fi, "%s\n", key.c_str());
    for (auto& l : lines)
        fprintf(fi, "%llu\t%u\t%u\t%u\t%x\t%d\t%s\t%s\n", (unsigned long long)l.off, l.size, l.w, l.h, l.fmt, l.hud,
                l.pack.c_str(), l.name.c_str());
    fclose(fi);
    // the index last: a cache without its index is rebuilt
    _wremove(idx.wstring().c_str());
    MoveFileExW(binTmp.wstring().c_str(), bin.wstring().c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(idxTmp.wstring().c_str(), idx.wstring().c_str(), MOVEFILE_REPLACE_EXISTING);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    reui::Log("REUI: menu pictures: %d images from %d packs in %.1f s (%d packs / %d images skipped) -> %s",
              (int)lines.size(), packs, s, badPacks, badImages, bin.string().c_str());
    return true;
}

void ScanImages(assets::Index& out) {
    g_img.clear();
    if (g_bpk.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(g_bpk, ec)) { reui::Log("REUI: no %s: no menu pictures", g_bpk.string().c_str()); return; }
    const std::filesystem::path cache = std::filesystem::path(g_dir) / "cache";
    std::filesystem::create_directories(cache, ec);
    const std::filesystem::path bin = cache / "fe_images.bin", idx = cache / "fe_images.idx";
    const std::string key = CacheKey();
    std::vector<Line> lines;
    if (!LoadIndex(idx, key, lines)) {
        lines.clear();
        reui::Log("REUI: building the menu picture cache from %s (once)", g_bpk.string().c_str());
        if (!BuildCache(bin, idx, key, lines)) return;
    }
    g_bin = bin;
    for (auto& l : lines) {
        assets::Tex t;
        t.name = l.name; t.pack = l.pack; t.source = "data.bpk";
        t.w = (uint16_t)l.w; t.h = (uint16_t)l.h; t.fmt = l.fmt; t.hud = l.hud != 0;
        t.id = (int)g_img.size();
        g_img.push_back({l.off, l.size});
        out.tex.push_back(std::move(t));
    }
}

bool Pixels(const assets::Tex& t, gfx::Format& fmt, std::vector<uint8_t>& px, int& pitch) {
    if (t.id < 0 || t.id >= (int)g_img.size()) return false;
    if (!g_binFile) g_binFile = _wfopen(g_bin.wstring().c_str(), L"rb");
    if (!g_binFile) return false;
    const Img& im = g_img[t.id];
    px.resize(im.size);
    if (_fseeki64(g_binFile, (long long)im.off, SEEK_SET) || fread(px.data(), 1, im.size, g_binFile) != im.size) return false;
    const bool dxt1 = t.fmt == 0x31545844;
    fmt = dxt1 ? gfx::DXT1 : t.fmt == 0x33545844 ? gfx::DXT3 : gfx::DXT5;
    pitch = (int)(((t.w + 3) / 4) * (dxt1 ? 8 : 16));
    return true;
}

// ---------------------------------------------------------------- vector fonts

void AddFont(assets::Index& out, const std::string& name, const std::string& path, const std::string& source) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    for (auto& f : out.fonts) if (f.name == name) return;
    assets::Font f; f.name = name; f.file = path; f.source = source; f.line = 32;
    out.fonts.push_back(f);
}

// The game's own fonts (Franklin Gothic Demi / Medium Cond, Amasis Black Italic, ...): made from the player's
// gfxfontlib.gfx (kept by the picture cache) into <data>\cache\fonts\*.ttf once, named as the menus name them
// ("$XR_TITLE", "$XR_NORMAL", "$XR_MAIN_HEADER", ...).
void GameFonts(assets::Index& out) {
    const std::filesystem::path cache = std::filesystem::path(g_dir) / "cache", gfx = cache / "gfxfontlib.gfx", dir = cache / "fonts";
    std::error_code ec;
    if (!std::filesystem::exists(gfx, ec)) return;
    std::filesystem::create_directories(dir, ec);
    const auto marker = dir / "XR_TITLE.ttf";
    if (!std::filesystem::exists(marker, ec) || std::filesystem::last_write_time(marker, ec) < std::filesystem::last_write_time(gfx, ec)) {
        std::vector<uint8_t> movie;
        if (FILE* f = _wfopen(gfx.wstring().c_str(), L"rb")) {
            fseek(f, 0, SEEK_END); movie.resize((size_t)ftell(f)); fseek(f, 0, SEEK_SET);
            if (fread(movie.data(), 1, movie.size(), f) != movie.size()) movie.clear();
            fclose(f);
        }
        std::vector<std::pair<std::string, std::string>> made; std::string err;
        if (swf_fonts::ToTtf(movie, dir.string() + "\\", made, err)) reui::Log("REUI: the game's fonts: %d made in %s", (int)made.size(), dir.string().c_str());
        else reui::Log("REUI: the game's fonts from %s failed: %s", gfx.string().c_str(), err.c_str());
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir.string() + "\\*.ttf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::string n = fd.cFileName;
        AddFont(out, "$" + n.substr(0, n.rfind('.')), dir.string() + "\\" + n, "the game (data.bpk)");
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void ScanFonts(assets::Index& out) {
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

void Scan(assets::Index& out) {
    ScanImages(out);   // (first: building its cache keeps the game's font movie)
    GameFonts(out);
    ScanFonts(out);
}

} // namespace

void Install(const std::string& dataDir, const std::filesystem::path& bpk) {
    g_dir = dataDir;
    g_bpk = bpk;
    g_stop = false;
    assets::source.scan = Scan;
    assets::source.pixels = Pixels;
}

void Shutdown() {
    g_stop = true;
    if (g_binFile) { fclose(g_binFile); g_binFile = nullptr; }
}

} // namespace assets_joyride
