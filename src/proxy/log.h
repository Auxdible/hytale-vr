// Session log and file locations. Everything lives in the HytaleVR folder next
// to opengl32.dll inside Hytale's Client folder.
#pragma once

#include <string>

namespace hvr {

void paths_init(void* module);
const std::wstring& client_dir();   // folder holding HytaleClient.exe and our opengl32.dll
const std::wstring& data_dir();     // <client>\HytaleVR
const std::wstring& log_dir();      // <client>\HytaleVR\logs
bool ensure_dir(const std::wstring& path);

void log_init();
void logf(const char* format, ...);

std::string narrow(const std::wstring& text);
std::wstring widen(const std::string& text);

}  // namespace hvr
