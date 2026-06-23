// Reconstruction of the four REAL CALLERS of the buildingtype_recon cluster.
// See buildingtype_callers.h for the provenance / routing summary.
//
// Translated 1:1 from the Hex-Rays decompile of gilde.exe:
//   VIBE_Scene_SyncMeisterBuildings   0x504ce0
//   VIBE_Building_RegisterNames       0x504a54  (full flow; pick = Building_PickName)
//   VIBE_Building_CreateGebaeude      0x586fb8  (full flow)
//   VIBE_Command_ExSellObjekt         0x496b90  (the two storage-node phases)
//   VIBE_Bauplatz_MapAllToSupermap    0x577464
#include "sim/buildingtype_callers.h"

#include <cstring>

#include "render/light.h"              // BroadcastGrayDword   (0x5c6af0)
#include "sim/building.h"              // BuildingTypeDefAt / g_buildingTypes[Loaded]
#include "sim/building2.h"             // BuildingArrays().objectTypeBase (13CE27C)
#include "sim/building4.h"             // Building4_RemoveStorageRoom / _EnsureDefaultObjects
#include "sim/building5.h"             // Building_FilterBlockedBauplatze (0x50c8ec)
#include "sim/building_create.h"       // g_buildingNextId / SetCreateGebaeudeHook
#include "sim/building_create2.h"      // Building_PickUniqueName / _ApplyTypeDefaults
#include "sim/building_type.h"         // Building_MapKindToCategory / _IsProductionKind
#include "sim/buildingtype_recon.h"    // Building_AllocStorageRoom / _PickName / MapOne
#include "sim/command_apply.h"         // g_lastTradeId (dword_631290)
#include "sim/entity.h"                // g_persons / g_objects / queries
#include "sim/person_personnel2.h"     // PersonSyncMasterShopObjects (0x594c94)
#include "sim/universe.h"              // UniverseSwitchActiveSlot (0x5b4a24)
#include "util/math_random.h"          // RandomModulo (0x58b89c)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Raw record accessors (the originals address everything by byte offset off an
// unaligned base; memcpy keeps that byte-faithful and portable).
// ---------------------------------------------------------------------------
namespace {
inline i32 rd32(const u8* p, int off) { i32 v; std::memcpy(&v, p + off, 4); return v; }
inline i16 rd16(const u8* p, int off) { i16 v; std::memcpy(&v, p + off, 2); return v; }
inline void wr32(u8* p, int off, i32 v) { std::memcpy(p + off, &v, 4); }
inline void wr16(u8* p, int off, u16 v) { std::memcpy(p + off, &v, 2); }

// *(BYTE*)(dword_13CE27C + 65 * proto) — the 65-stride object type table byte.
// Null base (table not loaded) reads 0; negative protos are clamped to 0 for
// memory safety (the binary would read out of bounds — never happens live).
inline u8 ObjectTypeByte(i16 proto) {
    const u8* base = BuildingArrays().objectTypeBase;
    if (!base || proto < 0)
        return 0;
    return base[65 * static_cast<i32>(proto)];
}

// The unrolled 2-byte copy loops at 0x504a8e/0x504af1/0x58713d/0x58729b etc.
// are behavior-identical to strcpy (copy through the NUL).
inline void CopyName(u8* dst, const char* src) {
    std::strcpy(reinterpret_cast<char*>(dst), src ? src : "");
}
}  // namespace

// ===========================================================================
// Module-state mirrors.
// ===========================================================================
namespace {
u16 g_sceneSyncFlags = 0;                 // word_63C740 mirror

Person* g_meisterAnchorA = nullptr;       // dword_6498E8
Person* g_meisterShopList[769] = {};      // dword_6498EC ([0] = anchorB, [1..] shops)
int     g_meisterShopCount = 0;

u8 g_lastGebProtLatch = 0;                // byte_6498D7
u8 g_lastGebSlotLatch = 0;                // byte_6498D8

BuildingCallerHooks  g_defaultCallerHooks;
BuildingCallerHooks* g_callerHooks = &g_defaultCallerHooks;
}  // namespace

void SetSceneSyncFlagsWord(u16 w) { g_sceneSyncFlags = w; }
u16  SceneSyncFlagsWord() { return g_sceneSyncFlags; }

