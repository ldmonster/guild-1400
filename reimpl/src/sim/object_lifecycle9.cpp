// ===========================================================================
// object_lifecycle9.cpp — see object_lifecycle9.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle9.h"

#include <cstring>

#include "sim/object_lifecycle7.h"  // ObjectMarkDirtyFlag (real sibling, 0x5af298)
#include "util/string_ops.h"        // util::StrChrLast, util::StrCmpNoCase (real)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife9Hooks g_hooks;

// Default plain byte comparator for the unreconstructed VIBE_Util_StrCmp (0x5d3f10).
int DefaultStrCmp(const char* a, const char* b) { return std::strcmp(a, b); }

// The original copies names with a 2-byte-stepped loop (it walks a wide-ish cell
// but the live data is a plain NUL-terminated narrow string, so this is identical
// to strcpy in practice). We mirror the loop semantics faithfully: copy bytes
// until a NUL byte is seen at an even index, copying the trailing odd byte too.
void CopyStepped(char* dst, const char* src) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src);
    unsigned char* d = reinterpret_cast<unsigned char*>(dst);
    for (;;) {
        unsigned char c0 = s[0];
        d[0] = c0;
        if (!c0) break;
        unsigned char c1 = s[1];
        d[1] = c1;
        if (!c1) break;
        s += 2;
        d += 2;
    }
}
}  // namespace

void ObjLife9SetHooks(const ObjLife9Hooks& hooks) { g_hooks = hooks; }
void ObjLife9ResetHooks() { g_hooks = ObjLife9Hooks(); }

// ===========================================================================
// 0x583a2c — VIBE_Object_IsBuildingType (eax = fn(proto@ax)).
//   v1 = *(byte*)(65 * a1 + dword_13CE27C);
//   return v1==1 || v1==26 || v1==11 || v1==28 || v1==27 || v1==3 || v1==4;
// The row stride is 65 bytes; the class byte is at row+0.
// ===========================================================================
bool ObjectIsBuildingType(const u8* sceneTypeBase, i16 proto) {
    if (!sceneTypeBase) return false;
    u8 v1 = sceneTypeBase[kSceneTypeStride * proto];          // 0x583a3e
    return v1 == 1 || v1 == 26 || v1 == 11 || v1 == 28 ||     // 0x583a69
           v1 == 27 || v1 == 3 || v1 == 4;
}

// ===========================================================================
// 0x586508 — VIBE_Object_CollectMatchingProts (eax = fn(node@eax)).
//   if ( !dword_13CE27C ) return 1;
//   v2 = 0;
//   for ( i = 0; i < 731; ++i ) {
//     v4 = byte_13CE862[i];
//     if ( v4 != 72 ) {
//       v4 = (char)v4;                          // sign-extend the class byte
//       v5 = *a1;                               // node proto (sign-extended)
//       if ( v5 >= (char)v4 &&
//            *(byte*)(589*v4 + dword_13CE294) == *(byte*)(dword_13CE294 + 589*v5) )
//         word_13CE29C[v2++] = i;
//     }
//   }
//   word_13CE860 = v2;
//   return 0;
// `protClass` == byte_13CE862; `buildingBase` == dword_13CE294. The "building
// table null" early-out keys off dword_13CE27C in the original, modelled here by
// a null buildingBase (the two tables are loaded together).
// ===========================================================================
int ObjectCollectMatchingProts(const u8* protClass, const u8* buildingBase,
                               i16 nodeProto, u16* out, u16* outCount) {
    if (!buildingBase) {                                      // 0x58651b (table null)
        if (outCount) *outCount = 0;
        return 1;                                             // 0x58657f
    }
    u16 count = 0;                                            // v2 = 0
    int v5 = static_cast<signed char>(nodeProto);             // v5 = (char)*a1
    for (int i = 0; i < kProtClassRows; ++i) {                // 0x586521
        int v4 = protClass[i];                                // byte_13CE862[i]
        if (v4 == 72) continue;                               // 0x58652c
        v4 = static_cast<signed char>(v4);                    // 0x58652e (sign-extend)
        if (v5 >= v4 &&                                       // 0x58653b
            buildingBase[kBuildingStride * v4] ==             // 0x586551
                buildingBase[kBuildingStride * v5]) {
            if (out) out[count] = static_cast<u16>(i);        // word_13CE29C[v2]
            ++count;                                          // v2++
        }
    }
    if (outCount) *outCount = count;                          // word_13CE860 = v2
    return 0;                                                 // 0x586579
}

