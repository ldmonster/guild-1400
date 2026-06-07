#include "render/texture_bin.h"
#include "render/bmp.h"

#include <cctype>
#include <cstring>

namespace guild::render {

namespace {

std::string upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

// Drop a trailing directory component; keep the basename.
std::string basename(const std::string& s) {
    auto pos = s.find_last_of("/\\");
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

// Drop a single trailing extension (".bmp", ".BMP", etc.).
std::string stripExt(const std::string& s) {
    auto pos = s.find_last_of('.');
    return pos == std::string::npos ? s : s.substr(0, pos);
}

} // namespace

std::string TextureNameStem(const char* name) {
    if (!name) return std::string();
    return upper(stripExt(basename(std::string(name))));
}

// gilde.exe 0x5F0C10 / 0x5F0CE4 — decode a BMP buffer to indexed (8-bit) +
// always-expanded RGBA. 8-bit sources keep their real indices + source palette;
// 24-bit sources fill rgba directly (and leave indices/palette empty).
DecodedBmp DecodeBmpBuffer(const std::vector<u8>& bmp) {
    DecodedBmp d;
    BmpInfo info = BmpReadHeaderInfo(bmp);
    if (!info.ok) return d;
    d.width = info.width;
    d.height = info.height;
    d.bpp = info.bitCount;
    d.square = (info.width == info.height && info.width > 0);

    if (info.bitCount == 8) {
        u8 pal[256 * 3];
        std::memset(pal, 0, sizeof(pal));
        int w = 0, h = 0;
        std::vector<u8> idx = BmpLoadBuffer(bmp, 8, w, h, pal);
        if (idx.empty() || w <= 0 || h <= 0) return d;
        d.width = w; d.height = h;
        d.indices = std::move(idx);
        d.palette.assign(pal, pal + sizeof(pal));
        // Expand to RGBA via the palette.
        d.rgba.resize((std::size_t)w * h * 4);
        for (std::size_t i = 0; i < d.indices.size(); ++i) {
            u8 ci = d.indices[i];
            d.rgba[i * 4 + 0] = pal[ci * 3 + 0];
            d.rgba[i * 4 + 1] = pal[ci * 3 + 1];
            d.rgba[i * 4 + 2] = pal[ci * 3 + 2];
            d.rgba[i * 4 + 3] = 255;
        }
        d.ok = true;
        return d;
    }

    // 24-bit (or anything BmpLoadBuffer can expand): get RGB top-down.
    int w = 0, h = 0;
    std::vector<u8> rgb = BmpLoadBuffer(bmp, 24, w, h, nullptr);
    if (rgb.empty() || w <= 0 || h <= 0) return d;
    d.width = w; d.height = h;
    d.rgba.resize((std::size_t)w * h * 4);
    for (std::size_t i = 0; i < (std::size_t)w * h; ++i) {
        d.rgba[i * 4 + 0] = rgb[i * 3 + 0];
        d.rgba[i * 4 + 1] = rgb[i * 3 + 1];
        d.rgba[i * 4 + 2] = rgb[i * 3 + 2];
        d.rgba[i * 4 + 3] = 255;
    }
    d.ok = true;
    return d;
}

bool TextureBin::Mount(shim::IFileSystem* fs, const char* archivePath) {
    mount_ = std::make_unique<io::ArchiveMount>();
    // caseInsensitive: keep stored casing, compare case-insensitively (mirrors the
    // VFS texture tree, byte_62EB84 set).
    if (!mount_->Mount(fs, archivePath, true)) {
        mount_.reset();
        return false;
    }
    buildIndex();
    return true;
}

void TextureBin::buildIndex() {
    byStem_.clear();
    byPath_.clear();
    bmpMembers_ = 0;
    for (const auto& m : mount_->members()) {
        std::string up = upper(m.name);
        if (up.size() < 4 || up.compare(up.size() - 4, 4, ".BMP") != 0)
            continue;
        ++bmpMembers_;
        byPath_.emplace(up, m.name);
        std::string stem = upper(stripExt(basename(m.name)));
        // First member wins for a duplicate stem (matches a linear tree scan).
        byStem_.emplace(stem, m.name);
    }
}

std::string TextureBin::ResolveName(const char* name) const {
    if (!name || !*name) return std::string();
    std::string raw = name;
    // 1) full path (with extension or +.bmp) — try the path index.
    std::string up = upper(raw);
    auto byp = byPath_.find(up);
    if (byp != byPath_.end()) return byp->second;
    if (up.size() < 4 || up.compare(up.size() - 4, 4, ".BMP") != 0) {
        auto byp2 = byPath_.find(up + ".BMP");
        if (byp2 != byPath_.end()) return byp2->second;
    }
    // 2) bare stem (drop dir + ext) — the material-name path.
    std::string stem = upper(stripExt(basename(raw)));
    auto bys = byStem_.find(stem);
    if (bys != byStem_.end()) return bys->second;
    return std::string();
}

const DecodedBmp* TextureBin::Decode(const char* name) {
    std::string member = ResolveName(name);
    if (member.empty()) return nullptr;
    std::string key = upper(member);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second->ok ? it->second.get() : nullptr;

    std::vector<u8> bytes;
    auto slot = std::make_unique<DecodedBmp>();
    if (mount_ && mount_->OpenMember(member.c_str(), bytes) && !bytes.empty()) {
        *slot = DecodeBmpBuffer(bytes);
        slot->member = member;
    }
    bool ok = slot->ok;
    DecodedBmp* ret = slot.get();
    cache_.emplace(key, std::move(slot));
    return ok ? ret : nullptr;
}

const DecodedBmp* TextureBin::DecodeBuffer(const char* key, const std::vector<u8>& bmp) {
    std::string member = key ? std::string(key) : std::string("<buf>");
    std::string k = upper(member);
    auto slot = std::make_unique<DecodedBmp>();
    *slot = DecodeBmpBuffer(bmp);
    slot->member = member;
    bool ok = slot->ok;
    DecodedBmp* ret = slot.get();
    cache_[k] = std::move(slot);
    // Register the synthetic member in the name index so ResolveName/Decode can
    // find it by bare name (the archive-less test path mirrors a mounted member).
    if (key && *key) {
        byPath_.emplace(k, member);
        std::string stem = upper(stripExt(basename(member)));
        byStem_.emplace(stem, member);
        if (k.size() >= 4 && k.compare(k.size() - 4, 4, ".BMP") == 0)
            ++bmpMembers_;
    }
    return ok ? ret : nullptr;
}

} // namespace guild::render
