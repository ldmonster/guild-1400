// ===========================================================================
// VIBE_Ai_CalcMeisterWache   @0x455cd8
// VIBE_Ai_CalcMeisterAmbush  @0x4588d0
//
// Guard-master and Ambush-master per-frame AI decision calculators.
// 1:1 reconstruction of gilde.exe — see CLAUDE.md rule 1.
//
// namespace guild::sim;  using namespace guild::sim::aimei;
//
// DATA MODEL:
//   meisterRec (mr) = the Meister's 536-byte person record (g_persons[i] base, u8*).
//   Record fields are accessed via rd*/wr* byte-offset accessors from
//   ai_meister_internal.h; POINTER columns via rdptr() (see SPEC UPDATE 1).
//
// COMMAND EMISSION:
//   VIBE_Light_SetGrayColorThunk(0,248,&buf) + field writes + QueueRequestSlotReset28
//   → MeisterCommand{} + g_meisterCmdSink->push(cmd).
//
// DROPPED (no sim side effect):
//   All VIBE_Crt_Sprintf_0 debug-log calls (write to a scratch buffer + dev console;
//   SPEC §"VIBE_Crt_Sprintf_0 → DROP"). They carry zero observable state change.
//
// LEAVES:
//   All external engine calls are routed through g_meisterLeaves fn-ptrs. Null hooks
//   return the same result the original would produce for a null/zero return.
//
// AUFLAUERLEGEN table:
//   aSpAuflauerlege @0x632275 — 8 entries × 32 bytes (zero-padded C strings).
//   get_bytes verified (see kAuflauerlegen table below). A random entry [0..7] is
//   chosen via RandomModulo(8) and strcpy'd into the command direction-name field.
//
// gilde.exe 0x455cd8 — VIBE_Ai_CalcMeisterWache  (__usercall, eax=a1)
// gilde.exe 0x4588d0 — VIBE_Ai_CalcMeisterAmbush (__usercall, eax=a1)
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"

#include "sim/entity.h"          // PersonQueryBegin, PersonIterNext, PersonFilter
#include "sim/building.h"        // BuildingPriceMode() — mirrors dword_63C744
#include "util/math_random.h"     // guild::util::RandomModulo
#include "util/math_rng_float.h"   // guild::util::RandomFloatScaled

#include <cstring>
#include <cmath>

