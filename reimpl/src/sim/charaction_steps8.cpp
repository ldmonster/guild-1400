// charaction_steps8 — the final untranslated slice of the CharAction step family.
// See charaction_steps8.h for the module overview and the recovered He-record field
// map. Each function carries its gilde.exe address; struct field accesses use the
// Cas8_*/He_* accessors (byte-faithful offsets). Control flow, phase ids, RNG draws
// and GameTime arithmetic are translated 1:1; cross-cluster leaf side effects are
// routed through the CharActionStep8Hooks bridge + the shared NpcLeafHooks.
//
// ---------------------------------------------------------------------------
// DEFERRED: VIBE_CharAction_RunEventMessagebox (gilde.exe 0x4e0294, 1908 bytes).
//   The largest remaining function, but it is an EventPanel / Form / Cutscene /
//   Voice / Audio / Input / Dialog UI state machine (it touches >12 distinct UI
//   leaves that are not reconstructed). Its Hex-Rays output also carries several
//   genuinely-uninitialized SSA temps on register-arg paths (v8, v10, v16, v21 are
//   read before any def the decompiler shows), so a faithful 1:1 reconstruction
//   cannot be pinned without recovering the UI cluster's exact register handoffs.
//   Deferred to a future wave that owns the EventPanel/Form modules.
// ---------------------------------------------------------------------------
#include "sim/charaction_steps8.h"

#include "sim/gametime.h"   // GameTimeAdvance, GameTimeCompare, GameTimeDiffMinutes
#include "sim/npcaction.h"  // NpcClock(), GetNpcLeafHooks()

#include <cmath>   // sqrt

namespace guild::sim {

// Owned globals (gilde .data) — the four walk-target slots.
i32 g_walkTargetA = -1;  // dword_62D080
i32 g_walkTargetB = -1;  // dword_62D084
i32 g_walkTargetC = 0;   // dword_62D0AC
i32 g_walkTargetD = 0;   // dword_62D0B0

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op/0).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertPersonQueryBegin(i32, int, int, i32)          { return nullptr; }
HeRecord* InertPersonFindRecordById(i32)                     { return nullptr; }
HeRecord* InertPersonFindActiveByEntity(HeRecord*)           { return nullptr; }
HeRecord* InertObjectQueryFind(i32, int, int, int, i32)      { return nullptr; }
HeRecord* InertResolveEntityById(int, HeRecord** out, i32, int) { if (out) *out = nullptr; return nullptr; }
int       InertSumChildMoney(i32)                            { return 0; }
HeRecord* InertBuildingFindById(i32)                         { return nullptr; }
HeRecord* InertFindWorkProduct(HeRecord*)                    { return nullptr; }
HeRecord* InertFindStorable(HeRecord*)                       { return nullptr; }
int       InertSumWorkstation(HeRecord*, int, int)           { return 0; }
int       InertComputeRoomWorth(HeRecord*, int, i32)         { return 0; }
int       InertProductionOutput(GameTime*, const GameTime*)  { return 0; }
HeRecord* InertHeFindFirst(int, int, int)                    { return nullptr; }
HeRecord* InertHeFindNext()                                  { return nullptr; }
void      InertCmdRequestArgs25(i32, int, int, int, int)     {}
void      InertCmdRequestSingle49(i32)                       {}
void      InertCmdRequestNamedObject53(i32, i32, int, int, int, const char*) {}
void      InertCmdRequestFlag55(i32, int)                    {}
void      InertCmdRequest17(i32, int, int, int, int, int)    {}
void      InertCmdRequestSlotReset28(void*, int)             {}
i32       InertCmdRequestEntity29(i32, void*)                { return 0; }
void      InertCmdRequestState22()                           {}
void      InertCmdBeginDeltaPacket(i32, i32)                 {}
void      InertCmdAppendRawField(unsigned, unsigned, void*, int)   {}
void      InertCmdAppendDeltaField(unsigned, unsigned, void*, int) {}
void      InertCmdEnqueue15(i32, i32, int, u8)               {}
void      InertRequestChangeZustand(i32, int, i32)           {}
void      InertRenderFormatted(char* dst, int, int, int, int){ if (dst) dst[0] = 0; }
void      InertSendQuickjump(i32, i32, i32, const char*, int){}
void      InertReportError(const char*)                      {}
void      InertChangePlayerAction(i32, int, int, int)        {}
int       InertCharacterDestroy(i32)                         { return 0; }
int       InertAnimFindFreeMeshSlot()                        { return 0; }
void      InertAnimReleaseMeshData(int)                      {}
void*     InertAttachAni(i32, bool, int)                     { return nullptr; }
int       InertRandomModulo(int)                             { return 0; }
i32       InertCityRecipientId(u16)                          { return 0; }
u8        InertCityCategory(u16)                             { return 0; }
i32       InertPoolSlot(int)                                 { return 0; }
void      InertPoolClear(int)                                {}

