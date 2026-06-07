#pragma once
// ===========================================================================
// object_lifecycle6.{h,cpp} — VIBE_Object_* SCRIPT-COMMAND entry points +
// pointer-VALIDATION batch (batch 6). namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family. Batches
// 1..5 covered the gameplay record, the simple flag/transform setters, the
// find/door/type classifiers, the alloc/link teardown leaves and the
// geometry/bone-transform/transparency/suspend group. This batch covers the
// script-facing Object_Cmd* command handlers (find / get-handle / move /
// rotate / kill), the object-id table lookup, and the heap-pointer VALIDATION
// pass:
//
//   * VIBE_Object_CmdGetObjectHandle        (0x43e3e0) — find a named object on
//     the active scene graph; report a script error when missing.
//   * VIBE_Object_CmdGetSubObjectHandle     (0x43e5c4) — same, rooted under a
//     supplied parent node.
//   * VIBE_Object_CollectMatchingHandle     (0x43e440) — SceneGraph_WalkAndInvoke
//     callback that appends matching node handles into a 32-slot collector.
//   * VIBE_Object_CmdFindRandomByName       (0x43e48c) — walk + collect by name,
//     return a random match (or 0).
//   * VIBE_Object_CmdFindRandomVisibleByName (0x43e520) — same, visibility-gated,
//     guarded by a valid-pointer check on the root.
//   * VIBE_Object_CmdRotateObject           (0x43e868) — set absolute euler angle
//     (deg->rad) and re-emit via the 3D listener-orientation leaf.
//   * VIBE_Object_CmdMoveObjectRelative     (0x43e8f0) — translate by a relative
//     delta added to the node's current world pos.
//   * VIBE_Object_CmdRotateObjectRelative   (0x43e968) — rotate by a relative
//     euler delta (deg->rad) added to the node's current angle.
//   * VIBE_Object_FindObjectById            (0x583a70) — linear scan of the
//     object-id table (stride 67, 8192 slots) by id.
//   * VIBE_Object_ValidatePointers          (0x44e6a4) — assert that each owned
//     heap pointer of one live-actor record is a valid pool pointer.
//   * VIBE_Object_ValidateAllPointers       (0x44e888) — run ValidatePointers
//     over every live actor + every building slot, then walk the scene graph.
//   * VIBE_Object_RunValidationPass         (0x44e848) — the scene-graph-only
//     validation walk.
//   * VIBE_Object_RunValidationPassDup      (0x44e868) — byte-identical sibling.
//   * VIBE_Object_ValidateCallbackStub      (0x44e844) — `return 1;` walk cb.
//
// Reuses g_activeUniverse (owned by character_query.cpp) via extern as the
// scene-graph walk root (the original's off_649D64 == &g_activeUniverse).
//
// All unreconstructed cross-module leaves (SceneGraph_WalkAndInvoke, FindByHandle,
// AttachToUniverseNode-not-here, Sound3d_SetListenerOrientation, Memory_*,
// Crt_Sprintf, Script_ReportError, Util_RandNext, DetachAndRelease,
// Light_SetGrayColorThunk) are routed through ObjLife6Hooks with inert defaults
// defined in THIS library .cpp. Tests install captor hooks.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x43e3e0  VIBE_Object_CmdGetObjectHandle
//   0x43e5c4  VIBE_Object_CmdGetSubObjectHandle
//   0x43e440  VIBE_Object_CollectMatchingHandle
//   0x43e48c  VIBE_Object_CmdFindRandomByName
//   0x43e520  VIBE_Object_CmdFindRandomVisibleByName
//   0x43e868  VIBE_Object_CmdRotateObject
//   0x43e8f0  VIBE_Object_CmdMoveObjectRelative
//   0x43e968  VIBE_Object_CmdRotateObjectRelative
//   0x583a70  VIBE_Object_FindObjectById
//   0x44e6a4  VIBE_Object_ValidatePointers
//   0x44e888  VIBE_Object_ValidateAllPointers
//   0x44e848  VIBE_Object_RunValidationPass
//   0x44e868  VIBE_Object_RunValidationPassDup
//   0x44e844  VIBE_Object_ValidateCallbackStub
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// SceneNode6 — flat byte view of the "d3:SpawnObject" scene-graph node, for
// the fields the command handlers read. Identical layout to SceneObject/Node5;
// re-declared here (named only where this batch touches) to avoid an ODR clash
// with those translation units' views.
//   +0x00  (0)    name[64]
//   +0x4C  (76)   pos[3]    (a1[19..21]) world translation
//   +0x84  (132)  angle[3]  (a1[33..35]) euler angle (rad)
// ===========================================================================
struct SceneNode6 {
    u8 raw[0x21C];

    float& f(int byteOff) { return *reinterpret_cast<float*>(raw + byteOff); }
    i32&   d(int byteOff) { return *reinterpret_cast<i32*>(raw + byteOff); }
    char*  str(int byteOff) { return reinterpret_cast<char*>(raw + byteOff); }