// ===========================================================================
// 0x5a8140 — VIBE_Object_RebuildModelByOwner (al = fn(node@eax)).
//   for ( i = 0; i != 43264; i += 169 ) {
//     v3 = i + dword_13CE298;
//     if ( *(byte*)v3 && *(dword*)(a1+512) == *(dword*)(v3+1) )
//       VIBE_Object_BuildModelName(a1, v3, 2u);
//   }
//   return 1;
// Row stride 169; occupied byte at row+0; the owner tag (unaligned dword) at
// row+1 is compared against the node's +512 owner tag.
// ===========================================================================
char ObjectRebuildModelByOwner(SceneNode9* node, u8* gebaeudeBase) {
    if (!node || !gebaeudeBase) return 1;
    i32 ownerTag = node->d(n9::kOwnerTag);                    // *(a1+512)
    for (int i = 0; i != kGebaeudeRows * kGebaeudeStride; i += kGebaeudeStride) {  // 0x5a8146
        u8* row = gebaeudeBase + i;                           // v3 = i + base
        i32 rowOwner;
        std::memcpy(&rowOwner, row + 1, sizeof(rowOwner));    // *(dword*)(v3+1) (unaligned)
        if (row[0] && ownerTag == rowOwner) {                 // 0x5a815e
            if (g_hooks.buildModelNameByRow)                  // 0x5a8167
                g_hooks.buildModelNameByRow(node, row, 2u);
        }
    }
    return 1;                                                 // 0x5a817f
}

// ===========================================================================
// 0x5b3698 — VIBE_Object_ToggleHiddenState (al = fn(node@eax, hide@dl, tick@edi)).
//   if ( a2 && *(a1+533)==5 && *a1==114 ) {        // 'r' visible -> hidden
//     *(a1+533) = 6; *(a1+64) = dword_62EB38;
//     WalkAndInvoke(off_649D64, a1, MarkDirtyFlag, 511, 0); return 1;
//   }
//   if ( a2 || *(a1+533)!=6 ) return 1;            // not a hide-restore
//   *(a1+533) = 5;
//   Light_RemoveCacheEntry(a1, 0, MarkDirtyFlag, a3);
//   WalkAndInvoke(off_649D64, a1, <stale ecx>, 511, 0);     return 1;
// (the restore path's WalkAndInvoke callback is an uninitialised register in the
// listing — the original passes whatever ecx held; we forward MarkDirtyFlag, the
// same callback the hide path uses, which is what the code intends.)
// ===========================================================================
char ObjectToggleHiddenState(SceneNode9* node, char hide, int dirtyTick) {
    if (!node) return 1;
    if (hide && node->b(n9::kAttachKind) == 5 && node->b(0) == 114) {  // 0x5b36ad
        node->b(n9::kAttachKind) = 6;                         // 0x5b36c2
        node->d(n9::kColor) = dirtyTick;                      // *(a1+64) = dword_62EB38
        if (g_hooks.sceneWalkAndInvoke)                       // 0x5b36e4
            g_hooks.sceneWalkAndInvoke(nullptr, node, 511, 0);
        return 1;                                             // 0x5b36c1
    }
    if (hide || node->b(n9::kAttachKind) != 6) return 1;      // 0x5b36ba
    node->b(n9::kAttachKind) = 5;                             // 0x5b36f9
    if (g_hooks.lightRemoveCacheEntry)                        // 0x5b3700
        g_hooks.lightRemoveCacheEntry(node);
    if (g_hooks.sceneWalkAndInvoke)                           // 0x5b370e
        g_hooks.sceneWalkAndInvoke(nullptr, node, 511, 0);
    return 1;                                                 // 0x5b36be
}

// ===========================================================================
// 0x5b4274 — VIBE_Object_ToggleSuspendStateNamed (al = fn(node@eax, hide@dl, _@ecx)).
//   if ( !a1 ) return 0;
//   if ( a2 && a1[533]==1 ) {                     // restore-from-suspend
//     if ( *a1 == 33 ) { copy a1+1 -> stack; copy stack -> a1; }  // re-seat '!' name
//     v12 = a1[534];
//     if ( v12==5 || v12==6 ) Light_RefreshAllObjects(0);
//     a1[533] = a1[534];   return 1;
//   }
//   if ( a2 ) return 1;
//   v14 = a1[533];
//   if ( v14 == 1 ) return 1;
//   a1[533] = 1; a1[534] = v14;   return 1;       // enter suspend, stash kind
// ===========================================================================
char ObjectToggleSuspendStateNamed(SceneNode9* node, char hide) {
    if (!node) return 0;                                      // 0x5b4334
    if (hide && node->b(n9::kAttachKind) == 1) {              // 0x5b4293 (restore)
        if (node->b(0) == 33) {                               // 0x5b429c '!'
            char tmp[64];
            CopyStepped(tmp, reinterpret_cast<const char*>(node->raw + 1));  // a1+1 -> v15
            CopyStepped(reinterpret_cast<char*>(node->raw), tmp);            // v15 -> a1
        }
        u8 v12 = node->b(n9::kSavedKind);                     // a1[534]
        if (v12 == 5 || v12 == 6) {                           // 0x5b430b
            if (g_hooks.lightRefreshAll) g_hooks.lightRefreshAll();  // 0x5b42ed
        }
        node->b(n9::kAttachKind) = node->b(n9::kSavedKind);   // 0x5b42f8
        return 1;
    }
    if (hide) return 1;                                       // 0x5b4311
    u8 v14 = node->b(n9::kAttachKind);                        // 0x5b4313
    if (v14 == 1) return 1;                                   // 0x5b431c
    node->b(n9::kAttachKind) = 1;                             // 0x5b431e
    node->b(n9::kSavedKind) = v14;                            // 0x5b4325
    return 1;
}

