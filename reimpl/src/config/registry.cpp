#include "config/registry.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace guild::config {

// gilde.exe aSoftwareAheadE @0x610a14
const char* const kRegistryPrefix = "Software\\Ahead Entertainment\\";

// gilde.exe 0x40c420 — VIBE_Registry_OpenKey
//   SubKey = sprintf("%s%s", "Software\Ahead Entertainment\", subkey)
//   mode<1            -> return -1
//   mode==1           -> RegOpenKeyExA(HKCU, SubKey, KEY_READ...)   == openKey(create=false)
//   mode==2           -> RegCreateKeyExA(HKCU, SubKey, KEY_WRITE..) == openKey(create=true)
//   mode>2            -> return -1
// On a failed Reg*ExA the original falls through to "return -1".
int OpenKey(IRegistryProvider& reg, const std::string& subkey, OpenMode mode) {
    const std::string full = std::string(kRegistryPrefix) + subkey;
    const u8 m = static_cast<u8>(mode);
    if (m == 0)
        return -1;
    if (m <= 1)
        return reg.openKey(full, /*create=*/false);
    if (m == 2)
        return reg.openKey(full, /*create=*/true);
    return -1;
}

// gilde.exe 0x40c4c8 — VIBE_Registry_CloseKey
int CloseKey(IRegistryProvider& reg, int handle) {
    return reg.closeKey(handle);
}

// gilde.exe 0x40c4d8 — VIBE_Registry_QueryDwordValue
// cbData=4; if RegQueryValueExA fails return 0 else return *(DWORD*)Data.
u32 QueryDwordValue(IRegistryProvider& reg, int handle, const std::string& name) {
    u32 v = 0;
    if (!reg.queryDword(handle, name, &v))
        return 0;
    return v;
}

// gilde.exe 0x40c510 — VIBE_Registry_QueryDwordOut
// if RegQueryValueExA fails return 0; else *out = value; return 1.
int QueryDwordOut(IRegistryProvider& reg, int handle, const std::string& name, u32* out) {
    u32 v = 0;
    if (!reg.queryDword(handle, name, &v))
        return 0;
    *out = v;
    return 1;
}

// gilde.exe 0x40c550 — VIBE_Registry_QueryStringValue
// The original reads up to 128 bytes then copies the NUL-terminated string.
int QueryStringValue(IRegistryProvider& reg, int handle, const std::string& name, std::string* dst) {
    std::string v;
    if (!reg.queryString(handle, name, &v))
        return 0;
    *dst = v;
    return 1;
}

// gilde.exe 0x40c5c0 — VIBE_Registry_QueryFloatValue
//   if (QueryStringValue) return (float)VIBE_Util_StrToDouble(buf,0);
//   else return 0.0;
float QueryFloatValue(IRegistryProvider& reg, int handle, const std::string& name) {
    std::string v;
    if (QueryStringValue(reg, handle, name, &v))
        return static_cast<float>(std::strtod(v.c_str(), nullptr));
    return 0.0f;
}

// gilde.exe 0x40c608 — VIBE_Registry_QueryFloatOut
//   if (!QueryStringValue) return 0;
//   v = (float)StrToDouble(buf,0);
//   if ((bits(v) & 0x7FFFFFFF)==0 && errno) return 0;   // value==0 AND errno set
//   *out = v; return 1;
int QueryFloatOut(IRegistryProvider& reg, int handle, const std::string& name, float* out) {
    std::string v;
    if (!QueryStringValue(reg, handle, name, &v))
        return 0;
    errno = 0;
    float f = static_cast<float>(std::strtod(v.c_str(), nullptr));
    u32 bits;
    std::memcpy(&bits, &f, 4);
    if ((bits & 0x7FFFFFFFu) == 0 && errno != 0)
        return 0;
    *out = f;
    return 1;
}

// gilde.exe 0x40c670 — VIBE_Registry_SetDwordValue
int SetDwordValue(IRegistryProvider& reg, int handle, const std::string& name, u32 value) {
    return reg.setDword(handle, name, value);
}

// gilde.exe 0x40c690 — VIBE_Registry_SetStringValue
// RegSetValueExA(..., REG_SZ, value, strlen(value)+1)
int SetStringValue(IRegistryProvider& reg, int handle, const std::string& name, const std::string& value) {
    return reg.setString(handle, name, value);
}

// gilde.exe 0x40c6b8 — VIBE_Registry_SetFloatValue
//   sprintf(buf, "%f", value); RegSetValueExA(..., REG_SZ, buf, strlen+1)
int SetFloatValue(IRegistryProvider& reg, int handle, const std::string& name, float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%f", value);
    return reg.setString(handle, name, buf);
}

} // namespace guild::config
