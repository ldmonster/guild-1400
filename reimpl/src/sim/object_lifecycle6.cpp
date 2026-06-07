// ===========================================================================
// object_lifecycle6.cpp — see object_lifecycle6.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle6.h"

#include <cstdio>
#include <cstring>

#include "sim/character_query.h"   // g_activeUniverse (off_649D64) — reused via extern

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife6Hooks g_hooks;

// VIBE_Memory_IsValidPointer with the module default policy (null => the caller
// already guards; non-null => valid unless a hook says otherwise).
inline int MemValid(void* p) {
    if (g_hooks.memoryIsValidPointer)
        return g_hooks.memoryIsValidPointer(p);
    return p != nullptr ? 1 : 0;
}

// The ValidatePointers failure path: VIBE_MemPool_ReportStackTrace(); __debugbreak();
inline void ValidationFail(void* badPtr) {
    if (g_hooks.validationFailure)
        g_hooks.validationFailure(badPtr);
    // Default: inert (no abort) so tests can count failures without crashing.
}
}  // namespace

void ObjLife6SetHooks(const ObjLife6Hooks& hooks) { g_hooks = hooks; }
void ObjLife6ResetHooks() { g_hooks = ObjLife6Hooks(); }

// ===========================================================================
// 0x43e440 — VIBE_Object_CollectMatchingHandle  (al = fn(node@eax, ctx@edx)).
// Faithful:
//   if ( NodeMatches(ctx, ctx+132) ) {       // loc_5CB930 predicate
//       v6 = ctx[128];  ctx[128] = v6 + 1;    // count++
//       ctx[v6] = node;                       // slots[count] = node
//   }
//   return ctx[128] < 32;                     // continue while < 32
// The predicate (loc_5CB930) is a node/visibility test on the COLLECTOR's own
// embedded node copy; here the walk feeds already-filtered nodes, so `matched`
// carries the predicate result. Note the original APPENDS at the PRE-increment
// index but reads count back from ctx[128] AFTER the store for the return — both
// reads see the post-increment value.
// ===========================================================================
bool ObjectCollectMatchingHandle(int node, HandleCollector* ctx, bool matched) {
    if (matched) {                               // 0x43e44f
        int v6 = ctx->count;                     // 0x43e466
        ctx->count = v6 + 1;                      // 0x43e474
        if (v6 >= 0 && v6 < 32)
            ctx->slots[v6] = node;                // 0x43e47a
    }
    return ctx->count < 32;                       // 0x43e462
}

// ===========================================================================
// 0x43e3e0 — VIBE_Object_CmdGetObjectHandle  (eax = fn(&name@eax, ...)).
//   result = FindByHandle(0, 320, name, 0, extra);
//   if (!result) { Sprintf("GetObjectHandle: Could not find object '%s'", name);
//                  ReportError(...); return 0; }
//   return result;
// ===========================================================================
int ObjectCmdGetObjectHandle(const char* name) {
    int result = g_hooks.objFindByHandle
                     ? g_hooks.objFindByHandle(0, 320, name, 0, 0)
                     : 0;                              // 0x43e3f7
    if (!result) {
        char msg[520];
        std::snprintf(msg, sizeof(msg),
                      "GetObjectHandle: Could not find object '%s'",
                      name ? name : "");
        if (g_hooks.reportError)
            g_hooks.reportError(msg);                  // 0x43e42d
        return 0;
    }
    return result;
}

