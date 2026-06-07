// ===========================================================================
// object_lifecycle7.cpp — see object_lifecycle7.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle7.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sim/character_query.h"   // g_activeUniverse (off_649D64) — reused via extern

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife7Hooks g_hooks;
}  // namespace

void ObjLife7SetHooks(const ObjLife7Hooks& hooks) { g_hooks = hooks; }
void ObjLife7ResetHooks() { g_hooks = ObjLife7Hooks(); }

// ===========================================================================
// 0x5af298 — VIBE_Object_MarkDirtyFlag  (al = fn(node@eax, clearHi@dl)).
//   a1[528] |= 4u;
//   if ( a2 ) a1[530] &= ~0x80u;
//   a1[531] &= ~1u;
//   return 1;
// (the original clears +531 unconditionally AFTER the conditional +530 clear).
// ===========================================================================
char ObjectMarkDirtyFlag(SceneNode7* node, char clearHi) {
    if (!node) return 1;
    node->b(528) |= 4u;                       // 0x5af298
    if (clearHi)                              // 0x5af2a1
        node->b(530) &= static_cast<u8>(~0x80u);  // 0x5af2ad
    node->b(531) &= static_cast<u8>(~1u);     // 0x5af2a3
    return 1;                                  // 0x5af2ac
}

// ===========================================================================
// 0x40e818 — VIBE_Object_Reinitialize  (eax = fn(a1@eax,a2@edx,a3@ecx,a4@ebx,a5)).
// Clip-clamp a dirty rectangle (a1=x, a2=y, a3=h, a4=w, a5=obj) against the
// screen bounds and append it into the dirty-rect queue (dword_62D2DC[0]).
//   v5 = base; v9 = a2; v6 = a3; result = a5;
//   if ( obj+400 && obj+424 ) {
//     if ( x > maxX ) x = maxX;
//     if ( (x & 1) != 0 ) { w += 2; w &= 0xFE (low byte); --x; }
//     if ( h + y > maxY ) v6 = maxY - y;
//     if ( w + x > maxX ) w = maxX - x;
//     if ( y < minY ) v9 = minY;
//     if ( x < minX ) x = minX;
//     if ( w < 0 ) w = 1;
//     if ( v6 < 0 ) v6 = 1;
//     scan occupied slots (stride 20, +8 != 0, bound 10240) -> v8 count;
//     if ( v8 < 512 ) {
//       rec = base + 20*v8;
//       rec[+12]=w; rec[+16]=v6; rec[+0]=x; rec[+4]=v9; rec[+8]=obj; base=v5;
//     }
//   }
//   return result;
// Here `q->base` (a non-zero flag) stands in for dword_62D2DC[0]; `obj` stands
// for the a5 record (its +400 / +424 fields). The caller passes `obj` !=0 to
// model "both fields set"; obj==0 models the reject path. `result` (eax) tracks
// the original: it is a5 unless a slot is appended, in which case it becomes the
// appended record's queue offset (base + 20*v8); but the low-fuel scan also
// leaves `result` as the scan terminator. We reproduce the value the original
// returns: the appended record offset when stored, else `result==obj` (a5).
// ===========================================================================
int ObjectReinitialize(DirtyRectQueue* q, int x, int y, int w, int h, int obj,
                       int clipMinX, int clipMinY, int clipMaxX, int clipMaxY) {
    if (!q) return obj;
    int v9 = y;                  // v9 = a2
    int v6 = h;                  // v6 = a3
    int result = obj;            // result = a5
    if (obj != 0) {              // *(a5+400) && *(a5+424)
        if (x > clipMaxX)        // 0x40e857
            x = clipMaxX;
        if ((x & 1) != 0) {      // 0x40e862
            w += 2;
            w = w & 0xFE;        // LOBYTE(a4) = a4 & 0xFE  (low byte only)
            --x;
        }
        if (h + y > clipMaxY)    // 0x40e880
            v6 = clipMaxY - y;
        if (w + x > clipMaxX)    // 0x40e897
            w = clipMaxX - x;
        if (y < clipMinY)        // 0x40e8ac
            v9 = clipMinY;
        if (x < clipMinX)        // 0x40e8bd
            x = clipMinX;
        if (w < 0)               // 0x40e8c5
            w = 1;
        if (v6 < 0)              // 0x40e8c9
            v6 = 1;
        // scan for the first free slot (record +8 == 0), bounded at 10240 bytes.
        int v8 = 0;
        for (result = 0;
             (q->slots[result / 20].obj != 0) && result < 10240;
             result += 20)
            ++v8;
        if (v8 < 512) {                       // 0x40e902
            DirtyRectQueue::Rect& rec = q->slots[v8];
            rec.w = w;                        // +12
            rec.h = v6;                       // +16
            rec.x = x;                        // +0
            rec.y = v9;                       // +4
            rec.obj = obj;                    // +8
            if (v8 + 1 > q->count) q->count = v8 + 1;
            result = q->base + 20 * v8;       // result = dword_62D2DC[0] + 20*v8
        }
    }
    return result;                            // 0x40e90a
}

