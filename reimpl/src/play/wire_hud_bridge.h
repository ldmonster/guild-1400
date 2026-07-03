#pragma once
// =============================================================================
// guild::play — REAL HUD / SPRITE / 2D-BLIT BRIDGE (PLAYABLE_PLAN P6).
//
// The "inert hooks" gap on the HUD path: the in-game HUD overlay renderer
// (play::hud_render — RenderHud) draws the bottom player-bar slot ICON and the
// map-marker ARTWORK through a single installable sprite hook,
// play::HudRenderHooks::drawSprite. Its DEFAULT slot is INERT (nullptr -> paints
// nothing): the deterministic track+fill+frame and the marker dot still draw, but
// no sprite-bank shape is ever blitted — exactly the engine's "the gfx-1403 icon
// bank is not present this frame" no-op.
//
// What the ORIGINAL does for the in-game HUD/sprite blit (IDA grounding):
//   * The bottom owned-object strip VIBE_PlayerBar_BuildContent @0x4b11e4 lays out
//     up-to-32 icon slots; each slot's icon is a sprite-bank SHAPE blitted into the
//     HUD surface.
//   * The per-shape blit bottoms out in the real 2D sprite leaf
//       VIBE_Shape_ShowFromBank   @0x5d861c  ("shp_ShowShapeFromBank")
//     which validates the shape index against the bank (count @+0x2A), resolves the
//     shape via the bank offset table (+0x45 = +4*idx+69), reads the shape depth
//     (+0x0C) and, for a row-RLE shape (depth 1), runs
//       VIBE_Shape_BlitColored16 @0x5d7164
//     to rasterise every opaque run into the destination 16bpp surface. (See the
//     decompile of 0x5d861c: depth<=1 -> ShapeBlitColored16(x,y,shape,surface).)
//
// Both leaves are ALREADY reconstructed faithfully in src/render:
//     render::ShapeShowFromBank   (src/render/sprite_scale.cpp, 0x5d861c)
//     render::ShapeBlitColored16  (src/render/shape_blit.cpp,   0x5d7164)
//
// InstallRealHudBridge() de-inerts the HUD by pointing
// play::HudRenderHooks::drawSprite at a trampoline that wraps the HUD render
// Surface as a render::ColorBlitTarget16 and calls the REAL ShapeShowFromBank over
// a real row-RLE sprite bank. Purely additive: it uses the PUBLIC setter
// play::SetHudRenderHooks (no owned file is edited). After install, the player-bar
// slot icon and the map-marker artwork are rasterised by the real 2D blit leaf
// instead of the inert no-op — sprite pixels that were ABSENT now appear.
//
// FINDING (documented, not worked around): the gfx-1403 icon ARTWORK is shipped in
// a gfx bank BIN that is not present in the available install (Resources/ has no
// .shp / icon bank), so the bridge builds a real row-RLE sprite bank IN-MEMORY in
// the exact on-disk bank/shape layout the real leaf consumes (count @+0x2A, offset
// table @+0x45; shape header width @+6 / height @+0xA / depth @+0xC=1; run table @
// +0x32). The bytes the blit consumes are real game-format bytes; only their
// provenance (synthesised vs loaded) differs, and that is the loader edge the
// brief names. Callers may also supply their OWN bank via SetHudSpriteBank.
// =============================================================================
#include "guild/common/types.h"
#include "render/colorformat.h"

#include <cstddef>

namespace guild::play {

// Install the REAL HUD/sprite bridge: point the inert HudRenderHooks::drawSprite
// slot at the real 2D sprite-bank blit leaf (render::ShapeShowFromBank ->
// ShapeBlitColored16). Idempotent. After this, RenderHud's slot-icon / marker
// artwork is rasterised by the real blit instead of the inert no-op.
void InstallRealHudBridge();

// Restore the inert default HudRenderHooks (clears the bridge). For tests that
// want to observe the inert-vs-real difference within one process.
void UninstallRealHudBridge();

// True while the real HUD/sprite bridge is installed.
bool RealHudBridgeInstalled();

// ---------------------------------------------------------------------------
// Bank access (so tests/callers can drive the exact same real leaf the installed
// hook routes to, and supply real artwork bytes).
// ---------------------------------------------------------------------------

// The in-memory real-format sprite bank the bridge blits by default (a single
// row-RLE shape index 0). Returns the bank base + its size in bytes. The bank is
// laid out exactly as VIBE_Shape_ShowFromBank reads it (count @+0x2A, offset table
// @+0x45, shape header @ shape+0/6/0xA/0xC, run table @ shape+0x32).
const u8* DefaultHudSpriteBank(std::size_t* outSize = nullptr);

// Override the sprite bank the bridge blits (e.g. a real loaded gfx bank). `bank`
// must remain valid for as long as the bridge is installed; pass nullptr to revert
// to the built-in DefaultHudSpriteBank.
void SetHudSpriteBank(const u8* bank);

// THE GAP IS CLOSED: feed a REAL gilde.gfx SHAPBANK blob (pixel-format 2 / 0)
// through the REAL conversion chain — render::ShapeBankConvertNew @0x5d80a8 ->
// render_leaves9 Shape_ConvertToNew @0x5d8080 -> render::ShapeConvertRgbTo16
// @0x5d7c0c / ShapeConvert8To16 @0x5d7924 (src/render/shape_convert16) -> the
// real ShapeBankAddShape @0x5d8330 — and make the resulting depth-1 bank the
// active HUD sprite bank (exactly the lazy convert VIBE_State_Helper @0x40e014
// runs after d2_LoadObj). Installs the converters into RenderLeaves9Hooks first
// (rule 13). Returns the converted bank (BRIDGE-OWNED; released on the next
// call or on nullptr), or nullptr on a degenerate blob. Pass (nullptr, 0) to
// release the converted bank and revert to the prior SetHudSpriteBank state.
const u8* SetHudSpriteBankFromGfx(const u8* bankBlob, std::size_t blobSize);

// Sprite-id -> bank-shape mapping for the installed hook. The default (base < 0)
// keeps the legacy single-shape behaviour: every gfx id blits shape 0. With a
// base set, the hook blits shape (gfxId - base) and DECLINES ids outside
// [base, base + bankShapeCount) — the real multi-shape indexing the engine's
// icon-object ids use (building icon object id = building code + 1010, the
// gui/infopanel_build kIconObjBias).
void SetHudSpriteIdBase(int base);
int  HudSpriteIdBase();

// Blit one shape of the active bank straight into a 16bpp pixel buffer via the
// REAL render::ShapeShowFromBank leaf (the exact call the installed hook makes).
// `pixels`/`widthPx` describe the destination 16bpp surface; `fmt` is its colour
// format. Returns the leaf's result (1 drawn / 0 invalid). Exposed for tests.
int BlitHudSprite(int x, int y, int shapeIndex, u16* pixels, int widthPx,
                  const render::ColorFormat& fmt);

} // namespace guild::play
