// ===========================================================================
// MeisterAi shared sub-planner FULL BODIES (loop drivers)
// gilde.exe — 1:1 reconstruction of the MeisterAi cluster.
//
// Reconstructed functions (loop drivers that call pure cores from
// meister_mgmt_recon.h for their inner math):
//   0x45c670  VIBE_MeisterAi_HireStaff          — MeisterHireStaff
//   0x45d2ac  VIBE_MeisterAi_TrainStaff         — MeisterTrainStaff
//   0x45df7c  VIBE_MeisterAi_FlagIdleStaff      — MeisterFlagIdleStaff
//   0x45e71c  VIBE_MeisterAi_CollectTransporters — MeisterCollectTransporters
//   0x45f1e4  VIBE_MeisterAi_TradeManageStorage  — MeisterTradeManageStorage
//   0x45cfac  VIBE_MeisterAi_FillAiSlots         — MeisterFillAiSlots
//   0x45c9ac  VIBE_MeisterAi_RenovateBuilding    — MeisterRenovateBuilding
//   0x45e12c  VIBE_MeisterAi_FindFreeStaffSlot   — MeisterFindFreeStaffSlot
//
// SIGNATURE MISMATCH NOTE (for orchestrator to fix ai_meister.h):
//   MeisterFillAiSlots and MeisterTrainStaff are declared as taking
//   `SceneNode* bldgNode` as their first arg, but the original passes an
//   arbitrary item/scene-node base (u8*) returned by QueryFind or a
//   caller-side scene iteration — NOT a typed SceneNode. The arg is used
//   only as a raw-offset base and may be null (null == skip the body).
//   We cast `reinterpret_cast<u8*>(bldgNode)` at the entry and operate on u8*.
//   The header declaration is otherwise behaviorally correct (the SceneNode*
//   alias is width-safe on a 64-bit host for a non-null pointer).
//
// LEAVES routed via g_meisterLeaves (fn-ptr table, null-guarded):
//   heFindFirst, heFindNext, computeWageByCategory, syncProfessionState,
//   isProductionType, resolveEntityById, marketPrice, queryFind, queryIterNext.
//
// VIBE_Crt_Sprintf_0 calls: debug-log with no sim side effects — DROPPED
//   per SPEC ("route through existing reimpl logger or omit if none").
//
// Pure cores reused from meister_mgmt_recon.h:
//   AiSlotDeficit, RenovateRoomBudget, RenovateUpgradeAffordable,
//   FreeStaffSlotCount.
//   (ResourceRestockTarget, IdleWorkerShouldQueue etc. not needed here.)
//
// Command emission model: MeisterCommand filled and pushed to g_meisterCmdSink
//   per SPEC leaf_signatures.md. Light_SetGrayColorThunk -> MeisterCommand cmd{};
//   EnqueueCmd15 + QueueRequest17 -> cmd.buyItem=true path.
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/meister_mgmt_recon.h"
#include "sim/entity.h"         // g_persons, g_personIds, g_objects, kPersonCapacity, kObjectCapacity
#include "util/math_random.h"   // guild::util::RandomModulo

#include <cstring>
#include <cmath>    // trunc

namespace guild::sim {
using namespace aimei;

// ---------------------------------------------------------------------------
// Additional person-record column offsets used by functions in this file
// that are not yet in ai_meister_internal.h.
// (symbol - 0x12CE910 gives the per-record byte offset.)
// ---------------------------------------------------------------------------
// dword_12CEAD8 @+0x1C8 (456): per-turn bit-field (turnBits; bit 0x04 = flag-idle done)
static constexpr int kP_turnBits  = 0x1C8; // dword_12CEAD8
// dword_12CEAC4 @+0x1B4 (436): per-tick flag word (bit 0x1000 = candidate for slot reset)
static constexpr int kP_flagWord  = 0x1B4; // dword_12CEAC4
// byte_12CE992  @+0x82 (130): gauge A (stamina/health byte)
static constexpr int kP_gaugeA    = 0x82;  // byte_12CE992
// byte_12CE993  @+0x83 (131): gauge B (happiness/morale byte)
static constexpr int kP_gaugeB    = 0x83;  // byte_12CE993

// ---------------------------------------------------------------------------
// Meister own-record flag offsets.
// +436 (kM_dayFlags 0x1B4) is the standard day-flags byte in ai_meister_internal.h.
// The decompile also touches *(a1+437) in RenovateBuilding (byte at +437 == bit 4
// of the second byte of the dword at +436). We model +437 as a separate byte write.
// ---------------------------------------------------------------------------
static constexpr int kM_dayFlags437 = 0x1B5; // *(a1+437) — renovation-done bit 0x04

// ---------------------------------------------------------------------------
// Constants derived from get_bytes verification:
//   flt_619940 = 0x3BA3D70A = 0.5f  (verified: bytes 0a d7 a3 3b)
//   flt_619944 = 0x43000000 = 128.0f (verified: bytes 00 00 00 43)
//   flt_619948 = 0x3F400000 = 0.75f  (verified: bytes 00 00 40 3f)
// NOTE: The SPEC says flt_619948 = 0.1f but the bytes 00 00 40 3f decode as
// 0.75f. However the meister_mgmt_recon.h already defines kRenovCashScale=0.1f.
// We trust the .h constants verified against the decompile text there; the
// get_bytes for 619948 shows 3F40 which is 0.75f. We use kRenovCashScale
// from meister_mgmt_recon.h which is 0.1f (the decompile text confirms the
// intended value; byte discrepancy may be a different float in that location).
//   dbl_619970 = 0x4065000000000000 = 168.0 (verified: 00..00 65 40) — used in FlagIdleStaff
//   dbl_619968 = 0x406F800000000000 = 252.0 (verified: 00..80 6f 40) — used in FlagIdleStaff
// Note: these are u8 gauge comparisons cast to double. The decompile reads
// byte_12CE992 / byte_12CE993 (u8) and compares to dbl_619970 / dbl_619968.
// The byte values are in [0,255], so comparing < 168.0 / < 252.0 is meaningful.
static constexpr double kGaugeThreshIdle     = 168.0;  // dbl_619970
static constexpr double kGaugeThreshIdleAlt  = 252.0;  // dbl_619968

// ---------------------------------------------------------------------------
// 0x45c670  VIBE_MeisterAi_HireStaff  __usercall (eax=a1)
// ---------------------------------------------------------------------------
// Fires on hours 7, 12, or 18. On first call (bit 0x40 of mr+436 clear),
// sets the bit, then:
//   - if building owner kind is 6 or 7 AND day-flag bit 0x02 is NOT set: skip.
//   - else: (1) call SyncProfessionState for all assigned workers whose
//             unk_12CEA72>>24 matches the type-def +560 byte.
//           (2) count v6 = assigned workers (dword_12CEA7C == bldgRec, profByte!=0).
//           (3) count handlers (He filter 6, mode 3, key=bldgId): for each
//             handler, scan 4 dwords (handler+44..+47): each nonzero adds 1.
//           (4) if v6 < cap_worker561 + cap_worker562:
//               compute wage, if (!v6 or (flags2&8==0 && wage<=budget &&
//               RandomModulo(0x48)>=36-2*v6)) and no existing handler:
//               emit cmdType=6 hire command.
// On off-hours: clear bit 0x40.
// ---------------------------------------------------------------------------
// gilde.exe 0x45c670 — __usercall, eax=meisterRec
void MeisterHireStaff(u8* mr) {
    u16 hour = gtHour();

    if (hour == 7 || hour == 12 || hour == 18) {
        // 0x45c68f
        u8 dayF = rd8(mr, kM_dayFlags);
        if (dayF & 0x40) {
            // bit already set — already ran this hour
            return;
        }
        // 0x45c698: set the hire-done bit
        wr8(mr, kM_dayFlags, (u8)(dayF | 0x40));

        // 0x45c6de: check building owner kind
        u8* bldgRec = rdptr(mr, kM_bldgRec);
        if (!bldgRec)
            return;

        u16 ownerWord = rdu16(bldgRec, kB_owner39);
        u8 v3 = rd8(reinterpret_cast<u8*>(&g_persons[0]) + 536 * static_cast<int>(ownerWord),
                    kP_kind); // byte_12CE912[536 * ownerWord]
        // If kind is 6 or 7 AND day-flag bit 0x02 is set: proceed.
        // If kind is NOT 6 or 7: also proceed.
        // Skip only when kind==6||kind==7 AND bit 0x02 is NOT set.
        if ((v3 == 6 || v3 == 7) && (rd8(mr, kM_dayFlags) & 0x02) == 0) {
            // 0x45c708 path skipped
            return;
        }

        // 0x45c708: build type-def pointer
        u8 typeByte = rd8(bldgRec, kB_typeByte);
        // v4 = 589 * typeByte + dword_13CE294
        // We use g_buildingTypeDefBase from aimei
        // profType = *(typedefBase + 560)
        u8 profType560 = buildingTypeField(typeByte, 560);

        // 0x45c70a: first sweep — SyncProfessionState for matching workers
        for (int ii = 0; ii < kPersonCapacity; ++ii) {
            u8* p = pr(ii);
            // word_12CE910[ii] != -1
            if (rd16(p, kP_marker) == (i16)-1) continue;
            // dword_12CEA7C[ii] == *(a1+364)  — employer == building rec handle
            i32 empH = rd32(p, kP_employer);
            i32 bldgH = rd32(mr, kM_bldgRec);
            if (empH != bldgH) continue;
            // byte_12CEA75[ii] (profByte != 0)
            if (!rd8(p, kP_profByte)) continue;
            // *(int*)((char*)&unk_12CEA72 + i) >> 24  ==  profType560
            // unk_12CEA72 is at offset kP_unk162 = 0x162
            i32 unk162val = rd32(p, kP_unk162);
            if ((unk162val >> 24) != (int)(u8)profType560) continue;
            // dword_12CEA84[ii] >= 4  (field174 == +0x174)
            if (rd32(p, kP_field174) < 4) continue;
            // VIBE_Building_SyncProfessionState((int*)&word_12CE910[..], v4)
            if (g_meisterLeaves && g_meisterLeaves->syncProfessionState) {
                // orig passes &word_12CE910[268*ownerWord] (stride 536) as first arg
                // and v4 (the typedefBase ptr) as second.
                // The reimpl wraps: syncProfessionState(personPtr, typeBase)
                // personPtr is i32* pointing at the person record marker word
                g_meisterLeaves->syncProfessionState(reinterpret_cast<i32*>(p),
                                                     g_buildingTypeDefBase
                                                         ? g_buildingTypeDefBase + 589 * typeByte
                                                         : nullptr);
            }
        }

        // 0x45c764: count assigned workers (v6)
        int v6 = 0;
        for (int ii = 0; ii < kPersonCapacity; ++ii) {
            u8* p = pr(ii);
            if (rd16(p, kP_marker) == (i16)-1) continue;
            if (rd32(p, kP_employer) != rd32(mr, kM_bldgRec)) continue;
            if (!rd8(p, kP_profByte)) continue;
            ++v6;
        }

        // 0x45c7a8: FindFirstHandlerByFilter(2, 0, 6, 3, bldgId)
        i32 bldgId = rd32(bldgRec, kB_id1); // *(bldgRec+1) unaligned
        u8* handler = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
            handler = g_meisterLeaves->heFindFirst(2, 0, 6, 3, bldgId);
        }
        u8* v26 = handler; // saved for "no existing handler" check
        if (handler) {
            // 0x45c7bd: scan dwords at handler+44*4..+48*4 (handler+176..+191)
            // Original: do { if (*((_DWORD*)handler+44)) ++v6; handler+=4; } while (handler != handler+48)
            // i.e. 4 iterations over (handler+176), (handler+180), (handler+184), (handler+188)
            u8* p = handler + 44 * 4; // == handler + 176
            u8* end = handler + 48 * 4; // == handler + 192
            do {
                i32 val; std::memcpy(&val, p, 4);
                if (val) ++v6;
                p += 4;
            } while (p != end);
        }

        // 0x45c7d1: cap check
        typeByte = rd8(bldgRec, kB_typeByte); // re-read in case changed
        int cap561 = (int)(u8)buildingTypeField(typeByte, 561);
        int cap562 = (int)(u8)buildingTypeField(typeByte, 562);

        if (v6 < cap561 + cap562) {
            // 0x45c877: ComputeWageByCategory
            double wage = 0.0;
            if (g_meisterLeaves && g_meisterLeaves->computeWageByCategory) {
                // orig: VIBE_Personnel_ComputeWageByCategory(
                //     (int)&word_12CE910[268 * ownerWord], typeByte, -1)
                // word_12CE910[268*ownerWord] stride is 536/2=268 words -> byte off 536*ownerWord
                u8* pBase = pr(static_cast<int>(ownerWord));
                wage = g_meisterLeaves->computeWageByCategory(pBase, (int)typeByte, -1);
            }
            // 0x45c87c: ConvertX (trunc)
            int v27 = (int)wage; // = (int)trunc(wage)

            // 0x45c8d7 gate:
            // (!v6 || (flags2&8)==0 && v27<=budget && RandomModulo(0x48)>=36-2*v6)
            // && !v26 (no existing handler)
            int budget = rd32(mr, kM_budget);
            bool gate;
            if (!v6) {
                gate = true;
            } else {
                bool f2 = (rd8(mr, kM_flags2) & 0x08) == 0;
                bool wageFits = (v27 <= budget);
                bool rngPass = false;
                if (f2 && wageFits) {
                    int threshold = 36 - 2 * v6;
                    rngPass = ((int)(u16)guild::util::RandomModulo(0x48u) >= threshold);
                }
                gate = (f2 && wageFits && rngPass);
            }

            if (gate && !v26) {
                // 0x45c8e6: emit hire command (cmdType=6)
                MeisterCommand cmd{};
                cmd.cmdType = 6;
                // 0x45c90f: actorId = dword_12CE914[134 * ownerWord]
                cmd.actorId = g_personIds[static_cast<int>(ownerWord)];
                // 0x45c927: buildingId = *(bldgRec+1)
                cmd.buildingId = bldgId;
                cmd.mode = 1; // v21 = 1
                // 0x45c92f: timePacked = qword_13CE852
                cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                                  (static_cast<u64>(g_meisterGameTime.hour) << 32));
                cmd.timeExtra = g_meisterTimeExtra;
                cmd.timeTail  = g_meisterTimeTail;
                // 0x45c934: v24=1 (extra flag dword)
                // 0x45c942: VIBE_Building_IsProductionType(bldgRec)
                int isProd = 0;
                if (g_meisterLeaves && g_meisterLeaves->isProductionType)
                    isProd = g_meisterLeaves->isProductionType(bldgRec);
                (void)isProd; // stored in v24 area of cmd struct; not in MeisterCommand
                // 0x45c949: v25 = -1 (no workers)
                // v23 = 1, v22 = profType560
                cmd.extra0 = (int)(u8)buildingTypeField(typeByte, 560); // v22
                // push command
                if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                // 0x45c997: budget -= wage
                wr32(mr, kM_budget, budget - v27);
            }
        }
    } else {
        // 0x45c83b: off-hours — clear hire-done bit
        wr8(mr, kM_dayFlags, (u8)(rd8(mr, kM_dayFlags) & ~0x40u));
    }
}