namespace guild::sim {

// File-scope using-directives so the helpers defined before the Calc bodies see
// the aimei accessors and the util RNG (added by orchestrator integration pass).
using namespace guild::sim::aimei;
using guild::util::RandomModulo;
using guild::util::RandomFloatScaled;

// Forward declaration: defined in ai_meister_calc_angriff.cpp (non-static public).
// Returns the local-player city word (dword_6498E4 mirror) used by the object sweep.
u16 CalcAngriff_GetLocalPlayerWord();

// ---------------------------------------------------------------------------
// AUFLAUERLEGEN direction-name table — gilde.exe aSpAuflauerlege @0x632275.
// 8 entries × 32 bytes, zero-padded C strings.  Verified with get_bytes.
//
//   [0] "sp_AUFLAUERLEGEN_NORDEN"   (0x73 70 5f 41 55 46 4c 41 55 45 52 4c 45 47 45 4e 5f 4e 4f 52 44 45 4e 00 00 00 00 00 00 00 00 00)
//   [1] "sp_AUFLAUERLEGEN_OSTEN"
//   [2] "sp_AUFLAUERLEGEN_SUEDEN"
//   [3] "sp_AUFLAUERLEGEN_WESTEN"
//   [4] "sp_AUFLAUERLEGEN_STADT_1"
//   [5] "sp_AUFLAUERLEGEN_STADT_2"
//   [6] "sp_AUFLAUERLEGEN_STADT_3"
//   [7] "sp_AUFLAUERLEGEN_STADT_4"
// ---------------------------------------------------------------------------
static const char kAuflauerlegen[8][32] = {
    "sp_AUFLAUERLEGEN_NORDEN",  // [0]
    "sp_AUFLAUERLEGEN_OSTEN",   // [1]
    "sp_AUFLAUERLEGEN_SUEDEN",  // [2]
    "sp_AUFLAUERLEGEN_WESTEN",  // [3]
    "sp_AUFLAUERLEGEN_STADT_1", // [4]
    "sp_AUFLAUERLEGEN_STADT_2", // [5]
    "sp_AUFLAUERLEGEN_STADT_3", // [6]
    "sp_AUFLAUERLEGEN_STADT_4", // [7]
};

// ---------------------------------------------------------------------------
// FP constants — all byte-exact, verified via get_bytes:
//   dbl_6193F8 @0x6193F8 : 33 33 33 33 33 33 eb 3f == 0.85
//   dbl_619898 @0x619898 : 33 33 33 33 33 33 eb 3f == 0.85
//   dbl_6198A0 @0x6198A0 : 00 00 00 00 00 00 e8 3f == 0.75
//   flt_6198A8 @0x6198A8 : 00 00 00 3e == 0.125f
//   dbl_6198B0 @0x6198B0 : 7b 14 ae 47 e1 7a 84 3f == 0.01
// (kGate085, kGate075, kTileBWeight, kRandScale001 already in ai_meister_internal.h)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helper: emit a MeisterCommand to the sink if one is installed.
// ---------------------------------------------------------------------------
static inline void SinkPush(const MeisterCommand& cmd) {
    if (g_meisterCmdSink)
        g_meisterCmdSink->push(cmd);
}

// ---------------------------------------------------------------------------
// Helper: copy the Auflauerlegen name entry `idx` (from kAuflauerlegen) into
// the direction-name buffer `dst` exactly as the original does (2-byte-at-a-time
// copy loop). The original writes into var_D8/v68 which lands in the cmd payload;
// we write into the MeisterCommand via a char[32] field embedded in the cmd by
// the caller. `dst` must point to at least 32 bytes.
// gilde.exe 0x4560bb..0x4560d1 (Wache), 0x458b5c..0x458b72 (Ambush).
// ---------------------------------------------------------------------------
static void CopyAuflauerlegen(char* dst, int idx) {
    // Exact 2-byte-at-a-time copy loop matching 0x4560bb:
    //   do { al = *src; *dst = al; if (!al) break;
    //        al = src[1]; src+=2; dst[1]=al; dst+=2; } while (al);
    const char* src = kAuflauerlegen[idx & 7];
    for (;;) {
        char c0 = src[0];
        dst[0] = c0;
        if (!c0) break;
        char c1 = src[1];
        src += 2;
        dst[1] = c1;
        dst += 2;
        if (!c1) break;
    }
}

// ---------------------------------------------------------------------------
// Helper: count staff in building `bldgHandle` matching the worker-loop
// conditions used across Wache and Ambush (the common "active staff" test):
//   - word_12CE910[i] != -1 (slot alive: marker != -1)
//   - byte_12CE912[i] != 10 (not kind-10)
//   - dword_12CEA7C[i] == bldgHandle (employed at this building)
//   - byte_12CEA75[i] != 0 (profession byte set)
//   - dword_12CEA94[i] != 0 (action-object ptr set)
//   - *([actionObj]+44) == *(employer+1) (action-object building-id matches)
// Returns [totalCount, idleCount] in out params.
// gilde.exe 0x455de9..0x456259 (Wache count loop), 0x4589d4..0x458bf1 (Ambush).
// Note: the loop advances `v9` (byte offset into the 536-byte person array) by
// 536 (0x218) per iteration, from 0 to 411648 (exclusive), i.e. 768 persons.
// `dword_12CEA7C[v9/4]` = g_persons[i]+kP_employer (raw 32-bit handle).
// `dword_12CEA94[v9/4]` = g_persons[i]+kP_actionObj (raw 32-bit handle).
// For the outer "total" count: just checks *(actionObj+44)==*(employer+1).
// For "idle" (v79/v67): additionally !dword_12CEA8C[v9/4] (not busy) AND
//   v35!=0 AND same building-id check (v34==*(v35+44)).
// ---------------------------------------------------------------------------
static void CountBuildingStaff(i32 bldgHandle, int& outTotal, int& outIdle) {
    // gilde.exe 0x455de0 — loop init
    int total = 0, idle = 0;

    // Byte offset into the 536-byte person array: 0, 536, 1072, ..., up to <411648.
    // The decompile checks byte_12CE918[v9] (=g_persons[i]+kP_isLive) but also
    // word_12CE910[v9/2] (=g_persons[i]+kP_marker as a word). The inner tests are
    // on indices derived from v9: v9/4 for dword columns, v9/2 for word column,
    // v9 for byte columns.
    for (unsigned int i = 0; i < 768u; ++i) {
        u8* p = pr(i);  // g_persons[i] base (word_12CE910 + 536*i)

        // 0x455de9: byte_12CE918[v9] — isLive byte at +8
        if (rd8(p, kP_isLive) == 0) continue;

        // 0x455df2: word_12CE910[v9/2] != -1 (marker != -1)
        if (rd16(p, kP_marker) == -1) continue;

        // 0x456239: byte_12CE912[v9] != 10 (kind byte)
        if (rd8(p, kP_kind) == 10) continue;

        // 0x456246: dword_12CEA7C[v9/4] == *(mr+91) (employer == meister building)
        if (rd32(p, kP_employer) != bldgHandle) continue;

        // 0x45625f: byte_12CEA75[v9] != 0
        if (rd8(p, kP_profByte) == 0) continue;

        // 0x45626c: v5 = dword_12CEA94[v9/4]
        u8* actionObj = rdptr(p, kP_actionObj);
        if (!actionObj) continue;

        // 0x456280: v1 = *(v5+44); v34 = *(employer+1)
        u8* employer = rdptr(p, kP_employer);
        i32 aoId    = rd32(actionObj, 44);
        i32 bldgId  = employer ? rd32(employer, kB_id1) : 0;

        // 0x45628a: v1 == v34 (action-object's building == employer's building-id)
        if (aoId != bldgId) continue;

        ++total; // 0x456296: ++v8

        // idle sub-count: !dword_12CEA8C[v9/4] && v35 (actionObjRaw != 0) && same id
        // gilde.exe 0x456290: v5 = dword_12CEA8C[v9/4]; 0x4562aa: !v5 && v35 && ...
        i32 busyVal = rd32(p, kP_busy);
        // v35 = dword_12CEA94[v9/4] (the raw handle, already checked non-null above)
        // The condition at 0x4562aa: !v5 && v35 && v34 == *(v35+44)
        //   => !busy && actionObj && bldgId == *(actionObj+44)
        if (!busyVal && aoId == bldgId) {
            ++idle; // 0x4562b0
        }
    }

    outTotal = total;
    outIdle  = idle;
}

// ---------------------------------------------------------------------------
// Helper: find the best-score tile in the 8x8 danger grid.
// Used by Wache patrol scan (outer 0..192 step 24, inner 0..8 rows).
//
// The Wache decompile iterates:
//   outer: i = 0, 24, 48, ..., 168 (8 columns by stride 24)
//   inner: v75 = 0..7 (8 rows), uses byte-offset v45 = i + 192*v75
// The "best tile" is found by comparing (dangerA>>3 + dangerB) scored as an
// UNSIGNED INTEGER (the decompile uses __int16* arithmetic, not float).
//
// The comparison at 0x455f54:
//   v1  = (int)(word_12349A0[v45] >> 3) + word_12349A2[v45]  (new score)
//   cur = word_12349A0[i] >> 3 + word_12349A2[i]             (best score so far)
//   if new > cur: update best to v15 (column pointer)
// with the entry guard: !*((_BYTE*)v14 + 4) OR (byte_12349A4[v45] && new > cur)
// The pointer returned (v14) is into word_12349A0 as a __int16*, pointing to
// the tile at (best col, best row=0?). The caller then uses it as follows:
//   PersonQueryBegin((int)v1, 1, 6) — passes v1 (the score) as the QueryBegin arg.
//   PersonQueryBegin(v39, 1, 6) for the escort version.
// Actually looking more carefully: v14 and v15 are __int16* pointers into
// word_12349A0. The "v1" passed to PersonQueryBegin is the tile pointer itself
// (as an int), NOT the score. VIBE_Person_QueryBegin takes a start-person index
// as arg1; in the original, the tile-pointer-as-int is passed. In the reimpl,
// g_meisterLeaves->personQueryBegin takes (i32 a, int op0, int op1).
//
// For patrol (cmdType=67/100/101): "best high-danger tile" = the tile with max
//   score = (word_A >> 3) + word_B (integer, unsigned __int16 result)
// For escort (cmdType=100): "best LOW-danger tile" = the tile with max score
//   score = word_B + (word_A >> 3) still, but in a separate second loop.
// Actually reading the decompile again for the escort section (0x4563b1..):
//   Outer loops i (v80) from 0 to 168, step 24, 8 iters (the cols).
//   Inner: same structure. The comparison at 0x4563f4:
//     new_score = word_B[v53] + (word_A[v53] >> 3)   > old_score
//   Same formula. But wait: the patrol loop selects the HIGHEST danger tile
//   (better for patrol — find the riskiest place), and this second loop seems
//   to do the same? Let me re-read the decompile comment about this...
//
// Re-reading carefully:
//   Patrol loop (v14 pointer): selects tile where score is HIGHER than current
//   best. This gives the max-score tile.
//   Escort loop (v37 pointer): same comparison direction. But the person FILTER
//   is different: escort looks for a person with *low* relation (v54 < bestRelation),
//   while patrol looks for a person with *high* relation (v46 > bestRelation).
//   The tile-finding loops are structurally identical (find highest-danger tile
//   for both patrol and escort target areas).
//
// Returns the byte-offset into g_cityTileGrid of the winning tile's +0 byte.
// Also outputs the integer score of the winning tile.
// gilde.exe 0x455f10..0x455f87 (patrol best tile), 0x4563a2..0x456419 (escort).
// ---------------------------------------------------------------------------
static int FindBestDangerTile() {
    // Mirrors the decompile's nested loop: outer col-stride, inner row-stride.
    // v14 = &word_12349A0[i/2] updated to the running best tile pointer.
    // The original initialises v14 = word_12349A0 (tile 0,0) before the loop.
    // We track best tile as byte-offset into g_cityTileGrid.
    int bestOff = 0; // tile (row=0, col=0) offset 0

    // outer: i = byte-offset of start of each column = 0,24,48,...,168
    for (int i = 0; i < 192; i += 24) {  // 0x455f1c: for(i=0; i!=192; i+=24)
        // v15 = &word_12349A0[i/2] — pointer into the column-start tile
        // inner: v75 = 0..7 (rows)
        for (int row = 0; row < 8; ++row) {  // 0x455f38: v75=0; do { ... ++v75; } while <8
            // v45 = i + 192*v75  (byte offset of tile at [row, col=i/24])
            int tileOff = i + kCityTileRowStride * row;  // = 24*col + 192*row

            // 0x4564eb: first branch: !*((_BYTE*)v14 + 4) — if best tile's valid==0, update
            u8 bestValid = g_cityTileGrid[bestOff + 4];
            if (!bestValid) {
                // Best tile not valid: always replace with current (v14 = v15)
                bestOff = tileOff;  // 0x455f4f: v14 = v15
            } else {
                // Check if this tile is better
                // 0x4564eb second branch: byte_12349A4[v45] (tile valid)
                u8 curValid = g_cityTileGrid[tileOff + 4];
                if (curValid) {
                    // 0x455f40: v1 = (int)(*(unsigned __int16*)((char*)word_12349A0 + v45) >> 3)
                    //                   + *(unsigned __int16*)((char*)word_12349A2 + v45)
                    u16 newA, newB;
                    std::memcpy(&newA, g_cityTileGrid + tileOff + 0, 2);
                    std::memcpy(&newB, g_cityTileGrid + tileOff + 2, 2);
                    unsigned int newScore = (unsigned int)(newA >> 3) + (unsigned int)newB;

                    // 0x455f52: (int)v1 > (unsigned __int16)v14[1] + ((int)(unsigned __int16)*v14 >> 3)
                    u16 bestA, bestB;
                    std::memcpy(&bestA, g_cityTileGrid + bestOff + 0, 2);
                    std::memcpy(&bestB, g_cityTileGrid + bestOff + 2, 2);
                    unsigned int bestScore = (unsigned int)(bestA >> 3) + (unsigned int)bestB;

                    if ((int)newScore > (int)bestScore) {  // signed comparison per decompile cast
                        bestOff = tileOff;  // 0x455f4f
                    }
                }
            }
        }
    }
    return bestOff;
}

// ---------------------------------------------------------------------------
// Same as above but for the escort loop (0x4563a2..0x456419).
// The decompile uses a separate outer-first loop: outer=col (v80=0..168 step 24),
// inner=row (v75=0..7). Same comparison. We reuse FindBestDangerTile().
// ---------------------------------------------------------------------------
static int FindBestDangerTileEscort() {
    // Same algorithm as patrol — find max (wordB + wordA>>3) tile.
    // The escort person-scan then looks for the person at that tile with the
    // LOWEST relation (v54 < bestRelation), not highest. The tile selection is the
    // same direction (max danger) — only the person-selection polarity differs.
    return FindBestDangerTile();  // Same formula, same loop structure per disasm.
}

// ===========================================================================
// gilde.exe 0x455cd8 — VIBE_Ai_CalcMeisterWache (__usercall, eax=a1)
// Guard master: patrol + escort decision planner.
//
// Phases:
//   1. If QueryFind(bldgRoot, 2,6,0, type=42) found: run storage/gather/equip/
//      transport/trade planners (the "resource management" block).
//   2. Always: HireStaff / FillAiSlots / TrainStaff / RenovateBuilding.
//   3. If QueryFind(type=52) found: FlagIdleStaff.
//   4. FindFreeStaffSlot.
//   5. Weekly-tick gate: hour>6 && (personId+hour)%3==0.
//   6. Day-flag gate: bit 0x20 of mr+436.
//   7. Count building staff (total + idle).
//   8. Decide mode (v78): via building-owner-kind (6/7 = nobility route, else merchant):
//      - nobility (kind 6/7): RandomModulo(3)==0 → patrol (v78=0), else check cached
//        target, possibly escort (v78=0 → patrol, or v78=1 → escort).
//      - merchant: RandomFloatScaled()>0.85 && idle>=3 && priceDay+7<currentDay → v78=4
//        (CalcAngriff); then RandomModulo(3) for 0=patrol/1=escort/else=skip.
//   9. v78==4: CalcAngriff; if returns 0: v78=0 (patrol).
//  10. v78==0: LABEL_24 → patrol-scan → emit patrol command (cmdType=101 Umland or
//      cmdType=67 patrol-near-person).
//  11. v78==1: LABEL_77 → escort-scan → emit escort command (cmdType=100).
//  12. LABEL_139: if v78>=2 return.
//
// cmdTypes (verified via disasm):
//   67  (0x43) — patrol near person   (v60 = 67, 0x45657d)
//  100  (0x64) — escort               (v60 = 100, 0x4567f4)
//  101  (0x65) — countryside patrol   (ch=0x65 at 0x45602f → mov var_12C, ch)
//
// Return: the original's `result` (an int; usually 0 or the last cmd queue result).
// ===========================================================================
int CalcMeisterWache(u8* meisterRec) {
    using namespace guild::util;  // RandomModulo, RandomFloatScaled
    using namespace guild::sim::aimei;

    // gilde.exe 0x455ce4
    u8* mr = meisterRec;

    // v82 = a1; v1 = QueryFind(bldgRoot+93, 2, 6, 0, 42)  [type=42 worker query]
    // gilde.exe 0x455ceb..0x455d0b
    u8* bldgRec = rdptr(mr, kM_bldgRec);  // *(mr+0x16C)
    i32 bldgRoot = bldgRec ? rd32(bldgRec, kB_sceneRoot93) : 0;

    // v1 = QueryFind result (nonzero if building has active scene node type 42)
    u8* v1 = nullptr;
    if (g_meisterLeaves && g_meisterLeaves->queryFind) {
        static const int kFilters42[] = { 6, 0, 42 };  // op 2 = 2 filters: (op6,no-val),(op0,42)
        v1 = g_meisterLeaves->queryFind(bldgRoot, kFilters42, 3);
    }

    // 0x4561b7: if v1 — run the storage/supply management block
    if (v1) {
        // 0x4561c2
        g_workOrderCount = 0;
        g_stockRowCount  = 0;
        MeisterCollectStorageItems(mr);                                       // 0x4561d5
        i32 neededId = bldgRec ? rd32(bldgRec, kB_needed57) : 0;
        MeisterGatherRequiredItems(mr, neededId);                             // 0x4561ec
        EquipStaffWeapon(mr, 0);                                              // 0x4561fa
        MeisterCollectTransporters(mr);                                       // 0x456206
        MeisterTradeManageStorage(mr);                                        // 0x456214
    }

    // LABEL_loc_455D11: Always run the management cluster.
    MeisterHireStaff(mr);               // 0x455d24

    // FillAiSlots(v1, mr, ecx, 10): eax=v1(SceneNode*), edx=mr, ecx=?, ebx=10
    // In the reimpl signature: void MeisterFillAiSlots(SceneNode* bldgNode, u8* mr, int cap)
    MeisterFillAiSlots(reinterpret_cast<SceneNode*>(v1), mr, 10);            // 0x455d2b

    // TrainStaff(v1, mr, 10): eax=v1, edx=mr, ebx=10
    MeisterTrainStaff(reinterpret_cast<SceneNode*>(v1), mr, 10);             // 0x455d3e

    MeisterRenovateBuilding(mr);        // 0x455d4a

    // 0x455d4f..0x455d7b: QueryFind(bldgRoot, 2,6,0, type=52) → if found, FlagIdleStaff
    // (fresh bldgRec/root read)
    {
        bldgRec  = rdptr(mr, kM_bldgRec);
        bldgRoot = bldgRec ? rd32(bldgRec, kB_sceneRoot93) : 0;
        u8* v5Node = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->queryFind) {
            static const int kFilters52[] = { 6, 0, 52 };
            v5Node = g_meisterLeaves->queryFind(bldgRoot, kFilters52, 3);
        }
        if (v5Node)
            MeisterFlagIdleStaff(mr);   // 0x455d7b
    }

