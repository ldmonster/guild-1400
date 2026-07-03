// =============================================================================
// guild::play — REAL HUD / SPRITE / 2D-BLIT BRIDGE implementation.
// See wire_hud_bridge.h. Additive: wires play::HudRenderHooks::drawSprite (a
// public installable hook) to the REAL reconstructed 2D sprite-bank blit leaf
// render::ShapeShowFromBank (0x5d861c) -> ShapeBlitColored16 (0x5d7164), via the
// PUBLIC setter play::SetHudRenderHooks. No owned file is edited.
// =============================================================================
#include "play/wire_hud_bridge.h"

#include "play/hud_render.h"              // HudRenderHooks + SetHudRenderHooks (public)
#include "render/shape.h"                // ShapeDecodeRle (0x5d70cc)
#include "render/sprite_scale.h"          // ShapeShowFromBank (0x5d861c)
#include "render/shape_blit.h"            // ColorBlitTarget16, ShapeBlitColored16
#include "render/shape_convert16.h"       // ShapeBankConvertNew (0x5d80a8) + leaves9 install
#include "render/animation_decode.h"      // FrameBlitState
#include "render/types.h"                 // render::Surface

#include <cstdlib>
#include <cstring>
#include <vector>

namespace guild::play {

namespace {

inline void PutU16(u8* b, u16 v) { std::memcpy(b, &v, 2); }
inline void PutU32(u8* b, u32 v) { std::memcpy(b, &v, 4); }
inline u16  GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }
inline u32  GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }

// ---------------------------------------------------------------------------
// Build a REAL-FORMAT sprite bank holding one row-RLE shape, laid out exactly as
// VIBE_Shape_ShowFromBank @0x5d861c reads it:
//   bank +0x2A  u16  shapeCount  (max valid index; ShowFromBank tests idx > count)
//   bank +0x45  u32  offset[i]   (bank-relative byte offset of shape i)
//   shape +0x06 u16  width       (pixels per row)
//   shape +0x0A u16  height      (rows)
//   shape +0x0C u8   depth       (1 = row-RLE -> BlitColored16 path)
//   shape +0x32 u32  row[0] runCount, then runs {u32 skip; u32 nPixels; u16 px[n]}
//                    (skip>>1 transparent pixels), next row's runCount follows.
// The shape is a `w`x`h` solid block (one opaque run per row, skip=0). The blit
// leaf recolours every opaque pixel to grey luma, so the destination shows a
// non-zero rectangle — observable sprite output that the inert hook never draws.
// ---------------------------------------------------------------------------
std::vector<u8> BuildBank(int w, int h, u16 fillPixel) {
    // Shape body: header (0x32 bytes) + run table.
    // Run table: h rows, each = u32 runCount(=1) + one run {u32 skip(=0);
    //            u32 nPixels(=w); u16 px[w]}.
    const std::size_t rowBytes = 4u + 4u + 4u + 2u * static_cast<std::size_t>(w);
    const std::size_t shapeSize = 0x32u + rowBytes * static_cast<std::size_t>(h);

    // Bank: place the single shape at a fixed offset past the header/offset table.
    // The offset table starts at +0x45; one entry (4 bytes) -> shape can start at
    // 0x49 onward. Use 0x80 for headroom (matches the original's padded banks).
    const std::size_t shapeOff = 0x80u;
    std::vector<u8> bank(shapeOff + shapeSize, 0);

    // Bank header: shape count (max valid index). One shape -> index 0 valid; the
    // leaf rejects idx > count, so count >= 0 admits index 0.
    PutU16(&bank[0x2A], 0);                       // shapeCount field (max index)
    PutU32(&bank[0x45], static_cast<u32>(shapeOff)); // offset[0]

    // Shape header.
    u8* shp = &bank[shapeOff];
    PutU16(shp + 0x06, static_cast<u16>(w));     // width
    PutU16(shp + 0x0A, static_cast<u16>(h));     // height
    shp[0x0C] = 1;                                // depth = 1 -> RLE BlitColored16

    // Run table: one full-width opaque run per row.
    u8* p = shp + 0x32;
    for (int row = 0; row < h; ++row) {
        PutU32(p, 1);            p += 4;          // runCount = 1
        PutU32(p, 0);            p += 4;          // skip = 0 (no leading transparent)
        PutU32(p, static_cast<u32>(w)); p += 4;   // nPixels = w
        for (int c = 0; c < w; ++c) { PutU16(p, fillPixel); p += 2; }
    }
    return bank;
}

