#pragma once
// =============================================================================
// guild::play — load + decode the REAL main-menu artwork from gfx/gilde.gfx and
// install it behind gui::MenuRenderHooks so the native menu (sdl_menu.cpp) draws
// the shipped background + buttons instead of the asset-less reconstruction.
//
// The menu uses these gfx records (gfx id == archive record index):
//   #1773 _MENUE_BACKGROUND 800x600   — the backdrop ("DIE GILDE" town banner)
//   #174  _BUTTON_RED       (6 shapes) — the 8 menu buttons (3-slice caps)
//   #1776 _MAIN_MENU_RAHMEN 300x320   — the menu frame/border
// All are depth-2 shapes; render/gfx_archive.h DecodeShape reconstructs them
// (the VIBE_FrameTable_Index @0x5fbb24 codec).
//
// MenuAssets is inert when gilde.gfx is absent: load() returns false and the
// caller falls back to the flat-rect/font path.  When loaded, InstallHooks()
// wires a drawSprite hook that blits the decoded gfx-id sprite into the 32bpp
// menu framebuffer; the background bitmap is exposed for the backdrop blit.
// =============================================================================
#include "guild/common/types.h"
#include "render/gfx_archive.h"

#include <deque>
#include <string>
#include <vector>

namespace guild::shim { class IFileSystem; }
namespace guild::render { struct Surface; }

namespace guild::play {

// ===========================================================================
// MenuFont — the real engine bitmap font (`_FONT`, gfx record 66 in gilde.gfx).
//
// gilde.exe glyph model (recovered):
//   The `_FONT` SHAPBANK indexes glyphs by RAW character code: the glyph for
//   character `ch` is shape number `ch` (VIBE_Coord_Transform @0x5d8b00 adds the
//   per-shape offset blob+0x45+4*ch to the bank base — i.e. shape `ch`). So
//   CP1251 Cyrillic (0xC0..0xFF) lands on shapes 192..255 directly.
//
// Per-glyph metric fields (in the loaded SHAPBANK shape header):
//   +6  (u16) width    (= advance + 1; the drawn glyph bitmap width)
//   +22 (u16) kern     (leftBearing; subtracted before drawing)
//   +26 (u16) advance  (pen step after drawing)
//   font+46 (u16)      line height (dword_69FFB0)
//
// Spacing globals (gilde.exe, statically initialised):
//   dword_62D274 = 2  (tracking, added per non-'~' char and once at the end)
//   dword_62D270 = 8  (extra width added for a space char)
//
// MeasureWidth is VIBE_Property_Get @0x4152cc 1:1; DrawText is the pen-advance of
// VIBE_Property_Set @0x4159dc 1:1 (kern subtracted on every char; glyph blitted at
// the running pen; advance after). Glyphs are rendered from their real RLE shape
// bitmaps (recoloured to the requested text colour, alpha-keyed).
// ===========================================================================
class MenuFont {
public:
    static constexpr int kTracking   = 2;   // dword_62D274
    static constexpr int kSpaceExtra = 8;   // dword_62D270
    static constexpr int kCapW       = 12;  // _BUTTON_RED cap shape width (shapes 0,1)
    static constexpr int kRecomputePad = 4; // RecomputeSize @0x41b164: +4 over caps+text

    // Decode the `_FONT` record (default name "_FONT") straight from the gfx
    // archive file: read the directory header to locate the record, read just its
    // SHAPBANK blob, and pull per-glyph metrics (+6/+22/+26) + glyph bitmaps for
    // all char codes. Targeted reads (header + the ~150 KB font blob), so it does
    // not pull the whole 59 MB archive. Returns true iff the font decoded.
    bool Load(shim::IFileSystem& fs, const char* archivePath = "gfx/gilde.gfx",
              const char* fontName = "_FONT");
    bool loaded() const { return loaded_; }

