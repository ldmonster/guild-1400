#pragma once
// Building / building-type / Bauplatz (plot) reconstruction for gilde.exe.
// MODULE: buildings (namespace guild::sim).  UNIQUE FILE: buildingtype_recon.*
//
// This unit translates four functions from the building cluster that the central
// buildings module (src/sim/building.cpp) had explicitly DEFERRED as
// "render / supermap / io / scene-graph coupled" (see building.cpp's deferral
// list at lines 241-252):
//
//   VIBE_Building_RegisterNames        0x504a54  (random building-name picker)
//   VIBE_Building_AllocStorageRoom     0x588988  (storage-room sub-object alloc)
//   VIBE_Bauplatz_GetSize              0x577628  (plot name -> size record)
//   VIBE_Bauplatz_MapOneToSupermap     0x5774b8  (plot quad -> supermap raster)
//
// 1:1 STRATEGY (CLAUDE.md rules 1, 3-5, 8):
//   The PURE / TABLE / arithmetic logic of each function is translated verbatim
//   (control flow, constants, clamp math, table strides, signed/unsigned exactly
//   as the binary).  The leaves that touch live engine state the rules core does
//   not own here (person iteration, scene-graph object allocation/lookup, the
//   3D bone-chain transform + supermap rasterizer, and the disk-loaded global
//   tables) are routed through INERT-DEFAULT hooks so the translated logic can be
//   exercised headless and wired to the real engine by installing real hooks.
//   No engine subsystem is faked or approximated — the hooks are pure injection
//   points; the math/flow above them is byte-faithful.
//
// The size-3 epilogue thunks 0x589a32..0x589a44 (VIBE_BuildingType_ReturnCodeNN)
// are NOT reconstructed here: they are `mov al,N; ret` jumptable case targets for
// VIBE_BuildingType_MapToProfessionCode and are ALREADY folded into the switch
// tables in src/sim/building_type.cpp (size < 12, per the SKIP rule).
//
// ODR: none of the four symbols are defined anywhere else in src/** (verified by
// grep) — building.cpp only LISTS them as deferred.  This file uses the existing
// guild::sim types (sim/types.h ObjectRec, sim/building_types.h BuildingTypeDef)
// without redefining them.
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ===========================================================================
// Bauplatz (building-plot) size record  (gilde.exe dword_1234600 array)
// ===========================================================================
// VIBE_Bauplatz_GetSize scans an array of (name, geometry) records of stride 96
// bytes (0x60).  dword_1234600 is the array base, dword_1234604 the entry count.
// The lookup compares the requested name (case-insensitive) against the record's
// leading name string and returns the matching record pointer (or 0).
//
// VIBE_Bauplatz_MapOneToSupermap then reads five DWORD-indexed geometry fields of
// that record to assemble the plot's four world-space corners:
//   [16] (+64)  cornerX_lo   |  [20] (+80)  cornerX_hi
//   [17] (+68)  cornerY      (shared Y for all four corners)
//   [18] (+72)  cornerZ_lo   |  [22] (+88)  cornerZ_hi
// The four corners (in the original's exact field-pick order) are:
//   c0 = ([16],[17],[18])   c1 = ([20],[17],[18])
//   c2 = ([20],[17],[22])   c3 = ([16],[17],[22])
// i.e. a rectangle in the X/Z plane at height [17], wound lo->hiX->hiX/hiZ->hiZ.
constexpr int kBauplatzRecordStride = 96;   // 0x60

GUILD_PACKED_BEGIN
struct BauplatzSizeRec {
    char name[64];   // +0x00  plot name (NUL-terminated; case-insensitive key)
    i32  cornerXlo;  // +0x40 (dword[16]) min-X world coord
    i32  cornerY;    // +0x44 (dword[17]) shared world Y (height)
    i32  cornerZlo;  // +0x48 (dword[18]) min-Z world coord
    i32  pad19;      // +0x4C (dword[19]) (unused by the mapper)
    i32  cornerXhi;  // +0x50 (dword[20]) max-X world coord
    i32  pad21;      // +0x54 (dword[21]) (unused by the mapper)
    i32  cornerZhi;  // +0x58 (dword[22]) max-Z world coord
    i32  pad23;      // +0x5C (dword[23]) (pads record out to 96 bytes)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(BauplatzSizeRec) == kBauplatzRecordStride,
              "BauplatzSizeRec stride must be 96");

// ---------------------------------------------------------------------------
// Injected Bauplatz size table (models dword_1234600 / dword_1234604).
// Real game loads this from disk; tests/engine seed it here.
// ---------------------------------------------------------------------------
void SetBauplatzTable(const BauplatzSizeRec* base, int count);
const BauplatzSizeRec* BauplatzTableBase();
int BauplatzTableCount();