Person* MeisterAnchorA() { return g_meisterAnchorA; }
Person* MeisterAnchorB() { return g_meisterShopList[0]; }
Person* MeisterShopAt(int i) {
    return (i >= 0 && i < g_meisterShopCount) ? g_meisterShopList[1 + i] : nullptr;
}
int MeisterShopCount() { return g_meisterShopCount; }

u8 LastGebProtLatch() { return g_lastGebProtLatch; }
u8 LastGebSlotLatch() { return g_lastGebSlotLatch; }

void SetBuildingCallerHooks(BuildingCallerHooks* hooks) {
    g_callerHooks = hooks ? hooks : &g_defaultCallerHooks;
}
BuildingCallerHooks* GetBuildingCallerHooks() { return g_callerHooks; }

// ===========================================================================
// Hook defaults — REAL reconstructions where they exist, inert otherwise.
// ===========================================================================
// 0x504cf7: VIBE_Universe_SwitchActiveSlot(0, 0, ..) -> the real universe.cpp
// reconstruction (eax=slot 0, dl=quiet 0).
void BuildingCallerHooks::SwitchActiveSlot() { UniverseSwitchActiveSlot(0, false); }

// 0x504db6: VIBE_Building_RecalcAllProduction() -> the real building_production
// reconstruction; its lifted clock args come from the hook's nowDay/nowMinute.
void BuildingCallerHooks::RecalcAllProduction() {
    Building_RecalcAllProduction(nowDay, nowMinute);
}

// byte_13CD6A0[756*slot] — the multi-city table is disk-loaded; inert: no city.
bool BuildingCallerHooks::CityAlive(int slot) { (void)slot; return false; }

// 0x583c74 VIBE_Building_ComputeSlotStats — UNRECONSTRUCTED leaf (named gap).
void BuildingCallerHooks::ComputeSlotStats(int slot) { (void)slot; }

// 0x504db5 tail: VIBE_Person_SyncMasterShopObjects(anchorA@esi).  The original
// passes the record POINTER as the opaque query slot arg; the reimpl port of
// that token is the record's g_persons index (0 when null).
void BuildingCallerHooks::SyncMasterShopObjects(Person* anchorA) {
    int slotArg = 0;
    if (anchorA)
        slotArg = static_cast<int>(anchorA - g_persons);
    PersonSyncMasterShopObjects(slotArg);
}

// 0x504a6e: VIBE_Person_QueryBegin(node, 1, 6) — one filter, op 6 (match-any),
// over the OBJECT array.  Routed to the real entity.cpp query.
ObjectRec* BuildingCallerHooks::MeisterQueryBegin() {
    PersonFilter f{6, 0};
    return PersonQueryBegin(&f, 1);
}
ObjectRec* BuildingCallerHooks::MeisterIterNext() { return PersonIterNext(); }

// byte_620EFC / dword_8C4788 / dword_8C4790 / unk_62661C — runtime/language-
// loaded name tables (all-zero in the cold image).  Inert providers.
const char* BuildingCallerHooks::DefaultShopName() { return ""; }
const char* BuildingCallerHooks::NameTemplate(u8 type, int i) {
    (void)type; (void)i; return nullptr;
}
const char* BuildingCallerHooks::PrimaryName(u8 type) { (void)type; return nullptr; }
const char* BuildingCallerHooks::DefaultBuildingName() { return ""; }

// 0x585af4 VIBE_GameObject_AddObjekt — UNRECONSTRUCTED scene-graph leaf.
u8* BuildingCallerHooks::AddObjekt(i32 parentId, i16 proto, i32 count,
                                   void* parentNode) {
    (void)parentId; (void)proto; (void)count; (void)parentNode;
    return nullptr;
}

// 0x58736e: QueryFind(*(rec+93), 1, 4, 2) + IterNext past type-253 nodes ->
// the real entity.cpp scene query (typedef-byte filter == 2).  The original
// roots the walk at the building's +93 container handle; the reimpl query
// walks the modeled scene array (startIndex -1 == array base).
i32 BuildingCallerHooks::QueryStorageNodeTypeWord(u8* rec) {
    (void)rec;
    SceneFilter f{4, 2};
    SceneNode* n = GameObjectQueryFind(-1, &f, 1);
    if (!n)
        return -1;
    while (n->type == 253) {                  // 0x587382 skip loop
        n = GameObjectIterNext();             // 0x5874ae
        if (!n)
            return -1;                        // LABEL_44
    }
    return n->type;
}

