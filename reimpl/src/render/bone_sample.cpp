#include "render/bone_sample.h"

#include <cstdint>
#include <cstring>

namespace guild::render {
namespace {

// Byte-offset accessors mirroring the original *(type*)(base + off) loads. The
// pointer fields the original reads (bone+104, anim+348) are 32-bit pointer slots
// at offsets that are NOT 8-byte aligned on a 64-bit host (348 % 8 == 4). Reading
// them via `*(const void* const*)` is a misaligned, strict-aliasing-violating load
// (UBSAN: "load of misaligned address ... requires 8 byte alignment"). memcpy the
// stored pointer instead — same value, no UB. [W11-ANIM UBSAN fix]
inline const u8* CBytePtr(const void* p) { return reinterpret_cast<const u8*>(p); }
inline const void* CPtrAt(const void* p, std::size_t off)
{
    const void* v;
    std::memcpy(&v, CBytePtr(p) + off, sizeof(v));
    return v;
}
// keyframe base = *(*(bone+104) + 348); each record is 192 bytes.
inline const float* KeyframeRec(const void* bone, int frameIdx)
{
    const void* anim = CPtrAt(bone, 104);
    const u8* base = reinterpret_cast<const u8*>(CPtrAt(anim, 348));
    return reinterpret_cast<const float*>(base + 192 * frameIdx);
}

} // namespace

// gilde.exe 0x5cc850 — VIBE_Anim_GetBonePosition
float* GetBonePosition(const float* boneFrame, const void* bone, int frameIdx, float* out)
{
    const float* kf = KeyframeRec(bone, frameIdx); // +44/+48/+52 -> float idx 11..13
    out[0] = boneFrame[33] + kf[11];
    out[1] = boneFrame[34] + kf[12];
    out[2] = boneFrame[35] + kf[13];
    return out;
}

// gilde.exe 0x5ccea0 — VIBE_Anim_GetBoneFramePose
int GetBoneFramePose(const void* bone, float* outRot, int frameIdx, float* outTrans)
{
    const float* kf = KeyframeRec(bone, frameIdx);
    outRot[0]   = kf[8];   // +32
    outRot[1]   = kf[9];   // +36
    outRot[2]   = kf[10];  // +40
    outTrans[0] = kf[11];  // +44
    outTrans[1] = kf[12];  // +48
    outTrans[2] = kf[13];  // +52
    return 192 * frameIdx;
}

} // namespace guild::render