    MeisterFindFreeStaffSlot(mr);       // 0x455d87

    // -----------------------------------------------------------------------
    // 0x455d8c: Weekly-tick gate.
    //   WORD2(qword_13CE852) == hour-of-day. Must be >6.
    //   (*(mr+4) + hour) % 3 == 0  [person-id is at +4; used as %3 shard].
    // gilde.exe 0x455d93: cmp dx, 6; jbe loc_45621E
    // gilde.exe 0x455db3: div ecx(3); test edx; jnz loc_45621E
    // -----------------------------------------------------------------------
    u16 hour = gtHour();
    if (hour <= 6u) {
        // Clear bit 0x20: return with flag cleared
        // 0x45621e
        wr8(mr, kM_dayFlags, rd8(mr, kM_dayFlags) & ~u8(0x20u));
        return (int)(uintptr_t)mr;
    }
    // (personId + hour) % 3: personId is *(mr+4), same as *((_DWORD*)v82+1)
    i32 personId = rd32(mr, kM_id);  // +4
    unsigned int hourU = (unsigned int)hour;
    unsigned int idU   = (unsigned int)personId;
    if ((idU + hourU) % 3u != 0u) {
        // Not our tick slot
        wr8(mr, kM_dayFlags, rd8(mr, kM_dayFlags) & ~u8(0x20u));  // 0x456225
        return (int)(uintptr_t)mr;
    }

    // 0x455dbd: result = (int)v82
    // 0x455dc4: v7 = *(mr+436) & 0x20
    u8 dayFlags = rd8(mr, kM_dayFlags);
    if (dayFlags & 0x20u)
        return (int)(uintptr_t)mr;  // 0x455dcd: already done this tick

    // 0x455dd8: set bit 0x20
    wr8(mr, kM_dayFlags, dayFlags | u8(0x20u));

    // -----------------------------------------------------------------------
    // 0x455de0: Count staff in this building (total=v8, idle=v79).
    // -----------------------------------------------------------------------
    bldgRec = rdptr(mr, kM_bldgRec);
    i32 bldgHandle = rd32(mr, kM_bldgRec);  // the raw handle (used for comparison)

    int v8 = 0, v79 = 0;
    CountBuildingStaff(bldgHandle, v8, v79);

    // -----------------------------------------------------------------------
    // 0x455e0c: Debug-HUD snapshot (dword_B53950 == bldgHandle).
    //   dword_B53954 = BuildingFindById(*(mr+448)) if *(mr+112)!=-1
    //   dword_B53964 = v79 (idle count)
    // -----------------------------------------------------------------------
    {
        bldgRec = rdptr(mr, kM_bldgRec);
        i32 bldgH = rd32(mr, kM_bldgRec);
        if (g_aiSelMeisterBuilding == bldgH) {
            // 0x455e27: v10 = *(mr+112) = *((_DWORD*)v82+112) → mr+448
            i32 targetBldgId = rd32(mr, kM_target);  // +0x1C0 = +448
            g_aiSelMeisterCount = v79;                // dword_B53964
            if (targetBldgId != -1) {
                u8* found = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->buildingFindById)
                    found = g_meisterLeaves->buildingFindById(targetBldgId);
                g_aiSelMeisterBuildingRec = found ? (i32)(uintptr_t)found : 0;  // dword_B53954
            }
        }
    }

    // -----------------------------------------------------------------------
    // 0x455e50: v11 = v8>>1; v12 = v8 - (v8>>1); if v11>=v12: v11=v12.
    // 0x455e56: if v11>v79: log "keine Leute" and return (Sprintf_0 dropped).
    // This is: if (min(v8/2, v8-v8/2) > v79) early-return (not enough idle staff).
    // -----------------------------------------------------------------------
    int v11 = v8 >> 1;
    int v12 = v8 - v11;
    if (v11 >= v12) v11 = v12;
    if (v11 > v79) {
        // 0x455e65: Sprintf_0("keine Leute") — dropped; return (the original returns
        // the result of the Sprintf_0 call, which is also the buf address as int).
        // We return mr-as-int to match the "result = (int)v58" structure.
        return 0;  // Sprintf_0 return value not observable; 0 is safe.
    }

    // -----------------------------------------------------------------------
    // 0x455e91: v13 = byte_12CE912[536 * *(u16*)(bldgRec+39)]
    //   = kind-byte of the building's OWNER person record.
    // -----------------------------------------------------------------------
    bldgRec = rdptr(mr, kM_bldgRec);
    u16 bldgOwner = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
    // byte_12CE912 = g_persons[i]+kP_kind. The index is the owner word (city-player index).
    u8 ownerKind = rd8(pr((int)bldgOwner), kP_kind);

    // -----------------------------------------------------------------------
    // Mode selection (v78): 0=patrol, 1=escort, 4=CalcAngriff
    // -----------------------------------------------------------------------
    int v78 = 0;  // default: patrol

    if (ownerKind == 6 || ownerKind == 7) {
        // 0x455eaf: nobility branch
        // v78 = RandomModulo(3); if (v78 != 0) check cached target
        v78 = (int)(unsigned)RandomModulo(3u);  // 0x455eaf
        if (v78 != 0) {
            // 0x455ec1: if (*(mr+112) != -1) RandomModulo(2); if (result) v78=0; goto LABEL_24
            // *(mr+112) → *((_DWORD*)v82+112) → mr+0x1C0 = mr+448
            if (rd32(mr, kM_target) != -1) {
                // 0x455eda
                int r2 = (int)(unsigned)RandomModulo(2u);
                if (r2) {
                    v78 = 0;        // 0x455eea: v78=0; fall to LABEL_24
                    goto LABEL_24;
                }
            }
            // v78 stays nonzero (1 or 2) → fall through to LABEL_76 → check v78
            goto LABEL_76;
        }
        // v78==0 → LABEL_24
        goto LABEL_24;
    } else {
        // 0x456300: merchant branch
        // 0x456300: if (RandomFloatScaled() > dbl_6193F8 && v36 >= 3)
        //   v36 is undefined in the decompile — it's v79 (idle count) by context.
        // gilde.exe 0x456300: this block is at the END of the function (after LABEL_77/139).
        // Actually looking at the decompile control flow: the "else" block runs
        // RandomFloatScaled() > 0.85 check at 0x456300. If fails: RandomModulo(3),
        // goto LABEL_51 (which goes to LABEL_52 if v65!=3, else LABEL_48).
        // But for Wache: the "else" block contains:
        //   if (RandomFloatScaled() > dbl_6193F8 && v36>=3) {
        //     v1 = dword_63C744; if (7-dword_63C744 <= (int)qword_13CE852) { v78=4; goto LABEL_72; }
        //   }
        //   result = RandomModulo(3); v78 = result;
        // Then fall through to LABEL_76.
        // v36 maps to v79 (idle count) in the Wache context (it's the same local).
        if (RandomFloatScaled() > kGate085 && v79 >= 3) {
            // 0x456307: compare 7-dword_63C744 <= (int)qword_13CE852
            // dword_63C744 == BuildingPriceMode() from sim/building.h.
            // (int)qword_13CE852 == gtDay().
            int dword63C744 = BuildingPriceMode();
            if (7 - dword63C744 <= gtDay()) {
                v78 = 4;
                goto LABEL_72;
            }
        }
        // 0x456351: result = RandomModulo(3); v78 = result;
        v78 = (int)(unsigned)RandomModulo(3u);
        // fall through to LABEL_76
    }

    // LABEL_76:
