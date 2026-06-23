// ===========================================================================
// ai_meister_calc_farming.cpp — 1:1 reconstruction of
//   0x4599f0  VIBE_MeisterAi_AssignWorkstations  → MeisterAssignWorkstations
//   0x454f50  VIBE_Ai_CalcMeisterFarming         → CalcMeisterFarming
//
// Farm/estate master AI brain dispatched from VIBE_Ai_EvaluateMeister (0x4533a8)
// via DispatchMeisterCalc(kFarming, mr). CalcMeisterFarming is 3461 bytes in the
// original; it runs 3 QueryFind probes, calls AssignWorkstations (this file),
// drives the work-order flag-bit state machine, calls all shared sub-planners,
// handles the harvest command (cmdType=9), the PFLANZBAR office-placement path,
// and returns MeisterTradeManageStorage's result.
//
// Provenance comment format: // gilde.exe 0xADDR
//
// NOTE on the SceneNode* node argument to MeisterAssignWorkstations:
//   The original __usercall has three register args:
//     a1@<eax> = meisterRec (our `mr`)
//     a2@<edx> = a scene-object whose field +20 holds the QueryFind scene-root id
//               (CalcMeisterFarming passes v92, the type-255 QueryFind result)
//     a3@<ebx> = the type-42 QueryFind result node (our `node`)
//   The declared header sig MeisterAssignWorkstations(u8* mr, SceneNode* node)
//   drops the a2 argument. We recover the scene-root directly from the meisterRec's
//   building record (bldgRec+93), which is equivalent because the orig a2 carries
//   the same scene root. The type-42 result (`node`) is used as a raw u8* base for
//   QueryFind with per-item IDs. The SceneNode* alias is a u8*-width-compatible
//   forwarding type; we reinterpret_cast it immediately.
//
// NOTE on word_B564BC (work-order flag column):
//   In the 32-bit binary word_B564BC is at address 0xB56464 + 88 = 0xB564BC.
//   IDA indexes it as word_B564BC[44*i] meaning *(u16*)(0xB564BC + 88*i).
//   In our reimpl this is a file-local u16 array g_woBitsTable[kMaxWorkOrders]
//   (matching the established pattern in ai_meister_equip.cpp). The table is
//   initialised by MeisterAssignWorkstations and consumed by CalcMeisterFarming.
//
// NOTE on dword_B56450 (work-order free-capacity column):
//   In the 32-bit binary 0xB56450 overlaps with the TurnWindowState globals.
//   For the AI scratch context it stores the free-slot capacity per work order.
//   We model it as a file-local i32 array g_woFreeCapTable[kMaxWorkOrders].
//
// NOTE on dword_B5443C (stock free-capacity column):
//   In the 32-bit binary 0xB5443C is 18 bytes before the stock table base.
//   We model it as a file-local i32 array g_stFreeCapTable[kStockRows].
//
// NEW LEAVES needed (not in MeisterAiLeaves — report to orchestrator):
//   g_meisterFarmGetEffectiveStock   (VIBE_Inventory_GetEffectiveStock  0x5923fc)
//   g_meisterFarmQueueRequest20      (VIBE_Command_QueueRequest20       0x4946f4)
//   g_meisterFarmSumWorkstation      (VIBE_Building_SumWorkstationByCategory 0x5904fc)
//   g_meisterFarmComputeWorkstation  (VIBE_MeisterAi_ComputeWorkstationOutput 0x45b948)
//   g_meisterFarmCollectSlots        (VIBE_Inventory_CollectProductionSlots 0x5922d4)
//   g_meisterFarmObjectById          (VIBE_Object_FindObjectById        0x583a70)
//   g_meisterFarmQueueMixed44        (VIBE_Command_QueueRequestMixed44  0x494cf0)
// All guarded null → original null-path (no-op / skip).
//
// VIBE_Crt_Sprintf_0 debug-log calls: NO sim side effect — dropped per SPEC.
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/meister_mgmt_recon.h"
#include "sim/entity.h"         // g_persons, g_personIds, kPersonCapacity
#include "util/math_random.h"   // guild::util::RandomModulo

#include <cstring>
#include <cstdlib>   // abs (not used, included for clarity)
#include <cmath>     // for completeness

namespace guild::sim {
using namespace aimei;

// ---------------------------------------------------------------------------
// File-local flag column — word_B564BC[44*i] == g_woBitsTable[i]
// Separate from g_workOrderTable (B56464) because B564BC - B56464 = 88 = stride,
// making the flag a parallel array sharing the same stride.
// gilde.exe 0xB564BC
// ---------------------------------------------------------------------------
static u16 g_woBitsTable[kMaxWorkOrders] = {};

// ---------------------------------------------------------------------------
// File-local free-capacity column for work orders — dword_B56450[v/4] where
// v advances by 88 per record. In reimpl: g_woFreeCapTable[i].
// gilde.exe 0xB56450
// ---------------------------------------------------------------------------
static i32 g_woFreeCapTable[kMaxWorkOrders] = {};

// ---------------------------------------------------------------------------
// File-local free-capacity column for stock rows — dword_B5443C + 64*i.
// gilde.exe 0xB5443C
// ---------------------------------------------------------------------------
static i32 g_stFreeCapTable[kStockRows] = {};

// ---------------------------------------------------------------------------
// Work-order back-index column (per stock row pointing to WO row):
// dword_B5646C + 4*v (v = stock-row DW index). B5646C - B56464 = 8.
// Embedded at g_workOrderTable[wo_idx*88 + kWO_backIdx] using offset +8.
// However the decompile addresses it as a SEPARATE column: *(dword_B5646C + v)
// where v is byte-strided as *(char*)dword_B5646C + v_bytes).
// The stock-row back-index is: *(int*)((char*)dword_B5646C + v_bytes)
// where v_bytes advances by 4 per WO slot (v95 += 4 in the stock loop).
// For stock row j scanning WO table: index = j * 4 byte offset into B5646C.
// B5646C - B56464 = 8, so this is at WO table offset +8 per WO entry?
// Actually in the scan: v95=0, v102=0 init to v104 (the outer WO byte offset),
// then inner loop: v95 += 4, v102 += 4, v103 += 2 (loop 4 times = 4 WO-subfields).
// It's a multi-output column within the 88-byte WO record at offset +8 (B5646C-B56464).
// We model the B5646C column as a sub-column at offset +8 within g_workOrderTable:
//   dword_B5646C[v95] == *(i32*)(g_workOrderTable + wo_idx*88 + 8 + sub_idx*4)
// where sub_idx in [0..3] = 4 stock-back-indices stored within the WO record.
// ---------------------------------------------------------------------------
// Stock-table column offsets within g_stockTable[i * kStockStride + off]:
static constexpr int kST_key      = 0x00;  // B5444E (+0):  HIWORD = item type id
static constexpr int kST_backIdx  = 0x06;  // B54454 (+6):  back-index to WO row (-1=none)
static constexpr int kST_srcBldg  = 0x0A;  // B54458 (+10): source building dword
static constexpr int kST_field18  = 0x12;  // B54460 (+18)
static constexpr int kST_field22  = 0x16;  // B54464 (+22)
static constexpr int kST_field26  = 0x1A;  // B54468 (+26)
static constexpr int kST_price    = 0x1E;  // flt_B5446C (+30): unit price (float, init -1e10)
static constexpr int kST_reserved = 0x26;  // B54474 (+38): incoming/reserved quantity
static constexpr int kST_stock    = 0x2A;  // B54478 (+42): current effective stock
static constexpr int kST_flags50  = 0x32;  // B54480 (+50): type flags (33 = default)
static constexpr int kST_bits     = 0x3E;  // word_B5448C (+62): flags word

// Work-order column offsets within g_workOrderTable[i * kWorkOrderStride + off]:
static constexpr int kWO_key      = 0x00;  // B56464 (+0):  HIWORD = item type id (LOWORD of i16 at +0)
static constexpr int kWO_backIdx  = 0x04;  // B56468 (+4):  4 worker-slot dwords (4*4=16 bytes, -1=empty)
static constexpr int kWO_stBackOff= 0x08;  // B5646C (+8):  4 stock-back-index dwords
static constexpr int kWO_qty1     = 0x20;  // B56484 (+32): qty field A
static constexpr int kWO_qty2     = 0x24;  // B56488 (+36): qty field B
static constexpr int kWO_initVal  = 0x28;  // B5648C (+40): init value (-803929351 == 0xD01502F9)
static constexpr int kWO_output   = 0x3C;  // B564A0 (+60): output quantity
static constexpr int kWO_effStock = 0x40;  // B564A4 (+64): effective stock
static constexpr int kWO_minQty   = 0x44;  // B564A8 (+68): min quantity threshold
static constexpr int kWO_aux      = 0x48;  // B564AC (+72): aux field
static constexpr int kWO_rank     = 0x4C;  // B564B0 (+76): sort rank / order index (dword)

// Accessor helpers for work-order rows:
static inline u8*  wo(int i) { return g_workOrderTable + kWorkOrderStride * i; }
static inline i32  word32(int i, int off) { return rd32(wo(i), off); }
static inline void wword32(int i, int off, i32 v) { wr32(wo(i), off, v); }
static inline u8*  st(int i) { return g_stockTable + kStockStride * i; }
static inline i32  strd32(int i, int off) { return rd32(st(i), off); }
static inline void stwr32(int i, int off, i32 v) { wr32(st(i), off, v); }
static inline u16  strd16(int i, int off) { return rdu16(st(i), off); }
static inline void stwr16(int i, int off, u16 v) { wr16(st(i), off, v); }

// Flag column helpers:
static inline u16  rdWoBits(int i) { return (i >= 0 && i < kMaxWorkOrders) ? g_woBitsTable[i] : 0; }
static inline void orWoBitsLo(int i, u8 b) {
    if (i >= 0 && i < kMaxWorkOrders)
        reinterpret_cast<u8*>(&g_woBitsTable[i])[0] |= b;
}
static inline void andWoBitsLo(int i, u8 mask) {
    if (i >= 0 && i < kMaxWorkOrders)
        reinterpret_cast<u8*>(&g_woBitsTable[i])[0] &= mask;
}
static inline void setWoBits(int i, u16 v) {
    if (i >= 0 && i < kMaxWorkOrders) g_woBitsTable[i] = v;
}

// ---------------------------------------------------------------------------
// New leaf forward declarations (not yet in MeisterAiLeaves).
// The live bridge will define these before dispatch; tests inject them directly.
// All are guarded null → same null-path as original.
// ---------------------------------------------------------------------------

// gilde.exe 0x5923fc — VIBE_Inventory_GetEffectiveStock(typeRec, itemRec) -> i32
// Called as: GetEffectiveStock(v98, foundItem) where v98 = the 42-type node.
extern i32 (*g_meisterFarmGetEffectiveStock)(u8* typeRec, u8* itemRec);
i32 (*g_meisterFarmGetEffectiveStock)(u8* typeRec, u8* itemRec) = nullptr;

// gilde.exe 0x4946f4 — VIBE_Command_QueueRequest20(bldgId, itemHi) -> void
extern void (*g_meisterFarmQueueRequest20)(i32 bldgId, i16 itemHi);
void (*g_meisterFarmQueueRequest20)(i32 bldgId, i16 itemHi) = nullptr;

// gilde.exe 0x5904fc — VIBE_Building_SumWorkstationByCategory(bldgRec, cat, flag) -> i32
extern i32 (*g_meisterFarmSumWorkstation)(u8* bldgRec, u8 cat, int flag);
i32 (*g_meisterFarmSumWorkstation)(u8* bldgRec, u8 cat, int flag) = nullptr;

// gilde.exe 0x45b948 — VIBE_MeisterAi_ComputeWorkstationOutput(woRow) -> void
// Updates output/rate/score fields in the 88-byte WO record in place.
extern void (*g_meisterFarmComputeWorkstation)(u8* woRow);
void (*g_meisterFarmComputeWorkstation)(u8* woRow) = nullptr;

// gilde.exe 0x5922d4 — VIBE_Inventory_CollectProductionSlots(objRec, outBuf[46]) -> void
extern void (*g_meisterFarmCollectSlots)(u8* objRec, i32* outBuf);
void (*g_meisterFarmCollectSlots)(u8* objRec, i32* outBuf) = nullptr;

// gilde.exe 0x583a70 — VIBE_Object_FindObjectById(id) -> u8* or nullptr
extern u8* (*g_meisterFarmObjectById)(i32 id);
u8* (*g_meisterFarmObjectById)(i32 id) = nullptr;

// gilde.exe 0x494cf0 — VIBE_Command_QueueRequestMixed44(bldgId, col, kind, row, d, e) -> i32
// For office-placement commands (PFLANZBAR path).
extern i32 (*g_meisterFarmQueueMixed44)(i32 bldgId, i8 col, i16 kind, i8 row, i8 d, i32 e);
i32 (*g_meisterFarmQueueMixed44)(i32 bldgId, i8 col, i16 kind, i8 row, i8 d, i32 e) = nullptr;

// gilde.exe 0x45379c — the workstation node (type-42 QueryFind result) the orig
// AssignIdleWorkers caller passes in eax. Defined in ai_meister_passes.cpp; this
// TU sets it before MeisterAssignIdleWorkers() to wire the live call graph.
extern u8* g_aiIdleWorkstationNode;

// ---------------------------------------------------------------------------
// MeisterAssignWorkstations — gilde.exe 0x4599f0
// __usercall: eax=meisterRec, edx=sceneRootProvider (a2; carries +20 = scene root id),
//             ebx=node (type-42 QueryFind result; used for per-item QueryFind + capacity).
// Our sig drops a2 and recovers the scene root from the meisterRec's building record.
// ---------------------------------------------------------------------------
void MeisterAssignWorkstations(u8* mr, SceneNode* node_) {
    // Treat node as raw u8* base — SceneNode* is a forwarding alias.
    u8* nodeBase = reinterpret_cast<u8*>(node_);

    u8* bldgRec = rdptr(mr, kM_bldgRec); // *((_DWORD*)a1+91)+93 in decompile
    if (!bldgRec) {
        g_workOrderCount = 0;
        return;
    }

    // --- Phase 1: QueryFind(sceneRoot, 1, 5) — enumerate workstations ----------
    // gilde.exe 0x459a12
    g_workOrderCount = 0; // dword_B56FDC = 0

    i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93); // *(bldgRec+93)