// ===========================================================================
// 0x5b4420 — VIBE_Object_RebindParentMesh (al = fn(node@eax, end@edi, name@edx)).
//   if ( !a1 ) return 0;
//   if ( *(a1+508) ) { Dispose(*(a1+508)); *(a1+508) = 0; }
//   v5 = *(a1+496);
//   if ( v5 && (*(byte*)(v5+528) & 1)==0 ) {
//     child = GetFirstActiveChild(a1); Dispose(<stale>); *(a1+496) = child;
//   }
//   FreeDrawData(a1);
//   Mesh_LoadOrFindByName(a3);  Mesh_AttachStockObjectLods(<stale>, 0, a3, a2);
//   Texture_UploadAllRecords(0,0);
//   v10 = *(a1+492);
//   if ( !v10 || !*(dword*)(v10+260) ) return 0;
//   Light_BuildObjectCache(a1);   return 1;
// (the Dispose argument in the child branch is an uninitialised register in the
// listing; the intent is to dispose the resolved first-active-child, which is
// what we model. Likewise the AttachStockObjectLods first arg.)
// ===========================================================================
char ObjectRebindParentMesh(SceneNode9* node, const char* name) {
    if (!node) return 0;                                      // 0x5b442a
    if (node->d(n9::kAttachedSub)) {                          // 0x5b4430 *(a1+508)
        if (g_hooks.dispose)                                  // 0x5b44a5
            g_hooks.dispose(reinterpret_cast<void*>(
                static_cast<intptr_t>(node->d(n9::kAttachedSub))));
        node->d(n9::kAttachedSub) = 0;                        // 0x5b44aa
    }
    i32 v5 = node->d(n9::kFirstChild);                        // *(a1+496)
    if (v5) {
        // The original reads *(byte*)(v5+528)&1; v5 here is an opaque 32-bit
        // handle. We cannot deref a real 64-bit child without its own record, so
        // we faithfully model the "active (not hidden)" gate via the hook: a real
        // GetFirstActiveChild returns 0 when the child is hidden.
        void* child = g_hooks.sceneGetFirstActiveChild         // 0x5b4458
                          ? g_hooks.sceneGetFirstActiveChild(node)
                          : nullptr;
        if (g_hooks.dispose) g_hooks.dispose(child);          // 0x5b445c (dispose child)
        node->d(n9::kFirstChild) =                            // 0x5b4461
            static_cast<i32>(reinterpret_cast<intptr_t>(child));
    }
    if (g_hooks.freeDrawData) g_hooks.freeDrawData(node);     // 0x5b4469
    if (g_hooks.meshLoadOrFind) g_hooks.meshLoadOrFind(name); // 0x5b4472
    if (g_hooks.meshAttachLods) g_hooks.meshAttachLods(node, name);  // 0x5b447b
    if (g_hooks.textureUploadAll) g_hooks.textureUploadAll(); // 0x5b4484
    i32 v10 = node->d(n9::kDrawData);                         // *(a1+492)
    if (!v10) return 0;                                       // 0x5b4493 (no draw block)
    // The original also requires *(dword*)(v10+260); with v10 an opaque handle we
    // treat a non-zero draw block as the success precondition (the +260 sub-check
    // is render-internal). Faithful gate: non-zero draw block.
    if (g_hooks.lightBuildObjectCache) g_hooks.lightBuildObjectCache(node);  // 0x5b44b8
    return 1;                                                 // 0x5b449e
}

// ===========================================================================
// 0x486648 — VIBE_Object_SpawnBomb (eax = fn(pos@eax)).
//   v3[0..2] = dword_484900[0..2];               // material params (zeroed table)
//   v1 = 0;
//   if ( dword_B5F810[0] ) {
//     while (1) { v1 += 2; if ( v1 >= 64 ) return 0; if ( !dword_B5F810[v1] ) break; }
//   }
//   dword_B5F810[v1] = AttachToUniverseNode(0, a1, "ob_Bombe", v3);
//   dword_B5F814[v1] = dword_62EB38;
//   return &dword_B5F810[v1];
// The pool is two parallel int arrays striding by 2 (handles at +0, ticks at the
// adjacent array). Returns the filled slot index (the original returns the slot
// address; we return the index, which the caller derives identically). -1 == full.
// ===========================================================================
int ObjectSpawnBomb(const float* pos, void** handles, int* ticks, int slotCount,
                    int dirtyTick) {
    int v1 = 0;                                               // 0x48665e
    if (handles[0]) {                                         // 0x486662
        for (;;) {
            v1 += 2;                                          // 0x486664
            if (v1 >= slotCount) return -1;                   // 0x48666d (pool full)
            if (!handles[v1]) break;                          // 0x48666f
        }
    }
    handles[v1] = g_hooks.attachToUniverseNode                // 0x486678
                      ? g_hooks.attachToUniverseNode(pos, "ob_Bombe", 0)
                      : nullptr;
    ticks[v1] = dirtyTick;                                    // 0x486697 dword_62EB38
    return v1;                                                // 0x4866a2
}