// ===========================================================================
// 0x41e388 — VIBE_Object_ApplyAnimScale  (eax = fn(handle@eax)).
//   v2 = handle;
//   if ( handle != -1 ) {
//     if ( AnimationFlags_Compute(handle, v6) ) {
//       node = base + 740*v2;  node[+108] = v3;       // v3 = computed scale
//       if ( v3 > 1 ) { node[+20]w = v3*v5; node[+22]w = v3*LOWORD(v6[0]); }
//       if ( v3 < -1 ) { node[+20]w = v5/abs(v3); node[+22]w = v6[0]/abs(v3); }
//     } else {
//       return AnimationFlags_Compute(v2, v6);        // no-flags path: eax = call
//     }
//   }
//   return result;
// v3 = the scale, v5 = baseW, v6[0] = baseH. We route AnimationFlags_Compute
// through the hook: it returns the scale (v3) and writes baseW->*outW, baseH->*outH.
// `node` is the SceneNode7 the original addresses as base+740*handle; the +108
// dword and the +20/+22 words are written there.
// ===========================================================================
int ObjectApplyAnimScale(SceneNode7* node, int handle) {
    int v2 = handle;
    if (handle == -1)                         // 0x41e395
        return handle;                        // result == v2 (untouched)

    int scale = 0, baseW = 0, baseH = 0;
    int ok = g_hooks.animationFlagsCompute
                 ? g_hooks.animationFlagsCompute(handle, &scale, &baseW, &baseH)
                 : 0;
    if (!ok) {
        // no-flags path: original re-invokes the compute and returns its result.
        return g_hooks.animationFlagsCompute
                   ? g_hooks.animationFlagsCompute(v2, &scale, &baseW, &baseH)
                   : 0;                        // 0x41e45d
    }
    if (!node) return 0;
    node->d(108) = scale;                     // 0x41e3d3
    if (scale > 1) {                          // 0x41e3d9
        *reinterpret_cast<i16*>(node->raw + 20) = static_cast<i16>(scale * baseW);
        *reinterpret_cast<i16*>(node->raw + 22) = static_cast<i16>(scale * baseH);
    }
    if (scale < -1) {                         // 0x41e3f3
        int a = std::abs(scale);
        *reinterpret_cast<i16*>(node->raw + 20) = static_cast<i16>(baseW / a);
        *reinterpret_cast<i16*>(node->raw + 22) = static_cast<i16>(baseH / a);
    }
    // result = base + 740*v2 in the original; we return the post-store marker 1.
    return 1;
}

