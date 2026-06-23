#pragma once
// character_recon4_avatar — 1:1 reconstruction of the Character avatar-slot
// *bookkeeping* cluster of gilde.exe.  These functions own the lazy allocation of
// a per-object / per-building "avatar" render-slot: they look up a slot index in a
// type-keyed table, allocate a free slot on demand, mark the slot live, and seed
// its NUL-terminated name string by copying it out of the per-type definition row.
//
// Reconstructed here (control logic + table bookkeeping, byte-faithful):
//   VIBE_Character_EnsureObjectAvatar   0x505074  (__usercall eax=arg)
//   VIBE_Character_EnsureBuildingAvatar 0x505134  (__usercall eax,edi,cx)
//   VIBE_Character_EnsureGateAvatars    0x505870  (__usercall edi,esi)
//   VIBE_Character_DestroyThunk         0x43cb20  (__usercall eax)
//   VIBE_Character_CmdPreloadSitMeshStub 0x43d9f0 (stub, returns 0)
//
// The genuine engine leaves these reach (scene loaders, the free-slot allocator,
// the person iterator, character destroy) are NOT yet reconstructed; they are
// routed through AvatarHooks below as INERT-DEFAULT hooks.  The slot-table layout,
// the sign-extended high-byte slot encoding, the 984-byte avatar-record stride,
// the per-type name-source strides (65 for objects, 589 for buildings), and the
// 2-bytes-at-a-time NUL-terminated copy loop are reproduced exactly from the
// Hex-Rays / disasm reference of record.
//
// Provenance is per-function below.  No third-party tech is introduced (rule 6 N/A).
#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;

// ===========================================================================
// Slot-table layout (exact, from disasm of 0x505074 / 0x505134)
// ---------------------------------------------------------------------------
// The original keeps two parallel "type -> avatar slot" tables.  In each the
// stored slot index is the *high byte* of the dword read at a +1/+0 biased
// address and then arithmetic-shifted right by 24 (`sar 0x18`).  A free entry
// reads back as -1 (0xFF in the high byte).  We model the table as a flat byte
// array and reproduce the read as a signed-8 fetch of that high byte, and the
// write as a single byte store — behaviour-identical to the original encoding.
//
// Object table  (dword_122DDAC @0x122DDAC):
//   read : slot = (i8) byteTable[type + 4]        ; (dword_122DDAC+1)[type] >>24
//   write: byteTable[type + 4] = (u8) slot         ; via byte_122DDB0 @0x122DDB0
//          (0x122DDB0 == 0x122DDAC + 4, the same high-byte cell)
//   name : src = objNames + 65*type + 1            ; dword_13CE27C @0x13CE27C
//
// Building table (dword_122DD5D @0x122DD5D):
//   read : slot = (i8) byteTable[type + 3]         ; dword_122DD5D[type] >>24
//   write: byteTable[type + 3] = (u8) slot
//   name : src = bldNames + 589*type + 1           ; dword_13CE294 @0x13CE294
//
// Avatar record array (byte_13ECEC8 @0x13ECEC8) — 984-byte stride per slot; the
// live flag lives in a parallel array (byte_13ED29D @0x13ED29D) at the same
// 984*slot stride.  EnsureObjectAvatar/EnsureBuildingAvatar set the live flag to
// 1 and copy the type's name string into byte_13ECEC8[984*slot].
// ===========================================================================
constexpr int kAvatarRecordStride = 984;  // 0x3D8 — byte_13ECEC8 / byte_13ED29D
constexpr int kObjectNameStride   = 65;   // dword_13CE27C row stride
constexpr int kBuildingNameStride = 589;  // dword_13CE294 row stride
constexpr int kObjectSlotHiByte   = 4;    // (dword_122DDAC+1)[type] high byte
constexpr int kBuildingSlotHiByte = 3;    // dword_122DD5D[type] high byte

// Table extents — recovered from VIBE_Object_DestroySpawnedEntities @0x4fff10,
// the routine that linearly resets both slot tables (disasm-verified):
//   object slot table : `cmp ecx, 2DBh` (0x4fff59)  -> 731 entries (types 0..730)
//   building slot table: `cmp ecx, 48h`  (0x4fff8a)  ->  72 entries (types 0..71)
// Both tables are reset to -1 (0FFh) per-entry (mov dl,0FFh / mov bl,0FFh), which
// is also their live default, so an un-allocated slot reads back as -1.  Neither
// EnsureObjectAvatar nor EnsureBuildingAvatar bounds-check the type — the bound is
// implicit in the table sizing; we size to the exact extents the binary uses.
constexpr int kObjectTypeCount   = 731;   // 0x2DB — object slot/name table rows
constexpr int kBuildingTypeCount = 72;    // 0x48  — building slot/name table rows

// Inert-default state for the avatar-slot subsystem.  Byte-faithful: the slot
// tables and the avatar/live arrays carry their exact strides.  Native engine
// pointers (scene records etc.) are NOT modelled — they belong to not-yet-
// reconstructed subsystems and are reached only via the hooks.
struct AvatarSlotState {
    // Per-type slot tables, sized to the binary's exact extents (731 object types,
    // 72 building types).  Accessed at the biased high-byte cell so the sign-
    // extended -1 default round-trips.  The original read forms a *dword* pointer
    // at (base + type [+1 for objects]) and shifts right by 24, so for the maximum
    // type the read touches the cell at base+type+hiByte (the high byte of that
    // dword).  We pad past the last high-byte cell so that dword read stays in
    // bounds, then init the whole region to 0xFF so an un-allocated slot reads
    // back as -1 — matching the binary's per-entry reset to -1 (0FFh).
    static constexpr int kObjectTableBytes   = kObjectTypeCount   + kObjectSlotHiByte;   // last hi-byte cell + room
    static constexpr int kBuildingTableBytes = kBuildingTypeCount + kBuildingSlotHiByte;
    u8 objectTable[kObjectTableBytes]     = {};  // dword_122DDAC region (731 types)
    u8 buildingTable[kBuildingTableBytes] = {};  // dword_122DD5D region (72 types)