LABEL_76:
    if (v78 != 4) {
        // LABEL_76 body: if (v78) goto LABEL_77; else goto LABEL_24;
        if (v78)
            goto LABEL_77;
        goto LABEL_24;
    }

    // LABEL_72:
LABEL_72:
    {
        // 0x456324: result = CalcAngriff(mr, v79, ...)
        int angriffResult = CalcAngriff(mr, v79);
        if (angriffResult)
            goto DISPATCH_456367;   // 0x456339: jnz loc_456367 (dispatch WITHOUT the ==4 check)
        v78 = 0;                    // 0x45633b
        // fall through to LABEL_24
    }
    goto LABEL_24;  // explicit: after v78=0 the original falls to loc_455EF1 (LABEL_24)

    // loc_456367: dispatch reached from CalcAngriff!=0 (v78 still 4 here) — has NO
    // v78==4 re-entry to LABEL_72 (unlike loc_45635D), so v78==4 routes to LABEL_139.
    // gilde.exe 0x456367: cmp v78,0; jz LABEL_24; 0x456375: cmp v78,1; jnz LABEL_139.
DISPATCH_456367:
    if (v78 == 0)
        goto LABEL_24;
    goto LABEL_77;  // LABEL_77 begins with: if (v78 != 1) goto LABEL_139

    // LABEL_24:
LABEL_24:
    {
        // -----------------------------------------------------------------------
        // LABEL_24: Patrol scan.
        // Find the highest-danger tile in the 8x8 grid, then find the person on
        // that tile with the highest relation to the Meister's owner.
        // gilde.exe 0x455ef1..0x455ffe
        // -----------------------------------------------------------------------
        // (Sprintf_0 debug log at 0x455ef1: dropped)

        // Find best (max danger) tile: uses FindBestDangerTile() → byte offset.
        int bestTileOff = FindBestDangerTile();

        // Patrol: PersonQueryBegin(v1_score, 1, 6).
        // In the original, v1 (the __int16* tile pointer, cast to int) is passed as
        // the first arg of PersonQueryBegin. In the reimpl, personQueryBegin takes
        // (i32 a, int op0, int op1). The first arg corresponds to a "start person" or
        // a score-derived query key. We pass the tile's danger-score integer.
        // For the reimpl PersonQueryBegin (entity.h), the PersonFilter {op,value} is:
        //   op=1 ("id==value"), op=6 ("match-any"). So we pass the tile ptr value
        //   as the iterator start (it's used as a person query filter in original).
        // We route through the leaves' personQueryBegin with the raw tile ptr value.
        bldgRec  = rdptr(mr, kM_bldgRec);
        bldgOwner = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;

        // v14 (tile ptr) → PersonQueryBegin passes its integer value as a1.
        // In the reimpl, the tile ptr value (cast to int) is passed as the filter value.
        i32 tileQueryArg = (i32)bestTileOff;  // cast of the __int16* tile pointer

        u8* v16 = nullptr;  // best escort/patrol candidate (null = none found)
        int bestRelation = -1;

        if (g_meisterLeaves && g_meisterLeaves->personQueryBegin) {
            // 0x455f8c: v76=8; then QueryBegin(v1, 1, 6)
            // PersonFilter: one filter with op=1, value=tileQueryArg (or op=6 for any-match)
            // Original: PersonQueryBegin((int)v1, 1, 6) — 3 args: a, op0, op1
            // This maps to the leaf: personQueryBegin(a, op0, op1)
            u8* person = g_meisterLeaves->personQueryBegin(tileQueryArg, 1, 6);
            for (; person; person = g_meisterLeaves->personIterNext
                           ? g_meisterLeaves->personIterNext() : nullptr) {
                // 0x455fa4: v18 = *(j+97)  (scene position ptr)
                i32 v18 = rd32(person, 97);
                if (!v18) continue;

                // 0x455fbc: WorldToCityTile((float*)(v18+76), &v75, &v76)
                int tRow = 0, tCol = 0;
                bool tileMatch = false;
                if (g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                    float* pos = reinterpret_cast<float*>(
                        static_cast<u8*>(nullptr) + v18 + 76);
                    // Actually v18 is a raw dword (a world-node pointer); in the
                    // reimpl it's an opaque handle. We use it as an integer offset.
                    // The person rec +97 holds the scene-node position ptr.
                    // In the reimpl, we pass the address computed by the engine.
                    // Since worldToCityTile takes (float* pos3), we call the leaf fn-ptr.
                    float pos3[3] = {0,0,0};  // bridge fills real pos from v18+76
                    // We can't dereference v18 in the reimpl directly (it's a 32-bit ptr
                    // in the original, not a reimpl address). Pass the raw int to the leaf
                    // via a compatible shim. The leaf bridge resolves it in-engine.
                    // For test purposes: pass the int reinterpret-cast to float*.
                    (void)pos;
                    // Call the leaf with the raw value cast to float* — the bridge handles it.
                    tileMatch = (g_meisterLeaves->worldToCityTile(
                        reinterpret_cast<float*>(static_cast<uintptr_t>(static_cast<u32>(v18) + 76u)),
                        &tRow, &tCol) != 0);
                }
                if (!tileMatch) continue;

                // 0x455fe5: v19 == &word_12349A0[96*v75 + 12*v76]
                // v19 is computed from tileOff. In the decompile, v19 is the
                // tile pointer after WorldToCityTile; it should point to the same tile
                // as v14 (bestTileOff). The check is: is person at best tile?
                // v19 = &word_12349A0[96*v75 + 12*v76]
                // word_12349A0[k] = g_cityTileGrid at byte offset 2*k.
                // 96*v75 + 12*v76 (as word index) → byte offset = 2*(96*v75+12*v76)
                //   = 192*row + 24*col   (= kCityTileRowStride*row + kCityTileStride*col)
                int personTileOff = kCityTileRowStride * tRow + kCityTileStride * tCol;
                if (personTileOff != bestTileOff) continue;

                // 0x455fe5 check passed: person is at the best tile.
                // 0x456535: if (!v16) update; else if (owner != 0xFFFF && relation > bestRelation)
                u16 pOwner = rdu16(person, 39);  // *(u16*)(j+39)
                if (!v16) {
                    v16 = person;
                    if (pOwner != 0xFFFFu && g_meisterLeaves && g_meisterLeaves->relationMatrix) {
                        bestRelation = g_meisterLeaves->relationMatrix(
                            rdu16(mr, 0), pOwner);  // *v82 (first word of mr) vs person owner
                    }
                } else if (pOwner != 0xFFFFu) {
                    if (g_meisterLeaves && g_meisterLeaves->relationMatrix) {
                        int relNew  = g_meisterLeaves->relationMatrix(rdu16(mr, 0), pOwner);
                        int relBest = g_meisterLeaves->relationMatrix(
                            rdu16(mr, 0), rdu16(v16, 39));
                        if (relNew > relBest) {
                            v16 = person;
                            bestRelation = relNew;
                        }
                    }
                }
            }
        }

        if (!v16)
            goto LABEL_38;  // 0x455ffe: if (!v16) goto LABEL_38

        // -----------------------------------------------------------------------
        // 0x456569..0x4566fd: Emit patrol command (cmdType=67, near a person).
        // Build the 248-byte command record, fill fields, collect worker ids.
        // -----------------------------------------------------------------------
        // (Sprintf_0 debug log: dropped)
        {
            MeisterCommand cmd{};
            cmd.cmdType   = 67;   // 0x45657d: v60 = 67 (0x43 = patrol)
            bldgRec       = rdptr(mr, kM_bldgRec);
            bldgOwner     = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
            // 0x456563: v61 = dword_12CE914[134 * *(u16*)(bldgRec+39)]
            cmd.actorId   = g_personIds[(int)bldgOwner];   // dword_12CE914[134*ownerWord]
            // 0x4565c6: v62 = *(bldgRec+1)
            cmd.buildingId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            // 0x4565cf: v63 = qword_13CE852
            cmd.timePacked = (u64)g_meisterGameTime.day | ((u64)g_meisterGameTime.hour << 32);
            cmd.timeExtra  = g_meisterTimeExtra;  // 0x4565d1
            cmd.timeTail   = g_meisterTimeTail;   // 0x4565d2
            cmd.mode       = 2;   // 0x4565d4: v66 = 2
            // 0x4565de: v68 = *(v16+1)  (patrol target person's building id)
            cmd.targetId   = rd32(v16, 1);  // +1 is the id dword of the person rec
            // 0x4565f5: v69 = *(bldgRec+1)
            cmd.srcId      = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            // 0x45660f: v74 = RandomModulo(4) + 2  [escort count hint, stored at +0xA7]
            int v74 = (int)(unsigned)RandomModulo(4u) + 2;
            (void)v74;  // stored at cmd buf offset 0xA7 (extra field, not in MeisterCommand)
            // 0x456619: worker count = min(v79, 4)
            int numWorkers = (v79 >= 4) ? 4 : v79;
            // 0x456636: collect worker ids (up to numWorkers)
            int collected = 0;
            for (unsigned int si = 0; si < 768u && collected < numWorkers; ++si) {
                u8* p = pr(si);
                if (rd16(p, kP_marker) == -1) continue;
                if (rd32(p, kP_employer) != bldgHandle) continue;
                if (!rd8(p, kP_profByte)) continue;
                if (rd32(p, kP_busy)) continue;
                u8* ao = rdptr(p, kP_actionObj);
                if (!ao) continue;
                u8* emp = rdptr(p, kP_employer);
                if (!emp) continue;
                if (rd32(ao, 44) != rd32(emp, kB_id1)) continue;
                cmd.workerIds.push_back(g_personIds[si]);
                ++collected;
            }
            // 0x4566b8: fill remaining slots with -1
            while ((int)cmd.workerIds.size() < 4)
                cmd.workerIds.push_back(-1);

            // 0x4566f0: if (v73[0] != -1) QueueRequestSlotReset28(...)
            // v73[0] is the first worker id. Emit if at least one valid worker.
            if (!cmd.workerIds.empty() && cmd.workerIds[0] != -1) {
                SinkPush(cmd);  // 0x4566fd
            }
        }
    }

    // LABEL_77:
