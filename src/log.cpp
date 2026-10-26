#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace uiedit {

static FILE* g_file = nullptr;
static CRITICAL_SECTION g_lock;
static bool g_lockInit = false;

void LogOpen(const std::string& path, bool enabled) {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
    if (!enabled) return;
    g_file = fopen(path.c_str(), "w");
}

void Log(const char* fmt, ...) {
    if (!g_file) return;
    EnterCriticalSection(&g_lock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
    fflush(g_file);
    LeaveCriticalSection(&g_lock);
}

void LogClose() {
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

} // namespace uiedit
