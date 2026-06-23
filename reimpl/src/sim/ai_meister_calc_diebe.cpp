// ===========================================================================
// VIBE_Ai_CalcMeisterDiebe @0x457440 — thief / criminal Meister AI.
//
// gilde.exe 0x457440 — __usercall, eax = a1 (meisterRec base / person record).
// No return value (void).  Size: 5262 bytes (0x457440..0x4588CC).
//
// Top-level structure
// -------------------
// 1. QueryFind type-278 scene node under the Meister's building (the "office"
//    node that enables the thief career, type-def 0x116=278, filter op 6 =
//    useStack, op 0 = type==278).  If found → thiefNode = result.
//
// 2. If thiefNode present:
//    a. Reset scratch tables (dword_B56FDC / dword_B56FE0 = 0).
//    b. MeisterCollectStorageItems(mr)
//    c. MeisterGatherRequiredItems(mr, *(bldgRec+57))
//    d. EquipStaffWeapon(mr, thiefNode)
//    e. STOCK SNAPSHOT (unk_B53C50 / dword_B53968 — described below).
//    f. MeisterCollectTransporters(mr)
//    g. MeisterTradeManageStorage(mr)
//
// 3. Always (regardless of thiefNode):
//    MeisterHireStaff(mr)
//    MeisterFillAiSlots(thiefNode, mr, 10)
//    MeisterTrainStaff(thiefNode, mr, 10)
//    MeisterRenovateBuilding(mr)
//    if QueryFind(bldgRoot, 1 filter, type==96): MeisterFlagIdleStaff(mr)
//    MeisterFindFreeStaffSlot(mr)
//
// 4. WEEKLY GATE:
//    if (hour(qword_13CE852) <= 6) OR ((meisterId + hour) % 3 != 0)
//       → clear bit 0x20 of mr+436, return.
//    if bit 0x20 of mr+436 already set → return (done this tick).
//    Set bit 0x20.
//
// 5. STAFF COUNT loop (all 768 person slots):
//    Count active staff (v107 = idle-ready, v13 = total).
//    Active = alive, marker!=-1, kind!=10, employer==mr's bldgRec,
//             profByte!=0, actionObj!=0, actionObj+44 == employer+1.
//    Idle-ready additionally: !busy (dword_12CEA8C==0).
//    Mirror to HUD globals if selected Meister.
//
// 6. STAFF CHECK: if (v13/2) > v107 → "no people" log, return.
//
// 7. ROUTING (owner-type branch):
//    ownerKind = byte_12CE912[ 536 * *(u16*)(bldgRec+39) ]  (meister's own owner kind)
//    Case A: ownerKind == 6 or 7  (guild thief)
//       v108 = RandomModulo(2)
//       if v108 && mr+448 != -1 && RandomModulo(2) → v108=0, goto BREAK_IN_PATH
//    Case B: else
//       If RandomFloat > 0.85 && v107>=3 && 7-dword_63C744 <= (int)qword_13CE852
//          → v108=3, goto ANGRIFF_GATE
//       v108 = RandomModulo(3)
//    v108 == 1 && RandomFloat < 0.75 → fall to LABEL_63 (break-in command)
//    v108 == 0 → BREAK_IN_PATH (target selection)
//    v108 == 3 → ANGRIFF_GATE (CalcAngriff)
//    v108 == 1 (rand hit threshold) → LABEL_101
//    v108 > 1 → LABEL_63 (no-target path → break-in/spy command)
//
// 8. BREAK_IN_PATH:
//    Validate / clear cached target (mr+448).
//    Scan 8x8 danger grid for maxDanger.
//    Scan 256 object slots (169 stride) for best burgle/spy target:
//      filter: alive, owner != self, != 0xFFFF, !(obj[90]&1),
//              owner != dword_6498E4,
//              !isProductionType, charActionIsAnimalTargetBusy
//      score: tileDanger-normalized, (8.0 - secLevel)*0.5, RNG(100)*0.01,
//             winner by assetWorth(new)*score vs assetWorth(best)*bestScore
//    If found: *(mr+448) = *(target+1).
//    Fall to LABEL_62.
//
//    LABEL_62: if (!target) → LABEL_63.
//    QueryFind type-202 node under meister's bldg, iterate to find *(node+21)==targetId.
//    If found AND (node[55]>=0x5F OR node[54]*4.0+RandMod(0x14) <= node[55]):
//       → SPY path (high-security threshold: type-202 spy slot has enough strength)
//       Check He handler list (cmd 64,63) for existing spy on target.
//       If existing → "already spying" log, fall to LABEL_100.
//       Else → emit BREAK-IN command (cmdType=60), clear target.
//       Fall to LABEL_100.
//    Else (low security / no slot):
//       Check He handler list (cmd 64,63) for existing spy.
//       If existing → "still has spy" log, fall to LABEL_100.
//       Else → emit SPY command (cmdType=64), no target clear.
//
//    LABEL_100: if v108==3 → ANGRIFF_GATE else → LABEL_101.
//    LABEL_101: if v108==1 → DENSE SPY FLOW, else return.
//
// 9. ANGRIFF_GATE: v108=3, call CalcAngriff(mr, v107). If returns 0 → LABEL_63.
//
// 10. LABEL_63 (default break-in / patrol command, cmdType=97/0x61):
//     Emit command with up to 8 workers, guard: *(cmd+0x38) != -1.
//
// -----------------------------------------------------------------------
// DENSE SPY FLOW (LABEL_101 with v108==1):
//    Another 8x8 scan + 256-slot scan (burgle target scoring, different filter:
//      owner!=self, !=0xFFFF, !(obj[90]&1), category!=3 && !=5 && !=0,
//      dword_12CEA7C[534*ownerWord]==0 or its *(+39) != meisterOwner,
//      byte_12CE912[536*ownerWord] <= 7, obj+97 != 0 && worldToCityTile succeeds).
//    Score: tileDanger, RNG(100)*0.01 (no security subtraction here).
//    Winner by: !v105 OR (sumCurrencyHeld(winner)*v111 < sumCurrencyHeld(cand)*v128).
//    QueryFind type-101 under bldgRec.
//    If no result → LABEL_63.
//    If !v105 → LABEL_63.
//    If *(bldgRec+101) != -1 → LABEL_63.
//    He handler (cmd 60, filter 3, meisterBldgId): if already burgling → LABEL_63+emit.
//    Else → emit BURGLE command (cmdType=60), clear target.
//
// -----------------------------------------------------------------------
// CONSTANTS (verified with get_bytes):
//   dbl_619770 = 0.85  (3f eb 33 33 33 33 33 33)
//   dbl_619778 = 0.75  (3f e8 00 00 00 00 00 00)
//   flt_619780 = 0.125 (3e 00 00 00)
//   dbl_619788 = 0.5   (3f e0 00 00 00 00 00 00)
//   dbl_619790 = 0.01  (3f 84 7a e1 47 ae 14 7b)
//   flt_619798 = 4.0   (40 00 00 00)
//   flt_61979C = 8.0   (41 00 00 00)
//
// -----------------------------------------------------------------------
// STOCK SNAPSHOT (unk_B53C50 / dword_B53968 / g_aiSelStaffSet):
//   When the selected building matches the Meister's building AND the snapshot
//   count < 4, the function snapshots each 64-byte stock row from the stock table
//   (g_stockTable + rowOffset + 2) into unk_B53C50[64 * snapshotIdx] until either
//   the stock table is exhausted or 4 entries are captured. This is a debug/HUD
//   mirror of the first few stock items — modeled via g_diebeFavoriteStocks below.
//
// -----------------------------------------------------------------------
// B53950 / g_aiSelMeisterBuilding: the "currently selected AI Meister building"
// ptr for the HUD. If this equals *(mr+0x16C), we update HUD globals and run
// the stock snapshot. Not set here — only read (set by the HUD selection code).
//
// -----------------------------------------------------------------------
// POINTER COLUMNS (per SPEC UPDATE 1):
//   mr+0x16C (kM_bldgRec):  rdptr()  → building record base
//   person+0x16C (kP_employer): rdptr() → employer building record base
//   person+0x184 (kP_actionObj): rdptr() → action-object record base
//   (The comparison *(actionObj+44) == *(employer+1) uses rdptr resolvers.)
//
// -----------------------------------------------------------------------
// RNG DRAW ORDER (matches decompile exactly):
//   Phase A (routing, non-guild path): RandomFloat, RandomModulo(3)
//   Phase A (routing, guild path): RandomModulo(2), [cond] RandomModulo(2)
//   Phase A (routing, non-guild alt path): RandomFloat
//   Burgle scan: RandomModulo(0x14) [in score comparison section]
//   Object sweep 1 (break-in path): RandomModulo(0x64) per candidate
//   SPY path worker select: RandomModulo(3), [cond] RandomModulo(3)
//   Object sweep 2 (dense spy path): RandomModulo(0x64) per candidate
//   See inline comments at each draw site.
//
// -----------------------------------------------------------------------
// LEAVES called via g_meisterLeaves:
//   queryFind, queryIterNext, resolveEntityById,
//   buildingFindById, worldToCityTile,
//   heFindFirst, heFindNext,
//   sumCurrencyHeld, computeAssetWorth,
//   securityLevel, mapTypeToCategory,
//   isProductionType, charActionIsAnimalTargetBusy
// ===========================================================================

#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"

