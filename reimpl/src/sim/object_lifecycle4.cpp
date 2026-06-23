// ===========================================================================
// object_lifecycle4.cpp — see object_lifecycle4.h for the module overview and
// the per-function provenance index.
// ===========================================================================
#include "sim/object_lifecycle4.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife4Hooks g_hooks{};

// Spawn tags (gilde.exe rodata).
const char* const kTagSpawn      = "d3:SpawnObject";
const char* const kTagLightInfo  = "d3:SpawnObject(lightinfo)";
const char* const kTagPolys      = "d3:AllocObjectPolys";
const char* const kTagPoints     = "d3:AllocObjectPoints";

void* Alloc(int size, const char* tag) {
    return g_hooks.allocDebug ? g_hooks.allocDebug(size, tag) : nullptr;
}
void Free(void* p) {
    if (p && g_hooks.freeDebug) g_hooks.freeDebug(p);
}
}  // namespace

// Module globals (gilde.exe), owned here.
void* g_objCurrent4       = nullptr;  // dword_13FCD1C
void* g_sceneListHead     = nullptr;  // dword_13FD140
void* g_sceneListSentinel = nullptr;  // &unk_13FCF4C
// g_activeUniverse (off_649D64) is owned by character_query.cpp; we reuse it via the extern below.
bool  g_lightInfoForce5   = false;    // byte_649D54

void ObjLife4SetHooks(const ObjLife4Hooks& h) { g_hooks = h; }
void ObjLife4ResetHooks() { g_hooks = ObjLife4Hooks{}; }

// ===========================================================================
// gilde.exe 0x5b054c — VIBE_Object_Spawn (al = kind, edx = name)
// ===========================================================================
//   v4 = AllocDebug(0x21C, "d3:SpawnObject");
//   if (kind >= 5) *((_DWORD*)v4+122) = AllocDebug(0x1AC, "d3:SpawnObject(lightinfo)");
//   InitStruct(v4);
//   if (kind < 5) { *(v4+533) = kind; goto LABEL_10; }
//   c = *name;
//   if (c >= 'r') {
//       if (c <= 'r')      *(v4+533) = 6;
//       else if (c=='s')   *(v4+533) = 8;
//       else               *(v4+533) = 5;
//   } else {
//       if (c != 'p')      *(v4+533) = 5;
//       else               *(v4+533) = 7;
//   }
//   if (byte_649D54 && *(v4+533)==6) *(v4+533) = 5;     // LABEL_7
//   LABEL_10: t = *(v4+533);
//   if (t==6) *((_DWORD*)*(v4+488)+101) = 0;            // *(lightInfo+404)=0
//   else if (t==8) *(v4+529) |= 4;
//   *(v4+534) = *(v4+533);
//   StrNCopyPad(v4, name, 64);
//   *(v4+520) = off_649D64;
//   return v4;
// ---------------------------------------------------------------------------
ObjNode4* ObjectSpawn(u8 kind, const char* name) {
    auto* node = static_cast<ObjNode4*>(Alloc(kNodeAllocSize, kTagSpawn));
    if (kind >= 5 && node) {
        node->lightInfo = Alloc(kLightInfoSize, kTagLightInfo);
    }
    if (g_hooks.initStruct && node) g_hooks.initStruct(node);

    if (!node) return nullptr;

    if (kind < 5) {
        node->nodeType = kind;
    } else {
        u8 c = static_cast<u8>(name ? name[0] : 0);
        if (c >= 0x72u) {                 // >= 'r'
            if (c <= 0x72u)               node->nodeType = 6;   // 'r'
            else if (c == 115)            node->nodeType = 8;   // 's'
            else                          node->nodeType = 5;
        } else {
            if (c != 112)                 node->nodeType = 5;
            else                          node->nodeType = 7;   // 'p'
        }
        if (g_lightInfoForce5 && node->nodeType == 6) {
            node->nodeType = 5;
        }
    }

    u8 t = node->nodeType;
    if (t == 6) {
        // *(_DWORD *)(*(_DWORD *)(node+488) + 404) = 0;  (lightInfo +404)
        if (node->lightInfo) {
            *reinterpret_cast<i32*>(static_cast<u8*>(node->lightInfo) + 404) = 0;
        }
    } else if (t == 8) {
        node->flags529 |= 4u;
    }
    node->nodeTypeShadow = node->nodeType;

    if (g_hooks.strNCopyPad) {
        g_hooks.strNCopyPad(node->name, name, 64);
    }
    node->universe = g_activeUniverse;
    return node;
}

