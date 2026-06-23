// =============================================================================
// guild::render — object-list / texture-budget leaves (implementation).
// 1:1 translations from the gilde.exe Hex-Rays reference of record.
// =============================================================================
#include "render_recon_objlist.h"

#include <cstdio>

namespace guild::render {

// ---------------------------------------------------------------------------
// Engine-global mirrors.
// ---------------------------------------------------------------------------
ObjListState& ObjListGlobals() {
    static ObjListState g{};
    return g;
}

// Stable opaque sentinel value. The original uses the address of unk_1408440;
// any value that the live list never legitimately equals works as the
// empty-list marker. We pick a fixed non-zero word and store the same value
// into listHead on init, so head()==ObjListSentinel() is the empty test.
u32 ObjListSentinel() {
    return 0x1408440u; // mirrors &unk_1408440 in the original image.
}

TextureBudgetState& TextureBudgetGlobals() {
    static TextureBudgetState g{};
    return g;
}

// ---------------------------------------------------------------------------
// Hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
int DefaultSprintfCardInfo(char* out, int translucent, int fakeTranslucency,
                           int perspectiveCorrection, int linearFilter, int canClip,
                           int minW, int minH, int maxW, int maxH) {
    // gilde.exe 0x629770 format string, reproduced verbatim.
    return std::sprintf(
        out,
        "3D-Card Information:\n"
        " translucent: %i, fake_translucency: %i, perspective_correction: %i, "
        "linear_filter: %i, can_clip: %i\n"
        " min_texture_width: %i, min_texture_height: %i, max_texture_width: %i, "
        "max_texture_height: %i",
        translucent, fakeTranslucency, perspectiveCorrection, linearFilter, canClip,
        minW, minH, maxW, maxH);
}
} // namespace

RenderReconObjListHooks& ObjListHooks() {
    static RenderReconObjListHooks h = [] {
        RenderReconObjListHooks d{};
        d.sprintfCardInfo = &DefaultSprintfCardInfo;
        // All other hooks remain null/inert; the math paths gate on them safely.
        return d;
    }();
    return h;
}

// ---------------------------------------------------------------------------
// 0x5e0e00 — VIBE_Render_InitObjectList
// ---------------------------------------------------------------------------
// dword_1408108/.. are float bit-patterns: 1065353216 == 0x3F800000 == 1.0f.
// The original writes a 12-word block plus three bookkeeping words and the list
// head sentinel, then returns 1.0f's bit pattern.
u32 InitObjectList() {
    ObjListState& g = ObjListGlobals();
    g.word_748 = 0;                 // dword_1408748 = 0
    g.word_43C = 0;                 // dword_140843C = 0
    g.word_74C = 0x1408130u;        // dword_140874C = &unk_1408130 (sentinel)
    g.block[0]  = 0;                // dword_1408100 = 0
    g.block[1]  = 0;                // dword_1408104 = 0
    g.block[2]  = 1065353216u;      // dword_1408108 = 1.0f
    g.block[3]  = 1065353216u;      // dword_140810C = 1.0f
    g.block[4]  = 0;                // dword_1408110 = 0
    g.block[5]  = 1065353216u;      // dword_1408114 = 1.0f
    g.block[6]  = 0;                // dword_1408118 = 0
    g.block[7]  = 0;                // dword_140811C = 0
    g.block[8]  = 1065353216u;      // dword_1408120 = 1.0f
    g.block[9]  = 0;                // dword_1408124 = 0
    g.block[10] = 1065353216u;      // dword_1408128 = 1.0f
    g.block[11] = 1065353216u;      // dword_140812C = 1.0f
    g.listHead  = ObjListSentinel();// dword_1408438 = &unk_1408440
    return 1065353216u;             // return 1.0f bit pattern
}