// 0x5877ac VIBE_Building_FindStorableObject — building4 owns the decision core
// but its handles are hook-domain; the found node's type word needs live scene
// memory.  Inert: none found (named gap).
i32 BuildingCallerHooks::StorableObjectTypeWord(u8* rec) { (void)rec; return -1; }

// 0x586ed8 VIBE_Building_InitWorkerCapacities — building4 owns the 1:1 clamp
// core (Building4_InitWorkerCapacities) but it takes the two RESOLVED worker
// node records, which only a live scene query yields.  Inert (named gap).
void BuildingCallerHooks::InitWorkerCapacities(u8* rec) { (void)rec; }

// qword_13CE852 — the live clock image.  Inert: zeroed record.
PackedTime BuildingCallerHooks::Now() { return PackedTime{}; }

// 0x49739b: VIBE_Building_RemoveStorageRoom(srcBuilding, proto, destOwner) ->
// the real building4 reconstruction (child-list/slot args are Building4Hooks-
// domain handles; 0 is the inert handle — the host's hooks resolve them).
i32 BuildingCallerHooks::RemoveStorageRoom(u8* srcBuildingRec, i16 proto,
                                           const u8* destOwnerRec) {
    (void)srcBuildingRec; (void)destOwnerRec;
    return Building4_RemoveStorageRoom(0, proto, 0);
}

// 0x49738b: VIBE_GameObject_RemoveByProt(childList, proto, destOwner) -> the
// Building4Hooks surface (inert hooks: no-op status 0).
i32 BuildingCallerHooks::RemoveByProt(u8* srcChildList, i16 proto,
                                      const u8* destOwnerRec) {
    (void)srcChildList; (void)destOwnerRec;
    return Building4HooksGet()->GameObjectRemoveByProt(0, proto, 0);
}

// ===========================================================================
// Building_IsNameTaken169 — the shared 169-stride name-clash scan
// (0x504b65 in RegisterNames, 0x5871b4 in CreateGebaeude; identical code).
// ===========================================================================
bool Building_IsNameTaken169(const char* name) {
    const u8* base = reinterpret_cast<const u8*>(g_objects);   // dword_13CE298
    int n = 0;                                                 // v15 / v24
    for (int k = 0; k < 43264; k += 169) {
        if (base[k] != 0 &&
            Util_StrCmp(name, reinterpret_cast<const char*>(base + k + 5)) == 0)
            break;                                             // clash -> stop early
        ++n;
    }
    return n < 256;                                            // broke early == taken
}

// ===========================================================================
// gilde.exe 0x504a54 — VIBE_Building_RegisterNames (full flow).
// ===========================================================================
namespace {
// INameRegistry adapter binding the recon pick core to the REAL leaves:
// IsNameTaken -> the 169-stride scan; RandomModulo -> util::RandomModulo with
// the original's (unsigned __int8) count cast (0x504c1d).
struct NameRegistry169 : INameRegistry {
    bool IsNameTaken(const char* name) override {
        return Building_IsNameTaken169(name);
    }
    int RandomModulo(int n) override {
        return util::RandomModulo(static_cast<u8>(n));
    }
};
}  // namespace