// ===========================================================================
// gilde.exe 0x5b0660 — VIBE_Object_DisposeResources (eax = node)
// ===========================================================================
//   if (!node) return node;
//   v3 = *(node+520);  // universe
//   if (node == dword_1408A70) { free dword_1408A78; clear A70/A74/A78. }
//   if (*(node+533)==1 && *(node+534)!=1) *(node+533) = *(node+534);
//   v5 = *(node+492);
//   if (v5 && *(v5+2312)) {
//       if (*(v5+2304)) { Free(*(v5+2304)); *(*(node+492)+2304)=0; }
//       if (*(*(node+492)+2308)) { Free(..); *(*(node+492)+2308)=0; }
//   }
//   t = *(node+533);
//   if (t==6 || (t==5 && *name==114)) LightRemoveCacheEntry(node);
//   WalkAndInvoke(universe, 0, LightRemoveCacheEntry, 7, node);
//   if (*(node+533)==8) WalkAndInvoke(universe, 0, ShadowRemoveByLight, 64, node);
//   if (*(node+529) & 4) ShadowClearAll(node);
//   return FreeDrawData(node);
// ---------------------------------------------------------------------------
// The dword_1408A70/74/78 "scratch geometry" cache is a render-owned global; we
// keep the faithful node-side control flow (the draw-data scratch triple at
// +2304/+2308/+2312, the type-driven light/shadow cache removals) and route the
// heap frees / scene-graph walks / cache removals through the hooks.
int ObjectDisposeResources(ObjNode4* node) {
    if (!node) return 0;

    if (node->nodeType == 1 && node->nodeTypeShadow != 1) {
        node->nodeType = node->nodeTypeShadow;
    }

    if (node->drawData) {
        auto* dd = static_cast<DrawData*>(node->drawData);
        if (dd->scratchValid) {
            if (dd->scratch0) { Free(dd->scratch0); dd->scratch0 = nullptr; }
            if (dd->scratch1) { Free(dd->scratch1); dd->scratch1 = nullptr; }
        }
    }

    u8 t = node->nodeType;
    if (t == 6 || (t == 5 && static_cast<u8>(node->name[0]) == 114)) {
        if (g_hooks.lightRemoveCache) g_hooks.lightRemoveCache(node);
    }
    if (g_hooks.walkAndInvoke) g_hooks.walkAndInvoke(node, 7, 0);
    if (node->nodeType == 8) {
        if (g_hooks.walkAndInvoke) g_hooks.walkAndInvoke(node, 64, 0);
    }
    if (node->flags529 & 4) {
        if (g_hooks.shadowClearAll) g_hooks.shadowClearAll(node);
    }
    return ObjectFreeDrawData(node);
}

// ===========================================================================
// gilde.exe 0x5b0a20 — VIBE_Object_LinkIntoScene (al = node)
// ===========================================================================
//   if (*(node+533)==3 && !dword_13FCD1C) {
//       if (node) { dword_13FCD1C = node; InvalidateCurrent(0); }
//       SetWorldTranslation(node, node+132);
//   }
//   if (*(node+533)) {
//       v2 = dword_13FD140;
//       *(node+496) = &unk_13FCF4C;     // prev = sentinel
//       *(node+500) = v2;               // next = old head
//       *(v2+496) = node;               // old-head.prev = node
//       *(node+528) |= 3;
//       dword_13FD140 = node;           // head = node
//   }
//   *(node+520) = off_649D64;           // universe
//   return node;
// ---------------------------------------------------------------------------
ObjNode4* ObjectLinkIntoScene(ObjNode4* node) {
    if (node->nodeType == 3 && g_objCurrent4 == nullptr) {
        if (node) {
            g_objCurrent4 = node;
            if (g_hooks.invalidateCurrent) g_hooks.invalidateCurrent(0);
        }
        if (g_hooks.setWorldTranslation) g_hooks.setWorldTranslation(node);
    }
    if (node->nodeType) {
        void* oldHead = g_sceneListHead;
        node->prevList = g_sceneListSentinel;
        node->nextList = oldHead;
        if (oldHead) {
            static_cast<ObjNode4*>(oldHead)->prevList = node;
        }
        node->flags528 |= 3u;
        g_sceneListHead = node;
    }
    node->universe = g_activeUniverse;
    return node;
}

