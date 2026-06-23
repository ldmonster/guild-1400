// guild::play — real main-menu artwork loader + MenuRenderHooks install. See header.
#include "play/menu_assets.h"

#include "gui/menu_render.h"   // MenuRenderHooks, SetMenuRenderHooks
#include "render/types.h"      // render::Surface
#include "render/gfx_archive.h" // DecodeShapeBlob (font glyph bitmaps)

#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"  // ResolveMainMenuLabels mounts via disk fs
#include "io/archive_mount.h"           // textbin PKZIP mount
#include "gui/text_load.h"              // BuildTextArray @0x44bb5c
#include "gui/text/textdb.h"            // TextDb::FindIndex/Text

#include <cstdint>
#include <cstring>
#include <cctype>

namespace guild::play {
namespace {

// gfx-id of the menu records (record index == gfx id in the archive).
constexpr int kGfxButtonRed     = 174;
constexpr const char* kButtonRedName  = "_BUTTON_RED";
constexpr const char* kBackgroundName = "_MENUE_BACKGROUND";

inline std::uint16_t RdU16(const std::uint8_t* p) {
    return (std::uint16_t)(p[0] | (p[1] << 8));
}
inline std::uint32_t RdU32(const std::uint8_t* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

// gilde.gfx directory record layout (84 bytes), mirrors render/gfx_archive.cpp.
constexpr std::size_t kRecSize    = 84;
constexpr std::size_t kRecDataOff = 48;
constexpr std::size_t kRecDataSz  = 56;
// SHAPBANK header / shape header offsets (the in-memory font layout the engine
// indexes through VIBE_Coord_Transform @0x5d8b00 — blob+0x45+4*ch == shape ch).
constexpr std::size_t kBankShapeCount = 0x2A;  // u16
constexpr std::size_t kBankOffTable   = 0x45;  // u32[] per shape (rel to blob)
constexpr std::size_t kShWidth        = 6;     // u16 glyph bitmap width (= adv+1)
constexpr std::size_t kShKern         = 22;    // u16 leftBearing (subtracted)
constexpr std::size_t kShAdvance      = 26;    // u16 pen advance
constexpr std::size_t kFontLineHeight = 46;    // u16 @ font+46 (dword_69FFB0)

} // namespace

int BlitDecodedSprite(render::Surface* s, int x, int y,
                      const render::DecodedShape& sprite) {
    if (!s || !s->pixels || sprite.width <= 0 || sprite.height <= 0) return 0;
    // The menu framebuffer is 32bpp packed 0xAARRGGBB (gui::Argb8888): our decoded
    // sprite is the same packing, so copy through (honouring transparency + clip).
    if (s->bpp != 32) return 0;

    const int sw = sprite.width, sh = sprite.height;
    const int W = s->widthPx ? s->widthPx : s->width;
    const int H = s->height;
    int drawn = 0;
    auto* base = reinterpret_cast<std::uint32_t*>(s->pixels);
    for (int row = 0; row < sh; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= H) continue;
        const std::uint32_t* src = sprite.argb.data() + (std::size_t)row * sw;
        std::uint32_t* dst = base + (std::size_t)dy * W;
        for (int col = 0; col < sw; ++col) {
            const std::uint32_t px = src[col];
            if ((px & 0xFF000000u) == 0u) continue;  // transparent
            const int dx = x + col;
            if (dx < 0 || dx >= W) continue;
            dst[dx] = px;
            ++drawn;
        }
    }
    return drawn;
}

// ===========================================================================
// MenuFont
// ===========================================================================
bool MenuFont::Load(shim::IFileSystem& fs, const char* archivePath,
                    const char* fontName) {
    loaded_ = false;
    lineHeight_ = 0;
    for (auto& g : glyphs_) g = Glyph{};
    if (!archivePath || !fontName) return false;

    shim::IFile* f = fs.open(archivePath, "rb");
    if (!f) return false;

    // Read the directory header (count) then the record table to find `fontName`.
    std::uint8_t hdr[4];
    if (f->read(hdr, 4) != 4) { fs.close(f); return false; }
    const std::uint32_t count = RdU32(hdr);
    if (count == 0 || count > 1000000u) { fs.close(f); return false; }

    std::vector<std::uint8_t> table((std::size_t)count * kRecSize);
    if (f->read(table.data(), table.size()) != table.size()) { fs.close(f); return false; }

    std::uint32_t fontOff = 0, fontSz = 0;
    bool found = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* rec = table.data() + (std::size_t)i * kRecSize;
        // Record name is NUL-padded, up to 48 bytes.
        std::size_t nlen = 0;
        while (nlen < 48 && rec[nlen] != '\0') ++nlen;
        if (nlen == std::strlen(fontName) &&
            std::memcmp(rec, fontName, nlen) == 0) {
            fontOff = RdU32(rec + kRecDataOff);
            fontSz  = RdU32(rec + kRecDataSz);
            found = true;
            break;
        }
    }
    if (!found || fontSz == 0) { fs.close(f); return false; }

