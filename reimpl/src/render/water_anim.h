#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — WATER TEXTURE ANIMATION: the texture-animation half of
//   0x5be428  VIBE_Floor_AnimateWaterVertices
// (the vertex wave-displacement half is in render/floorwater.cpp
//  AnimateWaterWaveGrid). For each animated water mesh the original advances the
// mesh's *active texture* through a texture group on a time-derived schedule:
//
//   tex = mesh[0];                                  ; a3[0] -> texture record ptr
//   if (tex && tex->memberCount /*+112*/) {
//       speedSel = tex->flags114 & 0x0F;            ; low nibble of +114
//       if (speedSel) {
//           groupId = (tex - texBase) >> 7;         ; 128-byte texture-record stride
//           frame   = (time / speedTable[speedSel]) % tex->memberCount;
//           mesh[1] = FindGroupMember(groupId, frame);   ; a3[1] = chosen member
//       }
//   }
//
// SPEED TABLE — gilde.exe dword_5D93C8 (recovered via get_bytes). The selector is a
// 4-bit value [1..15] (guarded nonzero); the engine's table holds, for selectors
// 1..10, the frame-divisor:  {15,13,11,9,8,7,5,3,2,1}.  (Index 0 is unused — the
// `if (speedSel)` guard skips it.) Larger divisor == slower animation.
//
// The texture bank itself (texBase = dword_1406A84, FindGroupMember @0x5daec0) is
// data-coupled global state; this module reproduces the SCHEDULE math
// (frame-index selection) and exposes FindGroupMember as an injectable callback so
// the per-mesh texture advance is testable without the bank. The bank walk inside
// FindGroupMember is LISTED deferred (see report).
// =============================================================================
namespace guild::render {

// gilde.exe dword_5D93C8[1..10] — animation frame-divisor table. Index 0 is unused
// (the speed selector is guarded nonzero). kWaterAnimSpeedTable[s] is the divisor
// for selector s in [1,10].
extern const u32 kWaterAnimSpeedTable[11];

// Compute the animated texture *frame index* for a water mesh, exactly as
// VIBE_Floor_AnimateWaterVertices does:
//   frame = (time / speedTable[speedSel]) % memberCount
// `time` is the global animation clock (the v22 == (int)result base time the
// engine threads through), `speedSel` the 4-bit selector (1..15), `memberCount`
// the texture group's member count (tex+112, a byte). Returns the 0-based frame
// index in [0, memberCount). Caller guards memberCount != 0 and speedSel != 0.
i32 WaterTextureFrameIndex(u32 time, u8 speedSel, u8 memberCount);

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (texture-animation half),
// one mesh. Given the mesh's current texture record fields and the global `time`,
// returns the new active texture id via the injected group lookup, OR a no-change
// sentinel. Mirrors the decompiled guard cascade:
//   if (!texPtr || !memberCount) -> no change (returns curMember unchanged)
//   speedSel = flags114 & 0x0F;  if (!speedSel) -> no change
//   frame = WaterTextureFrameIndex(time, speedSel, memberCount);
//   return findMember(groupId, frame);
// `findMember(groupId, frameByte)` is the injectable FindGroupMember (@0x5daec0).
// `groupId` is (texPtr - texBase) >> 7, supplied directly by the caller.
template <typename FindMember>
u32 AnimateWaterTexture(u32 time, u32 curMember, i32 groupId, u8 memberCount,
                        u8 flags114, FindMember findMember) {
    if (memberCount == 0)
        return curMember;                 // tex+112 == 0 -> skip
    u8 speedSel = (u8)(flags114 & 0x0Fu); // *(tex+114) & 0xF
    if (speedSel == 0)
        return curMember;                 // selector 0 -> skip
    i32 frame = WaterTextureFrameIndex(time, speedSel, memberCount);
    return findMember(groupId, (u8)frame);
}

} // namespace guild::render
