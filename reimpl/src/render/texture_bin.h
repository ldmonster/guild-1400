#pragma once
// =============================================================================
// guild::render — TEXTURE ARCHIVE (Resources/Textures.BIN) decode front-end.
//
// Resources/Textures.BIN is a PKZIP archive of 2370 standard Windows BMPs
// (DEFLATE / stored, magic "BM" / 0x424D), stored under nested group dirs:
//   _DYNAMIC/2D/BLACK.BMP, Stoffe/st_LEDE_04c_1n_BRAUNBUCH_einfach.bmp, ...
// All shipped BMPs are SQUARE (w == h) — the engine's texture loader rejects a
// non-square texture (VIBE_Texture_LoadByName @0x5DA714: software branch loads
// only when `width == height`). 8-bit paletted (BI_RGB) and 24-bit are present.
//
// The engine resolves a MATERIAL NAME (render::BgfMaterial.name0, e.g.
// "st_LEDE_04c_1n_BRAUNBUCH_einfach") to a BMP file through:
//   0x5DA714  VIBE_Texture_LoadByName     — find-or-load by name (slot cache).
//   0x5D97E8  VIBE_Texture_BuildBmpPath   — name + ".BMP", then
//   ...       VIBE_Vfs_ResolveAndBuildPath— resolves the bare name against the
//                                           mounted VFS tree (the .BIN members),
//                                           case-insensitively, finding the BMP
//                                           regardless of which group dir holds it.
//   0x5F0C10  VIBE_Bmp_ReadHeaderInfo     — width/height/bpp (square check).
//   0x5F0CE4  VIBE_Bmp_LoadBuffer         — decode to 8-bit indices (+palette).
//
// This module reconstructs that lookup as a standalone, testable operation: it
// mounts Textures.BIN through io::ArchiveMount (which decompresses members via
// the reconstructed InflateRaw), builds a BARE-NAME index over the members (so a
// material name with no path / no extension resolves to its BMP wherever it lives
// — exactly what VIBE_Vfs_ResolveAndBuildPath does over the tree), and decodes a
// found BMP to either 8-bit indexed (+ palette) or expanded RGBA via the existing
// render::BmpLoadBuffer. It does NOT redefine the BMP codec or the Texture record;
// it is the archive+name-resolution layer above them.
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"
#include "render/texture.h"
#include "shim/IFileSystem.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace guild::render {

// A decoded texture bitmap from the archive.
struct DecodedBmp {
    bool ok = false;
    int  width = 0;             // square side (w == h for shipped textures)
    int  height = 0;
    int  bpp = 0;              // source bit count (8 or 24)
    bool square = false;       // width == height (engine-loadable)
    std::string member;        // resolved archive member path
    std::vector<u8> indices;   // 8-bit palette indices, top-down (w*h) — always
                               // produced (24-bit sources are quantised? no: see
                               // rgba below). For 8-bit sources these are the real
                               // on-disk indices and `palette` is the source LUT.
    std::vector<u8> palette;   // 256*3 RGB triples (only for 8-bit sources)
    std::vector<u8> rgba;      // w*h*4 RGBA8, top-down (R,G,B,255). Always filled
                               // (8-bit expanded via palette, 24-bit direct).
};

// ---------------------------------------------------------------------------
// TextureBin — a mounted Resources/Textures.BIN with a bare-name index.
// ---------------------------------------------------------------------------
class TextureBin {
public:
    TextureBin() = default;

    // Mount the archive through `fs` (default "Resources/Textures.BIN") and build
    // the bare-name index. caseInsensitive mirrors the VFS resolve (always true
    // for the texture tree). Returns true on success.
    bool Mount(shim::IFileSystem* fs, const char* archivePath = "Resources/Textures.BIN");

    bool mounted() const { return mount_ && mount_->isMounted(); }
    std::size_t memberCount() const { return mount_ ? mount_->memberCount() : 0; }
    std::size_t bmpCount() const { return bmpMembers_; }

    // Resolve a material/texture name to a member path. The name may be a bare
    // stem ("st_LEDE..."), carry an extension (".bmp"/".BMP"), or be a full member
    // path; matching is case-insensitive and extension-flexible. Returns the
    // canonical member path, or "" if no BMP matches.
    std::string ResolveName(const char* name) const;

    // Decode a member (by resolved name, bare name, or path) into a DecodedBmp.
    // Returns dec.ok=false if the name can't be resolved or the BMP fails to
    // decode. Results are cached by canonical member path.
    const DecodedBmp* Decode(const char* name);

    // Decode straight from a flat BMP byte buffer (no archive needed) — the unit
    // path. `key` caches the result. Returns null on failure.
    const DecodedBmp* DecodeBuffer(const char* key, const std::vector<u8>& bmp);

    io::ArchiveMount* mount() { return mount_.get(); }

private:
    std::unique_ptr<io::ArchiveMount> mount_;
    // bare-stem (UPPER, no ext) -> canonical member path (first one wins).
    std::map<std::string, std::string> byStem_;
    // UPPER full path -> canonical member path.
    std::map<std::string, std::string> byPath_;
    std::map<std::string, std::unique_ptr<DecodedBmp>> cache_;
    std::size_t bmpMembers_ = 0;

    void buildIndex();
};

// ---------------------------------------------------------------------------
// Decode a flat BMP byte buffer into a DecodedBmp (the codec front-end shared by
// the archive + buffer paths). Fills indices/palette (8-bit sources) and always
// fills rgba (expanded). Returns ok=false on an unsupported / malformed BMP.
//   gilde.exe 0x5F0C10 VIBE_Bmp_ReadHeaderInfo + 0x5F0CE4 VIBE_Bmp_LoadBuffer.
// ---------------------------------------------------------------------------
DecodedBmp DecodeBmpBuffer(const std::vector<u8>& bmp);

// Normalize a name to its bare UPPER stem (drop directory + a single extension).
//   "Stoffe/st_LEDE.bmp" -> "ST_LEDE";  "st_LEDE" -> "ST_LEDE".
std::string TextureNameStem(const char* name);

} // namespace guild::render