// ===========================================================================
// gilde.exe 0x5b0adc — VIBE_Object_UnlinkFromScene (eax = node)
// ===========================================================================
//   switch (*(node+533)) {
//     case 3: if (node == dword_13FCD1C) dword_13FCD1C = 0;  // fallthrough
//     case 1,2,4,5,6,7,8:
//        v1 = *(node+500);                       // next
//        *(*(node+496)+500) = v1;                // prev.next = next
//        *(v1+496) = *(node+496);                // next.prev = prev
//        *(node+528) &= ~2;
//        break;
//     default: *(node+528) &= ~2; break;
//   }
//   return node;
// ---------------------------------------------------------------------------
// The original dereferences prev (*(node+496)) and next unconditionally; in the
// live game the scene list is sentinel-terminated so neither is null. We splice
// prev/next exactly (guarding null for test isolation). The default case
// (nodeType==0) just clears the linked bit.
ObjNode4* ObjectUnlinkFromScene(ObjNode4* node) {
    u8 t = node->nodeType;
    if (t == 0) {
        node->flags528 &= ~2u;
        return node;
    }
    if (t == 3 && node == g_objCurrent4) {
        g_objCurrent4 = nullptr;
    }
    auto* next = static_cast<ObjNode4*>(node->nextList);
    auto* prev = static_cast<ObjNode4*>(node->prevList);
    if (prev) prev->nextList = next;   // prev.next = next
    if (next) next->prevList = prev;   // next.prev = prev
    node->flags528 &= ~2u;
    return node;
}

// ===========================================================================
// gilde.exe 0x5b23ec — VIBE_Object_UnlinkFromList (eax = node)
// ===========================================================================
//   prev = *(node+496);
//   if (prev) {
//       next = *(node+500);
//       *(prev+500) = next;                       // prev.next = next
//       if (!next) {
//           parent = *(node+504);
//           if (node == *(parent+508)) *(parent+508) = *(node+496);  // first=prev
//       } else {
//           *(next+496) = prev;                   // next.prev = prev
//       }
//   } else {
//       next = *(node+500);
//       if (next) { *(next+496) = 0; }            // next.prev = 0
//       else {
//           parent = *(node+504);
//           if (node == *(parent+508)) *(parent+508) = 0;
//       }
//   }
//   *(node+504)=0; *(node+500)=0; *(node+496)=0; *(node+528) &= ~2;
//   return node;
// ---------------------------------------------------------------------------
// The decompiler's tangled gotos reduce to: detach a node from a doubly-linked
// sibling list whose tail is tracked by parent.firstChild(+508). Reconstructed
// faithfully from the two symmetric branches.
ObjNode4* ObjectUnlinkFromList(ObjNode4* node) {
    auto* prev = static_cast<ObjNode4*>(node->prevList);
    auto* next = static_cast<ObjNode4*>(node->nextList);
    if (prev) {
        prev->nextList = next;                 // prev.next = next
        if (!next) {
            if (auto* parent = static_cast<ObjNode4*>(node->parent)) {
                if (parent->firstChild == node) parent->firstChild = prev;
            }
        } else {
            next->prevList = prev;             // next.prev = prev
        }
    } else {
        if (next) {
            next->prevList = nullptr;          // next.prev = 0
        } else {
            if (auto* parent = static_cast<ObjNode4*>(node->parent)) {
                if (parent->firstChild == node) parent->firstChild = nullptr;
            }
        }
    }
    node->parent   = nullptr;
    node->nextList = nullptr;
    node->prevList = nullptr;
    node->flags528 &= ~2u;
    return node;
}

