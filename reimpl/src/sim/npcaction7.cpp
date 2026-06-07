// NpcAction7 — social / staff-management debug-command leaf family. See
// npcaction7.h for the function map and the calling-convention recovery.
//
// Calling convention (recovered from each prologue + VIBE_NpcAction_Dispatch):
//   * The Cmd leaves (Heal/SelectRoom) take the descriptor in eax; descriptor[+0]
//     (dword) is the actor's person id, descriptor[+4] (word) the message text
//     base. AssignWork/AdjustStat additionally take the player id in esi (passed
//     straight to Person_QueryBegin); AdjustStat reads the descriptor through ecx.
//   * The social resolvers take the descriptor in eax and the id-source in edx.
//   * The gossip spreaders take the speaker's Person record in eax and the He
//     descriptor in edx.
//
// Person table substrate (word_12CE910, 768 × 536 bytes):
//   record[+0]    (word)  marker / id-low (0xFFFF == free slot for AssignWork)
//   record[+2]    (byte)  byte_12CE912 — kind/marker byte (==1 employed for Heal)
//   record[+4]    (dword) dword_12CE914 — entity id (command target)
//   record[+0x16c](dword) dword_12CEA7C — owner-object id (Heal scan key)
//   record[+0x81] (byte)  health/illness stat (Heal delta source)
//   record[+128+chan] (byte) per-relation loyalty (gossip ceiling 0xFC)
//   record[+130]  (byte)  stat gate (SelectRoom)
//
// The bounded collection loops use the disassembly-recovered bounds (the
// Hex-Rays pseudocode leaves the loop counters uninitialized after the optimizer
// folded them into the offset register). The float stat math is x87-equivalent
// double precision truncated toward zero (fistp), matching the originals.

#include "sim/npcaction7.h"

#include "util/math_random.h"   // RandomModulo

#include <cstdint>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook plumbing (mirrors the NpcAction6 pattern).
// ---------------------------------------------------------------------------
static const NpcAction7Hooks kInertHooks{};
static const NpcAction7Hooks* g_hooks7 = &kInertHooks;
void SetNpcAction7Hooks(const NpcAction7Hooks* hooks) {
    g_hooks7 = hooks ? hooks : &kInertHooks;
}
const NpcAction7Hooks& GetNpcAction7Hooks() { return *g_hooks7; }

// ---------------------------------------------------------------------------
// Raw record accessors (byte-faithful to the disasm).
// ---------------------------------------------------------------------------
static inline i32 Desc_PersonId(HeRecord* h) {
    return *reinterpret_cast<i32*>(HeBytes(h) + 0);   // descriptor[+0]
}
static inline u16 Desc_TextBaseWord(HeRecord* h) {
    return *reinterpret_cast<u16*>(HeBytes(h) + 4);   // descriptor[+4] word
}

static inline u16 Per_Index(u8* p)      { return *reinterpret_cast<u16*>(p + 0); }
static inline u8  Per_Marker(u8* p)     { return *(p + 2); }
static inline i32 Per_EntityId(u8* p)   { return *reinterpret_cast<i32*>(p + 4); }
static inline i32 Per_OwnerObj(u8* p)   { return *reinterpret_cast<i32*>(p + kNpc7PersonOwnerObjOff); }
static inline u8  Per_HealthStat(u8* p) { return *(p + 0x81); }       // +129
static inline u8  Per_Relation(u8* p, int chan) { return *(p + 128 + chan); }
static inline u8  Per_RoomGate(u8* p)   { return *(p + 130); }

// Object/Building record fields touched by these scans.
static inline u8  Obj_TypeByte(u8* o)   { return *o; }                // object[+0]
static inline i32 Obj_Id(u8* o)         { return *reinterpret_cast<i32*>(o + 1); }   // object[+1]

// Truncate toward zero matching the x87 (int)double the originals emit.
static inline i32 ToIntTrunc(double v) { return static_cast<i32>(v); }

