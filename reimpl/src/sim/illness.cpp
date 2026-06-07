// illness — character disease / illness contraction + progression.
// Faithful 1:1 port of the gilde.exe disease sub-system decision logic. The
// cross-cluster effect tails (network state-delta packet + building stock
// debit) are left to the caller; this TU computes the byte-exact decision.
#include "sim/illness.h"

#include "crt/rand.h"     // RandNext (== VIBE_Util_RandNext @0x5cb8bc)
#include "util/coord.h"   // ConvertX (truncate toward zero, FPU chop)

namespace guild::sim {

// ---------------------------------------------------------------------------
// unk_647728 — 10 disease-event rows {i32 id, f32 costScale, i32 countMod},
// recovered byte-exact via get_bytes @0x647728. Row[i].id is stored as the
// integer i (the binary loads it as a float into an unused local — kept for
// fidelity, not used by the math). PickRandomDiseaseEvent indexes this table
// by (chosenBit + 2): disease groups 0..7 -> rows 2..9.
//   [0] id=0 scale=0.0  count=1
//   [1] id=1 scale=40.0 count=16
//   [2] id=2 scale=15.0 count=16
//   [3] id=3 scale=20.0 count=16
//   [4] id=4 scale=10.0 count=4
//   [5] id=5 scale=0.0  count=8
//   [6] id=6 scale=8.0  count=8
//   [7] id=7 scale=5.0  count=8
//   [8] id=8 scale=3.0  count=4
//   [9] id=9 scale=12.0 count=16
// ---------------------------------------------------------------------------
const DiseaseEvent kDiseaseEvents[10] = {
    { 0,  0.0f,  1 },
    { 1, 40.0f, 16 },
    { 2, 15.0f, 16 },
    { 3, 20.0f, 16 },
    { 4, 10.0f,  4 },
    { 5,  0.0f,  8 },
    { 6,  8.0f,  8 },
    { 7,  5.0f,  8 },
    { 8,  3.0f,  4 },
    { 9, 12.0f, 16 },
};

// ---------------------------------------------------------------------------
// Disease-group eligibility masks on the record+44 dword (little-endian). Each
// of the 8 groups occupies a disjoint sub-field; a group is FREE iff its mask
// bits are all clear. Recovered 1:1 from the 8 tests at 0x58ab13..0x58ab96
// (byte/word tests folded back into dword masks).
// ---------------------------------------------------------------------------
namespace {
constexpr u32 kGroupMask[8] = {
    0x000000F0u,  // group 0: byte+0 & 0xF0           (test @0x58ab13)
    0x00000F00u,  // group 1: byte+1 & 0x0F           (test @0x58ab21)
    0x00003000u,  // group 2: byte+1 & 0x30           (test @0x58ab33)
    0x0001C000u,  // group 3: dword & 0x1C000         (test @0x58ab45)
    0x000E0000u,  // group 4: byte+2 & 0x0E           (test @0x58ab5a)
    0x00700000u,  // group 5: byte+2 & 0x70           (test @0x58ab6c)
    0x01800000u,  // group 6: word+2 & 0x180          (test @0x58ab7e)
    0x1E000000u,  // group 7: byte+3 & 0x1E           (test @0x58ab92)
};
}  // namespace

// gilde.exe 0x4d762c — VIBE_Character_IsDiseaseCandidate  (__usercall, eax = (rec@eax))
//   if (*(WORD*)rec == 0xFFFF || !*(BYTE*)(rec+8) || *(char*)(rec+2) >= 10)
//       return 0;
//   if (VIBE_Character_IsOwnerForTurn(rec)) return 1;
//   return VIBE_Character_IsObjectForTurn(rec);
// A free slot (marker 0xFFFF), an absent actor (+8 == 0), or a non-person
// record (kind >= 10) is never a candidate; otherwise the record must be
// owned/objected by this peer this turn. NB the kind compare is SIGNED char.
bool IllnessIsDiseaseCandidate(const IllnessRec* rec, const IllnessTurnView& turn) {
    if (static_cast<u16>(rec->marker) == 0xFFFF)
        return false;
    if (rec->active == 0)
        return false;
    if (static_cast<i8>(rec->kind) >= 10)
        return false;
    if (turn.ownerForTurn)
        return true;
    return turn.objectForTurn;
}

// gilde.exe 0x58ab13..0x58ab96 — the per-group "free" eligibility test.
bool IllnessGroupIsFree(u32 state, int bit) {
    if (bit < 0 || bit >= 8)
        return false;
    return (state & kGroupMask[bit]) == 0;
}

// gilde.exe 0x58ac5d..0x58adb7 — pack `value` into disease `group` (== bit+2).
// Each case clears the group's sub-field and ORs in the (masked) value shifted
// into place. The value masks (0xF / 0x3 / 0x7) and shifts are recovered 1:1.
u32 IllnessPackGroup(u32 state, int group, int value) {
    switch (group) {
        case 2:  // group 0 sub-field, bits 4..7
            return (state & ~0xF0u) | (static_cast<u32>(value & 0xF) << 4);
        case 3:  // group 1 sub-field, bits 8..11
            return (state & ~0xF00u) | (static_cast<u32>(value & 0xF) << 8);
        case 4:  // group 2 sub-field, bits 12..13
            return (state & ~0x3000u) | (static_cast<u32>(value & 3) << 12);
        case 5:  // group 3 sub-field, bits 14..16
            return (state & 0xFFFE3FFFu) | (static_cast<u32>(value & 7) << 14);
        case 6:  // group 4 sub-field, bits 17..19
            return (state & 0xFFF1FFFFu) | (static_cast<u32>(value & 7) << 17);
        case 7:  // group 5 sub-field, bits 20..22
            return (state & 0xFF8FFFFFu) | (static_cast<u32>(value & 7) << 20);
        case 8:  // group 6 sub-field, bits 23..24
            return (state & 0xFE7FFFFFu) | (static_cast<u32>(value & 3) << 23);
        case 9:  // group 7 sub-field, bits 25..28
            return (state & 0xE1FFFFFFu) | (static_cast<u32>(value & 0xF) << 25);
        default:
            return state;
    }
}

// gilde.exe 0x58aaf4 — VIBE_Building_PickRandomDiseaseEvent
//   (__usercall, eax = (rec@eax, outEventByte@edx, outCost@ebx))
//
// Decision/packing core (the command-delta + stock-debit tail is the caller's):
//   1. Build the 8-entry "free" table (init all 0 from dword_5830C0; set 1 for
//      each group whose sub-field is clear). Group 0 is special-cased: if its
//      sub-field is NON-zero the function bails immediately (return 0).
//   2. Roll a random start in [0,8) and linear-probe (wrapping) up to 8 slots
//      for a free group; if none, return 0.
//   3. group = chosenBit + 2; row = kDiseaseEvents[group].
//   4. severity = (row.countMod ? RandNext() % row.countMod : 0), taken as u16.
//   5. cost = trunc(severity * row.costScale)  (FPU chop via ConvertX).
//   6. outEventByte = (severity ? group : 0).
//   7. if severity == 0 -> return 0 (no disease applied).
//      else newState = IllnessPackGroup(state, group, severity & 0xF); return 1.
DiseasePick IllnessPickRandomDiseaseEvent(u32 diseaseState) {
    DiseasePick out{};
    out.applied = false;
    out.chosenBit = -1;
    out.group = -1;
    out.severity = 0;
    out.cost = 0;
    out.newState = diseaseState;
    out.eventByte = 0;

    // (1) free-group table. dword_5830C0 is all-zero (8 ints): start all 0.
    int freeTable[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    // Group 0 special case: if its sub-field is set, bail (matches the early
    // `jz loc_58ACE4` that sets freeTable[0]=1 only when clear, then the test at
    // 0x58ab13 `jz` means a NON-zero high nibble of byte0 falls straight through
    // to the rest; a ZERO high nibble jumps to set freeTable[0]=1). So:
    if ((diseaseState & kGroupMask[0]) == 0)
        freeTable[0] = 1;
    for (int i = 1; i < 8; ++i) {
        if ((diseaseState & kGroupMask[i]) == 0)
            freeTable[i] = 1;
    }

    // (2) random start + linear probe for a free group.
    int start = static_cast<int>(static_cast<u16>(crt::RandNext() % 8));
    int chosen = -1;
    int probe = start;
    for (int n = 8; n > 0; --n) {
        if (freeTable[probe] == 1) {
            chosen = probe;
            break;
        }
        probe = (probe + 1) % 8;
    }
    if (chosen == -1)
        return out;        // no free group
    if (chosen >= 8)
        return out;        // (unreachable; kept for fidelity)

    out.chosenBit = chosen;
    int group = chosen + 2;
    if (group >= 10)
        return out;        // (unreachable; kept for fidelity)
    out.group = group;

    // (3)(4) draw severity from the group's event-table row.
    const DiseaseEvent& row = kDiseaseEvents[group];
    int severity = 0;
    if (static_cast<u16>(row.countMod) != 0)
        severity = static_cast<int>(crt::RandNext() % static_cast<u16>(row.countMod));
    severity = static_cast<u16>(severity);
    out.severity = severity;

    // (5) cost = trunc(severity * costScale). ConvertX sets FPU chop; the (int)
    // cast then truncates toward zero exactly as the original fistp does.
    double c = static_cast<double>(static_cast<u16>(severity)) * static_cast<double>(row.costScale);
    c = util::ConvertX(c);
    out.cost = static_cast<int>(c);

    // (6) event byte.
    out.eventByte = static_cast<char>(severity ? group : 0);

    // (7) no severity -> no disease applied (original returns 0 here).
    if (severity == 0)
        return out;

    out.newState = IllnessPackGroup(diseaseState, group, severity & 0xF);
    out.applied = true;
    return out;
}

}  // namespace guild::sim