// gilde.exe 0x577628 — VIBE_Bauplatz_GetSize  (__usercall, eax=(name@eax,key@ecx))
// Linear case-insensitive scan over the size table.  Returns the matching record
// or nullptr (the original also logs "rd_GetBKSize(): Unbekannter Bauplatz '%s'"
// via sprintf into a scratch buffer on miss — a pure formatting side effect with
// no observable state change; omitted as it produces no return value difference).
const BauplatzSizeRec* Bauplatz_GetSize(const char* name);

// ---------------------------------------------------------------------------
// Supermap mapping hooks (VIBE_Coord_WorldToTile @0x577690 +
// VIBE_Map_RasterizeBauplatzEdge @0x5776d8).
// WorldToTile projects a world point (3 floats) through the active map's
// bone-chain pivot to fractional tile coords; Rasterize stamps the plot edge into
// the supermap.  Both are render/heightmap-coupled (rule 3) — injected here.
// ---------------------------------------------------------------------------
struct IBauplatzMapHooks {
    virtual ~IBauplatzMapHooks() = default;
    // VIBE_Coord_WorldToTile(out_u@eax, out_v@edx, world@ebx, mapCtx@ecx):
    // writes the fractional tile coords of `world` into out_u/out_v.
    virtual void WorldToTile(float* outU, float* outV,
                             const i32 world[3], int mapCtx) {
        (void)world; (void)mapCtx;
        if (outU) *outU = 0.0f;
        if (outV) *outV = 0.0f;
    }
    // VIBE_Map_RasterizeBauplatzEdge(mapCtx@eax, quad@..., fill): stamp the
    // 4-corner (8-float) quad into the supermap with `fill`.  Returns a status int.
    virtual int RasterizeBauplatzEdge(int mapCtx, const float quad[8], int fill) {
        (void)mapCtx; (void)quad; (void)fill;
        return 0;
    }
};
void SetBauplatzMapHooks(IBauplatzMapHooks* hooks);
IBauplatzMapHooks* BauplatzMapHooks();

// gilde.exe 0x5774b8 — VIBE_Bauplatz_MapOneToSupermap (__usercall,
//   eax=(mapCtx@eax, name@edx, key@ecx)).
// Resolves the plot's size record, assembles its four world corners, projects
// each through WorldToTile, then rasterizes the resulting tile-space quad with
// fill code 255.  Returns 0 when the plot name is unknown (the original returns
// the sprintf result of its error message there — a nonzero scratch-buffer ptr in
// the binary; we return 0 to denote "unknown plot, nothing rasterized").
int Bauplatz_MapOneToSupermap(int mapCtx, const char* name);

// ===========================================================================
// VIBE_Building_AllocStorageRoom  (gilde.exe 0x588988)
// ===========================================================================
// Allocates a storage-room scene object for a building and seeds its security /
// staffing levels from the building TYPE record (dword_13CE294, stride 589).
// The pure logic recovered here is the SECURITY-LEVEL ASSIGNMENT for the two
// query-found sub-nodes (storage @kind 42 and market-stand storage @278/group 6):
//
//   The type record's bytes +573/+574/+575 (call them lo/hi/single) gate it:
//     * if lo==0 && hi==0 && single!=0   -> use `single` (clamped >= floor) [42]
//                                           or single (clamped >= 6)        [278]
//     * else                             -> field28 = max(lo, floor),
//                                           field29 = max(hi, floor)
//   where the per-node floor is 2 for the storage node (kind 42) and 6 for the
//   market-stand storage node (group 6 / 278).
//
// These clamps are the rules-core arithmetic; the scene-graph allocation
// (AddObjekt/QueryFind), person iteration and transport spawning are hooks.
// We expose the clamp math as a directly-testable pure helper plus the full
// AllocStorageRoom flow wired through the hooks.

// Pure helper: the storage/market security-level clamp the original performs
// inline at 0x588a92.. and 0x588aea.. .  Given the type record's gate bytes
// (b573,b574,b575) and the node floor F, writes the two output bytes
// (field+28, field+29).  Returns true if both fields were written (the
// "lo|hi|!single" branch), false if only field+28 was written from `single`.
//   Storage node  (kind 42):   F = 2,  both fields can be written.
//   Market node   (group 6):   F = 6;  in the "single" branch ONLY field+28 is
//                              written (= max(single, 6)); pass marketSingle=true.
struct StorageSecurityOut {
    u8 field28;
    u8 field29;
    bool wroteField29;   // false in the market-stand "single" branch
};
StorageSecurityOut Building_StorageSecurityClamp(u8 b573, u8 b574, u8 b575,
                                                 u8 floorVal, bool marketSingle);