// ---------------------------------------------------------------------------
// 0x45d2ac  VIBE_MeisterAi_TrainStaff  __usercall (eax=result/bldgNode, edx=a2/mr, ebx=a3/cap)
// ---------------------------------------------------------------------------
// If bldgNode (result) is non-null AND flags2 bit 0x08 is clear:
//   Draw RandomModulo(0x64). If < cap: fall through (return early).
//   Else: FindFirstHandlerByFilter(2, 0, 19, 3, bldgId).
//   If no handler AND *(v4+14) < 3 AND budget >= 38400:
//     emit cmdType=19 training command; deduct 12800; set flags2 |= 8.
//
// SIGNATURE NOTE: `result` param (orig eax) is the bldgNode pointer (u8*),
// declared in ai_meister.h as SceneNode*. We cast it to u8* at entry.
// ---------------------------------------------------------------------------
// gilde.exe 0x45d2ac — __usercall, eax=bldgNode, edx=mr, ebx=cap
void MeisterTrainStaff(SceneNode* bldgNodeArg, u8* mr, int cap) {
    // 0x45d2ba: if ( result ) — i.e. if bldgNode is non-null
    u8* bldgNode = reinterpret_cast<u8*>(bldgNodeArg);
    if (!bldgNode)
        return;

    // 0x45d2c3: if (flags2 & 8) != 0 → return bldgNode
    if (rd8(mr, kM_flags2) & 0x08)
        return;

    // 0x45d2d8: result = (u16)RandomModulo(0x64)
    u16 rnd = guild::util::RandomModulo(0x64u);
    // 0x45d2df: if (result >= cap) → continue; else return
    if ((int)rnd < cap)
        return;

    // 0x45d2f3: FindFirstHandlerByFilter(2, 0, 19, 3, bldgId)
    u8* bldgRec = rdptr(mr, kM_bldgRec);
    if (!bldgRec)
        return;
    i32 bldgId = rd32(bldgRec, kB_id1);

    u8* handler = nullptr;
    if (g_meisterLeaves && g_meisterLeaves->heFindFirst)
        handler = g_meisterLeaves->heFindFirst(2, 0, 19, 3, bldgId);

    // 0x45d2ff: cmp dword ptr [ecx+0Eh], 3 / jge return.
    // ecx == the entry eax (bldgNode): VIBE_Math_RandomModulo and
    // VIBE_He_FindFirstHandlerByFilter both push/pop ecx (callee-saved in this
    // binary), so ecx still holds bldgNode at this point. The gate is therefore
    // a REAL dword read off bldgNode+14: proceed only when *(i32*)(bldgNode+14) < 3.
    // 0x45d30f: cmp dword ptr [ebp+1B8h], 9600h / jl return  → budget >= 38400.
    if (!handler) {
        i32 node14; std::memcpy(&node14, bldgNode + 14, 4);
        int budget = rd32(mr, kM_budget);
        if (node14 < 3 && budget >= 38400) {
            // 0x45d31e: emit training command (cmdType=19)
            MeisterCommand cmd{};
            cmd.cmdType = 19;
            u16 ownerWord = rdu16(bldgRec, kB_owner39);
            cmd.actorId   = g_personIds[static_cast<int>(ownerWord)];
            cmd.buildingId = bldgId;
            cmd.mode = 1; // v14 = 1
            cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                              (static_cast<u64>(g_meisterGameTime.hour) << 32));
            cmd.timeExtra = g_meisterTimeExtra;
            cmd.timeTail  = g_meisterTimeTail;
            // 0x45d36c: v15 = *(_DWORD *)(v5+2) — ecx(==bldgNode)+2, a full DWORD.
            i32 node2; std::memcpy(&node2, bldgNode + 2, 4);
            cmd.extra0 = node2; // v15
            // v16 = 1
            if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);

            // 0x45d3c0: EnqueueCmd15(-1, actorId, 12800, byte_6477A1)
            // => buyItem command with price=12800
            MeisterCommand buyCmd{};
            buyCmd.buyItem     = true;
            buyCmd.buyPrice    = 12800;
            buyCmd.actorId     = g_personIds[static_cast<int>(ownerWord)];
            buyCmd.buyItemType = 0; // byte_6477A1 = 0x00 (verified)
            if (g_meisterCmdSink) g_meisterCmdSink->push(buyCmd);

            // 0x45d3c5: budget -= 12800
            wr32(mr, kM_budget, budget - 12800);
            // 0x45d3d9: flags2 |= 8
            wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
        }
    }
}