// VIBE_Person_FindRecordById fan-in.
static inline u8* Find(const NpcAction7Hooks& hk, i32 id) {
    return hk.findRecordById ? hk.findRecordById(id) : nullptr;
}

// ===========================================================================
// Deterministic cores (exposed for golden tests).
// ===========================================================================

// gilde.exe 0x575948.. — frac = (roll + base) * 0.01  (dbl_625544 == 0.01).
double NpcAction7_StatFraction(int roll, double base) {
    return (static_cast<double>(roll) + base) * 0.01;
}

// gilde.exe ..575a3a — pct = (int)(frac * 100.0)  (flt_62554C == 100.0).
int NpcAction7_StatPercent(double fraction) {
    return ToIntTrunc(fraction * 100.0);
}

// gilde.exe 0x5754e6.. — the heal/loyalty clamp. The original computes
//   roll1 = RandomModulo(span) + lo; if (stat < roll1) delta = stat;
//   else delta = (roll2 = RandomModulo(span) + lo);   // second draw is emitted
int NpcAction7_HealDelta(int stat, int firstRoll, int secondRoll, int lo) {
    int r1 = firstRoll + lo;
    if (stat < r1)
        return stat;
    return secondRoll + lo;
}

// ===========================================================================
// Social relation resolvers (0x56850c / 0x568578 / 0x5685e4).
//   Three byte-identical bodies differing only by the mood kind argument.
// ===========================================================================
static int ResolveTargetAndMood(HeRecord* desc, HeRecord* src, int moodKind) {
    const auto& hk = GetNpcAction7Hooks();

    // descriptor[+2] == 6 -> UI/local actor: open an office window to pick a
    // target. We model the window as the queryBegin leaf; nullptr == "no pick".
    if (*(HeBytes(desc) + 2) == 6) {
        u8* target = hk.queryBegin ? hk.queryBegin(0, 0, 0, 0) : nullptr;
        if (target) {
            if (hk.adjustRelationByMood) hk.adjustRelationByMood(target, moodKind);
            return 1;
        }
        return 0;
    }

    // Non-UI: resolve src[+4]'s id to a Person record and bump the mood.
    u8* rec = Find(hk, *reinterpret_cast<i32*>(HeBytes(src) + 4));
    if (rec) {
        if (hk.adjustRelationByMood) hk.adjustRelationByMood(rec, moodKind);
        return 1;
    }
    return 0;
}

int NpcAction7_ResolveTargetAndGreet(HeRecord* desc, HeRecord* src) {
    return ResolveTargetAndMood(desc, src, 1);   // 0x568549 / 0x568566
}
int NpcAction7_ResolveTargetAndFlirt(HeRecord* desc, HeRecord* src) {
    return ResolveTargetAndMood(desc, src, 3);   // 0x5685b5 / 0x5685d2
}
int NpcAction7_ResolveTargetAndCompliment(HeRecord* desc, HeRecord* src) {
    return ResolveTargetAndMood(desc, src, 4);   // 0x568621 / 0x56863e
}