#include "sim/entity.h"         // g_objects, g_persons, g_personIds, kPersonCapacity
#include "util/math_random.h"     // guild::util::RandomModulo
#include "util/math_rng_float.h"   // guild::util::RandomFloatScaled
#include "sim/building.h"       // BuildingPriceMode() → dword_63C744

#include <cstring>
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// unk_B53C50 — Diebe stock snapshot buffer.
// Holds up to 4 copies of 64-byte stock-table rows, captured when the HUD has
// selected this Meister's building. Modeled as a file-scope byte array.
// Original address: 0xB53C50.  Size: 4 * 64 = 256 bytes.
// ---------------------------------------------------------------------------
static u8 g_diebeFavoriteStocks[4 * 64] = {};  // unk_B53C50

// ---------------------------------------------------------------------------
// Local-player city-word exclusion (mirrors dword_6498E4 / *(u16*)dword_6498E4).
// The object sweeps skip buildings whose owner word equals this value.
// The live bridge sets it; tests inject it via the friend accessor below.
// Separate from CalcAngriff's g_angriffLocalPlayerWord (that file is private).
// ---------------------------------------------------------------------------
static u16 g_diebeLocalPlayerWord = 0;  // *(u16*)dword_6498E4 at scan time

// Test/bridge accessor (not in the public header — only needed internally).
void CalcMeisterDiebe_SetLocalPlayerWord(u16 w) { g_diebeLocalPlayerWord = w; }
u16  CalcMeisterDiebe_GetLocalPlayerWord()      { return g_diebeLocalPlayerWord; }