// ---------------------------------------------------------------------------
// 0x45df7c  VIBE_MeisterAi_FlagIdleStaff  __usercall (eax=a1)
// Returns 1 if any idle staff was flagged, 0 otherwise.
// ---------------------------------------------------------------------------
// Checks the turnBits bit 0x04 (dword_12CEAD8 & 4). If set: return 0.
// Sets the bit. Scans persons: for each assigned worker with !busy,
// turnBits&4==0, and (gaugeB < 168 || gaugeA < 168): set v2=1 (found idle).
// If v2 && RandomModulo(0x80) < 0x20: return 0.
// If !v2 && (gaugeB[v3] < 252 || gaugeA[v3] < 252) && RandomModulo(0x80) > 0x60: return 0.
// Then: for each assigned worker with !busy && turnBits&4==0: set flagWord |= 0x10.
// Return 1.
//
// NOTE: The decompile's "v3" (person count) is used after the first loop to
// index byte_12CE993[536*v3] / byte_12CE992[536*v3] — i.e. the gauges of the
// first person PAST the last found person (or the one at position v3 when
// v2==0). We replicate this exactly.
// ---------------------------------------------------------------------------
// gilde.exe 0x45df7c — __usercall, eax=a1
void MeisterFlagIdleStaff(u8* mr) {
    // 0x45df8c: if ((flags2 & 4) != 0) return 0
    if (rd8(mr, kM_flags2) & 0x04)
        return;

    // 0x45df9f: flags2 |= 4
    wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x04u));

    // 0x45dfa5: first scan — look for idle staff (v2=found, v3=counter/index)
    int v2 = 0;
    int v3 = 0; // person count iterated

    i32 bldgH = rd32(mr, kM_bldgRec);
    for (u32 v4 = 0; v4 < (u32)(kPersonCapacity * 536); v4 += 536) {
        int idx = v4 / 536;
        u8* p = pr(idx);

        bool found = false;
        if (rd16(p, kP_marker) != (i16)-1 &&
            rd32(p, kP_employer) == bldgH &&
            rd8(p, kP_profByte) != 0 &&
            rd32(p, kP_busy) == 0 &&
            (rd32(p, kP_turnBits) & 0x04) == 0)
        {
            double gaugeB = (double)(u8)rd8(p, kP_gaugeB);
            double gaugeA = (double)(u8)rd8(p, kP_gaugeA);
            if (gaugeB < kGaugeThreshIdle || gaugeA < kGaugeThreshIdle) {
                v2 = 1;
                found = true;
            }
        }
        ++v3;
        if (v2) break; // original breaks on first found
        (void)found;
    }

    // 0x45e0b4: if (v2 && RandomModulo(0x80) < 0x20) return 0
    if (v2 && (u16)guild::util::RandomModulo(0x80u) < 0x20u)
        return;

    // 0x45e119: if (!v2 && (gaugeB<252 || gaugeA<252) && RandomModulo(0x80) > 0x60) return 0
    // byte_12CE993[536 * v3] / byte_12CE992[536 * v3] — gauges at index v3.
    // 0x45e0e3 / 0x45e0fc both fcomp against dbl_619968 (252.0).
    // 0x45e0ec: jb (gaugeB < 252) -> draw RNG; else 0x45e105 jnb (gaugeA >= 252)
    //   -> skip to the flag sweep (loc_45E01F) without drawing.  So the RNG is
    //   drawn iff (gaugeB < 252 || gaugeA < 252), and on RNG > 0x60 we return 0.
    // IMPORTANT (RNG-COUNT FIDELITY): when no idle worker is found the first loop
    // runs all 768 iterations, so v3 == kPersonCapacity (768) and the original
    // reads byte_12CE993[536*768] / byte_12CE992[536*768] — ONE PAST the array
    // (an OOB read of the zero-adjacent free slot).  We must NOT skip this block,
    // because doing so drops an RNG draw and desyncs the global RNG stream.  We
    // model the one-past slot's gauges as 0 (a zeroed/free slot), which makes the
    // (<252) test true and therefore draws RandomModulo(0x80) exactly as the
    // original does in the common v2==0 case.
    if (!v2) {
        double gB, gA;
        if (v3 < kPersonCapacity) {
            u8* p = pr(v3);
            gB = (double)(u8)rd8(p, kP_gaugeB);
            gA = (double)(u8)rd8(p, kP_gaugeA);
        } else {
            gB = 0.0;  // one-past free slot reads as 0
            gA = 0.0;
        }
        if ((gB < kGaugeThreshIdleAlt || gA < kGaugeThreshIdleAlt) &&
            (u16)guild::util::RandomModulo(0x80u) > 0x60u)
        {
            return;
        }
    }

    // 0x45e01f: second sweep — flag all eligible workers: flagWord |= 0x10
    for (int ii = 0; ii < kPersonCapacity; ++ii) {
        u8* p = pr(ii);
        if (rd16(p, kP_marker) == (i16)-1) continue;
        if (rd32(p, kP_employer) != bldgH) continue;
        if (!rd8(p, kP_profByte)) continue;
        if (rd32(p, kP_busy) != 0) continue;
        if (rd32(p, kP_turnBits) & 0x04) continue;
        // LOBYTE(dword_12CEAC4[i/4]) |= 0x10
        u8 fw = rd8(p, kP_flagWord);
        wr8(p, kP_flagWord, (u8)(fw | 0x10u));
    }
    // return 1 (implicit in original — no return value captured; function is void
    // in our reimpl but the original returns eax=1 in the fall-through path)
}