// ===========================================================================
// Gossip / rumor spreaders (0x568998 / 0x568ec4).
// ---------------------------------------------------------------------------
// Recovered from disassembly: a 5-dword shuffle (var_28) holds a permutation of
// 0..4; the loop walks it in order (ecx step 4), and for each channel whose
// relation byte (record + 0x80 + chan) is below 0xFC, bumps the mood by that
// channel and remembers it. ToOne stops after the first match (esi >= 1), ToTwo
// after two (esi >= 2). On a "local" speaker (record[+2]==6) it emits the spread
// command and renders the rumor text; the textId carries the matched channel(s).
// ===========================================================================
static int SpreadGossip(u8* personRecord, HeRecord* desc, int wantMatches) {
    const auto& hk = GetNpcAction7Hooks();

    // desc[16] = 1 (mark active) — 0x5689ac / 0x568edd.
    *reinterpret_cast<i32*>(HeBytes(desc) + 16) = 1;

    i32 shuffled[5] = {0, 1, 2, 3, 4};
    if (hk.shuffleDwords) hk.shuffleDwords(5, shuffled);

    int matches = 0;
    int firstChan = 0;
    int secondChan = 0;
    for (int i = 0; i < 5; ++i) {
        int chan = shuffled[i];
        if (Per_Relation(personRecord, chan) < 0xFC) {
            if (hk.adjustRelationByMood) hk.adjustRelationByMood(personRecord, chan);
            ++matches;
            if (matches == 1) firstChan = chan;
            else if (matches == 2) secondChan = chan;
            if (matches >= wantMatches) break;
        }
        // The original's while-guard is (esi < want && ecx < 20): once `want`
        // matches are found it stops even mid-array; the break above mirrors it.
    }

    if (Per_Marker(personRecord) == 6) {        // record[+2] == 6 -> local speaker
        if (hk.requestBuildOp67) hk.requestBuildOp67(Per_EntityId(personRecord));
        // VIBE_Text_RenderFormattedMessage(..., 3245, 2*speakerIdx+2582, chan+4810
        //   [, firstChan+4810]) then VIBE_Panel_ShowUseObject. Rendered text is a
        //   UI side-effect; the deterministic state (relations, matched channels)
        //   is what we model. We surface the rumor via the status message hook so
        //   tests can observe the channel ids carried into the text.
        if (hk.sendEntityMessage) {
            int textId = 4810 + (wantMatches >= 2 ? secondChan : firstChan);
            hk.sendEntityMessage(Per_EntityId(personRecord), -1, textId);
        }
    }
    return 1;
}

int NpcAction7_SpreadGossipToOne(u8* personRecord, HeRecord* desc) {
    return SpreadGossip(personRecord, desc, 1);
}
int NpcAction7_SpreadGossipToTwo(u8* personRecord, HeRecord* desc) {
    return SpreadGossip(personRecord, desc, 2);
}

// ===========================================================================
// Building-candidate collection (shared by Heal / AssignWork / CollectTargets).
// ---------------------------------------------------------------------------
// Walk the Person_QueryBegin/IterNext iterator; for each record whose
// MapTypeToCategory(type) is in `acceptMask` (bit1 = cat 1, bit2 = cat 2), store
// the record pointer. Bound: stop once `slot` reaches `maxBytes/4` slots (the
// originals cap the byte offset at 0x20 == 8 entries) OR the iterator ends.
// Returns the match count; `out[]` holds up to (maxBytes/4) record pointers.
// ===========================================================================
static int CollectBuildings(const NpcAction7Hooks& hk, int player, u16 personIndex,
                            unsigned acceptMask, u8** out, int maxEntries) {
    int count = 0;
    int offset = 0;   // mirrors the original's byte offset register (step 4)
    u8* cur = hk.queryBegin ? hk.queryBegin(player, 1, 3, personIndex) : nullptr;
    while (cur) {
        int cat = hk.mapTypeToCategory ? hk.mapTypeToCategory(Obj_TypeByte(cur)) : 0;
        // cat is read as signed char then compared; cat 1 always, cat 2 if masked.
        bool accept = ((cat == 1) && (acceptMask & 1)) ||
                      ((cat == 2) && (acceptMask & 2));
        if (accept) {
            out[count] = cur;
            ++count;
            offset += 4;
        }
        cur = hk.iterNext ? hk.iterNext() : nullptr;
        if (!cur || offset >= maxEntries * 4) break;
    }
    return count;
}