// ===========================================================================
// 0x594a68 — VIBE_Object_RequestChangeZustand  (eax = fn(objId@eax, delta@dl, ctx@ecx)).
//   v8[0] = delta;
//   node = GameObject_QueryFind(0, 2, 7, 1, objId);
//   if ( !node ) return Sprintf(buf, "gm_ChangeObjektZustand():Could not find Object-ID: %i", ...);
//   if ( *(node+18) + delta < 0 ) v8[0] = 0;     // clamp so state never goes negative
//   Command_BeginDeltaPacket(node, ctx);
//   Command_AppendRawField(1u, 1u, v8, 18);
//   return Command_QueueRequestState22();
// The Sprintf on the error path formats into a stack buffer and returns its int
// result; we model that with snprintf's return (and forward the text to reportError).
// `curState` models *(node+18). The hook resolves the node.
// ===========================================================================
int ObjectRequestChangeZustand(int objId, char delta, int ctx, int curState) {
    void* node = g_hooks.gameObjectQueryFind
                     ? g_hooks.gameObjectQueryFind(0, 2, 7, 1, objId)
                     : nullptr;               // 0x594a82
    if (!node) {                              // 0x594a8c
        char buf[128];
        int r = std::snprintf(
            buf, sizeof(buf),
            "gm_ChangeObjektZustand():Could not find Object-ID: %i", objId);
        if (g_hooks.reportError) g_hooks.reportError(buf);
        return r;
    }
    char field = delta;                       // v8[0] = a2
    if (curState + delta < 0)                 // 0x594a9c
        field = 0;
    if (g_hooks.commandBeginDeltaPacket)
        g_hooks.commandBeginDeltaPacket(node, ctx);     // 0x594aab
    if (g_hooks.commandAppendRawField)
        g_hooks.commandAppendRawField(1u, 1u, &field, 18);  // 0x594acc
    return g_hooks.commandQueueRequestState22
               ? g_hooks.commandQueueRequestState22()
               : 0;                           // 0x594add
}

// ===========================================================================
// 0x510000 — VIBE_Object_FormatNameWithCountRecursive  (eax = fn(node@eax, depth@edx)).
//   if ( node ) {
//     v3 = typeTable + 65*(*node) ;    Light_SetGrayColorThunk(32, 512);
//     result = Sprintf(&buf[depth], "%s (%li)", v3+1, *(node+14));
//     for ( i = *(node+20); i; i = *(i+63) )
//       result = FormatNameWithCountRecursive(i, depth + 1);
//   }
//   return result;
// `buf[depth]` indexes into a _WORD[264] stack buffer (so each recursion writes
// at a 2-byte-stepped slot); we model the name table by `typeNames` indexed by
// the node's typeIndex, and write the formatted string at buf + 2*depth.
// Returns the buffer the deepest sprintf wrote to (faithful eax).
// ===========================================================================
char* ObjectFormatNameWithCountRecursive(NameCountNode* node, int depth,
                                         char* buf, const char* const* typeNames) {
    char* result = buf;
    if (node) {                                          // 0x510010
        if (g_hooks.lightSetGrayColorThunk)
            g_hooks.lightSetGrayColorThunk(32, 512);     // 0x51003d
        const char* nm = typeNames ? typeNames[node->typeIndex] : "";
        char* dst = buf + 2 * depth;                     // &v8[depth] (_WORD step)
        std::snprintf(dst, 256, "%s (%ld)", nm ? nm : "",
                      static_cast<long>(node->count));   // 0x510057
        result = dst;
        for (NameCountNode* i = node->firstChild; i; i = i->nextSibling)  // 0x510064
            result = ObjectFormatNameWithCountRecursive(i, depth + 1, buf,
                                                        typeNames);       // 0x51006d
    }
    return result;                                       // 0x510014
}