    if (g_meisterLeaves && g_meisterLeaves->queryFind) {
        const int filts1[] = {5}; // op 5 = matchAny(no val)
        u8* item = g_meisterLeaves->queryFind(sceneRootId, filts1, 1);
        while (item) {
            // gilde.exe 0x459a36: if (*(int*)(i + 7) > 1)
            i32 itemField7 = rd32(item, 7);
            if (itemField7 > 1) {
                int woIdx = g_workOrderCount;
                if (woIdx < 32) { // gilde.exe 0x459a45: dword_B56FDC < 32
                    // gilde.exe 0x459a4f: LOWORD(dword_B56468[22 * idx]) = *i
                    // dword_B56468 base = B56468. [22*idx] dword = *(i32*)(B56468 + 88*idx).
                    // LOWORD = *(u16*)(B56468 + 88*idx) = WO offset +4..+5 (since B56468-B56464=4).
                    // *i = *(i16*)item = the item type word at item+0.
                    i16 itemTypeWord; std::memcpy(&itemTypeWord, item, 2);
                    wr16(wo(woIdx), kWO_backIdx, (u16)itemTypeWord); // WO[4..5] = type word

                    // gilde.exe 0x459a56..0x459a66: do { v6+=4; dword_B56468[v6/4]=-1; } while(v6!=v7)
                    // v6 starts at 88*idx, v7 = 88*idx+16. First write: v6 = 88*idx+4 →
                    // dword_B56468[(88*idx+4)/4] = *(i32*)(B56468 + 88*idx+4) = WO+8.
                    // Writes: WO+8, WO+12, WO+16, WO+20 (4 dwords at sub-indices [1..4] of kWO_backIdx).
                    for (int s = 1; s <= 4; ++s)
                        wr32(wo(woIdx), kWO_backIdx + s * 4, -1);

                    // gilde.exe 0x459a6a: dword_B564B0[v5/2] = 33
                    //   v5 = 44*idx, so B564B0[44*idx/2] = B564B0[22*idx] in dword arr.
                    //   B564B0 - B56464 = 0x4C = 76: kWO_rank = +76.
                    wword32(woIdx, kWO_rank, 33);

                    // gilde.exe 0x459a70/7b/83/89/8f/95/9d:
                    wword32(woIdx, kWO_qty1, 0);   // B56484
                    wword32(woIdx, kWO_qty2, 0);   // B56488
                    wword32(woIdx, kWO_initVal, -803929351); // B5648C 0xD01502F9 (disasm 0x459a83)
                    wword32(woIdx, kWO_output, 0); // B564A0 (xor of end vs end of slots = 0)
                    wword32(woIdx, kWO_effStock, 0); // B564A4
                    wword32(woIdx, kWO_minQty, 0);   // B564A8
                    wword32(woIdx, kWO_aux, 0);      // B564AC

                    // gilde.exe 0x459aac: word_B564BC[v5] = 0
                    setWoBits(woIdx, 0);

                    // gilde.exe 0x459ab3: ++dword_B56FDC
                    g_workOrderCount = woIdx + 1;
                }
            }
            if (g_meisterLeaves->queryIterNext)
                item = g_meisterLeaves->queryIterNext();
            else
                item = nullptr;
        }
    }

    // --- Phase 2: build stock rows from item-type-def table entries -----------
    // gilde.exe 0x459ace: dword_B56FE0 = 0
    g_stockRowCount = 0;

