// Reconstruction of the deferred building / Bauplatz cluster.  See
// buildingtype_recon.h for the provenance / strategy summary.
//
// Translated 1:1 from the Hex-Rays decompile of gilde.exe:
//   VIBE_Building_RegisterNames        0x504a54
//   VIBE_Building_AllocStorageRoom     0x588988
//   VIBE_Bauplatz_GetSize              0x577628
//   VIBE_Bauplatz_MapOneToSupermap     0x5774b8
//   VIBE_Util_StrCmp                   0x5d3f10   (helper, byte-faithful)
//   VIBE_Util_StrCmpNoCase             0x5cb8f0   (helper, byte-faithful)
#include "sim/buildingtype_recon.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// String comparators (byte-faithful).
// ===========================================================================

// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp.  The original is a 4-byte-at-a-time
// unrolled strcmp; its observable result is identical to a plain byte strcmp that
// returns the signed difference of the first differing byte (0 on equality). We
// translate to that equivalent byte loop (provably behavior-identical: the dword
// fast path only short-circuits to 0 when both operands hit the same NUL inside
// the word, and otherwise falls through to the same per-byte unsigned compare /
// signed-difference epilogue).
int Util_StrCmp(const char* a, const char* b) {
    if (a == b) return 0;                  // 0x5d3f16 early-out
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);
    while (*p == *q) {
        if (*p == 0) return 0;
        ++p; ++q;
    }
    return static_cast<int>(*p) - static_cast<int>(*q);
}

// gilde.exe 0x5cb8f0 — VIBE_Util_StrCmpNoCase.  Folds 'A'..'Z' to lowercase by
// adding 32, compares, returns (foldedA - foldedB).  Verbatim.
int Util_StrCmpNoCase(const char* a, const char* b) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);
    unsigned char v3, v4;
    while (true) {
        v3 = *p;
        v4 = *q;
        if (v3 >= 0x41u && v3 <= 0x5Au) v3 += 32;   // 0x5cb8fd
        if (v4 >= 0x41u && v4 <= 0x5Au) v4 += 32;   // 0x5cb909
        if (v3 != v4 || !v4) break;                 // 0x5cb914
        ++p; ++q;
    }
    return static_cast<int>(v3) - static_cast<int>(v4);   // 0x5cb929
}

// ===========================================================================
// Bauplatz size table + lookup (VIBE_Bauplatz_GetSize @0x577628).
// ===========================================================================
namespace {
const BauplatzSizeRec* g_bauplatzBase = nullptr;   // dword_1234600
int                    g_bauplatzCount = 0;        // dword_1234604

IBauplatzMapHooks  g_defaultMapHooks;
IBauplatzMapHooks* g_mapHooks = &g_defaultMapHooks;

IStorageRoomHooks  g_defaultStorageHooks;
IStorageRoomHooks* g_storageHooks = &g_defaultStorageHooks;
}  // namespace

void SetBauplatzTable(const BauplatzSizeRec* base, int count) {
    g_bauplatzBase = base;
    g_bauplatzCount = count;
}
const BauplatzSizeRec* BauplatzTableBase() { return g_bauplatzBase; }
int BauplatzTableCount() { return g_bauplatzCount; }

void SetBauplatzMapHooks(IBauplatzMapHooks* hooks) {
    g_mapHooks = hooks ? hooks : &g_defaultMapHooks;
}
IBauplatzMapHooks* BauplatzMapHooks() { return g_mapHooks; }

void SetStorageRoomHooks(IStorageRoomHooks* hooks) {
    g_storageHooks = hooks ? hooks : &g_defaultStorageHooks;
}
IStorageRoomHooks* StorageRoomHooks() { return g_storageHooks; }

// gilde.exe 0x577628 — VIBE_Bauplatz_GetSize.
//   v3 = 0;
//   if ( dword_1234604 <= 0 ) miss;
//   v4 = 0;
//   while ( StrCmpNoCase(name, base + v4) ) { ++v3; v4 += 96; if (v3 >= count) miss; }
//   return base + v4;
const BauplatzSizeRec* Bauplatz_GetSize(const char* name) {
    if (g_bauplatzCount <= 0 || g_bauplatzBase == nullptr)   // 0x577638 / null base
        return nullptr;
    int v3 = 0;
    const char* rec = reinterpret_cast<const char*>(g_bauplatzBase);
    while (Util_StrCmpNoCase(name, rec) != 0) {              // 0x577651
        ++v3;                                                // 0x577659
        rec += kBauplatzRecordStride;                        // v4 += 96  (0x57765a)
        if (v3 >= g_bauplatzCount)                           // 0x57765f
            return nullptr;
    }
    return reinterpret_cast<const BauplatzSizeRec*>(rec);     // 0x577684
}