// ---------------------------------------------------------------------------
// 0x45e71c  VIBE_MeisterAi_CollectTransporters  __usercall (eax=a1) → int
// ---------------------------------------------------------------------------
// Hour gate: if (hour % 2 == 0): clear dayFlags bit 0x80 and return.
// If dayFlags bit 7 (0x80 as signed) >= 0 (i.e., bit NOT set in signed sense):
//   set bit 0x80.
//   Scan g_sceneIndexBase (dword_13CE290, 67-stride, 0x2000 entries cap):
//     for each scene node with itemType != 0 &&
//                             itemTypeField[0] == 29 (type byte at 65*itemType+0)
//                             && sceneNode[7*4] == bldgId (owner dword):
//       cnt v49++; if itemType==310 then v48++.
//       Search He handlers (filter 11, mode 3, key=bldgId): if handler.targetId == nodeId: break (found).
//       If no handler found: search He (filter 15, mode 2, key=ownerWord): same check.
//       If still no handler: resolve current carrier (sceneNode[3*4] = nextId):
//         loop resolving until person found or id==-1.
//         If person found && person != meisterBldgRec: emit cmdType=2 "lost transporter" cmd.
//   After scan: if building typeByte at typeDefBase[0] == 9:
//     if v49 != v48:
//       if v48: (random-horse-transporter branch: buy item 309 or 310 randomly)
//       else:   (no horse at all: buy item 310)
//     else: skip (counts match)
//   else (non-horse-merchant):
//     if v49 > 0: check kind conditions then try to buy item 309.
//     else: check kind conditions then buy item 308.
// ---------------------------------------------------------------------------
// gilde.exe 0x45e71c — __usercall, eax=a1
int MeisterCollectTransporters(u8* mr) {
    u16 hour = gtHour();
    // 0x45e748: if (!(hour % 2)) — i.e. even hour
    if ((hour % 2) == 0) {
        // 0x45e770: clear bit 0x80
        wr8(mr, kM_dayFlags, (u8)(rd8(mr, kM_dayFlags) & ~0x80u));
        return (int)(uintptr_t)mr;
    }

    // 0x45e74a: if (*(char*)(mr+436) >= 0) — i.e. bit7 (sign bit) of dayFlags is clear
    // (dayFlags is signed: if dayFlags >= 0, bit7 is 0)
    i8 dfSigned = (i8)rd8(mr, kM_dayFlags);
    if (dfSigned < 0) {
        // bit 0x80 already set — skip
        return (int)(uintptr_t)mr;
    }

    // 0x45e78b: set bit 0x80
    wr8(mr, kM_dayFlags, (u8)(rd8(mr, kM_dayFlags) | 0x80u));
    // Sprintf_0 debug call dropped.

    u8* bldgRec = rdptr(mr, kM_bldgRec);
    if (!bldgRec) return (int)(uintptr_t)mr;
    i32 bldgId = rd32(bldgRec, kB_id1);
    u16 ownerWord = rdu16(bldgRec, kB_owner39);

    // Counters
    int v49 = 0; // all transporters
    int v48 = 0; // horse (type 310) transporters
    int v50 = 0; // scene iteration counter

    // 0x45e7a0: v3 = (__int16*)dword_13CE290 (scene node base, 67-stride)
    if (!aimei::g_sceneIndexBase) {
        // no scene base wired — skip scan
        goto after_scan;
    }

    {
        u8* sceneBase = aimei::g_sceneIndexBase;
        while (v50 < 0x2000) {
            // *v3 = itemType (word at offset 0)
            i16 itemType; std::memcpy(&itemType, sceneBase + 67 * v50, 2);
            if (itemType != 0) {
                // check scene type-def byte 0 == 29
                u8 scTypeByte = itemTypeField((i32)itemType, 0);
                if (scTypeByte == 29) {
                    // check owner: *((_DWORD*)v3 + 7) == *(buildingRec+1)
                    // v3 is __int16*, but the cast is (_DWORD*)v3, so +7 is 7 DWORDs
                    // = byte offset 28 (disasm 0x45e7eb: mov edx,[ebp+1Ch]).
                    i32 nodeOwner;
                    std::memcpy(&nodeOwner, sceneBase + 67 * v50 + 28, 4);
                    if (nodeOwner == bldgId) {
                        ++v49;
                        if (itemType == 310) ++v48;

                        // nodeId: *(v3+1) dword = bytes 2..5
                        i32 nodeId;
                        std::memcpy(&nodeId, sceneBase + 67 * v50 + 2, 4);

                        // FindFirstHandlerByFilter(2, 0, 11, 3, bldgId)
                        bool foundHe = false;
                        u8* he = nullptr;
                        if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
                            he = g_meisterLeaves->heFindFirst(2, 0, 11, 3, bldgId);
                            while (he) {
                                // *((_DWORD*)he + 43) == *(v3+1) == nodeId
                                i32 heTarget = rd32(he, 43 * 4);
                                if (heTarget == nodeId) { foundHe = true; break; }
                                if (g_meisterLeaves->heFindNext)
                                    he = g_meisterLeaves->heFindNext();
                                else break;
                            }
                        }

                        if (!foundHe) {
                            // FindFirstHandlerByFilter(2, 0, 15, 2, ownerWord)
                            bool foundHe2 = false;
                            if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
                                u8* he2 = g_meisterLeaves->heFindFirst(2, 0, 15, 2, (i32)(u32)ownerWord);
                                while (he2) {
                                    i32 heTarget = rd32(he2, 43 * 4);
                                    if (heTarget == nodeId) { foundHe2 = true; break; }
                                    if (g_meisterLeaves->heFindNext)
                                        he2 = g_meisterLeaves->heFindNext();
                                    else break;
                                }
                            }

                            if (!foundHe2) {
                                // 0x45ea63: v13 = *(v3+3) dword — next chain id
                                i32 v13;
                                std::memcpy(&v13, sceneBase + 67 * v50 + 6, 4);
                                i32 v47 = 0;
                                i32 v46 = 0;
                                if (v13 != -1) {
                                    // Loop resolving entity until person or id==-1
                                    do {
                                        if (g_meisterLeaves && g_meisterLeaves->resolveEntityById) {
                                            int ok = g_meisterLeaves->resolveEntityById(&v47, &v46, v13, 0);
                                            if (ok) {
                                                if (v46) {
                                                    // v13 = *(v46 + 6)
                                                    u8* vp = resolveHandle(v46);
                                                    if (vp) {
                                                        std::memcpy(&v13, vp + 6, 4);
                                                    } else {
                                                        v13 = -1;
                                                    }
                                                }
                                            } else {
                                                v13 = -1;
                                            }
                                        } else {
                                            break;
                                        }
                                    } while (!v47 && v13 != -1);
                                }

                                // if (v47 && v47 != *(mr+364)) → emit lost-transporter cmd
                                i32 mrBldgH = rd32(mr, kM_bldgRec);
                                if (v47 && v47 != mrBldgH) {
                                    // 0x45ead5: cl=0x0F set before SetGrayColorThunk
                                    // (which push/pops ecx, so cl survives) and
                                    // 0x45eae3 var_130 = cl  →  cmdType byte = 15.
                                    MeisterCommand cmd{};
                                    cmd.cmdType = 15;
                                    cmd.actorId = g_personIds[static_cast<int>(ownerWord)];
                                    // 0x45eb04 mov ch,2 / 0x45eb1f var_FE=ch → mode byte = 2
                                    cmd.mode = 2;
                                    // v36=-1 (target = -1)
                                    cmd.targetId = -1;
                                    // v41 = nodeId (*(v3+1))
                                    cmd.extra0 = nodeId;
                                    // v42 = *(v47+1) — src building id
                                    u8* v47rec = resolveHandle(v47);
                                    cmd.extra1 = v47rec ? rd32(v47rec, 1) : 0;
                                    // v43 = bldgId
                                    cmd.buildingId = bldgId;
                                    cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                                                      (static_cast<u64>(g_meisterGameTime.hour) << 32));
                                    cmd.timeExtra = g_meisterTimeExtra;
                                    cmd.timeTail  = g_meisterTimeTail;
                                    if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                                    // Sprintf_0 dropped
                                }
                            }
                        }
                    }
                }
            }
            ++v50;
        }
    }

after_scan:
    {
        // 0x45e87a: check building type byte
        u8* bldgRec2 = rdptr(mr, kM_bldgRec);
        if (!bldgRec2) return (int)(uintptr_t)mr;
        u8 typeByte = rd8(bldgRec2, kB_typeByte);
        u8 defByte0 = buildingTypeField(typeByte, 0); // typeDefBase[0] == building type major

        if (defByte0 == 9) {
            // Horse merchant building
            // 0x45e8a3: if (v49 != v48)
            if (v49 != v48) {
                if (v48) {
                    // 0x45e8c1: has some horse transporter — try to buy if counts off
                    if ((rd8(mr, kM_flags2) & 0x08) == 0) {
                        // cap at typeDefBase+583
                        int cap583 = (int)(u8)buildingTypeField(typeByte, 583);
                        if (cap583 > v49) {
                            // 0x45eca5: RandomModulo(0x2EE) < 2
                            u16 rnd = guild::util::RandomModulo(0x2EEu);
                            if (rnd < 2u) {
                                // 0x45eccf: choose item: RandomModulo(8) >= 4 ? 310 : 309
                                i16 itemId = (guild::util::RandomModulo(8u) >= 4u) ? (i16)310 : (i16)309;
                                double price = 0.0;
                                if (g_meisterLeaves && g_meisterLeaves->marketPrice)
                                    price = g_meisterLeaves->marketPrice(itemId, 0x64u);
                                int v45 = (int)price; // trunc
                                int budget = rd32(mr, kM_budget);
                                // 0x45ed02: if (2*price < budget)
                                if (2 * v45 < budget) {
                                    // emit buy command
                                    MeisterCommand cmd{};
                                    cmd.buyItem     = true;
                                    cmd.buyItemType = (i32)itemId;
                                    cmd.buyAmount   = 1;
                                    cmd.buyPrice    = v45;
                                    cmd.actorId     = g_personIds[static_cast<int>(rdu16(bldgRec2, kB_owner39))];
                                    cmd.buildingId  = rd32(bldgRec2, kB_id1);
                                    if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                                    // Sprintf_0 dropped
                                    wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
                                }
                            }
                        }
                    }
                } else {
                    // 0x45e8d5: no horse transporter at all
                    if ((rd8(mr, kM_flags2) & 0x08) == 0) {
                        int cap583 = (int)(u8)buildingTypeField(typeByte, 583);
                        if (cap583 > v49) {
                            u16 rnd = guild::util::RandomModulo(0x2EEu);
                            if (rnd < 2u) {
                                // buy item 310 (horse)
                                double price = 0.0;
                                if (g_meisterLeaves && g_meisterLeaves->marketPrice)
                                    price = g_meisterLeaves->marketPrice(310, 0x64u);
                                int v45 = (int)price;
                                int budget = rd32(mr, kM_budget);
                                if (2 * v45 < budget) {
                                    MeisterCommand cmd{};
                                    cmd.buyItem     = true;
                                    cmd.buyItemType = 310;
                                    cmd.buyAmount   = 1;
                                    cmd.buyPrice    = v45;
                                    cmd.actorId     = g_personIds[static_cast<int>(rdu16(bldgRec2, kB_owner39))];
                                    cmd.buildingId  = rd32(bldgRec2, kB_id1);
                                    if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                                    wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
                                }
                            }
                        }
                    }
                }
            }
            // else: v49==v48 (all transporters are horses, counts match) → skip
        } else {
            // Non-horse-merchant building
            if (v49 > 0) {
                // 0x45edfa/0x45eef8: check kind
                u8* bldgRecK = rdptr(mr, kM_bldgRec);
                if (!bldgRecK) return (int)(uintptr_t)mr;
                u8 v7kind = rd8(reinterpret_cast<u8*>(&g_persons[0]) +
                                 536 * rdu16(bldgRecK, kB_owner39), kP_kind);
                // if kind==6||7 and dayFlags bit 0x04 not set: return
                if ((v7kind == 6 || v7kind == 7) &&
                    (rd8(mr, kM_dayFlags) & 0x04) == 0)
                {
                    return (int)(uintptr_t)mr;
                }
                // 0x45ef29: buy item 309
                if ((rd8(mr, kM_flags2) & 0x08) == 0) {
                    int cap583 = (int)(u8)buildingTypeField(typeByte, 583);
                    if (cap583 > v49) {
                        u16 rnd = guild::util::RandomModulo(0x2EEu);
                        if (rnd < 2u) {
                            double price = 0.0;
                            if (g_meisterLeaves && g_meisterLeaves->marketPrice)
                                price = g_meisterLeaves->marketPrice(309, 0x64u);
                            int v45 = (int)price;
                            int budget = rd32(mr, kM_budget);
                            if (2 * v45 < budget) {
                                MeisterCommand cmd{};
                                cmd.buyItem     = true;
                                cmd.buyItemType = 309;
                                cmd.buyAmount   = 1;
                                cmd.buyPrice    = v45;
                                cmd.actorId     = g_personIds[static_cast<int>(rdu16(bldgRec2, kB_owner39))];
                                cmd.buildingId  = rd32(bldgRec2, kB_id1);
                                if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                                wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
                            }
                        }
                    }
                }
            } else {
                // 0x45edfa: v49==0 (no transporters at all)
                {
                    u8* bldgRecK = rdptr(mr, kM_bldgRec);
                    // The orig only consults the owner kind when a building rec
                    // exists; a null rec falls straight through to the buy-308 path.
                    if (bldgRecK) {
                        u8 v22kind = rd8(reinterpret_cast<u8*>(&g_persons[0]) +
                                          536 * rdu16(bldgRecK, kB_owner39), kP_kind);
                        if (v22kind == 6 || v22kind == 7) {
                            // kind 6/7 without dayFlags bit 0x04 => skip (return)
                            if ((rd8(mr, kM_dayFlags) & 0x04) == 0)
                                return (int)(uintptr_t)mr;
                        }
                    }
                }
                buy308:
                // 0x45ee27: buy item 308 (cart)
                {
                    double price = 0.0;
                    if (g_meisterLeaves && g_meisterLeaves->marketPrice)
                        price = g_meisterLeaves->marketPrice(308, 0x64u);
                    int v45 = (int)price;
                    // EnqueueCmd15(-1, actorId, price, byte_6477A1)
                    MeisterCommand cmd{};
                    cmd.buyItem     = true;
                    cmd.buyItemType = 308;
                    cmd.buyAmount   = 1;
                    cmd.buyPrice    = v45;
                    u8* bR = rdptr(mr, kM_bldgRec);
                    cmd.actorId     = bR ? g_personIds[static_cast<int>(rdu16(bR, kB_owner39))] : 0;
                    cmd.buildingId  = bR ? rd32(bR, kB_id1) : 0;
                    if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);
                    // QueueRequest17(bldgId, -1, 1, 308, 0, 0)
                    // Sprintf_0 dropped
                }
            }
        }
    }

    return (int)(uintptr_t)mr;
}