void Building_RegisterNames() {
    BuildingCallerHooks* h = g_callerHooks;

    // ---- pass 1 (0x504a6e..0x504aa9): stamp the default template into the
    // name field (+5) of every queried record whose type kind byte != 28.
    // (The original reads *(dword_13CE294 + 589 * *i) unguarded; the accessor
    // returns null when the table is unloaded — kind reads as 0 there.)
    for (ObjectRec* i = h->MeisterQueryBegin(); i; i = h->MeisterIterNext()) {
        const BuildingTypeDef* td = BuildingTypeDefAt(i->alive);
        u8 kind = td ? *reinterpret_cast<const u8*>(td) : 0;
        if (kind != 28)
            CopyName(reinterpret_cast<u8*>(i) + 5, h->DefaultShopName());
    }

    // ---- pass 2 (0x504ab4..0x504c5e): per record, re-stamp the default, build
    // the 12 candidates, run the pick core, store the survivor.
    for (ObjectRec* j = h->MeisterQueryBegin(); j; j = h->MeisterIterNext()) {
        const BuildingTypeDef* td = BuildingTypeDefAt(j->alive);
        u8 kind = td ? *reinterpret_cast<const u8*>(td) : 0;
        if (kind == 28)
            continue;

        u8* nameField = reinterpret_cast<u8*>(j) + 5;
        CopyName(nameField, h->DefaultShopName());             // 0x504af1

        // dword_8C4790[14 * *j + i], i in 0..11 (0x504b34).
        const char* cand[12];
        for (int i = 0; i < 12; ++i) {
            const char* s = h->NameTemplate(j->alive, i);
            cand[i] = s ? s : "";                              // empty == skipped
        }

        // 0x504b17..0x504c3a — the recon pick core (empty/taken/len>=0x20
        // filters + uniform RandomModulo over the survivors).
        NameRegistry169 reg;
        int pick = Building_PickName(cand, 12, reg);
        if (pick >= 0)
            CopyName(nameField, cand[pick]);                   // 0x504c41
        // pick < 0: no survivor -> name keeps the default template.  (The
        // chosen-too-long ErrorLog branch @0x504cc2 is dead — survivors are
        // already length-filtered at collection.)
    }
}

// ===========================================================================
// gilde.exe 0x504ce0 — VIBE_Scene_SyncMeisterBuildings (full flow).
// ===========================================================================
void Scene_SyncMeisterBuildings() {
    BuildingCallerHooks* h = g_callerHooks;

    g_meisterAnchorA = nullptr;                    // dword_6498E8 = 0 (0x504ceb)
    g_meisterShopList[0] = nullptr;                // dword_6498EC[0] = 0 (0x504cf1)

    h->SwitchActiveSlot();                         // 0x504cf7

    if ((g_sceneSyncFlags & 0x40) == 0) {          // 0x504d11
        h->RecalcAllProduction();                  // 0x504db6
        if (h->CityAlive(0)) {                     // byte_13CD6A0[0] (0x504dc7)
            int v11 = 0;
            do {                                   // 0x504dcf..0x504de4
                h->ComputeSlotStats(v11);
                ++v11;
            } while (v11 < 4 && h->CityAlive(v11));
        }
    }

    // ---- first scan (0x504d25..0x504df5): pick the two kind-12 anchors by
    // sub-state byte (+12) — overwrite semantics, early-stop once both found.
    Person* v3 = g_meisterShopList[0];             // == 0
    Person* v4 = g_meisterAnchorA;                 // == 0
    int v5 = 0;
    {
        u8* base = reinterpret_cast<u8*>(g_persons);   // word_12CE910
        int i = 0;
        do {
            u8* rec = base + kPersonStride * i;
            if (rec[2] == 12) {                    // byte_12CE912[..] == 12
                u8 v7 = rec[12];                   // HIBYTE(dword_12CE919[..])
                if (v7 == 0) {
                    v4 = reinterpret_cast<Person*>(rec);
                    v5 |= 1;                       // 0x504d45/0x504d47
                } else if (v7 == 1) {
                    v3 = reinterpret_cast<Person*>(rec);
                    v5 |= 2;                       // 0x504e09/0x504e0b
                }
            }
            ++i;                                   // v6 += 268 (words)
        } while (i < kPersonCapacity && v5 != 3);  // v6 < 205824 && v5 != 3
    }

    // ---- second scan (0x504d5a..0x504d83): collect every kind-11 record into
    // the shop list, slots [1..] (the original pre-increments the index).
    {
        u8* base = reinterpret_cast<u8*>(g_persons);
        int v9 = 0;
        for (int i = 0; i < kPersonCapacity; ++i) {
            u8* rec = base + kPersonStride * i;
            if (rec[2] == 11)                      // byte_12CE912[..] == 11
                g_meisterShopList[++v9] = reinterpret_cast<Person*>(rec);
        }
        g_meisterShopCount = v9;
    }

    g_meisterAnchorA = v4;                         // dword_6498E8 (0x504d8b)
    g_meisterShopList[0] = v3;                     // dword_6498EC[0] (0x504d91)

    if ((g_sceneSyncFlags & 0x40) == 0 && (g_sceneSyncFlags & 4) == 0)
        Building_RegisterNames();                  // 0x504da1 (the WIRE)

    h->SyncMasterShopObjects(v4);                  // 0x504db5 (tail call)
}