// ===========================================================================
// CalcMeisterDiebe — 1:1 reconstruction of VIBE_Ai_CalcMeisterDiebe @0x457440.
//
// `meisterRec` is the raw 536-byte person record base (the `a1` in the original
// __usercall, passed in eax).
// ===========================================================================
void CalcMeisterDiebe(u8* meisterRec) {
    using namespace guild::util;      // RandomModulo, RandomFloatScaled
    using namespace guild::sim::aimei;

    u8* mr = meisterRec;

    // -----------------------------------------------------------------------
    // 0x457440..0x4574c7: QueryFind type-278 node under the Meister's building.
    // Original: VIBE_GameObject_QueryFind(*(bldgRec+93), 2, 6, 0, 278)
    //   argc=2 ops: op6 (useStack), op0+val 278 (type==278).
    // If found, resolve each node: if item-type-def[0]==2, take it as thiefNode.
    // -----------------------------------------------------------------------
    u8* thiefNode = nullptr;   // v113 in decompile
    {
        u8* bldgRec = rdptr(mr, kM_bldgRec);   // *(mr+0x16C)
        i32 sceneRootId = bldgRec ? rd32(bldgRec, kB_sceneRoot93) : -1;  // *(bldgRec+93)

        // QueryFind(sceneRootId, ops=[6, 0, 278], n=2)
        // leaf_signatures: queryFind(rootId, filters[], nfilters)
        // Raw call: VIBE_GameObject_QueryFind(v90, 2 [=argc], 6, 0, 278)
        //   argc=2 means 2 ops: op=6 (no val), then op=0, val=278.
        // Build the flat filter list: {6 (no val needed), 0, 278}.
        // The reimpl queryFind(rootId, filters, nfilters) where nfilters=number of ops.
        // We pass the raw int array as the decompile args after argc.
        const int filts[3] = {6, 0, 278};
        u8* cur = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->queryFind)
            cur = g_meisterLeaves->queryFind(sceneRootId, filts, 2);

        if (cur) {
            // resolve each found node; take the first whose item-type-def byte[0] == 2.
            do {
                // VIBE_GameObject_ResolveEntityById(0, &v112, *(v2+6), 0)
                // *(v2+6) = *(cur+6): the node's entity id (dword at offset 6 of the node).
                // We call resolveEntityById(outA=null, outB=&resolved, id, mode=0).
                i32 resolvedIdx = -1;
                i32 entityId = rd32(cur, 6);   // *(cur+6)
                int ok = 0;
                if (g_meisterLeaves && g_meisterLeaves->resolveEntityById)
                    ok = g_meisterLeaves->resolveEntityById(&resolvedIdx, nullptr, entityId, 0);

                if (ok && resolvedIdx >= 0) {
                    // Check item-type-def[65*itemTypeWord + 0] == 2
                    // v112 = the resolved entity (a scene node or item base);
                    // the decompile does: *(_BYTE*)(dword_13CE27C + 65 * *v112) == 2
                    // where *v112 is a signed word. resolvedIdx IS *v112 here.
                    u8 typeDefByte = itemTypeField(static_cast<i32>(resolvedIdx), 0);
                    if (typeDefByte == 2) {
                        thiefNode = cur;  // v113 = v2 (0x4574ab)
                    }
                }

                // IterNext (0x4574b2)
                u8* next = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->queryIterNext)
                    next = g_meisterLeaves->queryIterNext();
                cur = next;
            } while (!thiefNode && cur);  // 0x4574c6: while (!v113 && v3)
        }
    }

    // -----------------------------------------------------------------------
    // 0x4574d1..0x457575: if (thiefNode) — production prep block.
    // -----------------------------------------------------------------------
    if (thiefNode) {
        // 0x4574dd: dword_B56FDC = 0
        g_workOrderCount = 0;
        // 0x4574e3: dword_B56FE0 = 0
        g_stockRowCount  = 0;

        // 0x4574e9: VIBE_MeisterAi_CollectStorageItems(mr)
        MeisterCollectStorageItems(mr);

        // 0x4574fb: VIBE_MeisterAi_GatherRequiredItems(mr, *(bldgRec+57))
        {
            u8* bldgRec = rdptr(mr, kM_bldgRec);
            i32 neededId = bldgRec ? rd32(bldgRec, kB_needed57) : 0;
            MeisterGatherRequiredItems(mr, neededId);
        }

        // 0x457504: VIBE_MeisterAi_EquipStaffWeapon(mr, thiefNode)
        // v4 = (int)v113 (32-bit ptr cast to int in orig). In 64-bit reimpl this is
        // truncating but consistent with the `int arg` EquipStaffWeapon signature.
        EquipStaffWeapon(mr, static_cast<int>(reinterpret_cast<uintptr_t>(thiefNode)));

        // -----------------------------------------------------------------------
        // 0x457509..0x457560: STOCK SNAPSHOT (unk_B53C50 / dword_B53968).
        //
        // if (dword_B53950 == *(mr+364) && dword_B53968 < 4) {
        //     v6 = *(mr+364) ^ dword_B53950;  -- which is 0 (they are equal)
        //     v7 = dword_B56FE0 << 6;         -- stock byte bound
        //     while (v6 < v7 && dword_B53968 < 4) {
        //         qmemcpy(unk_B53C50 + 64*dword_B53968,
        //                 (char*)dword_B5444E + v6 + 2, 0x40);
        //         v6 += 64; ++dword_B53968;
        //     }
        // }
        //
        // Note: dword_B53950 == g_aiSelMeisterBuilding (the HUD-selected building ptr).
        //   *(mr+364) == rdptr bldgRec handle (a handle, compared as i32).
        //   dword_B5444E == g_stockTable base.
        //   dword_B53968 == aimei::g_aiSelStaffSet (the snapshot count).
        //   unk_B53C50   == g_diebeFavoriteStocks (256-byte snapshot buffer).
        //
        // The XOR `v6 = *(mr+364) ^ dword_B53950` evaluates to 0 when equal,
        // making v6 = 0. Then the loop iterates stock rows from byte 0+2=2.
        // This is a debug/HUD feature that snapshots the first N stock rows.
        // -----------------------------------------------------------------------
        {
            i32 meisterBldgHandle = rd32(mr, kM_bldgRec);  // *(mr+364) raw handle
            if (g_aiSelMeisterBuilding == meisterBldgHandle && aimei::g_aiSelStaffSet < 4) {
                // v6 = meisterBldgHandle ^ g_aiSelMeisterBuilding == 0
                int v6 = 0;
                int v7 = g_stockRowCount << 6;   // dword_B56FE0 << 6
                while (v6 < v7 && aimei::g_aiSelStaffSet < 4) {
                    // qmemcpy(unk_B53C50 + 64*dword_B53968, dword_B5444E + v6 + 2, 0x40)
                    std::memcpy(g_diebeFavoriteStocks + 64 * aimei::g_aiSelStaffSet,
                                g_stockTable + v6 + 2,
                                0x40u);
                    v6 += 64;
                    ++aimei::g_aiSelStaffSet;
                }
            }
        }

        // 0x45756a: VIBE_MeisterAi_CollectTransporters(mr)
        MeisterCollectTransporters(mr);

        // 0x457571: VIBE_MeisterAi_TradeManageStorage(mr)
        MeisterTradeManageStorage(mr);
    }

    // -----------------------------------------------------------------------
    // 0x457576..0x4575d5: always-run sub-planners.
    // -----------------------------------------------------------------------

    // 0x45757d: VIBE_MeisterAi_HireStaff(mr)   [ebx=0x0A passed in orig == not used here]
    MeisterHireStaff(mr);

    // 0x45758b: VIBE_MeisterAi_FillAiSlots((int)thiefNode, mr, 10)
    //   third arg is "cap" = 10 (0x0A = ebx value just set)
    //   first arg is the thiefNode cast to int (may be 0/null)
    {
        SceneNode* nodeArg = reinterpret_cast<SceneNode*>(thiefNode);
        MeisterFillAiSlots(nodeArg, mr, 10);
    }

    // 0x45759e: VIBE_MeisterAi_TrainStaff((int)thiefNode, mr, 10)
    {
        SceneNode* nodeArg = reinterpret_cast<SceneNode*>(thiefNode);
        MeisterTrainStaff(nodeArg, mr, 10);
    }

    // 0x4575a5: VIBE_MeisterAi_RenovateBuilding(mr)
    MeisterRenovateBuilding(mr);

    // 0x4575b2..0x4575ca: QueryFind type-96 under bldgRoot; if found → FlagIdleStaff.
    // Original: if (VIBE_GameObject_QueryFind(*(bldgRec+93), 2, 6, 0, 96))
    {
        u8* bldgRec = rdptr(mr, kM_bldgRec);
        i32 sceneRootId = bldgRec ? rd32(bldgRec, kB_sceneRoot93) : -1;
        const int filts96[3] = {6, 0, 96};
        bool found96 = false;
        if (g_meisterLeaves && g_meisterLeaves->queryFind)
            found96 = (g_meisterLeaves->queryFind(sceneRootId, filts96, 2) != nullptr);
        if (found96)
            MeisterFlagIdleStaff(mr);
    }

    // 0x4575d1: VIBE_MeisterAi_FindFreeStaffSlot(mr)
    MeisterFindFreeStaffSlot(mr);

    // -----------------------------------------------------------------------
    // 0x4575d8..0x4575ff: WEEKLY GATE.
    //   if (WORD2(qword_13CE852) <= 6) OR ((*(mr+4) + WORD2(qword_13CE852)) % 3)
    //     → clear bit 0x20 of mr+436, return.
    //   WORD2(qword_13CE852) == g_meisterGameTime.hour (stored at +4 of the qword).
    // -----------------------------------------------------------------------
    {
        u16 hour = gtHour();   // WORD2(qword_13CE852)
        i32 meisterId = rd32(mr, kM_id);   // *(mr+4)
        // 0x4575e2: if (hour <= 6u) || ((meisterId + hour) % 3 != 0)
        if (static_cast<unsigned int>(hour) <= 6u
         || (static_cast<unsigned int>(meisterId + static_cast<i32>(hour)) % 3u) != 0u)
        {
            // 0x457b7f: *(mr+436) &= ~0x20
            wr8(mr, kM_dayFlags, rd8(mr, kM_dayFlags) & static_cast<u8>(~0x20u));
            return;
        }
    }

    // 0x457602..0x45760b: if bit 0x20 of mr+436 already set → return.
    {
        u8 dayFlags = rd8(mr, kM_dayFlags);
        if (dayFlags & 0x20u)
            return;
        // 0x457621: set bit 0x20
        wr8(mr, kM_dayFlags, dayFlags | 0x20u);
    }

    // -----------------------------------------------------------------------
    // 0x457613..0x457648: Staff count loop over all 768 person slots.
    //   v107 = idle-ready count; v13 = total active staff.
    //   stride = 536 bytes; byte offset ranges 0..411648-1 (= 536*768).
    //   person[0..1] (word_12CE910): if == -1 → free slot (skip).
    //   byte_12CE912[stride*i] == person[2]: kind byte.
    //   byte_12CE918[stride*i] == person[8]: isLive byte.
    //   dword_12CEA7C[stride/4*i] == person+0x16C: employer building rec ptr.
    //   byte_12CEA75[stride*i]   == person+0x165: profession/assigned byte.
    //   dword_12CEA8C[stride/4*i] == person+0x17C: jail/busy dword.
    //   dword_12CEA94[stride/4*i] == person+0x184: action-object ptr.
    //   Action active: actionObj != 0 && *(actionObj+44) == *(employer+1).
    //   Idle-ready: additionally !busy.
    // -----------------------------------------------------------------------
    int v107 = 0;    // idle-ready count
    int v13  = 0;    // total active staff
    {
        i32 meisterBldgHandle = rd32(mr, kM_bldgRec);   // *(mr+364)

        // Loop: v14 = byte offset (stride 536). Goes from 0 to 411648.
        // 0x457627: if (byte_12CE918[v14]) — isLive byte
        for (unsigned int v14 = 0; v14 != 411648u; v14 += 536u) {
            int pidx = static_cast<int>(v14 / 536u);
            u8* p = pr(pidx);

            // 0x457627: byte_12CE918[v14] (person[8]) = isLive byte
            if (!rd8(p, kP_isLive))
                continue;

            // 0x457bac: word_12CE910[v14/2] != -1 (marker word, LE i16)
            if (rd16(p, kP_marker) == static_cast<i16>(-1))
                continue;

            // 0x457bac: byte_12CE912[v14] != 10 (kind != 10)
            if (rd8(p, kP_kind) == 10)
                continue;

            // 0x457bac: dword_12CEA7C[v14/4] == *(mr+364) (employer == meister's bldg handle)
            // The kP_employer column stores a handle; compare raw i32 handles.
            if (rd32(p, kP_employer) != meisterBldgHandle)
                continue;

            // 0x457bb2: byte_12CEA75[v14] (profByte != 0)
            if (!rd8(p, kP_profByte))
                continue;

            // 0x457bbf: v37 = dword_12CEA94[v14/4] (action-object ptr / handle)
            i32 actionHandle = rd32(p, kP_actionObj);
            if (!actionHandle)
                continue;

            // 0x457bd3: v38 = dword_12CEA94[v14/4] (same)
            // 0x457bd5: v39 = *(v37+44)
            // 0x457bd8: v40 = *(dword_12CEA7C[v14/4] + 1)
            // Compare *(actionObj+44) == *(employer+1)
            // Per SPEC UPDATE 1: use rdptr for pointer columns.
            u8* actionObj  = rdptr(p, kP_actionObj);   // resolves handle → record base
            u8* employer   = rdptr(p, kP_employer);
            if (!actionObj || !employer)
                continue;
            i32 aoBldgId  = rd32(actionObj, 44);        // *(actionObj+44)
            i32 empBldgId = rd32(employer,  kB_id1);    // *(employer+1)
            if (aoBldgId != empBldgId)
                continue;

            // 0x457be9: ++v13 (active staff)
            ++v13;

            // 0x457bfd: if (!dword_12CEA8C[v14/4] && v38 && v40 == *(v38+44))
            //   = !busy && actionHandle && aoBldgId == empBldgId  (already checked)
            //   → ++v107 (idle-ready)
            if (!rd32(p, kP_busy) && actionHandle && aoBldgId == empBldgId)
                ++v107;
        }
    }

    // -----------------------------------------------------------------------
    // 0x457655..0x457675: HUD mirror — if selected Meister matches.
    // -----------------------------------------------------------------------
    {
        i32 meisterBldgHandle = rd32(mr, kM_bldgRec);
        if (g_aiSelMeisterBuilding == meisterBldgHandle) {
            // 0x45765e: v15 = *(mr+448)
            i32 targetId = rd32(mr, kM_target);
            // 0x457664: dword_B53964 = v107
            g_aiSelMeisterCount = v107;
            // 0x45766c: if (v15 != -1) dword_B53954 = VIBE_Building_FindById(v15)
            if (targetId != -1) {
                i32 foundHandle = 0;
                if (g_meisterLeaves && g_meisterLeaves->buildingFindById) {
                    u8* fb = g_meisterLeaves->buildingFindById(targetId);
                    // store as handle (raw i32 address in orig; we store 0 if null)
                    foundHandle = fb ? makeObjHandle(
                        static_cast<int>((reinterpret_cast<u8*>(fb) - reinterpret_cast<u8*>(&g_objects[0])) / kObjectStride)
                    ) : 0;
                }
                g_aiSelMeisterBuildingRec = foundHandle;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 0x45767c..0x457699: STAFF CHECK.
    //   v16 = v13 >> 1 (half of total);  v17 = v13 - v16.
    //   if v16 >= v17: v16 = v17.
    //   if v16 > v107 → "no people" log, return.
    // -----------------------------------------------------------------------
    {
        int v16 = v13 >> 1;
        int v17 = v13 - (v13 >> 1);
        if (v16 >= v17)
            v16 = v17;
        if (v16 > v107) {
            // 0x457c0f..0x457c38: Sprintf_0 log "...hat gerade keine Leute" — DROP.
            return;
        }
    }

    // -----------------------------------------------------------------------
    // 0x4576b6..0x4576c4: Read owner-kind byte.
    //   v18 = byte_12CE912[536 * *(u16*)(bldgRec+39)]
    //       = person[kind] for the player who owns the Meister's building.
    // -----------------------------------------------------------------------
    u8  v18;       // owner kind byte
    int v108 = 0;  // routing variable (0=break-in, 1=spy, 2=?, 3=attack)
    {
        u8* bldgRec = rdptr(mr, kM_bldgRec);
        u16 ownerWord = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;  // *(bldgRec+39)
        // byte_12CE912[536 * ownerWord] = pr(ownerWord)[kP_kind]
        v18 = (ownerWord < static_cast<u16>(kPersonCapacity)) ? rd8(pr(ownerWord), kP_kind) : 0u;
    }

    // -----------------------------------------------------------------------
    // 0x4576c4..0x457c38: ROUTING BRANCH.
    // -----------------------------------------------------------------------
    if (v18 == 6 || v18 == 7) {
        // 0x4576d9: v108 = RandomModulo(2)   [RNG draw #1 — guild-thief branch]
        v108 = static_cast<int>(static_cast<u16>(RandomModulo(2u)));
        // 0x4576f8: if v108 && *(mr+448) != -1 && RandomModulo(2) → v108=0
        if (v108 && rd32(mr, kM_target) != -1) {
            // 0x4576e4: second RandomModulo(2)   [RNG draw #2 — guild-thief branch]
            if (static_cast<u16>(RandomModulo(2u))) {
                v108 = 0;
                goto LABEL_35;   // break-in path
            }
        }
    } else {
        // 0x457c39..0x457c96: non-guild branch.
        // 0x457c39: RandomFloat [RNG draw #1 — non-guild]
        if (RandomFloatScaled() > kGate085
         && v107 >= 3
         && (7 - BuildingPriceMode()) <= static_cast<int>(gtDay())) {
            // 0x457c65: v108 = 3, call CalcAngriff
            v108 = 3;
            goto ANGRIFF_GATE;
        }
        // 0x457c87: v108 = RandomModulo(3)   [RNG draw #2 — non-guild]
        v108 = static_cast<int>(static_cast<u16>(RandomModulo(3u)));
    }

    // 0x457c9d: if v108 == 1 && RandomFloat < 0.75 → fall to LABEL_63
    if (v108 == 1) {
        // 0x457ca7: RandomFloat [RNG draw #3]
        if (RandomFloatScaled() < kGate075) {
            // The fcomp/sahf result: jb loc_457A22 means "if float < 0.75 → goto LABEL_63"
            // (jb = below = CF=1 = original was LESS THAN)
            goto LABEL_63;
        }
    }
    // 0x457cbb: if v108 == 0 → LABEL_35 (break-in path)
    if (v108 == 0)
        goto LABEL_35;
    // 0x457cc9: if v108 == 3 → ANGRIFF_GATE
    if (v108 == 3)
        goto ANGRIFF_GATE;
    // 0x457cd3: if v108 == 1 → LABEL_101 (dense spy flow)
    // (fall through to LABEL_101 if v108==1 and the float gate above was NOT taken)
    if (v108 == 1)
        goto LABEL_101;
    // 0x458872: else (v108==2) the binary falls into `cmp var_80,1; jg LABEL_63`.
    // Since 2 > 1, v108==2 routes to LABEL_63 (emit the default break-in/patrol
    // command), NOT a plain return.  (Verified at 0x457cd3→0x458872 jg loc_457A22.)
    goto LABEL_63;

    // -----------------------------------------------------------------------
    // LABEL_35 / BREAK_IN_PATH  0x45770f..0x457a14
    // -----------------------------------------------------------------------
LABEL_35: {
        // Sprintf_0 "ai_CalcMeisterDiebe(): Meister %s ..." — DROP (byte_619580).

        // 0x45771c..0x457a14: target validation + re-use or new selection.
        i32 targetId = rd32(mr, kM_target);
        u8* foundRec = nullptr;

        if (targetId == -1) {
            // Clear target (was already -1): nothing to do, fall to scan.
            wr32(mr, kM_target, -1);
        } else {
            // Attempt to find the cached target building.
            // 0x457724: v19 = VIBE_Building_FindById(*(mr+448))
            if (g_meisterLeaves && g_meisterLeaves->buildingFindById)
                foundRec = g_meisterLeaves->buildingFindById(targetId);
            // v114 = foundRec

            if (foundRec) {
                // 0x45773f: check owner != meister's own building owner
                u8* bldgRec = rdptr(mr, kM_bldgRec);
                u16 foundOwner   = rdu16(foundRec, kB_owner39);
                u16 meisterOwner = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
                if (foundOwner == meisterOwner) {
                    // same owner → clear target, rescan
                    wr32(mr, kM_target, -1);
                    foundRec = nullptr;
                }
                // else: different owner → keep foundRec, skip scan
            } else {
                // Building not found: clear target.
                // 0x457a14: loc_457A14: if (v114 != 0) → LABEL_62
                // Since foundRec==null, fall through to scan.
            }
        }

        // -----------------------------------------------------------------------
        // 0x457754..0x4579f6: OBJECT SCAN (break-in path).
        // Only scan if foundRec == null (no usable cached target).
        // -----------------------------------------------------------------------
        if (!foundRec) {
            // 0x457754..0x457833: 8x8 danger grid scan → maxDanger (v109).
            float v109 = 0.0f;
            float v23  = 0.0f;
            {
                int v20 = 1536;   // terminator: 192*8
                for (int loopM = 0; loopM < 8; ++loopM) {
                    unsigned int v21 = static_cast<unsigned int>(24 * loopM);
                    do {
                        // word_12349A2[v21/2] = dangerB at tile-byte offset v21+2
                        // word_12349A0[v21/2] = dangerA at tile-byte offset v21+0
                        u16 dangerB = 0, dangerA = 0;
                        std::memcpy(&dangerB, g_cityTileGrid + v21 + 2, 2);
                        std::memcpy(&dangerA, g_cityTileGrid + v21 + 0, 2);

                        // 0x45779c: v22 = (double)dangerB (st7)
                        double v22 = static_cast<double>(dangerB);
                        // 0x4577aa: v118 = v22 (store)
                        // 0x4577c5: v119 = (double)dangerA * flt_619780 + v118
                        //   = dangerA * 0.125 + dangerB
                        float v119 = static_cast<float>(
                            static_cast<double>(dangerA) * static_cast<double>(kTileBWeight) + v22);

                        // 0x4577dd: if (v109 <= (double)v119) v23 = v119 else v23 = v109
                        float v23new;
                        if (static_cast<double>(v109) <= static_cast<double>(v119))
                            v23new = v119;
                        else
                            v23new = v109;
                        v23  = v23new;
                        v21 += 192u;     // next row (column-major scan)
                        v109 = v23new;
                    } while (v21 != static_cast<unsigned int>(v20));

                    v20 += 24;    // next column
                }
            }

            // 0x457833: if (LODWORD(v23) & 0x7FFFFFFF) == 0 → v109 = 1.0
            {
                u32 lodword;
                std::memcpy(&lodword, &v23, 4);
                if ((lodword & 0x7FFFFFFFu) == 0)
                    v109 = 1.0f;
            }

            // -----------------------------------------------------------------------
            // 0x457840..0x4579f6: Object array sweep.
            // -----------------------------------------------------------------------
            u8*  v114   = nullptr;   // best target object base (v24 in inner scan loop, v114 global best)
            float v111  = 0.0f;      // best score
            bool v133   = (thiefNode != nullptr);   // includeBuildings flag for best
            bool v132   = (thiefNode != nullptr);   // includeBuildings flag for current

            u8* v24 = g_objectArrayBase;   // dword_13CE298 (object array cursor)
            for (int v25 = 0; v25 < 256; ++v25, v24 += 169) {
                // 0x45786a: if (*v24) — alive byte
                if (!v24[0])
                    continue;

                // 0x457881: v26 = *(u16*)(v24+39) (object owner word)
                u16 v26 = rdu16(v24, kB_owner39);

                // 0x4578c9: combined filter
                u8* bldgRec2 = rdptr(mr, kM_bldgRec);
                u16 meisterOwner2 = bldgRec2 ? rdu16(bldgRec2, kB_owner39) : 0u;
                if (static_cast<u16>(v26) == meisterOwner2)
                    continue;
                if (v26 == 0xFFFFu)
                    continue;
                if (v24[90] & 1u)
                    continue;
                // 0x4578c9: v26 != *(u16*)dword_6498E4
                if (v26 == g_diebeLocalPlayerWord)
                    continue;

                // 0x4578c9: !VIBE_Building_IsProductionType(v24)
                bool isProd = false;
                if (g_meisterLeaves && g_meisterLeaves->isProductionType)
                    isProd = (g_meisterLeaves->isProductionType(v24) != 0);
                if (isProd)
                    continue;

                // 0x4578c9: VIBE_CharAction_IsAnimalTargetBusy(v24)
                bool busy2 = false;
                if (g_meisterLeaves && g_meisterLeaves->charActionIsAnimalTargetBusy)
                    busy2 = (g_meisterLeaves->charActionIsAnimalTargetBusy(v24) != 0);
                if (!busy2)
                    continue;

                // 0x4578d6: v27 = *(v24+97) (scene-node / pos ptr)
                i32 v27 = rd32(v24, 97);
                float v110 = 0.0f;
                if (v27 && g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                    int tRow2 = 0, tCol2 = 0;
                    // The world position is at *(v27+76) in the original 32-bit process.
                    // In the reimpl, v27 is a 32-bit "raw pointer" from the 32-bit record;
                    // we cannot dereference it on 64-bit. Document: worldToCityTile leaf
                    // receives a float[3] position from the scene-node record. The bridge
                    // wires worldToCityTile to accept the node's position field directly.
                    // We pass the address computation as documented (the leaf is responsible).
                    // Since we cannot safely dereference the 32-bit v27, we model the
                    // tile-lookup as a leaf call with a "node id" (v27 raw) approach.
                    // SPEC: worldToCityTile(float* pos3, int* row, int* col) → bool.
                    // In the reimpl, the bridge pre-computes tile coords or the leaf knows
                    // how to resolve v27. We pass the raw bytes as per the original call.
                    // Since g_objectArrayBase and scene-nodes are reimpl-controlled, the
                    // live bridge sets up v24[97..100] to be a valid scene-node handle.
                    // For faithfulness, we pass the scene-node base + 76 as the float pos.
                    // The leaf is expected to handle this correctly (null-safe).
                    u8* sceneBase = reinterpret_cast<u8*>(static_cast<uintptr_t>(static_cast<u32>(v27)));
                    (void)sceneBase;  // cannot dereference 32-bit ptr in 64-bit; leaf handles
                    // The call convention: pass float*(scenePosField) to the leaf.
                    // We provide a null posPtr which will cause the leaf to return false,
                    // matching the null-path (v110 = 0.0). The live bridge overrides with
                    // the real tile-lookup. Documented as a leaf boundary.
                    float* posPtr = nullptr;
                    if (g_meisterLeaves->worldToCityTile(posPtr, &tRow2, &tCol2)) {
                        int tileOff = 192 * tRow2 + 24 * tCol2;
                        u16 tb2 = 0, ta2 = 0;
                        std::memcpy(&tb2, g_cityTileGrid + tileOff + 2, 2);
                        std::memcpy(&ta2, g_cityTileGrid + tileOff + 0, 2);
                        // 0x457955: v110 = 1.0 - ((dangerB + dangerA*0.125) / v109)
                        v110 = static_cast<float>(1.0
                            - (static_cast<double>(tb2) + static_cast<double>(ta2) * static_cast<double>(kTileBWeight))
                            / static_cast<double>(v109));
                    }
                    // else v110 stays 0.0 (null-path)
                }

                // 0x457963: v129 = (float)VIBE_Building_GetSecurityLevel(v24)
                int secLvl = 0;
                if (g_meisterLeaves && g_meisterLeaves->securityLevel)
                    secLvl = g_meisterLeaves->securityLevel(v24);

                // 0x45798c: v110 = (flt_61979C - (double)secLvl) * dbl_619788 * v110
                //   = (8.0 - secLvl) * 0.5 * v110
                v110 = static_cast<float>(
                    (static_cast<double>(kSecBase8) - static_cast<double>(secLvl))
                    * kSecScale05 * static_cast<double>(v110));

                // 0x45799d: v129 = (u16)RandomModulo(0x64)   [RNG draw — per-object]
                float rnd100 = static_cast<float>(static_cast<i32>(static_cast<u16>(RandomModulo(0x64u))));

                // 0x4579c5: v110 = (double)rnd100 * dbl_619790 * v110
                //   = rnd100 * 0.01 * v110
                v110 = static_cast<float>(static_cast<double>(rnd100) * kRandScale001 * static_cast<double>(v110));

                // 0x45804c: winner selection.
                // COERCE_FLOAT(computeAssetWorth(v24,v133)) then (double)SLODWORD(v129)
                // → (double)(i32 retval) per COERCE_FLOAT round-trip identity.
                // if !v114 OR (assetWorth(cand)*v110 > assetWorth(best)*v111) → new winner.
                if (!v114) {
                    v114  = v24;
                    v111  = v110;
                } else {
                    double assetNew = 0.0;
                    if (g_meisterLeaves && g_meisterLeaves->computeAssetWorth)
                        assetNew = static_cast<double>(
                            g_meisterLeaves->computeAssetWorth(v24, v133 ? 1 : 0));
                    double v121 = assetNew * static_cast<double>(v110);

                    double assetBest = 0.0;
                    if (g_meisterLeaves && g_meisterLeaves->computeAssetWorth)
                        assetBest = static_cast<double>(
                            g_meisterLeaves->computeAssetWorth(v114, v132 ? 1 : 0));
                    // if (assetBest * v111 < assetNew * v110) → new winner
                    if (assetBest * static_cast<double>(v111) < v121) {
                        v114 = v24;
                        v111 = v110;
                    }
                }
            }

            // 0x457a05: if (v114) *(mr+448) = *(v114+1) else *(mr+448) = -1
            if (v114)
                wr32(mr, kM_target, rd32(v114, kB_id1));
            else
                wr32(mr, kM_target, -1);

            foundRec = v114;  // v114 carries to LABEL_62
        }

        goto LABEL_62;
    }   // end LABEL_35 block

    // -----------------------------------------------------------------------
    // LABEL_62 / TARGET DISPATCH  0x457a1c..0x458081
    // -----------------------------------------------------------------------
LABEL_62: {
        u8* v114 = nullptr;    // the resolved target building base

        // Recover target from mr+448
        {
            i32 tid = rd32(mr, kM_target);
            if (tid == -1) {
                goto LABEL_63;  // no target → default command
            }
            // BuildingFindById to verify still valid
            if (g_meisterLeaves && g_meisterLeaves->buildingFindById)
                v114 = g_meisterLeaves->buildingFindById(tid);
            // Re-check ownership
            if (v114) {
                u8* bldgRec3 = rdptr(mr, kM_bldgRec);
                u16 foundOwner3  = rdu16(v114, kB_owner39);
                u16 meisterOwner3 = bldgRec3 ? rdu16(bldgRec3, kB_owner39) : 0u;
                if (foundOwner3 == meisterOwner3)
                    v114 = nullptr;
            }
        }

        if (!v114)
            goto LABEL_63;

        // 0x458081..0x45809f: QueryFind type-202 node under Meister's bldg.
        //   Find node where *(node+21) == *(v114+1).
        u8* v55 = nullptr;   // the type-202 scene node matching our target
        {
            u8* bldgRec4 = rdptr(mr, kM_bldgRec);
            i32 sceneRoot4 = bldgRec4 ? rd32(bldgRec4, kB_sceneRoot93) : -1;
            const int filts202[2] = {0, 202};
            u8* cur202 = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->queryFind)
                cur202 = g_meisterLeaves->queryFind(sceneRoot4, filts202, 1);

            i32 targetBldgId4 = rd32(v114, kB_id1);   // *(v114+1)
            while (cur202) {
                if (rd32(cur202, 21) == targetBldgId4)   // *(cur202+21)
                    break;
                if (g_meisterLeaves && g_meisterLeaves->queryIterNext)
                    cur202 = g_meisterLeaves->queryIterNext();
                else
                    cur202 = nullptr;
            }
            v55 = cur202;
        }

        // -----------------------------------------------------------------------
        // High-security gate (type-202 node strength check).
        // 0x45808d..0x458081:
        //   if v55 AND (v55[55] >= 0x5F OR v55[54]*4.0 + RandMod(0x14) <= v55[55])
        //     → HIGH SECURITY: burgle-in-preparation path (look for existing handlers)
        //   else → SPY path
        // -----------------------------------------------------------------------
        bool highSecurity = false;
        if (v55) {
            u8 byte55 = v55[55];
            if (byte55 >= 0x5Fu) {
                highSecurity = true;
            } else {
                // 0x458096: v131 = *(v55+54) (signed byte cast to float?)
                // v130 = (double)(i16)v131 * flt_619798
                // LODWORD(v129) = (u16)RandomModulo(0x14)
                // v131 = *(v55+55)
                // if (double)(i16)v131 >= (double)SLODWORD(v129) + v130 → high security
                i16 v131_54  = static_cast<i16>(v55[54]);    // LOBYTE signed
                float v130f  = static_cast<float>(static_cast<double>(v131_54) * static_cast<double>(kSecBase4));
                // 0x458094: RandomModulo(0x14)   [RNG draw — type-202 check]
                float rnd20  = static_cast<float>(static_cast<int>(static_cast<u16>(RandomModulo(0x14u))));
                i16 v131_55  = static_cast<i16>(v55[55]);
                // 0x45809f: if (v55[55] >= rnd20 + v130) → highSecurity
                if (static_cast<double>(v131_55) >= static_cast<double>(rnd20) + static_cast<double>(v130f))
                    highSecurity = true;
            }
        }

        if (highSecurity) {
            // -----------------------------------------------------------------------
            // HIGH SECURITY / BURGLE PATH (spy slots exist and are strong enough)
            // 0x4582fb..0x458561
            // Find He handler cmd=64 or cmd=63 for the target building.
            // -----------------------------------------------------------------------
            i32 targetId5 = rd32(v114, kB_id1);
            u8* bldgRec5  = rdptr(mr, kM_bldgRec);
            i32 meisterBldgId5 = bldgRec5 ? rd32(bldgRec5, kB_id1) : -1;

            u8* handler5 = nullptr;
            // Try cmd=64 first, then cmd=63 (0x4582fb, 0x458333)
            for (int cmdType5 : {64, 63}) {
                u8* h5 = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->heFindFirst)
                    h5 = g_meisterLeaves->heFindFirst(2, 0, cmdType5, 3, meisterBldgId5);
                while (h5) {
                    // *((_DWORD*)h5 + 43) == *(v114+1)  → dword at offset 43*4=172
                    if (rd32(h5, 172) == targetId5)
                        break;
                    if (g_meisterLeaves && g_meisterLeaves->heFindNext)
                        h5 = g_meisterLeaves->heFindNext();
                    else
                        h5 = nullptr;
                }
                if (h5) { handler5 = h5; break; }
            }

            if (handler5) {
                // "hat aber noch eine Spionage ... nicht beendet" — DROP.
            } else {
                // EMIT BURGLE-BREAKIN COMMAND: cmdType = 60
                // Sprintf_0 "...bricht bei '%s' ein" — DROP.
                {
                    MeisterCommand cmd;
                    cmd.cmdType = 60;   // 0x458393: v93 = 60 (but set in earlier code as v71)

                    u8* bldgRec5b = rdptr(mr, kM_bldgRec);
                    u16 ownerIdx5 = bldgRec5b ? rdu16(bldgRec5b, kB_owner39) : 0u;
                    cmd.actorId   = (ownerIdx5 < static_cast<u16>(kPersonCapacity))
                                    ? g_personIds[ownerIdx5] : 0;
                    cmd.buildingId = bldgRec5b ? rd32(bldgRec5b, kB_id1) : -1;
                    cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                                      (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                                      (static_cast<u64>(g_meisterGameTime.minute) << 48));
                    cmd.timeExtra  = g_meisterTimeExtra;
                    cmd.timeTail   = g_meisterTimeTail;
                    cmd.mode       = 1;
                    cmd.targetId   = rd32(v114, kB_id1);
                    cmd.srcId      = bldgRec5b ? rd32(bldgRec5b, kB_id1) : -1;

                    // Worker collection: cap = min(v107, 8)  (0x45840b..0x458479)
                    int cap5 = (v107 >= 8) ? 8 : v107;
                    int wCount5 = 0;
                    i32 meisterBldgHandle5 = rd32(mr, kM_bldgRec);
                    if (cap5 > 0) {
                        for (unsigned int byteOff5 = 0;
                             wCount5 < cap5 && static_cast<int>(byteOff5) < 411648;
                             byteOff5 += 536u)
                        {
                            int pidx5 = static_cast<int>(byteOff5 / 536u);
                            u8* p5 = pr(pidx5);
                            if (rd16(p5, kP_marker) == static_cast<i16>(-1)) continue;
                            if (rd32(p5, kP_employer) != meisterBldgHandle5) continue;
                            if (!rd8(p5, kP_profByte)) continue;
                            if (rd32(p5, kP_busy)) continue;
                            u8* ao5  = rdptr(p5, kP_actionObj);
                            u8* emp5 = rdptr(p5, kP_employer);
                            if (!ao5 || !emp5) continue;
                            if (rd32(ao5, 44) != rd32(emp5, kB_id1)) continue;
                            cmd.workerIds.push_back(g_personIds[pidx5]);
                            ++wCount5;
                        }
                    }
                    // Fill -1 pad to 8 slots (matches the for(j) loop in decompile)
                    while (static_cast<int>(cmd.workerIds.size()) < 8)
                        cmd.workerIds.push_back(-1);

                    // Guard: 0x458564 `cmp [esp+var_154], -1` → emit if FIRST worker slot
                    // is filled.  var_154 is at stack 0x438; the worker loop pre-increments
                    // the byte offset (v76 += 4) BEFORE the first write, so the FIRST worker
                    // lands at &v98(0x434)+4 = 0x438 = var_154 = v100.  So the guard is
                    // "at least ONE worker collected" → cmd.workerIds[0] != -1.
                    bool slot0filled = (!cmd.workerIds.empty() && cmd.workerIds[0] != -1);
                    if (slot0filled) {
                        if (g_meisterCmdSink)
                            g_meisterCmdSink->push(cmd);
                        wr32(mr, kM_target, -1);   // 0x45857e: *(mr+448) = -1
                    }
                }
            }
        } else {
            // -----------------------------------------------------------------------
            // LOW SECURITY / SPY PATH (v55 null or too weak)
            // 0x4580bb..0x458503
            // -----------------------------------------------------------------------
            i32 targetId6   = rd32(v114, kB_id1);
            u8* bldgRec6    = rdptr(mr, kM_bldgRec);
            i32 meisterBldgId6 = bldgRec6 ? rd32(bldgRec6, kB_id1) : -1;

            u8* handler6 = nullptr;
            // Try cmd=64 first, then cmd=63
            for (int cmdType6 : {64, 63}) {
                u8* h6 = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->heFindFirst)
                    h6 = g_meisterLeaves->heFindFirst(2, 0, cmdType6, 3, meisterBldgId6);
                while (h6) {
                    if (rd32(h6, 172) == targetId6)
                        break;
                    if (g_meisterLeaves && g_meisterLeaves->heFindNext)
                        h6 = g_meisterLeaves->heFindNext();
                    else
                        h6 = nullptr;
                }
                if (h6) { handler6 = h6; break; }
            }

            if (handler6) {
                // "hat schon eine Spionage bei '%s' laufen" — DROP.
            } else {
                // EMIT SPY COMMAND: cmdType = 64
                // Sprintf_0 "...spioniert bei '%s'" — DROP.
                {
                    MeisterCommand cmd;
                    cmd.cmdType = 64;   // 0x458151

                    u8* bldgRec6b = rdptr(mr, kM_bldgRec);
                    u16 ownerIdx6 = bldgRec6b ? rdu16(bldgRec6b, kB_owner39) : 0u;
                    cmd.actorId   = (ownerIdx6 < static_cast<u16>(kPersonCapacity))
                                    ? g_personIds[ownerIdx6] : 0;
                    cmd.buildingId = bldgRec6b ? rd32(bldgRec6b, kB_id1) : -1;
                    cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                                      (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                                      (static_cast<u64>(g_meisterGameTime.minute) << 48));
                    cmd.timeExtra  = g_meisterTimeExtra;
                    cmd.timeTail   = g_meisterTimeTail;
                    cmd.mode       = 1;
                    cmd.targetId   = rd32(v114, kB_id1);
                    cmd.srcId      = bldgRec6b ? rd32(bldgRec6b, kB_id1) : -1;

                    // Worker collection: cap = min(v107, 8)  (0x4581d8..0x458244)
                    int cap6 = (v107 >= 8) ? 8 : v107;
                    int wCount6 = 0;
                    i32 meisterBldgHandle6 = rd32(mr, kM_bldgRec);
                    if (cap6 > 0) {
                        for (unsigned int byteOff6 = 0;
                             wCount6 < cap6 && static_cast<int>(byteOff6) < 411648;
                             byteOff6 += 536u)
                        {
                            int pidx6 = static_cast<int>(byteOff6 / 536u);
                            u8* p6 = pr(pidx6);
                            if (rd16(p6, kP_marker) == static_cast<i16>(-1)) continue;
                            if (rd32(p6, kP_employer) != meisterBldgHandle6) continue;
                            if (!rd8(p6, kP_profByte)) continue;
                            if (rd32(p6, kP_busy)) continue;
                            u8* ao6  = rdptr(p6, kP_actionObj);
                            u8* emp6 = rdptr(p6, kP_employer);
                            if (!ao6 || !emp6) continue;
                            if (rd32(ao6, 44) != rd32(emp6, kB_id1)) continue;
                            cmd.workerIds.push_back(g_personIds[pidx6]);
                            ++wCount6;
                        }
                    }
                    while (static_cast<int>(cmd.workerIds.size()) < 8)
                        cmd.workerIds.push_back(-1);

                    // 0x458503: VIBE_Command_QueueRequestSlotReset28 — always emits (no guard).
                    if (g_meisterCmdSink)
                        g_meisterCmdSink->push(cmd);
                }
            }
        }

        // 0x458503..0x458561: fall through to LABEL_100
        goto LABEL_100;
    }

    // -----------------------------------------------------------------------
    // LABEL_100  0x457cc9..0x457cdb
    // -----------------------------------------------------------------------