// ===========================================================================
// gilde.exe 0x575414 — VIBE_NpcAction_HealCmd.
// ===========================================================================
int NpcAction7_HealCmd(HeRecord* desc) {
    const auto& hk = GetNpcAction7Hooks();

    u8* actor = Find(hk, Desc_PersonId(desc));
    if (!actor) return kNpc7NoActor;

    // Collect up to 8 category-1 (house) buildings (acceptMask = bit1 only).
    u8* cands[8];
    int count = CollectBuildings(hk, 0, Per_Index(actor), 0x1, cands, 8);
    if (count == 0) return kNpc7Retry;

    // Pick one and read its object id.
    int pick = static_cast<u16>(util::RandomModulo(static_cast<u16>(count)));
    i32 buildingId = Obj_Id(cands[pick]);
    if (!buildingId) return kNpc7Retry;

    // Scan the Person array for the slot owned by this building (marker(+2)==1 and
    // ownerObj(+0x16c)==buildingId). Bounds: stride 536, capacity 768 (disasm
    // 0x5754b2: step 0x218, bound 0x64800).
    u8* base = hk.personTableBase ? hk.personTableBase() : nullptr;
    int cap = hk.personTableCapacity ? hk.personTableCapacity() : kNpc7PersonCapacity;
    u8* target = nullptr;
    if (base) {
        for (int i = 0; i < cap; ++i) {
            u8* slot = base + static_cast<std::size_t>(i) * kNpc7PersonStride;
            if (Per_Marker(slot) == 1 && Per_OwnerObj(slot) == buildingId) {
                target = slot;
                break;
            }
        }
    }
    if (!target) return kNpc7Retry;

    int stat = Per_HealthStat(target);          // *(target + 0x81)
    if (stat <= 0) return kNpc7Retry;

    // delta = min(stat, RandomModulo(0xA)+5) with the original's re-draw shape.
    int firstRoll = static_cast<u16>(util::RandomModulo(0xA));
    int secondRoll = firstRoll;
    if (stat >= firstRoll + 5)                   // 0x5754f8: jge re-rolls
        secondRoll = static_cast<u16>(util::RandomModulo(0xA));
    int delta = NpcAction7_HealDelta(stat, firstRoll, secondRoll, 5);

    if (hk.requestBuildOp93)
        hk.requestBuildOp93(Per_EntityId(target), 1, 0, static_cast<u8>(-delta));
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(actor), Per_EntityId(actor),
                             Desc_TextBaseWord(desc) + 3881);
    return kNpc7Ok;
}

// ===========================================================================
// gilde.exe 0x5755ac — VIBE_NpcAction_AssignWorkCmd.
// ===========================================================================
int NpcAction7_AssignWorkCmd(HeRecord* desc, int player) {
    const auto& hk = GetNpcAction7Hooks();

    u8* actor = Find(hk, Desc_PersonId(desc));
    if (!actor) return kNpc7NoActor;

    // Collect up to 8 category-1/2 buildings.
    u8* cands[8];
    int count = CollectBuildings(hk, player, Per_Index(actor), 0x3, cands, 8);
    if (count == 0) return kNpc7Retry;

    // Pick the work building.
    int pick = static_cast<u16>(util::RandomModulo(static_cast<u16>(count)));
    u8* building = cands[pick];

    // Choose a scan window: RandomModulo(2) selects half vs full; a further
    // RandomModulo(2) selects which half / direction (0x575654..0x575694,
    // 0x5757c2..). lo/hi are slot indices; step is +1 or -1.
    int lo, hi, step;
    if (static_cast<u16>(util::RandomModulo(2))) {     // half window
        hi = 384; lo = 0; step = 1;
        if (static_cast<u16>(util::RandomModulo(2))) { lo = 384; hi = 0; step = -1; }
    } else {                                            // full window
        hi = 768; lo = 0; step = 1;
        if (static_cast<u16>(util::RandomModulo(2))) { hi = 0; step = -1; lo = 768; }
    }

    // Scan for employable persons: marker(+2) not 6/7 and < 10, slot live
    // (index(+0) != 0xFFFF). Collect up to 16 (offset cap 0x40). (0x5756a1..)
    u8* base = hk.personTableBase ? hk.personTableBase() : nullptr;
    u8* people[16];
    int found = 0;
    if (base) {
        for (int idx = lo; (step > 0) ? (idx < hi) : (idx > hi); idx += step) {
            u8* slot = base + static_cast<std::size_t>(idx) * kNpc7PersonStride;
            u8 marker = Per_Marker(slot);
            if (marker != 6 && marker != 7 && marker < 10 && Per_Index(slot) != 0xFFFF) {
                people[found] = slot;
                ++found;
                if (found == 16) break;             // offset == 0x40
            }
        }
    }

    if (count == 0) return kNpc7Retry;              // building still required
    u8* worker = nullptr;
    if (found) worker = people[static_cast<u16>(util::RandomModulo(static_cast<u16>(found)))];
    if (!building || !worker) return kNpc7Retry;

    // Queue the work-assignment command (kind = RandomModulo(2)+308) + status.
    int kind = static_cast<u16>(util::RandomModulo(2)) + 308;
    if (hk.queueRequest17)
        hk.queueRequest17(Obj_Id(building), -1, 1, kind, hk.currencyByte, 0);
    if (hk.sendQuickjumpMessage)
        hk.sendQuickjumpMessage(Per_EntityId(actor), Per_EntityId(actor),
                                Desc_TextBaseWord(desc) + 3881, Obj_Id(building));
    return kNpc7Ok;
}