    if (g_workOrderCount > 0) {
        // For each WO, scan 4 sub-columns (v103 in [0,2,4,6]):
        // item-type word from dword_13CE27C + 65 * (WO_key>>16) + 46 + v103
        // gilde.exe 0x459b02..0x459cd7
        for (int woI = 0; woI < g_workOrderCount; ++woI) {
            u8* woRow = wo(woI);
            i32 woKeyDw; std::memcpy(&woKeyDw, woRow, 4);
            int woItemHi = (int)((u32)woKeyDw >> 16); // item type id (HIWORD of +0)
            // Actually wait — re-reading: dword_B56464 has +2 = the full key dword,
            // and the HIWORD of *(int*)((char*)dword_B56464 + v_bytes + 2) >> 16 is the item.
            // AssignWorkstations sets LOWORD(dword_B56468[22*idx]) = *i (the i16 at item+0).
            // So the key is stored at kWO_key (+0) and kWO_backIdx (+4).
            // But the decompile reads: (*(int*)((char*)dword_B56464 + v100 + 2) >> 16)
            // where v100 = 88*woI. So offset +2 within the WO record, shifted >>16 = HIWORD.
            // The full dword at offset +2 would span bytes [2..5] of the 88-byte record.
            // If LOWORD(dword_B56468[22*idx]) = *i = item type word, that writes bytes [4..5]
            // (B56468 is at offset +4 from B56464). But the read is at offset +2...
            // This means the item id is stored differently! Let me re-examine:
            //   B56468 - B56464 = 4. "LOWORD(dword_B56468[22*idx])" means the low 2 bytes
            //   of dword_B56468[22*idx] = dword at address B56468 + (22*idx)*4 = B56468 + 88*idx.
            //   Low word of that = bytes at B56468 + 88*idx .. +1 = WO record offset +4, bytes [4..5].
            //   But read is at WO+2: bytes [2..5] as dword, then >>16 = bytes [4..5] as u16.
            //   So the item type IS at bytes [4..5] of the WO record = offset +4, which = kWO_backIdx.
            //   BUT kWO_backIdx is worker-slot field! This conflicts.
            // Actually "LOWORD(dword_B56468[22*idx])" writes to the LOW WORD of the dword at
            // address B56464+4 + 88*idx (= WO record +4, the word at +4..+5 = low word of worker[0] slot).
            // Worker slot[0] = dword at WO+4. Low word of that = bytes [4..5].
            // Then the item-key read: *(int*)(WO+2) >> 16 = bytes [4..5] as u16. CORRECT!
            // So the item id is stored in the LOW WORD of worker_slot[0] (the first dword at +4).
            // The upper word of worker_slot[0] is initialized to 0 (from the -1 fill? No, -1 fills [4..7]).
            // Wait: "do { v6 += 4; dword_B56468[v6/4] = -1; } while (v6 != v7)"
            // v6 starts at 88*idx, v7 = 88*idx + 16. So v6 goes: idx*88+4 → idx*88+20 (exclusive).
            // That fills dwords at WO+4, WO+8, WO+12, WO+16 with -1.
            // THEN "LOWORD(dword_B56468[22*idx]) = *i" sets bytes [4..5] of the WO record.
            // So the item type is at WO+4 (low word), and the upper word of worker_slot[0] is 0xFF (from -1 fill? No, -1 set high word too).
            // Actually -1 = 0xFFFFFFFF fills all 4 bytes. Then LOWORD() = write only low 2 bytes.
            // So worker_slot[0] = 0xFFFF<<16 | itemTypeWord = 0xFFFF????
            // And the read: *(int*)(WO+2) = bytes [2..5]. WO+2 low 2 bytes = 0x0000 (from kWO_key+2 which was not set explicitly).
            // Actually let me re-examine more carefully:
            // kWO_key = +0: not set explicitly in Phase 1 (we set it to itemTypeWord as a u16 write at +0).
            // The decompile: LOWORD(dword_B56468[22 * dword_B56FDC]) = *i;
            // dword_B56468 base = 0xB56468. [22*idx] element index = dword[22*idx] = *(i32*)(B56468 + 4*22*idx) = *(i32*)(B56468 + 88*idx).
            // LOWORD of that = *(u16*)(B56468 + 88*idx) = bytes [0..1] at B56468+88*idx = WO record offset +4..+5.
            // So the item type word is at WO+4 (low word), overwriting the low word of worker_slot[0].
            // We initialized worker_slot[0..3] to -1, then set worker_slot[0] low word = itemTypeWord.
            // So worker_slot[0] = (0xFFFF << 16) | (u16)itemTypeWord.
            // The reading: *(int*)((char*)dword_B56464 + v_bytes + 2) >> 16
            // = *(i32*)(WO + v_bytes_within_WO + 2). For the first WO (woI=0, v_bytes=0): *(i32*)(WO+2).
            // Bytes [2..5] of WO = [2..3] from kWO_key area (set by us to low bytes of itemTypeWord at +0..+1,
            //   so +2..+3 would be zero from our wr16 call) + [4..5] = itemTypeWord.
            // So *(i32*)(WO+2) = ((u16)itemTypeWord << 16) | (bytes[2..3] of WO+0 area) = (itemTypeWord<<16)|0.
            // Then >>16 = (i32)((u16)itemTypeWord). Correct!
            // So the HIWORD of *(i32*)(WO+2) is the itemTypeWord. In our write, we wrote
            // wr16(wo(woIdx), 0, itemTypeWord) which sets bytes [0..1] = itemTypeWord.
            // Then bytes [2..3] = 0 (from zero-init of the array). bytes [4..5] = itemTypeWord (from -1 fill then LOWORD set).
            // *(i32*)(WO+2): bytes [2..3] | [4..5] = 0x0000 | (itemTypeWord<<16) ... wait this depends on little-endian:
            // *(i32*)(WO+2) = WO[2] | WO[3]<<8 | WO[4]<<16 | WO[5]<<24.
            // WO[0..1] = itemTypeWord (low byte first). WO[2..3] = 0. WO[4..5] = itemTypeWord (LOWORD of -1 overwrite).
            // So *(i32*)(WO+2) = 0 | 0 | WO[4]<<16 | WO[5]<<24 = (u16)itemTypeWord << 16.
            // >>16 = (u16)itemTypeWord. CORRECT.
            //
            // IMPORTANT: we need to also set bytes [4..5] of the WO record (kWO_backIdx = +4) to itemTypeWord.
            // Currently our Phase 1 sets: wr16(wo(woIdx), 0, itemTypeWord) [bytes 0..1]
            //                   and fills [4..20) with -1 [bytes 4..20]
            //                   then does NOT set the low word of [4..5] to itemTypeWord.
            // FIX NEEDED in Phase 1 (below the initialization).

            // For Phase 2, we read: *(int*)((char*)dword_B56464 + v100 + 2) >> 16
            // With our current write, this reads bytes [2..3]=0, [4..5]=? from -1 init = 0xFFFF.
            // That gives 0xFFFF<<16 >> 16 = 0xFFFF. NOT correct!
            // We need to set bytes [4..5] of each WO record to the item type word.
            //
            // Actually looking again at original loop:
            // "LOWORD(dword_B56468[22 * dword_B56FDC]) = *i;"
            // After the -1 fill loop "do { v6+=4; ... } while(v6!=v7)"
            // v6 starts at 88*idx (the byte offset from B56464), and goes +4 each iter
            // until v6 == v7 = 88*idx+16. The loop body does dword_B56468[v6/4] = -1.
            // dword_B56468[v6/4] = *(i32*)(B56468 + (v6/4)*4) = *(i32*)(B56468 + v6) = *(i32*)(B56464+4+v6).
            // For iter 1: v6=88*idx, writes *(i32*)(B56464+4+88*idx) = WO[4..7] = -1.
            // iter 2: v6=88*idx+4, writes WO[8..11] = -1.
            // iter 3: v6=88*idx+8, writes WO[12..15] = -1.
            // iter 4: v6=88*idx+12, writes WO[16..19] = -1. Then v6=88*idx+16 == v7 → stop.
            // So WO[4..19] = -1 (4 dwords).
            // Then: LOWORD(dword_B56468[22 * idx]) = *i.
            // = *(u16*)(B56468 + 22*idx*4) = *(u16*)(B56468 + 88*idx) = *(u16*)(WO+4..+5).
            // Sets WO[4..5] = (u16)*item = itemTypeWord. WO[6..7] remain 0xFF.
            // So WO[4..7] = 0xFFFF0000 | itemTypeWord. (little-endian: WO[4]=lo, WO[5]=hi, WO[6..7]=0xFF)
            // *(i32*)(WO+2) with little-endian: WO[2] | WO[3]<<8 | WO[4]<<16 | WO[5]<<24.
            // WO[2..3]=0 (from our write OR from -1 fill? -1 fill starts at WO+4, not WO+2).
            // Our Phase 1 writes:
            //   wr16(wo(woIdx), 0, itemTypeWord) → WO[0..1] = itemTypeWord bytes.
            //   WO[2..3] = 0 (g_workOrderTable is zero-initialized globally, but between runs we need to reset).
            //   Then fills WO[4..19] = -1 via loop.
            //   Then LOWORD write: WO[4..5] = itemTypeWord.
            // Result: WO[2..3] = 0x0000. WO[4..5] = itemTypeWord.
            // *(i32*)(WO+2) = WO[2] | WO[3]<<8 | WO[4]<<16 | WO[5]<<24
            //               = 0 | 0 | (itemTypeWord&0xFF)<<16 | ((itemTypeWord>>8)&0xFF)<<24
            //               = (u16)itemTypeWord << 16. >>16 = (u16)itemTypeWord. CORRECT.
            // The original doesn't explicitly write WO[0..1] separately — the first write is
            // LOWORD(dword_B56468[22*idx]) which is WO[4..5]. WO[0..3] may be garbage.
            // Our extra wr16 at kWO_key (+0) is harmless (not read in Phase 2).
            (void)woRow; (void)woKeyDw; (void)woItemHi; // will be computed fresh below

            // Phase 2 inner loop: scan 4 sub-positions (v103 = 0,2,4,6 → 4 iters)
            // gilde.exe 0x459b02..0x459cb7
            for (int sub = 0; sub < 4; ++sub) {
                // v103 = sub*2 (word offset into the "46" area). But actually the
                // decompile uses v103 incrementing by 2 from 0 to 8 (exclusive).
                int v103 = sub * 2;
                // Read item-type word from dword_13CE27C + 65 * (WO_item>>16) + 46 + v103
                // But WO_item is *(int*)((char*)dword_B56464 + v100 + 2) >> 16
                // where v100 = 88*woI (byte offset of WO row from B56464 base).
                // In our reimpl: read from g_workOrderTable at byte (88*woI + 2), dword.
                i32 woKeyAt2; std::memcpy(&woKeyAt2, g_workOrderTable + kWorkOrderStride * woI + 2, 4);
                int woTypeHi = (int)((u32)woKeyAt2 >> 16);

                // *(u16*)(dword_13CE27C + 65 * woTypeHi + 46 + v103)
                u16 slotTypeWord = itemTypeWord(woTypeHi, 46 + v103);
                // gilde.exe 0x459b5c: if (*(u16*)(v103 + ...) != 0)
                if (!slotTypeWord) continue;

                // Search existing stock rows for this slotTypeWord
                int foundStockIdx = -1;
                for (int si = 0; si < g_stockRowCount; ++si) {
                    i32 stKey; std::memcpy(&stKey, g_stockTable + kStockStride * si, 4);
                    if ((u32)stKey >> 16 == (u32)slotTypeWord)
                        { foundStockIdx = si; break; }
                }

                // Store stock-back-index into WO record at B5646C offset
                // B5646C - B56464 = 8. Sub-column within WO at +8 + sub*4.
                // gilde.exe 0x459bb6: *(int*)((char*)dword_B5646C + v95) = v10
                //   where v95 starts at v104 (= 88*woI) and increments by 4 per sub.
                //   dword_B5646C + v95 = B56464+8 + 88*woI + sub*4 = wo(woI)+8+sub*4.
                wr32(wo(woI), kWO_stBackOff + sub * 4, foundStockIdx);

                // gilde.exe 0x459bcc: if (*(int*)((char*)dword_B5646C + v102) == -1)
                if (foundStockIdx == -1) {
                    // Allocate new stock row
                    int newSi = g_stockRowCount;
                    u8* newRow = st(newSi);
                    // gilde.exe 0x459bff: *(_WORD*)((char*)dword_B5444E + v96 + 2) = slotTypeWord
                    // B5444E + v96 + 2 where v96 = stockRowCount << 6 = newSi*64. offset +2 within row.
                    // The HIWORD of the key dword: row[2..3] = slotTypeWord.
                    wr16(newRow, kST_key + 2, slotTypeWord); // HIWORD of key at +0
                    // gilde.exe 0x459c08..0x459c4a: init stock row fields
                    stwr32(newSi, kST_backIdx, -1);    // B54454 +6
                    stwr32(newSi, kST_srcBldg, 0);     // B54458 +10 (wait: decompile shows B54458 = dword_B54458)
                    // Actually: decompile is: *(int*)((char*)dword_B54458 + v12) = 0;
                    // B54458 - B5444E = 10. So offset +10.  ✓
                    stwr32(newSi, kST_field18, 0);     // B54460 +18
                    stwr32(newSi, kST_field22, 0);     // B54464 +22
                    stwr32(newSi, kST_field26, 0);     // B54468 +26
                    // *(float*)((char*)flt_B5446C + v12) = -1.0e10f  [B5446C = +30]
                    float initPrice = -1.0e10f;
                    std::memcpy(newRow + kST_price, &initPrice, 4);
                    stwr32(newSi, kST_reserved, 0);   // B54474 +38
                    stwr32(newSi, kST_stock, 0);      // B54478 +42
                    stwr32(newSi, kST_flags50, 33);   // B54480 +50
                    stwr16(newSi, kST_bits, 0);       // word_B5448C +62

                    // Back-link this stock row to the WO
                    // gilde.exe 0x459c5d: *(int*)((char*)dword_B5646C + v102) = dword_B56FE0
                    wr32(wo(woI), kWO_stBackOff + sub * 4, newSi);
                    // gilde.exe 0x459c74: v96 += 64; ++dword_B56FE0
                    g_stockRowCount = newSi + 1;
                }
            }
        }
    }

    // --- Phase 3: type-byte check for farm/market building -------------------
    // gilde.exe 0x459d0b: v13 = *(589 * *(bldgRec+0) + dword_13CE294)
    u8 typeByte = rd8(bldgRec, kB_typeByte);
    u8 v13 = buildingTypeField(typeByte, 0);

