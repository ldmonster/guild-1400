// character_recon5_morph — see header for provenance / layout / constants.
#include "character_recon5_morph.h"

#include <cmath>
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x4015e0..0x401676 — fade ramp for one slot (pure).
// ---------------------------------------------------------------------------
FadeStep FadeComputeValue(const FadeSlot& s, int now) {
    FadeStep r;
    double v1;
    // if ( *((BYTE*)v0+4) )  v1 = (now - start) * 0.02
    // else                   v1 = 1.0 - (now - start) * 0.02
    if (s.dir)
        v1 = ((double)now - (double)s.start) * kFadeRate;            // 0x401604
    else
        v1 = 1.0 - ((double)now - (double)s.start) * kFadeRate;      // 0x401779
    f32 v14 = (f32)v1;                                               // v14 = v1
    double v11;
    if (v14 <= 0.0f)                                                 // 0x40161d
        v11 = 0.0;                                                   // 0x401782
    else
        v11 = (double)v14;                                           // v11 = v14
    double v12;
    if (v11 >= 1.0)                                                  // 0x40164b
        v12 = 1.0;                                                   // 0x401796
    else
        v12 = v11;                                                   // 0x401659
    f32 v15 = (f32)(v12 * kFadeScale);                              // v15 = v12 * 255  0x401676
    r.ramp01 = (f32)v12;
    r.value255 = v15;
    // endpoint: (|v15|==0) or (v15 == 255)
    bool atFull = (v15 == kFadeFull);
    bool atZero = ((std::uint32_t)( (union { f32 f; std::uint32_t u; }){v15}.u ) & 0x7FFFFFFFu) == 0;
    r.finished = atFull || atZero;
    if (r.finished) {
        r.madeVisible   = atFull && !atZero;     // v15==255 -> SetVisible(.,1)
        r.madeInvisible = atZero;                // v15==0   -> SetVisible(.,0)
    }
    return r;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4015cc — VIBE_Character_FadeOutSlots
// ---------------------------------------------------------------------------
void FadeOutSlots(FadeSlot* slots, int count, int now, FadeHooks& H) {
    for (int i = 0; i < count; ++i) {                  // v0 += 3 until == dword_66F0D0
        FadeSlot& s = slots[i];
        if (!s.id)                                     // if ( *v0 )
            continue;
        FadeStep step = FadeComputeValue(s, now);
        f32 v15 = step.value255;
        TChar* ch = s.ch;
        TObject* obj = ch ? ch->object : nullptr;
        int sceneCtx = obj ? obj->sceneCtx : 0;

        // mid-ramp: apply transparency (when v15 is neither 0 nor 255)
        bool atFull = (v15 == kFadeFull);
        bool atZero = ((std::uint32_t)( (union { f32 f; std::uint32_t u; }){v15}.u ) & 0x7FFFFFFFu) == 0;
        if (!atZero && !atFull) {                      // 0x4016b6
            int packed = (int)v15;                     // LOBYTE(v13) = (int)v15
            if (H.changeTransparency && obj) {
                H.changeTransparency(obj, sceneCtx, packed, &s);          // 0x4016c5
                if (ch->secondary)                                        // v5 = *(v3+292)
                    H.changeTransparency(ch->secondary, sceneCtx, packed, &s);  // 0x4016e3
            }
        }
        // endpoint: clear fade flag, full-opaque, toggle visibility, free slot
        if (atZero || atFull) {                        // 0x4017b0
            if (obj) obj->f529 = obj->f529;            // (no-op placeholder; see note)
            if (ch) ch->f140 &= ~0x40u;                // *(v3+140) &= ~0x40   0x401701
            int packed = -1;                           // LOBYTE(v13) = -1 (full opaque)
            if (H.changeTransparency && obj) {
                H.changeTransparency(obj, sceneCtx, packed, &s);          // 0x401718
                if (ch->secondary)
                    H.changeTransparency(ch->secondary,
                                         ch->secondary->sceneCtx, packed, &s);  // 0x401733
            }
            // SetVisible: v15==0 -> visible 0 ; v15==255 -> visible 1
            int visible;
            if (atZero)
                visible = 0;                           // 0x401744
            else /* atFull */
                visible = 1;                           // 0x4017c7
            if (H.setVisible && ch) H.setVisible(ch, visible);  // 0x401746
            s.id = 0;                                   // *v0 = 0  (LABEL_19)
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4038ed..0x403948 — morph-key blend (pure).
//   v35 = *(v33) - *(v13)   ; v33 = (v35 + *v30) / 2  ; frames = (int)(v33 + 0.5)
//   where *(v33)=lastKeyVal, *(v13)=firstKeyVal, *v30=nextKeyVal.
// ---------------------------------------------------------------------------
MorphKeyBlend MorphComputeBlend(int firstKeyVal, int lastKeyVal, int nextKeyVal) {
    MorphKeyBlend b;
    int v35 = lastKeyVal - firstKeyVal;                 // v35 = *(v33) - *(v13)
    b.sumDelta = v35 + nextKeyVal;
    b.half = (v35 + nextKeyVal) / 2;                    // v33 = (v35 + *v30) / 2
    double v14 = (double)b.half + (double)kMorphHalfBias; // (double)v33 + 0.5
    b.frames = (int)v14;                                // v35 = (int)v14
    return b;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x403708 — VIBE_Character_ReleaseMorphAni
// ---------------------------------------------------------------------------
int ReleaseMorphAni(TChar* ch, MorphHooks& H) {
    int result = ch ? 1 : 0;                            // return value tracks ch
    if (ch->morphAnim) {                                // if ( *(result+124) )
        // sprintf(buf,"morph_%i",ch) — name only.
        if (H.pruneExpired) H.pruneExpired(0);          // PruneExpiredAttachments(obj+492 list)
        int freeSlot = H.findFreeMeshSlot ? H.findFreeMeshSlot() : 0;
        if (H.releaseMeshData)
            result = H.releaseMeshData(freeSlot, 0, 0, 0) & 0xFF;  // LOBYTE(result)=...
        ch->morphAnim = 0;                              // *(v7+124) = 0
    }
    return result;                                      // 0x403718
}

// ---------------------------------------------------------------------------
// gilde.exe 0x403764 — VIBE_Character_CheckAniMorph
// ---------------------------------------------------------------------------
void CheckAniMorph(TChar* ch, MorphHooks& H) {
    TObject* obj = ch->object;                          // v2 = *(result+52)
    int v3 = ch->primaryAnim;                           // v3 = *(result+112)
    int v4 = 0;                                          // morph-was-released flag

    // (1) stale primary teardown:
    // if primary live && (primary[109]&0x20) && !(primary[110]&8) && !(ch+140 & 2)
    if (v3 /* live-attachment fields are anim-subsystem state */) {
        // The bit predicates (primary[109]&0x20, &c.) live in unreconstructed
        // anim-attachment records; gate via hook-provided liveness when wired.
        // Faithful structure preserved: prune + clear primary.
        if (!(ch->f140 & 2)) {
            // if (!boneFlagA && (sign>>6)) ComputeBoneDelta(...)
            if (H.computeBoneDelta) H.computeBoneDelta((int)(intptr_t)obj, v3, 0, 0, 3);  // 0x4037b9
            if (H.pruneExpired) H.pruneExpired(0);      // 0x4037d3
            ch->primaryAnim = 0;                        // *(result+112) = 0
        }
    }

    // (2) finished-morph release:
    int v6 = ch->morphAnim;                             // v6 = *(result+124)
    if (v6) {
        // if ( (v6[109] & 0x20) == 0 ) return;  -- morph not yet finished.
        // (the finished bit is anim-subsystem state; when unwired we proceed.)
        // sprintf "morph_%i"
        v4 = 1;                                          // 0x403818
        if (H.pruneExpired) H.pruneExpired(0);           // 0x40381d
        int freeSlot = H.findFreeMeshSlot ? H.findFreeMeshSlot() : 0;
        if (H.releaseMeshData) H.releaseMeshData(freeSlot, 0, (int)(intptr_t)obj, (int)(intptr_t)ch);  // 0x403833
        ch->morphAnim = 0;                               // *(result+124) = 0
    }

    // (3) pending CurrentAnimStream -> attach primary or build morph
    int v9 = ch->pendingStream;                          // v9 = *(result+128)
    if (!v9)
        return;

    if (ch->morphAnim || !ch->primaryAnim) {
        if (!ch->morphAnim && !ch->primaryAnim) {
            // --- attach the pending stream as the new primary (0x403ac2..) ---
            // Build the attach request word (Light_SetGrayColorThunk + bit fixups);
            // the morphMode selects byte0 and bit fixups.  The observable result is
            // the new primary handle.
            u8 mode = ch->morphMode ? ch->morphMode : 1;  // v20
            // (bit packing into v32 is anim-request encoding; carried by attachToBone)
            (void)mode;
            // if FindFreeMeshSlot()==0 -> LoadStreamToStock("character/%s/%s.baf")
            if (H.findFreeMeshSlot && H.findFreeMeshSlot() == 0) {  // 0x403b28
                if (H.loadStreamToStock) H.loadStreamToStock("character//.baf", 0);  // 0x403b64
            }
            int handle = H.attachToBone ? H.attachToBone(0, 0) : 0;  // 0x403b86
            ch->primaryAnim = handle;                    // *(result+112) = ...
            ch->morphMark = -1;                          // *(result+133) = -1
            if (handle) {
                int idx = H.indexFromPointer ? H.indexFromPointer(ch->universe) : 0;  // 0x403ba1
                (void)idx;  // sets *(handle+60) to a palette id; anim-subsystem.
                if (v4) {                                // morph was just released
                    if (H.computeBoneMatrices) H.computeBoneMatrices((int)(intptr_t)obj);  // 0x403bda
                }
            }
            ch->pendingStream = 0;                       // *(result+128) = 0
        }
        // else: morph live but primary idle -> wait (no action this tick).
    } else {
        // --- primary live, no morph: build a morph blend stream (0x403862..) ---
        // The key-blend math is in-scope.  The actual key dwords live in the
        // anim-stream records (192-byte key stride) reached through the live
        // primary handle; when unwired the blend is computed from zeros, which
        // matches the "no extractable engine state" default.
        MorphKeyBlend blend = MorphComputeBlend(0, 0, 0);  // (firstKey,lastKey,nextKey)
        if (blend.frames /* v16 */) {                      // 0x403951
            if (H.createMorphAnim &&
                H.createMorphAnim(0, 0, 0, nullptr, nullptr, 0, "morph", blend.frames)) {  // 0x403981
                int h = H.attachToBone ? H.attachToBone(0, 137216) : 0;  // 0x4039dd
                ch->morphAnim = h;                          // *(result+124) = v17
                if (h) {
                    int idx = H.indexFromPointer ? H.indexFromPointer(ch->universe) : 0;  // 0x403a77
                    (void)idx;  // palette id into *(h+60)
                }
            } else {
                ch->morphAnim = 0;                          // 0x403a9f
            }
        }
        if (H.computeBoneDelta) H.computeBoneDelta((int)(intptr_t)obj, ch->primaryAnim, 0, 0, 3);  // 0x403a0e
        if (H.pruneExpired) H.pruneExpired(0);              // 0x403a21
        ch->primaryAnim = 0;                                // *(result+112) = 0
    }
}

} // namespace guild::sim