// The default bank: an 8x8 solid sprite (a stand-in for the gfx-1403 slot icon /
// gfx-1404 map-marker artwork). 0xFFFF (white) so the grey-luma recolour the real
// BlitColored16 applies still produces a clearly non-zero block.
const std::vector<u8>& DefaultBank() {
    static const std::vector<u8> kBank = BuildBank(8, 8, 0xFFFF);
    return kBank;
}

// Active bank (process-static). nullptr -> DefaultBank().
const u8* g_bank = nullptr;
int g_spriteIdBase = -1;   // <0 == legacy: every id -> shape 0 (SetHudSpriteIdBase)
bool      g_installed = false;

// ---------------------------------------------------------------------------
// The trampoline matching HudRenderHooks::drawSprite. Wrap the HUD surface as a
// render::ColorBlitTarget16 (the real engine HUD surface is 16bpp: +0x10 widthPx,
// +0x1C u16* pixels) and call the REAL render::ShapeShowFromBank. `gfxId` selects
// the bank shape index (the slot icon and marker pass distinct ids in hud_render;
// the single-shape bank maps both to index 0). Returns true if a shape was drawn.
// ---------------------------------------------------------------------------
bool HookDrawSprite(render::Surface* s, int x, int y, int gfxId, void* /*user*/) {
    // Debug tracing gate, resolved once (hot path — a getenv per blit is waste).
    static const bool kTrace = std::getenv("GUILD_SELECT_FIRST") != nullptr;
    if (!s || !s->pixels)
        return false;

    const u8* bank = g_bank ? g_bank : DefaultBank().data();

    render::ColorBlitTarget16 dst;
    dst.widthPx = s->widthPx;                                    // +0x10
    dst.pixels  = reinterpret_cast<u16*>(s->pixels);            // +0x1C

    // Shape index: legacy single-shape mode maps every id to 0; with an id base
    // set (SetHudSpriteIdBase), the hook indexes by (gfxId - base) and declines
    // out-of-range ids — the engine's icon-object indexing (code + kIconObjBias).
    int shapeIndex = 0;
    if (g_spriteIdBase >= 0) {
        shapeIndex = gfxId - g_spriteIdBase;
        if (shapeIndex < 0 || shapeIndex > (int)GetU16(bank + 0x2A)) {
            if (std::getenv("GUILD_SELECT_FIRST"))
                std::printf("  [sprite] id=%d -> shape=%d DECLINE range (count=%u)\n",
                            gfxId, shapeIndex, (unsigned)GetU16(bank + 0x2A));
            return false;
        }
    }

    // PRE-CLIP, exactly as the original HUD callers do before VIBE_Shape_ShowFromBank
    // (the unscaled BlitColored16 path does NO clipping of its own — "callers
    // pre-clip"). Resolve the shape's width@+6 / height@+0xA from the bank offset
    // table and skip the blit unless the whole block lands inside the surface; an
    // off-surface HUD position (a slot row past the strip / a marker off the map)
    // is simply not drawn, matching the engine's clip behaviour.
    if (static_cast<u16>(shapeIndex) > GetU16(bank + 0x2A))
        return false;
    const u32 shapeOff = GetU32(bank + 4 * shapeIndex + 0x45);
    const u8* shp = bank + shapeOff;
    const int sw = static_cast<int>(GetU16(shp + 0x06));
    const int sh = static_cast<int>(GetU16(shp + 0x0A));
    if (x < 0 || y < 0 || x + sw > s->widthPx || y + sh > s->height) {
        if (kTrace)
            std::printf("  [sprite] id=%d shape=%d DECLINE clip x=%d y=%d sw=%d sh=%d fb=%dx%d\n",
                        gfxId, shapeIndex, x, y, sw, sh, s->widthPx, s->height);
        return false;
    }

    // Id-mapped mode (real icon banks, e.g. the GEB building thumbnails):
    // the card icon is a PLAIN colour blit — VIBE_Shape_DecodeRle @0x5d70cc —
    // not the grey-recolouring BlitColored16 the legacy marker path uses.
    if (g_spriteIdBase >= 0) {
        render::BlitTarget16 pd;
        pd.widthPx = s->widthPx;
        pd.pixels  = reinterpret_cast<u16*>(s->pixels);
        render::ShapeDecodeRle(x, y, shp, pd);
        return true;
    }
    render::FrameBlitState st;   // clip rect unused by the unscaled BlitColored16 path
    int rv = render::ShapeShowFromBank(x, y, bank, shapeIndex, dst, s->fmt, st);
    return rv != 0;
}