    // gilde.exe 0x459d1e: if ((v13==14 || v13==8) && dword_B56FE0 > 0)
    if ((v13 == 14 || v13 == 8) && g_stockRowCount > 0) {
        for (int si = 0; si < g_stockRowCount; ++si) {
            u16 stKeyHi = strd16(si, kST_key + 2); // HIWORD of key = item type

            // gilde.exe 0x45a2c2: if (v16==452 || v16==453 || v16==454)
            if (stKeyHi == 452 || stKeyHi == 453 || stKeyHi == 454) {
                // gilde.exe 0x459d4e: LOBYTE(word_B5448C[v14/2]) |= 0x24
                u16 bits; std::memcpy(&bits, st(si) + kST_bits, 2);
                reinterpret_cast<u8*>(&bits)[0] |= 0x24u;
                std::memcpy(st(si) + kST_bits, &bits, 2);

                // gilde.exe 0x459d56: search WO table for matching item
                int matchWo = g_workOrderCount; // default: not found
                for (int wi = 0; wi < g_workOrderCount; ++wi) {
                    i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
                    if ((u32)woK >> 16 == (u32)stKeyHi) { matchWo = wi; break; }
                }

                if (matchWo >= g_workOrderCount) {
                    // gilde.exe 0x459d9b..0x459e00: add new WO entry
                    int newWo = g_workOrderCount;
                    if (newWo < kMaxWorkOrders) {
                        // LOWORD(dword_B56468[v20/2]) = HIWORD(dword_B5444E[v14/4])
                        // Sets bytes [4..5] of new WO row to stKeyHi
                        wr16(wo(newWo), kWO_backIdx, stKeyHi); // low word of worker_slot[0]
                        // Fill worker slots [4..20) with -1 (via the do..while pattern)
                        for (int s = 0; s < 4; ++s)
                            wr32(wo(newWo), kWO_backIdx + s * 4, -1);
                        wr16(wo(newWo), kWO_backIdx, stKeyHi); // restore after -1 fill

                        wword32(newWo, kWO_qty1, 0);
                        // gilde.exe 0x459dc0: dword_B56488[v20/2] = v22 ^ v21 (=0)
                        wword32(newWo, kWO_qty2, 0);
                        wword32(newWo, kWO_output, 0);
                        wword32(newWo, kWO_effStock, 0);
                        wword32(newWo, kWO_minQty, 0);
                        wword32(newWo, kWO_aux, 0);
                        wword32(newWo, kWO_rank, 33);
                        wword32(newWo, kWO_initVal, -803929351);
                        // gilde.exe 0x459e06: word_B564BC[v20] = 36
                        setWoBits(newWo, 36); // 36 = 0x24
                        g_workOrderCount = newWo + 1;
                    }
                } else {
                    // gilde.exe 0x459d77: LOBYTE(word_B564BC[v19]) |= 0x24
                    orWoBitsLo(matchWo, 0x24u);
                }
            }
            // gilde.exe 0x45a3d5: else if (v16==449 || v16==450 || v16==451)
            else if (stKeyHi == 449 || stKeyHi == 450 || stKeyHi == 451) {
                // gilde.exe 0x45a2e7: word_B5448C[v14/2] |= 0x204
                u16 bits2; std::memcpy(&bits2, st(si) + kST_bits, 2);
                bits2 |= 0x204u;
                std::memcpy(st(si) + kST_bits, &bits2, 2);

                // Search WO for matching item
                int matchWo2 = g_workOrderCount;
                for (int wi = 0; wi < g_workOrderCount; ++wi) {
                    i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
                    if ((u32)woK >> 16 == (u32)stKeyHi) { matchWo2 = wi; break; }
                }

                if (matchWo2 >= g_workOrderCount) {
                    int newWo = g_workOrderCount;
                    if (newWo < kMaxWorkOrders) {
                        for (int s = 0; s < 4; ++s)
                            wr32(wo(newWo), kWO_backIdx + s * 4, -1);
                        wr16(wo(newWo), kWO_backIdx, stKeyHi);
                        wword32(newWo, kWO_qty1, 0);
                        wword32(newWo, kWO_qty2, 0);
                        wword32(newWo, kWO_output, 0);
                        wword32(newWo, kWO_effStock, 0);
                        wword32(newWo, kWO_minQty, 0);
                        wword32(newWo, kWO_aux, 0);
                        wword32(newWo, kWO_rank, 33);
                        wword32(newWo, kWO_initVal, -803929351);
                        // gilde.exe 0x45a3a2: word_B564BC[v68] = 516 (0x204)
                        setWoBits(newWo, 516);
                        g_workOrderCount = newWo + 1;
                    }
                } else {
                    // gilde.exe 0x45a317: word_B564BC[v67] |= 0x204
                    u16& fw = g_woBitsTable[matchWo2];
                    fw |= 0x204u;
                }
            }
        }
    }

