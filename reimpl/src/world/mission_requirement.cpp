// Mission / objective requirement evaluation — faithful 1:1 port of the
// VIBE_MissionReq_* leaf checkers from gilde.exe. See mission_requirement.h.
#include "world/mission_requirement.h"

#include "sim/entity.h"    // guild::sim::PersonQueryBegin / PersonIterNext / ObjectRec
#include "sim/gametime.h"  // guild::sim::GameTimeDiffMinutes

#include <cstring>

namespace guild::world {

// ---------------------------------------------------------------------------
// Cross-cluster globals reused from their owning modules (NOT redefined here).
//   g_sysGameTime — qword_13CE852, the current wall clock (owned by
//   sim/command_apply5.cpp; declared in sim/command_apply5.h, but pulling that
//   heavy header in would drag its switch-table deps, so we extern it directly).
// ---------------------------------------------------------------------------
}  // namespace guild::world
namespace guild::sim { extern GameTime g_sysGameTime; }  // qword_13CE852
namespace guild::world {

// Recovered float/double constants (see get_bytes in the port notes).
static const float  kStatScale       = 0.02380952425301075f;   // flt_623D50 (~1/42)
static const float  kOwnRatioScale   = 100.0f;                  // flt_623D54
static const float  kAvgScale        = 0.009999999776482582f;  // flt_623D58 (~0.01)

// Float 1.0 as a raw bit pattern (the originals compare the average's bits as a
// signed int against 1065353216 == 1.0f).
static const i32 kOneBits = 1065353216;  // 0x3F800000

// Reinterpret a float's storage as a signed int (the original reads the float
// slot through an integer register before the SLODWORD/>= compare).
static i32 FloatBits(float f) {
    i32 v;
    std::memcpy(&v, &f, sizeof v);
    return v;
}

// ---------------------------------------------------------------------------
// Raw record access. The originals index the global arrays by byte and read a
// few fields that the cleaned C++ structs do not surface (e.g. the dword at
// person+9, the object/owner word at +0x27). We mirror that with byte-precise,
// alignment-safe reads against the real sim::g_persons array.
// ---------------------------------------------------------------------------

// Number of person records scanned by CheckOwnPersonRatio: the original loops
// `for (i = 0; i != 102912; i += 134)` over dword columns => 102912/134 == 768
// records (== kPersonStride/4 dwords each), i.e. the full person capacity.
static const int kPersonScanCount = 102912 / 134;  // 768
static_assert(102912 / 134 == sim::kPersonCapacity,
              "person scan bound must equal the person capacity");

// Base of person record n (byte_12CE912 is g_persons base +2 in the original;
// callers that need the +2 kind byte read PersonRecordPtr(n)[2]).
static const u8* PersonRecordPtr(int n) {
    return reinterpret_cast<const u8*>(&sim::g_persons[n]);
}

// Unaligned little-endian field reads (records are byte-packed; +9/+0x27 dwords
// and words are not naturally aligned).
static u16 ReadWordAt(const u8* base, int off) {
    u16 v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}
static i32 ReadDwordAt(const u8* base, int off) {
    i32 v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}

// gilde.exe 0x539c44 — VIBE_MissionReq_AccumulateTimer
//   (__usercall: eax=record@a1, edx=conditionMet@a2, ebx=requiredMinutes@a3).
bool MissionReqAccumulateTimer(ObjectiveRecord* record, bool conditionMet,
                               int requiredMinutes) {
    sim::GameTime* timer = &record->timer;            // a1 + 8
    if (conditionMet) {
        // "Fresh" timer test: all four time words (day/hour/minute/second) zero.
        if (timer->day == 0 && timer->hour == 0 && timer->minute == 0 &&
            timer->second == 0) {
            // Stamp with the current wall clock. The original copies the day
            // dword directly then VIBE_GameTime_Set-s the remaining fields from
            // the same clock; net effect is a full record copy.
            *timer = guild::sim::g_sysGameTime;
        }
        // DiffMinutes(timer, now) — elapsed minutes since the stamp.
        return guild::sim::GameTimeDiffMinutes(timer, &guild::sim::g_sysGameTime) >=
               requiredMinutes;
    }
    // Condition not held this tick: clear the timer and report "not met".
    *timer = sim::GameTime{};
    return false;
}

// gilde.exe 0x539160 — VIBE_MissionReq_CheckStatThreshold (eax=clan, edx=row).
// Counts the 5 stat bytes at clan[+128..+132] whose value*flt_623D50 reaches the
// float threshold (row+20 cast from row.threshold), then tests count>=row[+16].
bool MissionReqCheckStatThreshold(const u8* clanRecord, const ReqTableRow* row) {
    const float goal = static_cast<float>(row->timerMin);  // *(int*)(row+20)
    int count = 0;
    for (int i = 0; i < 5; ++i) {
        if (static_cast<double>(clanRecord[128 + i]) * kStatScale >= goal)
            ++count;
    }
    return count >= row->threshold;
}

// gilde.exe 0x539380 — VIBE_MissionReq_CheckOwnPersonRatio (eax=record, edx=row).
// Walks all 768 person records; among "citizens" (kind byte < 10) counts how many
// share the objective owner's top-byte selector (dword at person+9, >>24, compared
// to the objective record's dword at +9 >>24). Tests row.threshold against
// (owned/citizens)*flt_623D54.
bool MissionReqCheckOwnPersonRatio(const u8* objectiveOwnerRec,
                                   const ReqTableRow* row) {
    // The objective owner key: top byte of the dword at +9 (== byte at +12).
    const i32 ownerKey = ReadDwordAt(objectiveOwnerRec, 9) >> 24;

    int citizens = 0;  // v4
    int owned    = 0;  // v5
    for (int n = 0; n < kPersonScanCount; ++n) {
        const u8* person = PersonRecordPtr(n);
        if (person[2] < 10) {  // byte_12CE912[i*4] == g_persons[n].kind
            ++citizens;
            if ((ReadDwordAt(person, 9) >> 24) == ownerKey)  // dword_12CE919[i]>>24
                ++owned;
        }
    }
    // Original divides unconditionally; citizens is guaranteed > 0 in-game (the
    // player's own clan always qualifies). Preserve the division as written.
    return static_cast<double>(row->threshold) <=
           static_cast<double>(owned) / static_cast<double>(citizens) * kOwnRatioScale;
}

// gilde.exe 0x539d14 — VIBE_MissionReq_CountMembersByState (dl=state, ebx=out,
//                                                            eax=objective).
i32 MissionReqCountMembersByState(u8 state, MemberCount* out,
                                  const ObjectiveRecord* objective) {
    sim::ObjectRec* rec;
    if (state) {
        sim::PersonFilter f{5, static_cast<signed char>(state)};  // (1, 5, state)
        rec = sim::PersonQueryBegin(&f, 1);
    } else {
        sim::PersonFilter f{6, 0};                                // (1, 6) match-any
        rec = sim::PersonQueryBegin(&f, 1);
    }

    const u16 selector = ReadWordAt(reinterpret_cast<const u8*>(objective), 0);
    int sum = 0;    // ecx — owner-matched members
    int count = 0;  // edx — total members iterated
    while (rec) {
        const u16 ownerWord = ReadWordAt(reinterpret_cast<const u8*>(rec), 0x27);
        if (ownerWord == selector)
            ++sum;
        ++count;
        rec = sim::PersonIterNext();
    }

    out->count = count;
    out->sum = sum;
    if (count <= 0) {
        out->average = 0.0f;
        return 0;
    }
    out->average = static_cast<float>(static_cast<double>(sum) /
                                      static_cast<double>(count));
    return FloatBits(out->average);
}

// gilde.exe 0x539da0 — VIBE_MissionReq_CountGuildMembers (al=state, edx=out).
i32 MissionReqCountGuildMembers(u8 state, MemberCount* out) {
    sim::ObjectRec* rec;
    if (state) {
        sim::PersonFilter f{5, static_cast<signed char>(state)};
        rec = sim::PersonQueryBegin(&f, 1);
    } else {
        sim::PersonFilter f{6, 0};
        rec = sim::PersonQueryBegin(&f, 1);
    }

    int sum = 0;    // ecx — "busy" owners (kind 6 or 7)
    int count = 0;  // edx — total iterated
    while (rec) {
        const u16 ownerWord = ReadWordAt(reinterpret_cast<const u8*>(rec), 0x27);
        if (ownerWord != 0xFFFF) {
            const u8 kind = PersonRecordPtr(ownerWord)[2];  // byte_12CE912[0x218*owner]
            if (kind == 6 || kind == 7)
                ++sum;
        }
        ++count;
        rec = sim::PersonIterNext();
    }

    out->count = count;
    out->sum = sum;
    if (count <= 0) {
        out->average = 0.0f;
        return 0;
    }
    out->average = static_cast<float>(static_cast<double>(sum) /
                                      static_cast<double>(count));
    return FloatBits(out->average);
}

// NOTE: VIBE_Mission_FindGuildMemberState (0x539cbc) is already translated in
// world/mission_member.cpp and is reused there — not redefined in this module.

// gilde.exe 0x5393ec — VIBE_MissionReq_CheckMultiStat (this=objective).
bool MissionReqCheckMultiStat(const ObjectiveRecord* objective) {
    MemberCount c{};
    MissionReqCountMembersByState(19, &c, objective);
    if (FloatBits(c.average) < kOneBits)
        return false;
    MissionReqCountMembersByState(4, &c, objective);
    if (c.count > 0 && FloatBits(c.average) < kOneBits)
        return false;
    MissionReqCountMembersByState(16, &c, objective);
    if (c.count > 0 && FloatBits(c.average) < kOneBits)
        return false;
    return true;
}

// gilde.exe 0x53945c — VIBE_MissionReq_CheckStatCombo (this=objective).
bool MissionReqCheckStatCombo(const ObjectiveRecord* objective) {
    MemberCount c{};
    MissionReqCountMembersByState(21, &c, objective);
    if (!(c.count >= 3 && FloatBits(c.average) >= kOneBits))
        return false;
    MissionReqCountMembersByState(20, &c, objective);
    if (!(c.count >= 3 && FloatBits(c.average) >= kOneBits))
        return false;
    MissionReqCountMembersByState(18, &c, objective);
    if (!(c.count >= 3 && FloatBits(c.average) >= kOneBits))
        return false;
    return true;
}

// gilde.exe 0x539534 — VIBE_MissionReq_CheckCumulativeStats (edx=row).
bool MissionReqCheckCumulativeStats(const ReqTableRow* row) {
    MemberCount c{};
    MissionReqCountGuildMembers(21, &c);
    if (c.count > 0 && FloatBits(c.average) < kOneBits) return false;
    MissionReqCountGuildMembers(20, &c);
    if (c.count > 0 && FloatBits(c.average) < kOneBits) return false;
    MissionReqCountGuildMembers(18, &c);
    if (c.count > 0 && FloatBits(c.average) < kOneBits) return false;
    MissionReqCountGuildMembers(22, &c);
    if (c.count > 0 && FloatBits(c.average) < kOneBits) return false;
    MissionReqCountGuildMembers(8, &c);
    if (c.count > 0 && FloatBits(c.average) < kOneBits) return false;
    MissionReqCountGuildMembers(9, &c);
    // (sum + count) for state 9 must reach the threshold (last gate AND-ed in).
    return (c.count <= 0 || FloatBits(c.average) >= kOneBits) &&
           (c.sum + c.count) >= row->threshold;
}

// gilde.exe 0x53963c — VIBE_MissionReq_CheckMemberStats (ebx=row).
bool MissionReqCheckMemberStats(ObjectiveRecord* record, const ReqTableRow* row) {
    MemberCount c{};
    bool ok = true;
    MissionReqCountGuildMembers(11, &c);
    if (!(c.count <= 0 || FloatBits(c.average) >= kOneBits)) ok = false;
    if (ok) {
        MissionReqCountGuildMembers(12, &c);
        if (!(c.count <= 0 || FloatBits(c.average) >= kOneBits)) ok = false;
    }
    if (ok) {
        MissionReqCountGuildMembers(13, &c);
        if (!(c.count <= 0 || FloatBits(c.average) >= kOneBits)) ok = false;
    }
    const bool met = ok && (row->threshold <= 3);
    return MissionReqAccumulateTimer(record, met, row->timerMin);
}

// gilde.exe 0x5396dc — VIBE_MissionReq_CheckAverageStat (ecx=row).
bool MissionReqCheckAverageStat(const ReqTableRow* row) {
    MemberCount c{};
    MissionReqCountGuildMembers(0, &c);
    return static_cast<double>(row->threshold) * kAvgScale <=
           static_cast<double>(c.average);
}

}  // namespace guild::world