    char*  name()  { return reinterpret_cast<char*>(raw); }
    float* pos()   { return reinterpret_cast<float*>(raw + 76); }
    float* angle() { return reinterpret_cast<float*>(raw + 132); }

    void reset() { for (auto& x : raw) x = 0; }
    SceneNode6() { reset(); }
};

// ===========================================================================
// ValidationRecord — the owned-pointer slots of a live-actor record that
// VIBE_Object_ValidatePointers checks. The original reads them as int* fields:
//   v1[13]  (+52)  meshHandle        — checked directly
//   v1[28]  (+112) ; *(+104) of it   — sub-record whose +104 pointer is checked
//   v1[29]  (+116) ; *(+104) of it
//   v1[30]  (+120)                    — checked directly
//   v1[31]  (+124) ; *(+104) of it
//   v1[32]  (+128)                    — checked directly
// We model the four directly-checked pointers + the three "+104 sub-record"
// pointers as a small POD; null members are skipped exactly as the original.
// ===========================================================================
struct ValidationSub {
    void* ptr104 = nullptr;   // the +104 field of the sub-record
};
struct ValidationRecord {
    bool           selfValid = true;     // models VIBE_Memory_IsValidPointer(self)
    void*          mesh13 = nullptr;     // v1[13] (+52)
    ValidationSub* sub28 = nullptr;      // v1[28] (+112)
    ValidationSub* sub29 = nullptr;      // v1[29] (+116)
    void*          ptr30 = nullptr;      // v1[30] (+120)
    ValidationSub* sub31 = nullptr;      // v1[31] (+124)
    void*          ptr32 = nullptr;      // v1[32] (+128)
};

// ===========================================================================
// Object-id table view (gilde.exe dword_13CE290, stride 67, 8192 slots).
// FindObjectById scans for the first slot with a non-zero +0 word whose +2
// dword id matches. We model it as an array of records sized to the faithful
// scan bound (548864 / 67 == 8192).
// ===========================================================================
constexpr int kObjIdTableStride = 67;            // bytes per record
constexpr int kObjIdTableBound  = 548864;        // byte scan limit
constexpr int kObjIdTableSlots  = kObjIdTableBound / kObjIdTableStride;  // 8192
struct ObjIdRecord {
    u16 kind;   // +0  (non-zero => occupied)
    u8  pad[2];
    i32 id;     // +2  (4-byte aligned in our POD; matches the +2 dword read)
};

// ===========================================================================
// Recovered rodata: degrees->radians scale, identical to object_lifecycle2's
// kPi / kDegPerRadInv (flt_6173F4/flt_617478 == PI, flt_6173F8/flt_61747C == 1/180).
// Module-local copies (object_lifecycle2.h's are in this same namespace; we use
// distinct names to avoid an ODR clash on the constexpr globals).
// ===========================================================================
constexpr float kObj6Pi           = 3.1415927410125732f;     // flt_6173F4 / flt_617478
constexpr float kObj6DegPerRadInv = 0.0055555556900799274f;  // flt_6173F8 / flt_61747C

// ===========================================================================
// Mockable hooks for the unreconstructed leaves the originals call. nullptr =>
// inert (the field arithmetic / collection logic still runs faithfully).
// ===========================================================================
struct ObjLife6Hooks {
    // VIBE_SceneGraph_WalkAndInvoke(root, arg2, callback, flags, ctx). The
    // command handlers use it to drive CollectMatchingHandle; the captor lets a
    // test inject the set of nodes the walk would visit. Returns a char (the
    // original's bool-ish result). Default: inert (walks nothing), returns 0.
    char (*sceneGraphWalkAndInvoke)(void* root, int arg2,
                                    bool (*cb)(int, int),
                                    int flags, int ctx) = nullptr;

    // VIBE_Object_FindByHandle(root, flags, namePtr, z, extra) -> node handle (0
    // when not found). The original's `namePtr` is a name-string pointer; we pass
    // it as `const char*` so it survives on LP64 (an `int` slot would truncate a
    // 64-bit pointer). Default: 0.
    int (*objFindByHandle)(int root, int flags, const char* name, int z,
                           int extra) = nullptr;

    // VIBE_Sound3d_SetListenerOrientation(node, arg, px,py,pz, ax,ay,az, arg9, 0).
    // The move/rotate handlers re-emit the node transform through this leaf. The
    // captor records the 6 floats. Default: noop.
    void (*sound3dSetOrientation)(SceneNode6* node, int arg,
                                  float px, float py, float pz,
                                  float ax, float ay, float az,
                                  int arg9) = nullptr;

    // VIBE_Script_ReportError(ctx, fmt, msg) — the "Illegal/Not-found" diagnostic.
    void (*reportError)(const char* msg) = nullptr;

    // VIBE_Crt_Sprintf_0(dst, fmt, ...) — only used to format error strings; we
    // pass the already-formatted message text to reportError instead, so this is
    // not separately hooked.