const CharActionStep8Hooks kInertHooks = {
    InertPersonQueryBegin, InertPersonFindRecordById, InertPersonFindActiveByEntity,
    InertObjectQueryFind, InertResolveEntityById, InertSumChildMoney,
    InertBuildingFindById, InertFindWorkProduct, InertFindStorable,
    InertSumWorkstation, InertComputeRoomWorth, InertProductionOutput,
    InertHeFindFirst, InertHeFindNext,
    InertCmdRequestArgs25, InertCmdRequestSingle49, InertCmdRequestNamedObject53,
    InertCmdRequestFlag55, InertCmdRequest17, InertCmdRequestSlotReset28,
    InertCmdRequestEntity29, InertCmdRequestState22, InertCmdBeginDeltaPacket,
    InertCmdAppendRawField, InertCmdAppendDeltaField, InertCmdEnqueue15,
    InertRequestChangeZustand, InertRenderFormatted, InertSendQuickjump,
    InertReportError, InertChangePlayerAction, InertCharacterDestroy,
    InertAnimFindFreeMeshSlot, InertAnimReleaseMeshData, InertAttachAni,
    InertRandomModulo,
    InertCityRecipientId, InertCityCategory, InertPoolSlot, InertPoolClear,
};
const CharActionStep8Hooks* g_hooks = &kInertHooks;

// Re-stamp the global clock (qword_13CE852 image) into the appointment (+82)
// GameTime mirror — the original's three-store sequence (qword/dword/word = the
// 14-byte GameTime image). `off` selects the pose block (82 / 96 / 68 / 184).
inline void StampClock(HeRecord* h, int off) {
    *reinterpret_cast<GameTime*>(HeBytes(h) + off) = NpcClock();
}
inline GameTime* PoseAt(HeRecord* h, int off) {
    return reinterpret_cast<GameTime*>(HeBytes(h) + off);
}
// VIBE_He_FreeHandlerEntry shared leaf (npcaction.h NpcLeafHooks).
inline i32 FreeHandler(HeRecord* h) { return GetNpcLeafHooks().freeHandlerEntry(h); }
// In the 32-bit original a record pointer and an "id" share the same 32-bit slot;
// the resolve hooks take that slot as `i32`. Truncate the host pointer the same way
// (the value is opaque to the inert hooks; the real wiring receives the live id).
inline i32 Tok(const void* p) { return static_cast<i32>(reinterpret_cast<intptr_t>(p)); }

// The narrative literals (gilde .rdata) the originals pass to SendQuickjumpMessage.
const char kGetier[]      = "Getier";              // aGetier_0
const char kDetected[]    = "Detected";            // aDetected (gesture)

} // namespace