LABEL_77:
    if (v78 != 1)
        goto LABEL_139;  // 0x45637d: if (v78 != 1) goto LABEL_139

    // -----------------------------------------------------------------------
    // LABEL_77: Escort scan.
    // Find best-danger tile again, then find person with LOWEST relation.
    // gilde.exe 0x456380..0x456490
    // -----------------------------------------------------------------------
    {
        // (Sprintf_0 debug log: dropped)
        int escortTileOff = FindBestDangerTileEscort();

        bldgRec   = rdptr(mr, kM_bldgRec);
        bldgOwner  = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;

        // v39 score (the tile score as int) passed to PersonQueryBegin
        i32 escortQueryArg = (i32)escortTileOff;

        u8* v41 = nullptr;  // best escort target person
        int bestRelEscort = 0x7FFFFFFF;

        if (g_meisterLeaves && g_meisterLeaves->personQueryBegin) {
            u8* person = g_meisterLeaves->personQueryBegin(escortQueryArg, 1, 6);
            for (; person; person = g_meisterLeaves->personIterNext
                           ? g_meisterLeaves->personIterNext() : nullptr) {
                // 0x456436: v43 = *(k+97)
                i32 v43 = rd32(person, 97);
                if (!v43) continue;

                int tRow = 0, tCol = 0;
                bool tileMatch = false;
                if (g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                    tileMatch = (g_meisterLeaves->worldToCityTile(
                        reinterpret_cast<float*>(static_cast<uintptr_t>(static_cast<u32>(v43) + 76u)),
                        &tRow, &tCol) != 0);
                }
                if (!tileMatch) continue;

                int personTileOff = kCityTileRowStride * tRow + kCityTileStride * tCol;
                if (personTileOff != escortTileOff) continue;

                // 0x4567ac: escort filter — owner != 0xFFFF && bit0 of k[90] == 0 && ...
                // AND relation(mr[0], person[39]) < bestRelation (LOWEST relation = most hostile)
                u16 pOwner = rdu16(person, 39);
                // 0x456477 check at k[90] bit0: (k+90) & 1 == 0
                if (rd8(person, 90) & 1u) continue;

                if (!v41) {
                    v41 = person;
                    if (pOwner != 0xFFFFu && g_meisterLeaves && g_meisterLeaves->relationMatrix) {
                        bestRelEscort = g_meisterLeaves->relationMatrix(
                            rdu16(mr, 0), pOwner);
                    }
                } else if (pOwner != 0xFFFFu) {
                    if (g_meisterLeaves && g_meisterLeaves->relationMatrix) {
                        int relNew  = g_meisterLeaves->relationMatrix(rdu16(mr, 0), pOwner);
                        int relBest = g_meisterLeaves->relationMatrix(
                            rdu16(mr, 0), rdu16(v41, 39));
                        if (relNew < relBest) {  // 0x4567ac: v54 < bestRelation → LOWER
                            v41 = person;
                            bestRelEscort = relNew;
                        }
                    }
                }
            }
        }

        if (!v41) goto LABEL_139;  // 0x456490: if (!v41) goto LABEL_139

        // -----------------------------------------------------------------------
        // 0x4567e6..0x456934: Emit escort command (cmdType=100).
        // -----------------------------------------------------------------------
        // (Sprintf_0 debug log: dropped)
        {
            MeisterCommand cmd{};
            cmd.cmdType   = 100;   // 0x4567f4: v60 = 100 (0x64 = escort)
            bldgRec       = rdptr(mr, kM_bldgRec);
            bldgOwner     = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
            cmd.actorId   = g_personIds[(int)bldgOwner];
            cmd.buildingId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            cmd.timePacked = (u64)g_meisterGameTime.day | ((u64)g_meisterGameTime.hour << 32);
            cmd.timeExtra  = g_meisterTimeExtra;
            cmd.timeTail   = g_meisterTimeTail;
            cmd.mode       = 1;   // 0x45684b: v66 = 1
            // 0x456855: v69 = *(v41+1)  (escort target id)
            cmd.targetId   = rd32(v41, 1);
            // 0x45686c: v70 = *(bldgRec+1)
            cmd.srcId      = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            // 0x456880: v71 = *(FindStorableObject(bldgRec)+1)  [__int16*-typed +1 == byte +2]
            // disasm 0x456885: mov eax,[eax+2] — actual byte offset is 2, NOT 1.
            {
                u8* storable = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->findStorableObject)
                    storable = g_meisterLeaves->findStorableObject(bldgRec);
                cmd.extra0 = storable ? rd32(storable, 2) : 0;
            }
            cmd.workerIds.push_back(-1);  // 0x45689b: v68 = -1 initially

            // 0x4568a4: if (!v77): v77 is the collected worker count from patrol block.
            // Since we're in a different code path, re-scan. The decompile checks the
            // local v77 which was accumulated during the patrol worker-collection loop.
            // However, in the restructured code, v77 is the count from the patrol
            // section. We need to replicate: if v77==0, scan once for one worker.
            // v77 tracks how many patrol workers were found. Here we do a fresh scan
            // for one idle worker to put in v68.
            // 0x4568aa..0x45692a: scan for first idle worker at bldgHandle:
            //   word_12CE910[v56] != -1 && dword_12CEA7C[v56/2] == bldgHandle
            //   && byte_12CEA75[v56*2] && !dword_12CEA8C[v56/2]
            //   && dword_12CEA94[v56/2] && *(ao+44)==*(employer+1)
            // Note: stride for this loop is 268 (0x10C), stopping at 205824.
            // wait: disasm says: v56 += 268; while(!v77 && v56<205824)
            // 205824 / 268 = 768 — same number of persons. But stride 268??
            // Actually this is a DIFFERENT column walk: word_12CE910[v56] where
            // v56 increments by 268 and stops at 205824 = 768*268. This maps to
            // dword_12CEA7C accessed as dword_12CEA7C[v56/2] (v56 is a word-index).
            // But 268 is not a recognized person-record stride. Looking at the
            // decompile more carefully:
            //   v56 += 268; while(!v77 && (int)v56 < 205824)
            //   word_12CE910[v56]  — word index, so byte offset = 2*v56
            //   dword_12CEA7C[v56/2] — dword index v56/2, byte offset = 4*(v56/2) = 2*v56
            //   byte_12CEA75[v56*2] — byte index v56*2
            //   dword_12CEA8C[v56/2] — same dword indexing
            //   dword_12CEA94[v56/2] — same
            // So v56 is a WORD index. v56 starts at 0, increments by 268.
            // byte offset of word_12CE910[v56] = 2*v56 = 0, 536, 1072, ... (stride 536)
            // So word_12CE910[268*i] is person i. v56/2 = 134*i → dword_12CEA7C[134*i]
            //   = g_persons[i]+kP_employer (at offset 0x16C). Confirmed: stride 536 bytes.
            // byte_12CEA75[v56*2] = byte_12CEA75[536*i] = g_persons[i]+kP_profByte. OK.
            {
                bool foundWorker = false;
                for (unsigned int si = 0; si < 768u && !foundWorker; ++si) {
                    u8* p = pr(si);
                    if (rd16(p, kP_marker) == -1) continue;
                    if (rd32(p, kP_employer) != bldgHandle) continue;
                    if (!rd8(p, kP_profByte)) continue;
                    if (rd32(p, kP_busy)) continue;
                    u8* ao  = rdptr(p, kP_actionObj);
                    if (!ao) continue;
                    u8* emp = rdptr(p, kP_employer);
                    if (!emp) continue;
                    if (rd32(ao, 44) != rd32(emp, kB_id1)) continue;
                    cmd.workerIds[0] = g_personIds[si];
                    foundWorker = true;
                }
            }

            // 0x456934: if (v68 != -1) QueueRequestSlotReset28(...)
            if (!cmd.workerIds.empty() && cmd.workerIds[0] != -1) {
                SinkPush(cmd);  // 0x45693d
            }
        }
    }

    // LABEL_139:
LABEL_139:
    if (v78 < 2)
        return 0;  // 0x45694a: if (v78<2) return result

    // If v78 >= 2, fall through (v78==2 from a nobility-branch that jumped here)
    return 0;

    // -----------------------------------------------------------------------
    // LABEL_38: Countryside patrol ("im Umland").
    // gilde.exe 0x455ffe → 0x456004..0x45622c
    // -----------------------------------------------------------------------
