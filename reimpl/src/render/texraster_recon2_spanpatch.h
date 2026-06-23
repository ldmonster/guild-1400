#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — texraster_recon2: the SELF-MODIFYING span-constant patchers
// for the BLEND / MASKED / OR variants of the textured-span rasterizer.
//
// gilde.exe contains six near-identical patchers. The plain one (0x5F7500
// VIBE_Raster_PatchSpanConstantsTextured) is already modelled by
// BuildSpanTexParams() in src/render/raster_textured.{h,cpp}. The FIVE remaining
// variants are reconstructed here:
//
//   0x5f753f  VIBE_Raster_PatchSpanConstantsMasked        (-> loc_5F72xx span)
//   0x5f757e  VIBE_Raster_PatchSpanConstantsBlend         (-> loc_5F72Cx span)
//   0x5f75bd  VIBE_Raster_PatchSpanConstantsBlendMasked   (-> loc_5F73xx span)
//   0x5f75fc  VIBE_Raster_PatchSpanConstantsOr            (-> loc_5F73Dx span)
//   0x5f763b  VIBE_Raster_PatchSpanConstantsOrMasked      (-> loc_5F744x span)
//
// Each OVERWRITES the SAME six immediate operands embedded in its variant's span
// machine code with the SAME six live per-triangle source globals:
//
//   imm@(+2 of a mov)  <- unk_1406A8C   texel byte-array base   (texBase)
//   imm@(+1 of a mov)  <- dword_1406A78 index->16bpp palette LUT (palBase)
//   imm@(+1 of a mov)  <- dword_13FC594 U fractional step        (uStepFrac)
//   imm@(+2 of a mov)  <- dword_13FC5A8 light/U accumulator start (lightStart)
//   imm@(+2 of a mov)  <- dword_1406A88 texel-index wrap mask     (texelMask)
//   imm@(+2 of a mov)  <- byte_1407A91  texture-width log2 shift  (widthShift)
//
// SELF-MODIFYING CODE = PLATFORM BOUNDARY (Rule 3): we do NOT patch live machine
// code. Following the exact precedent set by raster_textured.cpp's
// BuildSpanTexParams (the plain variant), the SMC delivery is translated to
// filling a parameter struct — here SpanPatchVariantParams — from the six source
// globals. The variant tag records WHICH span loop the original would have
// patched. The values written are bit-identical; only the delivery mechanism
// (struct field vs. code immediate) differs.
//
// CONSTANTS RECOVERED (get_bytes / disasm): all six patchers move the identical
// six source globals; only the destination code addresses differ per variant.
// =============================================================================
namespace guild::render {

// Which span-loop variant a patcher targets (the original picked the variant by
// the blend/mask mode of the polygon; all share the six-immediate layout).
enum class SpanVariant : u8 {
    Masked       = 0,  // 0x5f753f -> loc_5F72xx
    Blend        = 1,  // 0x5f757e -> loc_5F72Cx
    BlendMasked  = 2,  // 0x5f75bd -> loc_5F73xx
    Or           = 3,  // 0x5f75fc -> loc_5F73Dx
    OrMasked     = 4,  // 0x5f763b -> loc_5F744x
};

// The six live source globals each patcher reads (carrying their addresses).
struct SpanPatchSources {
    const u8*  texBase    = nullptr; // unk_1406A8C
    const u16* palBase    = nullptr; // dword_1406A78
    i32        uStepFrac  = 0;       // dword_13FC594
    i32        lightStart = 0;       // dword_13FC5A8
    u32        texelMask  = 0;       // dword_1406A88
    u8         widthShift = 0;       // byte_1407A91
};

// The reconstructed "patched" parameters for one span-loop invocation. This is
// the struct-field analogue of the six immediates the SMC patcher wrote.
struct SpanPatchVariantParams {
    SpanVariant variant   = SpanVariant::Masked;
    const u8*   texBase    = nullptr; // <- unk_1406A8C
    const u16*  palBase    = nullptr; // <- dword_1406A78
    i32         uStepFrac  = 0;       // <- dword_13FC594
    i32         lightStart = 0;       // <- dword_13FC5A8
    u32         texelMask  = 0;       // <- dword_1406A88
    u8          widthShift = 0;       // <- byte_1407A91
};

// ---------------------------------------------------------------------------
// Build the patched params for a given variant from the six source globals.
// This is the portable 1:1 reconstruction of all five PatchSpanConstants*
// functions (they differ only in their `variant`). The same source global lands
// in the same logical field across every variant, mirroring the binary exactly.
// ---------------------------------------------------------------------------
SpanPatchVariantParams BuildSpanPatchParams(SpanVariant variant,
                                            const SpanPatchSources& src);

// Thin named wrappers, one per original address, for call-site fidelity / wiring.
SpanPatchVariantParams PatchSpanConstantsMasked(const SpanPatchSources& src);      // 0x5f753f
SpanPatchVariantParams PatchSpanConstantsBlend(const SpanPatchSources& src);       // 0x5f757e
SpanPatchVariantParams PatchSpanConstantsBlendMasked(const SpanPatchSources& src); // 0x5f75bd
SpanPatchVariantParams PatchSpanConstantsOr(const SpanPatchSources& src);          // 0x5f75fc
SpanPatchVariantParams PatchSpanConstantsOrMasked(const SpanPatchSources& src);    // 0x5f763b

// ---------------------------------------------------------------------------
// gilde.exe 0x5f71ac / 0x5f74ff — VIBE_Raster_NullStub17 / NullStub18.
// One-byte functions: a bare `retn`. They are the dispatch slots the engine
// pokes a real span-loop body into (the SMC machinery's no-op default). Inert.
// ---------------------------------------------------------------------------
void Raster_NullStub17();  // 0x5f71ac
void Raster_NullStub18();  // 0x5f74ff

} // namespace guild::render
