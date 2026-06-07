#pragma once
#include "render/skeleton.h"  // AnimHeader, AnimFrame layouts
#include <vector>

// Binary animation loader for the guild::render anim stage.
//
//   VIBE_ModelIo_LoadBinaryAnimation @0x5e450c (0xF5E bytes, "d3_LoadBinaryAnimation")
//
// The production loader streams from the VFS (VIBE_Vfs_OpenFile / ReadStream) through
// a one-byte token reader (VIBE_Script_ReadToken @0x5e3bd0) and 4-byte little-endian
// field reads (VIBE_Bio_ReadDwordSwapArgs @0x5dc8b0 — despite the name it does NOT
// byte-swap, it is a raw 4-byte read) and vec3 reads (VIBE_Bio_ReadVec3 @0x5dc938 =
// three raw floats). This reconstruction is a faithful 1:1 translation that reads the
// SAME token stream from an in-memory byte buffer instead of the VFS, so it is
// self-contained and golden-testable. The token IDs, field order, struct field
// offsets, and the post-load passes (segment durations, root-translation delta
// encoding, per-frame point AABB + quantization) are verbatim.
//
// Token reader semantics (VIBE_Script_ReadToken): one byte; if the read fails
// (end of buffer) the token is 43 (EOF); if the byte is > 0x3A it is 39 (invalid).
//
// Token IDs used by the format:
//   48 version marker   1 magic (must be <= 0xABCD0001)   35 frame count
//   51 has-vertex flag  36 has-uvface flag   55 attach count   54 sub-iteration count
//   52 vertex-count override   53 uvface-count override   41 start frame   42 end frame
//   24 frame index   25 vertex count   33 vertices (then 40 = done)
//   26 uvface count (then 34 / 40)   49 frame transform (6 floats @ frame+32..+52)
//   56 attach name   57 attach vec3 @+84   58 attach vec3 @+96
//   50 final pair (skipped)   47 end-of-chunk
namespace guild::render {

// A loaded animation: owns the header + frame array storage and exposes them.
struct Animation {
    AnimHeader              header{};
    std::vector<AnimFrame>  frames;
    // Per-frame morph point data, when present (vertexCount points * 3 floats each).
    // points[f] is frame f's decoded point array (delta-encoded from frame 0 when
    // the load flag is set, then quantized into the frame's wpoints in the engine).
    std::vector<std::vector<float>> points;
    bool valid = false;
};

// gilde.exe 0x5e450c — VIBE_ModelIo_LoadBinaryAnimation (byte-buffer reconstruction).
//   data/size : the raw .anim byte stream (the bytes VIBE_Vfs_ReadStream would yield).
//   name      : copied into header.name (StrNCopyPad 63).
//   loadFlag  : the original `a3@<bl>` (stored at header byte 361; selects the per-
//               frame point delta-encode post-pass).
// Returns true and fills `out` on a well-formed stream; false on a malformed one
// (matching the original's early returns: null/invalid magic).
bool LoadBinaryAnimation(const u8* data, size_t size, const char* name, u8 loadFlag,
                         Animation& out);

} // namespace guild::render