// ===========================================================================
// 0x5f1d00 — VIBE_Object_CloneOrFreeData  (eax = fn(block@eax, size@edx)).
//   if ( !block ) return Memory_AllocFromFreeList(size);
//   if ( size ) {
//     v4 = Memory_BlockHeaderClear(block);        // stored byte size
//     v6 = Memory_ShrinkBlock(block);             // non-zero when it already fits
//     if ( !v6 ) {
//       v7 = Memory_AllocFromFreeList(size);
//       v6 = v7;
//       if ( v7 ) { qmemcpy(v7, block, v4); Memory_ReturnToFreeList(block); }
//       else      { Memory_ShrinkBlock(block); }
//     }
//     return v6;
//   } else { Memory_ReturnToFreeList(block); return 0; }
// NOTE the original passes `v5` (an aliased register == size) to the second
// AllocFromFreeList; faithfully that is `size`.
// ===========================================================================
void* ObjectCloneOrFreeData(const void* block, unsigned size) {
    if (!block)                                          // 0x5f1d0b
        return g_hooks.memAllocFromFreeList
                   ? g_hooks.memAllocFromFreeList(size)
                   : nullptr;                            // 0x5f1d0f
    if (size) {                                          // 0x5f1d18
        unsigned v4 = g_hooks.memBlockHeaderClear
                          ? g_hooks.memBlockHeaderClear(block)
                          : 0;                           // 0x5f1d2c
        // v6 = ShrinkBlock(block): non-null (the in-place block ptr) when it fits,
        // returned as-is; else the realloc path produces v6. One void* tracks both,
        // exactly as the original reuses one register for v6.
        void* v6 = g_hooks.memShrinkBlock
                       ? g_hooks.memShrinkBlock(block)
                       : nullptr;                        // 0x5f1d35
        void* result = v6;
        if (!v6) {                                       // 0x5f1d39
            void* v7 = g_hooks.memAllocFromFreeList
                           ? g_hooks.memAllocFromFreeList(size)
                           : nullptr;                    // 0x5f1d3d
            result = v7;
            if (v7) {
                if (v4)
                    std::memcpy(v7, block, v4);          // qmemcpy(v7, block, v4)
                if (g_hooks.memReturnToFreeList)
                    g_hooks.memReturnToFreeList(block);   // 0x5f1d66
            } else {
                if (g_hooks.memShrinkBlock)
                    g_hooks.memShrinkBlock(block);        // 0x5f1d71
            }
        }
        return result;                                   // 0x5f1d76
    }
    if (g_hooks.memReturnToFreeList)
        g_hooks.memReturnToFreeList(block);              // 0x5f1d1a
    return nullptr;                                      // 0x5f1d1f
}

// ===========================================================================
// 0x42e12c — VIBE_Object_InsertNamedNode  (eax = fn(name@eax, arg@edx, kind@bl)).
//   copy name (2-byte stepped) into a 264-byte stack buffer;
//   result = ModelIo_LoadBinaryAnimation(buf, arg, kind);
//   if ( !result ) return 0;
//   v8 = head;
//   result[+89dword] = head;          // *(result+356) = old head
//   head = result;
//   result[+88dword] = &sentinel;     // *(result+352) = sentinel
//   *(v8+352) = result;               // old head's +352 link-back = result
//   return result;
// The name copy is a verbatim 2-byte-step loop (UTF-16-ish) but for ASCII C
// strings it copies through the NUL; we replicate the byte semantics with a plain
// copy (the loaded animation only uses the resulting C string). The global node
// list is modeled by `list` (head + sentinel) so storage stays test-owned.
// ===========================================================================
void* ObjectInsertNamedNode(const char* name, void* arg, char kind,
                            NamedNodeList* list) {
    char buf[264];
    // 2-byte-stepped copy of `name` (replicates the original byte movements).
    {
        const char* s = name ? name : "";
        char* d = buf;
        size_t i = 0;
        for (;;) {
            char c0 = s[i];
            d[i] = c0;
            if (!c0) break;
            char c1 = s[i + 1];
            d[i + 1] = c1;
            if (i + 2 >= sizeof(buf)) { d[sizeof(buf) - 1] = 0; break; }
            if (!c1) break;
            i += 2;
        }
    }
    void* result = g_hooks.modelIoLoadBinaryAnimation
                       ? g_hooks.modelIoLoadBinaryAnimation(buf, arg, kind)
                       : nullptr;                        // 0x42e15a
    if (!result)                                         // 0x42e163
        return nullptr;                                  // 0x42e165
    if (list) {
        void* oldHead = list->head;                      // v8 = dword_13FC8E4
        // result[+356] = oldHead;  head = result;  result[+352] = sentinel;
        *reinterpret_cast<void**>(static_cast<char*>(result) + 356) = oldHead;
        list->head = result;
        *reinterpret_cast<void**>(static_cast<char*>(result) + 352) =
            &list->sentinel;
        if (oldHead)
            *reinterpret_cast<void**>(static_cast<char*>(oldHead) + 352) = result;
    }
    return result;                                       // 0x42e167
}

