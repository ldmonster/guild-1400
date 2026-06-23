// charaction_steps3 — batch 3 of the self-contained CharAction step leaves.
// See charaction_steps3.h for the module overview and the recovered He-record
// field map. Each function carries its gilde.exe address; struct field accesses
// use the Cas3_*/He_* accessors (byte-faithful offsets into the He handler record).
#include "sim/charaction_steps3.h"

#include "sim/gametime.h"     // GameTimeAdvance
#include "sim/npcaction.h"    // NpcClock(), GetNpcLeafHooks()

#include <cstdint>            // std::intptr_t
#include <cstring>            // std::memcpy

namespace guild::sim {

// Byte-exact, alignment-safe load of a 32-bit value at an arbitrary byte offset.
// The original x86 binary uses unaligned `*(int*)(rec + N)` reads off He records
// whose fields are not 4-byte aligned (e.g. +1, +2, +170); binding an `i32&`/`i32*`
// to those addresses is UB in portable C++ (UBSAN flags the misaligned load). This
// reads the identical 4 little-endian bytes without forming a misaligned reference.
namespace {
inline i32 LoadI32At(const HeRecord* h, int off) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v));
    return v;
}
} // namespace

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertResolveEntity(i32)                 { return nullptr; }
HeRecord* InertPersonQuery(int, int, int)         { return nullptr; }
HeRecord* InertFindPerson(i32)                    { return nullptr; }
HeRecord* InertFindFirst(int, const int*, const int*) { return nullptr; }
HeRecord* InertFindNext()                         { return nullptr; }
i32       InertGuardTarget61(HeRecord*)           { return 0; }
void      InertRequestArgs25(i32, int, int, int)  {}
i32       InertBuildOp73(i32)                      { return 0; }
bool      InertFastTime()                          { return false; }
int       InertActionMinutes(i32)                  { return 0; }
void      InertSendNotify(i32, int, u16)           {}

const CharActionStep3Hooks kInertHooks = {
    InertResolveEntity, InertPersonQuery, InertFindPerson, InertFindFirst,
    InertFindNext, InertGuardTarget61, InertRequestArgs25, InertBuildOp73,
    InertFastTime, InertActionMinutes, InertSendNotify,
};
const CharActionStep3Hooks* g_hooks = &kInertHooks;

// Copy the 14-byte global clock image into a GameTime slot (qword_13CE852 +
// trailing dword/word = the full 14-byte time image). NpcClock() holds that
// single module-global GameTime.
inline void StampClock(GameTime& dst) { dst = NpcClock(); }

} // namespace

void SetCharActionStep3Hooks(const CharActionStep3Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep3Hooks& GetCharActionStep3Hooks() { return *g_hooks; }

// ===========================================================================
// cmd29 entity-request "arm" leaves.
// ===========================================================================

// gilde.exe 0x4ce9dc — VIBE_CharAction_PatrolFindTarget
i32 PatrolFindTarget(HeRecord* h) {
    if ((He_Flags(h) & 2) == 0)
        return 0;   // original returns the record pointer (eax); callers ignore it
    // VIBE_Person_QueryBegin(buf, a=1, b=1, c=counter@+172)
    HeRecord* found = GetCharActionStep3Hooks().personQueryBegin(1, 1, Cas3_Counter(h));
    if (!found) {
        StampClock(He_ApptTime(h));
        GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);   // +1 second
        Cas3_Packet(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    }
    StampClock(He_ApptTime(h));
    GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);        // +1 second
    i32 handle = GetNpcLeafHooks().queueRequestEntity29(0, h);
    Cas3_Packet(h) = handle;
    return handle;
}