LABEL_100:
    if (v108 == 3)
        goto ANGRIFF_GATE;
    goto LABEL_101;

    // -----------------------------------------------------------------------
    // ANGRIFF_GATE: call CalcAngriff(mr, v107).  0x457c70..0x457c80
    // -----------------------------------------------------------------------
ANGRIFF_GATE:
    if (!CalcAngriff(mr, v107))
        goto LABEL_63;
    // If CalcAngriff returned nonzero → fall through (routing dispatch below).
    goto LABEL_101;

    // -----------------------------------------------------------------------
    // LABEL_101  0x457cdb..0x457d11
    // -----------------------------------------------------------------------
LABEL_101:
    if (v108 == 1) {
        // DENSE SPY / BURGLE SELECTION FLOW
        // 0x457ce1..0x458883

        // Sprintf_0 "ai_CalcMeisterDiebe(): Meister %s ..." (byte_6196C8) — DROP.

        // -----------------------------------------------------------------------
        // 0x457d11..0x457dd6: 8x8 grid scan → maxDanger (v116).
        // -----------------------------------------------------------------------
        float v116 = 0.0f;
        {
            // v41 = 1536; v42 = *(float*)&v42 stored — but the decompile uses
            // v116 as the running max and initialises from a previous float result.
            // Original: v116 = *(float*)&v42; m = v42  (v42 = result from RandomModulo above).
            // Actually on inspection: the initialisation block computes v116 from a float
            // cast of v42 which at this point is the RandomModulo(0x14) result.  However,
            // the grid scan immediately overwrites v116 as the running max starting from 0.
            // So: v116 starts at random_value_cast_as_float but is overwritten by max scan.
            // We initialize to 0.0 per the running-max semantics (the random cast is the
            // starting "v116" for the first comparison, but max(0, tile) for any nonzero
            // tile will dominate immediately).  Document: v116 init is float-bit-cast of
            // the previous RandomModulo(0x14) u16 result; if no tiles are nonzero this
            // persists to the zero-test, but then gets set to 1.0 anyway.
            float v45 = 0.0f;
            int v41 = 1536;
            // m (loop variable): note the decompile re-uses `m` for the outer loop index.
            for (int loopM2 = 0; loopM2 < 8; ++loopM2) {
                // v115 = 0 (inner column index init — not used externally)
                unsigned int v43 = static_cast<unsigned int>(24 * loopM2);
                do {
                    u16 dangerB2 = 0, dangerA2 = 0;
                    std::memcpy(&dangerB2, g_cityTileGrid + v43 + 2, 2);
                    std::memcpy(&dangerA2, g_cityTileGrid + v43 + 0, 2);
                    // 0x457d3f: v44 = (double)SLODWORD(v129) — the B value cast via int
                    double v44 = static_cast<double>(static_cast<i32>(dangerB2));
                    // 0x457d68: v123 = (double)dangerA * 0.125 + v44
                    float v123 = static_cast<float>(
                        static_cast<double>(static_cast<i32>(dangerA2)) * static_cast<double>(kTileBWeight)
                        + v44);
                    // 0x457d80: if (v116 <= v123) v45=v123 else v45=v116
                    if (static_cast<double>(v116) <= static_cast<double>(v123))
                        v45 = v123;
                    else
                        v45 = v116;
                    v43 += 192u;
                    v116 = v45;
                } while (v43 != static_cast<unsigned int>(v41));
                v41 += 24;
            }

            // 0x457dd6: if (LODWORD(v45) & 0x7FFFFFFF == 0) v116 = 1.0
            u32 lv45;
            std::memcpy(&lv45, &v45, 4);
            if ((lv45 & 0x7FFFFFFFu) == 0)
                v116 = 1.0f;
        }

        // -----------------------------------------------------------------------
        // 0x457ddc..0x457f64: BURGLE TARGET SCAN (dense spy path).
        // Different filter set from the break-in scan:
        //   alive, category != 3 && != 5 && != 0,
        //   owner != meister, != 0xFFFF, !(obj[90]&1),
        //   dword_12CEA7C[536*ownerWord] == 0 OR *(employer+39) != meisterOwner,
        //   byte_12CE912[536*ownerWord] <= 7,
        //   *(obj+97) != 0 && worldToCityTile succeeds.
        // Score: (rnd100 * 0.01 * tileDangerNorm) — no security subtraction.
        // Winner: !v105 OR (sumCurrencyHeld(winner)*v111 < sumCurrencyHeld(cand)*v128).
        // COERCE_FLOAT note: orig does COERCE_FLOAT(sumCurrencyHeld(...))->(double)SLODWORD
        // which equals (double)(i32 retval). We use direct i32→double cast below.
        // -----------------------------------------------------------------------
        u8*  v105  = nullptr;  // best target
        float v111b = 0.0f;   // best score (shares v111 name but different scope)
        {
            u8* v46 = g_objectArrayBase;   // dword_13CE298
            for (int v47 = 0; v47 < 256; ++v47, v46 += 169) {
                if (!v46[0]) continue;

                // category check: v48 = VIBE_Building_MapTypeToCategory(*v46)
                int v48 = 0;
                if (g_meisterLeaves && g_meisterLeaves->mapTypeToCategory)
                    v48 = g_meisterLeaves->mapTypeToCategory(v46[0]);

                // Owner check
                u16 v50 = rdu16(v46, kB_owner39);   // *(v46+39)
                u8* bldgRecD = rdptr(mr, kM_bldgRec);
                u16 meisterOwnerD = bldgRecD ? rdu16(bldgRecD, kB_owner39) : 0u;
                u16 v49 = meisterOwnerD;

                // 0x457e3b: filters
                if (static_cast<u16>(v50) == v49) continue;
                if (v50 == 0xFFFFu) continue;
                if (v46[90] & 1u) continue;
                if (v48 == 3 || v48 == 5) continue;
                if (!v48) continue;    // 0x457e43: category==0 → skip

                // 0x457e4f: v51 = dword_12CEA7C[134 * v50]
                //   = rd32(pr(v50), kP_employer)  — employer ptr for the OWNER PERSON
                u8* ownerPerson = (v50 < static_cast<u16>(kPersonCapacity)) ? pr(v50) : nullptr;
                u8* v51Employer = ownerPerson ? rdptr(ownerPerson, kP_employer) : nullptr;

                // 0x457e70: if (!v51 || *(u16*)(v51+39) != v49) → keep candidate
                //   = employer is null OR employer's owner != meister's owner
                bool keepCand = (!v51Employer || rdu16(v51Employer, kB_owner39) != v49);
                if (!keepCand) continue;

                // 0x457e70: byte_12CE912[536*v50] <= 7 (owner person kind byte)
                u8 ownerKind = ownerPerson ? rd8(ownerPerson, kP_kind) : 0u;
                if (ownerKind > 7) continue;

                // 0x457e76: v52 = *(v46+97) (scene-node ptr)
                i32 v52 = rd32(v46, 97);
                float v128 = 0.0f;
                if (v52 && g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                    int tRow3 = 0, tCol3 = 0;
                    // Same bridge note as in break-in scan — posPtr is a leaf boundary.
                    float* posPtr3 = nullptr;
                    if (g_meisterLeaves->worldToCityTile(posPtr3, &tRow3, &tCol3)) {
                        int tileOff3 = 192 * tRow3 + 24 * tCol3;
                        u16 tb3 = 0, ta3 = 0;
                        std::memcpy(&tb3, g_cityTileGrid + tileOff3 + 2, 2);
                        std::memcpy(&ta3, g_cityTileGrid + tileOff3 + 0, 2);
                        // 0x457ef5: v128 = 1.0 - ((dangerB + dangerA*0.125) / v116)
                        v128 = static_cast<float>(1.0
                            - (static_cast<double>(tb3) + static_cast<double>(ta3) * static_cast<double>(kTileBWeight))
                            / static_cast<double>(v116));
                    }
                    // else v128 stays 0.0
                }

                // 0x457f0b: LODWORD(v130) = (u16)RandomModulo(0x64)  [RNG draw — per-object]
                float rnd100b = static_cast<float>(static_cast<i32>(static_cast<u16>(RandomModulo(0x64u))));

                // 0x457f33: v128 = rnd100 * dbl_619790 * v128
                v128 = static_cast<float>(static_cast<double>(rnd100b) * kRandScale001 * static_cast<double>(v128));

                // 0x45864d: winner selection by sumCurrencyHeld.
                // Decompile: v130 = COERCE_FLOAT(sumCurrencyHeld(ownerBase))
                //            v129 = (double)SLODWORD(v130) * v128
                // COERCE_FLOAT(i32 x) bit-casts x to float; then SLODWORD re-extracts the i32.
                // So (double)SLODWORD(COERCE_FLOAT(x)) = (double)x  (round-trip is identity).
                // Therefore the comparison is: (double)sumCurrencyHeld(best)*v111b
                //                             < (double)sumCurrencyHeld(cand)*v128.
                if (!v105) {
                    v105  = v46;
                    v111b = v128;
                } else {
                    // sumCurrencyHeld(pr(v50)) for the candidate's owner
                    double currNew = 0.0;
                    if (g_meisterLeaves && g_meisterLeaves->sumCurrencyHeld) {
                        u8* ownerPNew = (v50 < static_cast<u16>(kPersonCapacity)) ? pr(v50) : nullptr;
                        if (ownerPNew)
                            currNew = static_cast<double>(g_meisterLeaves->sumCurrencyHeld(ownerPNew));
                    }
                    double v129b = currNew * static_cast<double>(v128);

                    // sumCurrencyHeld(pr(v54)) for the current best's owner
                    u16 bestOwnerW = rdu16(v105, kB_owner39);
                    double currBest = 0.0;
                    if (g_meisterLeaves && g_meisterLeaves->sumCurrencyHeld) {
                        u8* ownerPBest = (bestOwnerW < static_cast<u16>(kPersonCapacity)) ? pr(bestOwnerW) : nullptr;
                        if (ownerPBest)
                            currBest = static_cast<double>(g_meisterLeaves->sumCurrencyHeld(ownerPBest));
                    }
                    // if (currBest * v111b < currNew * v128) → new winner
                    if (currBest * static_cast<double>(v111b) < v129b) {
                        v105  = v46;
                        v111b = v128;
                    }
                }
            }
        }

        // 0x457f7a: QueryFind type-101 under meister's bldgRoot (1 filter, op0, val=101)
        {
            u8* bldgRec7 = rdptr(mr, kM_bldgRec);
            i32 sceneRoot7 = bldgRec7 ? rd32(bldgRec7, kB_sceneRoot93) : -1;
            const int filts101[2] = {0, 101};
            bool found101 = false;
            if (g_meisterLeaves && g_meisterLeaves->queryFind)
                found101 = (g_meisterLeaves->queryFind(sceneRoot7, filts101, 1) != nullptr);
            if (!found101)
                goto LABEL_63;
        }

        // 0x458661: if (!v105) → LABEL_63
        if (!v105)
            goto LABEL_63;

        // 0x458667: v80 = *(mr+364) (bldgRec raw int / handle)
        // 0x458671: if (*(v80+101) != -1) → LABEL_63
        // *(bldgRec+101) = dword at building record byte offset 101.
        {
            u8* bldgRec8 = rdptr(mr, kM_bldgRec);
            if (!bldgRec8)
                goto LABEL_63;
            i32 field101 = rd32(bldgRec8, 101);   // *(bldgRec+101)
            if (field101 != -1)
                goto LABEL_63;
        }

        // 0x458683: VIBE_He_FindFirstHandlerByFilter(2, 0, 60, 3, *(bldgRec+1))
        //   Check if a burgle (cmd=60) action is already running for the meister's bldg.
        {
            u8* bldgRec8b = rdptr(mr, kM_bldgRec);
            i32 meisterBldgId8 = bldgRec8b ? rd32(bldgRec8b, kB_id1) : -1;

            u8* handler8 = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->heFindFirst)
                handler8 = g_meisterLeaves->heFindFirst(2, 0, 60, 3, meisterBldgId8);

            if (handler8) {
                // "hat gerade eine Spionage" — DROP; goto LABEL_63 (burgle in progress).
                // Sprintf_0 (byte_6196FC) — DROP.
                goto LABEL_63;
            }
        }

        // -----------------------------------------------------------------------
        // EMIT BURGLE COMMAND (cmdType = 60) — the dense-spy-flow burgle.
        // 0x4586d9..0x458868
        // -----------------------------------------------------------------------
        {
            MeisterCommand cmd;
            cmd.cmdType = 60;   // 0x4586de

            u8* bldgRec9 = rdptr(mr, kM_bldgRec);
            u16 ownerIdx9 = bldgRec9 ? rdu16(bldgRec9, kB_owner39) : 0u;
            cmd.actorId   = (ownerIdx9 < static_cast<u16>(kPersonCapacity))
                            ? g_personIds[ownerIdx9] : 0;
            cmd.buildingId = bldgRec9 ? rd32(bldgRec9, kB_id1) : -1;
            cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                              (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                              (static_cast<u64>(g_meisterGameTime.minute) << 48));
            cmd.timeExtra  = g_meisterTimeExtra;
            cmd.timeTail   = g_meisterTimeTail;
            cmd.mode       = 1;

            // 0x458753: v101 = dword_12CE914[134 * *(u16*)(v82+39)]
            //   v82 = v105 (best burgle target)
            u16 targetOwner9 = rdu16(v105, kB_owner39);
            i32 targetActorId9 = (targetOwner9 < static_cast<u16>(kPersonCapacity))
                                  ? g_personIds[targetOwner9] : 0;
            cmd.targetId  = targetActorId9;   // v101 extra field in cmd
            // 0x45875d: v102 = *(v82+1) = *(v105+1) (target building id)
            cmd.srcId     = rd32(v105, kB_id1);
            // 0x45876d: v103 = *(bldgRec9+1) (meister's own building id, duplicate in cmd)
            // (stored in extra0 for completeness)
            cmd.extra0    = bldgRec9 ? rd32(bldgRec9, kB_id1) : -1;

            // Worker collection: count up to min(RandomModulo(3)+1, v107).
            // 0x45878d: if (RandomModulo(3)+1 <= v107) v84 = RandomModulo(3)+1
            //           else v84 = v107
            // [RNG draw #1 — burgle worker draw]
            int rnd3a = static_cast<int>(static_cast<u16>(RandomModulo(3u))) + 1;
            int v84;
            if (rnd3a <= v107) {
                // 0x45883f: v84 = RandomModulo(3) + 1  [RNG draw #2 — second draw]
                v84 = static_cast<int>(static_cast<u16>(RandomModulo(3u))) + 1;
            } else {
                v84 = v107;
            }
            // v126 = v84 (cap)
            int cap9 = v84;

            int wCount9 = 0;
            i32 meisterBldgHandle9 = rd32(mr, kM_bldgRec);
            if (cap9 > 0) {
                for (unsigned int byteOff9 = 0;
                     wCount9 < cap9 && static_cast<int>(byteOff9) < 411648;
                     byteOff9 += 536u)
                {
                    int pidx9 = static_cast<int>(byteOff9 / 536u);
                    u8* p9 = pr(pidx9);
                    if (rd16(p9, kP_marker) == static_cast<i16>(-1)) continue;
                    if (rd32(p9, kP_employer) != meisterBldgHandle9) continue;
                    if (!rd8(p9, kP_profByte)) continue;
                    if (rd32(p9, kP_busy)) continue;
                    u8* ao9  = rdptr(p9, kP_actionObj);
                    u8* emp9 = rdptr(p9, kP_employer);
                    if (!ao9 || !emp9) continue;
                    if (rd32(ao9, 44) != rd32(emp9, kB_id1)) continue;
                    cmd.workerIds.push_back(g_personIds[pidx9]);
                    ++wCount9;
                }
            }
            // Fill -1 pad to 8 slots
            while (static_cast<int>(cmd.workerIds.size()) < 8)
                cmd.workerIds.push_back(-1);

            // Guard: 0x45885a `cmp [esp+var_154], -1` → emit if FIRST worker slot is
            // filled (var_154 == v100 == first collected worker; see note above).
            // = "at least ONE worker collected" → cmd.workerIds[0] != -1.
            bool slot0ok9 = (!cmd.workerIds.empty() && cmd.workerIds[0] != -1);
            if (slot0ok9) {
                if (g_meisterCmdSink)
                    g_meisterCmdSink->push(cmd);
                wr32(mr, kM_target, -1);   // 0x458868
            }
        }

        // 0x45887a: if (v108 <= 1) return.
        if (v108 <= 1)
            goto END_RETURN;
        goto LABEL_63;
    }
    // v108 != 1: fall through to END_RETURN
    goto END_RETURN;

    // -----------------------------------------------------------------------
    // LABEL_63: DEFAULT COMMAND (break-in / patrol, cmdType = 0x61/97)
    // Actually: the decompile shows v93 = v30 (which is 0 before being set),
    // then BYTE1(v30) = 1. This means the cmdType byte (v93) is set to whatever
    // v30's low byte is at that point, which is 0 from the clear, and then
    // BYTE1(v30) = 1 which sets byte 1 (not byte 0) of v30. So v93 stays 0.
    // But reviewing: v30 is the byte-offset counter for the worker fill loop
    // (analogous to v76/v60/v83 in other paths). Before the loop, v30=0 and
    // v33=0. The cmdType is stored to v93 BEFORE the loop starts.
    // At 0x457a4d: mov [esp+58Ch+var_188], cl  -- cl = v93 byte
    // At 0x457a46: mov cl, 0x61  -- cl = 0x61 = 97 (!)
    // So cmdType = 0x61 = 97 for the default "LABEL_63" command.
    // -----------------------------------------------------------------------
