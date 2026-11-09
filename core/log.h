#pragma once
#include <string>

namespace reui {

// Opens scripts\NFSU2_REUI.log next to the .asi (overwritten each launch).
void LogOpen(const std::string& path, bool enabled);
void Log(const char* fmt, ...);
void LogClose();

} // namespace reui