    // VIBE_Property_Get @0x4152cc — text pixel width of a CP1251 byte string.
    // Mirrors the original exactly: per non-'~' char add tracking+advance (+space
    // extra for ' '); subtract kern when i>0 and the previous char was not a
    // space; add a final tracking. Returns 0 for null/empty.
    int MeasureWidth(const char* s) const;

    // _BUTTON_RED label button design width: MeasureWidth(label) + capL + capR + 4
    // (RecomputeSize sprite-kind-9 path; caps are 12 each => +28).
    int ButtonWidth(const char* label) const {
        return MeasureWidth(label) + kCapW + kCapW + kRecomputePad;
    }

    // Draw `s` into a 32bpp 0xAARRGGBB buffer starting at pen (x,y), point-scaled
    // by `scale` (>=1) for the menu's design->framebuffer upscale, using text colour
    // (r,g,b). Pen advance is VIBE_Property_Set @0x4159dc 1:1. The glyph bitmap is
    // recoloured to (r,g,b) and alpha-keyed by the shape's opaque pixels.
    void DrawText(u32* dst, int W, int H, int x, int y, const char* s,
                  int scale, u8 r, u8 g, u8 b) const;

    int lineHeight() const { return lineHeight_; }

private:
    struct Glyph {
        u16 width   = 0;   // +6
        u16 kern    = 0;   // +22
        u16 advance = 0;   // +26
        render::DecodedShape shape;  // glyph bitmap (RLE), opaque pixels = ink
        bool present = false;
    };
    bool loaded_ = false;
    int  lineHeight_ = 0;
    Glyph glyphs_[256];
};

// Resolve the eight main-menu button labels from a textbin archive mounted under
// `gameDir` (via shim::DiskFileSystem). The labels are the localized
// `_OPTIONEN_MENUE_*` strings (Text_O_Optionen.res); the button->key mapping is
// fixed by VIBE_Menu_RunMainMenu @0x529d08 (the dword_8C98xx label-array slots).
// `out[i]` is indexed by main-menu button order (row y {10,53,...,311}). Returns
// true iff the archive mounted and at least one label resolved; missing keys leave
// that slot empty (caller falls back to its built-in caption).
bool ResolveMainMenuLabels(const std::string& gameDir, std::string out[8]);

class MenuAssets {
public:
    // Load gfx/gilde.gfx under the mounted game dir via `fs` and decode the menu
    // records.  `archivePath` defaults to "gfx/gilde.gfx".  Returns true iff the
    // archive loaded AND the background decoded to a sane 800x600 image.  On
    // failure the object stays !loaded() and InstallHooks() is a no-op.
    bool Load(shim::IFileSystem& fs, const char* archivePath = "gfx/gilde.gfx",
              const char* bgName = "_MENUE_BACKGROUND");

    bool loaded() const { return loaded_; }

    // The decoded background (#1773): 0xAARRGGBB, row-major, w*h entries.
    const std::vector<u32>& background() const { return bg_.argb; }
    int backgroundWidth()  const { return bg_.width; }
    int backgroundHeight() const { return bg_.height; }

    // The button sprite-bank record (#174). frameCount() shapes are decoded.
    int buttonFrameCount() const { return (int)buttons_.size(); }
    const render::DecodedShape* buttonFrame(int i) const {
        return (i >= 0 && i < (int)buttons_.size()) ? &buttons_[i] : nullptr;
    }

    // The mouse cursor (#14 _MOUSE_CURSOR shape 0 — the gloved pointing hand,
    // 49x48, black==transparent). nullptr until loaded.
    const render::DecodedShape* cursor() const {
        return cursor_.width > 0 ? &cursor_ : nullptr;
    }

    // The real engine `_FONT` (gfx record 66) — glyph metrics + bitmaps for the
    // menu label text. Valid (font().loaded()) when the archive carried _FONT.
    const MenuFont& font() const { return font_; }

    // Decode an arbitrary record-by-gfx-id sprite (shape 0) on demand; returns
    // nullptr on failure.  Cached so the render hook is allocation-free per frame.
    const render::DecodedShape* SpriteForGfxId(int gfxId);