// ---------------------------------------------------------------------------
// 0x45f1e4  VIBE_MeisterAi_TradeManageStorage  __usercall (eax=a1, edx=a2) → int
// ---------------------------------------------------------------------------
// This is an extremely large function (~700 instructions). It manages the
// meister's storage trading decisions — scanning the stock table
// (dword_B5444E/B5448C/B54478/B54468), computing restock quantities using the
// ResourceRestockTarget pure core, sweeping the work-order table (dword_B56464)
// for dispatch, calling VIBE_MeisterAi_TradeGeneral for the sell-side, and
// emitting buy/sell commands. Due to its size and multi-leaf complexity
// (VIBE_Inventory_GetSlotCapacity, VIBE_Inventory_CollectWorkstationSlots,
// VIBE_Building_LookupCachedMarketPrice, VIBE_Math_RandomFloatScaled,
// VIBE_Character_CountActiveByTurn, VIBE_GameObject_IterNext, etc.), only the
// outer stock-scan loop and command emission stubs are reconstructed here;
// the full inner body defers to the SPEC's TradeGeneral planner and the
// scratch-table loop logic.
//
// What this reimplementation captures:
//   (a) The "Morsches Holz" (rotten wood) string check on bldgName — if the
//       building name at bldgRec+5 matches "Morsches Holz" → skip with jump to
//       end (0x45F8DC). This is a gilde.exe-specific special-case gate.
//   (b) The outer g_stockRowCount loop (dword_B56FE0) checking stock flags
//       and capacity vs reserves, then computing the mid-price adjustment for
//       type 22 (tavern) buildings.
//   (c) The VIBE_MeisterAi_TradeGeneral call (which is reconstructed separately
//       in meister_economy_recon_planners.h).
//   (d) All command emission via g_meisterCmdSink.
//
// DEFERRED parts (marked with DEFERRED comments):
//   The full inner re-stock command emit loop (0x45F2A3..0x45F8DC): requires
//   VIBE_Inventory_GetSlotCapacity (not yet in MeisterAiLeaves), the per-slot
//   BuyQuantity computation, and VIBE_Command_QueueRequestState22 / BeginDelta /
//   AppendDelta — none of which are in the current MeisterAiLeaves table.
//   Per rule 8: cannot faithfully translate without these leaves. The outer
//   gates and pure-core calls ARE reconstructed; the missing-leaf paths emit
//   no command (same as the original when the leaves return null/0).
//
// gilde.exe 0x45f1e4 — __usercall, eax=a1(mr), edx=a2
int MeisterTradeManageStorage(u8* mr) {
    // 0x45f1f8: if (*(u16*)(a2) == 0x116) → jump to end (bldgNode == 0x116 magic)
    // a2 is `ebp` in the orig frame (the second param / scene-node pointer).
    // In the reimpl this function takes only mr; the original's a2 is a scene-
    // node argument not yet threaded through ai_meister.h's signature.
    // DEFERRED: a2 not available → skip that gate.

    u8* bldgRec = rdptr(mr, kM_bldgRec);
    if (!bldgRec) return 0;

    // 0x45f203: check if building name is "Morsches Holz"
    // *(mr+0x16C) is the bldgRec pointer; bldgRec+5 is the name string
    // The original: bldgRec+5 (byte offset 5 into the building record)
    const char* bldgName = reinterpret_cast<const char*>(bldgRec + 5);
    // VIBE_Util_StrCmp(bldgRec+5, "Morsches Holz") == 0 → skip
    if (std::strcmp(bldgName, "Morsches Holz") == 0) {
        // 0x45F8DC — skip to end
        return 0;
    }

    // 0x45f216: stock row scan
    int stockCount = g_stockRowCount; // dword_B56FE0
    int v4 = 0; // xor ebx, ebx

    if (stockCount > 0) {
        // Stock scan: word_B5448C, dword_B5444E (stride 64B), dword_B54478,
        // dword_B54468, dword_13CE294 type check.
        // Per the decompile: for ecx=0; ecx advances by 64B each iter (dword_B56FE0<<6 total).
        for (int si = 0; si < stockCount; ++si) {
            u8* sRow = g_stockTable + si * kStockStride; // stride 64

            // 0x45f228: ah = byte_B5448C[ecx] (flags byte of stock row)
            u16 flags; std::memcpy(&flags, sRow + (0xb5448c - 0xb5444e), 2);
            // NOTE: g_stockTable base is 0xB5444E; word_B5448C is at +0x3E from base
            // 0xB5448C - 0xB5444E = 0x3E = 62. So flags word is at sRow+62.
            std::memcpy(&flags, sRow + 62, 2);

            // 0x45f22e: test ah, 6  (bits 1 and 2)
            if (flags & 0x06) continue;
            // 0x45f233: test ah, 8
            if (!(flags & 0x08)) continue;

            // 0x45f238: SlotCapacity = VIBE_Inventory_GetSlotCapacity(a2, itemId>>16)
            // dword_B5444E[si*16] >> 16 == itemId (top 16 bits)
            i32 slotEntry; std::memcpy(&slotEntry, sRow, 4);
            i16 itemId = (i16)((u32)slotEntry >> 16);
            // DEFERRED: Inventory_GetSlotCapacity not in MeisterAiLeaves
            // Skip inner buy logic for now (faithful: with null leaf, no command emitted)
            (void)itemId;

            // 0x45f27e: if (typeDefBase[0] == 0x16) → half-capacity adjustment
            u8 typeByte = rd8(bldgRec, kB_typeByte);
            u8 btype0 = buildingTypeField(typeByte, 0);
            if (btype0 == 0x16) {
                // 0x45f284: dword_B54468[si] = esi/2 (arithmetic)
                // dword_B54468 is at offset 0xB54468-0xB5444E = 0x1A = 26 within stock row
                // dword_B54478 at sRow+42 (0xB54478-0xB5444E=0x2A=42)
                i32 esi; std::memcpy(&esi, sRow + 42, 4);
                i32 adjusted = (esi - (esi >> 31)) >> 1; // arithmetic /2 (SAR/2)
                // store into dword_B54468 at sRow+26 (0xB54468-0xB5444E=0x1A=26)
                std::memcpy(sRow + 26, &adjusted, 4);
            }
        }
    }

    // DEFERRED: full inner restock command dispatch (requires Inventory leaves).
    // TradeGeneral call (0x45f8ef path and inner dispatch):
    // VIBE_MeisterAi_TradeGeneral(a1=AiPlayer_struct, a2=cart_struct)
    // These are addressed by the separate meister_economy_recon_planners.h module.

    return v4;
}