// ---------------------------------------------------------------------------
// AllocStorageRoom engine hooks.  All scene-graph / object-array touching leaves
// are injected; the type-record read + clamp math runs verbatim above them.
// ---------------------------------------------------------------------------
struct IStorageRoomHooks {
    virtual ~IStorageRoomHooks() = default;
    // dword_13CE27C != 0 && dword_13CE294 != 0 && dword_6498C0 + 64 < 0x2000
    // is the original's "can allocate" guard.  Default: allow.
    virtual bool CanAllocate() { return true; }
    // 589 * typeIndex + dword_13CE294 — pointer to the building TYPE record.
    // Default: nullptr (no table) -> AllocStorageRoom returns the new object as-is.
    virtual const BuildingTypeDef* TypeRecordFor(u8 typeIndex) {
        (void)typeIndex; return nullptr;
    }
    // VIBE_GameObject_AddObjekt(parentId, protoType, flag, parentNode):
    // create a scene object; returns an opaque non-null handle (0 == fail).
    virtual void* AddObjekt(i32 parentId, i16 protoType, int flag, void* parent) {
        (void)parentId; (void)protoType; (void)flag; (void)parent;
        return nullptr;
    }
    // VIBE_GameObject_QueryFind(root, ...): find a sub-node by selector.
    // `kind`==the type word (slot-1 arg); `group`==the optional group (slot-3).
    // Returns the node's writable byte buffer (>= 30 bytes) or nullptr.
    virtual u8* QueryFind(void* root, int kind, int group) {
        (void)root; (void)kind; (void)group;
        return nullptr;
    }
};
void SetStorageRoomHooks(IStorageRoomHooks* hooks);
IStorageRoomHooks* StorageRoomHooks();

// gilde.exe 0x588988 — VIBE_Building_AllocStorageRoom (__usercall,
//   eax=(building@eax /*ObjectRec: type@+0, id@+1*/, protoType@dx, parent@ebp)).
// Returns the new storage object handle (from AddObjekt), or nullptr if the
// "can allocate" guard fails.  The two security-clamp passes run against the
// building's type record; the market-stand (type 146..151) transport-spawn branch
// is left to the hooks (no observable state in the rules core).
void* Building_AllocStorageRoom(const ObjectRec* building, i16 protoType,
                                void* parentNode);

// ===========================================================================
// VIBE_Building_RegisterNames  (gilde.exe 0x504a54)
// ===========================================================================
// For each "Meister" person of the active player, picks a building name: it
// builds a pool of up-to-12 candidate names from the building's name-template
// table, drops any name already in use by an existing object (case-sensitive
// VIBE_Util_StrCmp against the object array's name field @+5), keeps only names
// shorter than 0x20 bytes, then stores a uniformly-random surviving candidate
// into the person record's name field (person+5).
//
// PURE LOGIC recovered: the candidate de-dup + length filter + uniform-random
// pick (VIBE_Math_RandomModulo).  Person iteration and the global name tables
// (byte_620EFC default template, dword_8C4790 per-type templates, the object name
// table) are hooks/inputs.
//
// We expose the inner candidate-selection as a directly-testable pure helper.

// Pure helper: given a list of candidate names and a predicate "is this name
// already taken by an existing object", returns the chosen name index into
// `candidates`, or -1 if none survive.  Mirrors 0x504bc6..0x504c41:
//   * a candidate is REJECTED if it is already taken (StrCmp == match).
//   * a candidate is REJECTED if strlen(name) >= 0x20.
//   * among survivors, pick index = RandomModulo(survivorCount) over the
//     survivors in their original order.
// `randModulo(n)` must return VIBE_Math_RandomModulo(n) (== RandNext()%n, 0 if n==0).
struct INameRegistry {
    virtual ~INameRegistry() = default;
    // Case-sensitive "is this exact name already used by a live object?"
    // (the original scans the 169-stride object name table @dword_13CE298+5 with
    //  VIBE_Util_StrCmp).  Default: nothing is taken.
    virtual bool IsNameTaken(const char* name) { (void)name; return false; }
    // VIBE_Math_RandomModulo(n): RandNext() % n, or 0 if n == 0.
    virtual int RandomModulo(int n) { (void)n; return 0; }
};

// Returns the chosen candidate index, or -1 if no candidate survives the
// taken/length filters.  `count` candidates, each a NUL-terminated C string.
int Building_PickName(const char* const* candidates, int count,
                      INameRegistry& reg);

// Byte-faithful VIBE_Util_StrCmp (0x5d3f10) and VIBE_Util_StrCmpNoCase
// (0x5cb8f0) reimplementations used by the name logic.  Exposed for testing the
// exact comparison semantics the originals use.
int Util_StrCmp(const char* a, const char* b);
int Util_StrCmpNoCase(const char* a, const char* b);

}  // namespace guild::sim