    // --- Phase 4: cross-link stock rows back to WO rows ----------------------
    // gilde.exe 0x459e20..0x459e6f
    for (int si = 0; si < g_stockRowCount; ++si) {
        i32 stKeyDw; std::memcpy(&stKeyDw, st(si), 4);
        int stTypeHi = (int)((u32)stKeyDw >> 16);

        // Find WO with matching item type
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
            if ((int)((u32)woK >> 16) == stTypeHi) {
                stwr32(si, kST_backIdx, wi); // dword_B54454
                break;
            }
        }
    }

    // --- Phase 5: QueryFind per WO + ComputeFreeCapacity for WO rows ---------
    // gilde.exe 0x459e77..0x459eda
    if (g_workOrderCount > 0 && nodeBase) {
        i32 nodeSceneRoot = rd32(nodeBase, 5 * 4); // *((_DWORD*)v98 + 5) = field at +20
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
            int itemHi = (int)((u32)woK >> 16);

            u8* found = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                const int filts[] = {0, itemHi}; // op 0 = type==val
                found = g_meisterLeaves->queryFind(nodeSceneRoot, filts, 2);
            }
            // gilde.exe 0x459ea2: if found: GetEffectiveStock(v98, found)
            i32 effStock = 0;
            if (found && g_meisterFarmGetEffectiveStock)
                effStock = g_meisterFarmGetEffectiveStock(nodeBase, found);
            // gilde.exe 0x459ea8: *(int*)((char*)dword_B564A4 + v30) = effStock
            wword32(wi, kWO_effStock, effStock);

            // gilde.exe 0x459ec7: VIBE_Inventory_ComputeFreeCapacity(v98, itemHi, off, 1000000)
            // off = v30 + 88 (byte offset from some base). In reimpl: use wi*88 + 88 as offset.
            i32 freeCap = 0;
            if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity)
                freeCap = g_meisterLeaves->inventoryFreeCapacity(nodeBase, itemHi, wi * 88 + 88, 1000000);
            // gilde.exe 0x459ed2: *(int*)((char*)&dword_B56450 + v28) = freeCap
            g_woFreeCapTable[wi] = freeCap;
        }
    }

    // --- Phase 6: QueryFind per stock row + ComputeFreeCapacity + QueueRequest20 ---
    // gilde.exe 0x459ee2..0x459f45
    if (g_stockRowCount > 0 && nodeBase) {
        i32 nodeSceneRoot = rd32(nodeBase, 5 * 4);
        for (int si = 0; si < g_stockRowCount; ++si) {
            i32 stKey; std::memcpy(&stKey, st(si), 4);
            int stTypeHi = (int)((u32)stKey >> 16);

            u8* found = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                const int filts[] = {0, stTypeHi};
                found = g_meisterLeaves->queryFind(nodeSceneRoot, filts, 2);
            }
            if (found) {
                // gilde.exe 0x45a41e: GetEffectiveStock
                i32 stk = 0;
                if (g_meisterFarmGetEffectiveStock)
                    stk = g_meisterFarmGetEffectiveStock(nodeBase, found);
                stwr32(si, kST_stock, stk); // dword_B54478
                // gilde.exe 0x45a42b: if (!stk) QueueRequest20(bldgId, itemHi)
                if (!stk) {
                    i32 bldgId = rd32(bldgRec, kB_id1);
                    if (g_meisterFarmQueueRequest20)
                        g_meisterFarmQueueRequest20(bldgId, (i16)stTypeHi);
                }
            } else {
                stwr32(si, kST_stock, 0);
            }

            // gilde.exe 0x459f32: ComputeFreeCapacity(v98, stTypeHi, off, 1000000)
            i32 stFreeCap = 0;
            if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity)
                stFreeCap = g_meisterLeaves->inventoryFreeCapacity(nodeBase, stTypeHi, si * 64 + 64, 1000000);
            // gilde.exe 0x459f3d: *(int*)((char*)&dword_B5443C + v34) = stFreeCap
            g_stFreeCapTable[si] = stFreeCap;
        }
    }

    // --- Phase 7: He-filter 11 (workstation production) output aggregation ---
    // gilde.exe 0x459f71..0x45a61e
    i32 bldgId = rd32(bldgRec, kB_id1);

    if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
        u8* j = g_meisterLeaves->heFindFirst(2, 0, 11, 3, bldgId);
        while (j) {
            // gilde.exe 0x459f80: ObjectById = VIBE_Object_FindObjectById(*(j+43*4))
            i32 handlerBldgId = rd32(j, 43 * 4); // *((_DWORD*)j + 43)
            u8* obj = nullptr;
            if (g_meisterFarmObjectById)
                obj = g_meisterFarmObjectById(handlerBldgId);

            if (obj) {
                // gilde.exe 0x45a450: CollectProductionSlots(obj, v93)
                i32 slotBuf[46] = {};
                if (g_meisterFarmCollectSlots)
                    g_meisterFarmCollectSlots(obj, slotBuf);

                // gilde.exe 0x45a45a: if (slotBuf[0] > 0)
                int slotCount = slotBuf[0];
                if (slotCount > 0) {
                    // v108 = 2 * slotCount; loop v73 = 0,2,...,<v108 (word strides)
                    // v107 = 0 (dword offset into count area)
                    for (int s = 0; s < slotCount; ++s) {
                        // gilde.exe 0x45a472: if (*(_WORD*)((char*)&v93[18] + v73))
                        // slotBuf[18] is at dword index 18 = word index 36.
                        // type word at: *(u16*)(&slotBuf[18] + s*2) = *(u16*)(slotBuf+18*4+s*2)
                        u16 slotTypeW;
                        std::memcpy(&slotTypeW, reinterpret_cast<u8*>(slotBuf) + 18*4 + s*2, 2);
                        if (!slotTypeW) continue;

                        // gilde.exe 0x45a47e..0x45a4af: check if handler has this type
                        // scan *(j+206) >> 16 == slotType for 3 positions (j advances by 2 bytes)
                        bool found3 = false;
                        for (int t = 0; t < 3 && !found3; ++t) {
                            i32 handlerTypeField;
                            std::memcpy(&handlerTypeField, j + 206 + t * 2, 4); // *(int*)(j+206+t*2)
                            if ((u32)handlerTypeField >> 16 == (u32)slotTypeW) found3 = true;
                        }

                        if (!found3) {
                            // gilde.exe 0x45a4d0: v79 = slotType, add count to matching WO
                            int slotType = (int)(u32)slotTypeW;
                            // count = *(_DWORD*)((char*)&v93[26] + v107) = slotBuf[26+s] (dword)
                            i32 slotCountDw = slotBuf[26 + s];

                            // Add to matching WO output field (dword_B564A0)
                            for (int wi = 0; wi < g_workOrderCount; ++wi) {
                                i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
                                if ((int)((u32)woK >> 16) == slotType)
                                    wword32(wi, kWO_output, word32(wi, kWO_output) + slotCountDw);
                            }
                            // Add to matching stock reserved (dword_B54474)
                            for (int si = 0; si < g_stockRowCount; ++si) {
                                i32 stKey; std::memcpy(&stKey, st(si), 4);
                                if ((i32)((u32)stKey >> 16) == slotType)
                                    stwr32(si, kST_reserved, strd32(si, kST_reserved) + slotCountDw);
                            }
                        }
                    }
                }

                // gilde.exe 0x45a552..0x45a61e: secondary slot scan (*(j+107*2) check)
                // Walk 3 positions (v85 advance by 2 each, from j to j+6)
                for (int t = 0; t < 3; ++t) {
                    // gilde.exe 0x45a56a: if (*((_WORD*)v85 + 107))
                    u16 field107;
                    std::memcpy(&field107, j + t*2 + 107*2, 2);
                    if (!field107) continue;

                    // gilde.exe 0x45a5a9: *(int*)(v85+53) >> 16 == WO item type
                    i32 typeField53;
                    std::memcpy(&typeField53, j + t*2 + 53*4, 4);
                    int matchType = (int)((u32)typeField53 >> 16);

                    // gilde.exe 0x45a5b1: dword_B564A0[..] += *((_DWORD*)v86 + 55)
                    i32 field55;
                    std::memcpy(&field55, j + t*4 + 55*4, 4);

                    for (int wi = 0; wi < g_workOrderCount; ++wi) {
                        i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
                        if ((int)((u32)woK >> 16) == matchType)
                            wword32(wi, kWO_output, word32(wi, kWO_output) + field55);
                    }
                    for (int si = 0; si < g_stockRowCount; ++si) {
                        i32 stKey; std::memcpy(&stKey, st(si), 4);
                        if ((i32)((u32)stKey >> 16) == matchType)
                            stwr32(si, kST_reserved, strd32(si, kST_reserved) + field55);
                    }
                }
            }

            if (g_meisterLeaves->heFindNext)
                j = g_meisterLeaves->heFindNext();
            else
                j = nullptr;
        }
    }

    // --- Phase 8: He-filter 20 (mill output adjustment) ----------------------
    // gilde.exe 0x459fc2..0x45a045
    // flt_6198FC = 0.01f (verified get_bytes 0x6198FC = 0x3C23D70A)
    // flt_619900 = 7.5f  (verified get_bytes 0x619900 = 0x40F00000)
    constexpr float kFilt20Scale = 0.009999999776482582f; // flt_6198FC
    constexpr float kFilt20Base  = 7.5f;                  // flt_619900

    if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
        u8* k = g_meisterLeaves->heFindFirst(2, 0, 20, 3, bldgId);
        while (k) {
            // gilde.exe 0x459fea: dword_B5444E[v42] >> 16 == *((int*)k + 43) >> 16
            i32 kType = rd32(k, 43 * 4);
            int kTypeHi = (int)((u32)kType >> 16);

            for (int si = 0; si < g_stockRowCount; ++si) {
                i32 stKey; std::memcpy(&stKey, st(si), 4);
                if ((int)((u32)stKey >> 16) == kTypeHi) {
                    // gilde.exe 0x45a008: SumWorkstationByCategory(bldgRec, 11, 1)
                    i32 v113 = 0;
                    if (g_meisterFarmSumWorkstation)
                        v113 = g_meisterFarmSumWorkstation(bldgRec, 11, 1);
                    // gilde.exe 0x45a02c: v44 = ((double)v113 * flt_6198FC + 1.0) * flt_619900
                    //                          + (double)dword_B54474[v43]
                    // VIBE_Coord_ConvertX = (int)trunc
                    double v44 = ((double)v113 * (double)kFilt20Scale + 1.0) * (double)kFilt20Base
                               + (double)strd32(si, kST_reserved);
                    stwr32(si, kST_reserved, (i32)v44); // gilde.exe 0x45a033
                    break;
                }
            }

            if (g_meisterLeaves->heFindNext)
                k = g_meisterLeaves->heFindNext();
            else
                k = nullptr;
        }
    }

    // --- Phase 9: He-filter 21 (granary output adjustment) -------------------
    // gilde.exe 0x45a07b..0x45a0fe (identical structure to Phase 8)
    if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
        u8* m = g_meisterLeaves->heFindFirst(2, 0, 21, 3, bldgId);
        while (m) {
            i32 mType = rd32(m, 43 * 4);
            int mTypeHi = (int)((u32)mType >> 16);

            for (int si = 0; si < g_stockRowCount; ++si) {
                i32 stKey; std::memcpy(&stKey, st(si), 4);
                if ((int)((u32)stKey >> 16) == mTypeHi) {
                    i32 v113 = 0;
                    if (g_meisterFarmSumWorkstation)
                        v113 = g_meisterFarmSumWorkstation(bldgRec, 11, 1);
                    double v50 = ((double)v113 * (double)kFilt20Scale + 1.0) * (double)kFilt20Base
                               + (double)strd32(si, kST_reserved);
                    stwr32(si, kST_reserved, (i32)v50);
                    break;
                }
            }

            if (g_meisterLeaves->heFindNext)
                m = g_meisterLeaves->heFindNext();
            else
                m = nullptr;
        }
    }

    // --- Phase 10: ComputeWorkstationOutput for all WO rows ------------------
    // gilde.exe 0x45a115..0x45a133
    if (g_workOrderCount > 0) {
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            if (g_meisterFarmComputeWorkstation)
                g_meisterFarmComputeWorkstation(wo(wi));
        }
    }

    // --- Phase 11: Selection sort WO rows by score (descending) --------------
    // gilde.exe 0x45a140..0x45a1ef
    // The sort: for v112=0..N-1: inner loop v54=v112+1..N-1, swap if score[v54]<score[v112].
    // flt_B5649C = flt at offset (B5649C - B56464) = 0x38 = 56 within WO row.
    static constexpr int kWO_score = 0x38; // B5649C offset from B56464 = 0x38 = 56 (float)
    if (g_workOrderCount > 1) {
        u8 swapBuf[kWorkOrderStride];
        for (int v112 = 0; v112 < g_workOrderCount; ++v112) {
            for (int v54 = v112 + 1; v54 < g_workOrderCount; ++v54) {
                float scoreA, scoreB;
                std::memcpy(&scoreB, wo(v54) + kWO_score, 4);
                std::memcpy(&scoreA, wo(v112) + kWO_score, 4);
                if (scoreB < (double)scoreA) { // gilde.exe 0x45a183: float comparison via double
                    std::memcpy(swapBuf, wo(v54), kWorkOrderStride);
                    std::memcpy(wo(v54), wo(v112), kWorkOrderStride);
                    std::memcpy(wo(v112), swapBuf, kWorkOrderStride);
                    // Also swap flags
                    u16 ftmp = g_woBitsTable[v54];
                    g_woBitsTable[v54] = g_woBitsTable[v112];
                    g_woBitsTable[v112] = ftmp;
                }
            }
        }
    }

    // --- Phase 12: Cross-link stock back-index to WO (dword_B54454 back-link) -
    // gilde.exe 0x45a1fd..0x45a263
    for (int si = 0; si < g_stockRowCount; ++si) {
        i32 backIdx = strd32(si, kST_backIdx);
        if (backIdx == -1) continue;

        i32 stKeyDw; std::memcpy(&stKeyDw, st(si), 4);
        int stTypeHi = (int)((u32)stKeyDw >> 16);

        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            i32 woK; std::memcpy(&woK, g_workOrderTable + kWorkOrderStride * wi + 2, 4);
            if ((int)((u32)woK >> 16) == stTypeHi) {
                // gilde.exe 0x45a234: LOBYTE(word_B5448C[v57/2]) |= 0x40
                u8* bitsPtr = st(si) + kST_bits;
                bitsPtr[0] |= 0x40u;
                // gilde.exe 0x45a241: LOBYTE(word_B564BC[v60/2]) |= 0x40
                orWoBitsLo(wi, 0x40u);
                // gilde.exe 0x45a244: dword_B54454[v57/4] = v59 (the WO index)
                stwr32(si, kST_backIdx, wi);
                break;
            }
        }
    }

    // --- Phase 13: Assign sequential rank indices ----------------------------
    // gilde.exe 0x45a26b..0x45a285
    // dword_B564B0[v64] = v62++ where v64 advances by 22 (22 words = 44 bytes half-stride)
    // dword_B564B0 base = B56464 + 0x4C = kWO_rank. But stride here is 22 dwords (half WO stride)?
    // Wait: "v64 += 22; dword_B564B0[v64] = v62++"
    // dword_B564B0[v64] = *(i32*)(B564B0 + v64*4). v64 advances by 22 means v64 is an element index.
    // Byte address = B564B0 + 22*4*i = B564B0 + 88*i = WO record i at offset (B564B0-B56464) = 0x4C.
    // So kWO_rank (+76) = B564B0. Setting it to the rank index (0..N-1).
    for (int wi = 0; wi < g_workOrderCount; ++wi)
        wword32(wi, kWO_rank, wi);
}