// ===========================================================================
// gilde.exe 0x586fb8 — VIBE_Building_CreateGebaeude (full flow).
// ===========================================================================
u8* Building_CreateGebaeudeFlow(u8 prot, u16 ownerWord) {
    BuildingCallerHooks* h = g_callerHooks;

    if (!g_buildingTypesLoaded)                    // dword_13CE294 == 0 (0x586fcf)
        return nullptr;

    // 0x586fe4 VIBE_Building_FindFreeSlot — 1:1 scan of the 169-stride array.
    u8* rec = nullptr;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (g_objects[i].alive == 0) {
            rec = reinterpret_cast<u8*>(&g_objects[i]);
            break;
        }
    }
    if (!rec)
        return nullptr;                            // 0x586ff8 -> return result(0)

    bool v52 = false;                              // var_28 = 0 ("2/6 slot seen")
    // var_1C: edx = dword_13CE294 + 589 * prot (recovered at 0x58701f..0x587025).
    const u8* typeRec = reinterpret_cast<const u8*>(&g_buildingTypes[prot]);

    rec[0] = prot;                                 // *v3 = prot (0x58702e)
    // (0x587045 sprintf "----->GebID= %i GebProt= %i" — scratch-only side effect.)

    // ---- fixed scalar field block (every store recovered from the disasm) ----
    wr16(rec, 41, 0);                              // [esi+29h] word = 0
    rec[47] = 0;                                   // [esi+2Fh] byte = 0
    wr32(rec, 57, 32000);                          // [esi+39h] = 0x7D00
    wr32(rec, 61, 100);                            // [esi+3Dh] = 100
    wr32(rec, 65, 2);                              // [esi+41h] = 2
    wr32(rec, 69, 100);                            // [esi+45h] = 100
    wr32(rec, 73, 0x3F800000);                     // [esi+49h] = 1.0f
    rec[92] = 100;                                 // [esi+5Ch] = 100
    wr32(rec, 149, -1);                            // [esi+95h] = -1
    wr32(rec, 43, 0);                              // [esi+2Bh] = 0 (ecx == 0)
    wr32(rec, 93, 0);                              // [esi+5Dh] = 0
    wr32(rec, 97, 0);                              // [esi+61h] = 0

    i32 id = g_buildingNextId;                     // dword_649890 (0x58708f)
    wr32(rec, 1, id);                              // [esi+1] = id
    wr16(rec, 37, ownerWord);                      // [esi+25h] = owner
    wr16(rec, 39, ownerWord);                      // [esi+27h] = owner
    g_buildingNextId = id + 1;                     // dword_649890 = id + 1
    wr32(rec, 48, 0);                              // [esi+30h] = 0

    // 0x5870c5 VIBE_Light_SetGrayColorThunk(rec+153, gray 0, 16): broadcast the
    // grey byte (real render::BroadcastGrayDword) and fill the 16-byte block.
    {
        u32 grey = render::BroadcastGrayDword(0);
        for (int i = 0; i < 16; i += 4)
            std::memcpy(rec + 153 + i, &grey, 4);
    }
    wr32(rec, 165, -1);                            // [esi+0A5h] = -1 (0x5870d5)
    rec[153] |= 1;                                 // [esi+99h] |= 1 (0x5870df)
    std::memset(rec + 101, 0, 48);                 // the rep-stos zero of +101..+148

    // ---- name init (0x587107..0x587155) -------------------------------------
    // v15 = dword_8C4790[14*prot]; primary = *v15 ? dword_8C4788[14*prot]
    //                                            : unk_62661C.
    {
        const char* cand0 = h->NameTemplate(prot, 0);
        const char* primary;
        if (cand0 && cand0[0]) {
            const char* p = h->PrimaryName(prot);
            primary = p ? p : "";
        } else {
            primary = h->DefaultBuildingName();
        }
        CopyName(rec + 5, primary);
    }

    // ---- candidate de-dup loop (0x587158..0x587253) --------------------------
    char survivors[12][64];                        // v48[1024 + 64*k] slots
    int v53 = 0;
    for (int i = 0; i < 12; ++i) {
        const char* nm = h->NameTemplate(prot, i); // dword_8C4790[14*prot + i]
        if (!nm || !nm[0])
            continue;                              // empty -> not collected
        if (Building_IsNameTaken169(nm))
            continue;                              // clash (v24 < 256) -> dropped
        if (std::strlen(nm) >= 0x20)
            continue;                              // "Building name too long: %s"
                                                   // (scratch sprintf only)
        CopyName(reinterpret_cast<u8*>(survivors[v53]), nm);
        ++v53;
    }
    if (v53) {                                     // 0x58726b
        int pick = Building_PickUniqueName(v53);   // RandNext() % (u16)v53 (REAL)
        CopyName(rec + 5, survivors[pick]);        // 0x58729b
    }

    // ---- room-slot loop (0x5872c7..0x587359) over typeRec words @+35 ---------
    {
        int off = 0;                               // v37 walks typeRec+35..
        if (rd16(typeRec, 35) != 0) {              // 0x5872d1 outer gate
            int v56 = 0;
            do {
                u16 w = static_cast<u16>(rd16(typeRec, 35 + off));   // v39
                if (w != 0xFFFF) {                 // 0x5872df
                    u16 wm = w & 0x7FFF;           // HIBYTE(v39) &= ~0x80
                    u8 tb = ObjectTypeByte(static_cast<i16>(wm));    // v40
                    if (tb == 2 || tb == 6)
                        v52 = true;                // 0x5872fe / 0x587453
                    // hi-bit slot: *(char*)(typeRec+36+off) < 0  (bit 0x8000)
                    if ((w & 0x8000) != 0) {       // 0x58730d
                        if (static_cast<u16>(rd16(typeRec, 35 + off)) == 0xFFFF) {
                            // 0x587498 — DEAD branch (outer guard excludes it);
                            // translated verbatim: latch prot + slot index.
                            g_lastGebProtLatch = prot;       // byte_6498D7
                            g_lastGebSlotLatch = static_cast<u8>(v56); // byte_6498D8
                        } else if (tb == 2 || tb == 6) {
                            v52 = true;            // 0x587336
                            // 0x58733d — the WIRE: the recon AllocStorageRoom.
                            Building_AllocStorageRoom(
                                reinterpret_cast<const ObjectRec*>(rec),
                                static_cast<i16>(wm), rec);
                        } else if (!v52) {         // 0x58746f
                            // 0x587487 AddObjekt(*(rec+1), wm, 1, rec).
                            h->AddObjekt(rd32(rec, 1), static_cast<i16>(wm), 1,
                                         rec);
                        }
                    }
                }
                off += 2;                          // v37 += 2
                ++v56;                             // 0x58734d
            } while (v56 < 64 && rd16(typeRec, 35 + off) != 0);   // 0x587359
        }
    }

    // ---- storage node word (0x58736e..0x587388) ------------------------------
    {
        i32 w = h->QueryStorageNodeTypeWord(rec);  // QueryFind(.,1,4,2)+skip 253
        if (w >= 0)
            wr16(rec, 41, static_cast<u16>(w));    // *(rec+41) = *v42
    }
    // ---- FindStorableObject (0x58738c..0x58739a) -----------------------------
    {
        i32 w = h->StorableObjectTypeWord(rec);    // 0x5877ac (hook-domain)
        if (w >= 0)
            wr16(rec, 41, static_cast<u16>(w));    // *(rec+41) = *StorableObject
    }

    // ---- per-type defaults: IsProductionType gate + the switch(prot) tail ----
    // 0x5873a0 `if (IsProductionType(rec)) +48 = 5000` and the whole switch are
    // the REAL Building_ApplyTypeDefaults core (building_create2.cpp).
    bool isProd = Building_IsProductionKind(typeRec[0]);     // 0x587f80 (REAL)
    i32 ownerPersonId = -1;                        // dword_12CE914[134*owner]
    if (ownerWord != 0xFFFF && ownerWord < kPersonCapacity)  // (bounds: safety)
        ownerPersonId = g_persons[ownerWord].id;
    Building_ApplyTypeDefaults(rec, prot, ownerWord, ownerPersonId, h->Now(),
                               isProd);

    // LABEL_71 (prot 20 / 21 / 22, 0x5874d9): AddObjekt(*(rec+1), 437, 1, rec)
    // and stamp the new node (+28/+29 = 0xFF, +30 dword = -1, +34 = 0).  (Applied
    // after ApplyTypeDefaults — no shared state with the switch's writes.)
    //   - prot 20 (0x14): `< 0x15` arm, `if (v58 != 20) goto LABEL_52` else fall.
    //   - prot 21 (0x15): `cmp bl,15h; jnb 5874C0` then `jbe loc_5874D9` (==0x15).
    //   - prot 22 (0x16): falls past `cmp bl,16h; ja 5873DB` to loc_5874D9.
    // Wave: prot 21 was previously dropped — confirmed reachable at 0x5874c0.
    if (prot == 20 || prot == 21 || prot == 22) {
        u8* n = h->AddObjekt(rd32(rec, 1), 437, 1, rec);
        if (n) {                                   // 0x5874ed
            n[28] = 0xFF;                          // *(v45+28) = -1
            n[29] = 0xFF;                          // *(v45+29) = -1
            wr32(n, 30, -1);                       // *(v45+30) = -1
            n[34] = 0;                             // *(v45+34) = 0
        }
    }

    // LABEL_52 tail (the prot-38 +41=270 write runs inside ApplyTypeDefaults).
    h->InitWorkerCapacities(rec);                  // 0x5873e9 (building4 core gap)
    (void)Building_MapKindToCategory(rec[0]);      // 0x5873f2 (REAL; result unused)
    Building4_EnsureDefaultObjects(rec);           // 0x5873f9 (REAL building4)

    return rec;                                    // 0x58740b
}