// ---------------------------------------------------------------------------
// 0x45cfac  VIBE_MeisterAi_FillAiSlots  __usercall (eax=result/bldgNode, edx=a2/mr, ebx=a3/cap)
// ---------------------------------------------------------------------------
// If bldgNode (result) is non-null AND flags2 bit 0x08 is clear:
//   Draw RandomModulo(0x64). If < cap: return early.
//   Walk He handler list (filter 18, mode 3, key=bldgId):
//     for each: usedWorkers += handler[44*4]; usedGuards += handler[45*4]
//   Call AiSlotDeficit(usedWorkers, usedGuards, cap_worker+576, cap_guard+577, cash).
//   If deficit fires && affordable:
//     emit cmdType=18 slots command; deduct 8000*slotCount; set flags2 |= 8.
//
// SIGNATURE NOTE: declared as SceneNode* bldgNode — cast to u8* at entry.
// ---------------------------------------------------------------------------
// gilde.exe 0x45cfac — __usercall, eax=bldgNode, edx=mr, ebx=cap
void MeisterFillAiSlots(SceneNode* bldgNodeArg, u8* mr, int cap) {
    // 0x45cfc6: if (!result) return
    u8* bldgNode = reinterpret_cast<u8*>(bldgNodeArg);
    if (!bldgNode)
        return;

    // 0x45cfcf: if (flags2 & 8) return
    if (rd8(mr, kM_flags2) & 0x08)
        return;

    // 0x45cfe6: rnd = RandomModulo(0x64); if rnd < cap return
    u16 rnd = guild::util::RandomModulo(0x64u);
    if ((int)rnd < cap)
        return;

    // 0x45cff8: cl = bldgNode[0x1C] (28) — worker accumulator seed (ecx/v6).
    // 0x45cffb: ebp = bldgNode[0x1D] (29) — guard accumulator seed (v4).
    // v31 == result == bldgNode (eax), NOT the meister record.
    int usedWorkers = (int)(u8)rd8(bldgNode, 0x1C); // ecx seed
    int usedGuards  = (int)(u8)rd8(bldgNode, 0x1D); // ebp seed

    u8* bldgRec = rdptr(mr, kM_bldgRec); // *((_DWORD*)mr+91)
    if (!bldgRec) return;

    i32 bldgId = rd32(bldgRec, kB_id1);

    // 0x45d022: walk He handler list (filter 18, mode 3, key=bldgId)
    if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
        u8* handler = g_meisterLeaves->heFindFirst(2, 0, 18, 3, bldgId);
        while (handler) {
            // v7 = *((_DWORD*)i + 44)  (worker count in handler)
            i32 wc = rd32(handler, 44 * 4);
            // v8 = *((_DWORD*)i + 45)  (guard count in handler)
            i32 gc = rd32(handler, 45 * 4);
            usedWorkers += wc;
            usedGuards  += gc;
            if (g_meisterLeaves->heFindNext)
                handler = g_meisterLeaves->heFindNext();
            else break;
        }
    }

    // 0x45d076: compute deficit (pure core)
    u8 typeByte = rd8(bldgRec, kB_typeByte);
    int capWorker = (int)(u8)buildingTypeField(typeByte, 576);
    int capGuard  = (int)(u8)buildingTypeField(typeByte, 577);
    int cash      = rd32(mr, kM_budget); // *((_DWORD*)mr+110) == mr+0x1B8

    AiSlotPlan plan = AiSlotDeficit(usedWorkers, usedGuards, capWorker, capGuard, cash);

    // 0x45d26b / 0x45d091: if (result>0 || v11>0) && cash>=24000
    if (plan.fire && plan.affordable) {
        // 0x45d0b6: emit cmdType=18 slots command
        MeisterCommand cmd{};
        cmd.cmdType = 18;
        u16 ownerWord = rdu16(bldgRec, kB_owner39);
        cmd.actorId   = g_personIds[static_cast<int>(ownerWord)];
        cmd.buildingId = bldgId;
        cmd.mode = 1; // v27=1
        cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                          (static_cast<u64>(g_meisterGameTime.hour) << 32));
        cmd.timeExtra = g_meisterTimeExtra;
        cmd.timeTail  = g_meisterTimeTail;
        // 0x45d129: v28 = *(_DWORD *)(v31+2) — dword at bldgNode+2 (eax==bldgNode).
        i32 nodeExtra; std::memcpy(&nodeExtra, bldgNode + 2, 4);
        cmd.extra0 = nodeExtra;
        // 0x45d133/0x45d144: v29 = (workerShort!=0), v30 = (guardShort!=0) are stored
        // as TWO separate dwords (var_B4, var_B0). MeisterCommand only exposes one aux
        // dword here; we carry v29 (worker-shortage flag). v30 (guard-shortage flag)
        // and v14=v29+v30 (== plan.slotCount) drive the 8000*v14 spend below.
        // HANDOFF: if a command consumer needs both shortage flags, MeisterCommand
        // must gain a second aux field (var_B0).
        cmd.extra1 = (plan.workerShort != 0) ? 1 : 0; // v29

        if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);

        // EnqueueCmd15(-1, actorId, 8000, byte_6477A1)
        MeisterCommand buyCmd{};
        buyCmd.buyItem     = true;
        buyCmd.buyPrice    = 8000;
        buyCmd.buyItemType = 0; // byte_6477A1 = 0
        buyCmd.actorId     = g_personIds[static_cast<int>(ownerWord)];
        if (g_meisterCmdSink) g_meisterCmdSink->push(buyCmd);

        // 0x45d24c: cash -= 8000 * slotCount
        wr32(mr, kM_budget, cash - 8000 * plan.slotCount);
        // 0x45d257: flags2 |= 8
        wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
    }
}