// ---------------------------------------------------------------------------
// 0x5e0f30 — VIBE_Render_FreeObjectNode
// ---------------------------------------------------------------------------
// Disasm reference (0x5e0f30):
//   if (off_649D64 == node[+0x2F0]) {           // node owned by global list
//       prev = node[+0x308]; next = node[+0x30C];
//       prev[+0x30C] = next; next[+0x308] = prev;
//       if (node == owner[+0xA4]) owner[+0xA4] = node[+0x308]; // (== prev)
//       if (node == owner[+0xA8]) owner[+0xA8] = node[+0x308];
//   } else { UnlinkObjectNode(node); }
//   if (node[+0xD4]) Texture_ReleaseEntry(node[+0xD4]);
//   FreeDebug(node[+0x28]); node[+0x28]=0;
//   FreeDebug(node[+0xDC]); node[+0xDC]=0;
//   FreeDebug(node[+0xE0]); node[+0xE0]=0;
//   FreeDebug(node);
//
// NOTE on the head/tail fixups: the original loads ecx from node[+0x308]
// (== the splice predecessor) and stores that into owner head/tail when the
// node was the head/tail. We reproduce that literally (it stores `prev`).
void FreeObjectNode(RenderNode* node, RenderNodeOwner* globalOwner) {
    RenderReconObjListHooks& h = ObjListHooks();

    if (node->owner == static_cast<void*>(globalOwner)) {
        RenderNode* prev = node->prev;     // [edx+308h]
        RenderNode* next = node->next;     // [edx+30Ch]
        prev->next = next;                 // [ecx+30Ch] = ebx(next)
        next->prev = prev;                 // [ecx+308h] = ebx(prev)
        if (node == globalOwner->head)     // cmp edx, [eax+0A4h]
            globalOwner->head = node->prev;// [eax+0A4h] = [ebx+308h]==node prev
        if (node == globalOwner->tail)     // cmp edx, [eax+0A8h]
            globalOwner->tail = node->prev;// [eax+0A8h] = [esi+308h]==node prev
    } else {
        if (h.unlinkObjectNode) h.unlinkObjectNode(node); // 0x5e0e9c
    }

    if (node->texEntry) {                  // [edx+0D4h]
        if (h.releaseTextureEntry) h.releaseTextureEntry(node->texEntry);
    }
    // FreeDebug(node[+0x28]); node[+0x28]=0;  (order: free then zero)
    if (h.memoryFreeDebug) h.memoryFreeDebug(node->alloc28);
    node->alloc28 = 0;
    if (h.memoryFreeDebug) h.memoryFreeDebug(node->allocDC);
    node->allocDC = 0;
    if (h.memoryFreeDebug) h.memoryFreeDebug(node->allocE0);
    node->allocE0 = 0;
    // FreeDebug(node) — the record itself.
    if (h.memoryFreeNode) h.memoryFreeNode(node);
}

// ---------------------------------------------------------------------------
// 0x5e0e74 — VIBE_Render_FreeObjectList
// ---------------------------------------------------------------------------
// for ( ; dword_1408438 != &unk_1408440; FreeObjectNode(this) ) ;
// The original repeatedly frees the node currently at the global list anchor
// (dword_1408438) until that anchor equals the sentinel. We model the anchor
// through caller-supplied head()/advance() so the unit stays self-contained:
// head() yields the current anchored node, FreeObjectNode tears it down, and
// the caller's advance() (or FreeObjectNode's own list fixup) moves the anchor.
void FreeObjectList(RenderNode* (*head)(), void (*advance)(), RenderNode* sentinel,
                    RenderNodeOwner* globalOwner) {
    for (;;) {
        RenderNode* cur = head();
        if (cur == sentinel) break;
        FreeObjectNode(cur, globalOwner);
        if (advance) advance();
    }
}