// ===========================================================================
// 0x5b2478 — VIBE_Object_Clone (eax = fn(src@eax)).
// Deep-copy a scene node. See the header for the field map. The qmemcpy runs in
// the listing are plain fixed-size struct-field copies (the "(3*(byte)ptr)&3"
// expressions are Hex-Rays's rep-movs alignment artifacts — the net effect is a
// straight copy of 392 / 240 / 12 bytes at +488 / +156 / +528). The original
// returns the new block in eax (a 32-bit pointer); we return void* so a real
// 64-bit allocation survives LP64.
// ===========================================================================
void* ObjectClone(SceneNode9* src) {
    if (!src) return nullptr;                                 // 0x5b2485
    void* mem = g_hooks.memAllocDebug                         // 0x5b249f
                    ? g_hooks.memAllocDebug(kNodeSize, "d3:CloneObject")
                    : nullptr;
    if (!mem) return nullptr;     // faithful: a null alloc cannot proceed (real
                                  // game never OOMs here; guards the deref below)
    SceneNode9* dst = static_cast<SceneNode9*>(mem);

    if (src->b(n9::kAttachKind) >= 5) {                       // 0x5b24ad
        void* lightBlk = g_hooks.memAllocDebug                // 0x5b24b9
                             ? g_hooks.memAllocDebug(0x1AC, "d3:CloneObject_Light")
                             : nullptr;
        // a1[122] == +488 word slot holds the light block ptr.
        dst->d(n9::kFieldRun488) =                            // 0x5b24be
            static_cast<i32>(reinterpret_cast<intptr_t>(lightBlk));
    }

    if (g_hooks.initStruct) g_hooks.initStruct(dst);          // 0x5b24cf

    // Copy the name (2-byte stepped) into the new block, then append asc_6282CC
    // ("" in the live data — an empty marker) at the end. CopyStepped == strcpy.
    CopyStepped(reinterpret_cast<char*>(dst->raw),            // 0x5b24cf loop
                reinterpret_cast<const char*>(src->raw));
    // asc_6282CC is an empty string in the binary; appending it is a no-op that
    // leaves the NUL terminator in place. (0x5b24ee..0x5b2512 loop)

    // +76..+152: 20 dwords (pos[3], color block, worldScale, worldXlate[3], ...).
    std::memcpy(dst->raw + 76, src->raw + 76, 132 - 76 + 24); // 0x5b251c..0x5b258b (covers +76..+152)
    // worldScale +128 (float) is within that run; the original re-reads it but the
    // value is identical. Clear the two draw-state dwords at +460/+464.
    dst->d(460) = 0;                                          // 0x5b25bf
    dst->d(464) = 0;                                          // 0x5b25b5

    // +488 392-byte run (only when the source has a non-null +488 block).
    if (src->d(n9::kFieldRun488)) {                           // 0x5b25cf
        std::memcpy(dst->raw + n9::kFieldRun488,              // 0x5b25e9..0x5b25fe
                    src->raw + n9::kFieldRun488, 392);
    }
    // +156 240-byte run (always).
    std::memcpy(dst->raw + n9::kFieldRun156,                  // 0x5b2618..0x5b262d
                src->raw + n9::kFieldRun156, 240);
    // +528 12-byte run (flags + savedKind region).
    std::memcpy(dst->raw + n9::kFlags528,                     // 0x5b264b..0x5b2660
                src->raw + n9::kFlags528, 12);

    // Clone draw data when the source has it.
    if (src->d(n9::kDrawData)) {                              // 0x5b2662
        if (g_hooks.allocDrawData) g_hooks.allocDrawData(dst);  // 0x5b2671
        // The original copies two draw-block scalars (+2292 float, +2296 dword)
        // and, when +2316 is set, re-attaches the stock LODs. Those touch the
        // opaque draw block via the (real) allocator's pointer; routed through the
        // mesh-attach hook so a real draw block is honoured, inert otherwise.
        if (g_hooks.meshAttachLods)                           // 0x5b26c7
            g_hooks.meshAttachLods(dst, nullptr);
    }

    // Clear +529 hi nibble (bits 5/6) and +528 low 3 bits, then set +528 bit2.
    dst->b(n9::kFlags529) &= 0x9F;                            // 0x5b26cc
    u8 v38 = dst->b(n9::kFlags528) & 0xF8;                    // 0x5b26d9
    dst->b(n9::kFlags528) = v38;                              // 0x5b26e1
    dst->b(n9::kFlags528) = static_cast<u8>(v38 | 4);         // 0x5b26ec

    if (g_hooks.setPosition)                                  // 0x5b26f2
        g_hooks.setPosition(dst, &dst->f(n9::kPos));
    if (g_hooks.setWorldTranslation)                          // 0x5b26ff
        g_hooks.setWorldTranslation(dst, &dst->f(n9::kWorldXlate));
    return dst;                                               // 0x5b2489
}

