// FEng package tracking: PackageInit 0x54E4A0 (virtual, thiscall(owner, FEPackage*), "Init Package %s") adds a package,
// PackageWillUnload 0x51C8C0 (thiscall(owner, FEPackage*) -> bool) drops it. FEPackage +0x0C name, +0x4C first object.
#include "feng.h"

#include "../log.h"

#include <MinHook.h>
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace reui {

namespace fe {
constexpr uintptr_t PackageInit = 0x54E4A0;
constexpr uintptr_t PackageWillUnload = 0x51C8C0;
}

struct Pkg { void* pkg; std::string name; };
static std::vector<Pkg> g_pkgs;

using InitFn = void(__fastcall*)(void* self, void* edx, void* pkg);
using UnloadFn = bool(__fastcall*)(void* self, void* edx, void* pkg);
static InitFn g_origInit = nullptr;
static UnloadFn g_origUnload = nullptr;

static void Drop(void* pkg) {
    g_pkgs.erase(std::remove_if(g_pkgs.begin(), g_pkgs.end(), [&](const Pkg& p) { return p.pkg == pkg; }), g_pkgs.end());
}
static void __fastcall InitHook(void* self, void* edx, void* pkg) {
    Drop(pkg);   // re-init, or a new package at a freed one's address
    const char* name = pkg ? *(const char**)((uint8_t*)pkg + 0x0C) : nullptr;
    g_pkgs.push_back({pkg, name && !IsBadReadPtr(name, 1) ? std::string(name) : std::string()});
    g_origInit(self, edx, pkg);
}
static bool __fastcall UnloadHook(void* self, void* edx, void* pkg) {
    Drop(pkg);   // its objects are about to be freed
    return g_origUnload(self, edx, pkg);
}

void FengInstall() {
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return;
    bool ok = MH_CreateHook((void*)fe::PackageInit, (void*)&InitHook, (void**)&g_origInit) == MH_OK &&
              MH_CreateHook((void*)fe::PackageWillUnload, (void*)&UnloadHook, (void**)&g_origUnload) == MH_OK &&
              MH_EnableHook((void*)fe::PackageInit) == MH_OK && MH_EnableHook((void*)fe::PackageWillUnload) == MH_OK;
    Log("feng: package tracking %s", ok ? "ready" : "failed");
}

std::vector<std::pair<void*, std::string>> FengPackages() {
    std::vector<std::pair<void*, std::string>> v;
    for (auto& p : g_pkgs) v.emplace_back(p.pkg, p.name);
    return v;
}

} // namespace reui