// ---------------------------------------------------------------------------
// 0x5dcd50 — VIBE_Render_FormatCardInfo
// ---------------------------------------------------------------------------
// Bit extraction reproduced exactly from the original's byte shifts on
// caps[+780] (call it f):
//   translucent             = f & 1
//   fake_translucency       = (u8)(f << 6) >> 7   == (f >> 1) & 1
//   perspective_correction  = (u8)(f * 32) >> 7   == (f >> 2) & 1
//   linear_filter           = (u8)(f * 8)  >> 7   == (f >> 4) & 1
//   can_clip                = (u8)(f * 16) >> 7   == (f >> 3) & 1
// We keep the literal shift form to stay byte-identical.
int FormatCardInfo(const CardCaps* caps, char* out) {
    const u8 f = caps->flags; // *(BYTE*)(a1 + 780)

    int translucent      = f & 1;
    int fakeTranslucency  = static_cast<u8>(static_cast<u8>(f << 6) >> 7);
    int perspective       = static_cast<u8>(static_cast<u8>(32 * f) >> 7);
    int linearFilter      = static_cast<u8>(static_cast<u8>(8 * f) >> 7);
    int canClip           = static_cast<u8>(static_cast<u8>(16 * f) >> 7);

    RenderReconObjListHooks& h = ObjListHooks();
    return h.sprintfCardInfo(out, translucent, fakeTranslucency, perspective,
                             linearFilter, canClip, caps->minTexW, caps->minTexH,
                             caps->maxTexW, caps->maxTexH);
}

// ---------------------------------------------------------------------------
// 0x5b379c — VIBE_Render_AdjustTextureBudget
// ---------------------------------------------------------------------------
// Faithful translation of the disasm. The DDraw EvictManagedTextures call and
// the available-vidmem query are routed through hooks (rule-3 boundary); the
// budget arithmetic on the 0x649D8C.. globals is reproduced 1:1.
//
//   if (!engineEnabled || !deviceReady || lockDepth > 1) return a1.lo;
//   if ((u32)(tick - lastTick) < 0x40) return a1.lo;
//   total = QueryAvailableVidMem(NULL, &free, NULL);  // total in ecx, free=v6[0]
//   if (total >= limitA) goto check_free;             // 0x5b387a
//   loop_pressure:                                    // 0x5b37f2
//     elapsed = tick - lastTick;
//     if (total >= limitA) {                          // 0x5b38a7 branch
//         missesB = 0;
//         if (elapsed > 0x100) missesA = 1;
//         else if (++missesA >= 2 && limitB >= 2) { missesA = 0; limitB >>= 1; }
//     } else {                                        // 0x5b380a branch
//         missesA = 0;
//         if (elapsed > 0x100) missesB = 1;
//         else if (++missesB >= 2 && limitA >= 2) { missesB = 0; limitA >>= 1; }
//     }
//     status = EvictManagedTextures(); if (status) ReportDDrawError();
//     lastTick = tick; return status.lo;
//   check_free:                                       // 0x5b387a
//     if (free >= 0x20000) return a1.lo;
//     if (arg2 >= limitB) return a1.lo;
//     goto loop_pressure;
//
// Mapping of decompiler temporaries: missesA==dword_649D98, missesB==dword_649D9C,
// limitA==dword_649D8C, limitB==dword_649D90.
u8 AdjustTextureBudget(u32 tick, u32 arg2) {
    RenderReconObjListHooks& h = ObjListHooks();
    TextureBudgetState& g = TextureBudgetGlobals();

    u8 ret = static_cast<u8>(tick); // a1 carried in al; low byte propagated on early return

    // gate: engineEnabled && deviceReady && lockDepth <= 1
    bool active = h.budgetActive ? h.budgetActive() : false;
    if (!active) return ret;

    ret = static_cast<u8>(tick - g.lastTick);     // LOBYTE(a1) = a1 - lastTick
    if (static_cast<u32>(tick - g.lastTick) < 0x40u) return ret; // jnb loc_5B37DB

    // QueryAvailableVidMem(0, &free, 0): total -> ecx, free -> v6[0].
    // gilde.exe 0x5b37e1: `LOBYTE(a1) = QueryAvailableVidMem(...)`. With a1=0,
    // a2=&free, a3=0 the callee's al-return is the free value's low byte (its
    // `LOBYTE(a1)=v15; *a2=v15` success path). So `al` (=> `ret`) is overwritten
    // with the low byte of `free` on return, which the post-query early-out
    // paths (0x5b37d3, 0x5b3893) propagate — NOT the prior `tick - lastTick`.
    u32 freeMem = 0;
    u32 total = h.queryVidMem ? h.queryVidMem(&freeMem) : 0;
    ret = static_cast<u8>(freeMem);

    // The eviction body (closure over current state).
    auto doEvict = [&]() -> u8 {
        u32 elapsed = tick - g.lastTick;
        if (total >= g.limitA) {                   // 0x5b38a7
            g.missesB = 0;
            if (elapsed > 0x100u) {
                g.missesA = 1;
            } else if (++g.missesA >= 2 && g.limitB >= 2) {
                g.missesA = 0;
                g.limitB >>= 1;
            }
        } else {                                   // 0x5b380a
            g.missesA = 0;
            if (elapsed > 0x100u) {
                g.missesB = 1;
            } else if (++g.missesB >= 2 && g.limitA >= 2) {
                g.missesB = 0;
                g.limitA >>= 1;
            }
        }
        u32 status = h.evictManagedTextures ? h.evictManagedTextures() : 0;
        // (status != 0 -> ReportDDrawError(); inert here, status still propagates)
        g.lastTick = tick;                         // dword_649D94 = tick
        return static_cast<u8>(status);
    };

    if (total < g.limitA) {            // 0x5b37e6 jnb loc_5B387A taken when total>=limitA
        return doEvict();              // falls into loop_pressure (0x5b37f2)
    }
    // total >= limitA -> check_free (0x5b387a)
    if (freeMem >= 0x20000u) return ret;           // 0x5b387a jnb loc_5B37D3
    if (arg2 >= g.limitB) return ret;              // 0x5b3887 jb taken -> loop, else ret
    return doEvict();                              // loop_pressure
}

