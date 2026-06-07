#pragma once
#include "guild/common/types.h"
#include "render/skeleton.h"   // AnimFrame (192-byte keyframe stride)

// =============================================================================
// guild::render — the name-keyed bone-matrix PALETTE builder. Faithful 1:1
// reconstruction of the gilde.exe (d3_engine.c) bone-matrix accumulator:
//
//   0x5cc0d0  VIBE_Anim_ComputeBoneMatrices
//
// WHAT IT DOES
// ---------------------------------------------------------------------------
// After VIBE_Anim_UpdateSkeletonPose has advanced every per-bone track, this
// routine collapses up to FOUR animation "groups" (each a triple of 116-byte
// bone tracks) into a small palette of up to FOUR 164-byte accumulation records,
// keyed by BONE NAME. Each distinct bone name gets one palette record; tracks
// that target the same bone name accumulate their per-frame translation +
// rotation contributions into the shared record (VIBE_Math_VectorLerp blends the
// from/to keyframes by the sub-frame phase, MatrixFromEuler builds the per-frame
// rotation rows). After all groups are folded, each record is averaged by its
// contribution count, its rotation rows are converted back to Euler
// (MatrixToEuler), and the result is pushed to the matching scene-graph child
// object via VIBE_Object_SetPosition / VIBE_Object_SetWorldTranslation.
//
// The scene-graph push (the child-list walk + Object_SetPosition tail) is the
// engine-coupled APPLICATION leaf; it is forward-declared and supplied by the
// caller (the e2e test provides a capturing stub). What is reconstructed here
// byte-for-byte is the PALETTE build + name resolution + average/decompose, which
// is the function's deterministic payload.
//
// RECOVERED RECORD LAYOUTS (byte-for-byte from the decompilation)
// ---------------------------------------------------------------------------
// Palette scratch: v82[656] == 4 records * 164 bytes.  Per 164-byte record:
//   +0    char name[64]   bone name (copied 2 bytes at a time until NUL)
//   +64   i32  count      number of tracks folded into this record
//   +68   f32  accT[3]    accumulated (blended) translation  (+68/+72/+76)
//   +84   f32  refT[3]    reference/base translation column   (+84/+88/+92)
//   +100  f32  rows[3][4] three lerped 4-float rotation rows  (+100..+148)
// Group track record: 116 bytes (group base = drawData+272, 3 groups stride 384;
//   per group the 3 tracks start at +28, stride 116, count 348/116 == 3):
//   +0    i32  fromFrame  current keyframe index (*(track))
//   +4    i32  toFrame    target keyframe index  (*(track+4))
//   +8    i32  phaseNum   sub-frame phase numerator (*(track+8))
//   +64   f32  weight     this track's blend weight (*(track+64))
//   +104  AnimHeader* hdr animation header (*(track+104)); hdr+348 == frame array
//   +112  u8   boneIndex  bone slot (0xFF == inactive); name @ track+64+(idx<<6)
// Bone-name table (drawData+260 -> base): per 88-byte record, count at base+520:
//   +0    char name[…]    bone name (compared via StrCmp)
//   +180  f32  refT[3]    reference translation (record[45..47]) copied to +84
// =============================================================================
namespace guild::render {

// One 164-byte palette accumulation record (offsets are the ORIGINAL byte offsets
// within v82; sizeof must stay 164 so a 4-record array == the 656-byte scratch).
struct BonePaletteRecord {
    char  name[64];     // +0    bone name
    i32   count;        // +64   tracks folded in
    float accT[3];      // +68   accumulated translation (+68/+72/+76)
    u8    _gap80[4];    // +80   (the v82 scratch had a 4-byte hole here)
    float refT[3];      // +84   reference translation    (+84/+88/+92)
    u8    _gap96[4];    // +96   (4-byte hole before the rotation rows)
    float rows[12];     // +100  three 4-float rotation rows (+100..+148)
    u8    _pad[164 - 148]; // +148..+163  tail to the 164-byte stride
};
static_assert(sizeof(BonePaletteRecord) == 164,
              "BonePaletteRecord must be the 164-byte palette stride");

// One animation group's three bone tracks (the 116-byte track records). The
// reconstruction surfaces the fields ComputeBoneMatrices reads; `active` mirrors
// the *(group+104) header-present test, `boneIndex==0xFF` the inactive marker.
struct BoneTrack {
    bool             active = false; // *(track+104) != 0
    u8               boneIndex = 0xFF;// *(track+112)  (0xFF == skip)
    const char*      name = nullptr; // track+64+(boneIndex<<6) — the bone name
    i32              fromFrame = 0;  // *(track+0)
    i32              toFrame = 0;    // *(track+4)
    i32              phaseNum = 0;   // *(track+8)
    float            weight = 0.0f;  // *(track+64)
    const AnimFrame* frames = nullptr; // *(track+104)+348 == hdr frame array
};

// One animation group (3 tracks). The engine iterated 3 groups of 384 bytes; the
// palette only consumes the per-group track triple.
struct BoneGroup {
    BoneTrack tracks[3];
};

// A bone-name-table entry (the 88-byte records at drawData+260). `refT` is the
// reference translation copied into a new palette record's +84 column.
struct BoneNameEntry {
    const char* name;     // record+0
    float       refT[3];  // record[45..47] (bytes 180/184/188)
};

// The scene-graph APPLICATION leaf. After the palette is built+averaged the
// engine walked the object's child list pushing each matching child's pose via
// VIBE_Object_SetPosition (translation @rec+68) and VIBE_Object_SetWorldTranslation
// (the +0/decompose @rec). The caller supplies this; `name` is the palette
// record's bone name, `position` the averaged translation (3 floats), `euler` the
// averaged rotation (MatrixToEuler result, 3 floats).
using PaletteApplyFn = void (*)(void* ctx, const char* name,
                                const float* position, const float* euler);

// gilde.exe 0x5cc0d0 — VIBE_Anim_ComputeBoneMatrices (palette build core).
//   groups/groupCount : the active animation groups (the engine read up to 4 via
//                       the drawData+272 base, *(drawData+624) gate). Up to 4
//                       distinct bone names produce up to 4 palette records.
//   names/nameCount   : the bone-name table (drawData+260, count @+520) used to
//                       seed a new record's reference translation.
//   out               : receives the built palette (caller-sized >= 4).
//   apply/ctx         : the scene-graph push leaf (may be null to skip).
// Returns the number of palette records produced (0..4). Faithful to the original's
// name-dedupe (StrCmp), the 4-record cap, the per-track VectorLerp blend + Euler
// row build, and the final 1/count average + MatrixToEuler decompose.
i32 ComputeBoneMatrices(const BoneGroup* groups, i32 groupCount,
                        const BoneNameEntry* names, i32 nameCount,
                        BonePaletteRecord* out, PaletteApplyFn apply, void* ctx);

} // namespace guild::render