const HudRenderHooks& BridgeHooks() {
    static const HudRenderHooks kHooks = []{
        HudRenderHooks h;
        h.drawSprite = &HookDrawSprite;
        h.userData   = nullptr;
        return h;
    }();
    return kHooks;
}

} // namespace

// ---------------------------------------------------------------------------
void InstallRealHudBridge() {
    SetHudRenderHooks(BridgeHooks());
    g_installed = true;
}

void UninstallRealHudBridge() {
    SetHudRenderHooks(HudRenderHooks{});   // drawSprite == nullptr -> inert
    g_installed = false;
}

bool RealHudBridgeInstalled() { return g_installed; }

// ---------------------------------------------------------------------------
const u8* DefaultHudSpriteBank(std::size_t* outSize) {
    const std::vector<u8>& b = DefaultBank();
    if (outSize) *outSize = b.size();
    return b.data();
}

void SetHudSpriteBank(const u8* bank) { g_bank = bank; }

void SetHudSpriteIdBase(int base) { g_spriteIdBase = base; }
int  HudSpriteIdBase() { return g_spriteIdBase; }

// ---------------------------------------------------------------------------
// THE GAP IS CLOSED (see header): real depth-2/-0 gilde.gfx banks now convert
// to depth-1 through the REAL VIBE chain and feed the REAL blit leaf.
namespace {
u8* g_convertedBank = nullptr;   // ShapeBankConvertNew result (malloc'd, owned here)
}

const u8* SetHudSpriteBankFromGfx(const u8* bankBlob, std::size_t blobSize) {
    // Release the previously converted bank (and detach it if active).
    if (g_convertedBank) {
        if (g_bank == g_convertedBank) g_bank = nullptr;
        std::free(g_convertedBank);
        g_convertedBank = nullptr;
    }
    if (!bankBlob || blobSize == 0)
        return nullptr;                       // revert-only call

    // Loader-edge validation (not engine logic): ShapeBankConvertNew copies the
    // 0x845-byte bank header region, so a shorter blob never reaches it.
    if (blobSize < 0x845 || std::memcmp(bankBlob, "SHAPBANK", 8) != 0)
        return nullptr;

    // Rule-13 wiring: the converters must sit in the RenderLeaves9Hooks slots so
    // the 0x5d8080 driver dispatches the REAL 0x5d7c0c / 0x5d7924. Idempotent.
    render::InstallShapeConvertersIntoLeaves9();

    // Own a malloc copy so the real free path of ShapeBankConvertNew (the
    // original's `if (!ebx) VIBE_Memory_FreeDebug(bank)`) runs faithfully.
    u8* raw = static_cast<u8*>(std::malloc(blobSize));
    if (!raw) return nullptr;
    std::memcpy(raw, bankBlob, blobSize);

    u8* converted = render::ShapeBankConvertNew(raw, 1, /*keepSource=*/false);
    if (!converted) {                         // writeCursor == 0 — not converted,
        std::free(raw);                       // source NOT freed by the original
        return nullptr;
    }
    // (converted == raw means the bank was already pixel-format 1 — it is then
    //  owned here verbatim, exactly the original's pass-through return.)
    g_convertedBank = converted;
    g_bank = g_convertedBank;
    return g_convertedBank;
}

int BlitHudSprite(int x, int y, int shapeIndex, u16* pixels, int widthPx,
                  const render::ColorFormat& fmt) {
    if (!pixels) return 0;
    const u8* bank = g_bank ? g_bank : DefaultBank().data();
    render::ColorBlitTarget16 dst;
    dst.widthPx = widthPx;
    dst.pixels  = pixels;
    render::FrameBlitState st;
    return render::ShapeShowFromBank(x, y, bank, shapeIndex, dst, fmt, st);
}

} // namespace guild::play