// gilde.exe 0x4d0724 — VIBE_CharAction_GuardRequestTarget
i32 GuardRequestTarget(HeRecord* h) {
    // VIBE_GameObject_ResolveEntityById(&out, 0, target@+176, 0)
    HeRecord* entity = GetCharActionStep3Hooks().resolveEntityById(Cas3_TargetId(h));
    if (!entity)
        GetNpcLeafHooks().freeHandlerEntry(h);
    // NB: the original falls through and still dereferences `entity` even when the
    // resolve failed (it freed the handler but did not return). With the inert hook
    // entity is null here; the bridge guarantees a valid record in the real game.
    Cas3_GuardPacket(h) = GetCharActionStep3Hooks().queueRequestGuardTarget61(entity);
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, 6);  // +6 minutes
}

// gilde.exe 0x4dc590 — VIBE_CharAction_IsAnimalTargetBusy
i32 IsAnimalTargetBusy(HeRecord* h) {
    if (!h)
        return 1;
    const int sel[1] = {0};
    const int val[1] = {98};   // kind == 0x62
    HeRecord* m = GetCharActionStep3Hooks().findFirstByFilter(1, sel, val);
    if (!m)
        return 1;
    // The original compares m[+176] against *(int*)(h+1) (an unaligned dword read
    // off the input record at offset +1) and m[+184] against 1.
    i32 key = LoadI32At(h, 1);   // [ecx+1] (unaligned)
    for (;;) {
        if (*reinterpret_cast<i32*>(HeBytes(m) + 176) == key &&   // [eax+0B0h]
            *reinterpret_cast<i32*>(HeBytes(m) + 184) == 1) {     // [eax+0B8h]
            return 0;   // busy
        }
        m = GetCharActionStep3Hooks().findNextMatching();
        if (!m)
            return 1;
    }
}

// gilde.exe 0x4d1fb8 — VIBE_CharAction_FindInteractionPartner
HeRecord* FindInteractionPartner(HeRecord* h) {
    // VIBE_He_FindFirstHandlerByFilter(2, 0,95, 2,index@+8) — pairs (0,95),(2,idx).
    const int sel[2] = {0, 2};
    const int val[2] = {95, static_cast<int>(He_CityIndex(h))};
    HeRecord* m = GetCharActionStep3Hooks().findFirstByFilter(2, sel, val);
    while (m == h) {                   // skip self
        m = GetCharActionStep3Hooks().findNextMatching();
        if (!m)
            break;
    }
    if (!m) {
        He_State(h) = 0;                              // *(h+112) = 0
        *reinterpret_cast<u8*>(HeBytes(h) + 186) = 0; // *(h+186) = 0
        return nullptr;
    }
    StampClock(He_ApptTime(h));   // 14-byte copy, no advance
    // cmd29(-1) returns the packet handle (an i32); the original stores it into
    // +132 and returns it (the function's eax). We surface it as a pointer-shaped
    // value (the callers null-test / ignore it).
    i32 handle = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    Cas3_Packet(h) = handle;
    return reinterpret_cast<HeRecord*>(static_cast<std::intptr_t>(handle));
}

// gilde.exe 0x4d286c — VIBE_CharAction_FindBeggarTarget
i32 FindBeggarTarget(HeRecord* h) {
    if ((He_Flags(h) & 4) != 0)
        return 0;   // original returns the record pointer (eax); callers ignore it
    const int sel[1] = {0};
    const int val[1] = {89};   // kind == 0x59
    HeRecord* m = GetCharActionStep3Hooks().findFirstByFilter(1, sel, val);
    while (m == h) {                   // skip self
        m = GetCharActionStep3Hooks().findNextMatching();
        if (!m)
            break;
    }
    if (!m) {
        // "no other beggar handler" branch: state := 5, restore saved pose, arm cmd29(5).
        He_State(h) = 5;                       // *(h+112) = 5
        He_ApptTime(h) = He_SavedTime(h);      // +68 -> +82 (14 bytes)
        Cas3_Packet(h) = -1;                   // *(h+132) = -1
        GetNpcLeafHooks().queueRequestEntity29(5, h);
        return 5;
    }
    // another matching handler exists: stamp clock into +82, cmd29(-1) -> +132.
    StampClock(He_ApptTime(h));
    i32 handle = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    Cas3_Packet(h) = handle;
    return handle;
}

