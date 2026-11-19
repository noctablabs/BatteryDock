#pragma once
#include <windows.h>
#include <string>

// Native, modal Win32 name editor. Returns false if the user cancels.
// No shell or external process is used.
bool PromptDeviceName(HWND owner, const std::wstring& currentName, std::wstring& result);
// Custom build: asks for a web driver address or program path for a device. Empty = no link.
bool PromptDeviceLink(HWND owner, const std::wstring& currentLink, std::wstring& result);