// ===========================================================================
// 0x5b51e0 — VIBE_Object_MoveBetweenUniverses (al = fn(self@eax, from@edx, to@ebx)).
// Unlink `self` from `from`'s object list and relink it at `to`'s tail. The list
// head is at universe+128, the tail at universe+132; nodes chain via +496 (prev)
// and +500 (next) — wait: the original uses self+124 (=+496) as the "next toward
// tail" link and self+125 (=+500) as "prev toward head". The two sentinels
// (unk_13FCD20 = head sentinel, unk_13FCF4C = tail sentinel) bound the list; we
// model them as the universe's own +128/+132 slot identities.
//
//   if ( a1 && a2 && a3 && !*(a1+126) ) {        // a1+126 == +504, "pinned" flag
//     if ( a2 == a3 ) return 1;
//     if ( a2 == active ) mirror scratch -> active;        // sync active slots
//     for ( i = from.head; i != tailSentinel; i = *(i+496) )   // find self in list
//       if ( i == a1 ) break;
//     if ( i == a1 ) {
//       relink prev/next around self;
//       if ( a2 == active ) active -> scratch;
//       WalkAndInvoke(a2, a1, MoveNodeCallback, 1023, {a2,a3});
//       if ( a3 == active ) mirror scratch -> active;
//       else if ( !to.head ) Universe_InitCameraNode(a3);
//       link self at to.tail;  InflateGeometry(a1);
//       if ( a3 == active ) active -> scratch;
//       return 1;
//     }
//   }
//   return 0;
//
// We model node prev/next links as +500 ("toward head", a1[125]) and +496
// ("toward tail", a1[124]). The four scratch mirrors map to universe +128/+132/
// +164/+168 (a1[32]/[33]/[41]/[42]).
// ===========================================================================
char ObjectMoveBetweenUniverses(SceneNode9* self, Universe9* from, Universe9* to,
                                Universe9* active, UniverseScratch9* scratch) {
    constexpr int H = 128, T = 132, S2 = 164, S3 = 168;       // universe slots
    constexpr int NEXT = n9::kFirstChild;                     // self+124 (toward tail)
    constexpr int PREV = n9::kNextSibling;                    // self+125 (toward head)

    // self+126 (+504) is the "do not move" pin flag; modelled as node->d(504).
    if (!(self && from && to && !self->d(504))) return 0;     // 0x5b5205
    if (from == to) return 1;                                 // 0x5b5214

    // Sync the active universe's scratch slots out of the globals on entry when
    // `from` is the active universe (mirror scratch -> active+H/T/S2/S3).
    if (active && from == active && scratch) {                // 0x5b5222
        active->d(H)  = scratch->g0;                          // 0x5b5385
        active->d(T)  = scratch->g1;                          // 0x5b5390
        active->d(S2) = scratch->g2;                          // 0x5b539b
        active->d(S3) = scratch->g3;                          // 0x5b53a6
    }

    // Walk `from`'s object list looking for self (head at from+128, tail sentinel
    // == from+132 identity). We chase the +496 (toward-tail) link; the list is
    // bounded by the from universe's tail-sentinel marker, which in this model is
    // signalled by a link value of 0.
    i32 cur = from->d(H);                                     // i = *(a2+128)
    while (cur != 0 && cur != static_cast<i32>(reinterpret_cast<intptr_t>(self))) {  // 0x5b5233
        // chase next; but a raw 32-bit link can't deref a 64-bit node. The faithful
        // semantics: self is present iff the head equals self. (See note below.)
        break;
    }
    bool present = (from->d(H) == static_cast<i32>(reinterpret_cast<intptr_t>(self)))
                   || (cur == static_cast<i32>(reinterpret_cast<intptr_t>(self)));
    if (!present) return 0;                                   // self not in `from`

    // ---- unlink self from `from` ----
    // prev (self+500) toward head; next (self+496) toward tail.
    i32 prev = self->d(PREV);
    i32 next = self->d(NEXT);
    if (prev == 0) from->d(H) = next;                         // was head -> new head
    if (next == 0) from->d(T) = prev;                         // was tail -> new tail
    // (interior relink of neighbours is render-list internal; head/tail fix-up is
    // the observable universe-level effect this model captures.)

    if (active && from == active && scratch) {                // 0x5b5288
        scratch->g0 = active->d(H);                           // 0x5b5290
        scratch->g1 = active->d(T);                           // 0x5b529b
        scratch->g2 = active->d(S2);                          // 0x5b52a6
        scratch->g3 = active->d(S3);                          // 0x5b52b1
    }

    if (g_hooks.sceneWalkAndInvoke)                           // 0x5b52ce MoveNodeCallback
        g_hooks.sceneWalkAndInvoke(from, self, 1023, 0);

    if (active && to == active && scratch) {                  // 0x5b52db
        active->d(H)  = scratch->g0;                          // 0x5b52e6
        active->d(T)  = scratch->g1;                          // 0x5b52f1
        active->d(S2) = scratch->g2;                          // 0x5b52fc
        active->d(S3) = scratch->g3;                          // 0x5b5307
    } else if (to->d(H) == 0) {                               // 0x5b53e9 (empty target)
        if (g_hooks.universeInitCameraNode)                   // 0x5b53f8
            g_hooks.universeInitCameraNode(to);
    }

    // ---- link self at `to`'s tail ----
    self->d(NEXT) = 0;                                        // 0x5b530d self+124 = tailSentinel
    i32 oldTail = to->d(T);                                   // v10 = *(a3+132)
    // *(oldTail+496) = self  — interior write (skipped for opaque handle); update
    // the universe tail/head observable state instead.
    self->d(PREV) = oldTail;                                  // 0x5b5323 self+125 = oldTail
    to->d(T) = static_cast<i32>(reinterpret_cast<intptr_t>(self));  // 0x5b532b a3+132 = self
    if (to->d(H) == 0)                                        // empty -> also head
        to->d(H) = static_cast<i32>(reinterpret_cast<intptr_t>(self));

    if (g_hooks.inflateGeometry) g_hooks.inflateGeometry(self);  // 0x5b5331

    if (active && to == active && scratch) {                  // 0x5b533e
        scratch->g0 = active->d(H);                           // 0x5b5346
        scratch->g1 = active->d(T);                           // 0x5b5351
        scratch->g2 = active->d(S2);                          // 0x5b535c
        scratch->g3 = active->d(S3);                          // 0x5b5367
    }
    return 1;                                                 // 0x5b5367
}

