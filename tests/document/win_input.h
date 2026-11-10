#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
inline bool test_input_desktop_available() {
    const auto input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!input) return false;
    wchar_t actual[128]{}, caller[128]{}; DWORD needed = 0;
    const bool okay = GetUserObjectInformationW(input, UOI_NAME, actual, sizeof(actual), &needed) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, caller, sizeof(caller), &needed) &&
        lstrcmpW(actual, caller) == 0;
    CloseDesktop(input); return okay;
}
// A real native input test must own foreground focus before calling SendInput.
inline bool test_foreground(HWND window) {
    const auto thread = GetCurrentThreadId();
    const auto foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attached = foreground && foreground != thread && AttachThreadInput(thread, foreground, TRUE);
    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window); SetForegroundWindow(window); SetFocus(window);
    if (attached) AttachThreadInput(thread, foreground, FALSE);
    return GetForegroundWindow() == window;
}
#endif