// gilde.exe 0x5774b8 — VIBE_Bauplatz_MapOneToSupermap.
// Reads the five geometry dwords, builds the four corners in the original's exact
// pick order, projects each through WorldToTile into the contiguous v9..v16 float
// pairs, then rasterizes with fill 255.
int Bauplatz_MapOneToSupermap(int mapCtx, const char* name) {
    const BauplatzSizeRec* rec = Bauplatz_GetSize(name);     // 0x5774c4
    if (!rec)                                                // 0x5774d?
        return 0;   // original returns the sprintf scratch ptr; "unknown" == 0 here

    IBauplatzMapHooks* h = g_mapHooks;

    // The decompiler exposes a scratch dword[3] (v17,v18,v19) reloaded before each
    // WorldToTile call.  Field picks (Size[16]/[17]/[18]/[20]/[22]):
    //   v9/v10  <- (cornerXlo, cornerY, cornerZlo)
    //   v11/v12 <- (cornerXhi, cornerY, cornerZlo)
    //   v13/v14 <- (cornerXhi, cornerY, cornerZhi)
    //   v15/v16 <- (cornerXlo, cornerY, cornerZhi)
    float quad[8];   // v9..v16 packed pairs (u0,v0, u1,v1, u2,v2, u3,v3)
    i32 world[3];

    world[0] = rec->cornerXlo; world[1] = rec->cornerY; world[2] = rec->cornerZlo;
    h->WorldToTile(&quad[0], &quad[1], world, mapCtx);       // 0x57750f

    world[0] = rec->cornerXhi; world[1] = rec->cornerY; world[2] = rec->cornerZlo;
    h->WorldToTile(&quad[2], &quad[3], world, mapCtx);       // 0x577538

    world[0] = rec->cornerXhi; world[1] = rec->cornerY; world[2] = rec->cornerZhi;
    h->WorldToTile(&quad[4], &quad[5], world, mapCtx);       // 0x577561

    world[0] = rec->cornerXlo; world[1] = rec->cornerY; world[2] = rec->cornerZhi;
    h->WorldToTile(&quad[6], &quad[7], world, mapCtx);       // 0x57758a

    return h->RasterizeBauplatzEdge(mapCtx, quad, 255);      // 0x5774e2
}

// ===========================================================================
// VIBE_Building_AllocStorageRoom  (0x588988) — security clamp + flow.
// ===========================================================================

// Pure clamp, recovered from 0x588a92.. (storage / kind 42, floor 2) and
// 0x588aea.. (market-stand / group 6, floor 6):
//
//   if ( b573 || b574 || (single = b575) == 0 ) {
//       field28 = (b573 >= F) ? b573 : F;
//       field29 = (b574 >= F) ? b574 : F;
//   } else {
//       // single branch:
//       //   storage node:   field28 = single                  (no extra clamp)
//       //   market node:    field28 = (single >= 6) ? single : 6
//       field28 = marketSingle ? ((single >= F) ? single : F) : single;
//       field29 unchanged;
//   }
StorageSecurityOut Building_StorageSecurityClamp(u8 b573, u8 b574, u8 b575,
                                                 u8 floorVal, bool marketSingle) {
    StorageSecurityOut out{};
    if (b573 != 0 || b574 != 0 || b575 == 0) {   // 0x588a92 / 0x588afe
        out.field28 = (b573 >= floorVal) ? b573 : floorVal;
        out.field29 = (b574 >= floorVal) ? b574 : floorVal;
        out.wroteField29 = true;
    } else {
        u8 single = b575;
        if (marketSingle)
            out.field28 = (single >= floorVal) ? single : floorVal;  // 0x588b14
        else
            out.field28 = single;                                    // 0x588ab8 (storage)
        out.field29 = 0;
        out.wroteField29 = false;
    }
    return out;
}

