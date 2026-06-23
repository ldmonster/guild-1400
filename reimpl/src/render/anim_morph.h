#pragma once
#include "guild/common/types.h"
#include <vector>
#include <string>

// =============================================================================
// guild::render — morph-anim builder (gilde.exe d3_anim.c).
//
//   0x5cf150  VIBE_Anim_CreateMorphAnim
//
// CreateMorphAnim allocates a 364-byte (0x16C) morph-anim record plus a frame
// sub-buffer (192 bytes * frameStride, frameStride fixed = 2) and fills it from:
//   a1 : SOURCE anim node     (reads +64 point-list ptr, +68 point count, +80..+100)
//   a2 : DEST anim/object     (reads +64 bone-name table, +324 bone count)
//   a3 : alt morph endpoint   (used only when a6 == 0; bone table + +64..)
//   a4 : control float array  (47 dwords; a4[45]/a4[47] gate the WPoints/Points blobs,
//                              a4[2..13]/a4[15..20] are the keyframe parameter blocks)
//   a5 : alt morph param block (used only when a6 == 0)
//   a6 : primary morph endpoint (when nonzero: bone table + transform at +64..+100)
//   a7 : name string (copied into record +0)
//   a8 : frame count (>1 required)
//
// The genuinely interesting math is the morph-delta quantization
// (0x5cf7c8..0x5cfa57): per source point it computes the (target-source) delta
// vector, tracks the component-wise min/max over all points, then re-quantizes
// each delta to a 3-byte value  b = (delta - min) * 255 / range  (truncated toward
// zero by VIBE_Coord_ConvertX), storing the per-axis scale = range * (1/255) and
// the per-axis min into the frame buffer. This is reconstructed bit-for-bit.
//
// The allocation / engine global-list insertion (dword_13FC8E4) and the raw
// engine struct shapes of a1/a2/a3/a5/a6 are modelled as byte buffers so the
// kernel is byte-faithful and testable in isolation. See anim_morph.cpp.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Source anim node view (a1). Only the fields CreateMorphAnim reads:
//   +64  pointList   float* (3 floats per point, stride 24 bytes in the source)
//   +68  numPoints   int    (record dword[80])
//   +80..+100        6 floats: the base transform (pos+rot), subtracted from a6's
// ---------------------------------------------------------------------------
struct MorphSrcNode {
    const float* pointList = nullptr; // +64 (each point at stride 24 bytes / 6 floats)
    i32 numPoints = 0;                // +68
    float xform[6] = {0, 0, 0, 0, 0, 0}; // +80,+84,+88,+92,+96,+100
};

// Dest anim/object view (a2). Reads the bone-name table at +64 (64-byte stride,
// 16 dwords) and the bone count at +324.
struct MorphDestNode {
    const char* boneNames = nullptr; // +64 (entry stride 64 bytes)
    i32 numBones = 0;                // +324 (record dword[81])
};

// One matched morph endpoint bone: name + 6 dwords of matrix data (a6+ v44[45..50]
// when a6 != 0, or a5/a3 v74[21..26] when a6 == 0). Copied verbatim into the frame
// buffer at +276 + 24*boneIndex when the bone name matches a dest bone name.
struct MorphEndpointBone {
    const char* name = nullptr; // 64-byte name entry
    u32 mat[6] = {0, 0, 0, 0, 0, 0};
};

// Morph endpoint (a6 primary, or a5/a3 alt). Carries the bone match table plus the
// transform (a6+80..+100) used to derive the +224..+244 position/rotation deltas,
// and the per-point target positions (a6+64 point list) for the morph-delta blob.
struct MorphEndpoint {
    const MorphEndpointBone* bones = nullptr; // bone-name match table
    i32 numBones = 0;
    float xform[6] = {0, 0, 0, 0, 0, 0};      // +80..+100 (a6 path) or a5[8..13]
    const float* pointList = nullptr;          // +64 point list (stride 24 / 6 floats)
};

// The control float array a4 (only the gated/copied fields modelled):
//   a4[45] (== ctrl.wpoints) : when nonzero, build the 3-byte WPoints blob path
//   a4[47] (== ctrl.points)  : when nonzero, build the 12-byte Points blob path
//   a4[2..7]  : 6 floats copied to frame +8..+28  (WPoints path only)
//   a4[8..13] : 6 floats copied to frame +32..+52 (always)
//   a4[15+6*i .. 20+6*i] : per-bone 6-float keyframe block -> frame +84+24*i
// In the binary a4[45]/a4[47] are pointers to the alt source blobs; here we model
// the gate as a bool plus (for the a6==0 path) the source blob pointers on a5.
struct MorphControl {
    bool hasWPoints = false;     // a4[45] != 0
    bool hasPoints = false;      // a4[47] != 0
    float blk2[6] = {0,0,0,0,0,0};   // a4[2..7]
    float blk8[6] = {0,0,0,0,0,0};   // a4[8..13]
    const float* boneBlocks = nullptr; // a4[15..] (6 floats * numBones, stride 24 bytes)
};

// The built morph-anim record. We expose the frame buffer as a flat byte vector so
// every offset write matches the binary exactly; named getters aid the tests.
struct MorphAnim {
    std::vector<u8> frame;     // the 192*frameStride frame buffer (record dword[87])
    std::vector<u8> wpoints0;  // frame[+180] blob: 3*numPoints bytes (a4[45] path)
    std::vector<u8> points0;   // frame[+188] blob: 12*numPoints bytes (a4[47] path)
    std::vector<u8> wpoints1;  // frame[+372] blob: 3*numPoints bytes (quantized morph)
    std::vector<u8> points1;   // frame[+380] blob: 12*numPoints bytes (float morph delta)
    std::string name;          // record +0
    i32 numPoints = 0;         // record dword[80]
    i32 numBones = 0;          // record dword[81]
    i32 frameStride = 2;       // record dword[82]

    float* frameF(int byteOff) { return reinterpret_cast<float*>(frame.data() + byteOff); }
    u32*   frameD(int byteOff) { return reinterpret_cast<u32*>(frame.data() + byteOff); }
};

// gilde.exe 0x5cf150 — VIBE_Anim_CreateMorphAnim. Returns a built record (heap),
// or nullptr on the early reject (0x5cf18b):
//   !a4 || !a1 || (!a6 && (!a3 || !a5)) || a8 <= 1.
// `prim` is the a6 endpoint (nullptr => the a3/a5 "alt" path is taken, requiring
// `alt`). `a8` is the frame count.
MorphAnim* CreateMorphAnim(const MorphSrcNode& src, const MorphDestNode& dest,
                           const MorphControl& ctrl, const MorphEndpoint* prim,
                           const MorphEndpoint* alt, const char* name, int frames);

// Quantize one morph delta component, exactly as 0x5cf9c3..0x5cfa48:
//   b = (delta - min) * 255.0 / range   then VIBE_Coord_ConvertX (truncate toward 0),
//   narrowed to a byte (the binary stores the low byte; no clamp).
u8 MorphQuantizeByte(float delta, float minV, float range);

} // namespace guild::render