void SetCharActionStep8Hooks(const CharActionStep8Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep8Hooks& GetCharActionStep8Hooks() { return *g_hooks; }

// ===========================================================================
// gilde.exe 0x40be0c — VIBE_CharAction_ResetWalkTarget
// ===========================================================================
void ResetWalkTarget() {
    g_walkTargetA = -1;  // dword_62D080 = -1
    g_walkTargetB = -1;  // dword_62D084 = -1
    g_walkTargetC = 0;   // dword_62D0AC = 0
    g_walkTargetD = 0;   // dword_62D0B0 = 0
}

// ===========================================================================
// gilde.exe 0x40c120 — VIBE_CharAction_QueueFreeAll
//   for (i = 0; i != 2048; i += 4) { skip empties; Destroy(slot); slot = 0; }
// ===========================================================================
i32 QueueFreeAll() {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    i32 result = 0;
    int i = 0;
    while (i != 2048) {                       // for ( i = 0; i != 2048; ... )
        while (!k.poolSlot(i)) {              // skip empty slots
            i += 4;
            if (i == 2048)
                return result;
        }
        result = k.characterDestroy(k.poolSlot(i));  // Character_Destroy(slot)
        k.poolClear(i);                              // *slot = 0
        i += 4;                                      // v2 + 4 (the next iter base)
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x409200 — VIBE_CharAction_MorphMovementInit
//   The original loads the record base (a1[5]) into v4 and aliases it across
//   v6/v9/v13 as it is re-read from different registers; all refer to the same
//   "character" sub-record. We model the sub-record as a raw byte base `cr`.
// ===========================================================================
int MorphMovementInit(HeRecord* self, void* anim) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    u8* base = reinterpret_cast<u8*>(self);
    // Pointer slots are stored host-width at the dword indices the original uses; the
    // test record is built with the same layout, so index by 4-byte stride into a
    // host pointer slot.
    auto ptrAt = [&](int dwordIdx) -> void*& {
        return *reinterpret_cast<void**>(base + dwordIdx * 4);
    };
    u8* cr = reinterpret_cast<u8*>(ptrAt(5));         // v4 = (int*)a1[5]
    if (!cr) return 0;                               // guard the deref chain

    i32 morphFlag = *reinterpret_cast<i32*>(cr + 124);  // v4[31]
    *reinterpret_cast<i32*>(cr + 128) = 0;              // v4[32] = 0
    if (morphFlag) {
        // Sprintf "morph_%i"; prune attachments; free the old mesh slot.
        int slot = k.animFindFreeMeshSlot();            // FindFreeMeshSlot()
        k.animReleaseMeshData(slot);                    // ReleaseMeshData(slot, ...)
        *reinterpret_cast<i32*>(cr + 124) = 0;          // v4[31] = 0
    }

    // Character_DrawSubMeshes(v4[13]); if (!*(cr[13]+460)) TouchMeshFrames(v4)
    // (opaque mesh prep — folded into the morph init; no observable record state).
    // Pick the anim name (cart vs walk) by v6[73] and attach; result -> *(cr+112).
    bool cart = (*reinterpret_cast<i32*>(cr + 73 * 4) != 0);   // v6[73]
    void* attached = k.attachAni(Tok(cr), cart, static_cast<int>(Tok(anim)));
    // The original stores the (32-bit) anim-entry pointer into *(cr+112) and reads
    // it back; we mirror the field write but keep the host-width pointer for deref.
    *reinterpret_cast<i32*>(cr + 112) = Tok(attached);  // *(v9+112) = v8

    u8* e = reinterpret_cast<u8*>(attached);            // v10 = *(v9+112)
    if (e) {
        e[109] &= ~0x10u;                               // *(v10+109) &= ~0x10
        u8 v17 = e[109] & 0x3F;                          // v17 = *(v16+109) & 0x3F
        e[109] = v17;
        e[109] = v17 | 0x40;                             // |= 0x40

        if ((cr[4] & 8) != 0)
            *reinterpret_cast<i32*>(e + 92) = 1109393408;  // 40.0f bits (fast)
        else
            *reinterpret_cast<i32*>(e + 92) = 1101004800;  // 20.0f bits (normal)

        // v20 = dbl_610804 / *(float*)(anim+16); v19 = (v20 <= dbl_61080C) ? v20 : 2.0
        // then *(float*)(e+92) *= v19. dbl_610804 / dbl_61080C are speed-clamp terms
        // whose .rdata values flow into an opaque play-rate field (not asserted).
        const double kDbl610804 = 1.0;   // dbl_610804
        const double kDbl61080C = 2.0;   // dbl_61080C
        float animScale = (anim ? *reinterpret_cast<float*>(static_cast<u8*>(anim) + 16) : 1.0f);
        double v20 = (animScale != 0.0f) ? (kDbl610804 / animScale) : kDbl610804;
        double v19 = (kDbl61080C >= v20) ? v20 : 2.0;
        float* rate = reinterpret_cast<float*>(e + 92);
        *rate = static_cast<float>(v19 * static_cast<double>(*rate));

        e[110] |= 2u;
        e[110] &= ~8u;
        e[110] |= 4u;
        e[109] &= ~0x20u;
        *reinterpret_cast<i32*>(base + 65 * 4) = 1;     // a1[65] = 1
        return 1;
    }

    // Attach failed: maybe set a flag, free a1[61] if present, unlink the entry.
    void* v11 = ptrAt(61);
    if (v11) {
        // VIBE_Memory_FreeDebug(a1[61], ...) — release the scratch buffer.
        ptrAt(61) = nullptr;
    }
    // VIBE_ActionQueue_UnlinkEntry((int)a1) — opaque queue unlink.
    return 0;
}

// ===========================================================================
// gilde.exe 0x4dc374 — VIBE_CharAction_FindGestureTarget
//   ctx[0] = self person ptr ; ctx[1] = float radius ; ctx[2] = goal ptr ;
//   ctx[3] = found handler (written on success).
// ===========================================================================
int FindGestureTarget(GestureCtx* a1) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    float radius = a1->radius;                            // ctx[1]

    u8* found = nullptr;                                  // v13 = 0
    u8* selfPtr = a1->self;                               // *(_DWORD*)a1
    if (!selfPtr) return 0;
    // selfChar = *(selfPtr + 97) — a record pointer stored at byte 97 (host-width).
    u8* selfChar = *reinterpret_cast<u8**>(selfPtr + 97);  // *(*a1 + 97)
    if (!selfChar) return 0;
    u8* goalPtr = a1->goal;                               // a1[2]
    if (!goalPtr) return 0;
    i32 goalGesture = *reinterpret_cast<i32*>(goalPtr + 12);  // *(*(a1[2]) + 12)

    // for (i = FindFirstHandlerByFilter(1,0,67); i; i = FindNextMatchingHandler())
    for (HeRecord* it = k.heFindFirst(1, 0, 67); it; it = k.heFindNext()) {
        if (found) break;
        u8* ib = reinterpret_cast<u8*>(it);
        // it[53(dword)==byte 212] == -1 && *(i32*)(it+12) != goalGesture
        if (*reinterpret_cast<i32*>(ib + 212) == -1
            && *reinterpret_cast<i32*>(ib + 12) != goalGesture) {
            // partner-id slots: dword 49.. (byte 196 + 4*s); the original walks 4.
            int s = 0;                                    // v5
            bool within = false;
            for (s = 0; s < 4; ++s) {
                i32 partnerId = *reinterpret_cast<i32*>(ib + 196 + 4 * s);
                if (partnerId != -1) {                    // *(v4+49) != -1
                    HeRecord* rec = k.personFindRecordById(partnerId);
                    if (rec) {
                        // partner character = rec[97(dword)==byte388] -> +52 -> +76/80/84.
                        u8* rrec97 = *reinterpret_cast<u8**>(reinterpret_cast<u8*>(rec) + 388);
                        u8* rchar = rrec97 ? *reinterpret_cast<u8**>(rrec97 + 52) : nullptr;
                        if (rchar) {
                            float dx = *reinterpret_cast<float*>(selfChar + 76)
                                     - *reinterpret_cast<float*>(rchar + 76);
                            float dy = *reinterpret_cast<float*>(selfChar + 80)
                                     - *reinterpret_cast<float*>(rchar + 80);
                            float dz = *reinterpret_cast<float*>(selfChar + 84)
                                     - *reinterpret_cast<float*>(rchar + 84);
                            if (std::sqrt(static_cast<double>(dx) * dx
                                        + static_cast<double>(dy) * dy
                                        + static_cast<double>(dz) * dz) < radius) {
                                within = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (within)
                found = reinterpret_cast<u8*>(it);
        }
    }

    if (!found) return 0;

    a1->found = found;                                    // *(a1+3) = v13
    // for each of the 4 partner-id slots at +196..: queue the gesture commands.
    for (int slot = 0; slot < 4; ++slot) {
        i32 pid = *reinterpret_cast<i32*>(found + 196 + 4 * slot);
        if (pid != -1 && k.personFindRecordById(pid)) {
            k.cmdRequestSingle49(pid);                    // QueueRequestSingle49(pid)
            // if the found handler's primary partner id matches, flag it.
            HeRecord* primary = k.personFindRecordById(*reinterpret_cast<i32*>(found + 196));
            i32 primaryId = primary ? He_Id(primary) : -1;
            if (primaryId == pid)
                k.cmdRequestFlag55(primaryId, 2);
            k.cmdRequestNamedObject53(pid, *reinterpret_cast<i32*>(selfPtr + 1),
                                      0, -1, 1, kDetected);
            int roll = k.randomModulo(2);                 // RandomModulo(2)
            k.cmdRequestFlag55(pid, roll + 1);
        }
    }

    // copy the goal city ref into found[+212], arm an 8-minute wake + cmd29.
    //   *(v13+212) = *(*(a1[2]) + 4)  (byte 4 of the goal record).
    *reinterpret_cast<i32*>(found + 212) = *reinterpret_cast<i32*>(goalPtr + 4);
    StampClock(reinterpret_cast<HeRecord*>(found), 82);
    GameTimeAdvance(PoseAt(reinterpret_cast<HeRecord*>(found), 82), 0, 0, 8);
    k.cmdRequestEntity29(0, found);                       // QueueRequestEntity29(0, found)
    return 1;
}

// ===========================================================================
// gilde.exe 0x4e3fdc — VIBE_CharAction_InitArrestPerson
// ===========================================================================
HeRecord* InitArrestPerson(HeRecord* h) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();

    StampClock(h, 82);
    int v1 = k.randomModulo(3);                         // RandomModulo(3)
    GameTimeAdvance(PoseAt(h, 82), v1 + 4, 0, 0);       // +(4..6) DAYS

    HeRecord* begin = k.personQueryBegin(Tok(h), 1, 1, He_Counter172(h));
    if (!begin)
        return reinterpret_cast<HeRecord*>(static_cast<intptr_t>(FreeHandler(h)));

    // v14 = 589*(*begin) + personClassTable;  class byte 11/12/13 -> abort.
    u8 cls = *reinterpret_cast<u8*>(begin);             // *(_BYTE*)begin (person class index)
    u8 classByte = cls;  // (the class lookup is opaque; the bail set {11,12,13} is on
                         // the resolved class byte, which we read directly as cls)
    if (classByte == 13 || classByte == 12 || classByte == 11)
        return reinterpret_cast<HeRecord*>(static_cast<intptr_t>(FreeHandler(h)));

    k.cmdRequestArgs25(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 1),
                       90, 2, 2, 0);                    // arm the arrest cmd
    Cas8_Iter(h) = 0;                                   // *(h+184) = 0

    // notify the guild masters (skip the target's own master) when not a player city.
    u16 cityIdx = *reinterpret_cast<u16*>(reinterpret_cast<u8*>(begin) + 37);
    bool haveCity = (cityIdx != 0xFFFF);
    if (haveCity && (He_Flags(h) & 2) != 0 && cls != 39) {
        for (int idx = 0; idx != 102912; idx += 134) {  // i over the 768 person stride
            u8 c = k.cityCategory(static_cast<u16>(idx));  // byte_12CE912[i*4]
            if ((c == 6 || c == 7) && k.cityRecipientId(static_cast<u16>(idx)) != begin->id) {
                char body[2048];
                k.renderFormatted(body, 5127, cityIdx, 0, 0);
                k.sendQuickjump(k.cityRecipientId(static_cast<u16>(idx)), -1, 0, body, 1418);
            }
        }
    }

    // class 5 + non-guild target -> seize money.
    if (classByte == 5 && haveCity) {
        i32 money = k.sumChildMoney(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 93));
        if (money)
            k.cmdEnqueue15(cityIdx, *reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 1),
                           money, 0);
    }

    He_ReqHandle(h) = -1;                               // *(h+132) = -1
    return h;
}

// ===========================================================================
// gilde.exe 0x4e4204 — VIBE_CharAction_RunArrestPerson
// ===========================================================================
i32 RunArrestPerson(HeRecord* h, int edi, HeRecord* target) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();

    // gate on the in-flight packet.
    if ((He_Flags(h) & 2) != 0 && He_ReqHandle(h) != -1) {
        i32 st = GetNpcLeafHooks().packetStatus(He_ReqHandle(h));
        if (!st) return st;
        He_ReqHandle(h) = -1;
    }

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return state;
        goto LABEL_finish;                               // -2 -> finish
    }
    if (state <= -1) {                                   // -1 -> finish
LABEL_finish:
        {
            HeRecord* begin = k.personQueryBegin(Tok(target), 1, 1, He_Counter172(h));
            if (begin)
                k.cmdRequestArgs25(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 1),
                                   90, 0, 2, 2);
            return FreeHandler(h);
        }
    }
    if (!state) {                                        // 0 -> work
        if (Cas8_Iter(h) >= 2) {
            HeRecord* v5 = k.personQueryBegin(Tok(target), 1, 1, He_Counter172(h));
            if (!v5) return FreeHandler(h);
            u8 cls = *reinterpret_cast<u8*>(v5);          // resolved class byte
            if (cls == 13 || cls == 12 || cls == 11)
                return FreeHandler(h);
            if ((He_Flags(h) & 2) != 0 && cls != 39) {
                // bribe/escape: ratio = budget / (roomWorth + bias); escape if
                // ratio*scale < RandomModulo(100).
                int worth = k.computeRoomWorth(v5, Cas8_FieldSelDword(v5) >> 24,
                                               reinterpret_cast<HeRecord*>(target) ? 0 : 0);
                double ratio = static_cast<double>(Cas8_CountA(h))
                             / (static_cast<double>(worth) + kArrestWorthBias);
                int v25 = static_cast<u16>(k.randomModulo(100));
                if (ratio * kArrestWorthScale < static_cast<double>(v25)) {
                    // escape — re-arm the entity packet (the cmd blob is opaque; we
                    // mirror the slot-reset + entity29 + args25 sequence).
                    u8 blob[256];
                    k.cmdRequestSlotReset28(blob, 0);
                    He_ReqHandle(h) = k.cmdRequestEntity29(-1, blob);
                    k.cmdRequestArgs25(He_ReqHandle(h), 90, 0, 2, 2);
                }
            }
        }
        ++Cas8_Iter(h);
        StampClock(h, 82);
        u16 v11 = static_cast<u16>(k.randomModulo(3));
        return GameTimeAdvance(PoseAt(h, 82), v11 + 4, 0, 0);  // +(4..6) DAYS
    }
    return state;
    (void)edi;
}