void* Building_AllocStorageRoom(const ObjectRec* building, i16 protoType,
                                void* parentNode) {
    IStorageRoomHooks* h = g_storageHooks;

    if (!building || !h->CanAllocate())          // 0x5889b2 guard
        return nullptr;

    // v5 = AddObjekt(building->id, protoType, 1, parent)   (0x5889da)
    void* obj = h->AddObjekt(building->id, protoType, 1, parentNode);

    // v28 = 589 * (*building) + dword_13CE294 — the building TYPE record.  The
    // type index is the building's +0 byte (ObjectRec::alive doubles as the type
    // byte in the live record, per building_types.h).  The +573/+574/+575 gate
    // bytes fall inside BuildingTypeDef::pad565 (offsets +565..+582), so we read
    // them by RAW byte offset into the 589-byte record to stay byte-faithful.
    const BuildingTypeDef* td = h->TypeRecordFor(building->alive /*type byte @+0*/);
    const u8* rec = reinterpret_cast<const u8*>(td);

    // The original next walks the type record's +35 room-slot list to spawn the
    // room sub-objects via AddObjekt (0x588a20..0x588c25) and decide whether to
    // recurse for each slot.  That enumeration is scene-graph state the rules core
    // does not own; it is driven through the installed AddObjekt/QueryFind hooks.

    if (rec) {
        const u8 b573 = rec[573];
        const u8 b574 = rec[574];
        const u8 b575 = rec[575];

        // --- storage sub-node (kind 42), floor 2  (0x588a6d..0x588abf) ---
        if (u8* node = h->QueryFind(obj, /*kind word*/42, /*group*/0)) {
            // *(node+3) = building->id; *(node+7) = 2 set by the original on the
            // node's id/state fields (scene-graph fields owned by the hook).
            StorageSecurityOut s =
                Building_StorageSecurityClamp(b573, b574, b575, /*floor*/2,
                                              /*marketSingle*/false);
            node[28] = s.field28;
            if (s.wroteField29)
                node[29] = s.field29;
        }

        // --- market-stand storage sub-node (kind 278, group 6), floor 6
        //     (0x588aca..0x588b34) ---
        if (u8* node = h->QueryFind(obj, /*kind word*/278, /*group*/6)) {
            StorageSecurityOut s =
                Building_StorageSecurityClamp(b573, b574, b575, /*floor*/6,
                                              /*marketSingle*/true);
            node[28] = s.field28;
            if (s.wroteField29)
                node[29] = s.field29;
        }
    }

    // The market-stand transport branch (type 146..151 at 0x588b36) spawns scene
    // transport objects (AddObjekt loop) and sets a label sub-node's bytes — all
    // scene-graph state routed through the hooks; no rules-core arithmetic here.
    return obj;   // 0x5889b6 returns v5
}

// ===========================================================================
// VIBE_Building_RegisterNames  (0x504a54) — candidate pick.
// ===========================================================================
//
// gilde.exe 0x504bc6..0x504c5b inner loop, recovered:
//   v30 = 0 (survivor count); for each of 12 templates -> candidate name v28:
//     if candidate empty -> skip;
//     scan object name table; if a live object's name matches (StrCmp==0) -> in use
//       -> NOT collected;
//     else if strlen(name) >= 0x20 -> "too long" -> NOT collected (logged);
//     else -> append to survivor list (v25), ++v30.
//   if v30: pick = RandomModulo(v30); chosen = survivor[pick];
//     if strlen(chosen) >= 0x20 -> log & skip; else store into person+5.
int Building_PickName(const char* const* candidates, int count,
                      INameRegistry& reg) {
    // Build the survivor list (indices into `candidates`) in original order.
    // Max 12 templates in the original; we honor the caller's `count`.
    int survivors[64];
    int survivorCount = 0;
    for (int i = 0; i < count; ++i) {
        const char* name = candidates[i];
        if (!name || name[0] == 0)            // 0x504b3c "if ( *v11 )"
            continue;
        if (reg.IsNameTaken(name))            // 0x504b83 StrCmp match -> v15 < 256
            continue;
        if (std::strlen(name) >= 0x20)        // 0x504bb8 length guard
            continue;
        if (survivorCount < 64)               // (original caps at 12 via v31)
            survivors[survivorCount] = i;
        // HARDENING (wave-11): the original's survivor array holds at most 12
        // templates; ours is 64. Never let survivorCount outrun what was actually
        // stored in survivors[] — otherwise an oversized `count` makes
        // RandomModulo(survivorCount) (and survivors[pick]) index OOB. Faithful
        // inputs (count <= 12) never reach 64, so this is a no-op for valid data.
        if (survivorCount < 64)
            ++survivorCount;                  // ++v30
    }
    if (survivorCount == 0)
        return -1;                            // no name stored (person+5 unchanged)

    int pick = reg.RandomModulo(survivorCount);  // 0x504c23 RandomModulo(v30)
    if (pick < 0) pick = 0;
    if (pick >= survivorCount) pick = survivorCount - 1;

    int chosen = survivors[pick];
    // 0x504c2e: if the chosen name is too long it is rejected at store time.
    if (std::strlen(candidates[chosen]) >= 0x20)
        return -1;
    return chosen;
}

}  // namespace guild::sim