LABEL_38:
    {
        // (Sprintf_0 debug log: dropped)
        MeisterCommand cmd{};
        // 0x45602f: ch = 0x65; (after Light_SetGrayColorThunk, mov var_12C, ch)
        cmd.cmdType = 101;   // 0x65 = 101 — countryside patrol (verified via disasm @0x45602f)
        bldgRec     = rdptr(mr, kM_bldgRec);
        bldgOwner   = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
        cmd.actorId = g_personIds[(int)bldgOwner];
        cmd.buildingId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
        cmd.timePacked = (u64)g_meisterGameTime.day | ((u64)g_meisterGameTime.hour << 32);
        cmd.timeExtra  = g_meisterTimeExtra;
        cmd.timeTail   = g_meisterTimeTail;
        cmd.mode       = 2;  // 0x456094: mov var_FA, al (al=2 from 'mov al,2')

        // 0x4560a0..0x4560b4: pick random direction from Auflauerlegen table
        int dirIdx = (int)(unsigned)RandomModulo(8u);
        // 0x4560ba..0x4560d1: 2-byte-at-a-time copy into var_D8 buffer
        char dirBuf[32] = {};
        CopyAuflauerlegen(dirBuf, dirIdx);
        // dirBuf lands in the command payload at an offset corresponding to the
        // direction-name field (var_D8 @ [esp+0x458] relative to cmd base).
        // In MeisterCommand we store it as extra0/extra1 pair (or the raw name).
        // Since MeisterCommand doesn't have a dedicated field for the direction string,
        // we pack the first 4 bytes into extra0 and the next 4 into extra1 as-is.
        // (The command structure's consumer decodes this as a 32-byte string region
        //  in the 248-byte packet; the reimpl just captures the raw bytes.)
        std::memcpy(&cmd.extra0, dirBuf + 0, 4);
        std::memcpy(&cmd.extra1, dirBuf + 4, 4);

        // 0x4560e4: v73[2] = *(bldgRec+1)  (building id)
        cmd.srcId = bldgRec ? rd32(bldgRec, kB_id1) : 0;

        // 0x4560f8: v73[3] = *(FindStorableObject(bldgRec)+1)  [__int16*-typed +1 == byte +2]
        // disasm 0x4560fd: mov eax,[eax+2] — actual byte offset is 2, NOT 1.
        {
            u8* storable = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->findStorableObject)
                storable = g_meisterLeaves->findStorableObject(bldgRec);
            cmd.targetId = storable ? rd32(storable, 2) : 0;
        }

        // 0x456111: worker count = min(v79, 6)
        int numWorkers = (v79 >= 6) ? 6 : v79;
        int collected = 0;
        // 0x456127: scan for up to numWorkers idle workers
        for (unsigned int si = 0; si < 768u && collected < numWorkers; ++si) {
            u8* p = pr(si);
            if (rd16(p, kP_marker) == -1) continue;
            if (rd32(p, kP_employer) != bldgHandle) continue;
            if (!rd8(p, kP_profByte)) continue;
            if (rd32(p, kP_busy)) continue;
            u8* ao  = rdptr(p, kP_actionObj);
            if (!ao) continue;
            u8* emp = rdptr(p, kP_employer);
            if (!emp) continue;
            if (rd32(ao, 44) != rd32(emp, kB_id1)) continue;
            cmd.workerIds.push_back(g_personIds[si]);
            ++collected;
        }
        // 0x45619a: fill remaining slots with -1 up to count 6
        while ((int)cmd.workerIds.size() < 6)
            cmd.workerIds.push_back(-1);

        // 0x456988: if (v67 != -1 && v28 >= 2) QueueRequestSlotReset28(...)
        // v67 = first field of var_F8 (the command direction word), v28 = collected count.
        // v67 != -1: the direction buffer was set (it's the first dword after the gametime block).
        // Effectively: if collected >= 2 (v28 >= 2) and direction was set.
        if (collected >= 2) {
            SinkPush(cmd);  // 0x456995
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x4588d0 — VIBE_Ai_CalcMeisterAmbush (__usercall, eax=a1)
// Ambush master: lay ambush + optionally attack.
//
// Phases identical to Wache for the management cluster (storage/hire/train...).
// Then:
//   5. Weekly gate (same: hour>6 && (id+hour)%3==0).
//   6. Day-flag gate (bit 0x20 of mr+436).
//   7. Count staff.
//   8. Mode (v65):
//      - building owner kind 6/7 (nobility): RandomModulo(2); if v65 && target!=-1
//        && RandomModulo(2): goto LABEL_21 (ambush cmd).
//        else: if v65==3 goto LABEL_48; else LABEL_52.
//      - merchant: RandomFloatScaled()>0.85 && v67>=3 && priceDay+7<currentDay:
//        v65=3 → LABEL_48; else v65=RandomModulo(4) → LABEL_51.
//   LABEL_48: CalcAngriff; if returns 0: goto LABEL_21 (ambush).
//   LABEL_52: if (!v65 && RandomFloatScaled() < 0.75) goto LABEL_21.
//             if (v65) goto LABEL_21.
//             if (!v65): target-selection scan (build ambush site) → emit cmdType=98.
//   LABEL_21: emit "direction ambush" command (cmdType=0x48=72 per disasm ch=0x48).
//
// cmdTypes (verified via disasm):
//   72  (0x48) — LABEL_21 direction ambush  (ch=0x48 at 0x458ad3)
//   98  (0x62) — target ambush               (0x62 at 0x458fb8)
//
// ===========================================================================
void CalcMeisterAmbush(u8* meisterRec) {
    using namespace guild::util;
    using namespace guild::sim::aimei;

    u8* mr = meisterRec;

    // 0x4588f3: v49 = *(*(a1+364)+93)  = bldgRec.sceneRoot
    u8* bldgRec = rdptr(mr, kM_bldgRec);
    i32 bldgRoot = bldgRec ? rd32(bldgRec, kB_sceneRoot93) : 0;

    // 0x4588f4: v69 = 0
    int v69 = 0;  // idle-worker found-one flag for the single-worker slot in ambush cmd

    // 0x458905: QueryFind(bldgRoot, 2,6,0, type=278)
    u8* v3 = nullptr;
    if (g_meisterLeaves && g_meisterLeaves->queryFind) {
        static const int kFilters278[] = { 6, 0, 278 };
        v3 = g_meisterLeaves->queryFind(bldgRoot, kFilters278, 3);
    }

    // 0x45890b: if v3 — resource management
    if (v3) {
        g_workOrderCount = 0;
        g_stockRowCount  = 0;
        MeisterCollectStorageItems(mr);
        i32 neededId = bldgRec ? rd32(bldgRec, kB_needed57) : 0;
        MeisterGatherRequiredItems(mr, neededId);
        EquipStaffWeapon(mr, 0);
        MeisterCollectTransporters(mr);
        MeisterTradeManageStorage(mr);
    }

    MeisterHireStaff(mr);
    MeisterFillAiSlots(reinterpret_cast<SceneNode*>(v3), mr, 10);
    MeisterTrainStaff(reinterpret_cast<SceneNode*>(v3), mr, 10);
    MeisterRenovateBuilding(mr);
    MeisterFlagIdleStaff(mr);    // 0x458977: always called (unlike Wache which has a gate)
    MeisterFindFreeStaffSlot(mr);

    // -----------------------------------------------------------------------
    // 0x458991: Weekly-tick gate (same as Wache)
    // -----------------------------------------------------------------------
    u16 hour = gtHour();
    if (hour <= 6u) {
        wr8(mr, kM_dayFlags, rd8(mr, kM_dayFlags) & ~u8(0x20u));
        return;
    }
    i32 personId = rd32(mr, kM_id);  // *(a1+4)
    if (((unsigned)personId + (unsigned)hour) % 3u != 0u) {
        wr8(mr, kM_dayFlags, rd8(mr, kM_dayFlags) & ~u8(0x20u));
        return;
    }

    // 0x4589af: dayFlags bit 0x20 gate
    u8 dayFlags = rd8(mr, kM_dayFlags);
    if (dayFlags & 0x20u) return;
    wr8(mr, kM_dayFlags, dayFlags | u8(0x20u));  // 0x4589ce

    // -----------------------------------------------------------------------
    // 0x4589c2: Count staff
    // -----------------------------------------------------------------------
    bldgRec = rdptr(mr, kM_bldgRec);
    i32 bldgHandle = rd32(mr, kM_bldgRec);

    int v9 = 0, v67 = 0;
    CountBuildingStaff(bldgHandle, v9, v67);

    // -----------------------------------------------------------------------
    // Debug HUD snapshot
    // -----------------------------------------------------------------------
    {
        if (g_aiSelMeisterBuilding == rd32(mr, kM_bldgRec)) {
            i32 targetId = rd32(mr, kM_target);  // *(a1+448)
            g_aiSelMeisterCount = v67;
            if (targetId != -1) {
                u8* found = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->buildingFindById)
                    found = g_meisterLeaves->buildingFindById(targetId);
                g_aiSelMeisterBuildingRec = found ? (i32)(uintptr_t)found : 0;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 0x458a29: staff-count minimum guard
    // -----------------------------------------------------------------------
    int v12 = v9 >> 1;
    int v13 = v9 - v12;
    if (v12 >= v13) v12 = v13;
    if (v12 > v67) {
        // Sprintf_0("keine Leute") — dropped
        return;
    }

    // -----------------------------------------------------------------------
    // 0x458a5f: building owner kind
    // -----------------------------------------------------------------------
    bldgRec   = rdptr(mr, kM_bldgRec);
    u16 bldgOwner = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
    u8 ownerKind  = rd8(pr((int)bldgOwner), kP_kind);

    int v65 = 0;  // mode: 0=fall-through, 3=angriff, else ambush

    if (ownerKind == 6 || ownerKind == 7) {
        // 0x458a82: v65 = RandomModulo(2)
        v65 = (int)(unsigned)RandomModulo(2u);
        // 0x458aa1: if v65 && *(a1+448)!=-1 && RandomModulo(2)
        if (v65 && rd32(mr, kM_target) != -1) {
            if ((int)(unsigned)RandomModulo(2u)) {
                goto LABEL_21;  // 0x458aaf
            }
        }
        // LABEL_51:
        // 0x458ce4: if (v65 != 3) goto LABEL_52; else goto LABEL_48
        if (v65 == 3)
            goto LABEL_48;
        goto LABEL_52;
    } else {
        // 0x458ca2: merchant branch
        // if (RandomFloatScaled() > 0.85 && v67>=3 && 7-dword_63C744<=(int)qword_13CE852)
        if (RandomFloatScaled() > kGate085 && v67 >= 3) {
            int dword63C744 = BuildingPriceMode();
            if (7 - dword63C744 <= gtDay()) {
                v65 = 3;
                goto LABEL_48;
            }
        }
        // 0x458cc6: v65 = RandomModulo(4)
        v65 = (int)(unsigned)RandomModulo(4u);
        // 0x458cc6 falls through to loc_458CDC (LABEL_51) — NOT directly to LABEL_52.
        // RandomModulo(4) can yield 3, which must route to LABEL_48 (CalcAngriff).
        // LABEL_51 (loc_458CDC): if (v65 == 3) goto LABEL_48; else fall to LABEL_52.
        if (v65 == 3)
            goto LABEL_48;
        goto LABEL_52;
    }

LABEL_48:
    // 0x458cb8: CalcAngriff(a1, v67, (char*)a1)
    if (!CalcAngriff(mr, v67))
        goto LABEL_21;
    // else: fall through (return after LABEL_52 exits)
    goto LABEL_52;

LABEL_52:
    // 0x458cfe: if (!v65 && RandomFloatScaled() < dbl_6198A0) goto LABEL_21
    if (!v65 && RandomFloatScaled() < kGate075) {
        goto LABEL_21;
    }
    // 0x458d0d: if (!v65)
    if (!v65) {
        // -----------------------------------------------------------------------
        // Target-selection scan: find the best building to ambush.
        // gilde.exe 0x458d21..0x4590da
        //
        // First: find max tile danger score (float) via 2-level loop over the grid.
        // The Ambush version uses float arithmetic (v73, v63 are floats).
        //   v73 = running max danger (float)
        //   inner: v72 = (double)v74 * flt_6198A8 + v63  (= dangerA * 0.125 + dangerB)
        //   update: v73 = max(v73, v72)
        //   zero-guard: if (LODWORD(v36) & 0x7FFFFFFF)==0: v73 = 1.0
        // -----------------------------------------------------------------------
        // 0x458d21: v32 = 1536; outer do{ v61=0; v34=24*v62[0]; inner: ... } while (v62[0]<8)
        // The Ambush grid loop is DIFFERENT from Wache: it iterates with outer=row, inner=col
        // matching the word_12349A2/A0 layout. v34 starts at 24*row, adds 192 per inner step.
        // After the loops, the max danger score is in v73 (float).
        float v73_max = 0.0f;
        {
            // outer: v62[0] = row = 0..7
            for (int row = 0; row < 8; ++row) {
                // inner: v34 byte-offset = 24*row + 192*col (but Ambush starts at 24*row and
                // adds 192 per col, looping to v32=1536). Wait: the decompile shows:
                // v34 = 24 * v62[0]; do { ... v34 += 192; } while (v34 != v32);
                // v32 starts at 1536 (total bytes) and increments by 24 each outer step.
                // So for row=0: v34 = 0, loop until v34==1536 (192*8 cols). Stride 192=row-stride.
                // But that means inner loop visits byte offsets 0,192,384,...,1344 → 8 cols.
                // outer row: v62[0]; inner: byte offset = 24*row + 192*col? No:
                // v34 starts at 24*v62[0] (= 24*row). Adds 192 each step.
                // For row=0: v34 = 0, 192, 384, ..., 1344 → then v34=1536=v32 stops.
                // This visits: (24*0+192*col) for col 0..7, but base is 24*row=0.
                // Actually: byte-offset = 24*row + 192*col gives (for row=0) 0+0=0, 0+192=192,...
                // But 24*row is added to 192*col via the increment. For row=1: v34=24,216,408,...
                // This is the transposed access pattern vs Wache (inner=row, outer=col vs Wache).
                // Ambush outer loop: v62[0]=row (0..7), v32 start=1536 increments 24/iter.
                // For row=r: v34 starts at 24*r, adds 192 per inner step, until v34==1536+24*r.
                // Inner iterates 8 times: v34 = 24*r + 192*0, 24*r + 192*1, ..., 24*r + 192*7.
                // Tile byte offset = 24*r + 192*col. This is column-major (col varies fastest).

                for (int col = 0; col < 8; ++col) {
                    // tile byte offset = 24*row + 192*col  (same formula, different loop order)
                    int tileOff = 24 * row + kCityTileRowStride * col;
                    // 0x458d5f: v74 = word_12349A2[v34/2]  (dangerB word)
                    u16 dangerB, dangerA;
                    std::memcpy(&dangerB, g_cityTileGrid + tileOff + 2, 2);
                    // 0x458d76: v74 = word_12349A0[v34/2]  (dangerA word, reuses v74)
                    std::memcpy(&dangerA, g_cityTileGrid + tileOff + 0, 2);
                    // 0x458d6f: v35 = (double)v74 (first load of dangerB)
                    double v35 = (double)dangerB;
                    // 0x458d7d: v63 = v35  (store as float)
                    float v63_tile = (float)v35;
                    // 0x458d98: *(float*)&v72 = (double)v74 * flt_6198A8 + v63
                    //   = dangerA * 0.125 + dangerB
                    float v72 = (float)((double)dangerA * (double)kTileBWeight + (double)v63_tile);
                    // 0x458db0: if (v73 <= (double)v72) v36 = v72; else v36 = LODWORD(v73)
                    float v36_f;
                    if ((double)v73_max <= (double)v72)
                        v36_f = v72;   // 0x45910d
                    else
                        v36_f = v73_max;
                    v73_max = v36_f;
                }
            }
        }
        // 0x458e06: if ((LODWORD(v36) & 0x7FFFFFFF) == 0) v73 = 1.0
        {
            u32 lod;
            std::memcpy(&lod, &v73_max, 4);
            if ((lod & 0x7FFFFFFFu) == 0)
                v73_max = 1.0f;  // 0x459119
        }

        // -----------------------------------------------------------------------
        // 0x458e0c..0x458f6e: Object array sweep — find best building to ambush.
        // Sweep 256 buildings (g_objects, 169-byte stride). Filter:
        //   - alive (*v37 != 0)
        //   - owner word (v37+39) != meister's building owner AND != 0xFFFF
        //   - bit0 of v37[90] == 0
        //   - owner != *(u16*)dword_6498E4  (local player city word, same as g_angriffLocalPlayerWord)
        //   - !VIBE_Building_IsProductionType(v37)
        //   - has scene node: *(v37+97) != 0 && WorldToCityTile(*(v37+97)+76, &row, &col)
        // Scoring:
        //   tileScore = dangerB + dangerA * 0.125  (at (row,col))
        //   normScore = 1.0 - tileScore / v73_max  (float)
        //   rng0 = RandomModulo(0x64) (u16, cast double)
        //   finalScore = rng0 * 0.01 * normScore  (double)
        //   winner: first candidate OR (sum*finalScore > sum_best*bestScore)
        //     where sum = PersonSumCurrencyHeld(g_persons[ownerWord])
        // -----------------------------------------------------------------------
        // Access the local-player word via the public accessor in calc_angriff.cpp.
        // CalcAngriff_GetLocalPlayerWord() is defined in ai_meister_calc_angriff.cpp
        // and is part of the guild::sim namespace (defined there; declared here).
        u16 localPlayerWord = CalcAngriff_GetLocalPlayerWord();

        bldgRec   = rdptr(mr, kM_bldgRec);
        bldgOwner  = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;

        u8* v70 = nullptr;  // best target object record
        double v68_score = 0.0;  // best score

        u8* v37 = aimei::g_objectArrayBase;  // 0x458e0c: v37 = dword_13CE298
        for (int v38 = 0; v38 < 256; ++v38, v37 += 169) {
            // 0x458e22: *v37 (alive byte)
            if (!v37[0]) continue;

            // 0x458e3d: v39 = *(u16*)(v37+39)  (owner word)
            u16 v39 = rdu16(v37, 39);

            // 0x458e76: filters
            if ((u16)v39 == bldgOwner) continue;                    // own building
            if (v39 == 0xFFFFu) continue;                           // invalid owner
            if (v37[90] & 1u) continue;                             // flag bit
            if ((u16)v39 == localPlayerWord) continue;              // local player
            // 0x458e83: !VIBE_Building_IsProductionType(v37)
            if (g_meisterLeaves && g_meisterLeaves->isProductionType) {
                if (g_meisterLeaves->isProductionType(v37)) continue;
            }

            // 0x458e83: v40 = *(v37+97)  (scene node / position ptr)
            i32 v40 = rd32(v37, 97);
            int tRow = 0, tCol = 0;
            float v71_norm = 0.0f;

            if (v40 && g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                if (g_meisterLeaves->worldToCityTile(
                    reinterpret_cast<float*>(static_cast<uintptr_t>(static_cast<u32>(v40) + 76u)),
                    &tRow, &tCol)) {
                    // 0x458ebf: v41 = 24*v62[0] + 192*v61  (byte offset of tile)
                    int tileOff = 24 * tCol + kCityTileRowStride * tRow;  // col/row reversed per Ambush loop
                    // 0x458eca: LODWORD(v64) = word_12349A2[tileOff/2]
                    u16 tDangerB, tDangerA;
                    std::memcpy(&tDangerB, g_cityTileGrid + tileOff + 2, 2);
                    // 0x458eda: v74 = word_12349A0[tileOff/2]
                    std::memcpy(&tDangerA, g_cityTileGrid + tileOff + 0, 2);
                    // 0x458f02: v71 = 1.0 - ((double)SLODWORD(v64) + (double)v74 * flt_6198A8) / v73
                    v71_norm = (float)(1.0 - ((double)(i32)tDangerB +
                               (double)tDangerA * (double)kTileBWeight) /
                               (double)v73_max);
                } else {
                    v71_norm = 0.0f;  // 0x45912b
                }
            } else {
                v71_norm = 0.0f;  // 0x45912b: no scene node
            }

            // 0x458f15: v74 = RandomModulo(0x64)  (u16)
            u16 rng0 = RandomModulo(0x64u);
            // 0x458f3d: v71 = (double)v74 * dbl_6198B0 * v71
            //   = rng0 * 0.01 * normScore
            double finalScore = (double)rng0 * kRandScale001 * (double)v71_norm;

            // 0x4591a5: if (!v70) first candidate; else compare:
            //   SumCurrencyHeld(g_persons[ownerWord(v37)]) * finalScore
            //   > SumCurrencyHeld(g_persons[ownerWord(v46/v70)]) * bestScore
            if (!v70) {
                v70 = v37;
                v68_score = finalScore;
            } else {
                // 0x4591a5:
                i32 sumNew = 0, sumBest = 0;
                if (g_meisterLeaves && g_meisterLeaves->sumCurrencyHeld) {
                    u8* recNew  = pr((int)(u16)v39);
                    u8* recBest = pr((int)rdu16(v70, 39));
                    sumNew  = g_meisterLeaves->sumCurrencyHeld(recNew);
                    sumBest = g_meisterLeaves->sumCurrencyHeld(recBest);
                }
                // if ((double)sumNew * finalScore > (double)sumBest * v68_score)
                if ((double)sumNew * finalScore > (double)sumBest * v68_score) {
                    v70 = v37;
                    v68_score = finalScore;
                }
            }
        }

        // 0x458f7e: *(a1+448) = *(v70+1)  (store best target building id)
        if (v70) {
            wr32(mr, kM_target, rd32(v70, 1));  // store target building id at +448
        }

        // -----------------------------------------------------------------------
        // 0x458fb3: Emit ambush target command (cmdType=98 = 0x62).
        // -----------------------------------------------------------------------
        // (Sprintf_0 debug log: dropped)
        if (v70) {
            MeisterCommand cmd{};
            cmd.cmdType = 98;   // 0x458fb8: mov var_150, 62h  (0x62 = 98 = ambush target)
            bldgRec     = rdptr(mr, kM_bldgRec);
            bldgOwner   = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
            cmd.actorId   = g_personIds[(int)bldgOwner];
            cmd.buildingId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            cmd.timePacked = (u64)g_meisterGameTime.day | ((u64)g_meisterGameTime.hour << 32);
            cmd.timeExtra  = g_meisterTimeExtra;
            cmd.timeTail   = g_meisterTimeTail;
            cmd.mode       = 2;  // 0x458ffb: mov bl,2 (stored at +0x11E)
            // 0x45901c: v60[0] = -1  (worker slot init, -1 = none yet)
            cmd.workerIds.push_back(-1);
            // 0x459026: v60[1] = *(v70+1)  (target building id)
            cmd.targetId = rd32(v70, 1);
            // 0x459036: v60[4] = *(bldgRec+1)  (own building id)
            cmd.srcId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
            // 0x459043: FindStorableObject(bldgRec)
            {
                u8* storable = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->findStorableObject)
                    storable = g_meisterLeaves->findStorableObject(bldgRec);
                // 0x459048: v60[5] = *(storable+1)  [__int16*-typed +1 == byte +2]
                // disasm 0x459048: mov eax,[eax+2] — actual byte offset is 2, NOT 1.
                cmd.extra1 = storable ? rd32(storable, 2) : 0;
            }

            // 0x45905b: if (!v69): scan for first idle worker
            if (!v69) {
                for (unsigned int si = 0; si < 768u && !v69; ++si) {
                    u8* p = pr(si);
                    if (rd16(p, kP_marker) == -1) continue;
                    if (rd32(p, kP_employer) != bldgHandle) continue;
                    if (!rd8(p, kP_profByte)) continue;
                    if (rd32(p, kP_busy)) continue;
                    u8* ao  = rdptr(p, kP_actionObj);
                    if (!ao) continue;
                    u8* emp = rdptr(p, kP_employer);
                    if (!emp) continue;
                    if (rd32(ao, 44) != rd32(emp, kB_id1)) continue;
                    cmd.workerIds[0] = g_personIds[si];
                    ++v69;
                }
            }
            // 0x4590e4: if (v60[0] != -1) QueueRequestSlotReset28(...)
            if (!cmd.workerIds.empty() && cmd.workerIds[0] != -1) {
                SinkPush(cmd);
            }
        }

        // 0x4590fa: if (v65 >= 1) goto LABEL_21
        if (v65 >= 1)
            goto LABEL_21;
        return;  // v65==0 and target-selection done — done.
    }

    // v65 >= 1: goto LABEL_21
    if (v65 >= 1)
        goto LABEL_21;
    return;

    // -----------------------------------------------------------------------
    // LABEL_21: Emit direction-ambush command (cmdType=72 = 0x48).
    // gilde.exe 0x458aaf..0x459252
    // -----------------------------------------------------------------------
LABEL_21:
    {
        // (Sprintf_0 debug log: dropped)
        MeisterCommand cmd{};
        // 0x458ad3: ch = 0x48; mov var_150, ch  → cmdType = 0x48 = 72
        cmd.cmdType = 72;   // 0x48 = direction-ambush (LABEL_21 branch)
        bldgRec     = rdptr(mr, kM_bldgRec);
        bldgOwner   = bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
        cmd.actorId   = g_personIds[(int)bldgOwner];
        cmd.buildingId = bldgRec ? rd32(bldgRec, kB_id1) : 0;
        cmd.timePacked = (u64)g_meisterGameTime.day | ((u64)g_meisterGameTime.hour << 32);
        cmd.timeExtra  = g_meisterTimeExtra;
        cmd.timeTail   = g_meisterTimeTail;
        cmd.mode       = 2;  // 0x458b27: mov al,2; mov var_11E, al

        // 0x458b35..0x458b4e: random direction from Auflauerlegen table
        int dirIdx = (int)(unsigned)RandomModulo(8u);
        // 0x458b5c..0x458b72: 2-byte copy loop into v17 (= (char*)v60)
        char dirBuf[32] = {};
        CopyAuflauerlegen(dirBuf, dirIdx);
        std::memcpy(&cmd.extra0, dirBuf + 0, 4);
        std::memcpy(&cmd.extra1, dirBuf + 4, 4);

        // 0x458b78: worker count = min(v67, 8); v22 = capped count
        int numWorkers = (v67 >= 8) ? 8 : v67;
        int v23 = 0, v24 = 0;  // byte-advance / worker count

        // 0x458b8d: if (v22 > 0) collect workers
        if (numWorkers > 0) {
            // 0x458b8f: v25=0, v26=0
            // 0x4591bf: if (v23 >= 4 || v25 >= 411648) break
            // The cap of 4 at 0x4591bf (checked BEFORE incrementing): this inner loop
            // collects at most 4 workers even if numWorkers up to 8. The break is:
            //   if (v23 >= 4 || v25 >= 411648) break
            // So workers collected = min(min(v67,8), 4) = min(v67, 4).
            // Each collected worker: v26 += 4; ++v24; ++v23;
            //   store g_personIds[si] at (char*)&v57 + v26  (after gametime/mode fields)
            for (unsigned int si = 0; si < 768u && v23 < 4; ++si) {
                u8* p = pr(si);
                if (rd16(p, kP_marker) == -1) continue;
                if (rd32(p, kP_employer) != bldgHandle) continue;
                if (!rd8(p, kP_profByte)) continue;
                if (rd32(p, kP_busy)) continue;
                u8* ao  = rdptr(p, kP_actionObj);
                if (!ao) continue;
                u8* emp = rdptr(p, kP_employer);
                if (!emp) continue;
                if (rd32(ao, 44) != rd32(emp, kB_id1)) continue;
                cmd.workerIds.push_back(g_personIds[si]);
                ++v24;
                ++v23;
            }
        }
        // 0x458ba1: fill remaining up to 8 slots with -1
        while ((int)cmd.workerIds.size() < 8)
            cmd.workerIds.push_back(-1);

        // 0x459245: if (v59 != -1 && v23 >= 2) QueueRequestSlotReset28(...)
        // v59 is the first dword of the direction-name buffer (var_11C at [esp+0x438]).
        // After CopyAuflauerlegen, v59 contains the first 4 bytes of the string (non-zero
        // for any valid name). The check ensures the direction was set (not null-string).
        // We check dirBuf[0] != 0 (string is non-empty) and v23 >= 2.
        if (dirBuf[0] != 0 && v23 >= 2) {
            SinkPush(cmd);  // 0x459252
        }
    }
}

}  // namespace guild::sim