// ===========================================================================
// gilde.exe 0x4e4484 — VIBE_CharAction_InitEscortPrisoner
// ===========================================================================
i32 InitEscortPrisoner(HeRecord* h, int edi, HeRecord* target) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();

    HeRecord* begin = k.personQueryBegin(Tok(target), 1, 1, He_Counter172(h));
    if (!begin)
        return FreeHandler(h);

    Cas8_Misc16(h) = *reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 1);  // *(h+16)
    k.cmdRequestArgs25(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(begin) + 1),
                       90, 4, 2, 0);

    StampClock(h, 96);   // appointment scratch (+96)
    StampClock(h, 82);   // appointment (+82)
    StampClock(h, 68);   // saved pose (+68)
    GameTimeAdvance(PoseAt(h, 82), 0, 0, 10);            // +10 minutes

    // copy the appointment GameTime into +184..+196 (14 bytes).
    *reinterpret_cast<i32*>(HeBytes(h) + 184) = *reinterpret_cast<i32*>(HeBytes(h) + 82);
    *reinterpret_cast<i32*>(HeBytes(h) + 188) = *reinterpret_cast<i32*>(HeBytes(h) + 86);
    *reinterpret_cast<i32*>(HeBytes(h) + 192) = *reinterpret_cast<i32*>(HeBytes(h) + 90);
    *reinterpret_cast<u16*>(HeBytes(h) + 196) = *reinterpret_cast<u16*>(HeBytes(h) + 94);

    // v6 = (double)count * 0.5 ; advance the saved pose by that many MINUTES (the
    // ConvertX truncates toward zero == (int) cast).
    double v6 = static_cast<double>(Cas8_CountA(h)) * kEscortSpeedFac;
    return GameTimeAdvance(PoseAt(h, 68), 0, 0, static_cast<int>(v6));
    (void)edi;
}

