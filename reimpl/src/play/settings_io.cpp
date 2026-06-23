// guild::play — settings load/save over the shim filesystem. See settings_io.h.
//
// REUSED (extern, not redefined — ODR):
//   config::IniFile / config::ReadGfxAndSoundSettings  (src/config/ini.*, 0x56b834)
//   app::ConfigWriteGfxSettings                        (src/app/config_write.*, 0x56af54)
#include "play/settings_io.h"

#include "app/config_write.h"
#include "shim/IFileSystem.h"

#include <cctype>
#include <cstddef>
#include <vector>

namespace guild::play {
namespace {

// ---- small text helpers (ASCII case folding, like the Win32 profile APIs) ----

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// One physical line of the INI file: `text` (no EOL) + the EOL it carried
// ("" only for a final line with no trailing newline).
struct Line {
    std::string text;
    std::string eol;
};

std::vector<Line> SplitLines(const std::string& t) {
    std::vector<Line> lines;
    std::size_t i = 0;
    while (i < t.size()) {
        std::size_t j = t.find('\n', i);
        if (j == std::string::npos) {
            lines.push_back({t.substr(i), ""});
            break;
        }
        // Keep the CR with the EOL, not the text.
        std::size_t end = j;
        std::string eol = "\n";
        if (end > i && t[end - 1] == '\r') { --end; eol = "\r\n"; }
        lines.push_back({t.substr(i, end - i), eol});
        i = j + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<Line>& lines) {
    std::string out;
    for (const Line& l : lines) { out += l.text; out += l.eol; }
    return out;
}

// "[Name]" header? Returns the trimmed section name via `name`.
bool ParseSectionHeader(const std::string& line, std::string& name) {
    const std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[') return false;
    const std::size_t close = t.find(']');
    if (close == std::string::npos) return false;
    name = Trim(t.substr(1, close - 1));
    return true;
}

// "key=value" line? Returns the trimmed key via `key`. Comment lines
// (';' first non-space, the Win32 profile comment) are not keys.
bool ParseKeyLine(const std::string& line, std::string& key) {
    const std::string t = Trim(line);
    if (t.empty() || t.front() == ';' || t.front() == '[') return false;
    const std::size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = Trim(t.substr(0, eq));
    return !key.empty();
}

} // namespace

// ---------------------------------------------------------------------------
// WritePrivateProfileStringA(section, key, value, file) — file-text semantics.
// ---------------------------------------------------------------------------
std::string IniWriteProfileString(std::string iniText, const std::string& section,
                                  const std::string& key, const std::string& value) {
    // Line ending for written/inserted lines: the file's own convention; an
    // empty (new) file gets CRLF — Win32 WritePrivateProfileStringA writes CRLF.
    const std::string eol =
        (iniText.empty() || iniText.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

    std::vector<Line> lines = SplitLines(iniText);

    // Locate the section: [secBegin, secEnd) are the line indices of the
    // section's body (after its header, before the next header / EOF).
    std::size_t secHeader = lines.size();
    std::size_t secEnd = lines.size();
    {
        std::string name;
        bool inTarget = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (ParseSectionHeader(lines[i].text, name)) {
                if (inTarget) { secEnd = i; break; }
                if (IEquals(name, section)) { secHeader = i; inTarget = true; }
            }
        }
        if (!inTarget) secHeader = lines.size();
    }

    if (secHeader == lines.size()) {
        // Section absent -> append "[section]" + "key=value" at end of file.
        // Make sure the previous content ends with a line break first.
        if (!lines.empty() && lines.back().eol.empty()) lines.back().eol = eol;
        lines.push_back({"[" + section + "]", eol});
        lines.push_back({key + "=" + value, eol});
        return JoinLines(lines);
    }

    // Scan the section body for the key (case-insensitive).
    std::string k;
    for (std::size_t i = secHeader + 1; i < secEnd; ++i) {
        if (ParseKeyLine(lines[i].text, k) && IEquals(k, key)) {
            // Key exists: rewrite this line only, PRESERVING the file's
            // original key spelling (Win32 keeps the stored name and replaces
            // the value); everything else stays byte-identical.
            const std::string keepEol = lines[i].eol.empty() ? eol : lines[i].eol;
            lines[i] = {k + "=" + value, keepEol};
            return JoinLines(lines);
        }
    }

    // Key absent: insert after the section's last non-blank line (before the
    // blank separator / next "[section]" header).
    std::size_t insertAt = secHeader + 1;
    for (std::size_t i = secHeader + 1; i < secEnd; ++i) {
        if (!Trim(lines[i].text).empty()) insertAt = i + 1;
    }
    // The line we insert after must terminate with a line break.
    if (insertAt > 0 && lines[insertAt - 1].eol.empty()) lines[insertAt - 1].eol = eol;
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
                 {key + "=" + value, eol});
    return JoinLines(lines);
}

// ---------------------------------------------------------------------------
// Load / Save.
// ---------------------------------------------------------------------------
namespace {

bool ReadWholeFile(shim::IFileSystem& fs, const std::string& name, std::string& out) {
    shim::IFile* f = fs.open(name.c_str(), "rb");
    if (!f) return false;
    const std::int64_t n = f->size();
    out.clear();
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        const std::size_t got = f->read(&out[0], out.size());
        out.resize(got);
    }
    fs.close(f);
    return true;
}

} // namespace

bool LoadSettings(shim::IFileSystem& fs, const std::string& iniName, SettingsBundle& out) {
    out = SettingsBundle{};
    std::string text;
    const bool found = ReadWholeFile(fs, iniName, text);
    // A missing file still runs the reader: GetPrivateProfile* against a
    // missing file yields every default, exactly like an empty provider.
    config::IniFile ini(found ? text : std::string());
    config::ReadGfxAndSoundSettings(ini, out.gfx, out.sound, out.game);
    return found;
}

bool SaveSettings(shim::IFileSystem& fs, const std::string& iniName, const SettingsBundle& s) {
    std::string text;
    ReadWholeFile(fs, iniName, text);   // absent -> start from empty (file is created)

    // The REAL serializer (0x56af54) drives the merge: its exact section/key
    // order and value formatting, one WritePrivateProfileStringA per key.
    app::ConfigWriteGfxSettings(
        s.gfx, s.sound, s.game,
        [&text](const std::string& section, const std::string& key, const std::string& value) {
            text = IniWriteProfileString(std::move(text), section, key, value);
        });

    shim::IFile* f = fs.open(iniName.c_str(), "wb");
    if (!f) return false;
    const std::size_t wrote = f->write(text.data(), text.size());
    fs.close(f);
    return wrote == text.size();
}

} // namespace guild::play