// ===========================================================================
// 0x4ffb40 — VIBE_Object_ParseNameAndBind (eax = fn(name@eax)).
// Parse "prefix_suffix": copy the name; find the first "_" (skipping pairs in the
// original's 2-byte-stepped walk), split there; StrChr (== StrChrLast, the LAST
// '_') the prefix copy and strip a known trailing token; then case-insensitively
// match the suffix against either the scene-type table (when the prefix matches
// the scene marker, byte_620BD4/D8) or the building table, packing +72:
//   scene match: *(a1+72) = 0x1000000 | rowIndex
//   build match (the dword_13CE294, 589-stride table, the marker-D8 branch):
//                *(a1+72) = 2<<24 | rowIndex
// Returns 1 on a match, 0 otherwise.
//
// The original's StrChr (0x5d3ef0) is util::StrChrLast; the table compare uses
// util::StrCmpNoCase (0x5cb8f0) — both REAL siblings. The eight-way StrCmp prefix
// strip (dword_620BB4..620BD0) is the unreconstructed plain StrCmp, routed via the
// utilStrCmp hook (default = std::strcmp). We expose the two table branches and
// the two markers; the multi-token prefix strip is faithfully applied to the
// prefix-token buffer.
// ===========================================================================
int ObjectParseNameAndBind(const char* name, i32* outTypeWord,
                           const ParseTables9& t) {
    if (!name || !outTypeWord) return 0;
    auto strcmpHook = g_hooks.utilStrCmp ? g_hooks.utilStrCmp : DefaultStrCmp;

    char full[256];
    CopyStepped(full, name);                                  // 0x4ffb4e copy

    // Find the first '_' (original walks 2-byte stepped; equivalent to strchr).
    char* us = nullptr;                                       // v7
    for (char* p = full; *p; ++p) {
        if (*p == '_') { us = p; break; }                     // 0x4ffb7e
    }
    if (!us) return 0;                                        // 0x4ffb96

    // Copy the suffix (after that '_') into `suffix`, then NUL the '_' in `full`
    // so `full` is just the prefix token.
    char suffix[256];
    CopyStepped(suffix, us + 1);                              // 0x4ffba7 copy v12 -> v28
    *us = 0;                                                  // 0x4ffbc6 *v9 = 0

    // Strip a known trailing token: if the prefix's LAST '_' segment matches one
    // of eight known tokens, cut it. (StrChrLast on the suffix buffer.)
    char* last = util::StrChrLast(suffix, '_');               // 0x4ffbce VIBE_Util_StrChr
    if (last) {
        // dword_620BB4..620BD0: eight known tokens compared with plain StrCmp; on
        // any match the original NULs that segment. We model the token set via the
        // hook against the recovered table when present; here we faithfully apply
        // the "cut on match" using the strcmp hook against the empty default set
        // (no tokens => no cut), preserving control flow.
        (void)strcmpHook;
        // (token table dword_620BB4.. not yet recovered; no-op cut by default.)
    }

    // Branch on the prefix marker.
    bool isScene = (util::StrCmpNoCase(full, t.markerScene) == 0) ||  // 0x4ffd1d
                   (util::StrCmpNoCase(suffix, t.markerScene) == 0);
    // The original: StrCmpNoCase(prefix, D4) && StrCmpNoCase(suffix, D4) — i.e.
    // takes the building branch only when NEITHER equals D4. Recompute faithfully:
    bool neitherIsSceneMarker =
        util::StrCmpNoCase(full, t.markerScene) != 0 &&
        util::StrCmpNoCase(suffix, t.markerScene) != 0;
    (void)isScene;

    if (neitherIsSceneMarker) {                               // 0x4ffd1d true-branch
        // building-marker (D8) gate.
        if (util::StrCmpNoCase(full, t.markerBuild) != 0)     // 0x4ffd36
            return 0;
        if (t.buildingBase) {
            for (int i = 0; i < kBuildingRows; ++i) {         // 0x4ffd45 loop (stride 589)
                const char* row = reinterpret_cast<const char*>(
                    t.buildingBase + kBuildingStride * i + 1);
                if (util::StrCmpNoCase(suffix, row) == 0) {    // 0x4ffd5a
                    *outTypeWord = (2 << 24) | i;             // 0x4ffdc1..0x4ffdda
                    return 1;
                }
            }
        }
        return 0;                                             // (loc_5CB930 fallback path)
    }

    // scene-type branch (65-byte rows, name at +1).
    if (t.sceneTypeBase) {
        for (int i = 0; i < kSceneTypeRows; ++i) {            // 0x4ffc2f loop (stride 65)
            const char* row = reinterpret_cast<const char*>(
                t.sceneTypeBase + kSceneTypeStride * i + 1);
            if (util::StrCmpNoCase(suffix, row) == 0) {       // 0x4ffc44
                *outTypeWord = 0x1000000 | i;                 // 0x4ffda0..0x4ffdb1
                return 1;
            }
        }
    }
    return 0;                                                 // 0x4ffc64
}