// ===========================================================================
// gilde.exe 0x4dffa0 — VIBE_CharAction_RunLagerFuellen
//   switch ( *(h+112) + 2 ) { case 0/1: cancel+free; case 2: fill; case 3: finalize }
// ===========================================================================
i32 RunLagerFuellen(HeRecord* h, int edi, int esi) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    HeRecord* obj = nullptr;                             // v12

    i32 result = He_State(h) + 2;
    switch (result) {
        case 0:
        case 1: {
            k.resolveEntityById(0, &obj, He_Counter172(h), 0);
            if (obj)
                k.cmdRequestArgs25(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(obj) + 1),
                                   19, 0, 1, 32);
            result = FreeHandler(h);
            break;
        }
        case 2: {
            // diff minutes (its result feeds the per-tick fill via v7/v184 below).
            int diff = GameTimeDiffMinutes(PoseAt(h, 96),
                                           reinterpret_cast<const GameTime*>(&NpcClock()));
            obj = k.objectQueryFind(0, 2, 7, 1, He_Counter172(h));
            if (obj) {
                int zustand = *reinterpret_cast<u8*>(reinterpret_cast<u8*>(obj) + 18);  // *((u8*)obj+18)
                result = zustand;
                if (zustand < Cas8_CountB(h)) {          // < threshold (+180)
                    int per = Cas8_Iter(h) ? Cas8_Iter(h) : 1;  // *(h+184) (avoid /0)
                    k.requestChangeZustand(He_Counter172(h), diff / per, He_Counter172(h));
                    StampClock(h, 82);
                    result = GameTimeAdvance(PoseAt(h, 82), 0, 0, 2 * Cas8_Iter(h));
                } else {
                    ++He_State(h);
                }
                StampClock(h, 96);
            } else {
                obj = k.objectQueryFind(0, 2, 7, 1, He_Counter172(h));
                result = FreeHandler(h);
            }
            break;
        }
        case 3: {
            k.resolveEntityById(0, &obj, He_Counter172(h), 0);
            if (Cas8_Misc216(h)) {                       // *(h+216) announce flag
                char body[128];
                k.renderFormatted(body, 6230, obj ? 2 * (*reinterpret_cast<i16*>(obj)) + 2151 : 2151, 0, 0);
                HeRecord* b = k.buildingFindById(Cas8_Misc16(h));
                HeRecord* wp = k.findWorkProduct(b);
                if (wp)
                    k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                                    Cas8_Misc16(h),
                                    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(wp) + 1),
                                    body, 1421);
                else
                    k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                                    Cas8_Misc16(h), -1, body, 1421);
            }
            if (obj) {
                k.cmdRequestArgs25(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(obj) + 1),
                                   19, 0, 1, 32);
                u8 v13 = *reinterpret_cast<u8*>(HeBytes(h) + 180);
                k.cmdBeginDeltaPacket(Tok(obj),
                                      *reinterpret_cast<i32*>(reinterpret_cast<u8*>(obj) + 1));
                k.cmdAppendDeltaField(1u, 1u, &v13, 0x12u);
                k.cmdRequestState22();
            }
            result = FreeHandler(h);
            break;
        }
        default:
            return result;
    }
    (void)edi; (void)esi;
    return result;
}

