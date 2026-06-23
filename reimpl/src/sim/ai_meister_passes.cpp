// ===========================================================================
// ai_meister_passes.cpp — 1:1 reconstruction of the three work-order pass
// DRIVER functions for the MeisterAi planner cluster.
//
//   0x45d4c4  VIBE_MeisterAi_CancelMatchingTasks → MeisterCancelMatchingTasks
//   0x45d618  VIBE_MeisterAi_DispatchOrders      → MeisterDispatchOrders
//   0x45379c  VIBE_Ai_AssignIdleWorkers           → MeisterAssignIdleWorkers
//
// These are the LOOP DRIVERS around the pure decision cores already in
// src/sim/meister_mgmt_recon.{h,cpp}:
//   CancelTaskFlagMask / TaskMatchesOrder  — used by MeisterCancelMatchingTasks
//   DispatchOrderCount                     — used by MeisterDispatchOrders
//   IdleWorkerShouldQueue                  — used by MeisterAssignIdleWorkers
//
// DATA MODEL (rule 1 byte-faithful): record arrays are addressed by raw byte
// offset through the rd*/wr*/rdptr helpers from ai_meister_internal.h.
// Work-order scratch table: g_workOrderTable (kWorkOrderStride=88 bytes/row,
// g_workOrderCount rows). Flag column: g_woBitsTable (local parallel array,
// word_B564BC base; stride 88 bytes = one u16 per WO entry, indexed as
// g_woBitsTable[i] for WO index i). This mirrors the identical static array
// in ai_meister_calc_farming.cpp and ai_meister_equip.cpp — each TU has its
// own copy because the original 32-bit image had a single global address
// (0xB564BC); in the reimpl each TU holds a local copy and the test harness
// injects values directly.
//
// DispatchOrders args[] layout (int* a1 in orig, stack struct in callers):
//   a1[0] = mode     — command type byte: 4 (Wache), 8 (Angriff/spy), 3 (patrol),
//                       20 (farmhand dispatch), 21 (amt/office dispatch), 40 (guard slot)
//   a1[1] = meisterRec — the meister's person record base (u8*, as int in orig)
//   a1[2] = orderRow   — ptr to the current work-order row (dword_B56468 element);
//                         *(i16*)(a1[2]) is the order type word. Read as:
//                         HIWORD = item type id, LOWORD = category
//   a1[3] = orderRow3  — secondary WO row ptr (mode 3 and 4 only);
//                         *(dword*)(a1[3]+2) holds a building id
//   a1[4] = orderRow4  — tertiary WO row ptr (mode 3 and 8 only);
//                         *(dword*)(a1[4]+2) holds a building id
//   a1[5] = divisor    — count distribution divisor (number of dispatch passes)
//   a1[6] = total      — total worker slots available
//   a1[7] = passIndex  — 0 on first call; function increments it before returning
//   a1[8] = already    — running count of already-dispatched workers;
//                         function increments it for each worker dispatched
//
// DispatchOrderCount (pure core) formula:
//   if (divisor <= 1 || passIndex) count = total/divisor + already
//   else                           count = total%divisor + total/divisor + already
//   (the remainder folds into the first pass only)
//
// MeisterAssignIdleWorkers (0x45379c) receives the TYPE-42 QueryFind result
// (the workstation node) in eax from each caller (CalcMeisterProduction /
// CalcMeisterCraftProduction / CalcMeisterFarming) — not the meisterRec.
// *(workstationNode+2) is the building id used as the first arg of QueueRequest20.
// The reimpl header declares void MeisterAssignIdleWorkers() with no args since
// the callers provide the workstation node from their own context; the function
// reads the building id via g_aiIdleWorkstationNode (set by the caller bridge).
//
// VIBE_Light_SetGrayColorThunk(0,248,&buf): memset(buf,0,248) — reproduced as
// MeisterCommand cmd{} (value-initialized, equivalent zero-init of the 248-byte
// stack struct the original builds).
//
// VIBE_Crt_Sprintf_0: NO sim side-effect — dropped per SPEC.
//
// VIBE_Amt_FindRecordByKey (0x56e8ec) in mode-40 branch: uses AmtFindRecordByKey
// from world/amt_slot_table.h. The orig passes a 64-entry AmtSlot table pointer
// loaded from *(meisterBldgRec+113); the reimpl exposes this via g_meisterLeaves->
// amtFindOfficeTypeRecord (closest existing hook; documented below).
//
// External leaves: all through g_meisterLeaves (null-guarded) or the local
// callback g_aiIdleQueueRequest20 for AssignIdleWorkers. See below.
// ===========================================================================

#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/meister_mgmt_recon.h"
#include "sim/entity.h"         // g_persons, g_personIds, kPersonCapacity
#include "util/math_random.h"   // guild::util::RandomModulo (unused here but sibling pattern)

#include "world/amt_slot_table.h"  // AmtFindRecordByKey, AmtSlot, kAmtSlotCount

#include <cstring>
#include <cstdint>

