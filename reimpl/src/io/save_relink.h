#pragma once
// gilde.exe — guild::io  (MODULE: save-time pointer<->id relink, person/object pass)
//
// Around a save write the engine must turn live pointers into stable ids (and back
// afterwards) for two distinct data structures:
//
//   (A) the OBJECT array (dword_13CE298, stride 169, 256 slots — the "g_objects"
//       array from sim/person_record.h): each live object holds a back-pointer at
//       +97 to its live actor; the actor's +512 slot is overwritten with the
//       object's id (+1) before the write and restored to the object's address
//       afterwards.
//   (B) the 32768x10 relink table (dword_B5FB66 id @+6, byte_B5FB61 tag @+1 — the
//       same table modelled in io/save.h): each used entry's pointer is converted
//       to an id by reading a tagged member offset off the live pointer.
//
//   VIBE_Save_RelinkPersonObjects   @0x5a3d8c  pre-save: actor+512 <- object id,
//       then table pointer -> id (tag-dispatched member read).
//   VIBE_Save_RelinkPersonExtraData @0x5a3f14  brackets WorldIo_SaveSceneObjects:
//       actor+512 <- object id, save scene objects, actor+512 <- object address.
//
// These touch sim globals (the object array, the active-scene slot dword_649D60,
// VIBE_Universe_SwitchActiveSlot) and io/worldio (VIBE_WorldIo_SaveSceneObjects)
// that this slice does not own. Per the cross-module rule they are injected through
// a small view/callback bundle so the passes stay byte-faithful and testable. The
// relink-table constants are REUSED from io/save.h (not redefined).
#include "guild/common/types.h"
#include "io/save.h"   // kRelinkBytes / kRelinkStride / RelinkTag, etc. (reused)
#include <cstddef>

namespace guild::io {

// --- recovered object-array field offsets (stride 169, base dword_13CE298) ---
// Matches guild::sim::person_reconcile (kObjectScanBound 43264, stride 169). Kept
// here as local constants so io does not depend on sim headers; the values are the
// decompiled literals from @0x5a3d8c / @0x5a3f14.
constexpr int      kRelinkObjStride    = 169;     // object record stride
constexpr int      kRelinkObjScanBound = 43264;   // 169 * 256
constexpr unsigned kRelinkObjAliveOff  = 0;       // *(byte)(rec+0)  alive/type
constexpr unsigned kRelinkObjIdOff     = 1;       // *(dword)(rec+1) object id
constexpr unsigned kRelinkObjActorOff  = 97;      // *(dword)(rec+97) live-actor ptr
constexpr unsigned kRelinkActorBackOff = 512;     // actor +512 back-reference slot

// Injected view over the live object array + active-scene + scene-save machinery.
// In the binary these are direct global accesses / calls; the reimpl supplies them
// so the relink passes can run without owning sim/worldio. All pointers are opaque
// 32-bit tokens (matching the original's pointer width).
struct RelinkPersonEnv {
    // The object array: `objectBase` is the base of `objectCount` records of
    // `kRelinkObjStride` bytes (the test/host provides a flat buffer). Reads use the
    // recovered offsets above.
    guild::u8* objectBase;
    int        objectCount;        // number of records actually populated (<=256)

    // dword_649D60 active-scene slot save/restore + VIBE_Universe_SwitchActiveSlot.
    int  (*getActiveSlot)(void* ctx);                 // read dword_649D60
    void (*switchActiveSlot)(int slot, void* ctx);    // SwitchActiveSlot(slot, 1)

    // Resolve a live-actor token to its +512 write target. The original writes
    // straight to *(actor+512); the reimpl routes the write through this so actors
    // can live in a side buffer. `actorToken` is the value read from object+97.
    void (*writeActorBack)(guild::u32 actorToken, guild::u32 value, void* ctx);

    // VIBE_WorldIo_SaveSceneObjects(0, a3) — opaque scene serialize hook (ExtraData
    // only). May be null in tests.
    void (*saveSceneObjects)(int arg, void* ctx);

    // Tag-dispatched pointer->id member read for the relink table (pre-save). The
    // original dereferences the live pointer at a tag-specific offset:
    //   tag1 (Person)   id = *(ptr+4)
    //   tag2 (Building)  id = *(ptr+1)
    //   tag3 (Object)    id = *(ptr+2)
    //   tag9 (Cutscene)  id = *(ptr+0)
    // The reimpl injects one resolver per tag so the dereference target is testable.
    guild::u32 (*ptrToIdPerson)(guild::u32 ptr, void* ctx);
    guild::u32 (*ptrToIdBuilding)(guild::u32 ptr, void* ctx);
    guild::u32 (*ptrToIdObject)(guild::u32 ptr, void* ctx);
    guild::u32 (*ptrToIdCutscene)(guild::u32 ptr, void* ctx);

    void* ctx;
};

// VIBE_Save_RelinkPersonObjects @0x5a3d8c — write each live object's id into its
// actor's +512 slot (bracketed by SwitchActiveSlot(0)/SwitchActiveSlot(saved)),
// then convert every used relink-table pointer to an id via the tag resolvers.
// `relinkTable` is the 327680-byte (32768x10) blob (may be null to skip phase B).
void SaveRelinkPersonObjects(const RelinkPersonEnv& env, guild::u8* relinkTable);

// VIBE_Save_RelinkPersonExtraData @0x5a3f14 — actor+512 <- object id, call
// saveSceneObjects, then actor+512 <- object record address (the post-save restore).
// Bracketed by SwitchActiveSlot(0)/SwitchActiveSlot(saved). Returns 1 (as original).
int SaveRelinkPersonExtraData(const RelinkPersonEnv& env, int sceneArg);

} // namespace guild::io