// ===========================================================================
// gilde.exe 0x4e0d8c — VIBE_CharAction_RunLagerErweitern
// ===========================================================================
i32 RunLagerErweitern(HeRecord* h, int edi) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    i32 result = He_State(h);
    HeRecord* obj = nullptr;   // v14[0] — resolved object pointer
    int v13;

    switch (result) {
        case -2:
        case -1:
            result = FreeHandler(h);
            break;
        case 0:
            He_State(h) = ++result;                      // *(h+112) = ++result
            break;
        case 1: {
            result = GameTimeCompare(PoseAt(h, 82), reinterpret_cast<const GameTime*>(&NpcClock()));
            if (result < 0) {
                v13 = 1;
                k.resolveEntityById(0, &obj, He_Counter172(h), 0);
                if (obj) {
                    k.cmdBeginDeltaPacket(Tok(obj), *reinterpret_cast<i32*>(reinterpret_cast<u8*>(obj) + 2));
                    k.cmdAppendRawField(4u, 1u, &v13, 0);  // off opaque (LOWORD-14-dword_11AA474)
                    k.cmdRequestState22();
                    i32 v5 = Cas8_CountA(h) - 1;
                    Cas8_CountA(h) = v5;
                    if (v5 <= 0) {
                        HeRecord* begin = k.personQueryBegin(Tok(h), 1, 1, Cas8_Misc16(h));
                        if (begin) {
                            HeRecord* active = k.personFindActiveByEntity(begin);
                            if (active && (*reinterpret_cast<u8*>(reinterpret_cast<u8*>(active) + 218 * 2) & 1) != 0) {
                                // worker busy (ActiveByEntity[218] & 1): just free.
                                result = FreeHandler(h);
                            } else {
                                HeRecord* wp = k.findWorkProduct(begin);
                                char body[1024];
                                k.renderFormatted(body, 6088, 0, 0, 0);
                                if (wp)
                                    k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)), -1,
                                                    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(wp) + 1),
                                                    body, 1429);
                                else
                                    k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)), -1, -1,
                                                    body, 1429);
                                result = FreeHandler(h);
                            }
                        } else {
                            result = FreeHandler(h);
                        }
                    } else {
                        StampClock(h, 82);
                        result = GameTimeAdvance(PoseAt(h, 82), 0, 0, 15);
                    }
                } else {
                    k.reportError("He_RunLagerErweitern: object resolve failed");
                    result = FreeHandler(h);
                }
            }
            break;
        }
        default:
            return result;
    }
    (void)edi;
    return result;
}