    // Read just the font SHAPBANK blob.
    std::vector<std::uint8_t> blob(fontSz);
    f->seek((std::int64_t)fontOff, 0 /*SEEK_SET*/);
    const std::size_t got = f->read(blob.data(), blob.size());
    fs.close(f);
    if (got != blob.size()) return false;
    if (blob.size() < kBankOffTable + 4) return false;

    const std::uint16_t shapeCount = RdU16(blob.data() + kBankShapeCount);
    // Glyph for char `ch` == shape `ch`. Pull metrics + bitmap for every shape
    // that exists, up to char-code 255.
    const int n = shapeCount < 256 ? (int)shapeCount : 256;
    for (int ch = 0; ch < n; ++ch) {
        const std::size_t offEntry = kBankOffTable + (std::size_t)ch * 4;
        if (offEntry + 4 > blob.size()) break;
        const std::uint32_t so = RdU32(blob.data() + offEntry);
        if ((std::size_t)so + 28 > blob.size()) continue;
        Glyph& g = glyphs_[ch];
        g.width   = RdU16(blob.data() + so + kShWidth);
        g.kern    = RdU16(blob.data() + so + kShKern);
        g.advance = RdU16(blob.data() + so + kShAdvance);
        g.present = true;
        // Decode the glyph bitmap (RLE shape `ch`). A missing/odd bitmap leaves
        // the metric usable for layout but draws nothing (space, '~').
        render::DecodeShapeBlob(blob.data(), blob.size(), ch, g.shape);
    }
    // Line height = u16 @ font+46, read off the bank base (dword_69FFB0).
    if (blob.size() >= kFontLineHeight + 2)
        lineHeight_ = RdU16(blob.data() + kFontLineHeight);

    loaded_ = glyphs_[(unsigned char)'A'].present;
    return loaded_;
}

int MenuFont::MeasureWidth(const char* s) const {
    // VIBE_Property_Get @0x4152cc, 1:1.
    if (!s || !*s) return 0;
    const unsigned char* p = (const unsigned char*)s;
    const int len = (int)std::strlen(s);
    int v4 = 0;
    int i = 0;
    for (int idx = 0; idx < len; ++idx, ++i) {
        const unsigned char ch = p[idx];
        if (ch != 126) {  // '~'
            const Glyph& g = glyphs_[ch];
            if (i && p[idx - 1] != 32)
                v4 -= (int)g.kern;
            if (ch == 32)
                v4 += kTracking + kSpaceExtra + (int)g.advance;
            else
                v4 += kTracking + (int)g.advance;
        }
    }
    return v4 + kTracking;
}

void MenuFont::DrawText(u32* dst, int W, int H, int x, int y, const char* s,
                        int scale, u8 r, u8 g, u8 b) const {
    // VIBE_Property_Set @0x4159dc pen advance, 1:1 (no clip-right bound here; the
    // menu labels always fit the button). Glyph bitmaps recoloured to (r,g,b).
    if (!s || !*s || !dst || scale < 1) return;
    const std::uint32_t col = 0xFF000000u | ((std::uint32_t)r << 16) |
                              ((std::uint32_t)g << 8) | (std::uint32_t)b;
    const unsigned char* p = (const unsigned char*)s;
    const int len = (int)std::strlen(s);
    int pen = x;  // design-space pen (already scaled in by the caller's x)
    for (int idx = 0; idx < len; ++idx) {
        const unsigned char ch = p[idx];
        const Glyph& gl = glyphs_[ch];
        pen -= (int)gl.kern * scale;  // kern subtracted on every char
        if (ch != 126) {  // '~'
            if (ch == 32) {
                pen += kSpaceExtra * scale;
            } else {
                // Blit the glyph bitmap at (pen, y), point-scaled.
                const render::DecodedShape& sh = gl.shape;
                if (sh.width > 0 && sh.height > 0) {
                    for (int sy = 0; sy < sh.height; ++sy) {
                        const std::uint32_t* srow = sh.argb.data() + (std::size_t)sy * sh.width;
                        for (int sx = 0; sx < sh.width; ++sx) {
                            if ((srow[sx] & 0xFF000000u) == 0u) continue;  // transparent
                            for (int dy = 0; dy < scale; ++dy)
                                for (int dx = 0; dx < scale; ++dx) {
                                    const int X = pen + sx * scale + dx;
                                    const int Y = y + sy * scale + dy;
                                    if (X >= 0 && X < W && Y >= 0 && Y < H)
                                        dst[(std::size_t)Y * W + X] = col;
                                }
                        }
                    }
                }
                pen += (kTracking + (int)gl.advance) * scale;
            }
        }
    }
}