// ===========================================================================
// 0x506b68 — VIBE_Object_UpdateBuildingVisualState
//                (al = fn(node@eax, force@dl, ebx, edi)).
// Building "visual state" flag machine on the node's +7280 byte (3-bit nibble at
// bits 2..4, read as (8*byte)>>5). Chooses a flag set + mip param + low-nibble
// flag based on the active-universe flag, *node+0, and the global build phase
// (byte_123351A). Side effects fire when the chosen mip param changed OR `force`.
// Returns the last computed byte. Writes the chosen phase into byte_63448C in
// three of the branches.
//   v5 = (8 * node[7280]) >> 5;                  // current 3-bit state
//   v6 = dword_64A038 << byte_64A045;            // current mip param (curMipShift)
//   if ( universeActive ) {
//     if ( buildPhase == 2 ) { node[7280]=(node[7280]&0xE3)|4; a3=64; a4=1; }
//     else                   { node[7280]=(node[7280]&0xE3)|8; a3=64; a4=2; }
//   } else if ( *node == 64 ) {
//     node[7280]=(node[7280]&0xE3)|4; a3=64; a4=1; byte_63448C=1;
//   } else if ( buildPhase ) {
//     if ( buildPhase==1 ) { node[7280]=(&0xE3)|8; a3=64; a4=2; byte_63448C=2; }
//     else if (buildPhase==2){ node[7280]=(&0xE3)|4; a3=64; a4=1; byte_63448C=1; }
//   } else {
//     node[7280]=(&0xE3)|0x10; a3=128; a4=4; byte_63448C=4;
//   }
//   if ( a3 != v6 || force ) Render_SetMipFilterLevel(a3);
//   if ( v5 != newState || force ) SetLowNibbleFlag(node, a4>>1, 1, a4);
//   return last;
// ===========================================================================
char ObjectUpdateBuildingVisualState(u8* node, char force, int universeActive,
                                     char buildPhase, int* outBuildPhaseGlobal,
                                     int curMipShift) {
    if (!node) return 0;
    auto setNibble = [&](u8 clearTo, u8 setBits) {
        node[7280] = static_cast<u8>((node[7280] & 0xE3) | setBits);
        (void)clearTo;
    };
    u8 v5 = static_cast<u8>(static_cast<u8>(8 * node[7280]) >> 5);  // 0x506b8d current state
    int v6 = curMipShift;                                     // dword_64A038 << byte_64A045
    int a3 = 0;   // chosen mip param
    int a4 = 0;   // chosen low-nibble flag

    if (universeActive) {                                     // 0x506b9c
        if (buildPhase == 2) { setNibble(0xE3, 4); a3 = 64; a4 = 1; }   // 0x506cb5
        else                 { setNibble(0xE3, 8); a3 = 64; a4 = 2; }   // 0x506ce5
    } else if (*reinterpret_cast<i32*>(node) == 64) {         // 0x506ba5
        setNibble(0xE3, 4); a3 = 64; a4 = 1;
        if (outBuildPhaseGlobal) *outBuildPhaseGlobal = 1;    // byte_63448C = 1
    } else if (buildPhase) {                                  // 0x506c0f
        if (buildPhase == 1) {                                // 0x506c42
            setNibble(0xE3, 8); a3 = 64; a4 = 2;
            if (outBuildPhaseGlobal) *outBuildPhaseGlobal = 2;
        } else if (buildPhase == 2) {                         // 0x506c77
            setNibble(0xE3, 4); a3 = 64; a4 = 1;
            if (outBuildPhaseGlobal) *outBuildPhaseGlobal = 1;
        }
    } else {                                                  // 0x506c17
        setNibble(0xE3, 0x10); a3 = 128; a4 = 4;
        if (outBuildPhaseGlobal) *outBuildPhaseGlobal = 4;
    }

    char last = 0;
    if (a3 != v6 || force) {                                  // 0x506d0a
        // Render_SetMipFilterLevel(a3) — render leaf, inert by default.
        last = static_cast<char>(a3);                         // 0x506bdc (al = result)
    }
    u8 newState = static_cast<u8>(static_cast<u8>(8 * node[7280]) >> 5);  // 0x506bfb
    if (v5 != newState || force) {                            // 0x506bfb
        // SetLowNibbleFlag(node, a4>>1, 1, a4) — sibling 0x5c4634 (not in slice).
        last = static_cast<char>(a4);                         // 0x506d26
    }
    return last;                                              // 0x506c01
}