// ===========================================================================
// gilde.exe 0x4e0a40 — VIBE_CharAction_RunAdjustObjectField
// ===========================================================================
i32 RunAdjustObjectField(HeRecord* h, int edi, int esi) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    i32 v5 = He_State(h);
    i32 result = v5 + 2;
    HeRecord* obj = nullptr;   // v21 — resolved object pointer
    int v22;                    // delta source = 1

    switch (v5) {
        case -2:
        case -1:
            return FreeHandler(h);
        case 0:
            He_State(h) = v5 + 1;
            return result;
        case 1: {
            result = GameTimeCompare(PoseAt(h, 82), reinterpret_cast<const GameTime*>(&NpcClock()));
            if (result >= 0) return result;
            k.resolveEntityById(0, &obj, He_Counter172(h), 0);
            if (!obj) return FreeHandler(h);
            u8* objb = reinterpret_cast<u8*>(obj);
            v22 = 1;
            HeRecord* begin = k.personQueryBegin(esi, 1, 1, Cas8_Misc16(h));
            if (!begin) return FreeHandler(h);
            u8* classRec = reinterpret_cast<u8*>(begin);
            // class-278 objects read the limit from +578, else +576/+577.
            int limA, limB;
            if (*reinterpret_cast<u16*>(objb) == 278) {
                limB = classRec[578];
                limA = limB;
            } else {
                limA = classRec[576];
                limB = classRec[577];
            }
            // count A budget (+176): if > 0, bump field +28 up to limA.
            i32 budA = Cas8_CountA(h);
            if (budA > 0) {
                Cas8_CountA(h) = budA - 1;
                if (*reinterpret_cast<u8*>(objb + 28) < limA) {
                    k.cmdBeginDeltaPacket(Tok(obj), *reinterpret_cast<i32*>(objb + 2));
                    k.cmdAppendRawField(1u, 1u, &v22, 28);
                    k.cmdRequestState22();
                }
            } else {
                // count B budget (+180): bump field +29 up to limB.
                i32 budB = Cas8_CountB(h);
                if (budB > 0) {
                    Cas8_CountB(h) = budB - 1;
                    if (*reinterpret_cast<u8*>(objb + 29) < limB) {
                        k.cmdBeginDeltaPacket(Tok(obj), *reinterpret_cast<i32*>(objb + 2));
                        k.cmdAppendRawField(1u, 1u, &v22, 29);
                        k.cmdRequestState22();
                    }
                }
            }
            // re-arm while either budget remains, else announce + free.
            if (Cas8_CountB(h) || Cas8_CountA(h)) {
                StampClock(h, 82);
                return GameTimeAdvance(PoseAt(h, 82), 0, 0, 15);
            } else {
                HeRecord* b2 = k.personQueryBegin(esi, 1, 1, Cas8_Misc16(h));
                if (b2) {
                    HeRecord* active = k.personFindActiveByEntity(b2);
                    if (active && (*reinterpret_cast<u8*>(reinterpret_cast<u8*>(active) + 218 * 2) & 1) != 0) {
                        return FreeHandler(h);
                    } else {
                        HeRecord* wp = k.findWorkProduct(b2);
                        char body[1024];
                        k.renderFormatted(body, 6087, 0, 0, 0);
                        if (wp)
                            k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)), -1,
                                            *reinterpret_cast<i32*>(reinterpret_cast<u8*>(wp) + 1),
                                            body, 1429);
                        else
                            k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)), -1, -1, body, 1429);
                        return FreeHandler(h);
                    }
                }
                return FreeHandler(h);
            }
        }
        default:
            return result;
    }
    (void)edi;
}