    // Avatar live flags / records, one 984-byte row per slot.
    // Slot count = 64: FindFreeSlot @0x4266f4 caps at `v1 >= 62976` with stride
    // 984 (62976 / 984 == 64).
    static constexpr int kSlotCount = 64;
    u8 avatarLive[kSlotCount * kAvatarRecordStride] = {};   // byte_13ED29D
    u8 avatarRec [kSlotCount * kAvatarRecordStride] = {};   // byte_13ECEC8

    // Per-type name source rows (objects: 65 stride x731, buildings: 589 stride x72).
    // The original reads (row_base + 1), i.e. a 1-byte header precedes the name.
    u8 objectNames  [kObjectTypeCount   * kObjectNameStride]   = {};  // dword_13CE27C
    u8 buildingNames[kBuildingTypeCount * kBuildingNameStride] = {};  // dword_13CE294

    AvatarSlotState() {
        for (int i = 0; i < kObjectTableBytes;   ++i) objectTable[i]   = 0xFF;
        for (int i = 0; i < kBuildingTableBytes; ++i) buildingTable[i] = 0xFF;
    }
};

// Inert-default hooks for the engine leaves the avatar cluster calls.  The
// faithful control logic lives in the reconstructed functions; only the
// subsystem effects are stubbed.  Defaults reproduce the "nothing happened"
// branch so the bookkeeping path is exercised verbatim.
struct AvatarHooks {
    // VIBE_Character_FindFreeSlot 0x4266f4 — allocate a render slot, -1 on full.
    int (*findFreeSlot)(void* ctx) = nullptr;
    // VIBE_Scene_LoadObjektScene 0x500270 — load the object scene for a type.
    void (*loadObjektScene)(void* ctx, int type) = nullptr;
    // VIBE_Scene_LoadGebaeudeScene 0x5006a8 — load the building scene for a type.
    void (*loadGebaeudeScene)(void* ctx, int type, int a2, i16 a3) = nullptr;
    // VIBE_Person_QueryBegin 0x586c20 — begin a person iterator; returns a record
    // pointer (here a building/object record on which EnsureBuildingAvatar runs).
    void* (*personQueryBegin)(void* ctx, void* rec, int a2, int a3, int a4) = nullptr;
    // VIBE_Character_Destroy 0x402120 — destroy the character referenced by *p.
    int (*characterDestroy)(int handle) = nullptr;
    void* ctx = nullptr;

    // The two "special" object/building type pointers that force a scene load
    // instead of slot allocation (dword_63174C / dword_631748).  Modelled as
    // type values; -1 means "no special type" (default → never matches).
    int specialObjectType = -1;     // *arg == specialObjectType (== dword_63174C)
    int specialBuildingType = -1;   // arg == dword_631748
};

// ---------------------------------------------------------------------------
// gilde.exe 0x505074 — VIBE_Character_EnsureObjectAvatar
//   __usercall eax = ensure(__int16 *objType)
// Look up the avatar slot for object-type *objType.  If already assigned
// (!= -1) return it.  If this is the special object type, load its scene and
// return whatever the table then holds.  Otherwise allocate a free slot, mark
// it live, copy the type's name into the avatar record, and return the slot.
// `objType` points at a 16-bit type value (read sign-extended).
// ---------------------------------------------------------------------------
int EnsureObjectAvatar(AvatarSlotState& st, AvatarHooks& H, i16 objType);

// ---------------------------------------------------------------------------
// gilde.exe 0x505134 — VIBE_Character_EnsureBuildingAvatar
//   __usercall eax = ensure(char *bldType, int a2@<edi>, __int16 a3@<cx>)
// As above but for buildings.  Special-cases the special building type and
// type 30 (0x1E) to force a scene load; building name stride is 589.
// `bldType` points at an 8-bit type value (read sign-extended).
// ---------------------------------------------------------------------------
int EnsureBuildingAvatar(AvatarSlotState& st, AvatarHooks& H, i8 bldType, int a2, i16 a3);

// ---------------------------------------------------------------------------
// gilde.exe 0x505870 — VIBE_Character_EnsureGateAvatars
//   __usercall eax = gate(int a1@<edi>, int a2@<esi>)
// Begin a person query (filter 1/5/11) for the gate at universe a2; if it
// resolves a building record, ensure its building avatar.  Returns the avatar
// slot (cast to pointer in the original) or null when the query is empty.
// Returns -1 sentinel here when the query yields no record (matches null path).
// ---------------------------------------------------------------------------
int EnsureGateAvatars(AvatarSlotState& st, AvatarHooks& H, int a1, int a2);

// ---------------------------------------------------------------------------
// gilde.exe 0x43cb20 — VIBE_Character_DestroyThunk
//   __usercall eax = thunk(int *p)  ->  VIBE_Character_Destroy(*p)
// Trivial one-liner: dereference and forward to Character_Destroy.
// ---------------------------------------------------------------------------
int DestroyThunk(AvatarHooks& H, const int* p);

// ---------------------------------------------------------------------------
// gilde.exe 0x43d9f0 — VIBE_Character_CmdPreloadSitMeshStub
// Compiled-out command handler: returns 0 unconditionally.
// ---------------------------------------------------------------------------
int CmdPreloadSitMeshStub();

} // namespace guild::sim