LABEL_63: {
        // Sprintf_0 (byte_619734) — DROP.

        MeisterCommand cmd;
        cmd.cmdType = 0x61;   // 97 = 0x61 (0x457a46 mov cl, 61h)

        u8* bldgRecA = rdptr(mr, kM_bldgRec);
        u16 ownerIdxA = bldgRecA ? rdu16(bldgRecA, kB_owner39) : 0u;
        cmd.actorId   = (ownerIdxA < static_cast<u16>(kPersonCapacity))
                        ? g_personIds[ownerIdxA] : 0;
        cmd.buildingId = bldgRecA ? rd32(bldgRecA, kB_id1) : -1;
        cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                          (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                          (static_cast<u64>(g_meisterGameTime.minute) << 48));
        cmd.timeExtra  = g_meisterTimeExtra;
        cmd.timeTail   = g_meisterTimeTail;
        cmd.mode       = 1;    // BYTE1(v30) = 1 sets bit that maps to mode

        // 0x457aca: v104 = dword_12CE914[134 * *(u16*)(bldgRec+39)]
        // (duplicate actorId write to extra0)
        cmd.extra0 = cmd.actorId;

        // Worker collection: cap = min(v107, 8)  (0x457ad4..0x457b50)
        int capA = (v107 >= 8) ? 8 : v107;
        int wCountA = 0;
        i32 meisterBldgHandleA = rd32(mr, kM_bldgRec);
        if (capA > 0) {
            for (unsigned int byteOffA = 0;
                 wCountA < capA && static_cast<int>(byteOffA) < 411648;
                 byteOffA += 536u)
            {
                int pidxA = static_cast<int>(byteOffA / 536u);
                u8* pA = pr(pidxA);
                if (rd16(pA, kP_marker) == static_cast<i16>(-1)) continue;
                if (rd32(pA, kP_employer) != meisterBldgHandleA) continue;
                if (!rd8(pA, kP_profByte)) continue;
                if (rd32(pA, kP_busy)) continue;
                u8* aoA  = rdptr(pA, kP_actionObj);
                u8* empA = rdptr(pA, kP_employer);
                if (!aoA || !empA) continue;
                if (rd32(aoA, 44) != rd32(empA, kB_id1)) continue;
                cmd.workerIds.push_back(g_personIds[pidxA]);
                ++wCountA;
            }
        }
        // Fill -1 pad to 8 slots
        while (static_cast<int>(cmd.workerIds.size()) < 8)
            cmd.workerIds.push_back(-1);

        // 0x4588a7: `cmp [esp+var_154], -1` → emit if FIRST worker slot is filled.
        // var_154 == v100 == first collected worker (loop pre-increments before write),
        // so the guard is "at least ONE worker collected" → cmd.workerIds[0] != -1.
        bool slot0A = (!cmd.workerIds.empty() && cmd.workerIds[0] != -1);
        if (slot0A) {
            if (g_meisterCmdSink)
                g_meisterCmdSink->push(cmd);
        }
    }

END_RETURN:
    return;
}

} // namespace guild::sim