// ===========================================================================
// Init / setup leaves.
// ===========================================================================

// gilde.exe 0x4d30f4 — VIBE_CharAction_GroupGatherInit
i32 GroupGatherInit(HeRecord* h) {
    *reinterpret_cast<u8*>(HeBytes(h) + 216) = 0;          // *(h+216) = 0
    for (int i = 0; i != 24; i += 4) {
        i32 member = *reinterpret_cast<i32*>(HeBytes(h) + i + 140);
        if (member != -1)
            ++*reinterpret_cast<u8*>(HeBytes(h) + 216);    // ++*(h+216)
    }
    if (*reinterpret_cast<u8*>(HeBytes(h) + 216) == 0)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    // Reset the 6 dwords at +172..+192 to -1 (the original walks eax = h+4..h+24,
    // writing [eax+168]).
    for (int eax = 4; eax <= 24; eax += 4)
        *reinterpret_cast<i32*>(HeBytes(h) + eax + 168) = -1;
    *reinterpret_cast<u8*>(HeBytes(h) + 210) = 0;          // *(h+210) = 0
    StampClock(*reinterpret_cast<GameTime*>(HeBytes(h) + 196));  // clock -> +196 sub-record
    StampClock(He_ApptTime(h));                            // clock -> +82
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, 2);      // +2 minutes
}

// gilde.exe 0x4dd150 — VIBE_CharAction_InitTargetState
int InitTargetState(HeRecord* h) {
    i32 typeId = LoadI32At(h, 170) >> 16;  // sar 16 (unaligned load)
    StampClock(He_ApptTime(h));
    if (GetCharActionStep3Hooks().fastTimeEnabled())
        return GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);   // +1 second
    int minutes = GetCharActionStep3Hooks().targetActionMinutes(typeId);
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, minutes); // +duration minutes
}

// gilde.exe 0x4dfeb4 — VIBE_CharAction_InitLagerErweitern
int InitLagerErweitern(HeRecord* h) {
    Cas3_SubTime192(h) = He_SavedTime(h);   // +68 -> +192 (14 bytes)
    int minutes = Cas3_Mult(h) * Cas3_TargetId(h);   // [+184] * [+176]
    GameTimeAdvance(&Cas3_SubTime192(h), 0, 0, minutes);
    He_ApptTime(h) = He_SavedTime(h);       // +68 -> +82
    GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);   // +1 second
    HeRecord* obj = GetCharActionStep3Hooks().resolveEntityById(Cas3_Counter(h));  // [+172]
    if (obj) {
        if ((*reinterpret_cast<u8*>(HeBytes(obj) + 19) & 0x20) != 0)
            return GetNpcLeafHooks().freeHandlerEntry(h);
        // cmd25(obj->id@+2, 19, 32, 1, 0). The id column is read at obj+2 (dword).
        i32 entityId = LoadI32At(obj, 2);  // (unaligned)
        GetCharActionStep3Hooks().queueRequestArgs25(entityId, 19, 32, 1);
    }
    StampClock(*reinterpret_cast<GameTime*>(HeBytes(h) + 96));   // clock -> +96 scratch image
    // result := [+176] + (objByte+18), clamped to 100, stored into [+180].
    int objCap = obj ? *reinterpret_cast<u8*>(HeBytes(obj) + 18) : 0;
    int result = objCap + Cas3_TargetId(h);
    if (result >= 100) {
        Cas3_Extent(h) = 100;
        return 100;
    }
    Cas3_Extent(h) = result;
    return result;
}

// gilde.exe 0x4e20a0 — VIBE_CharAction_InitSabotage
i32 InitSabotage(HeRecord* h) {
    StampClock(He_ApptTime(h));
    GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);   // +1 second
    if (Cas3_Slot16(h) == -1) {
        // VIBE_Person_QueryBegin(buf,1,5,22) then (buf,1,5,15)
        HeRecord* begin = GetCharActionStep3Hooks().personQueryBegin(1, 5, 22);
        if (!begin)
            begin = GetCharActionStep3Hooks().personQueryBegin(1, 5, 15);
        if (begin)
            Cas3_Slot16(h) = LoadI32At(begin, 1);  // [begin+1] (unaligned)
    }
    Cas3_Misc196(h) = -1;   // *(h+196) = -1
    i32 handle = GetCharActionStep3Hooks().requestBuildOp73Sabotage(Cas3_Slot16(h));
    *reinterpret_cast<i32*>(HeBytes(h) + 200) = handle;   // *(h+200) = handle
    return handle;
}