// ---------------------------------------------------------------------------
// 0x5b9ef4 — VIBE_Render_SetGammaTable
// ---------------------------------------------------------------------------
//   if ((BYTE)gamma != byte_64A02C) {
//       result = TextureCache_Reset();
//       byte_64A02C = gamma;
//       if (dword_64A028) result = Floor_ReloadTextures(dword_64A028);
//       for (i = 0; i != 15744; i += 246)
//           if (rec = dword_13ECF74[i]; rec && rec != dword_64A028)
//               result = Floor_ReloadTextures(rec);
//   }
//   return result;
// The floor-record table walk is routed through the forEachFloorRecord hook so
// the unit need not embed the 15744-byte global table; arithmetic/order match.
int SetGammaTable(u8 gamma) {
    RenderReconObjListHooks& h = ObjListHooks();
    int result = 0;

    u8 cur = h.gammaByte ? *h.gammaByte : 0;
    if (gamma != cur) {
        if (h.textureCacheReset) result = h.textureCacheReset();
        if (h.gammaByte) *h.gammaByte = gamma;
        u32 primary = h.activeFloor ? h.activeFloor() : 0;
        if (primary) {
            if (h.floorReloadTextures) result = h.floorReloadTextures(primary);
        }
        if (h.forEachFloorRecord) {
            // Static trampoline cannot capture; the hook owns iteration order.
            // We thread the reload through a thread-local cursor so the visitor
            // can reach floorReloadTextures + capture `result`.
            static int* s_result = nullptr;
            static RenderReconObjListHooks* s_hooks = nullptr;
            s_result = &result;
            s_hooks = &h;
            h.forEachFloorRecord(primary, [](u32 rec) {
                if (s_hooks->floorReloadTextures)
                    *s_result = s_hooks->floorReloadTextures(rec);
            });
        }
    }
    return result;
}

} // namespace guild::render