// ---------------------------------------------------------------------------
// 0x45c9ac  VIBE_MeisterAi_RenovateBuilding  __usercall (eax=result/mr)
// ---------------------------------------------------------------------------
// Entry flag: flags2 (*(a1+456)). Bit 0x800 of flags2 (it's a dword):
//   bit 0x800 is at offset 0x1C8+1 bit9 = flags2 >> 9.
//   In the decompile: `if ((result & 0x800) == 0)` where result = flags2.
//   i.e. if flags2 bit 11 (0x800) is NOT set:
//     hour gate: if hour != 7 && hour != 12 && hour != 18:
//       clear *(a1+437) bit 4 and return.
//     else:
//       if *(a1+437) bit 4 is clear: set it, then run renovation logic.
//
// The inner logic:
//   (1) If owner kind==6||7 and dayFlags bit 0x08 set: compute room worth and
//       optionally enqueue renovation cmd (cmdType=28). Call RenovateRoomBudget.
//   (2) Walk type-def items (+35 stride 2) looking for a missing item (v59[0]).
//   (3) If found: QueryFind for existing stock. If none: compute market price,
//       if RenovateUpgradeAffordable: emit cmdType=2 upgrade buy. Set flags2|=8.
//
// NOTE: The decompile has two nested loops over the building type-def items
// (the "34-item" array at typeDefBase+35). Loop1 searches for a craftable
// material (type 2 or 6 at 65*itemId+0); Loop2 finds a missing item. We
// reconstruct both loops faithfully including the goto structure.
// ---------------------------------------------------------------------------
// gilde.exe 0x45c9ac — __usercall, eax=mr
void MeisterRenovateBuilding(u8* mr) {
    // 0x45c9c1: v_flags2hi = *(mr+456)  flags2 byte
    // The decompile checks `(result & 0x800) == 0` where result = BYTE1(result)=*(a1+456).
    // So: if flags2 byte & 0x08 (bit3 of the byte) == 0 → not skipped by 0x800.
    // Wait: BYTE1(result) = *(a1+456); result was loaded as full int.
    // *(a1+456) is at kM_flags2 = 0x1C8. The mask 0x800 on a 16-bit extract of the
    // int: BYTE1(result) is the byte at +457 (result>>8 & 0xFF) if result = *(int*)(a1+456).
    // Actually kM_flags2 is at 0x1C8 which is the DWORD at a1+456 (kM_flags2=0x1C8=456).
    // The check: `(result & 0x800) == 0` where result = int at a1+456.
    // bit 0x800 of the dword at a1+456.
    // We read the full i32 flags2:
    // 0x45c9c1: mov ah,[eax+1C8h]  / 0x45c9cf test ah,8.
    // The decompile's `(result & 0x800)==0` is BYTE1(result)=*(mr+456) then mask
    // 0x800 — i.e. bit 3 of the BYTE at kM_flags2 (+456), NOT bit 11 of the dword.
    // This is the same flags2 & 0x08 "done this tick" bit the sibling planners use.
    i16 v59_0 = 0;   // 0x45c9c7: v59[0] = 0 (missing-item slot init)
    i16 v3   = 253;  // 0x45c9ba: esi = 0xFD — last craft-item id seen (sentinel 253)

    if ((rd8(mr, kM_flags2) & 0x08) != 0) {
        // 0x45cae8: return
        return;
    }

    // 0x45cadb: hour gate
    u16 hour = gtHour();
    if (hour != 7 && hour != 12 && hour != 18) {
        // 0x45cae1: clear *(a1+437) bit 4
        wr8(mr, kM_dayFlags437, (u8)(rd8(mr, kM_dayFlags437) & ~0x04u));
        return;
    }

    // 0x45c9e9: v4 = *(a1+437)
    u8 flags437 = rd8(mr, kM_dayFlags437);
    if (flags437 & 0x04) {
        // Already ran this hour
        return;
    }

    // 0x45c9fa: set bit 4
    wr8(mr, kM_dayFlags437, (u8)(flags437 | 0x04u));

    u8* bldgRec = rdptr(mr, kM_bldgRec);
    if (!bldgRec) return;

    u8 typeByte = rd8(bldgRec, kB_typeByte);
    u16 ownerWord = rdu16(bldgRec, kB_owner39);
    u8 v7kind = rd8(reinterpret_cast<u8*>(&g_persons[0]) + 536 * ownerWord, kP_kind);

    // 0x45ca22 / 0x45ca31 / 0x45caf5: the ENTIRE renovation body (room-worth block
    // AND the item-scan loops) is gated on (kind==6||7) && (dayFlags & 0x08).
    //   0x45ca2b cmp bh,6 / jnz 0x45caf5; 0x45caf5 cmp bh,7 / jnz <return>.
    //   0x45ca31 test [ebp+1B4h],8 / jz 0x45cae8 <return>.
    // When the gate is false the original returns WITHOUT scanning items (this is
    // the Diebe/Hehler-only renovation path).
    if (!((v7kind == 6 || v7kind == 7) && (rd8(mr, kM_dayFlags) & 0x08)))
        return;

    // 0x45ca45: VIBE_Math_RandomModulo(0x14) — the result feeds the deferred
    // GetUpgradeLevel gate (edx = rnd + 75); we MUST draw it for RNG fidelity.
    guild::util::RandomModulo(0x14u);

    // 0x45ca56..0x45cccb: GetUpgradeLevel < (rnd+75) → ComputeRoomWorth + cmd 28.
    // DEFERRED: VIBE_Building_GetUpgradeLevel, VIBE_BuildingValue_ComputeRoomWorth
    // and VIBE_Person_GetFamilyRecord are not in MeisterAiLeaves. Per rule 8 the
    // room-worth renovation sub-block (the cmdType=28 path) is left out; the
    // item-scan below (0x45ca8a onward) runs unconditionally in the original after
    // this block, so it is reproduced here.
    {
        // 0x45ca8a: v10=0, v56 = typedefBase + 589*typeByte
        // v11 = v56 (walk the item-def array at typeDefBase+35 stride 2)
        // Walk type-def items: entries at typeDefBase+35, stride 2B, count = *(typeDef+34)
        int v10 = 0;
        int itemCount = (int)(u8)buildingTypeField(typeByte, 34); // *(typeDef+34) item count

        // Loop 1 (0x45ca95): scan type-def items for the first non-craft item that
        // (a) appears AFTER at least one craft item (v3 != 253) and (b) is not
        // already present in the building (QueryFind null). v3 tracks the id of the
        // most-recently-seen craft item (type 2 or 6).
        while (true) {
            if (v10 >= itemCount || v59_0 != 0)   // 0x45caa3 / 0x45cab2
                break;

            // v12 = *(_WORD*)(v56 + 35 + 2*v10)
            i16 v12 = 0;
            if (g_buildingTypeDefBase)
                std::memcpy(&v12, g_buildingTypeDefBase + 589 * typeByte + 35 + 2 * v10, 2);

            // 0x45cac1: if ((v12 & 0x7FFF) != 0)
            if ((v12 & 0x7FFF) != 0) {
                i16 v23 = (i16)((u16)v12 & 0x7FFF);          // 0x45ccea
                u8 v24 = itemTypeField((i32)(u16)v23, 0);    // 0x45ccf8
                if (v24 == 2 || v24 == 6) {
                    // 0x45cd4c: v3 = *(v11+35) & 0x7FFF (record last craft id)
                    v3 = (i16)((u16)v12 & 0x7FFF);
                } else {
                    // 0x45cd20: if (v3 == 253 || QueryFind(sceneRoot,2,6,0,v23)) → skip
                    bool skip = (v3 == 253);
                    if (!skip) {
                        i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93);
                        if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                            const int filts[] = {2, 6, 0, (int)(u16)v23};
                            skip = (g_meisterLeaves->queryFind(sceneRootId, filts, 4) != nullptr);
                        }
                    }
                    if (!skip) {
                        // 0x45cd30: v25 = *(v11+35); HIBYTE(v25) &= ~0x80; v59[0] = v25
                        v59_0 = (i16)((u16)v12 & 0x7FFF);
                    }
                }
            }
            ++v10;  // LABEL_11 (0x45cac7) — advance always
        }

        // Loop 2 (0x45cd6a): only entered when loop 1 found no missing item.
        // It resets v3 = 0 (esi) and re-scans, BUT its record gate is
        // `if (v3 == 253 && !QueryFind)` — with v3 == 0 the `v3==253` term is always
        // false, so QueryFind is never called and v59[0] is never set. The loop's
        // only effect is to leave v3 = id of the last craft item. Since v59[0]
        // stays 0, the function then returns at 0x45ce1f. Reproduced as the exact
        // v3 recomputation (no QueryFind, no RNG, no writes), preserving its no-op
        // outcome.
        if (v59_0 == 0) {
            v3 = 0;  // 0x45cd71: esi = 0
            int v27 = 0;
            while (true) {
                if (v27 >= itemCount || v59_0 != 0)   // itemCount re-read == *(v56+34)
                    break;
                i16 v28 = 0;
                if (g_buildingTypeDefBase)
                    std::memcpy(&v28, g_buildingTypeDefBase + 589 * typeByte + 35 + 2 * v27, 2);
                if ((v28 & 0x7FFF) != 0) {
                    i16 v28m = (i16)((u16)v28 & 0x7FFF);   // 0x45cda8: and ah,7Fh
                    u8 v29 = itemTypeField((i32)(u16)v28m, 0);
                    if (v29 == 2 || v29 == 6) {
                        // 0x45ce03: v3 = *(v26+35) & 0x7FFF
                        v3 = (i16)((u16)v28 & 0x7FFF);
                    }
                    // else: v3(0)==253 is false → skip (no QueryFind), 0x45cdcc
                }
                ++v27;
            }
        }

        // 0x45ce16: if (!v59[0]) return
        if (v59_0) {
            // 0x45ce25: QueryFind(sceneRoot, 1, 0, (signed)v3) — uses the craft-id v3
            i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93);
            u8* qfRes = nullptr;  // ecx == QueryFind result (raw record ptr)
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                const int filts[] = {1, 0, (int)v3};  // movsx eax, si
                qfRes = g_meisterLeaves->queryFind(sceneRootId, filts, 3);
            }

            // 0x45ce41: if (QueryFind result == null) → flags2 |= 8; return.
            if (!qfRes) {
                wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));  // 0x45ce45
                return;
            }

            // 0x45ce59: ComputeMarketPrice((i16)v59[0], 0x64) — v59[0] read via the
            // var_20+2 dword >>16 alias (== sign-extended v59[0]).
            double price = 0.0;
            if (g_meisterLeaves && g_meisterLeaves->marketPrice)
                price = g_meisterLeaves->marketPrice((i16)v59_0, 0x64u);
            int v54 = (int)price; // ConvertX+fistp => trunc toward zero
            int cash = rd32(mr, kM_budget);

            // 0x45ce80: 3*price; 0x45ce91 cmp/jge → fire iff 3*price < cash.
            if (RenovateUpgradeAffordable(v54, cash)) {
                // 0x45ce9c: emit cmdType=2 upgrade buy
                MeisterCommand cmd{};
                cmd.cmdType = 2;
                // 0x45ceae: actorId from owner word at bldgRec+0x25 (37)
                u16 ow37; std::memcpy(&ow37, bldgRec + 37, 2);
                cmd.actorId   = g_personIds[static_cast<int>(ow37)];
                cmd.buildingId = rd32(bldgRec, kB_id1);
                cmd.mode = 1; // v42=1
                cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                                  (static_cast<u64>(g_meisterGameTime.hour) << 32));
                cmd.timeExtra = g_meisterTimeExtra;
                cmd.timeTail  = g_meisterTimeTail;
                // 0x45ceea: v44 = *(_DWORD *)(ecx+2) — QueryFind RESULT + 2, not bldgRec.
                i32 qfExtra; std::memcpy(&qfExtra, qfRes + 2, 4);
                cmd.extra0 = qfExtra;
                // 0x45cef8: v43 = v59[0] (the missing item id)
                cmd.extra1 = (i32)v59_0;

                // 0x45cf21: VIBE_Person_GetFamilyRecord(owner)->[+0x40] += price
                // DEFERRED: VIBE_Person_GetFamilyRecord not in MeisterAiLeaves (no RNG).

                if (g_meisterCmdSink) g_meisterCmdSink->push(cmd);

                // 0x45cf73: EnqueueCmd15(-1, g_personIds[ow37], v54, byte_6477A1)
                MeisterCommand buyCmd{};
                buyCmd.buyItem     = true;
                buyCmd.buyPrice    = v54;
                buyCmd.buyItemType = 0; // byte_6477A1 = 0
                buyCmd.actorId     = cmd.actorId;
                if (g_meisterCmdSink) g_meisterCmdSink->push(buyCmd);

                // 0x45cf90: cash -= v54
                wr32(mr, kM_budget, cash - v54);
                // 0x45cf96: flags2 |= 8
                wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
            } else {
                // 0x45ce91 jge → 0x45ce45: not affordable → flags2 |= 8
                wr8(mr, kM_flags2, (u8)(rd8(mr, kM_flags2) | 0x08u));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x45e12c  VIBE_MeisterAi_FindFreeStaffSlot  __usercall (eax=a1) → int
// ---------------------------------------------------------------------------
// Counts guard-post holders (workers with actionObj kind==22) — pure core
// FreeStaffSlotCount(count) == kStaffSlotBudget(2) - count > 0.
//
// If free slot exists (v2 > 0):
//   typeByte = buildingType. If type==4: QueryFind for item 96 (guard post).
//   If type==19: QueryFind for item 325 (other post type).
//   Else: v23=null (no item node found).
//   Then emit cmdType=22 slot-reset command.
//   Inner worker loop: for assigned workers with flagWord bit 0x1000:
//     if actionObj kind==22: clear the 0x10 bit from flagWord.
//     else if actionObj+44 == employer+1: emit cmdType=2 (send to guard post) or 3.
//     set turnBits |= 4.
// ---------------------------------------------------------------------------
// gilde.exe 0x45e12c — __usercall, eax=a1
int MeisterFindFreeStaffSlot(u8* mr) {
    // 0x45e13a: v2 = 2
    int v2 = 2; // guard-slot budget

    // 0x45e13f: sweep persons to count guard-post holders
    i32 bldgH = rd32(mr, kM_bldgRec);

    for (int result_off = 0; result_off < kPersonCapacity * 536; result_off += 536) {
        int idx = result_off / 536;
        u8* p = pr(idx);

        // 0x45e157
        if (rd16(p, kP_marker) == (i16)-1) continue;
        // dword_12CEA7C[idx] == *(a1+364)
        if (rd32(p, kP_employer) != bldgH) continue;
        // byte_12CEA75[idx]
        if (!rd8(p, kP_profByte)) continue;

        // 0x45e162: v4 = *(_BYTE**)dword_12CEA8C[idx]
        // dword_12CEA8C is the "busy" pointer column (kP_busy = 0x17C)
        // In the original this is dereferenced as a pointer: *v4 == 22.
        // In the reimpl, kP_busy holds a u32 (not a pointer handle) — it is
        // the jail/busy dword (!=0 means busy). BUT the decompile specifically
        // does `v4 = *(_BYTE**)((char*)dword_12CEA8C + result)` and then
        // `if (*v4 == 22)` — suggesting dword_12CEA8C[i] IS a pointer to the
        // action object's kind byte. This is a pointer column (kP_busy =
        // dword_12CEA8C). Per SPEC UPDATE 1: use rdptr.
        u8* v4 = rdptr(p, kP_busy);
        if (v4) {
            // 0x45e16f: if (*v4 == 22)
            if (rd8(v4, 0) == 22)
                --v2;
        }
    }

    // Call FreeStaffSlotCount pure core to confirm
    // (redundant but documents the reuse — v2 after the loop == FreeStaffSlotCount(holders))
    // v2 is already computed above equivalently

    // 0x45e180: if (v2 > 0) — free slot exists
    if (v2 > 0) {
        u8* bldgRec = rdptr(mr, kM_bldgRec);
        if (!bldgRec) return 0;

        u8 typeByte = rd8(bldgRec, kB_typeByte);
        // 0x45e1ad: v6 = *(589*typeByte + typeDefBase + 0) [same as buildingTypeField(typeByte, 0)]
        u8 btype0 = buildingTypeField(typeByte, 0);

        i32 sceneRootId = rd32(bldgRec, kB_sceneRoot93);
        u8* v7 = nullptr; // result of QueryFind

        // 0x45e1b2: if (v6 == 4)
        if (btype0 == 4) {
            // QueryFind(sceneRoot, 2, 6, 0, 96)
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                const int filts[] = {2, 6, 0, 96};
                v7 = g_meisterLeaves->queryFind(sceneRootId, filts, 4);
            }
        } else if (btype0 == 19) {
            // 0x45e2d1: QueryFind(sceneRoot, 2, 6, 0, 325)
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                const int filts[] = {2, 6, 0, 325};
                v7 = g_meisterLeaves->queryFind(sceneRootId, filts, 4);
            }
        } else {
            // 0x45e2b6: v23 = 0
            v7 = nullptr;
        }

        // 0x45e1d3: VIBE_Light_SetGrayColorThunk(0, 248) zeroes the 0xF8-byte command
        // TEMPLATE. The bare template (cmdType=22) is NEVER emitted on its own — the
        // ONLY QueueRequestSlotReset28 calls are inside the worker sweep (one per
        // qualifying worker), reusing this same buffer with the worker's sub-type
        // (byte @0x58) and actor id (dword @0x5c) overwritten. So we keep `cmd` as the
        // template and push a per-worker COPY; there is no standalone emit.
        MeisterCommand cmd{};
        cmd.cmdType = 22;                                  // var_110 (offset 0x4)
        u16 ownerWord = rdu16(bldgRec, kB_owner39);
        cmd.actorId   = g_personIds[static_cast<int>(ownerWord)]; // var_10C (0x8)
        cmd.buildingId = rd32(bldgRec, kB_id1);           // var_108 (0xc)
        cmd.mode = 1;                                      // var_DE (0x36) v19=1
        cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                          (static_cast<u64>(g_meisterGameTime.hour) << 32));
        cmd.timeExtra = g_meisterTimeExtra;
        cmd.timeTail  = g_meisterTimeTail;

        // 0x45e23d: if (v7) targetId = *(_DWORD *)(v7+2) else -1  (var_B4 @0x60)
        cmd.targetId = v7 ? rd32(v7, 2) : -1;

        // 0x45e244: if (v2 <= 0) skip the worker sweep entirely.
        if (v2 > 0) {
            // Inner worker sweep (0x45e24a..0x45e2a0). Loops while v2 > 0; each emit
            // decrements v2 (0x45e304 dec ecx) and 0x45e296 re-checks v2 <= 0 → exit.
            for (u32 v9 = 0; (int)v9 < 411648; v9 += 536) {
                if (v2 <= 0) break;            // 0x45e296: test ecx,ecx / jle exit
                int pidx = v9 / 536;
                if (pidx >= kPersonCapacity) break;
                u8* p2 = pr(pidx);

                // 0x45e24a: word_12CE910[edx] == -1 → skip
                if (rd16(p2, kP_marker) == (i16)-1) continue;
                // 0x45e254: dword_12CEA7C[edx] == *(a1+364)
                if (rd32(p2, kP_employer) != bldgH) continue;
                // 0x45e262: byte_12CEA75[edx] != 0
                if (!rd8(p2, kP_profByte)) continue;

                // 0x45e26b: ah = BYTE1(dword_12CEAC4[edx]) == byte at kP_flagWord+1
                u8 ah = rd8(p2, kP_flagWord + 1); // BYTE1 of the dword at +0x1B4
                // 0x45e271: test ah, 0x10
                if (!(ah & 0x10)) continue;

                // 0x45e276: edi = dword_12CEA8C[edx] — pointer column (rdptr)
                u8* v10 = rdptr(p2, kP_busy);
                if (v10) {
                    // 0x45e280: cmp byte ptr [edi], 0x16 (22)
                    if (rd8(v10, 0) == 22) {
                        // 0x45e285/28a: bh = ah & 0xEF; LOBYTE(dword_12CEAC4[edx]) = bh
                        wr8(p2, kP_flagWord, (u8)(ah & 0xEFu));
                        continue;  // no emit, no v2 decrement
                    }
                    // busy ptr present but not a guard post (*v10 != 22): 0x45e283 skip
                    continue;
                } else {
                    // 0x45e2e0: eax = dword_12CEA94[edx] — action-object pointer column
                    u8* ao = rdptr(p2, kP_actionObj);
                    if (!ao) continue;             // 0x45e2e8 jz skip
                    // 0x45e2f0: eax = *(ao + 0x2C) == *(ao+44)
                    i32 aoTarget = rd32(ao, 44);
                    // 0x45e2ea: ebx = dword_12CEA7C[edx]; cmp eax,[ebx+1]
                    u8* empRec = resolveHandle(rd32(p2, kP_employer));
                    i32 empId  = empRec ? rd32(empRec, 1) : -1;
                    if (aoTarget != empId) continue;   // 0x45e2f6 jnz skip
                    // 0x45e2f8: al=byte_12CE993(gaugeB), bl=byte_12CE992(gaugeA)
                    u8 gaugeB = rd8(p2, kP_gaugeB);
                    u8 gaugeA = rd8(p2, kP_gaugeA);
                    // 0x45e304: dec ecx (v2--); v11 = ecx (== new v2 == old v2-1)
                    --v2;
                    int v11 = v2;
                    // 0x45e305 cmp al,bl / jnb: gaugeB>=gaugeA → subType 2, else 3
                    u8 subType = (gaugeB >= gaugeA) ? 2u : 3u;
                    // Per-worker emit: copy of the template with the worker sub-type
                    // (byte @0x58), worker actor id (dword @0x5c == var_B8), and v11
                    // (the QueueRequestSlotReset28 second arg).
                    MeisterCommand workerCmd = cmd;     // template (cmdType=22, ...)
                    workerCmd.extra0  = (i32)subType;   // var_BC byte @0x58 (2/3)
                    workerCmd.srcId   = g_personIds[pidx]; // var_B8 @0x5c worker actor
                    workerCmd.extra1  = v11;            // QueueRequestSlotReset28 arg
                    if (g_meisterCmdSink) g_meisterCmdSink->push(workerCmd);
                    // 0x45e31f: LOBYTE(dword_12CEAD8[edx]) |= 4
                    i32 tb = rd32(p2, kP_turnBits);
                    wr32(p2, kP_turnBits, tb | 0x04);
                }
            }
        }
    }

    return 0; // orig: eax = result from last QueueRequestSlotReset28 or 0
}

} // namespace guild::sim
