#include "io/path.h"

#include <cstring>

namespace guild::io {

// gilde.exe 0x44eb54 — VIBE_Path_ConvertBackslashToSlash  (__usercall, eax=str).
// Scans `len` bytes once, replacing '\\' (92) with '/' (47).
char* ConvertBackslashToSlash(char* s) {
    if (!s)
        return s;
    int len = static_cast<int>(std::strlen(s));
    for (int i = 0; i < len; ++i) {
        if (s[i] == '\\')
            s[i] = '/';
    }
    return s;
}

// gilde.exe 0x44eb90 — VIBE_Path_ConvertSlashToBackslash  (__usercall, eax=str).
char* ConvertSlashToBackslash(char* s) {
    if (!s)
        return s;
    int len = static_cast<int>(std::strlen(s));
    for (int i = 0; i < len; ++i) {
        if (s[i] == '/')
            s[i] = '\\';
    }
    return s;
}

} // namespace guild::io