// ===========================================================================
// gilde.exe 0x496b90 — ExSellObjekt storage-node phases.
// ===========================================================================
bool Sell_DepleteSourceStockNode(SellResolve& r, i32 qty) {
    if (!r.srcIdPresent)                           // *(a1+20) == -1 (0x496ec2)
        return true;

    // 0x496ed5: v20 = *(v56+14) - qty; *(v56+14) = v20.
    i32 v20;
    if (r.srcStockNodePtr) {
        v20 = rd32(r.srcStockNodePtr, 14) - qty;
        wr32(r.srcStockNodePtr, 14, v20);
    } else {
        v20 = r.srcRawCount - qty;                 // model fallback (no live node)
        r.srcRawCount = v20;
    }

    if (v20 <= 0) {                                // 0x496edc
        u8 tb = ObjectTypeByte(r.proto);           // 0x49735c
        if (tb == 2 || tb == 6) {                  // 0x497382
            if (!r.srcBuildingRec)                 // 0x49736c -> return 1
                return false;
            g_callerHooks->RemoveStorageRoom(r.srcBuildingRec, r.proto,
                                             r.destOwnerRec);   // 0x49739b
        } else {
            g_callerHooks->RemoveByProt(r.srcChildList, r.proto,
                                        r.destOwnerRec);        // 0x49738b
        }
    }
    return true;
    // (The *(a1+35) raw-material AddObjektToParent tail @0x496ee2 stays with
    // trade_sell's deferred leaves — named gap in the progress file.)
}

