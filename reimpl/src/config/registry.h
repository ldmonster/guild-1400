#pragma once
// gilde.exe registry access (VIBE_Registry_*, 0x40c420-0x40c70e).
//
// The original stores per-user settings under the registry key
//   HKEY_CURRENT_USER\Software\Ahead Entertainment\<subkey>
// using REG_DWORD and REG_SZ values; float values are stored as REG_SZ text
// ("%f"). All Win32 Reg* calls are routed through IRegistryProvider (the Win32
// substitute, see provider.h); this file reproduces the original key/value
// schema and the open/query/set control flow.
#include "config/provider.h"
#include "guild/common/types.h"
#include <string>

namespace guild::config {

// The fixed key prefix the original sprintf()s in front of every subkey.
// gilde.exe aSoftwareAheadE @0x610a14.
extern const char* const kRegistryPrefix; // "Software\\Ahead Entertainment\\"

// Open mode byte from VIBE_Registry_OpenKey's ecx argument.
enum class OpenMode : u8 {
    None  = 0, // <1 : original returns -1 without touching the registry
    Open  = 1, // open existing  (RegOpenKeyExA, KEY_READ|KEY_QUERY_VALUE)
    Create = 2 // create/open    (RegCreateKeyExA, KEY_WRITE|KEY_QUERY_VALUE)
};

// gilde.exe 0x40c420 — VIBE_Registry_OpenKey  (__usercall, eax=(subkey@eax), ecx=mode)
// Prepends kRegistryPrefix to `subkey` then opens (mode 1) or creates (mode 2)
// the key under HKCU. Returns the provider handle, or -1 on any failure / mode<1.
int OpenKey(IRegistryProvider& reg, const std::string& subkey, OpenMode mode);

// gilde.exe 0x40c4c8 — VIBE_Registry_CloseKey  (__usercall, eax=handle)
int CloseKey(IRegistryProvider& reg, int handle);

// gilde.exe 0x40c4d8 — VIBE_Registry_QueryDwordValue  (eax=key, edx=name)
// Returns the DWORD value, or 0 if the value is absent/wrong size.
u32 QueryDwordValue(IRegistryProvider& reg, int handle, const std::string& name);

// gilde.exe 0x40c510 — VIBE_Registry_QueryDwordOut  (eax=key, edx=name, ebx=out)
// Returns 1 and writes *out on success; returns 0 (and leaves *out) on failure.
int QueryDwordOut(IRegistryProvider& reg, int handle, const std::string& name, u32* out);

// gilde.exe 0x40c550 — VIBE_Registry_QueryStringValue  (eax=key, edx=name, ebx=dst)
// Reads a REG_SZ value into `dst` (caller-sized, original used 128 bytes).
// Returns 1 on success, 0 on failure (dst left untouched on failure).
int QueryStringValue(IRegistryProvider& reg, int handle, const std::string& name, std::string* dst);

// gilde.exe 0x40c5c0 — VIBE_Registry_QueryFloatValue  (eax=key, edx=name) -> st0
// Reads a REG_SZ value and parses it as a float; returns 0.0 if absent.
float QueryFloatValue(IRegistryProvider& reg, int handle, const std::string& name);

// gilde.exe 0x40c608 — VIBE_Registry_QueryFloatOut  (eax=key, edx=name, ecx=out)
// Returns 1 and writes *out on success. Matches the original's quirk: a parsed
// value of exactly 0 combined with a set errno is treated as failure.
int QueryFloatOut(IRegistryProvider& reg, int handle, const std::string& name, float* out);

// gilde.exe 0x40c670 — VIBE_Registry_SetDwordValue  (eax=key, edx=name, ebx=value)
int SetDwordValue(IRegistryProvider& reg, int handle, const std::string& name, u32 value);

// gilde.exe 0x40c690 — VIBE_Registry_SetStringValue  (eax=key, edx=name, ebx=value)
int SetStringValue(IRegistryProvider& reg, int handle, const std::string& name, const std::string& value);

// gilde.exe 0x40c6b8 — VIBE_Registry_SetFloatValue  (__userpurge, eax=key, float)
// Formats the float as "%f" and stores it as a REG_SZ value.
int SetFloatValue(IRegistryProvider& reg, int handle, const std::string& name, float value);

} // namespace guild::config