// ===========================================================================
// ResolveMainMenuLabels
// ===========================================================================
bool ResolveMainMenuLabels(const std::string& gameDir, std::string out[8]) {
    for (int i = 0; i < 8; ++i) out[i].clear();
    if (gameDir.empty()) return false;

    shim::DiskFileSystem fs(gameDir);
    // The shipped localized text DB. Match the sibling screens' choice.
    const char* arch = "Resources/textbin_deutsch.BIN";
    if (!fs.exists(arch)) {
        arch = "Resources/textbin.BIN";
        if (!fs.exists(arch)) return false;
    }
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, arch, /*caseInsensitive=*/true)) return false;

    gui::text::TextDb db;
    for (const io::ArchiveMember& m : mount.members()) {
        const std::string& nm = m.name;
        if (nm.size() < 4) continue;
        std::string ext = nm.substr(nm.size() - 4);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".res") continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(nm.c_str(), bytes) || bytes.empty()) continue;
        gui::text::BuildTextArray(bytes.data(), bytes.size(), db);
    }

    // gilde.exe 0x529d08 main-menu button -> label-global (dword_8C98xx) mapping,
    // each global a slot of the localized _OPTIONEN_MENUE_* array (Text_O_Optionen,
    // VIBE_Menu_RunOptionsMain @0x56dccc reads the same contiguous block):
    //   row 10  NewGame     8C9870 -> _OPTIONEN_MENUE_NEW       ("Нова° игра")
    //   row 53  Load        8C9854 -> _OPTIONEN_MENUE_LOAD      ("Загрузить игру")
    //   row 96  Multiplayer 8C9874 -> _OPTIONEN_MENUE_NETWORK   ("Сетева° игра")
    //   row 139 GameOptions 8C985C -> _OPTIONEN_MENUE_GAME      ("Настройки игры")
    //   row 182 GfxOptions  8C9860 -> _OPTIONEN_MENUE_GFX       ("Настройки графики")
    //   row 225 SfxOptions  8C9864 -> _OPTIONEN_MENUE_SFX       ("Настройки звука")
    //   row 268 Credits     8C9878 -> _OPTIONEN_MENUE_CREDITS   ("Авторы")
    //   row 311 Quit        8C987C -> _OPTIONEN_MENUE_PROG_EXIT ("Выход из игры")
    static const char* const kKeys[8] = {
        "_OPTIONEN_MENUE_NEW+0",       // 0: New Game (row 10)
        "_OPTIONEN_MENUE_LOAD+0",      // 1: Load (row 53)
        "_OPTIONEN_MENUE_NETWORK+0",   // 2: Multiplayer (row 96)
        "_OPTIONEN_MENUE_GAME+0",      // 3: Game options (row 139)
        "_OPTIONEN_MENUE_GFX+0",       // 4: Gfx options (row 182)
        "_OPTIONEN_MENUE_SFX+0",       // 5: Sfx options (row 225)
        "_OPTIONEN_MENUE_CREDITS+0",   // 6: Credits (row 268)
        "_OPTIONEN_MENUE_PROG_EXIT+0", // 7: Quit (row 311)
    };
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        const int idx = db.FindIndex(kKeys[i]);
        if (idx >= 0) {
            const char* t = db.Text(idx);
            if (t && *t) { out[i] = t; any = true; }
        }
    }
    return any;
}

bool MenuAssets::Load(shim::IFileSystem& fs, const char* archivePath, const char* bgName) {
    loaded_ = false;
    buttons_.clear();
    spriteCache_.clear();
    nameCache_.clear();
    bg_ = render::DecodedShape{};
    cursor_ = render::DecodedShape{};

    if (!archivePath || !fs.exists(archivePath)) return false;
    if (!archive_.LoadFromFile(fs, archivePath)) return false;

    // Decode the requested background (#1773 _MENUE_BACKGROUND 800x600, or the
    // _1024 / _1152 resolution variants), shape 0. Fall back to the 800x600 record
    // when the requested name is absent so callers can always ask for the big one.
    if (!bgName || !*bgName) bgName = kBackgroundName;
    if (!archive_.DecodeShapeByName(bgName, 0, bg_))
        if (!archive_.DecodeShapeByName(kBackgroundName, 0, bg_)) return false;
    if (bg_.width <= 0 || bg_.height <= 0) return false;

    // Decode the button frames (#174 _BUTTON_RED — all shapes).
    const int btnRec = archive_.FindByName(kButtonRedName);
    if (btnRec >= 0) {
        const int n = archive_.ShapeCount(btnRec);
        for (int i = 0; i < n; ++i) {
            render::DecodedShape sh;
            if (archive_.DecodeShape(btnRec, i, sh)) buttons_.push_back(std::move(sh));
        }
    }

    // Decode the mouse cursor (#14 _MOUSE_CURSOR shape 0 — the gloved hand). Best
    // effort: a missing/odd cursor record just leaves cursor_ empty.
    archive_.DecodeShapeByName("_MOUSE_CURSOR", 0, cursor_);

    // Decode the real `_FONT` (gfx record 66) for label text. Best effort: an
    // absent font leaves font_.loaded()==false and the caller falls back.
    font_.Load(fs, archivePath, "_FONT");

    loaded_ = true;
    return true;
}

