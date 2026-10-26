#pragma once
#include <string>

namespace uiedit {

// Opens scripts\NFSU2UIEditor.log next to the .asi (overwritten each launch).
void LogOpen(const std::string& path, bool enabled);
void Log(const char* fmt, ...);
void LogClose();

} // namespace uiedit