// ===========================================================================
// Intro-message emitters.
// ===========================================================================

// gilde.exe 0x4cf9c8 — VIBE_CharAction_DuelIntroMessage(a1@eax, a2@edx, a3@ebx)
i32 DuelIntroMessage(HeRecord* h, HeRecord* combatantA, HeRecord* combatantB) {
    // Disarm both combatants: cmd25(combatant->id@+4, 456, 1024, 4, 0).
    GetCharActionStep3Hooks().queueRequestArgs25(
        *reinterpret_cast<i32*>(HeBytes(combatantA) + 4), 456, 1024, 4);
    GetCharActionStep3Hooks().queueRequestArgs25(
        *reinterpret_cast<i32*>(HeBytes(combatantB) + 4), 456, 1024, 4);
    // If A->byte+2 in {6,7}: send intro 6531 (name = B word@0) to A->id@+4.
    u8 ka = *reinterpret_cast<u8*>(HeBytes(combatantA) + 2);
    if (ka == 6 || ka == 7) {
        GetCharActionStep3Hooks().sendNotifyMessage(
            *reinterpret_cast<i32*>(HeBytes(combatantA) + 4), 6531,
            *reinterpret_cast<u16*>(combatantB));
    }
    // If B->byte+2 in {6,7}: send intro 6530 (name = A word@0) to B->id@+4.
    u8 kb = *reinterpret_cast<u8*>(HeBytes(combatantB) + 2);
    if (kb == 6 || kb == 7) {
        GetCharActionStep3Hooks().sendNotifyMessage(
            *reinterpret_cast<i32*>(HeBytes(combatantB) + 4), 6530,
            *reinterpret_cast<u16*>(combatantA));
    }
    return GetNpcLeafHooks().queueRequestEntity29(2, h);
}

// gilde.exe 0x4d0918 — VIBE_CharAction_NotifyMessageInit
i32 NotifyMessageInit(HeRecord* h) {
    Cas3_Packet(h) = -1;   // *(h+132) = -1
    if ((He_Flags(h) & 4) != 0)
        return 0;   // original returns the record pointer (eax); callers ignore it
    StampClock(He_ApptTime(h));
    GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);   // +24 days
    if ((He_Flags(h) & 2) == 0)
        return GetNpcLeafHooks().queueRequestEntity29(0, h);
    // Resolve the two persons at +176 and +172.
    HeRecord* rec176 = GetCharActionStep3Hooks().findPersonById(Cas3_TargetId(h));   // [+176]
    HeRecord* rec172 = GetCharActionStep3Hooks().findPersonById(Cas3_Counter(h));    // [+172]
    if (rec172 && rec176) {
        u8 k = *reinterpret_cast<u8*>(HeBytes(rec176) + 2);
        if (k == 6 || k == 7) {
            // RenderFormattedMessage(buf, 6500, *rec172, &h+82, *rec172) +
            // SendEntityMessage(rec176->id@+4, ...). Recipient = rec176->id.
            GetCharActionStep3Hooks().sendNotifyMessage(
                *reinterpret_cast<i32*>(HeBytes(rec176) + 4), 6500,
                *reinterpret_cast<u16*>(rec172));
        }
        return GetNpcLeafHooks().queueRequestEntity29(0, h);
    }
    // either resolve failed: restamp clock into +82 (no advance), cmd29(-1).
    StampClock(He_ApptTime(h));
    return GetNpcLeafHooks().queueRequestEntity29(-1, h);
}

} // namespace guild::sim