const render::DecodedShape* MenuAssets::SpriteForGfxId(int gfxId) {
    if (!loaded_) return nullptr;
    for (auto& kv : spriteCache_)
        if (kv.first == gfxId) return &kv.second;

    // Fast path: the buttons are already decoded under gfx 174.
    if (gfxId == kGfxButtonRed && !buttons_.empty()) {
        // Use the 3rd frame (the 100-wide centre face) if present, else frame 0.
        const render::DecodedShape& src =
            buttons_.size() > 2 ? buttons_[2] : buttons_[0];
        spriteCache_.emplace_back(gfxId, src);
        return &spriteCache_.back().second;
    }

    render::DecodedShape sh;
    if (!archive_.DecodeShape(gfxId, 0, sh)) return nullptr;
    spriteCache_.emplace_back(gfxId, std::move(sh));
    return &spriteCache_.back().second;
}

const render::DecodedShape* MenuAssets::SpriteByName(const char* name, int shapeNr) {
    if (!loaded_ || !name || !*name) return nullptr;
    std::string key = std::string(name) + "#" + std::to_string(shapeNr);
    for (auto& kv : nameCache_)
        if (kv.first == key) return kv.second.width > 0 ? &kv.second : nullptr;
    render::DecodedShape sh;
    const bool ok = archive_.DecodeShapeByName(name, shapeNr, sh);
    nameCache_.emplace_back(key, ok ? std::move(sh) : render::DecodedShape{});
    return nameCache_.back().second.width > 0 ? &nameCache_.back().second : nullptr;
}

// ===========================================================================
// ResolveOptionLabels — generalized multi-key form of ResolveMainMenuLabels.
// ===========================================================================
bool ResolveOptionLabels(const std::string& gameDir, const char* const* keys, int n,
                         std::string* out) {
    for (int i = 0; i < n; ++i) out[i].clear();
    if (gameDir.empty() || !keys || n <= 0) return false;

    shim::DiskFileSystem fs(gameDir);
    const char* arch = "Resources/textbin_deutsch.BIN";
    if (!fs.exists(arch)) {
        arch = "Resources/textbin.BIN";
        if (!fs.exists(arch)) return false;
    }
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, arch, /*caseInsensitive=*/true)) return false;

    gui::text::TextDb db;
    for (const io::ArchiveMember& m : mount.members()) {
        const std::string& nm = m.name;
        if (nm.size() < 4) continue;
        std::string ext = nm.substr(nm.size() - 4);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".res") continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(nm.c_str(), bytes) || bytes.empty()) continue;
        gui::text::BuildTextArray(bytes.data(), bytes.size(), db);
    }

    bool any = false;
    for (int i = 0; i < n; ++i) {
        if (!keys[i] || !*keys[i]) continue;
        const int idx = db.FindIndex(keys[i]);
        if (idx >= 0) {
            const char* t = db.Text(idx);
            if (t && *t) { out[i] = t; any = true; }
        }
    }
    return any;
}

namespace {
// The drawSprite hook: resolve the gfx-id sprite and blit it into the surface.
bool DrawSpriteHook(render::Surface* s, int x, int y, int gfxId, void* userData) {
    auto* self = static_cast<MenuAssets*>(userData);
    if (!self || !self->loaded()) return false;
    const render::DecodedShape* sprite = self->SpriteForGfxId(gfxId);
    if (!sprite) return false;
    return BlitDecodedSprite(s, x, y, *sprite) > 0;
}
} // namespace

void MenuAssets::InstallHooks() {
    if (!loaded_) return;
    gui::MenuRenderHooks hooks;
    hooks.drawSprite = &DrawSpriteHook;
    hooks.userData = this;
    gui::SetMenuRenderHooks(hooks);
}

void MenuAssets::ClearHooks() {
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});  // inert default
}

} // namespace guild::play