    // VIBE_Util_RandNext() -> int. Default: 0 (picks the first match).
    int (*randNext)() = nullptr;

    // VIBE_Memory_IsValidPointer(p) -> non-zero when valid. Used by
    // ValidatePointers and CmdFindRandomVisibleByName. Default: treat all
    // non-null pointers as valid (return 1).
    int (*memoryIsValidPointer)(void* p) = nullptr;

    // VIBE_MemPool_ReportStackTrace() + __debugbreak() — the failure path of
    // ValidatePointers. We fold both into one hook (the abort callback). Default:
    // noop (so a test can observe the failure count without crashing).
    void (*validationFailure)(void* badPtr) = nullptr;

    // VIBE_Light_SetGrayColorThunk(a, b) — the find handlers call it before the
    // name copy (a UI side effect). Default: noop.
    void (*lightSetGrayColorThunk)(int a, int b) = nullptr;
};
void ObjLife6SetHooks(const ObjLife6Hooks& hooks);
void ObjLife6ResetHooks();

// The 32-slot handle collector that CollectMatchingHandle appends into and the
// find handlers read. The original lays it out as a stack buffer: an int[32]
// slot array followed by an int count at +128 (i.e. ctx[32]). We model it as a
// struct so the callback can be tested directly.
struct HandleCollector {
    int  slots[32] = {0};
    int  count = 0;     // ctx[32] / +128
};

// ===========================================================================
// Public faithful entry points. See the .cpp for the 1:1 control flow.
// ===========================================================================

// 0x43e440 — SceneGraph_WalkAndInvoke callback. `node` is the visited handle,
// `ctx` is the HandleCollector. Predicate VIBE_SceneGraph_NodeMatches (loc_5CB930)
// is modeled by `matches` (the walk feeds already-filtered nodes => default true
// path is exercised via the collector). Appends node when matched + room. Returns
// (count < 32): the walk continues while true.
bool ObjectCollectMatchingHandle(int node, HandleCollector* ctx, bool matched);

// 0x43e3e0 — find a named object on the active scene graph (root = g_activeUniverse).
// Returns the found handle, or 0 (reporting a script error) when missing.
int ObjectCmdGetObjectHandle(const char* name);

// 0x43e5c4 — same, rooted under `parent`. Returns the handle, or 0 on miss.
int ObjectCmdGetSubObjectHandle(int parent, const char* name);

// 0x43e48c — collect by name across the scene graph, return a random match (or 0).
int ObjectCmdFindRandomByName(const char* name);

// 0x43e520 — visibility-gated random-by-name. `root` is the root node handle:
// when null or not a valid pointer, returns 0 without walking.
int ObjectCmdFindRandomVisibleByName(int root, const char* name);

// 0x43e868 — set absolute euler angle (degrees in dx/cx/bx -> radians) and re-emit
// the node's transform through the 3D orientation leaf. Returns the node handle.
SceneNode6* ObjectCmdRotateObject(SceneNode6* node, int xDeg, int zDeg, int yDeg,
                                  int arg);

// 0x43e8f0 — translate by a relative delta (dx,bx,cx) added to the node's current
// world pos (a1[19..21]). Returns 0.
int ObjectCmdMoveObjectRelative(SceneNode6* node, int dx, int dy, int dz, int arg);

// 0x43e968 — rotate by a relative euler delta (deg->rad) added to the node's
// current angle (a1[33..35]). Returns 1.
int ObjectCmdRotateObjectRelative(SceneNode6* node, int xDeg, int zDeg, int yDeg,
                                  int arg);

// 0x583a70 — linear scan of the object-id table for the first occupied slot
// whose id matches `id`. Returns the record (or nullptr).
ObjIdRecord* ObjectFindObjectById(ObjIdRecord* table, int id);

// 0x44e6a4 — validate one live-actor record's owned heap pointers. Calls
// validationFailure(badPtr) for each invalid pointer encountered (in source
// order). Returns the last pointer the original would return (faithful eax).
void* ObjectValidatePointers(ValidationRecord* rec);

// 0x44e888 — validate every live actor + every building slot, then walk the
// scene graph with the stub callback. Returns the walk result.
//   `actors` : kLive6Capacity ptr array (null slots skipped).
//   `buildings` : building slots; `bldgAlive[i]` models *(+9) and `bldgRec[i]`
//   models the *(+20) record pointer to validate.
char ObjectValidateAllPointers(ValidationRecord* const* actors, int actorCount,
                               const bool* bldgAlive,
                               ValidationRecord* const* bldgRec, int bldgCount);

// 0x44e848 / 0x44e868 — the scene-graph-only validation walk (two byte-identical
// originals).
char ObjectRunValidationPass();
char ObjectRunValidationPassDup();

// 0x44e844 — the validation walk callback. Always returns 1 (true => continue).
char ObjectValidateCallbackStub();

constexpr int kLive6Capacity = 512;   // dword_66F0D0 (validation actor array)

}  // namespace guild::sim
