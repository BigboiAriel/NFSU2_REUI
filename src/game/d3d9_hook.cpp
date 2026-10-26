#include "d3d9_hook.h"

#include "../log.h"
#include "../panel.h"
#include "addresses.h"

#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>

namespace uiedit {

using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using EndSceneFn = HRESULT(WINAPI*)(IDirect3DDevice9*);

constexpr int kVtReset = 16;
constexpr int kVtPresent = 17;
constexpr int kVtEndScene = 42;

// Inline hooks on the functions d3d9.dll's vtable points to. Other mods that swap vtable entries
// still end up calling these functions, so we run no matter who else hooks Present/Reset.
static PresentFn g_origPresent = nullptr;
static ResetFn g_origReset = nullptr;
static void* g_presentTarget = nullptr;
static void* g_resetTarget = nullptr;
static bool g_firstFrame = true;
static volatile LONG g_presentCount = 0;
static volatile LONG g_endSceneCount = 0;
static EndSceneFn g_origEndScene = nullptr;
static bool g_inOurScene = false;
// If Present never reaches us (e.g. the game presents through a swap chain), draw from the
// last EndScene before each frame instead. Decided after 3 seconds of frames.
static bool g_useEndScene = false;
static DWORD g_lastEndSceneFrameTick = 0;

static const char* ModuleOf(void* p) {
    static char name[MAX_PATH];
    HMODULE m = nullptr;
    name[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m) && m) {
        char full[MAX_PATH];
        GetModuleFileNameA(m, full, MAX_PATH);
        const char* s = strrchr(full, '\\');
        lstrcpynA(name, s ? s + 1 : full, MAX_PATH);
    } else {
        lstrcpyA(name, "?");
    }
    return name;
}

static void OurFrame(IDirect3DDevice9* dev, bool insideScene) {
    if (g_firstFrame) {
        g_firstFrame = false;
        Log("first Present (device %p)", dev);
        // First game frame: safe place to start ImGui (game thread, not DllMain).
        D3DDEVICE_CREATION_PARAMETERS cp{};
        dev->GetCreationParameters(&cp);
        HWND hwnd = cp.hFocusWindow;
        if (!hwnd) {
            IDirect3DSwapChain9* sc = nullptr;
            if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
                D3DPRESENT_PARAMETERS pp{};
                sc->GetPresentParameters(&pp);
                hwnd = pp.hDeviceWindow;
                sc->Release();
            }
        }
        PanelInit(hwnd, dev);
    }
    PanelPollKey();
    if (!PanelWantsFrame()) return;
    if (insideScene) {
        PanelFrame();
    } else if (SUCCEEDED(dev->BeginScene())) {
        g_inOurScene = true;
        PanelFrame();
        dev->EndScene();
        g_inOurScene = false;
    }
}

static HRESULT WINAPI HookPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
    InterlockedIncrement(&g_presentCount);
    if (!g_useEndScene) OurFrame(dev, false);
    return g_origPresent(dev, src, dst, wnd, dirty);
}

static HRESULT WINAPI HookEndScene(IDirect3DDevice9* dev) {
    InterlockedIncrement(&g_endSceneCount);
    if (g_useEndScene && !g_inOurScene) {
        // At most once per ~frame: only draw into the back buffer's scene, roughly 60 Hz.
        DWORD now = GetTickCount();
        if (now - g_lastEndSceneFrameTick >= 15) {
            g_lastEndSceneFrameTick = now;
            OurFrame(dev, true);
        }
    }
    return g_origEndScene(dev);
}

static HRESULT WINAPI HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    PanelBeforeReset();
    HRESULT hr = g_origReset(dev, pp);
    if (SUCCEEDED(hr)) PanelAfterReset();
    Log("device Reset -> 0x%08lX", (unsigned long)hr);
    return hr;
}

static DWORD WINAPI WaitThread(LPVOID) {
    IDirect3DDevice9* dev = nullptr;
    for (int i = 0; i < 1200 && !dev; ++i) {  // up to 2 minutes
        dev = *reinterpret_cast<IDirect3DDevice9**>(addr::D3DDevice);
        if (!dev) Sleep(100);
    }
    if (!dev) {
        Log("no IDirect3DDevice9 at 0x%X after 2 minutes, giving up", (unsigned)addr::D3DDevice);
        return 0;
    }
    void** vt = *reinterpret_cast<void***>(dev);
    g_presentTarget = vt[kVtPresent];
    g_resetTarget = vt[kVtReset];
    void* endSceneTarget = vt[kVtEndScene];
    Log("device %p vtable %p: Present %p (%s)", dev, vt, g_presentTarget, ModuleOf(g_presentTarget));
    Log("                       Reset   %p (%s)", g_resetTarget, ModuleOf(g_resetTarget));

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed: %s", MH_StatusToString(st));
        return 0;
    }
    st = MH_CreateHook(g_presentTarget, (void*)&HookPresent, (void**)&g_origPresent);
    Log("hook Present: %s", MH_StatusToString(st));
    st = MH_CreateHook(g_resetTarget, (void*)&HookReset, (void**)&g_origReset);
    Log("hook Reset: %s", MH_StatusToString(st));
    st = MH_CreateHook(endSceneTarget, (void*)&HookEndScene, (void**)&g_origEndScene);
    Log("hook EndScene %p (%s): %s", endSceneTarget, ModuleOf(endSceneTarget), MH_StatusToString(st));
    st = MH_EnableHook(MH_ALL_HOOKS);
    Log("enable hooks: %s", MH_StatusToString(st));

    // Diagnostics: confirm frames reach us, and watch for the device being replaced.
    for (int i = 0; i < 30; ++i) {
        Sleep(1000);
        if (i == 2 && g_presentCount == 0 && g_endSceneCount > 0) {
            g_useEndScene = true;
            Log("Present not reaching us (EndScene %ld calls); drawing from EndScene instead", (long)g_endSceneCount);
        }
        if (i == 4 || i == 14 || i == 29) {
            IDirect3DDevice9* now = *reinterpret_cast<IDirect3DDevice9**>(addr::D3DDevice);
            void** nvt = now ? *reinterpret_cast<void***>(now) : nullptr;
            Log("after %ds: %ld Present / %ld EndScene calls, device %p, vtable %p, vt[Present] %p (%s)", i + 1, (long)g_presentCount, (long)g_endSceneCount, now, nvt,
                nvt ? nvt[kVtPresent] : nullptr, nvt ? ModuleOf(nvt[kVtPresent]) : "-");
        }
    }
    return 0;
}

void StartD3D9Hook() {
    HANDLE h = CreateThread(nullptr, 0, WaitThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

void StopD3D9Hook() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

} // namespace uiedit