namespace guild::sim {
using namespace aimei;
using guild::util::RandomModulo;

// ---------------------------------------------------------------------------
// File-local work-order flag column — word_B564BC[44*i] == g_woBitsTable[i]
// (u16 per WO entry, indexed as g_woBitsTable[woIdx]; stride 88 bytes from
// B564BC base). Same layout as in ai_meister_calc_farming.cpp and
// ai_meister_equip.cpp. Each TU owns its own copy; tests inject via direct
// array access.
// gilde.exe 0xB564BC
// ---------------------------------------------------------------------------
static u16 g_woBitsTable[kMaxWorkOrders] = {};

// ---------------------------------------------------------------------------
// Helper: read work-order item-type HIWORD from WO row i.
// The orig: *(int*)((char*)&dword_B56464[v/2] + 2) >> 16
// where v is a word-stride index = 44*i. In byte terms: g_workOrderTable +
// kWorkOrderStride*i + 2, read as i32, right-shifted 16.
// ---------------------------------------------------------------------------
static inline int woItemHi(int i) {
    i32 v;
    std::memcpy(&v, g_workOrderTable + kWorkOrderStride * i + 2, 4);
    return (int)((u32)v >> 16);
}

// ---------------------------------------------------------------------------
// Helper: read work-order effective-stock (kWO_effStock = +0x40 from B56464).
// In the orig: dword_B564A4[v3/2] where v3 advances by 44 (word stride),
// which byte-indexes as *(i32*)(B564A4 + 88*i) = WO[i] + 0x40.
// kWO_effStock = B564A4 - B56464 = 0x40.
// ---------------------------------------------------------------------------
static constexpr int kWO_effStock = 0x40;  // B564A4 - B56464 = 64

// Access to the work order row flags (u8 LOBYTE, matching orig "LOBYTE(word_B564BC[...])"):
static inline u16  rdWoBits(int i) {
    return (i >= 0 && i < kMaxWorkOrders) ? g_woBitsTable[i] : 0u;
}
// (used by tests for direct injection):
static inline void setWoBits(int i, u16 v) {
    if (i >= 0 && i < kMaxWorkOrders) g_woBitsTable[i] = v;
}

// ---------------------------------------------------------------------------
// g_aiIdleQueueRequest20 — the QueueRequest20 leaf for AssignIdleWorkers.
// The original calls VIBE_Command_QueueRequest20(bldgId, itemHi).
// Routed through g_meisterLeaves->queueRequest20 (already in MeisterAiLeaves).
// gilde.exe 0x4946f4
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// g_aiIdleWorkstationNode — the workstation node base (type-42 QueryFind result)
// that the CALLER places in eax before calling AssignIdleWorkers. In the orig
// the caller's eax holds this pointer; in the reimpl the caller sets this global
// before calling MeisterAssignIdleWorkers().  *(g_aiIdleWorkstationNode+2) is
// the building id used as the first arg to QueueRequest20 (the orig mov eax,[esi+2]).
// The bridge/tests set this before each MeisterAssignIdleWorkers() call.
// ---------------------------------------------------------------------------
// gilde.exe 0x45379c, line 0x4537c9: "mov eax, [esi+2]"  (esi = the node ptr)
u8* g_aiIdleWorkstationNode = nullptr;

// ---------------------------------------------------------------------------
// 0x45d4c4 VIBE_MeisterAi_CancelMatchingTasks
// __usercall: eax=meisterRec, edx=filterType
//
// Iterates the He handler list (heFindFirst(2,0,filterType,3,bldgId)/heFindNext).
// For each handler, scans the work-order table looking for an order whose item
// type HIWORD matches the handler's HIWORD (at handler+170) AND whose flag bit
// (CancelTaskFlagMask(filterType)) is clear. If found (v5=1), walks g_persons
// and calls changePlayerAction on any person whose kP_busy dword (dword_12CEA8C)
// matches the handler base pointer (the orig "i == dword_12CEA8C[j/4]").
//
// In the reimpl the "busy" column (kP_busy = dword_12CEA8C) is a 32-bit HANDLE
// (not a raw 32-bit pointer) per the UPDATE 1 handle model. The comparison
// "i == dword_12CEA8C[j/4]" in the original compares the handler ADDRESS with
// the 32-bit handle stored in the busy column. Since handler base is a live
// pointer from the He subsystem (not a reimpl handle), we compare the HANDLE
// stored in kP_busy against a handle encoding of the handler. The bridge must
// store the handler pointer as a handle; in practice the test harness puts the
// raw pointer casted to i32 in kP_busy (matching the orig store pattern) and
// we compare as i32 values.
//
// handler item-type: *(int*)(handler+170) >> 16 == *(i+170) in orig.
//   handler+170 = handler + 42*4 + 2. The He handler base is a u8* in our model.
//
// gilde.exe 0x45d4c4
// ---------------------------------------------------------------------------
void MeisterCancelMatchingTasks(u8* meisterRec, int filterType) {
    // gilde.exe 0x45d4e6: FindFirstHandlerByFilter(2, 0, filterType, 3, bldgId)
    u8* bldgRec = rdptr(meisterRec, kM_bldgRec);
    if (!bldgRec) return;
    i32 bldgId = rd32(bldgRec, kB_id1);  // *(bldgRec+1)

    if (!g_meisterLeaves || !g_meisterLeaves->heFindFirst) return;

    u16 flagMask = CancelTaskFlagMask(filterType);

    u8* handler = g_meisterLeaves->heFindFirst(2, 0, filterType, 3, bldgId);
    // gilde.exe 0x45d4f2: for (i=(int)result; result; i=(int)result)
    while (handler) {
        // gilde.exe 0x45d4fb: v5 = 0 (found flag)
        int v5 = 0;

        // --- Filter-type branches (all equivalent: check flag bit vs handler item type)
        // The decompile has three separate if-blocks (a2==4, a2==8, a2==3),
        // each scanning the full WO table. We unify using CancelTaskFlagMask + TaskMatchesOrder.
        // The WO item key HIWORD: *(int*)((char*)&dword_B56464[v/2]+2)>>16 = woItemHi(i).
        // Handler item HIWORD:    *(int*)(handler+170)>>16.
        //
        // gilde.exe 0x45d509, 0x45d54f, 0x45d58f
        if (flagMask != 0 && g_workOrderCount > 0) {
            // handler item type HIWORD: *(i32*)(handler+170) >> 16
            i32 handlerKeyDw;
            std::memcpy(&handlerKeyDw, handler + 170, 4);

            // Scan WO table — v6/v7/v8 advances by 44 (word stride); v6/44 = WO index.
            // gilde.exe 0x45d517: v6 = 0; do { ... v6 += 44; } while (v6 < 44*dword_B56FDC)
            for (int i = 0; i < g_workOrderCount; ++i) {
                // gilde.exe 0x45d533 / 0x45d573 / 0x45d5b3:
                // orderKey = *(int*)((char*)&dword_B56464[v6/2u]+2)  (the per-WO key dword at offset+2)
                // handlerKey = *(int*)(handler+170)
                // orderFlags = word_B564BC[v6] (flag at v6*2 bytes from B564BC; v6 is word-stride)
                // In reimpl: orderKey hi = woItemHi(i); handlerKey hi = handlerKeyDw>>16;
                // orderFlags = g_woBitsTable[i].
                //
                // TaskMatchesOrder compares (orderKey>>16)==(handlerKey>>16) && (orderFlags&mask)==0.
                // We pass the raw dwords:
                i32 orderKeyAt2;
                std::memcpy(&orderKeyAt2, g_workOrderTable + kWorkOrderStride * i + 2, 4);
                if (TaskMatchesOrder((int)orderKeyAt2, handlerKeyDw, rdWoBits(i), flagMask)) {
                    v5 = 1;  // gilde.exe 0x45d535 / 0x45d575 / 0x45d5b5
                    // (the original doesn't break here — it continues scanning all WO entries)
                }
            }
        }

        // gilde.exe 0x45d5c3: if (v5) { ... walk g_persons ... }
        if (v5) {
            // Walk g_persons (stride 536, 768 entries = 411648 total bytes).
            // gilde.exe 0x45d5c5: for (j=0; j!=411648; j+=536)
            //   if (word_12CE910[j/2] != -1 && i == dword_12CEA8C[j/4])
            //     ChangePlayerAction(*(a1+364), 0, 0, (u16)word_12CE910[j/2])
            // "i" here is the handler base (pointer in orig; handle/value in reimpl).
            // dword_12CEA8C[j/4] = kP_busy column. We compare as i32 values.
            i32 handlerAsI32 = (i32)(std::uintptr_t)handler;  // the orig pointer fits i32 in 32-bit

            for (unsigned int j = 0; j < (unsigned int)(kPersonCapacity * kPersonStride);
                 j += (unsigned int)kPersonStride) {
                int pIdx = (int)(j / (unsigned int)kPersonStride);
                u8* pBase = pr(pIdx);

                // gilde.exe 0x45d5da: word_12CE910[j/2] != -1
                i16 marker; std::memcpy(&marker, pBase + kP_marker, 2);
                if (marker == (i16)(-1)) continue;

                // gilde.exe 0x45d5da: i == dword_12CEA8C[j/4]
                // kP_busy = 0x17C = dword_12CEA8C. Compare raw i32 (handle or pointer value).
                i32 busyVal; std::memcpy(&busyVal, pBase + kP_busy, 4);
                if (busyVal != handlerAsI32) continue;

                // gilde.exe 0x45d5eb: ChangePlayerAction(*(a1+364), 0, 0, (u16)word_12CE910[j/2])
                // *(a1+364) = rd32(meisterRec, kM_bldgRec) — the handle, NOT the resolved ptr.
                i32 bldgRecHandle = rd32(meisterRec, kM_bldgRec);
                u16 personMarkerWord = (u16)(marker & 0xFFFF);
                if (g_meisterLeaves && g_meisterLeaves->changePlayerAction)
                    g_meisterLeaves->changePlayerAction(bldgRecHandle, 0, nullptr, personMarkerWord);
            }
        }

        // gilde.exe 0x45d5fe: result = VIBE_He_FindNextMatchingHandler()
        if (g_meisterLeaves && g_meisterLeaves->heFindNext)
            handler = g_meisterLeaves->heFindNext();
        else
            handler = nullptr;
    }
    // gilde.exe 0x45d60d: return result (eax = last FindNext result = nullptr)
}

// ---------------------------------------------------------------------------
// 0x45d618 VIBE_MeisterAi_DispatchOrders
// __usercall: eax=a1 (int* args, described in module comment above)
//
// Dispatches work-orders from the WO scratch table to available workers.
// Mode dispatch:
//   *a1 == 40 : office/Amt slot dispatch (guard-slot mode)
//   *a1 == 20 : farmhand/production dispatch (no He scan, direct WO)
//   *a1 == 21 : mode-21 (granary/mill, with QueryFind on scene root)
//   *a1 == 4/8/3 : combat/espionage dispatch (He scan for existing handler,
//                  new command if not found, or ChangePlayerAction if found)
//
// The inner worker scan: stride 536 bytes, 411648 total (768 persons).
// Conditions: word_12CE910[v/2]!=-1 (alive), dword_12CEA7C[v/4]==bldgRec (employer),
//             byte_12CEA75[v] (profession assigned), !dword_12CEA8C[v/4] (not busy),
//             dword_12CEA94[v/4] (has action obj), *(actionObj+44)==*(employer+1) (same bldg).
//
// gilde.exe 0x45d618
// ---------------------------------------------------------------------------
void MeisterDispatchOrders(MeisterDispatchArgs* args) {
    int  mode       = args->mode;       // a1[0]
    u8*  mr         = args->meisterRec; // a1[1]
    u8*  orderRow   = args->orderRow;   // a1[2]
    u8*  orderRow3  = args->orderRow3;  // a1[3]
    u8*  orderRow4  = args->orderRow4;  // a1[4]
    // a1[5..8] are divisor/total/passIndex/already.

    u8*  bldgRec = rdptr(mr, kM_bldgRec);  // *((_DWORD*)a1[1] + 91) = mr+364

    // Helper: read bldgId from meister's building record.
    auto getBldgId = [&]() -> i32 {
        return bldgRec ? rd32(bldgRec, kB_id1) : 0;
    };

    // Helper: owner word (a1[1]+364+39 in orig = bldgRec+39 = kB_owner39).
    auto getOwnerWord = [&]() -> u16 {
        return bldgRec ? rdu16(bldgRec, kB_owner39) : 0u;
    };

    // Helper: pack a MeisterCommand header (Light_SetGrayColorThunk → cmd{}).
    // gilde.exe 0x45d681 / 0x45db09 / 0x45dd08 / 0x45d960
    auto makeCmd = [&]() -> MeisterCommand {
        MeisterCommand c{};
        c.cmdType     = (u8)mode;  // *(BYTE*)a1 → cmd+4
        // actor id: dword_12CE914[134 * *(u16*)(bldgRec+39)]
        u16 ownerW    = getOwnerWord();
        c.actorId     = (ownerW < (u16)kPersonCapacity) ? g_personIds[ownerW] : 0;
        c.buildingId  = getBldgId();
        // game time stamp: qword_13CE852 / unk_13CE85A / unk_13CE85E
        c.timePacked  = (u64)(u32)g_meisterGameTime.day |
                        ((u64)(u32)g_meisterGameTime.hour << 32);
        c.timeExtra   = g_meisterTimeExtra;
        c.timeTail    = g_meisterTimeTail;
        return c;
    };

    // -------------------------------------------------------------------------
    // Worker-scan predicate (the inner loop over g_persons):
    // Conditions for a person to be a dispatchable worker (for modes 20/21/40):
    //   word_12CE910[v/2] != -1          (alive, kP_marker != -1)
    //   dword_12CEA7C[v/4] == bldgRec    (employer == this building)
    //   byte_12CEA75[v]    != 0           (profession assigned, kP_profByte)
    //   dword_12CEA8C[v/4] == 0           (not busy, kP_busy == 0)
    //   dword_12CEA94[v/4] != 0           (has action object, kP_actionObj)
    //   *(actionObj+44) == *(employer+1)  (action obj building == employer building)
    //
    // For modes 4/8/3 (found-handler path): same conditions but NO actionObj check
    //   (orig calls ChangePlayerAction directly from the found handler).
    // -------------------------------------------------------------------------
    auto personQualifies = [&](int pIdx) -> bool {
        u8* pBase = pr(pIdx);
        i16 marker; std::memcpy(&marker, pBase + kP_marker, 2);
        if (marker == (i16)(-1)) return false;

        // employer == bldgRec handle
        i32 empH = rd32(pBase, kP_employer);
        i32 bldgH = bldgRec ? rd32(mr, kM_bldgRec) : 0;
        if (empH != bldgH) return false;

        if (!rd8(pBase, kP_profByte)) return false;    // profession byte == 0
        if (rd32(pBase, kP_busy) != 0) return false;   // busy dword != 0

        u8* ao = rdptr(pBase, kP_actionObj);
        if (!ao) return false;                          // no action object

        // *(actionObj+44) == *(employer+1) → action obj's building id == employer id
        u8* emp = rdptr(pBase, kP_employer);
        if (!emp) return false;
        i32 aoBldg; std::memcpy(&aoBldg, ao + 44, 4);
        i32 empBldg = rd32(emp, kB_id1);
        return aoBldg == empBldg;
    };

    // -------------------------------------------------------------------------
    // MODE 40 — office/Amt guard-slot dispatch
    // gilde.exe 0x45d7ff..0x45da6b / 0x45d8ab..0x45da41
    // -------------------------------------------------------------------------
    if (mode == 40) {
        // gilde.exe 0x45d802: v85 = *(_DWORD**)(*(a1[1]+364)+113)
        //   bldgRec+113 (BYTE offset) holds a RAW pointer to the 64-entry Amt slot
        //   table (24-byte AmtSlot records). This is embedded DATA, not a record
        //   handle, so we read it raw — exactly the original's *(_DWORD**)(...).
        //   In a live bridge this is a real table; headless/tests inject it (or it
        //   is null → no slots → no decrement, no emit — which is faithful).
        guild::world::AmtSlot* v85 = nullptr;
        if (bldgRec) std::memcpy(&v85, bldgRec + 113, sizeof(v85));

        // gilde.exe 0x45d812: count distribution
        int v86 = DispatchOrderCount(args->divisor, args->total,
                                     args->passIndex, args->already);

        // gilde.exe 0x45d861..0x45d897: He-handler loop — for each handler whose
        // Amt record matches the order type, decrement v86.
        if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
            u8* hj = g_meisterLeaves->heFindFirst(2, 0, mode, 3, getBldgId());
            while (hj) {
                // gilde.exe 0x45d863: edx = handler[0xB8] = *(i32*)(handler+184) = j[46]
                i32 handlerKey = rd32(hj, 0xB8);
                // gilde.exe 0x45d870: RecordByKey = VIBE_Amt_FindRecordByKey(v85, j[46])
                if (v85) {
                    int recIdx = guild::world::AmtFindRecordByKey(v85, handlerKey);
                    if (recIdx >= 0) {
                        // gilde.exe 0x45d879: eax = RecordByKey[2] = *(i32*)(slot+8); sar 16
                        i32 slotDw8;
                        std::memcpy(&slotDw8,
                                    reinterpret_cast<u8*>(&v85[recIdx]) + 8, 4);
                        // gilde.exe 0x45d882: edx = (i16)*orderRow  (movsx)
                        i16 ordW = 0;
                        if (orderRow) std::memcpy(&ordW, orderRow, 2);
                        // gilde.exe 0x45d885: if (slotDw8>>16 == (i16)*orderRow) --v86
                        if ((slotDw8 >> 16) == (int)ordW) --v86;
                    }
                }
                hj = g_meisterLeaves->heFindNext ? g_meisterLeaves->heFindNext()
                                                 : nullptr;
            }
        }

        // gilde.exe 0x45d8a3: if (v86 <= a1[8]) goto LABEL_21 (++passIndex, return)
        if (v86 <= args->already) goto dispatch_done;

        // gilde.exe 0x45d8ab..0x45da41: worker scan loop. For each qualifying worker,
        // scan v85 for a free slot (marker +0xD > 1 && *(slot+0x10) == -1); if NONE
        // found, RETURN immediately WITHOUT bumping passIndex (gilde.exe 0x45d94c →
        // 0x45d7e6). Otherwise emit a command and ++already.
        {
            unsigned int v87 = 0;
            for (;;) {
                int pIdx = (int)(v87 / (unsigned int)kPersonStride);
                if (personQualifies(pIdx)) {
                    // gilde.exe 0x45d92f..0x45d948: linear free-slot scan (cap 64).
                    guild::world::AmtSlot* freeSlot = nullptr;
                    if (v85) {
                        for (int s = 0; s < guild::world::kAmtSlotCount; ++s) {
                            u8* sb = reinterpret_cast<u8*>(&v85[s]);
                            // *(char*)(slot+0xD) > 1   (signed byte)
                            i8 marker; std::memcpy(&marker, sb + 0xD, 1);
                            i32 objAt10; std::memcpy(&objAt10, sb + 0x10, 4);
                            if ((int)marker > 1 && objAt10 == -1) { freeSlot =
                                &v85[s]; break; }  // gilde.exe 0x45d93b mov ecx,eax
                        }
                    }
                    // gilde.exe 0x45d94c: if (!v11) return (NO ++passIndex)
                    if (!freeSlot) return;

                    // gilde.exe 0x45d960: Light_SetGrayColorThunk(0,248,&v55)
                    MeisterCommand cmd = makeCmd();
                    cmd.mode      = 1;            // gilde.exe 0x45d9a7: var_214 = 1
                    // gilde.exe 0x45d9eb: v65 = *(bldgRec+1)
                    cmd.targetId  = getBldgId();
                    // gilde.exe 0x45d9d8: v67 = *v14 = *(i32*)(freeSlot+0)
                    i32 slotKey0; std::memcpy(&slotKey0,
                                  reinterpret_cast<u8*>(freeSlot), 4);
                    cmd.srcId     = slotKey0;
                    // gilde.exe 0x45d9ff: v66 = dword_12CE914[v87/4] = g_personIds[pIdx]
                    cmd.extra0    = (pIdx < kPersonCapacity) ? g_personIds[pIdx] : 0;
                    // gilde.exe 0x45da0d: QueueRequestSlotReset28(&v55, v14)
                    if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                    ++args->already;  // gilde.exe 0x45da12
                }

                // gilde.exe 0x45da15..0x45da41: tail. v87+=536;
                //   if (already >= v86) → LABEL_21 (++passIndex);
                //   if (v87 >= 411648) { ++passIndex; return; }
                v87 += (unsigned int)kPersonStride;
                if (args->already >= v86) goto dispatch_done;
                if (v87 >= (unsigned int)(kPersonCapacity * kPersonStride)) {
                    ++args->passIndex;  // gilde.exe 0x45da47
                    return;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // MODE 20 — farmhand/production dispatch (no He scan, direct WO)
    // gilde.exe 0x45d634..0x45d7b1
    // -------------------------------------------------------------------------
    if (mode == 20) {
        // gilde.exe 0x45d644: count distribution
        int v83 = DispatchOrderCount(args->divisor, args->total, args->passIndex, args->already);

        // gilde.exe 0x45d681: Light_SetGrayColorThunk(0,248,v42)
        MeisterCommand cmd = makeCmd();
        // gilde.exe 0x45d700: v53 = *(WORD*)a1[2] (the order type word)
        i16 orderTypeWord = 0;
        if (orderRow) std::memcpy(&orderTypeWord, orderRow, 2);
        // gilde.exe 0x45d719: v54 = *(DWORD*)(FindWorkProductObject(*(bldgRec))+1)
        // FindWorkProductObject is Building3_FindWorkProductObject or via leaf.
        // In our reimpl: the WO row's item-type id. Not exposed via leaf directly.
        // DOCUMENTED GAP: VIBE_Building_FindWorkProductObject (0x587674) — not in
        // g_meisterLeaves. The cmd.extra1 field gets *(product+1). We set 0 when unavailable.
        cmd.extra1 = 0;  // FindWorkProductObject result not available without bridge

        // gilde.exe 0x45d72a: if (v83 > a1[8])
        if (v83 > args->already) {
            // gilde.exe 0x45d730: v5=0; do { ... v5+=536; } while (a1[8]<v83 && v5<411648)
            unsigned int v5 = 0;
            do {
                int pIdx = (int)(v5 / (unsigned int)kPersonStride);
                if (personQualifies(pIdx)) {
                    // gilde.exe 0x45d788: v51[v45++] = dword_12CE914[v5/4]
                    cmd.workerIds.push_back(pIdx < kPersonCapacity ? g_personIds[pIdx] : -1);
                    ++args->already;  // gilde.exe 0x45d796
                }
                v5 += (unsigned int)kPersonStride;
            } while (args->already < v83 && v5 < (unsigned int)(kPersonCapacity * kPersonStride));
        }
        // gilde.exe 0x45d7b3: LOBYTE(j) = v45 (worker count); v52 = v45
        // gilde.exe 0x45d7d5: if (!v45 || !v53) goto LABEL_21
        if (!cmd.workerIds.empty() && orderTypeWord != 0) {
            // gilde.exe 0x45d7de: QueueRequestSlotReset28(v42, v4)
            if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
        }
        goto dispatch_done;
    }

    // -------------------------------------------------------------------------
    // MODE 21 — granary/mill dispatch (QueryFind on scene root + direct worker scan)
    // gilde.exe 0x45daaa..0x45dc7b
    // -------------------------------------------------------------------------
    if (mode == 21) {
        // gilde.exe 0x45daaa: count distribution
        int v81 = DispatchOrderCount(args->divisor, args->total, args->passIndex, args->already);

        // gilde.exe 0x45daf8: QueryFind(bldgRec+93, 2, 6, 0, 221) — find type-221 node.
        // CRITICAL: the QueryFind RESULT (returned in ecx/v4 at 0x45db00 "mov ecx,eax")
        // is the source of the product id below — it is NOT a1[4]/orderRow4 (an earlier
        // wave wrongly read *(orderRow4+2)). The result node's +2 dword is the bldg id.
        u8* qfNode221 = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->queryFind && bldgRec) {
            i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93);
            const int filts221[] = {6, 0, 221};  // op6, op0 type==221
            qfNode221 = g_meisterLeaves->queryFind(sceneRootId, filts221, 3);  // = v4/ecx
        }

        // gilde.exe 0x45db09: Light_SetGrayColorThunk(0,248,v68)
        MeisterCommand cmd = makeCmd();
        // gilde.exe 0x45db84: v79 = *(WORD*)a1[2]
        i16 orderTypeW21 = 0;
        if (orderRow) std::memcpy(&orderTypeW21, orderRow, 2);

        // gilde.exe 0x45db8c: if (v4) v17 = *(DWORD*)(v4+2);  (v4 = QueryFind result)
        //                     else { wpo = FindWorkProductObject(bldgRec);
        //                            if(!wpo){ v80=-1; goto LABEL_59; } v17 = *(wpo+1); }
        // gilde.exe 0x45db97: v80 = v17
        // FindWorkProductObject (0x587674) is NOT in g_meisterLeaves → DOCUMENTED GAP:
        // when the QueryFind node is null we fall back to -1 (the not-found branch).
        i32 extraBldgId21;
        if (qfNode221) {
            std::memcpy(&extraBldgId21, qfNode221 + 2, 4);  // *(DWORD*)(v4+2)
        } else {
            extraBldgId21 = -1;  // FindWorkProductObject gap → not-found path (v80=-1)
        }
        cmd.extra1 = extraBldgId21;

        // gilde.exe 0x45dba8: if (v81 > a1[8])
        if (v81 > args->already) {
            unsigned int v18 = 0;
            do {
                int pIdx = (int)(v18 / (unsigned int)kPersonStride);
                if (personQualifies(pIdx)) {
                    // gilde.exe 0x45dc04: v77[v71++] = dword_12CE914[v18/4]
                    cmd.workerIds.push_back(pIdx < kPersonCapacity ? g_personIds[pIdx] : -1);
                    ++args->already;
                }
                v18 += (unsigned int)kPersonStride;
            } while (args->already < v81 && v18 < (unsigned int)(kPersonCapacity * kPersonStride));
        }
        // gilde.exe 0x45dc2f: LOBYTE(j) = v71; v78 = v71
        // gilde.exe 0x45dc55: if (!v71 || !v79) goto LABEL_21
        if (!cmd.workerIds.empty() && orderTypeW21 != 0) {
            // gilde.exe 0x45dc5b..0x45dc62: QueueRequestSlotReset28(v68, v4)
            if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
        }
        goto dispatch_done;
    }

    // -------------------------------------------------------------------------
    // MODES 4 / 8 / 3 — combat/espionage dispatch (He scan path)
    // gilde.exe 0x45dcd4..0x45df44
    // -------------------------------------------------------------------------
    {
        // gilde.exe 0x45dcd4: FindFirstHandlerByFilter(2,0,mode,3,bldgId)
        // Scan He handlers for one matching the order type.
        u8* foundHandler = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->heFindFirst && bldgRec) {
            u8* h = g_meisterLeaves->heFindFirst(2, 0, mode, 3, getBldgId());
            while (h) {
                // gilde.exe 0x45dcf1/0x45de1c/0x45de39:
                // Check: *(int*)(h+170)>>16 == *(i16*)a1[2]
                i32 hKeyDw;
                std::memcpy(&hKeyDw, h + 170, 4);
                int hTypeHi = (int)((u32)hKeyDw >> 16);
                i16 orderTypeSigned = 0;
                if (orderRow) std::memcpy(&orderTypeSigned, orderRow, 2);
                if (hTypeHi == (int)(i16)orderTypeSigned) {
                    foundHandler = h;
                    break;
                }
                if (g_meisterLeaves->heFindNext)
                    h = g_meisterLeaves->heFindNext();
                else
                    h = nullptr;
            }
        }

        if (foundHandler) {
            // FOUND handler path — ChangePlayerAction for each eligible worker.
            // gilde.exe 0x45de53..0x45df44
            int v23 = args->divisor;
            int v84  = DispatchOrderCount(v23, args->total, args->passIndex, args->already);

            // gilde.exe 0x45de8b: for modes 8 or 3, QueryFind the scene root for the scene node
            u8* sceneRef = nullptr;
            if ((mode == 8 || mode == 3) && g_meisterLeaves && g_meisterLeaves->queryFind && bldgRec) {
                i32 sceneRoot93 = rd32(bldgRec, kB_sceneRoot93);
                const int filtScene[] = {0, 55};  // op0 type==55
                sceneRef = reinterpret_cast<u8*>(
                    g_meisterLeaves->queryFind(sceneRoot93, filtScene, 2));
            }

            // gilde.exe 0x45dec3: if (v84 > a1[8])
            if (v84 > args->already) {
                unsigned int v24 = 0;
                while (v24 < (unsigned int)(kPersonCapacity * kPersonStride)) {
                    int pIdx = (int)(v24 / (unsigned int)kPersonStride);
                    u8* pBase = pr(pIdx);

                    // gilde.exe 0x45ded3: word_12CE910[v24/2] != -1
                    i16 marker; std::memcpy(&marker, pBase + kP_marker, 2);
                    if (marker != (i16)(-1)) {
                        // gilde.exe 0x45def1: employer==bldgRec && profByte && !busy
                        i32 empH = rd32(pBase, kP_employer);
                        i32 bldgH = rd32(mr, kM_bldgRec);
                        if (empH == bldgH && rd8(pBase, kP_profByte) && !rd32(pBase, kP_busy)) {
                            // gilde.exe 0x45df0d: ChangePlayerAction(bldgRec, sceneRef, foundHandler, ownerWord)
                            i32 bldgRecH = rd32(mr, kM_bldgRec);
                            u16 ownerW = getOwnerWord();
                            if (g_meisterLeaves && g_meisterLeaves->changePlayerAction)
                                g_meisterLeaves->changePlayerAction(bldgRecH,
                                    (int)(std::uintptr_t)sceneRef,
                                    foundHandler, ownerW);
                            ++args->already;  // gilde.exe 0x45df12
                        }
                    }
                    // gilde.exe 0x45df1c: j=a1[8]; v24+=536
                    v24 += (unsigned int)kPersonStride;
                    if (args->already >= v84) goto dispatch_done;
                    if (v24 >= (unsigned int)(kPersonCapacity * kPersonStride)) {
                        // gilde.exe 0x45df33..0x45df44
                        ++args->passIndex;
                        return;
                    }
                }
            }
            goto dispatch_done;
        }

        // NOT FOUND handler path — build new command and emit it.
        // gilde.exe 0x45dd08..0x45de00
        {
            // gilde.exe 0x45dd08: Light_SetGrayColorThunk(0,248,v27)
            MeisterCommand cmd = makeCmd();
            // gilde.exe 0x45dd6e: mode-specific field fills
            if (mode == 4) {
                // gilde.exe 0x45dd76..0x45dd9a
                cmd.targetId  = -1;
                i16 owTWord = 0;
                if (orderRow) std::memcpy(&owTWord, orderRow, 2);
                cmd.srcId = (i32)owTWord;               // v36 = *(WORD*)a1[2]
                cmd.buildingId = getBldgId();            // v40 = *(bldgRec+1)
                if (orderRow3) {
                    i32 v41; std::memcpy(&v41, orderRow3 + 2, 4);
                    cmd.extra0 = v41;                    // v41 = *(a1[3]+2)
                }
            } else if (mode == 8) {
                // gilde.exe 0x45ddaa..0x45ddc5
                i16 owTWord = 0;
                if (orderRow) std::memcpy(&owTWord, orderRow, 2);
                cmd.srcId = (i32)owTWord;
                cmd.buildingId = getBldgId();            // v37 = *(bldgRec+1)
                if (orderRow4) {
                    i32 v38; std::memcpy(&v38, orderRow4 + 2, 4);
                    cmd.extra0 = v38;                    // v38 = *(a1[4]+2)
                }
            } else if (mode == 3) {
                // gilde.exe 0x45ddcd..0x45ddfa
                i16 owTWord = 0;
                if (orderRow) std::memcpy(&owTWord, orderRow, 2);
                cmd.srcId = (i32)owTWord;
                cmd.buildingId = getBldgId();
                if (orderRow3) {
                    i32 v41; std::memcpy(&v41, orderRow3 + 2, 4);
                    cmd.extra0 = v41;
                }
                if (orderRow4) {
                    i32 v40; std::memcpy(&v40, orderRow4 + 2, 4);
                    cmd.targetId = v40;
                }
            }
            // gilde.exe 0x45ddfe..0x45de00: goto LABEL_20 → emit
            if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
        }
    }

dispatch_done:
    // gilde.exe LABEL_21 (0x45d7e3 / 0x45dc7b / 0x45df4e / 0x45df27):
    // ++a1[7]; return
    ++args->passIndex;
}

// ---------------------------------------------------------------------------
// 0x45379c VIBE_Ai_AssignIdleWorkers
// __usercall: eax = workstation node pointer (the type-42 QueryFind result
//             from the calling Calc*Production/Farming function).
//
// Declared as void MeisterAssignIdleWorkers() (no args in header) because the
// caller places the node pointer in eax via __usercall; the reimpl reads it
// from the global g_aiIdleWorkstationNode, which the caller bridge sets before
// calling this function.
//
// For each work-order entry (g_workOrderCount rows, word-stride 44 in orig):
//   if (!dword_B564A4[v3/2] && (word_B564BC[v3] & 8)==0)
//     QueueRequest20(*(workstationNode+2), HIWORD(WO_key))
//
// Where:
//   dword_B564A4[v3/2] = WO effective-stock (kWO_effStock = +0x40 within WO row)
//   word_B564BC[v3]    = g_woBitsTable[i] (flag word)
//   HIWORD(WO_key)     = woItemHi(i) = *(i32*)(WO+2) >> 16
//   *(workstationNode+2) = building id at node+2 (the orig mov eax,[esi+2])
//
// Pure core reused: IdleWorkerShouldQueue(effStock, flags) — returns the predicate.
// gilde.exe 0x45379c
// ---------------------------------------------------------------------------
void MeisterAssignIdleWorkers() {
    // gilde.exe 0x4537a1: v1 = result (esi = eax = workstation node ptr)
    u8* wsNode = g_aiIdleWorkstationNode;

    // gilde.exe 0x4537ad: if (dword_B56FDC > 0)
    if (g_workOrderCount <= 0) return;

    // Building id: *(wsNode+2) — dword read at node offset +2.
    // In the original: "mov eax, [esi+2]" = *(int*)(workstationNode+2).
    // SceneNode has id at +0x02 (see types.h, SceneNode::id @+0x02, i32, unaligned).
    // word_B564BC[v3] stride: v3 advances by 0x58=88 (byte index into word_B564BC array).
    // "test byte ptr word_B564BC[ecx], 8" — byte test, ecx=byte offset from B564BC.
    i32 wsNodeBldgId = 0;
    if (wsNode) std::memcpy(&wsNodeBldgId, wsNode + 2, 4);

    // gilde.exe 0x4537af: ecx=0 (byte stride index into WO table / flag array)
    // loop: v3=0; do { ...predicate...; ++v2; v3+=44; } while (v2 < dword_B56FDC)
    // Note: the disasm shows ecx advancing by 0x58=88, NOT 44. The word_B564BC[ecx]
    // byte-indexes with ecx. dword_B564A4[ecx] dword-indexes with ecx/4 but the
    // disasm shows "cmp dword_B564A4[ecx], 0" — byte index ecx, dword element width.
    // That means dword_B564A4[ecx] = *(i32*)(B564A4+ecx) where ecx advances by 88.
    // B564A4 - B56464 = 0x40 = kWO_effStock. So: *(i32*)(WO_base+0x40+88*i).
    // Confirmed: kWO_effStock = 0x40 within the 88-byte WO record.
    for (int i = 0; i < g_workOrderCount; ++i) {
        // gilde.exe 0x4537b1: cmp dword_B564A4[ecx], 0 (effStock == 0?)
        i32 effStock = rd32(g_workOrderTable + kWorkOrderStride * i, kWO_effStock);

        // gilde.exe 0x4537ba: test byte ptr word_B564BC[ecx], 8
        u16 flags = rdWoBits(i);

        // Pure core: IdleWorkerShouldQueue(effStock, flags)
        // gilde.exe 0x4537c1: jnz short loc_4537D4 (skip if busy or flag set)
        if (!IdleWorkerShouldQueue((int)effStock, flags)) continue;

        // gilde.exe 0x4537c3: mov edx, (dword_B56464+2)[ecx]; sar edx,10h
        // = HIWORD(*(i32*)(g_workOrderTable + 88*i + 2))
        int itemHi = woItemHi(i);

        // gilde.exe 0x4537cf: call VIBE_Command_QueueRequest20(eax=wsNodeBldgId, edx=itemHi)
        if (g_meisterLeaves && g_meisterLeaves->queueRequest20)
            g_meisterLeaves->queueRequest20(wsNodeBldgId, (i16)itemHi);
    }
}

} // namespace guild::sim
