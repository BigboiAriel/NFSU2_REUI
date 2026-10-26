// NFSU2 UI Editor: an ImGui editor for recreating the game's HUD and menus out of its own textures and fonts.
#include "config.h"
#include "log.h"
#include "game/addresses.h"
#include "game/d3d9_hook.h"
#include "game/feng.h"

#include <windows.h>
#include <string>

using namespace uiedit;

static bool IsSupportedExe() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return false;
    return fa.nFileSizeHigh == 0 && fa.nFileSizeLow == addr::kExeSize;
}

static void Start(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string dir = path;
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    g_scriptsDir = dir;
    LoadConfig(dir + "NFSU2UIEditor.ini");
    LogOpen(dir + "NFSU2UIEditor.log", g_config.logFile);
    Log("NFSU2 UI Editor build " __DATE__ " " __TIME__);
    if (!IsSupportedExe()) {
        Log("unsupported SPEED2.EXE (expected v1.2 NA, %u bytes); doing nothing", (unsigned)addr::kExeSize);
        return;
    }
    FengInstall();
    StartD3D9Hook();
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Start(inst);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr) StopD3D9Hook();   // FreeLibrary only
        Log("unloading");
        LogClose();
    }
    return TRUE;
}