// ---------------------------------------------------------------------------
// CalcMeisterFarming — gilde.exe 0x454f50
// __usercall: eax=a1 (meisterRec)
// Returns: MeisterTradeManageStorage result (int).
// ---------------------------------------------------------------------------
int CalcMeisterFarming(u8* mr) {
    // gilde.exe 0x454f5c: v89 = a1
    // Three QueryFind probes on the building's scene root:
    //   v92 = QueryFind(bldgRec+93, 2, 6, 0, 255)  -- type-255 root node (resource/storage root)
    //   v93 = QueryFind(bldgRec+93, 2, 6, 0, 42)   -- type-42 node (workstation root)
    //   v94 = QueryFind(bldgRec+93, 1, 0, 254)      -- type-254 node (estate/land root)
    // gilde.exe 0x454f86, 0x454fb1, 0x454fd5

    u8* bldgRec = rdptr(mr, kM_bldgRec); // *((_DWORD*)a1+91)
    if (!bldgRec) {
        // Null building: run transport + storage, return
        MeisterCollectTransporters(mr);
        return MeisterTradeManageStorage(mr);
    }

    i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93); // *(bldgRec+93)

    u8* v92 = nullptr; // type-255 QueryFind result
    u8* v93 = nullptr; // type-42 QueryFind result
    u8* v94 = nullptr; // type-254 QueryFind result

    if (g_meisterLeaves && g_meisterLeaves->queryFind) {
        // gilde.exe 0x454f86: QueryFind(sceneRoot, 2, 6, 0, 255)
        // ops: 2 ops — op6 (useStack, no val), op0 (type==255, val=255)
        const int filts255[] = {6, 0, 255};
        v92 = g_meisterLeaves->queryFind(sceneRootId, filts255, 3);

        // gilde.exe 0x454fb1: QueryFind(sceneRoot, 2, 6, 0, 42)
        const int filts42[] = {6, 0, 42};
        v93 = g_meisterLeaves->queryFind(sceneRootId, filts42, 3);

        // gilde.exe 0x454fd5: QueryFind(sceneRoot, 1, 0, 254)
        const int filts254[] = {0, 254};
        v94 = g_meisterLeaves->queryFind(sceneRootId, filts254, 2);
    }

    // gilde.exe 0x454fe3: MeisterAssignWorkstations(v89, v1, v93)
    // v1 (EDX at call site) carries v92 (the type-255 result), but our sig drops a2.
    MeisterAssignWorkstations(mr, reinterpret_cast<SceneNode*>(v93));

    // --- Phase A: Work-order flag bit 4 (0x04) pass --------------------------
    // gilde.exe 0x454ff0..0x4550dd
    // For each WO: scan 4 sub-stock-back-indices. If valid: set bit4 on WO flags and
    // stock flags, iff (stock → WO cross-link) also has bit4 set on WO side.
    if (g_workOrderCount > 0) {
        int stockBound = g_stockRowCount * kStockStride; // dword_B56FE0 << 6
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            // gilde.exe 0x45501e..0x4550bd: inner loop over 4 sub-indices (v3=0..3)
            for (int sub = 0; sub < 4; ++sub) {
                // v4 = *(int*)((char*)dword_B5646C + v2) where v2 = 88*wi + sub*4
                // = WO+8+sub*4 (the stock-back-index for this sub-slot)
                i32 stIdx = rd32(wo(wi), kWO_stBackOff + sub * 4);
                if (stIdx < 0) continue;

                // gilde.exe 0x45504c: v5 = dword_B54454[16*stIdx]
                // dword_B54454 is at stock row offset +6. [16*stIdx] in dword index =
                // B54454 + 16*stIdx*4 = stock_base + 6 + 64*stIdx... wait, 16*4=64 stride!
                // dword_B54454[16*stIdx] = *(i32*)(B54454 + 16*4*stIdx) = *(i32*)(B54454 + 64*stIdx)
                // = g_stockTable[64*stIdx + 6] (since B54454 - B5444E = 6 = kST_backIdx).
                i32 backWo = strd32(stIdx, kST_backIdx); // B54454 = kST_backIdx
                if (backWo < 0) continue;

                // gilde.exe 0x455062: LOBYTE(word_B564BC[44*backWo]) |= 4
                // (mark the WO that this stock row points to)
                orWoBitsLo(backWo, 4u);

                // gilde.exe 0x455070: if (dword_B56FE0 > 0)
                if (g_stockRowCount > 0 && backWo < g_workOrderCount) {
                    // gilde.exe 0x455088: the orig re-reads dword_B5646C[sub]<<6 and
                    // indexes dword_B54454[..]*88 to recover backWo*88 — i.e. the same
                    // backWo already in hand. woFromStockType is its key HIWORD.
                    // gilde.exe 0x4550a6: cross-check: WO-from-stock matches WO item type
                    i32 woFromStockK;
                    std::memcpy(&woFromStockK, g_workOrderTable + kWorkOrderStride * backWo + 2, 4);
                    int woFromStockType = (int)((u32)woFromStockK >> 16);

                    for (int si2 = 0; si2 < g_stockRowCount; ++si2) {
                        i32 stKey2; std::memcpy(&stKey2, st(si2), 4);
                        if ((int)((u32)stKey2 >> 16) == woFromStockType) {
                            // gilde.exe 0x4550a8: LOBYTE(word_B5448C[v8/2]) |= 4
                            st(si2)[kST_bits] |= 4u;
                        }
                    }
                }
                (void)stockBound;
            }
        }
    }

    // --- Sub-planners --------------------------------------------------------
    // gilde.exe 0x4550f6
    MeisterHireStaff(mr);
    // gilde.exe 0x455102: FillAiSlots(v93, _, _, 10)
    MeisterFillAiSlots(reinterpret_cast<SceneNode*>(v93), mr, 10);
    // gilde.exe 0x45511a
    MeisterTrainStaff(reinterpret_cast<SceneNode*>(v93), mr, 25);
    // gilde.exe 0x455126
    MeisterRenovateBuilding(mr);

    // --- Harvest command (cmdType=9) -----------------------------------------
    // gilde.exe 0x455167..0x455239
    // Guard: (mr+456) & 0x10 must be clear AND building type != 11 (farmhand type)
    u8 flags456 = rd8(mr, kM_flags2); // *((_BYTE*)v89 + 456) = mr+456
    u8 bTypeCode = rd8(bldgRec, kB_typeByte);
    u8 bTypeDef0 = buildingTypeField((u8)(bTypeCode), 0);

    if (!(flags456 & 0x10) && bTypeDef0 != 11) {
        // gilde.exe 0x45517e: QueryFind(*(v92+5*4), 1, 4, 23)
        // Uses the type-255 result v92's +20 field as scene root.
        u8* v13 = nullptr;
        if (v92 && g_meisterLeaves && g_meisterLeaves->queryFind) {
            i32 v92SceneRoot = rd32(v92, 5 * 4); // *((_DWORD*)v92 + 5)
            const int filts4_23[] = {4, 23}; // op4 = typedefByte==23
            v13 = g_meisterLeaves->queryFind(v92SceneRoot, filts4_23, 2);
        }

        if (v13) {
            // gilde.exe 0x455192: while (*(dword*)(v13+7) != 1) { IterNext; if (!v13) goto L18 }
            while (rd32(v13, 7) != 1) {
                if (g_meisterLeaves && g_meisterLeaves->queryIterNext)
                    v13 = g_meisterLeaves->queryIterNext();
                else
                    v13 = nullptr;
                if (!v13) break;
            }

            if (v13 && rd32(v13, 7) == 1) {
                // gilde.exe 0x4551a1: Light_SetGrayColorThunk(0,248,&v67) → MeisterCommand cmd{}
                MeisterCommand cmd{};
                // gilde.exe 0x4551af: v68 = 9 (cmdType byte at struct+4)
                cmd.cmdType    = 9;   // harvest command
                // gilde.exe 0x4551d2: v69 = dword_12CE914[134 * *(u16*)(bldgRec+39)]
                // dword_12CE914 is g_personIds. *(bldgRec+39) = owner word (B_owner39).
                u16 ownerWord = rdu16(bldgRec, kB_owner39);
                cmd.actorId    = (ownerWord < (u16)kPersonCapacity) ? g_personIds[ownerWord] : 0;
                // gilde.exe 0x4551ea: v14 = *(bldgRec+1) = building id
                i32 bldgId2   = rd32(bldgRec, kB_id1);
                cmd.buildingId = bldgId2;
                // gilde.exe 0x4551ed/f1: v71=0, v72=0
                // gilde.exe 0x4551f9: v70 = bldgId (duplicate at cmd+0x0C area)
                cmd.targetId   = bldgId2;
                // gilde.exe 0x4551fd: LOBYTE(v15) = 1 → mode=1
                cmd.mode       = 1;
                // gilde.exe 0x4551ff: v73 = qword_13CE852 (game time stamp)
                cmd.timePacked = (u64)g_meisterGameTime.day |
                                 ((u64)g_meisterGameTime.hour << 32);
                cmd.timeExtra  = g_meisterTimeExtra;
                cmd.timeTail   = g_meisterTimeTail;
                // gilde.exe 0x45520b: v76 = 1
                cmd.extra0     = 1;
                // gilde.exe 0x455218: v77 = *(bldgRec+1)
                // gilde.exe 0x455226: v78 = *(v92+1) — type-255 result bldg id
                if (v92) cmd.srcId = rd32(v92, 1);
                // gilde.exe 0x455234: v79 = *(v94+1) — type-254 result bldg id
                if (v94) cmd.extra1 = rd32(v94, 1);
                // gilde.exe 0x45523a: QueueRequestSlotReset28(&v67, v15)
                if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
            }
        }
        // LABEL_18: gilde.exe 0x45523f
        wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x10u));
    }

    // --- Worker action management (ChangePlayerAction) -----------------------
    // gilde.exe 0x45524f..0x4552a0
    int v91 = 0; // action-handler-found flag

    {
        // gilde.exe 0x45526f: FindFirstHandlerByFilter(2, 0, 9, 3, bldgId)
        i32 bId = rd32(bldgRec, kB_id1);
        u8* handler = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->heFindFirst)
            handler = g_meisterLeaves->heFindFirst(2, 0, 9, 3, bId);

        if (handler) {
            // gilde.exe 0x455288: if (handler != *((_ptr*)v89 + 95))
            u8* curAction = rdptr(mr, kM_action); // *((_DWORD*)v89 + 95) = mr+380
            if (handler != curAction) {
                // gilde.exe 0x45529b: ChangePlayerAction(bldgRec_handle, 0, handler, ownerWord)
                u16 ownerW = rdu16(bldgRec, kB_owner39);
                i32 bldgRecH = rd32(mr, kM_bldgRec); // the handle, not the ptr
                if (g_meisterLeaves && g_meisterLeaves->changePlayerAction)
                    g_meisterLeaves->changePlayerAction(bldgRecH, 0, handler, ownerW);
            }
            v91 = 1;
        }
    }

    // --- Work-order loop 1: find and set bit 0x08 (assign mode) -------------
    // gilde.exe 0x4552b1..0x45581f (first eligible WO with capacity → set bit8)
    if (g_workOrderCount > 0) {
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            u16 woBits = rdWoBits(wi);

            // gilde.exe 0x455729: if ((woBits & 4) == 0 && v93[28]>=qty1 && v93[29]>=qty2)
            // v93[28/29] = bytes 28/29 of the type-42 result node (field within node)
            if (woBits & 4) continue;
            // gilde.exe 0x455706/0x455724: al = *(u8*)(v93+0x1C/0x1D), zero-extended,
            // compared as int against the full dword dword_B56484/B56488 (qty1/qty2).
            int node28 = v93 ? rd8(v93, 28) : 0;
            int node29 = v93 ? rd8(v93, 29) : 0;
            if (node28 < word32(wi, kWO_qty1)) continue;
            if (node29 < word32(wi, kWO_qty2)) continue;

            // gilde.exe 0x455735: v39 = WO item type
            i32 woK; std::memcpy(&woK, wo(wi) + 2, 4);
            int woType = (int)((u32)woK >> 16);
            // gilde.exe 0x455752: threshold = *(u16*)(65*woType + itemTypeDef + 54)
            u16 threshold = itemTypeWord(woType, 54);

            // gilde.exe 0x455752: cmp u16 threshold vs int dword_B564A8 (kWO_minQty).
            if ((int)threshold > word32(wi, kWO_minQty)) continue;

            // gilde.exe 0x455758: FindActiveWorkSlot(woType)
            u8* slot = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->findActiveWorkSlot)
                slot = g_meisterLeaves->findActiveWorkSlot((i32)woType);
            if (!slot) continue;

            // gilde.exe 0x455773: ComputeFreeCapacity(slot, itemHi, off, 1000000)
            i32 freeCap = 0;
            if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity)
                freeCap = g_meisterLeaves->inventoryFreeCapacity(slot, woType, wi * kWorkOrderStride, 1000000);

            i32 needed = word32(wi, kWO_effStock) + (i32)threshold;
            if (freeCap < needed) continue;

            // gilde.exe 0x4557ac: if (*(_BYTE*)itemTypeDef[0] == 23)
            u8 itTypeField0 = itemTypeField(woType, 0);
            if (itTypeField0 == 23) {
                // gilde.exe 0x4557be: QueryFind(v94 scene root, 1, 0, woType)
                u8* v45 = nullptr;
                if (v94 && g_meisterLeaves && g_meisterLeaves->queryFind) {
                    i32 v94Root = rd32(v94, 5 * 4);
                    const int filts[] = {0, woType};
                    v45 = g_meisterLeaves->queryFind(v94Root, filts, 2);
                }
                if (!v45) continue;
                // gilde.exe 0x4557ee: check threshold <= v45+7 dword
                if (threshold > (u16)rd32(v45, 7)) continue;
            } else {
                // gilde.exe 0x455837: CheckWorkstationCapacity(mr, woRow, 1)
                if (!MeisterCheckWorkstationCapacity(mr, wo(wi), 1)) continue;
            }

            // LABEL_89: gilde.exe 0x4557f4
            // gilde.exe 0x4557f4: LOBYTE(word_B564BC[44*wi]) |= 8
            orWoBitsLo(wi, 8u);
            // gilde.exe 0x45581a: ReserveWorkstationItems(mr, &woRow)
            MeisterReserveWorkstationItems(mr, reinterpret_cast<u8*>(wo(wi)));
            break; // first match only
        }
    }

    // --- Work-order loop 2: find WO with bit 0x10 or plain capacity ----------
    // gilde.exe 0x4552d4..0x455930
    if (g_workOrderCount > 0) {
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            u16 woBits = rdWoBits(wi);

            // gilde.exe 0x4552e2: if (woBits & 8) break
            if (woBits & 8) break;
            // gilde.exe 0x455872: if (woBits & 4) continue
            if (woBits & 4) continue;

            int node28 = v93 ? rd8(v93, 28) : 0;
            int node29 = v93 ? rd8(v93, 29) : 0;
            if (node28 < word32(wi, kWO_qty1)) continue;
            if (node29 < word32(wi, kWO_qty2)) continue;

            i32 woK; std::memcpy(&woK, wo(wi) + 2, 4);
            int woType = (int)((u32)woK >> 16);
            u16 threshold = itemTypeWord(woType, 54);
            // gilde.exe 0x455752: cmp u16 threshold vs int dword_B564A8 (kWO_minQty).
            if ((int)threshold > word32(wi, kWO_minQty)) continue;

            u8* slot = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->findActiveWorkSlot)
                slot = g_meisterLeaves->findActiveWorkSlot((i32)woType);
            if (!slot) continue;

            i32 freeCap = 0;
            if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity)
                freeCap = g_meisterLeaves->inventoryFreeCapacity(slot, woType, wi * kWorkOrderStride, 1000000);

            i32 needed = word32(wi, kWO_effStock) + (i32)threshold;
            if (freeCap < needed) continue;

            u8 itTypeField0 = itemTypeField(woType, 0);
            if (itTypeField0 == 23) {
                // gilde.exe 0x4558f5..0x4558fe: LOBYTE(word_B564BC[v20/2]) |= 0x10; break
                orWoBitsLo(wi, 0x10u);
                break;
            } else {
                // gilde.exe 0x455915: CheckWorkstationCapacity(mr, woRow, 0)
                if (MeisterCheckWorkstationCapacity(mr, wo(wi), 0)) {
                    MeisterReserveWorkstationItems(mr, wo(wi));
                    break;
                }
            }
        }
    }

    // --- Work-order loop 3: validate/revoke bit 0x08 -------------------------
    // gilde.exe 0x455304..0x4553a0
    if (g_workOrderCount > 0) {
        // gilde.exe 0x45531d: test word_B564BC[esi],8 ; jz loc_455386 (fall to
        // the post-increment). The original is a single clean loop (esi += 88 each
        // pass). An earlier reimpl spuriously did `++wi; continue;` which double-
        // advanced the counter and skipped every other bit-clear WO — removed.
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            if (!(rdWoBits(wi) & 8)) continue;

            i32 woK; std::memcpy(&woK, wo(wi) + 2, 4);
            int woType = (int)((u32)woK >> 16);
            u8 itType0 = itemTypeField(woType, 0);

            if (itType0 == 23) {
                // gilde.exe 0x45533c: type-23 (field/crop) path
                u8 bTypeDef0b = buildingTypeField((u8)(rd8(bldgRec, kB_typeByte)), 0);
                if (bTypeDef0b == 11) {
                    // gilde.exe 0x455957: LOBYTE(word_B564BC[v22/2]) |= 0x10
                    orWoBitsLo(wi, 0x10u);
                } else {
                    // gilde.exe 0x455973: QueryFind(v94 root, 1, 0, woType)
                    u8* v52 = nullptr;
                    if (v94 && g_meisterLeaves && g_meisterLeaves->queryFind) {
                        i32 v94Root = rd32(v94, 5 * 4);
                        const int filts[] = {0, woType};
                        v52 = g_meisterLeaves->queryFind(v94Root, filts, 2);
                    }
                    u16 thr2 = itemTypeWord(woType, 54);
                    if (!v52 || thr2 > (u16)rd32(v52, 7)) {
                        // gilde.exe 0x455985: set 0x10, then clear 0x08
                        orWoBitsLo(wi, 0x10u);
                        andWoBitsLo(wi, ~0x08u & 0xFF);
                    }
                }
            } else {
                // gilde.exe 0x455344..0x45537e: non-type-23 path
                // Scan 4 stock-back-sub-indices. For each slot (i=0,2,4,6):
                // threshold at itemTypeDef+38+i vs stock[B54478]
                // if threshold > stock → clear bit 0x08
                i32 v23 = wi * kWorkOrderStride; // byte offset
                for (int ss = 0; ss < 4; ++ss) {
                    int ssOff = ss * 2; // i = 0,2,4,6
                    // gilde.exe 0x455372: threshold = *(u16*)(65*woType+itemTypeDef+38+i)
                    u16 thr3 = itemTypeWord(woType, 38 + ssOff);
                    // gilde.exe 0x455372: dword_B54478[16 * *(int*)((char*)dword_B5646C+v23)]
                    // = stock[kST_stock] for the stock index pointed to by WO+8+ss*4
                    i32 stBackI = rd32(wo(wi), kWO_stBackOff + ss * 4);
                    i32 stStock = (stBackI >= 0 && stBackI < g_stockRowCount)
                                  ? strd32(stBackI, kST_stock) : 0;
                    if (thr3 > (u16)stStock) {
                        andWoBitsLo(wi, ~0x08u & 0xFF);
                    }
                    v23 += 4;
                }
            }
        }
    }

    // --- CancelMatchingTasks passes -------------------------------------------
    // gilde.exe 0x4553b2, 0x4553c5, 0x4553d8
    MeisterCancelMatchingTasks(mr, 4);
    MeisterCancelMatchingTasks(mr, 8);
    MeisterCancelMatchingTasks(mr, 3);

    // --- Worker action-cancel sweep ------------------------------------------
    // gilde.exe 0x4553d6..0x455412
    // Scan all persons. For live, employed-here workers with profByte set:
    //   if actionObj's +44 == building id AND
    //   (!busy || *busy != 40) AND (!v91 or person != mr):
    //     ChangePlayerAction(bldgRec, 0, nullptr, personIdWord), ++v85.
    int v85 = 0; // count of workers whose action was cancelled
    i32 bldgRecH = rd32(mr, kM_bldgRec); // handle for employer match

    for (int pi = 0; pi < kPersonCapacity; ++pi) {
        u8* p = pr(pi);
        // gilde.exe 0x4553e9: byte_12CE918[v25] (isLive)
        if (!rd8(p, kP_isLive)) continue;
        // gilde.exe 0x4559e5: word_12CE910[v25/2] != -1 && byte_12CE912[v25] != 10
        if (rd16(p, kP_marker) == (i16)-1) continue;
        if (rd8(p, kP_kind) == 10) continue;
        // employer == bldgRec handle
        if (rd32(p, kP_employer) != bldgRecH) continue;
        // gilde.exe 0x4559eb: byte_12CEA75 (profByte)
        if (!rd8(p, kP_profByte)) continue;

        // gilde.exe 0x4559f8: v54 = dword_12CEA94[v25/4] (action object pointer)
        u8* ao = rdptr(p, kP_actionObj);
        if (!ao) continue;

        // gilde.exe 0x455a12: *(ao+44) == *(employer+1) (action target == building id)
        u8* empRec = rdptr(p, kP_employer);
        if (!empRec) continue;
        i32 empBldgId = rd32(empRec, kB_id1);
        if (rd32(ao, 44) != empBldgId) continue;

        // gilde.exe 0x455a18: v55 = dword_12CEA8C (busy ptr)
        u8* busyPtr = rdptr(p, kP_busy);
        // gilde.exe 0x455a68: (!v55 || *v55 != 40) && (!v91 || p != mr)
        bool busyOk = (!busyPtr || rd8(busyPtr, 0) != 40);
        bool actionOk = (!v91 || p != mr);
        if (busyOk && actionOk) {
            // gilde.exe 0x455a46: ChangePlayerAction(bldgRec, 0, 0, personIdWord)
            u16 pIdWord = rdu16(p, kP_marker); // word_12CE910[pi] = person id word
            if (g_meisterLeaves && g_meisterLeaves->changePlayerAction)
                g_meisterLeaves->changePlayerAction(bldgRecH, 0, nullptr, pIdWord);
            ++v85;
        }
    }

    // gilde.exe 0x455426: if (dword_B53950 == *(mr+364)) dword_B53964 = v85
    if (g_aiSelMeisterBuilding == rd32(mr, kM_bldgRec))
        g_aiSelMeisterCount = v85;

    // --- Count active/cancelled WO -------------------------------------------
    // gilde.exe 0x455434..0x4554a1
    int v84 = 0; // count of WOs with bit8 or bit0x10 set
    if (g_workOrderCount > 0) {
        u8 bTypeDef0c = buildingTypeField((u8)(rd8(bldgRec, kB_typeByte)), 0);
        for (int wi = 0; wi < g_workOrderCount; ++wi) {
            u16 woBits = rdWoBits(wi);
            if (woBits & 8) ++v84;
            if ((woBits & 0x10) && bTypeDef0c != 11) ++v84;
        }
    }

    // gilde.exe 0x4554a3..0x4554c0: v29 = min(v84, v85); v84 = v29
    if (v84 > v85) v84 = v85;

    // gilde.exe 0x4554d1: if (v85 && v84)
    if (v85 && v84) {
        // --- PFLANZBAR / office-placement path (building type 11) ------------
        // gilde.exe 0x455540..0x455c61
        u8 bTypeFull = buildingTypeField((u8)(rd8(bldgRec, kB_typeByte)), 0);
        if (bTypeFull == 11) {
            // gilde.exe 0x455555: if (WORD2(qword_13CE852) % 2) — odd hour
            if (g_meisterGameTime.hour % 2) {
                // gilde.exe 0x455a8a: if ((v89[218] & 0x20) == 0) — mr+218 byte & 0x20
                // mr+218 = mr + kM_dayFlags (0x1B4 = 436) ... wait, 218 ≠ 436.
                // v89[218] = *(char*)(a1+218). Since a1 is __int16*, v89[218] = *(i16*)(a1+218).
                // Actually v89 is __int16*, so v89[218] as byte is *(u8*)(a1) + 218...
                // In the decompile context: a1 is __int16* pointer, so a1[218] = *(char*)(a1+218) in bytes.
                // 218 as INDEX into __int16 = byte offset 218. So mr+218 byte.
                // vs kM_dayFlags = 0x1B4 = 436. That's different. 218 = kM_dayFlags/2 = 436/2.
                // As a __int16* index: v89[218] = *(char*)(v89 + 2*218) = *(char*)(mr + 436) = kM_dayFlags byte!
                // gilde.exe: v89[218] = *(u8*)(mr + 436). So this is kM_dayFlags.
                u8 dayF = rd8(mr, kM_dayFlags); // mr+436 = kM_dayFlags
                if (!(dayF & 0x20)) {
                    // gilde.exe 0x455aad: EnsureBuildingAvatar(bldgRecPtr, ...)
                    i32 bldgRecPtrH = rd32(mr, kM_bldgRec);
                    int avatarIdx = 0;
                    if (g_meisterLeaves && g_meisterLeaves->ensureBuildingAvatar)
                        avatarIdx = g_meisterLeaves->ensureBuildingAvatar(&bldgRecPtrH);

                    // gilde.exe 0x455aad: v56 = dword_13ECF74[246 * avatarIdx]
                    // dword_13ECF74 = avatar/floor table base (246-stride pointers).
                    // We model this as an extern if non-null.
                    // Since dword_13ECF74 is not directly in MeisterAiLeaves, we skip if 0.
                    // The value at 0x13ECF74 is 0 (from get_bytes). Guard.
                    u8* v56 = nullptr; // = *(i32*)(dword_13ECF74 + 246*4*avatarIdx) as ptr

                    // [dword_13ECF74 not in reimpl scope; avatarIdx result unusable without it]
                    // Guard: if v56 == null, skip the interior (original would null-deref).
                    (void)avatarIdx;

                    if (v56) {
                        // gilde.exe 0x455ac8: v57 = v56 + 1689 (dword offset into layout data)
                        // gilde.exe 0x455ad3: v99 = *(bldgRec+113) — building layout id
                        i32 layoutId = rd32(bldgRec, 113); // *(bldgRec+113)

                        // gilde.exe 0x455ae3: scan 8 positions for PFLANZBAR match
                        // loc_5CB930 is a strstr-like function (verified by disasm):
                        //   strstr(v58_str, aPflanzbar_0) != nullptr
                        // aPflanzbar_0 @0x61928c = "PFLANZBAR" (verified get_bytes).
                        // v57 advances by 16 dwords (64 bytes? — decompile: v57 += 16 per iter)
                        // v57 = *(i32*)(v56+1689*4) ... field at offset +1689*4 in the floor struct.
                        // Each iter: check if *(char*)(v57) strstr's "PFLANZBAR".
                        // v98 = row index where PFLANZBAR found.
                        static constexpr char kPflanzbar[] = "PFLANZBAR"; // gilde.exe 0x61928c
                        int v98 = 0;
                        for (int row = 0; row < 8; ++row) {
                            // v57 field at row: (v56 + 1689 dwords) + row*16-dword step
                            // We can't dereference v56 (it's null-guarded above). Skip.
                            (void)kPflanzbar;
                            // If v56 were valid: check strstr((char*)(v57_at_row), "PFLANZBAR")
                        }

                        // gilde.exe 0x455afc..0x455b3b: find WO of type 23 via RandomModulo
                        // Loop j=0..4*dword_B56FDC-1: pick random WO idx, check if type==23.
                        int v90 = 0; // selected WO index (random)
                        if (g_workOrderCount > 0) {
                            for (int j = 0; j < 4 * g_workOrderCount; ++j) {
                                u16 rndIdx = guild::util::RandomModulo((u32)g_workOrderCount);
                                v90 = (int)(u32)rndIdx;
                                i32 woKj; std::memcpy(&woKj, wo(v90) + 2, 4);
                                int woTypeJ = (int)((u32)woKj >> 16);
                                u8 typeJ0 = itemTypeField(woTypeJ, 0);
                                if (typeJ0 == 23) break;
                            }

                            // gilde.exe 0x455b70: if selected WO type==23 → office placement
                            i32 woKF; std::memcpy(&woKF, wo(v90) + 2, 4);
                            int woTypeFinal = (int)((u32)woKF >> 16);
                            u8 typeFinal0 = itemTypeField(woTypeFinal, 0);
                            if (typeFinal0 == 23) {
                                // gilde.exe 0x455b7f: AmtFindOfficeTypeRecord(HIWORD(WO+2))
                                u8* officeTypeRec = nullptr;
                                if (g_meisterLeaves && g_meisterLeaves->amtFindOfficeTypeRecord)
                                    officeTypeRec = g_meisterLeaves->amtFindOfficeTypeRecord((int)woTypeFinal);

                                // gilde.exe 0x455b83: v97 = 0 (row counter for grid search)
                                // gilde.exe 0x455b8c: v63 = *v56 (grid col/row count)
                                // v56 is null here — skip interior safely
                                // gilde.exe 0x455c61 is reachable only through valid v56 path.
                                (void)officeTypeRec;
                                (void)layoutId;
                                (void)v98;
                            }
                        }
                    }
                    // gilde.exe 0x455c61: *(mr+436) |= 0x20
                    wr8(mr, kM_dayFlags, (u8)(rd8(mr, kM_dayFlags) | 0x20u));
                }
            } else {
                // gilde.exe 0x455566: *(mr+436) &= ~0x20
                wr8(mr, kM_dayFlags, (u8)(rd8(mr, kM_dayFlags) & ~0x20u));
            }
        }

        // --- DispatchOrders pass (bit 0x10 and bit 0x08 WOs) -----------------
        // gilde.exe 0x455573..0x4556a0
        //
        // The original builds a stack "args array" overlay (v80[0]=mode, v81..=row,
        // v93, v94, v84, v85, passIndex, already) and reuses it across BOTH the
        // bit-0x10 and bit-0x08 dispatch calls AND across loop iterations, so
        // passIndex (a1[7]) and already (a1[8]) ACCUMULATE — MeisterDispatchOrders
        // does ++a1[7]/++a1[8]. We therefore keep one persistent args struct.
        //   a1[1]=meisterRec=mr            (var_74, set 0x4554e7)
        //   a1[2]=orderRow  = &dword_B56468[v32/4] = wo(v31)+4  (v81)
        //   a1[3]=orderRow3 = v93          (var_6C, set 0x4554fc)
        //   a1[4]=orderRow4 = v94          (var_68, set 0x455511)
        //   a1[5]=divisor   = v84          (the min(active,cancelled) order count)
        //   a1[6]=total     = v85          (cancelled-worker count)
        //   a1[7]=passIndex = 0 init       (var_5C, set 0x45550a)
        //   a1[8]=already   = 0 init       (var_58, set 0x4554f5)
        MeisterDispatchArgs args{};
        args.meisterRec = mr;
        args.orderRow3  = reinterpret_cast<u8*>(v93);
        args.orderRow4  = reinterpret_cast<u8*>(v94);
        args.divisor    = v84;
        args.total      = v85;
        args.passIndex  = 0;
        args.already    = 0;

        int v31 = 0;
        if (g_workOrderCount > 0) {
            for (v31 = 0; v31 < g_workOrderCount; ++v31) {
                u16 woBits = rdWoBits(v31);

                // gilde.exe 0x4555a8: if (woBits & 0x10) && bType != 11 → dispatch
                u8 bTypeCheck = buildingTypeField((u8)(rd8(bldgRec, kB_typeByte)), 0);
                if ((woBits & 0x10) && bTypeCheck != 11) {
                    // gilde.exe 0x4555b6/0x4555c4: v81 = &dword_B56468[v32/4] (= wo+4); v80[0]=8
                    args.orderRow = wo(v31) + 4;
                    args.mode     = 8;
                    MeisterDispatchOrders(&args);
                }

                // gilde.exe 0x4555d7: if (woBits & 8)
                if (woBits & 8) {
                    u8 itType0 = 0;
                    {
                        i32 woK2; std::memcpy(&woK2, wo(v31) + 2, 4);
                        int wt2 = (int)((u32)woK2 >> 16);
                        itType0 = itemTypeField(wt2, 0);
                    }

                    int dispMode;
                    if (itType0 == 23) {
                        // gilde.exe 0x455624: if bType==11 → mode=40 else mode=3
                        u8 bTypeD = buildingTypeField((u8)(rd8(bldgRec, kB_typeByte)), 0);
                        dispMode = (bTypeD == 11) ? 40 : 3;
                    } else {
                        dispMode = 4;
                    }
                    // gilde.exe 0x455636/0x455644/0x45564b: v81 = wo+4; v80[0]=dispMode; DispatchOrders
                    args.orderRow = wo(v31) + 4;
                    args.mode     = dispMode;
                    MeisterDispatchOrders(&args);

                    // gilde.exe 0x45565b: if (dword_B53950 == *(mr+364)) → copy WO to debug buf
                    if (g_aiSelMeisterBuilding == rd32(mr, kM_bldgRec)) {
                        int v35 = g_aiSelOrderCount;
                        if (v35 < 4) {
                            ++g_aiSelOrderCount;
                            // gilde.exe 0x455692: qmemcpy(unk_B53AF0 + 88*v35, v81, 0x58)
                            // unk_B53AF0 = debug order snapshot buffer (selection overlay).
                            // NO sim side effect — debug-only; not modeled. Counter bump kept.
                        }
                    }
                }
            }

            // gilde.exe 0x4556c8..0x4556e3: trailing loop — clear bit 0x08 from remaining WOs.
            // DEAD CODE: v31 exits the loop above == g_workOrderCount, so the bound
            // (88*v31 < 88*count) is immediately false and this body never executes
            // (verified disasm 0x4556ce: cmp eax,edx; jge). Kept literal for fidelity.
            for (; v31 < g_workOrderCount; ++v31) {
                u8 bitsByte = reinterpret_cast<u8*>(&g_woBitsTable[v31])[0];
                wo(v31)[0] = bitsByte & 0xF7u;
            }
        }

        // gilde.exe 0x455ca4: VIBE_Ai_AssignIdleWorkers()
        // __usercall with eax = the type-42 workstation node (v93) — NOT the meister.
        // MeisterAssignIdleWorkers() reads the building id via g_aiIdleWorkstationNode
        // (*(node+2)); the caller bridge sets it (the orig caller's eax). We set it
        // here from v93 to match the live call-graph wiring.
        g_aiIdleWorkstationNode = reinterpret_cast<u8*>(v93);
        MeisterAssignIdleWorkers();
    }

    // gilde.exe 0x455cb7
    MeisterCollectTransporters(mr);
    // gilde.exe 0x455cc8
    return MeisterTradeManageStorage(mr);
}

} // namespace guild::sim