// ===========================================================================
// gilde.exe 0x5b0e18 — VIBE_Object_InitSubMeshEntry (eax = entry, edx = parent)
// ===========================================================================
//   *(entry+376) = 0; *(entry) = 0; *(entry+8)=0; *(entry+4)=0; *(entry+12)=0;
//   *(entry+16)=0; *(entry+24)=parent;
//   end = entry + 348;  *(BYTE*)(entry+376) = -1;       // net tag = 0xFF
//   do {                                                // 3 LOD slots
//       entry += 116;
//       *(entry-84)=0; *(entry+20)=0;                   // field32, extraPtr
//       v3 = *(entry-84); *(entry-80)=v3; *(entry-88)=v3; // field36, field28
//       *(entry+16)=0;
//       *(BYTE*)(entry+22) &= 0xFD;                      // slotFlags bit1 clear
//   } while (entry != end);
//   return entry;
// ---------------------------------------------------------------------------
void ObjectInitSubMeshEntry(SubMeshEntry* entry, void* parentDraw) {
    entry->points     = nullptr;
    entry->polys      = nullptr;
    entry->polyCount  = 0;
    entry->pointCount = 0;
    entry->mesh       = nullptr;
    entry->parentDraw = parentDraw;
    entry->tag        = 0xFFu;          // dword zeroed then low byte set to 0xFF
    for (int k = 0; k < 3; ++k) {
        SubMeshLod& s = entry->lod[k];
        s.field32  = 0;
        s.extraPtr = nullptr;
        s.field36  = s.field32;         // = *(entry-84) after the zero write
        s.field28  = s.field32;
        s.meshData = nullptr;           // +16 within the slot view -> meshData
        s.slotFlags = static_cast<u8>(s.slotFlags & 0xFDu);
    }
}