bool Sell_EnsureDestStorageNode(SellResolve& r, i32 qty) {
    if (!r.destIdPresent)                          // *(a1+16) == -1 -> LABEL_63
        return true;

    u8* node = r.destStockNodePtr;                 // v55
    if (!node) {                                   // 0x496f59
        u8 tb = ObjectTypeByte(r.proto);           // 0x4973f8
        if (tb == 2 || tb == 6) {                  // 0x497422
            if (!r.destBuildingRec)                // 0x497408 -> return 1
                return false;
            // 0x4974af — the WIRE: the recon Building_AllocStorageRoom.
            node = static_cast<u8*>(Building_AllocStorageRoom(
                reinterpret_cast<const ObjectRec*>(r.destBuildingRec), r.proto,
                r.srcStorageRec));
            if (!node)                             // 0x4974b8 miss -> return 1
                return false;
        } else {
            // 0x49742a AddObjekt(*(a1+16), proto, qty, v4).
            node = g_callerHooks->AddObjekt(r.destId, r.proto, qty,
                                            r.srcStorageRec);
            if (!node)                             // 0x49743a -> return 1
                return false;
            if (r.destStorageRec) {                // v57 (0x497448)
                wr32(node, 14, rd32(node, 14) + 1);          // 0x497464/67
                u8 tb2 = ObjectTypeByte(r.proto);            // v43 (0x497465)
                i16 dt = rd16(r.destStorageRec, 0);          // *v57
                if ((tb2 == 23 || tb2 == 37) && (dt == 42 || dt == 278)) {
                    if (r.destOwnerRec) {          // v3 (0x497483)
                        u8 k = r.destOwnerRec[2];  // *((BYTE*)v3 + 2)
                        if (k == 6 || k == 7)      // 0x4974e8
                            wr32(node, 28, 4);     // *((DWORD*)v55+7) = 4
                    }
                }
            }
        }
    } else {
        // 0x496f69: *(v55+14) += qty (existing dest stock node).
        wr32(node, 14, rd32(node, 14) + qty);
    }

    // LABEL_58 (0x496f6c..0x496f79): dword_631290 = *(v55+2).
    r.destStockNodePtr = node;
    r.lastDestNodeId = rd32(node, 2);
    g_lastTradeId = r.lastDestNodeId;
    return true;
}