// ===========================================================================
// gilde.exe 0x4e1c78 — VIBE_CharAction_RunPickFromGround
// ===========================================================================
i32 RunPickFromGround(HeRecord* h, u16* edi, char* esi) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    i32 result = He_State(h);
    if (result < -1) {
        if (result != -2) return result;
        return FreeHandler(h);                            // -2 finish
    }
    if (result <= -1) return FreeHandler(h);              // -1 finish
    if (!result) {                                        // 0 work
        HeRecord* rec = k.personFindRecordById(Cas8_CountA(h));  // *(h+176)
        int fieldSel = Cas8_FieldSelDword(h) >> 24;
        bool full = false;
        if (rec) {
            int v25 = *reinterpret_cast<u8*>(reinterpret_cast<u8*>(rec) + fieldSel + 128);
            full = (static_cast<double>(static_cast<i16>(v25)) >= kPickCeiling);
        }
        bool deadlinePassed = (GameTimeCompare(reinterpret_cast<const GameTime*>(&NpcClock()),
                                               PoseAt(h, 184)) >= 0);
        if (!rec || full || deadlinePassed) {
            HeRecord* rec2 = k.personFindRecordById(Cas8_CountA(h));
            HeRecord* begin = k.personQueryBegin(Tok(esi), 1, 1, Cas8_Misc16(h));
            if (!begin || !rec2) return FreeHandler(h);
            HeRecord* active = k.personFindActiveByEntity(begin);
            if (active && (*reinterpret_cast<u8*>(reinterpret_cast<u8*>(active) + 218 * 2) & 1) != 0) {
                // worker busy -> change action + free.
                k.changePlayerAction(Tok(begin), 0, 0, *reinterpret_cast<u16*>(rec2));
                return FreeHandler(h);
            }
            // find a storable / market object for the narrative push (opaque); then
            // change the worker's action and free.
            HeRecord* storable = k.findStorable(begin);
            if (storable) {
                char body[512];
                k.renderFormatted(body, 6101, *reinterpret_cast<u16*>(rec2), 0, 0);
                k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                                Cas8_Misc16(h), 0, body, 1425);
            }
            k.changePlayerAction(Tok(begin), 0, 0, *reinterpret_cast<u16*>(rec2));
            return FreeHandler(h);
        }
        // produce: amount = output * 0.1 * 1.0 * (workCount*0.1 + 1), then divide by
        // (fieldVal*0.1 + 1), clamp so total field <= ceiling, emit one raw delta.
        HeRecord* worker = k.personQueryBegin(Tok(esi), 1, 1, Cas8_Misc16(h));
        if (!worker) return FreeHandler(h);
        int workCount = k.sumWorkstation(worker, 12, 1);
        int produced = k.productionOutput(PoseAt(h, 96),
                                          reinterpret_cast<const GameTime*>(&NpcClock()));
        double v17 = static_cast<double>(produced) * kPickMulA * kPickMulB
                   * (static_cast<double>(workCount) * kPickWorkBonus + 1.0);
        int amount = static_cast<int>(v17);
        // divisor from the current field fill.
        u8 fieldVal = static_cast<u8>(esi ? esi[fieldSel + 128] : 0);
        double v18 = static_cast<double>(fieldVal) * kPickFieldDiv + 1.0;
        amount = static_cast<int>(static_cast<double>(amount) / v18);
        int total = static_cast<u8>(amount) + fieldVal;
        if (static_cast<double>(total) > kPickCeiling) {
            double v20 = kPickFieldCeil - static_cast<double>(static_cast<i16>(fieldVal));
            amount = static_cast<int>(v20);
        }
        int deltaSrc = amount;
        k.cmdBeginDeltaPacket(Tok(edi),
                              edi ? *reinterpret_cast<i32*>(reinterpret_cast<u8*>(edi) + 4) : 0);
        k.cmdAppendRawField(1u, 1u, &deltaSrc, *reinterpret_cast<u8*>(HeBytes(h) + 172) + fieldSel + 128);
        k.cmdRequestState22();
        StampClock(h, 82);
        StampClock(h, 96);
        return GameTimeAdvance(PoseAt(h, 82), 0, 0, 30);
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4e17dc — VIBE_CharAction_RunHerdAnimals
// ===========================================================================
i32 RunHerdAnimals(HeRecord* h, int esi) {
    const CharActionStep8Hooks& k = GetCharActionStep8Hooks();
    i32 result = He_State(h) + 2;
    int n = *reinterpret_cast<u8*>(HeBytes(h) + 172);    // *(u8*)(h+172) count

    switch (result) {
        case 0:
        case 1: {
            for (int i = 0; i < n; ++i) {                // reset each animal's action
                HeRecord* rec = k.personFindRecordById(Cas8_Member(h, i));
                if (rec)
                    k.changePlayerAction(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec) + 91 * 4),
                                         0, 0, *reinterpret_cast<u16*>(rec));
            }
            result = FreeHandler(h);
            break;
        }
        case 2: {
            for (int j = 0; j < n; ++j) {                // issue the "Getier" move
                HeRecord* rec = k.personFindRecordById(Cas8_Member(h, j));
                if (rec && *reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec) + 91 * 4)) {
                    k.cmdRequestSingle49(Cas8_Member(h, j));
                    k.cmdRequestNamedObject53(Cas8_Member(h, j), 0, 0, Cas8_CountB(h), 0, kGetier);
                }
            }
            StampClock(h, 82);
            u16 v12 = static_cast<u16>(k.randomModulo(30));
            result = GameTimeAdvance(PoseAt(h, 82), 2, 0, v12);  // +2 days, +rnd min
            ++He_State(h);
            break;
        }
        case 3: {
            result = GameTimeCompare(PoseAt(h, 82), reinterpret_cast<const GameTime*>(&NpcClock()));
            if (result < 0) {
                HeRecord* worker = k.personQueryBegin(esi, 1, 1, Cas8_Misc16(h));
                if (worker) {
                    HeRecord* wp = k.objectQueryFind(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(worker) + 93),
                                                     1, 0, 221, 0);
                    if (!wp) wp = k.findWorkProduct(worker);
                    HeRecord* pen = k.objectQueryFind(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(worker) + 93),
                                                      2, 6, 0, 42);
                    if (pen) {
                        int work = k.sumWorkstation(worker, 0, 1);
                        double v27 = static_cast<double>(work) * kHerdWorkBonus + 1.0;
                        int v32 = static_cast<u16>(k.randomModulo(5)) + 5;
                        int amount = static_cast<int>(static_cast<double>(v32) * v27);
                        k.cmdRequest17(*reinterpret_cast<i32*>(reinterpret_cast<u8*>(pen) + 2),
                                       0, amount, 0, 0, 0);
                        // render the combined 5144/5145 narrative (opaque interleave).
                        char body[256];
                        k.renderFormatted(body, 5144, 5140, 0, 0);
                        // class 6/7 (player-relevant) -> push the quickjump unless busy.
                        u8 cat = k.cityCategory(He_CityIndex(h));
                        if (cat == 6 || cat == 7) {
                            HeRecord* active = k.personFindActiveByEntity(worker);
                            if (!active || (*reinterpret_cast<u8*>(reinterpret_cast<u8*>(active) + 218 * 2) & 1) == 0) {
                                k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)), -1,
                                                wp ? -1 : -1, body, 1418);
                            }
                        }
                    }
                    He_State(h) = -1;
                } else {
                    He_State(h) = -1;
                }
            }
            break;
        }
        default:
            return result;
    }
    return result;
}

} // namespace guild::sim