// ===========================================================================
// gilde.exe 0x5b0c8c — VIBE_Object_FreeSubMeshData (eax = entry)
// ===========================================================================
//   if (*(BYTE*)(entry+376)!=0xFF || (*(BYTE*)(entry+378)&1))
//       ChangeTransparency(*(entry+24), entry, 255, entry);
//   if (*(int*)(entry+8) > 0 && *(entry)) { Free(*(entry)); *(entry)=0; *(entry+8)=0; }
//   if (*(int*)(entry+12) > 0 && *(entry+4)) { Free(*(entry+4)); *(entry+4)=0; *(entry+12)=0; }
//   if (*(entry+20)) {
//       if (*(entry+16))
//          for (i=0; i < *(*(entry+16)+480); ++i)
//              if (*(*(entry+20)+4*i)) TextureRelease(*(*(entry+20)+4*i));
//       Free(*(entry+20)); *(entry+20)=0;
//   }
//   v6 = *(entry+16);
//   if (v6) { (*(void(**)(int))(v6+488))(255); *(entry+16)=0; }  // mesh vtbl release
//   v8 = entry + 348;
//   do { v9 = *(v7+132); if (v9) { AnimReleaseMeshData(v9); *(v7+132)=0; } v7+=116; }
//   while (v7 != v8);
//   return v9;
// ---------------------------------------------------------------------------
// The texture-array loop bound *(*(entry+16)+480) and the mesh vtbl release at
// +488 are render-owned; the texArray and the mesh's texture-count are passed
// to the texture-release hook through the entry's own fields. We keep the
// faithful field clears; heap free / texture release / anim release are hooks.
int ObjectFreeSubMeshData(SubMeshEntry* entry) {
    if (entry->tag != 0xFFu || (entry->flagsByte & 1)) {
        if (g_hooks.changeTransparency) {
            ObjNode4* parent = static_cast<ObjNode4*>(entry->parentDraw);
            g_hooks.changeTransparency(parent, entry, 255, 0);
        }
    }
    if (entry->polyCount > 0 && entry->points) {
        Free(entry->points);
        entry->points = nullptr;
        entry->polyCount = 0;
    }
    if (entry->pointCount > 0 && entry->polys) {
        Free(entry->polys);
        entry->polys = nullptr;
        entry->pointCount = 0;
    }
    if (entry->texArray) {
        if (entry->mesh) {
            // *(*(entry+16)+480) is the texture count; each non-null slot of the
            // texArray is released. Modeled as the mesh's +480 dword.
            int count = *reinterpret_cast<i32*>(
                static_cast<u8*>(entry->mesh) + 480);
            auto** tex = reinterpret_cast<void**>(entry->texArray);
            for (int i = 0; i < count; ++i) {
                if (tex[i] && g_hooks.textureRelease) {
                    g_hooks.textureRelease(tex[i]);
                }
            }
        }
        Free(entry->texArray);
        entry->texArray = nullptr;
    }
    if (entry->mesh) {
        // (*(void (**)(int))(mesh+488))(255) — model-release vtbl call (hook-less
        // here; the field clear is the observable effect).
        entry->mesh = nullptr;
    }
    for (int k = 0; k < 3; ++k) {
        if (entry->lod[k].meshData) {
            if (g_hooks.animReleaseMesh) g_hooks.animReleaseMesh(entry->lod[k].meshData);
            entry->lod[k].meshData = nullptr;
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x5b0da0 — VIBE_Object_FreeDrawData (eax = node)
// ===========================================================================
//   dd = *(node+492);
//   if (dd) {
//       for (i=0; i < *(BYTE*)(dd+2316); ++i) FreeSubMeshData(dd + 244 + 384*i);
//       FreeSubMeshData(dd + 1396);
//       *(BYTE*)(dd+2316) = 0;
//       Free(dd); *(node+492) = 0;
//   }
//   *(node+460) = 0;
//   return node;
// ---------------------------------------------------------------------------
// RETURN NOTE (disasm 0x5b0da0): the original returns eax = the input node on
// the null-drawData path, and eax = VIBE_Memory_FreeDebug's return on the freed
// path. FreeDebug is a void hook here, so its eax is unavailable; we return 1
// (the header's documented stand-in). The node-side effects are 1:1. The sole
// caller VIBE_Object_DisposeResources tail-returns this, and its own callers
// likewise do not branch on the value.
int ObjectFreeDrawData(ObjNode4* node) {
    if (node->drawData) {
        auto* dd = static_cast<DrawData*>(node->drawData);
        int n = dd->subMeshCount;
        for (int i = 0; i < n; ++i) {
            if (dd->subMeshes) ObjectFreeSubMeshData(&dd->subMeshes[i]);
        }
        if (dd->trailing) ObjectFreeSubMeshData(dd->trailing);
        dd->subMeshCount = 0;
        Free(node->drawData);
        node->drawData = nullptr;
    }
    node->meshFrame = nullptr;
    return 1;
}

// ===========================================================================
// gilde.exe 0x5b0c10 — VIBE_Object_AllocPolysAndPoints (eax = entry)
// ===========================================================================
//   if (*(entry+8) && *(entry+12)) {           // polyCount && pointCount
//       if (*(entry+4)) { Free(*(entry+4)); *(entry+4) = 0; }
//       *(entry+4) = AllocDebug(40 * *(entry+12), "d3:AllocObjectPolys");
//       if (*(entry)) { Free(*(entry)); *(entry) = 0; }
//       result = AllocDebug(80 * (*(entry+8) + 8), "d3:AllocObjectPoints");
//       *(entry) = result;
//   }
//   return result;
// ---------------------------------------------------------------------------
// Free+realloc polys (40 * pointCount) THEN points (80 * (polyCount+8)).
// Returns the points block. (disasm 0x5b0c10: on the no-op path the original
// returns eax = the input `entry` pointer; we return `entry->points` instead —
// observationally dead, no caller uses the no-op return. Active-path return is
// the freshly-alloc'd points block, faithful.)
void* ObjectAllocPolysAndPoints(SubMeshEntry* entry) {
    void* result = entry->points;
    if (entry->polyCount && entry->pointCount) {
        if (entry->polys) { Free(entry->polys); entry->polys = nullptr; }
        entry->polys = Alloc(40 * entry->pointCount, kTagPolys);
        if (entry->points) { Free(entry->points); entry->points = nullptr; }
        result = Alloc(80 * (entry->polyCount + 8), kTagPoints);
        entry->points = result;
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x5b2964 — VIBE_Object_ChangeTransparencySubMeshes (eax=node, edx=&alpha)
// ===========================================================================
//   if (*(node+492)) {
//       v4 = 0; v5 = 0;
//       while (1) {
//           v6 = *(node+492);
//           if (v5 >= *(BYTE*)(v6+2316)) break;
//           ChangeTransparency(node, v6+244+v4, *alpha, ++v5);
//           v4 += 384;
//       }
//   }
//   return 1;
// ---------------------------------------------------------------------------
char ObjectChangeTransparencySubMeshes(ObjNode4* node, const int* alpha) {
    if (node->drawData) {
        auto* dd = static_cast<DrawData*>(node->drawData);
        unsigned v5 = 0;
        while (v5 < static_cast<unsigned>(dd->subMeshCount)) {
            SubMeshEntry* sub = dd->subMeshes ? &dd->subMeshes[v5] : nullptr;
            ++v5;
            if (g_hooks.changeTransparency) {
                g_hooks.changeTransparency(node, sub, *alpha,
                                           static_cast<int>(v5));
            }
        }
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x5b29b0 — VIBE_Object_ApplyTransparencyTree (eax = node, edx = alpha)
// ===========================================================================
//   if (node)
//       LOBYTE(node) = WalkAndInvoke(off_649D64, node,
//                                    ChangeTransparencySubMeshes, 576, alpha);
//   return (char)node;
// ---------------------------------------------------------------------------
char ObjectApplyTransparencyTree(ObjNode4* node, int alpha) {
    if (node) {
        if (g_hooks.walkAndInvoke) {
            return static_cast<char>(g_hooks.walkAndInvoke(node, 576, alpha));
        }
        return 0;
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x5c4634 — VIBE_Object_SetLowNibbleFlag (eax=node, dl=new, bl=rebuild)
// ===========================================================================
//   v5 = *(BYTE*)(node+7281) & 0xF;
//   result = newNibble;
//   if (v5 != newNibble) {
//       FloorFreeTileBuffers(node);
//       hi = *(BYTE*)(node+7281) & 0xF0;
//       *(node+7281) = hi;
//       *(node+7281) = (newNibble & 0xF) | hi;
//       result = FloorAllocInflateBuffers(node);
//       if (rebuild) return FloorBuildTilePolys(node);
//   }
//   return result;
// ---------------------------------------------------------------------------
// *(node+7281) lies outside the 540-byte ObjNode4 block, so the byte is passed
// by pointer (the original reads/writes node+7281 directly).
//
// RETURN-VALUE NOTE (disasm 0x5c4634, verified): the original's return is
//   * unchanged path:  the new nibble `a2` (== `newNibble`);
//   * changed path:    the *return value of the floor leaves* — eax from
//     VIBE_Floor_AllocInflateBuffers (no rebuild) or VIBE_Floor_BuildTilePolys
//     (rebuild). Those leaves (0x5bce10 / 0x5bc45c) are NOT reconstructed here;
//     they are routed through the void floor hooks, so their eax is unavailable.
//   Both live callers (VIBE_Cutscene_LoadScene 0x4aa2e8 and
//   VIBE_Object_UpdateBuildingVisualState 0x506d26) DISCARD the return, so the
//   value is observationally dead. We model the changed-path return as the
//   resulting +7281 byte (a stable, testable stand-in for the unavailable
//   leaf eax); the unchanged path returns `newNibble` faithfully.
u8 ObjectSetLowNibbleFlag(u8* tileByte, u8 newNibble, bool rebuild,
                          ObjNode4* node) {
    u8 v5 = static_cast<u8>(*tileByte & 0x0Fu);
    u8 result = newNibble;
    if (v5 != newNibble) {
        if (g_hooks.floorFreeTiles) g_hooks.floorFreeTiles(node);
        u8 hi = static_cast<u8>(*tileByte & 0xF0u);
        *tileByte = hi;
        *tileByte = static_cast<u8>((newNibble & 0x0Fu) | hi);
        if (g_hooks.floorAllocInflate) g_hooks.floorAllocInflate(node);
        result = *tileByte;
        if (rebuild) {
            if (g_hooks.floorBuildPolys) g_hooks.floorBuildPolys(node);
            return *tileByte;
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x567520 — VIBE_Object_MarkState2  (__thiscall, ecx = this)
// ===========================================================================
//   VIBE_Command_RequestBuildOp90_Thunk(*(this+4), 2);  return 1;
// disasm: `mov edx,2; mov eax,[eax+4]; call thunk`. The thunk's first arg is
// the pointer field at this+4 (a gameplay/command object, NOT an ObjNode4),
// edx=2. The build-op thunk is a void hook; arg-1 is passed as `node` here.
int ObjectMarkState2(ObjNode4* node) {
    if (g_hooks.requestBuildOp) g_hooks.requestBuildOp(node, 2);
    return 1;
}

// ===========================================================================
// gilde.exe 0x538400 / 0x53841c — VIBE_Object_ResetState[Alt] (byte-identical)
// ===========================================================================
//   VIBE_Light_SetGrayColorThunk(0, 4608);  return 1;
int ObjectResetState(ObjNode4* /*node*/) {
    if (g_hooks.lightSetGray) g_hooks.lightSetGray(0, 4608);
    return 1;
}
int ObjectResetStateAlt(ObjNode4* node) { return ObjectResetState(node); }

}  // namespace guild::sim
