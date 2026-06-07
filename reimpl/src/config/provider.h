#pragma once
// Win32 substitute: internal key-value provider interface.
//
// The original config module reads three OS-backed key-value stores:
//   - the Windows registry (RegOpenKeyExA / RegQueryValueExA / RegSetValueExA)
//   - .INI profile files     (GetPrivateProfileStringA / GetPrivateProfileIntA)
//   - the process environment (getenv / putenv)
//
// AGENT_GUIDE forbids direct OS/vendor calls in src/. To keep the PARSING and
// LOGIC of those original functions 1:1 while staying platform-neutral, the
// reconstructed registry/ini/env code is written against the small interfaces
// below. They are backed by an in-memory implementation (this file) and an
// optional file-backed INI loader (ini.cpp). On real Windows a thin adapter
// would forward each call to the matching Win32 API; that adapter is the only
// place a real OS call would live.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace guild::config {

// --- Registry provider -----------------------------------------------------
// Mirrors the subset of the Win32 registry API the original uses. A "key" is a
// subkey path under HKEY_CURRENT_USER (the original always opens HKCU). Values
// are typed: REG_DWORD (4 bytes) or REG_SZ (string). RegFloat values are stored
// by the original as REG_SZ text, so they map onto string values here.
enum class RegType { None, Dword, Sz };

struct RegValue {
    RegType type = RegType::None;
    std::uint32_t dword = 0;
    std::string sz;
};

// Open modes match VIBE_Registry_OpenKey's `mode` byte:
//   1 = open existing (KEY_READ-ish), 2 = create-if-absent.
class IRegistryProvider {
public:
    virtual ~IRegistryProvider() = default;

    // Returns a non-negative handle on success, or -1 on failure.
    // create=false -> fail if the key does not exist.
    virtual int openKey(const std::string& subkey, bool create) = 0;
    virtual int closeKey(int handle) = 0; // 0 == ERROR_SUCCESS

    // Reads return true and fill *out on success; false if missing/wrong type.
    virtual bool queryDword(int handle, const std::string& name, std::uint32_t* out) = 0;
    virtual bool queryString(int handle, const std::string& name, std::string* out) = 0;

    // Writes return 0 (ERROR_SUCCESS) on success.
    virtual int setDword(int handle, const std::string& name, std::uint32_t value) = 0;
    virtual int setString(int handle, const std::string& name, const std::string& value) = 0;
};

// --- Profile (.INI) provider ----------------------------------------------
// Mirrors GetPrivateProfileStringA/IntA. Section and key matching is
// case-insensitive (as on Win32). A missing section/key yields the supplied
// default.
class IProfileProvider {
public:
    virtual ~IProfileProvider() = default;

    // GetPrivateProfileStringA equivalent: returns the value text, or `def` if
    // the section/key is absent.
    virtual std::string getString(const std::string& section,
                                  const std::string& key,
                                  const std::string& def) const = 0;

    // GetPrivateProfileIntA equivalent. See ini.cpp for the exact integer
    // parse semantics (leading sign + decimal digits, stop at first non-digit;
    // missing key -> def).
    virtual int getInt(const std::string& section,
                       const std::string& key,
                       int def) const = 0;
};

// --- Environment provider --------------------------------------------------
class IEnvProvider {
public:
    virtual ~IEnvProvider() = default;
    // Returns true and fills *out if the variable exists.
    virtual bool get(const std::string& name, std::string* out) const = 0;
    virtual void set(const std::string& name, const std::string& value) = 0;
};

// ---------------------------------------------------------------------------
// In-memory backends (the default test/host substitute).
// ---------------------------------------------------------------------------

class MemRegistry : public IRegistryProvider {
public:
    int openKey(const std::string& subkey, bool create) override;
    int closeKey(int handle) override;
    bool queryDword(int handle, const std::string& name, std::uint32_t* out) override;
    bool queryString(int handle, const std::string& name, std::string* out) override;
    int setDword(int handle, const std::string& name, std::uint32_t value) override;
    int setString(int handle, const std::string& name, const std::string& value) override;

private:
    using Key = std::map<std::string, RegValue>;
    std::map<std::string, Key> keys_;
    std::vector<std::string> handles_; // handle -> subkey path; "" = unused slot
    Key* keyFor(int handle);
};

class MemEnv : public IEnvProvider {
public:
    bool get(const std::string& name, std::string* out) const override;
    void set(const std::string& name, const std::string& value) override;

private:
    std::map<std::string, std::string> vars_;
};

} // namespace guild::config