bool SellStoragePhase1to1::SourcePhase(SellResolve& r, i32 qty) {
    return Sell_DepleteSourceStockNode(r, qty);
}
bool SellStoragePhase1to1::DestPhase(SellResolve& r, i32 qty) {
    return Sell_EnsureDestStorageNode(r, qty);
}

// ===========================================================================
// gilde.exe 0x577464 — VIBE_Bauplatz_MapAllToSupermap.
// ===========================================================================
int Bauplatz_MapAllToSupermap(i32 mapCtx) {
    // 0x577479: SetGrayColorThunk(buf, gray 0, 0x400) — a zero fill of the
    // 256-entry pointer frame (grey 0 broadcasts to 0).
    const u8* frame[256];
    std::memset(frame, 0, sizeof(frame));

    // 0x577482 — the REAL building5 reconstruction of 0x50c8ec.
    int result = Building_FilterBlockedBauplatze(0, frame, 256);
    if (result > 0) {                              // 0x577487/0x57748b
        // Safety clamp: the binary frame holds 256 entries; the original would
        // read past its stack frame beyond that (never happens live).
        int n = result < 256 ? result : 256;
        for (int i = 0; i < n; ++i) {              // v5 += 4 over 4*result bytes
            if (frame[i]) {                        // 0x577499
                // 0x5774b1 — the WIRE: the plot NODE pointer is the name (scene
                // nodes begin with their name string).
                result = Bauplatz_MapOneToSupermap(
                    mapCtx, reinterpret_cast<const char*>(frame[i]));
            }
        }
    }
    return result;                                 // last MapOne result / count
}

// ===========================================================================
// Wiring (rule 13).
// ===========================================================================
namespace {
u8* CreateGebaeudeFlowTrampoline(u8 typeByte, u16 ownerWord) {
    return Building_CreateGebaeudeFlow(typeByte, ownerWord);
}
SellStoragePhase1to1 g_sellStoragePhase1to1;
}  // namespace

void WireBuildingCallers() {
    // 0x586fb8 backend -> live callers VIBE_Command_HandleSpawnObject (0x496520)
    // and VIBE_Command_ExCreateGebaeude (0x49c19c) in command_apply5.cpp.
    SetCreateGebaeudeHook(&CreateGebaeudeFlowTrampoline);
    // 0x496b90 storage phases -> live caller command_apply6.cpp ExSellObjekt
    // (dispatched by VIBE_Command_ExecCommands 0x494088 / command_receive.cpp).
    TradeSetStoragePhase(&g_sellStoragePhase1to1);
}

}  // namespace guild::sim