// ===========================================================================
// 0x41e49c — VIBE_Object_SetButtonCallback  (eax = fn(w@eax, cb@edx)).
//   result = 87 * *(widgetBase + 740*w + 116);    // callback-table index
//   callbackBase[result] = cb;
//   return result * 4;
// ===========================================================================
int ObjectSetButtonCallback(const WidgetTable& wt, const CallbackTable& ct,
                            int w, int cb) {
    if (!wt.base || !ct.base) return 0;
    i32 slotIdx = *reinterpret_cast<i32*>(
        wt.base + kWidgetSlotStride * w + kWidgetSlotIdxField);  // *(+116)
    int result = kCallbackStride * slotIdx;              // 87 * idx
    ct.base[result] = cb;                                // dword_695098[result] = cb
    return result * 4;                                   // 0x41e4ca
}

// ===========================================================================
// 0x5b5184 — VIBE_Object_MoveNodeCallback  (al = fn(handle@eax, arg@edx)).
//   SceneGraph_WalkAndInvoke(*arg, 0, Light_RemoveCacheEntry, 7, handle);
//   *(handle+520) = arg[1];                  // owner tag from arg[1]
//   v4 = *(*arg + 164);                        // render-node list head
//   if ( v4 != &sentinel ) {
//     do { v5 = v4[194]; if ( handle == v4[184] ) Render_FreeObjectNode(v4, handle);
//          v4 = v5; } while ( v5 != &sentinel );
//   }
//   return 1;
// `newTreeRoot` == *arg (the destination tree root), `newOwnerTag` == arg[1].
// We model the render-node list directly (head + sentinel) since it is the
// *(arg+164) chain. The light-cache flush walk goes through the walk hook (the
// callback Light_RemoveCacheEntry is unreconstructed -> inert).
// ===========================================================================
char ObjectMoveNodeCallback(int handle, int newTreeRoot, int newOwnerTag,
                            RenderNode* renderHead, RenderNode* renderSentinel) {
    if (g_hooks.sceneGraphWalkAndInvoke)
        g_hooks.sceneGraphWalkAndInvoke(newTreeRoot, 0, nullptr, 7, handle);  // 0x5b519b
    // *(handle+520) = arg[1]: the owner-tag write. handle here is the node index;
    // since the node block is not passed, the side effect is captured by the
    // caller (the original writes into the live node). We expose newOwnerTag.
    (void)newOwnerTag;
    RenderNode* v4 = renderHead;                         // *(*arg + 164)
    if (v4 != renderSentinel) {                          // 0x5b51b6
        do {
            RenderNode* v5 = v4->next;                   // v4[194]
            if (handle == v4->owner)                     // v4[184]
                if (g_hooks.renderFreeObjectNode)
                    g_hooks.renderFreeObjectNode(v4, handle);  // 0x5b51d9
            v4 = v5;
        } while (v4 != renderSentinel);                  // 0x5b51d0
    }
    return 1;                                            // 0x5b51d4
}