    // Decode shape `shapeNr` of the gilde.gfx record named `name` on demand
    // (cached). Used by the sub-screens (options/load/network) to pull the real
    // panel/slider/headline art (_OPTIONEN_PIC, _TOOL_TIP_BIG,
    // _SLIDER_GOLD_WAAGERECHT, _HEADLINE_GOLD_BIG, ...). Returns nullptr if the
    // record/shape is absent. Valid only while loaded().
    const render::DecodedShape* SpriteByName(const char* name, int shapeNr = 0);

    // The gold horizontal option-slider shapes (`_SLIDER_GOLD_WAAGERECHT`), as
    // consumed by the engine's slider draw VIBE_Entity_InteractionLogic @0x41078c
    // (Coord_Transform(gfx, n)):
    //   shape 0 = LEFT end cap, shape 5 = RIGHT end cap
    //   shape 1 = track fill (100x6), shape 3 = the thumb/knob (66x19)
    //   shape 6 / 7 = the left/right +/- button HOVER-pressed overlays
    // A bool / cycle option is the SAME slider with the thumb snapped to a discrete
    // position and the current option TEXT drawn at the thumb (it is NOT a separate
    // checkbox graphic). These accessors return nullptr if a shape is absent.
    static constexpr const char* kSliderGoldName = "_SLIDER_GOLD_WAAGERECHT";
    const render::DecodedShape* SliderCapLeft()  { return SpriteByName(kSliderGoldName, 0); }
    const render::DecodedShape* SliderCapRight() { return SpriteByName(kSliderGoldName, 5); }
    const render::DecodedShape* SliderTrack()    { return SpriteByName(kSliderGoldName, 1); }
    const render::DecodedShape* SliderThumb()    { return SpriteByName(kSliderGoldName, 3); }

    // Install this MenuAssets behind gui::SetMenuRenderHooks so RenderMenuForm /
    // RenderMainMenu paint the real gfx-id sprites.  Idempotent.  Pass the same
    // instance you keep alive for the whole menu loop (the hook holds a pointer).
    void InstallHooks();

    // Restore the inert default MenuRenderHooks (so tests / fallback are clean).
    static void ClearHooks();

private:
    bool loaded_ = false;
    render::GfxArchive archive_;
    render::DecodedShape bg_;
    std::vector<render::DecodedShape> buttons_;
    render::DecodedShape cursor_;
    MenuFont font_;

    // gfxId -> decoded shape0 cache (for the generic sprite hook).
    // std::deque: push_back keeps references to existing elements VALID, so the
    // pointers SpriteForGfxId/SpriteByName hand out stay alive across later
    // decodes (a std::vector would reallocate and dangle them — a real segfault
    // once a screen caches one sprite pointer then decodes another).
    std::deque<std::pair<int, render::DecodedShape>> spriteCache_;
    // "name#shape" -> decoded shape cache (for SpriteByName).
    std::deque<std::pair<std::string, render::DecodedShape>> nameCache_;
};

// Generalized localized-label resolver (the multi-key form of ResolveMainMenuLabels):
// mount the install's textbin under `gameDir`, build the text DB from every `.res`
// member, and resolve each `keys[i]` (e.g. "_OPTIONEN_GFX+0") to its localized
// string into `out[i]`. Unresolved keys leave `out[i]` empty. Returns true iff at
// least one key resolved. Used by the options/load/network sub-screens for their
// real captions (CP1251), exactly as the engine reads the same _OPTIONEN_* array.
bool ResolveOptionLabels(const std::string& gameDir, const char* const* keys, int n,
                         std::string* out);

// Blit a decoded sprite (0xAARRGGBB, A==0 transparent) into a 32bpp render::Surface
// at (x,y) with per-pixel transparency and clipping.  Returns pixels written.
// This is the asset-side counterpart of the engine's depth-2 shape blit.
int BlitDecodedSprite(render::Surface* s, int x, int y,
                      const render::DecodedShape& sprite);

} // namespace guild::play