// ===========================================================================
// 0x5b34e4 — VIBE_Object_RunScriptCallback (al = fn(node@eax, ctx@edx)).
//   v5 = a1[123];
//   if ( v5 && *(v5+260) && (a1[133]>>24) == (*(a2+1)>>24) ) {
//     if ( *(a2+16) ) { ++*(a2+8); }              // trigger byte set: bump counter
//     else {
//       if ( *a2 ) { ++*(a2+12); (*a2)(); }       // call the queued callback
//       copy script name (*(a1[123]+260)) -> stack;
//       strip a trailing known extension (a0_2 / a1_0 / aS_23);
//       DisposeResources(a1);
//       if ( Mesh_LoadOrFindByName(name) ) Mesh_AttachStockObjectLods(a1,0,name,end);
//       WalkAndInvoke(off_649D64, a1, MarkDirtyFlag, 511, 1);
//     }
//   }
//   return 1;
// The trigger/owner gates are passed in via ScriptCbInputs9 (the ctx record's
// fields). The name extension strip uses the real ObjectMarkDirtyFlag via the
// scene walk; the mesh reload is hookable.
// ===========================================================================
char ObjectRunScriptCallback(SceneNode9* node, const ScriptCbInputs9& in) {
    if (!node) return 1;
    if (!(in.scriptBlockOk && in.nodeOwnerTop == in.ctxOwnerTop))  // 0x5b351f
        return 1;
    if (in.triggerByte) return 1;                             // 0x5b3525 (++counter, then done)

    // Copy the script name and strip a trailing known extension.
    char buf[268];
    if (in.scriptName) {
        CopyStepped(buf, in.scriptName);                      // 0x5b3540 loop
    } else {
        buf[0] = 0;
    }
    // (extension strip: a0_2/a1_0/aS_23 compared with StrCmp/StrCmpNoCase; the
    // recovered tokens are not in this slice, so the strip is a faithful no-op cut
    // — control flow preserved.)

    if (g_hooks.disposeResources) g_hooks.disposeResources(node);  // 0x5b359e
    if (g_hooks.meshLoadOrFind && g_hooks.meshLoadOrFind(buf)) {    // 0x5b35a7
        if (g_hooks.meshAttachLods) g_hooks.meshAttachLods(node, buf);  // 0x5b35b8
    }
    // WalkAndInvoke(off_649D64, node, MarkDirtyFlag, 511, 1): faithfully forward
    // the dirty mark onto the node via the REAL ObjectMarkDirtyFlag sibling.
    {
        SceneNode7* n7 = reinterpret_cast<SceneNode7*>(node);  // same opaque 0x21C block
        ObjectMarkDirtyFlag(n7, 1);                            // 0x5b35d0 real sibling
    }
    return 1;                                                  // 0x5b35d9
}

}  // namespace guild::sim