// ===========================================================================
// 0x43e624 — VIBE_Object_CmdShowObject  (eax = fn(this@ecx, a2@eax, a3@edx, a4@ebx)).
//   v7[0] = *a2; v7[1] = *a3; v7[2] = *a4;   memset(v8, 0, 12);
//   v4 = AttachToUniverseNode(0, v7, *this, v8);
//   if ( !v4 ) return 0;
//   Light_BuildObjectCache(v4);
//   return v4;
// a2/a3/a4 are three pointers each contributing one position component; `this`
// (ecx) holds the model pointer (*this). v8 is a zeroed 12-byte extra block.
// ===========================================================================
int ObjectCmdShowObject(const float* posA, const float* posB, const float* posC,
                        void* model) {
    float pos[4] = {0, 0, 0, 0};
    pos[0] = posA ? posA[0] : 0.0f;          // v7[0] = *a2
    pos[1] = posB ? posB[0] : 0.0f;          // v7[1] = *a3
    pos[2] = posC ? posC[0] : 0.0f;          // v7[2] = *a4
    int extra[3] = {0, 0, 0};                // v8 zeroed
    int v4 = g_hooks.attachToUniverseNode
                 ? g_hooks.attachToUniverseNode(0, pos, model,
                                                reinterpret_cast<intptr_t>(extra))
                 : 0;                        // 0x43e650
    if (!v4)                                 // 0x43e659
        return 0;                            // 0x43e65b
    if (g_hooks.lightBuildObjectCache)
        g_hooks.lightBuildObjectCache(v4);   // 0x43e661
    return v4;                               // 0x43e65d
}

// ===========================================================================
// 0x43e66c — VIBE_Object_CmdShowObjectAtDummy  (eax = fn(a1@eax, a2@edx)).
//   if ( !*a2 ) return 0;
//   Transform_PointThroughBoneChain(*a2, *a2 + 76, v7);   // dummy world pos
//   v5 = AttachToUniverseNode(0, v7, *a1, *ecx + 132);
//   if ( v5 ) Light_BuildObjectCache(v5);
//   return v5;
// `dummy` == *a2 (the dummy node); its matrix base is +0 and local pos +76.
// `extra` models the *(ecx)+132 argument (a per-call extra block offset).
// ===========================================================================
int ObjectCmdShowObjectAtDummy(SceneNode7* dummy, void* model, int extra) {
    if (!dummy)                              // 0x43e675
        return 0;                            // 0x43e67a
    float worldPos[6] = {0, 0, 0, 0, 0, 0};  // v7[6]
    const float* mtx = reinterpret_cast<const float*>(dummy->raw + 0);
    const float* localPos = reinterpret_cast<const float*>(dummy->raw + 76);
    if (g_hooks.transformPointThroughBoneChain) {
        g_hooks.transformPointThroughBoneChain(mtx, localPos, worldPos);  // 0x43e68c
    } else {
        // inert default: pass the dummy's local position straight through.
        worldPos[0] = localPos[0];
        worldPos[1] = localPos[1];
        worldPos[2] = localPos[2];
    }
    int v5 = g_hooks.attachToUniverseNode
                 ? g_hooks.attachToUniverseNode(0, worldPos, model, extra)
                 : 0;                        // 0x43e6a3
    if (v5)                                  // 0x43e6ac
        if (g_hooks.lightBuildObjectCache)
            g_hooks.lightBuildObjectCache(v5);  // 0x43e6ae
    return v5;                               // 0x43e67c
}

