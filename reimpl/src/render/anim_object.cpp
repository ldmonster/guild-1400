// =============================================================================
// anim_object.cpp — see anim_object.h for the module overview + provenance.
// namespace guild::render.
// =============================================================================
#include "render/anim_object.h"

#include <cstring>

namespace guild::render {

namespace {
AnimObjHooks g_hooks;
std::vector<AnimStream*> g_stock;
}  // namespace

void SetAnimObjHooks(const AnimObjHooks* hooks) {
    g_hooks = hooks ? *hooks : AnimObjHooks();
}
const AnimObjHooks& GetAnimObjHooks() { return g_hooks; }

std::vector<AnimStream*>& AnimStock() { return g_stock; }
void ResetAnimStock() { g_stock.clear(); }

// gilde.exe 0x5cb8f0 VIBE_Util_StrCmpNoCase -> guild::util::StrCmpNoCase (reused).
// gilde.exe 0x5ccf18 VIBE_Anim_AdvanceFrameIndex -> guild::render::AdvanceFrameIndex
//   (skeleton.cpp, reused). Both pulled in via the headers; not redefined here.

// ---------------------------------------------------------------------------
// gilde.exe 0x5cf114 — VIBE_Anim_FindFreeMeshSlot.
//   v2 = head; if (head == sentinel) return 0;
//   while (StrCmpNoCase_Thunk(a1, v2)) { v2 = *(v3+352); if (v2==sentinel) return 0; }
//   return v3;
// The thunk returns 0 only on an exact (case-folded) match -> the loop stops at
// the first match. Empty list (head==sentinel) -> 0.
// ---------------------------------------------------------------------------
AnimStream* FindFreeMeshSlot(const char* name) {
    if (g_stock.empty())                            // head == &unk_13FC780
        return nullptr;                             // 0x5cf142
    for (AnimStream* s : g_stock) {                 // walk via +352 next ptr
        if (StrCmpNoCase(name, s->name.c_str()) == 0)
            return s;                               // 0x5cf146
    }
    return nullptr;                                 // hit sentinel
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d3858 — VIBE_Anim_LoadStreamToStock.
// The stock lookup + the record name use the a3/edx KEY string, not the built
// I/O path (`FindFreeMeshSlot(a3)`; `LoadBinaryAnimation(v16, a3, loop)` —
// LoadBinaryAnimation @0x5e450c StrNCopyPads its a2 into the record name).
// ---------------------------------------------------------------------------
AnimStream* LoadStreamToStock(const char* name, u8 loopFlag, const char* key) {
    // strcpy("animations/") then append name (the two char-copy loops). 0x5d386b
    std::string path = "animations/";
    path += name;

    AnimStream* existing = FindFreeMeshSlot(key);   // 0x5d38b9 (key = a3/edx)
    if (existing) {
        // d3_LoadAnimStream(): animation already in stock: %s  (logged) 0x5d38ec
        return existing;                            // 0x5d3914
    }
    AnimStream* loaded = g_hooks.loadBinaryAnimation
                             ? g_hooks.loadBinaryAnimation(path.c_str(), key, loopFlag)
                             : nullptr;             // 0x5d38c8
    if (loaded) {                                   // 0x5d38d1
        // Prepend to the stock list: result[89]=head; head=result;
        // result[88]=sentinel; old-head[88]=result.  (0x5d38d3..)
        g_stock.insert(g_stock.begin(), loaded);
    }
    return loaded;                                  // 0x5d38f6
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5cec00 — VIBE_Anim_FreeObjAnimData.
//   if (node+464) { free(*(rec+56)); *(rec+56)=0; free(rec); node+464=0; }
// ---------------------------------------------------------------------------
void FreeObjAnimData(AnimNode* node) {
    if (!node) return;
    if (node->objAnim) {                            // 0x5cec0c
        delete node->objAnim;                       // frees the rec (+ its frames)
        node->objAnim = nullptr;                    // 0x5cec31
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5cef14 — VIBE_Anim_CreateObjectAnim.
// ---------------------------------------------------------------------------
ObjAnimRecord* CreateObjectAnim(AnimNode* node, i32 srcHandle, i32 frameCount,
                                const std::vector<i32>& srcFrames, bool looped) {
    if (!node) return nullptr;

    // Free any existing object-anim record first (0x5cef1e..0x5cef51).
    if (node->objAnim) {
        delete node->objAnim;
        node->objAnim = nullptr;
    }

    ObjAnimRecord* rec = nullptr;                   // v7 = 0
    const u8 cls = node->classByte;                 // *(node+533) 0x5cef5a
    if (cls == 4 || cls == 3) {                     // 0x5cef68
        rec = new ObjAnimRecord();
        rec->frameCount = frameCount;               // *(rec)   = a4  0x5cef7e
        rec->src        = srcHandle;                // *(rec+11)= a2  0x5cef80
        rec->ctrl46     = 0x10;                     // |0x10 then &~0x20 -> 0x10  0x5cef91/0x5cefa3
        rec->scale      = 100;                      // *(rec+4) = 100 0x5cef8a
        rec->weight     = 0x3f800000;               // *(rec+12)= 1.0f bits 0x5cef94
        rec->subFrame   = 0;                        // *(rec+13)= 0   0x5cefaa
        rec->ctrl45     = looped ? 0x02 : 0x00;     // bit1 of *(rec+45) drives looped path

        // Copy the source keyframe array (the qmemcpy at 0x5cefce..0x5cf003),
        // then the "*3" frame-count fixup over every frame's +0 field, plus the
        // last-frame[0] = prev-frame[0] fixup (0x5cf03d).
        rec->frames = srcFrames;                    // alloc(88*v12) + qmemcpy
        if (frameCount >= 2 && static_cast<int>(rec->frames.size()) >= frameCount) {
            rec->frames[frameCount - 1] = rec->frames[frameCount - 2];  // 0x5cf03d
        }
        for (i32 i = 0; i < frameCount && i < static_cast<int>(rec->frames.size()); ++i)
            rec->frames[i] *= 3;                     // 0x5cf054

        // Snapshot the node pose into the record (0x5cf06e..0x5cf095).
        rec->pos[0] = node->pos[0]; rec->pos[1] = node->pos[1]; rec->pos[2] = node->pos[2];
        rec->rot[0] = node->rot[0]; rec->rot[1] = node->rot[1]; rec->rot[2] = node->rot[2];

        if (rec->ctrl45 & 2) {                       // 0x5cf09b looped
            const i32 last = rec->frameCount - 1;    // v27
            rec->curFrame = last;                    // *(rec+4) = v27
            rec->subFrame = (last >= 0 && last < static_cast<int>(rec->frames.size()))
                                ? rec->frames[last] - 1 : -1;  // 0x5cf0fd
            rec->nextFrame = AdvanceFrameIndex(
                static_cast<u8>((rec->ctrl45 >> 8) & 0xff /*BYTE1*/), rec->curFrame,
                last, 0, last);                      // 0x5cf10c
            // NOTE: BYTE1(*(rec+44)) is the control word's 2nd byte; ctrl45 lives
            // in the low byte, so BYTE1 is 0 here (matches: no extra ping/clamp).
        } else {                                     // 0x5cf09d non-looped
            rec->subFrame  = 0;                      // *(rec+12) = 0
            rec->nextFrame = 1;                      // *(rec+8)  = 1
            rec->curFrame  = 0;                      // *(rec+4)  = 0
        }

        if (g_hooks.buildFrameTangents)              // 0x5cf0b4
            g_hooks.buildFrameTangents(rec);
    }

    node->objAnim    = rec;                          // *(node+464) = v7  0x5cf0bd
    node->frameStamp = g_hooks.frameCounter;         // *(node+64) = dword_62EB38 0x5cf0cb
    return rec;                                       // 0x5cf0d0
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d0b64 — VIBE_Anim_AttachToBone.
// ---------------------------------------------------------------------------
i32 AttachToBone(AnimNode* model, const char* name, i32 ctrlWord) {
    if (!model) return -1;                           // 0x5d0b6e (orig: return 0)

    AnimStream* stream = FindFreeMeshSlot(name);     // 0x5d0b7c

    // Count occupied channels (0..2): scan while +132 (stream) set, cap at 3.
    int slot = 0;                                    // v6
    if (model->bone[0].used && model->bone[0].stream) {  // *(a1+132)
        for (slot = 0; slot < 3 && model->bone[slot].used && model->bone[slot].stream; )
            ++slot;                                  // 0x5d0b95
    }
    // Reject: no stream, mesh-type mismatch, or all 3 channels in use.
    if (!stream || model->meshType != stream->meshType || slot >= 3)  // 0x5d0bb8
        return -1;                                   // 0x5d0d2e

    BoneAttachChannel& ch = model->bone[slot];
    ch.used = true;
    model->frameStamp = g_hooks.frameCounter;        // *(v7+64)=dword_62EB38 0x5d0be7
    model->boneActive = true;                        // *(a1+380)=1 0x5d0bea

    ch.blendInit = 0x3f800000;                       // +72 = 1.0f bits 0x5d0bf1
    ch.accumB    = 0;                                // +100 = 0       0x5d0bf8
    ch.weight    = 100;                              // +60 = 100      0x5d0bff
    ch.stream    = stream;                            // +104 0x5d0c06
    ++stream->refCount;                              // ++*(stream+332) 0x5d0c09
    stream->frameStamp = g_hooks.animEpoch;          // *(stream+344)=dword_649D58 0x5d0c14

    if (!stream->normalsBuilt) {                     // 0x5d0c1a
        if (model->meshNormalsSrc) {                 // *(a1+16)
            if (g_hooks.calculateAnimNormals)        // 0x5d0c2c
                g_hooks.calculateAnimNormals(stream, model->meshNormalsSrc);
            stream->normalsBuilt = 1;                // 0x5d0c31
        }
    }

    ch.nameId   = ctrlWord;                          // +108 = a2 0x5d0c38
    // The binary PRESERVES the channel's existing flag bits: +110 = (old & 0xA7)
    // | 0x10 (0x5d0c3b/0x5d0c4e) and +109 &= ~0x20 (0x5d0c4b) — channels are
    // reused across attach/prune cycles, so stale bits (e.g. the +109 loop bit
    // tested below) survive by design.
    ch.flags110 = static_cast<u8>((ch.flags110 & 0xA7) | 0x10);
    ch.flags109 = static_cast<u8>(ch.flags109 & ~0x20u);
    if ((ctrlWord & 0xff) == 1)                      // (BYTE)a2 == 1
        ch.flags109 |= 0x10;                         // 0x5d0c56

    ch.markFrame = -1;                               // +28 = -1 0x5d0c5a
    ch.accumA    = 0;                                // +48 = 0  0x5d0c61
    ch.speed     = 0;                                // +56 = 0  0x5d0c68
    ch.blendInit = 0x3f800000;                       // +72 = 1.0f 0x5d0c6f
    ch.blendA    = 0;                                // +16 = 0  0x5d0c76
    ch.frameB    = ch.markFrame;                     // +24 = +28 (-1) 0x5d0c80
    ch.frameA    = ch.markFrame;                     // +20 = +28 (-1) 0x5d0c83
    ch.accumB    = ch.speed;                         // +52 = +56 (0)  0x5d0c89
    ch.blendDst  = ch.blendInit;                     // +68 = +72 0x5d0c8f
    ch.blendCur  = ch.blendDst;                      // +64 = +68 0x5d0c95

    if (stream->noResetFrame)                        // *(stream+361) 0x5d0c98
        ch.weightCur = 0;                            // +12 = 0
    else
        ch.weightCur = stream->loDefault;            // +12 = *(stream+336) 0x5d0d14

    // BYTE1(a2) was loaded into flags110|0x10; the loop test reads channel +109's
    // 0x2 bit (the "looped" sub-flag). Here that bit is not set by this path, so
    // the non-loop branch runs unless a caller pre-sets it. We honor the field.
    if (ch.flags109 & 2) {                           // 0x5d0cac looped
        const i32 last = stream->frameTotal - 1;     // v11
        ch.curFrame = last;                          // *v8 = v11
        if (last >= 0 && last < static_cast<int>(stream->poseFrameCounts.size()))
            ch.hiStop = stream->poseFrameCounts[last] - 1;  // 0x5d0ccc
        ch.nextFrame = AdvanceFrameIndex(
            static_cast<u8>((ch.nameId >> 8) & 0xff /*BYTE1(+108)*/),
            ch.curFrame, stream->hiDefault, stream->loDefault,
            stream->frameTotal);                     // 0x5d0cf0
    } else {                                         // 0x5d0d19 non-looped
        ch.hiStop    = 0;                            // +8 = 0
        ch.nextFrame = 1;                            // +4 = 1
        ch.curFrame  = ch.hiStop;                    // *v8 = +8 (0) 0x5d0d2a
    }

    if (g_hooks.assignSubMeshBones)                  // 0x5d0cf5
        g_hooks.assignSubMeshBones(model->drawData);
    if (g_hooks.computeBoneMatrices)                 // 0x5d0cfc
        g_hooks.computeBoneMatrices(model->drawData);
    return slot;                                     // 0x5d0b72 (orig: channel ptr)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d0d38 — VIBE_Anim_PruneExpiredAttachments.
// ---------------------------------------------------------------------------
void PruneExpiredAttachments(AnimNode* model, const char* name) {
    if (!model) return;                              // 0x5d0d41

    // Walk all 3 channels (stride 116, terminator = model+348 = base+116*... ).
    for (int k = 0; k < 3; ++k) {                    // do { } while (v4 != v6)
        BoneAttachChannel& ch = model->bone[k];
        if (ch.used && ch.stream) {                  // v7 = *(v4+132)
            AnimStream* s = ch.stream;
            // case-SENSITIVE compare (VIBE_Util_StrCmp @0x5d3f10), drop on match.
            if (s && std::strcmp(s->name.c_str(), name) == 0) {  // !StrCmp -> equal
                ch.flags110 &= static_cast<u8>(~2u);  // &~2 0x5d0dac
                // VIBE_Anim_ReleaseMeshData(stream, 0) @0x5d0dbb decrements the
                // refcount UNCONDITIONALLY (and at <= 0 runs the budget/eviction
                // path — the full LRU form lives in anim_recon4_mesh_lru.cpp
                // over the raw record arena; this modeled AnimStream keeps the
                // count bookkeeping only).
                --s->refCount;
                ch.stream = nullptr;                   // +132 = 0 0x5d0dc0
                ch.used   = false;
            }
        }
    }

    // Recompute boneActive: if channel 0 empty, scan forward; if all 3 empty
    // (count reaches 3) -> clear (0x5d0d61..0x5d0d86).
    int empty = 0;
    if (!model->bone[0].stream) {
        for (int k = 0; k < 3 && !model->bone[k].stream; ++k)
            ++empty;
    }
    if (empty == 3)                                  // 0x5d0d84
        model->boneActive = false;                   // *(a1+380) = 0

    if (g_hooks.assignSubMeshBones)                  // 0x5d0d8f
        g_hooks.assignSubMeshBones(model->drawData);
    if (g_hooks.computeBoneMatrices)                 // 0x5d0d96
        g_hooks.computeBoneMatrices(model->drawData);
}

}  // namespace guild::render
