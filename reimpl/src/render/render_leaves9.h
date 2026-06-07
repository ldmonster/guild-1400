#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — render-math leaves, batch 9 (gilde.exe vector/shape/view/decimal).
//
// A ninth slice of self-contained, deterministic leaves translated 1:1 from the
// Hex-Rays reference (cross-checked against the raw disassembly for the
// register-passed 96-bit decimal accumulator). Each function is pure arithmetic
// / pointer-graph bookkeeping over caller-supplied records or module-local view
// state — no DDraw/GDI/device calls — so it is golden-testable in isolation.
//
// (NOTE: the VIBE_Render_GetViewParam* getters at 0x5af260..0x5af290 are already
// translated in render_leaves3.cpp as GetViewParamA..G; they are NOT re-done here.)
//
// Translated functions (each verified UNTRANSLATED at time of writing, by both
// address AND bare name across all of src/, case-insensitively):
//   0x407414  VIBE_Vector_Copy3            (copy 3 dwords vec src -> dst)
//   0x5d4bf8  VIBE_Shape_CopyPaletteHeader (header-driven palette blit into shape)
//   0x5de760  VIBE_Render_RetTrue          (constant true)
//   0x5fa684  VIBE_Math_ShiftAccumulate    (normalize 96-bit -> 64-bit mantissa)
//   0x5fa622  VIBE_Math_ParseDecimal       (ASCII digits -> 80-bit extended float)
//   0x5d8968  VIBE_Shape_DrawFromBankMode1 (temp draw-mode=1 around frame draw)
//   0x5d88e4  VIBE_Shape_DrawFromBankMode5 (temp draw-mode=5 around frame draw)
//   0x5d8080  VIBE_Shape_ConvertToNew      (depth-dispatch to 16-bit converters)
//
// Cross-module callees that ARE reconstructed elsewhere but are routed through an
// installable RenderLeaves9Hooks here (so this file stays standalone-linkable and
// integration tests wire the real siblings):
//   VIBE_FrameData_Process  (0x5d781c, render/animation_decode) — the bank frame
//                           draw call DrawFromBankMode1/5 brackets.
//   VIBE_Shape_ConvertRgbTo16 (0x5d7c0c) / VIBE_Shape_Convert8To16 (0x5d7924) —
//                           the two depth-conversion leaves ConvertToNew selects.
// =============================================================================

namespace guild::render {

// ---------------------------------------------------------------------------
// Installable hooks for the not-locally-defined siblings. Defaults are inert
// (return 0 / no-op); tests install captured/real implementations.
// ---------------------------------------------------------------------------
struct RenderLeaves9Hooks {
    // VIBE_FrameData_Process(a3,a2,framePtr,a4) — the actual frame rasterizer.
    int  (*FrameDataProcess)(int a3, int a2, void* framePtr, int a4);
    // VIBE_Shape_ConvertRgbTo16(shape) -> converted buffer.
    void* (*ConvertRgbTo16)(void* shape);
    // VIBE_Shape_Convert8To16(shape) -> converted buffer.
    void* (*Convert8To16)(void* shape);
};
RenderLeaves9Hooks& Leaves9Hooks();
void SetLeaves9Hooks(const RenderLeaves9Hooks& h);
void ResetLeaves9Hooks();

// 0x407414 — VIBE_Vector_Copy3. Copies dst[0..2] = src[0..2] (raw dwords; the
// original is element-typed but byte-identical for float or int vectors).
// Returns dst.
u32* Vector_Copy3(u32* dst, const u32* src);

// 0x5d4bf8 — VIBE_Shape_CopyPaletteHeader. Copies 4 * palette[count] bytes from
// `src` into the shape blob at `shape + *(u32*)(shape+42)`, where the palette
// entry count is the u16 at shape+10. Returns the byte count copied.
//   count   = *(u16*)(shape + 10)
//   dstOff  = *(u32*)(shape + 42)
//   bytes   = 4 * count
//   memcpy(shape + dstOff, src, bytes)
u32 Shape_CopyPaletteHeader(const void* src, void* shape);

// 0x5de760 — VIBE_Render_RetTrue. Constant predicate returning 1.
i8  Render_RetTrue();

// Output record of the 80-bit extended-precision conversion below. mantissaLo /
// mantissaHi are the low/high 32 bits of the 64-bit significand (edx:eax in the
// original) and exponent is the 16-bit field stored at +8.
struct Extended80 {
    u32 mantissaLo;   // stored at +0
    u32 mantissaHi;   // stored at +4
    u16 exponent;     // stored at +8
};

// 0x5fa684 — VIBE_Math_ShiftAccumulate. Normalizes a 96-bit unsigned magnitude
// (hi:mid:lo) into a left-justified 64-bit significand (returned via hiOut/loOut)
// and an adjusted 16-bit exponent (returned via expOut). Returns the new low
// dword of the significand (eax), exactly as the original. On an all-zero input
// the value is returned unchanged and the exponent is left untouched.
//   hi  = edx (a2), mid = eax (result), lo = ebp (a3), exp = edi (a4)
u32 ShiftAccumulate(u32 mid, u32 hi, u32 lo, i32 exp,
                    u32* hiOut, u32* loOut, u16* expOut);

// 0x5fa622 — VIBE_Math_ParseDecimal. Reads a NUL-terminated run of ASCII decimal
// digits (only the low nibble of each byte is used, matching the original), folds
// them into a 96-bit accumulator (value = value*10 + digit), then normalizes via
// ShiftAccumulate with a base exponent of 0x405E (16478) to produce an 80-bit
// extended-precision value. Returns the significand-low dword.
u32 ParseDecimal(const char* digits, Extended80* out);

// 0x5d8968 — VIBE_Shape_DrawFromBankMode1. Validates the shape index against the
// bank entry count (u16 @ bank+42), temporarily forces the frame record's
// draw-mode byte (@ frame+13) to 1, invokes FrameDataProcess on the frame, then
// restores the original draw-mode byte. Returns 0 if the index is out of range,
// else 1. (a3/a2/a4 are the rasterizer parameters threaded straight through.)
i32 Shape_DrawFromBankMode1(void* bank, int a2, int a3, int a4, u8 shapeIdx);

// 0x5d88e4 — VIBE_Shape_DrawFromBankMode5. As Mode1 but forces draw-mode 5. The
// original also logs an error via sprintf on an invalid index; we keep the same
// return contract (0 on invalid index, 1 otherwise) without the log side effect.
i32 Shape_DrawFromBankMode5(void* bank, int a2, int a3, int a4, u8 shapeIdx);

// 0x5d8080 — VIBE_Shape_ConvertToNew. Depth dispatcher: if shape depth (@+12) is
// 2 and target==1, calls ConvertRgbTo16; if depth is 0 and target==1, calls
// Convert8To16; otherwise returns null. Returns the converted buffer (or null).
void* Shape_ConvertToNew(void* shape, i8 target);

} // namespace guild::render