// ===========================================================================
// gilde.exe 0x575804 — VIBE_NpcAction_CollectTargets.
// ===========================================================================
int NpcAction7_CollectTargets(u8* personRecord, i32* out, float rate) {
    const auto& hk = GetNpcAction7Hooks();

    // Collect up to 8 category-1/2 buildings.
    u8* cands[8];
    int count = CollectBuildings(hk, 0, Per_Index(personRecord), 0x3, cands, 8);
    if (count == 0) return 0;

    // Pick one; write its id to *out (candidate[+1]).
    int pick = static_cast<u16>(util::RandomModulo(static_cast<u16>(count)));
    u8* building = cands[pick];
    *out = Obj_Id(building);

    // Find the building's workshop: QueryFind(candidate[+0x5D], 2, 6, 0, 0x2A).
    i32 workshopKey = *reinterpret_cast<i32*>(building + 0x5D);
    u8* workshop = hk.gameObjectQueryFind5
                       ? hk.gameObjectQueryFind5(workshopKey, 2, 6, 0, 0x2A)
                       : nullptr;
    if (!workshop) return 0;

    // Iterate up to 12 of its rooms: QueryFind(workshop[+0x14], 1, 5) + IterNext.
    i32 roomKey = *reinterpret_cast<i32*>(workshop + 0x14);
    u8* rooms[12];
    int roomCount = 0;
    u8* room = hk.gameObjectQueryFind3 ? hk.gameObjectQueryFind3(roomKey, 1, 5) : nullptr;
    while (room) {
        rooms[roomCount++] = room;
        room = hk.gameObjectIterNext ? hk.gameObjectIterNext() : nullptr;
        if (!room || roomCount >= 12) break;
    }
    if (roomCount == 0) return 0;

    // For each room whose count(+14) > 1, queue a proportional command:
    //   amount = max(1, (int)(count * rate)). (0x5758e7..)
    for (int i = 0; i < roomCount; ++i) {
        u8* r = rooms[i];
        i32 roomFill = *reinterpret_cast<i32*>(r + 14);
        if (roomFill > 1) {
            int amount = ToIntTrunc(static_cast<double>(roomFill) * static_cast<double>(rate));
            if (amount < 1) amount = 1;
            if (hk.queueRequest17)
                hk.queueRequest17(-1, *reinterpret_cast<i32*>(workshop + 2),
                                  amount, *reinterpret_cast<i16*>(r), hk.currencyByte, 0);
        }
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x575c64 — VIBE_NpcAction_SelectRoomCmd.
// ===========================================================================
int NpcAction7_SelectRoomCmd(HeRecord* desc) {
    const auto& hk = GetNpcAction7Hooks();

    u8* actor = Find(hk, Desc_PersonId(desc));
    if (!actor) return kNpc7NoActor;

    // Gate: if RandomModulo(0x100) < stat(+130) -> retry. (0x575cad)
    int gate = Per_RoomGate(actor);
    if (static_cast<u16>(util::RandomModulo(0x100)) < gate) return kNpc7Retry;

    // Iterate the actor's rooms (object[+94]); collect up to 6 whose type-kind
    // (typeDescriptor[+0], 65-byte stride) != 9. (0x575cba.., cap v10 < 24.)
    i32 roomKey = *reinterpret_cast<i32*>(actor + 94);
    u8* rooms[6];
    int count = 0;
    u8* room = hk.gameObjectQueryFind3 ? hk.gameObjectQueryFind3(roomKey, 1, 5) : nullptr;
    int offset = 0;
    while (room) {
        // The original reads *(byte*)(65 * room[+0] + dword_13CE27C); the type
        // table is cross-module, so a test maps it through mapTypeToCategory's
        // sibling semantics. We treat "kind 9" as the reject; the kind byte is
        // surfaced via object[+0] here (the test mock controls the value).
        int kind = hk.mapTypeToCategory ? hk.mapTypeToCategory(Obj_TypeByte(room)) : 0;
        if (kind != 9) {
            rooms[count] = room;
            ++count;
            offset += 4;
        }
        room = hk.gameObjectIterNext ? hk.gameObjectIterNext() : nullptr;
        if (!room || offset >= 24) break;
    }
    if (count == 0) return kNpc7Retry;

    u8* picked = rooms[static_cast<u16>(util::RandomModulo(static_cast<u16>(count)))];
    if (hk.queueRequest17)
        hk.queueRequest17(-1, Per_EntityId(actor), 1, *reinterpret_cast<i16*>(picked),
                          hk.currencyByte, 0);
    if (hk.sendEntityMessage)
        hk.sendEntityMessage(Per_EntityId(actor), Per_EntityId(actor),
                             Desc_TextBaseWord(desc) + 3881);
    return kNpc7Ok;
}

// ===========================================================================
// gilde.exe 0x575948 / 0x575a4c / 0x575b58 — VIBE_NpcAction_AdjustStatCmd{A,B,C}.
// ===========================================================================
static int AdjustStatCmd(HeRecord* desc, int player, int span, double base,
                         bool quickjump) {
    const auto& hk = GetNpcAction7Hooks();

    // roll = RandomModulo(span); frac = (roll + base) * 0.01. (0x575958..)
    int roll = static_cast<u16>(util::RandomModulo(static_cast<u16>(span)));
    double frac = NpcAction7_StatFraction(roll, base);

    u8* actor = Find(hk, Desc_PersonId(desc));
    if (!actor) return kNpc7NoActor;

    i32 scratch[2] = {0, 0};
    if (!NpcAction7_CollectTargets(actor, &scratch[0], static_cast<float>(frac)))
        return kNpc7Retry;

    // Gate: QueryBegin(player, 1, 1, scratch[0]) must succeed. (0x5759bc)
    u8* gate = hk.queryBegin ? hk.queryBegin(player, 1, 1, static_cast<u16>(scratch[0]))
                             : nullptr;
    if (!gate) return kNpc7Retry;

    int pct = NpcAction7_StatPercent(frac);     // (int)(frac * 100)
    scratch[1] = pct;

    int textId = Desc_TextBaseWord(desc) + 3881;
    if (quickjump) {
        if (hk.sendQuickjumpMessage)
            hk.sendQuickjumpMessage(Per_EntityId(actor), Per_EntityId(actor), textId, pct);
    } else {
        if (hk.sendEntityMessage)
            hk.sendEntityMessage(Per_EntityId(actor), Per_EntityId(actor), textId);
    }
    return kNpc7Ok;
}

int NpcAction7_AdjustStatCmdA(HeRecord* desc, int player) {
    return AdjustStatCmd(desc, player, 0xD, 13.0, false);   // SendEntityMessage
}
int NpcAction7_AdjustStatCmdB(HeRecord* desc, int player) {
    return AdjustStatCmd(desc, player, 0xF, 5.0, true);     // SendQuickjumpMessage
}
int NpcAction7_AdjustStatCmdC(HeRecord* desc, int player) {
    return AdjustStatCmd(desc, player, 0x14, 5.0, true);    // SendQuickjumpMessage
}

} // namespace guild::sim