// ===========================================================================
// 0x43ea80 — VIBE_Object_CmdLoadScene  (eax = fn(this@ecx, a2@eax)).
//   if ( !Scene_LoadFromStream(*a2, 0, (i16)this, 0) )
//       Script_ReportError(ctx, *(ctx+152), "ecmd_LoadScene: Could not find scene on disk...");
//   return 1;
// ===========================================================================
int ObjectCmdLoadScene(const char* name, i16 target) {
    int ok = g_hooks.sceneLoadFromStream
                 ? g_hooks.sceneLoadFromStream(name, 0, target, 0)
                 : 0;                        // 0x43ea88
    if (!ok) {                               // 0x43ea90
        if (g_hooks.reportError)
            g_hooks.reportError(
                "ecmd_LoadScene: Could not find scene on disk...");  // 0x43eaa9
    }
    return 1;                                // 0x43ea97
}

// ===========================================================================
// 0x4ffe0c — VIBE_Object_BuildModelName  (eax = fn(node@eax, rec@edx, mode@bl)).
//   if ( mode ) {
//     if ( mode > 1 ) {
//       if ( mode == 2 ) {
//         Sprintf(node, "gb_%s", buildingTable + 589*(*rec) + 1);
//         *(rec+388dword) = node;                            // *(rec+97 int) back-ptr
//         if ( *firstByte == 10 ) { *(node+535) = 0; *(node+536 dword) = 0; }
//         else { v9 = *(node+533); *(node+535) = 3;
//                if ( v9 == 1 && (*(rec+90) & 1) == 0 ) Universe_RestoreObjectStates(node, 1); }
//       }
//     } else {  // mode == 1
//       Sprintf(node, "ob_%s", sceneTable + 65*(*(i16*)rec) + 1);
//       *(node+535) = 4;
//     }
//   }
//   result = ParseNameAndBind(node, node);
//   v7 = *(node+492);                                        // sub-mesh block
//   *(node+512) = rec;                                       // owner-record back-ptr
//   if ( v7 ) *v7 = HandlerEntry_DestroyIconsAndMesh;        // install destroy handler
//   return result;
// `typeName` supplies the already-resolved table-row name (table+stride*idx+1).
// `firstByteIsTen` models *(the formatted dst's first byte == 10) — in practice
// the type-name's first char; we expose it as a flag. `recFlag90` is *(rec+90),
// `recAttachKind` is *(node+533).
// ===========================================================================
int ObjectBuildModelName(SceneNode7* node, u8 /*typeIndex*/, u8 recFlag90,
                         u8 recAttachKind, u8 firstByteIsTen, u8 mode,
                         const char* typeName) {
    if (!node) return 0;
    if (mode) {                                          // 0x4ffe15
        if (mode > 1) {                                  // 0x4ffe17
            if (mode == 2) {                             // 0x4ffe62
                std::snprintf(reinterpret_cast<char*>(node->raw), 0x21C,
                              "gb_%s", typeName ? typeName : "");  // 0x4ffe8e
                if (firstByteIsTen) {                    // *(...) == 10
                    node->b(535) = 0;                    // 0x4ffec8
                    node->d(536) = 0;                    // 0x4ffecf
                } else {
                    u8 v9 = recAttachKind;               // *(node+533)
                    node->b(535) = 3;                    // 0x4ffea6
                    if (v9 == 1 && (recFlag90 & 1) == 0) {        // 0x4ffeb5
                        if (g_hooks.universeRestoreObjectStates)
                            g_hooks.universeRestoreObjectStates(node, 1u);  // 0x4ffebe
                    }
                }
            }
        } else {                                         // mode == 1
            std::snprintf(reinterpret_cast<char*>(node->raw), 0x21C,
                          "ob_%s", typeName ? typeName : "");  // 0x4ffe32
            node->b(535) = 4;                            // 0x4ffe3a
        }
    }
    int result = g_hooks.parseNameAndBind ? g_hooks.parseNameAndBind(node) : 0;  // 0x4ffe43
    // *(node+512) = rec back-ptr is a side effect on the live node; the sub-mesh
    // handler install (*(node+492) -> DestroyIconsAndMesh) is an unreconstructed
    // leaf, left to the caller / its own module. Faithful eax is the parse result.
    return result;                                       // 0x4ffe5c
}

}  // namespace guild::sim