// ===========================================================================
// 0x43e5c4 — VIBE_Object_CmdGetSubObjectHandle  (eax = fn(node@eax, &name@edx)).
//   result = FindByHandle(node, 320, name, 0, extra);
//   if (!result) { Sprintf("GetSubObjectHandle: Could not find object '%s'", name);
//                  ReportError(...); return <undef ecx>; }
//   return result;
// (The miss-path returns an uninitialized register in the original; we return 0.)
// ===========================================================================
int ObjectCmdGetSubObjectHandle(int parent, const char* name) {
    int result = g_hooks.objFindByHandle
                     ? g_hooks.objFindByHandle(parent, 320, name, 0, 0)
                     : 0;                              // 0x43e5da
    if (!result) {
        char msg[524];
        std::snprintf(msg, sizeof(msg),
                      "GetSubObjectHandle: Could not find object '%s'",
                      name ? name : "");
        if (g_hooks.reportError)
            g_hooks.reportError(msg);                  // 0x43e613
        return 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// The two find-random handlers share: copy name into a stack buffer (a UTF-16-ish
// 2-byte copy in the original — semantically a string copy), call the walk with
// CollectMatchingHandle, then pick slots[Rand % count] or 0. We thread the
// collector through the walk hook's ctx parameter.
// ---------------------------------------------------------------------------
namespace {
// The active collector the walk hook can reach. The original keeps it on the
// stack and passes its address as the walk ctx; we mirror that ownership here.
HandleCollector* g_walkCollector = nullptr;
}  // namespace

// Re-entrant trampoline matching the walk hook's `bool (*)(int,int)` callback
// shape: cb(node, matched) where `matched` is delivered by the walk (the test
// captor decides which nodes "match"). When no walk hook is installed nothing
// is visited (count stays 0).
extern "C" bool ObjLife6CollectTrampoline(int node, int matched) {
    return ObjectCollectMatchingHandle(node, g_walkCollector, matched != 0);
}

// ===========================================================================
// 0x43e48c — VIBE_Object_CmdFindRandomByName  (eax = fn(&name@eax, ...)).
//   SetGrayColorThunk(0, 196); copy name; collector.count = 0;
//   WalkAndInvoke(g_activeUniverse, 0, CollectMatchingHandle, 320, &collector);
//   if (count) return slots[Rand % count]; else return 0;
// ===========================================================================
int ObjectCmdFindRandomByName(const char* name) {
    HandleCollector collector;                          // stack v7[]/v8 count
    if (g_hooks.lightSetGrayColorThunk)
        g_hooks.lightSetGrayColorThunk(0, 196);          // 0x43e4a9
    char buf[256];                                       // name copy (v9)
    std::snprintf(buf, sizeof(buf), "%s", name ? name : "");

    g_walkCollector = &collector;
    if (g_hooks.sceneGraphWalkAndInvoke)                 // 0x43e4de
        g_hooks.sceneGraphWalkAndInvoke(
            &g_activeUniverse, 0, ObjLife6CollectTrampoline, 320,
            /*ctx token; the collector is reached via g_walkCollector*/ 0);
    g_walkCollector = nullptr;

    if (collector.count) {                               // 0x43e4eb
        int r = g_hooks.randNext ? g_hooks.randNext() : 0;
        return collector.slots[r % collector.count];     // 0x43e50e
    }
    return 0;                                            // 0x43e4ed
}

// ===========================================================================
// 0x43e520 — VIBE_Object_CmdFindRandomVisibleByName  (eax = fn(&root@eax, &name@edx)).
//   if (!*root) return 0;
//   if (!IsValidPointer(*root)) return <that result>;   // 0 on invalid
//   SetGrayColorThunk(0, 196); copy name; collector.count = 0;
//   WalkAndInvoke(g_activeUniverse, *root_ctx, CollectMatchingHandle, 832, &collector);
//   if (count) return slots[Rand % count]; else return 0;
// ===========================================================================
int ObjectCmdFindRandomVisibleByName(int root, const char* name) {
    if (!root)                                           // 0x43e52e
        return 0;
    int valid = MemValid(reinterpret_cast<void*>(static_cast<intptr_t>(root)));
    if (!valid)                                          // 0x43e54a
        return 0;                                         // 0x43e536 (returns the 0 result)

    HandleCollector collector;
    if (g_hooks.lightSetGrayColorThunk)
        g_hooks.lightSetGrayColorThunk(0, 196);          // 0x43e55c
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", name ? name : "");

    g_walkCollector = &collector;
    if (g_hooks.sceneGraphWalkAndInvoke)                 // 0x43e591 (flags 832, visibility-gated)
        g_hooks.sceneGraphWalkAndInvoke(
            &g_activeUniverse, root, ObjLife6CollectTrampoline, 832,
            /*ctx token; the collector is reached via g_walkCollector*/ 0);
    g_walkCollector = nullptr;

    if (collector.count) {                               // 0x43e59e
        int r = g_hooks.randNext ? g_hooks.randNext() : 0;
        return collector.slots[r % collector.count];     // 0x43e5b3
    }
    return 0;
}

// ===========================================================================
// 0x43e868 — VIBE_Object_CmdRotateObject  (eax = fn(node@eax, &x@edx,&z@ecx,&y@ebx, arg)).
//   if (*node) {
//       v7 = x * PI * (1/180);   // [esp+0] angle for axis from edx
//       v8 = y * PI * (1/180);   // ebx
//       v9 = z * PI * (1/180);   // ecx
//       SetListenerOrientation(node, arg, pos.x,pos.y,pos.z, v7,v8,v9, arg, 0);
//   } else ReportError("ECmd RotateObject: Illegal object");
//   return node;
// The original passes the CURRENT world pos (node+76/+80/+84) as the position
// triple and the new angle triple as the orientation. The deg args are register
// scalars (edx=x, ecx=z, ebx=y) — preserved here as ints; the float cast matches.
// ===========================================================================
SceneNode6* ObjectCmdRotateObject(SceneNode6* node, int xDeg, int zDeg, int yDeg,
                                  int arg) {
    if (node) {                                          // 0x43e870
        float ax = static_cast<float>(xDeg) * kObj6Pi * kObj6DegPerRadInv; // v7
        float ay = static_cast<float>(yDeg) * kObj6Pi * kObj6DegPerRadInv; // v8
        float az = static_cast<float>(zDeg) * kObj6Pi * kObj6DegPerRadInv; // v9
        if (g_hooks.sound3dSetOrientation)               // 0x43e8c1
            g_hooks.sound3dSetOrientation(node, arg,
                                          node->f(76), node->f(80), node->f(84),
                                          ax, ay, az, arg);
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECmd RotateObject: Illegal object");  // 0x43e8df
    }
    return node;                                         // 0x43e8c8
}

// ===========================================================================
// 0x43e8f0 — VIBE_Object_CmdMoveObjectRelative
//   (eax = fn(node@eax, &x@edx,&z@ecx,&y@ebx, arg)).
//   if (*node) {
//       v11 = z + node[21];  v10 = y + node[20];  v9 = x + node[19];
//       SetListenerOrientation(node, arg, v9,v10,v11, node[33],node[34],node[35], arg, 0);
//   } else ReportError("ECmd MoveObject: Illegal object");
//   return 0;
// node[19..21] = pos (+76/+80/+84); node[33..35] = angle (+132/+136/+140).
// ===========================================================================
int ObjectCmdMoveObjectRelative(SceneNode6* node, int dx, int dy, int dz,
                                int arg) {
    if (node) {                                          // 0x43e8f8
        float px = static_cast<float>(dx) + node->f(76);   // v9
        float py = static_cast<float>(dy) + node->f(80);   // v10
        float pz = static_cast<float>(dz) + node->f(84);   // v11
        if (g_hooks.sound3dSetOrientation)               // 0x43e93c
            g_hooks.sound3dSetOrientation(node, arg,
                                          px, py, pz,
                                          node->f(132), node->f(136), node->f(140),
                                          arg);
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECmd MoveObject: Illegal object");    // 0x43e95a
    }
    return 0;                                            // 0x43e943
}

// ===========================================================================
// 0x43e968 — VIBE_Object_CmdRotateObjectRelative
//   (eax = fn(node@eax, &x@edx,&z@ecx,&y@ebx, arg)).
//   if (*node) {
//       v11 = z*PI*(1/180) + node[35]; v10 = y*PI*(1/180) + node[34];
//       v9  = x*PI*(1/180) + node[33];
//       SetListenerOrientation(node, arg, node[19],node[20],node[21], v9,v10,v11, arg, 0);
//   } else ReportError("ECmd RotateObject: Illegal object");
//   return 1;
// ===========================================================================
int ObjectCmdRotateObjectRelative(SceneNode6* node, int xDeg, int zDeg, int yDeg,
                                  int arg) {
    if (node) {                                          // 0x43e970
        float ax = static_cast<float>(xDeg) * kObj6Pi * kObj6DegPerRadInv + node->f(132); // v9
        float ay = static_cast<float>(yDeg) * kObj6Pi * kObj6DegPerRadInv + node->f(136); // v10
        float az = static_cast<float>(zDeg) * kObj6Pi * kObj6DegPerRadInv + node->f(140); // v11
        if (g_hooks.sound3dSetOrientation)               // 0x43e9d8
            g_hooks.sound3dSetOrientation(node, arg,
                                          node->f(76), node->f(80), node->f(84),
                                          ax, ay, az, arg);
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECmd RotateObject: Illegal object");  // 0x43e9f9
    }
    return 1;                                            // 0x43e9e2
}

// NOTE: VIBE_Object_MoveObject (0x43e804) and VIBE_Object_KillObject (0x43e724)
// were already reconstructed in object_lifecycle3.cpp (ObjectMoveObject /
// ObjectKillObject, SceneNode3 view); they are intentionally NOT re-translated
// here to avoid an ODR clash in the unified build. The Cmd*Relative move/rotate
// handlers above are distinct (unique) entry points.

// ===========================================================================
// 0x583a70 — VIBE_Object_FindObjectById  (eax = fn(id@eax)).
//   v2 = 0;
//   while ( !*(WORD*)(table + v2) || id != *(DWORD*)(table + v2 + 2) ) {
//       v2 += 67;  if (v2 >= 548864) return 0;
//   }
//   return table + v2;
// First occupied (kind != 0) slot whose +2 id matches; else nullptr.
// ===========================================================================
ObjIdRecord* ObjectFindObjectById(ObjIdRecord* table, int id) {
    if (!table)
        return nullptr;
    for (int i = 0; i < kObjIdTableSlots; ++i) {         // v2 += 67 loop
        ObjIdRecord& rec = table[i];
        if (rec.kind != 0 && rec.id == id)               // 0x583a89
            return &rec;                                  // 0x583a9f
    }
    return nullptr;                                       // 0x583aa0
}

// ===========================================================================
// 0x44e6a4 — VIBE_Object_ValidatePointers  (eax = fn(rec@eax)).
//   if (!rec) return rec;
//   if (!IsValidPointer(rec)) FAIL(rec);
//   if (rec[13] && !IsValidPointer(rec[13])) FAIL;
//   v2 = rec[28]; if (v2 && *(v2+104) && !IsValidPointer(*(v2+104))) FAIL;
//   v3 = rec[29]; if (v3 && *(v3+104) && !IsValidPointer(*(v3+104))) FAIL;
//   r = rec[30];  if (r && !IsValidPointer(r)) FAIL;
//   v4 = rec[31]; if (v4 && *(v4+104) && !IsValidPointer(*(v4+104))) FAIL;
//   if (rec[32] && !IsValidPointer(rec[32])) FAIL;
//   return <last result>;
// FAIL == ReportStackTrace(); __debugbreak() (folded into validationFailure).
// ===========================================================================
void* ObjectValidatePointers(ValidationRecord* rec) {
    void* result = rec;                                  // v1 = result
    if (!rec)                                            // 0x44e6ae
        return result;

    if (!rec->selfValid) {                               // 0x44e75c IsValidPointer(rec)
        ValidationFail(rec);                             // 0x44e769
    }
    if (rec->mesh13 && !MemValid(rec->mesh13)) {         // 0x44e6c3 (v1[13])
        ValidationFail(rec->mesh13);
    }
    if (rec->sub28 && rec->sub28->ptr104 &&              // 0x44e6e4 (v1[28], +104)
        !MemValid(rec->sub28->ptr104)) {
        ValidationFail(rec->sub28->ptr104);
    }
    if (rec->sub29 && rec->sub29->ptr104 &&              // 0x44e705 (v1[29], +104)
        !MemValid(rec->sub29->ptr104)) {
        ValidationFail(rec->sub29->ptr104);
    }
    result = rec->ptr30;                                 // 0x44e714 (v1[30])
    if (rec->ptr30) {                                    // 0x44e719
        result = reinterpret_cast<void*>(
            static_cast<intptr_t>(MemValid(rec->ptr30)));
        if (!MemValid(rec->ptr30)) {                     // 0x44e722
            ValidationFail(rec->ptr30);
        }
    }
    if (rec->sub31) {                                    // 0x44e72f (v1[31])
        result = rec->sub31;
        if (rec->sub31->ptr104) {                        // 0x44e731
            result = reinterpret_cast<void*>(
                static_cast<intptr_t>(MemValid(rec->sub31->ptr104)));
            if (!MemValid(rec->sub31->ptr104)) {         // 0x44e743
                ValidationFail(rec->sub31->ptr104);
            }
        }
    }
    if (rec->ptr32) {                                    // 0x44e74b (v1[32])
        result = reinterpret_cast<void*>(
            static_cast<intptr_t>(MemValid(rec->ptr32)));
        if (!MemValid(rec->ptr32)) {                     // 0x44e77d
            ValidationFail(rec->ptr32);
        }
    }
    return result;                                       // 0x44e755
}

// ===========================================================================
// 0x44e844 — VIBE_Object_ValidateCallbackStub  ->  return 1.
// ===========================================================================
char ObjectValidateCallbackStub() {
    return 1;                                            // 0x44e846
}

// ---------------------------------------------------------------------------
// The scene-graph validation walk: WalkAndInvoke(g_activeUniverse, 0,
// ValidateCallbackStub, 64, 0). The callback shape differs from the collector
// trampoline (it takes no node arg in the original's cdecl thunk); we feed a
// 2-arg lambda-compatible trampoline that ignores its args and returns the stub.
// ---------------------------------------------------------------------------
namespace {
bool ValidationWalkTrampoline(int /*node*/, int /*ctx*/) {
    return ObjectValidateCallbackStub() != 0;
}
char RunSceneGraphValidationWalk() {
    if (g_hooks.sceneGraphWalkAndInvoke)
        return g_hooks.sceneGraphWalkAndInvoke(
            &g_activeUniverse, 0, ValidationWalkTrampoline, 64, 0);
    return 0;
}
}  // namespace

// ===========================================================================
// 0x44e888 — VIBE_Object_ValidateAllPointers.
//   for (i=0; i!=512; ++i) if (actors[i]) ValidatePointers(actors[i]);
//   for (j=0; j!=517120; j+=404) if (*(slot+9)) ValidatePointers(*(slot+20));
//   return WalkAndInvoke(g_activeUniverse, 0, ValidateCallbackStub, 64, 0);
// (517120/404 == 1280 building slots.)
// ===========================================================================
char ObjectValidateAllPointers(ValidationRecord* const* actors, int actorCount,
                               const bool* bldgAlive,
                               ValidationRecord* const* bldgRec, int bldgCount) {
    for (int i = 0; i < actorCount; ++i) {               // 0x44e88b
        if (actors && actors[i])                         // 0x44e88d
            ObjectValidatePointers(actors[i]);           // 0x44e8e5
    }
    for (int j = 0; j < bldgCount; ++j) {                // 0x44e8a2
        if (bldgAlive && bldgAlive[j])                   // 0x44e8ab (*(+9))
            ObjectValidatePointers(                      // 0x44e8b4 (*(+20))
                bldgRec ? bldgRec[j] : nullptr);
    }
    return RunSceneGraphValidationWalk();                // 0x44e8e1
}

// ===========================================================================
// 0x44e848 / 0x44e868 — the scene-graph-only validation walk (two identical
// originals; both emit the same WalkAndInvoke call).
// ===========================================================================
char ObjectRunValidationPass()    { return RunSceneGraphValidationWalk(); }  // 0x44e865
char ObjectRunValidationPassDup() { return RunSceneGraphValidationWalk(); }  // 0x44e885

}  // namespace guild::sim
