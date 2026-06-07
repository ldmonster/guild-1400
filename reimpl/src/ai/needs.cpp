#include "ai/needs.h"

#include "crt/rand.h"

namespace guild::ai {

// gilde.exe 0x5cb8bc — VIBE_Util_RandNext (guild::crt::RandNext). The decay/pick
// functions call (int)RandNext() % n; n is small so the sign of the dividend (the
// 15-bit RandNext result is always >= 0) never matters, but we keep the exact
// `% n` form to mirror VIBE_Math_RandomModulo / the inline modulo in the binary.

// gilde.exe 0x58adc0 — VIBE_AiNeeds_ApplyRandomDecayField
//   (__usercall: st0=, eax=person, edx=, ecx=)
int ApplyRandomDecayField(NeedAgent& a, NeedsCommandHook* hook) {
    // if ((*(person+44) & 0xF) != 0) return 0;  — low nibble already set => skip.
    if ((a.needWord & 0xFu) != 0)
        return 0;

    // mag = word_64773C ? RandNext() % word_64773C : 0;
    u16 mag = 0;
    if (kDecayBound)
        mag = static_cast<u16>(crt::RandNext() % kDecayBound);

    // amount = (int)((double)mag * unk_647738);  (40.0f)
    int amount = static_cast<int>(static_cast<double>(mag) * static_cast<double>(kDecayScale));

    // if (!mag) return 0;  (the v5==0 path after computing v12).
    if (!mag)
        return 0;

    // needWord = (needWord & ~0xF) | (mag & 0xF);
    a.needWord = (a.needWord & ~0xFu) | (static_cast<u32>(mag) & 0xFu);

    // Emit delta + consume stock (the AI->command boundary).
    if (hook) {
        hook->EmitNeedDelta(a.id, a.needWord);
        hook->AdjustStock(a.type, -amount);
    }
    return amount;
}

// Shared RandNext()%N forward-scan selector (identical in every picker):
//   start = RandNext() % n;  chosen = -1;
//   for (tries=n; ; ) { if (eligible[idx]==1){chosen=idx;break;}
//                       idx=(idx+1)%n; if(!--tries) break; }
int PickEligibleSlot(const int* eligible, int n) {
    int start = static_cast<int>(crt::RandNext()) % n;  // RandNext() always >= 0
    int idx = static_cast<int>(static_cast<u16>(start)); // (unsigned __int16)(%n)
    int chosen = -1;
    int tries = n;
    while (eligible[idx] != 1) {
        idx = (idx + 1) % n;
        if (!--tries)
            return -1;
    }
    chosen = idx;
    return chosen;
}

namespace {
// Common tail: clear `clearMask` bits in the need word, return need-id, and emit.
u8 FinishPick(NeedAgent& a, NeedsCommandHook* hook, u32 clearMask, u8 needId) {
    a.needWord &= ~clearMask;
    if (hook)
        hook->EmitNeedDelta(a.id, a.needWord);
    return needId;
}
} // namespace

// gilde.exe 0x58b4e8 — VIBE_AiNeeds_PickRandomFlagFromFourA
u8 PickRandomFlagFromFourA(NeedAgent& a, NeedsCommandHook* hook) {
    // Defaults dword_583128 = {0,0,0,0}; slot set to 1 iff the group's bits set.
    int e[4] = {0, 0, 0, 0};
    u32 w = a.needWord;
    if (w & 0xF0u)    e[0] = 1;
    if (w & 0xF00u)   e[1] = 1;
    if (w & 0x3000u)  e[2] = 1;
    if (w & 0x1C000u) e[3] = 1;

    int slot = PickEligibleSlot(e, 4);
    if (slot == -1)
        return 0;

    switch (slot) {
        case 0: return FinishPick(a, hook, 0xF0u,    2); // v7 & 0xF (clear bits 4-7)
        case 1: return FinishPick(a, hook, 0xF00u,   3); // BYTE1 &= 0xF0
        case 2: return FinishPick(a, hook, 0x3000u,  4); // BYTE1 &= 0xCF
        case 3: return FinishPick(a, hook, 0x1C000u, 5); // &= 0xFFFE3FFF
    }
    return 0;
}

// gilde.exe 0x58b614 — VIBE_AiNeeds_PickRandomFlagFromFourB
u8 PickRandomFlagFromFourB(NeedAgent& a, NeedsCommandHook* hook) {
    // Defaults dword_583138 = {0,0,0,0}.
    int e[4] = {0, 0, 0, 0};
    u32 w = a.needWord;
    if (w & 0xE0000u)   e[0] = 1;
    if (w & 0x700000u)  e[1] = 1;
    if (w & 0x1800000u) e[2] = 1;
    if (w & 0xF00u)     e[3] = 1;

    int slot = PickEligibleSlot(e, 4);
    if (slot == -1)
        return 0;

    switch (slot) {
        case 0: return FinishPick(a, hook, 0xE0000u,   6); // &= 0xFFF1FFFF
        case 1: return FinishPick(a, hook, 0x700000u,  7); // &= 0xFF8FFFFF
        case 2: return FinishPick(a, hook, 0x1800000u, 8); // &= 0xFE7FFFFF
        case 3: return FinishPick(a, hook, 0xF00u,     3); // BYTE1 &= 0xF0
    }
    return 0;
}

// gilde.exe 0x58b2f4 — VIBE_AiNeeds_PickRandomFlagFromEight
u8 PickRandomFlagFromEight(NeedAgent& a, NeedsCommandHook* hook) {
    // Defaults dword_583108 = {0}*8.
    int e[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    u32 w = a.needWord;
    if (w & 0xF0u)        e[0] = 1;
    if (w & 0xF00u)       e[1] = 1;
    if (w & 0x3000u)      e[2] = 1;
    if (w & 0x1C000u)     e[3] = 1;
    if (w & 0xE0000u)     e[4] = 1;
    if (w & 0x700000u)    e[5] = 1;  // (unk_700000 used as the 0x700000 mask)
    if (w & 0x1800000u)   e[6] = 1;
    if (w & 0x1E000000u)  e[7] = 1;

    int slot = PickEligibleSlot(e, 8);
    if (slot == -1)
        return 0;

    switch (slot) {
        case 0: return FinishPick(a, hook, 0xF0u,        2);
        case 1: return FinishPick(a, hook, 0xF00u,       3);
        case 2: return FinishPick(a, hook, 0x3000u,      4);
        case 3: return FinishPick(a, hook, 0x1C000u,     5);
        case 4: return FinishPick(a, hook, 0xE0000u,     6);
        case 5: return FinishPick(a, hook, 0x700000u,    7);
        case 6: return FinishPick(a, hook, 0x1800000u,   8);
        case 7: return FinishPick(a, hook, 0x1E000000u,  9);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// PickRandomNeedAndClearGroup / ...GroupB (consume + restock pickers).
// ---------------------------------------------------------------------------

namespace {

// Per-need restock table recovered byte-for-byte from unk_647728 @0x647728
// (10 rows of 12 bytes: int needId; float scale; int cap). We keep just the two
// fields the pickers read — scale (v22) and cap (v23) — indexed by need-id.
struct NeedRestockRow {
    float scale;  // unk_647728[12*id + 4]  (stock amount = mag * scale)
    int   cap;    // unk_647728[12*id + 8]  (bound = (cap * 0.5) before % draw)
};
const NeedRestockRow kNeedRestockTable[10] = {
    {0.0f,   1},  // 0
    {40.0f, 16},  // 1
    {15.0f, 16},  // 2
    {20.0f, 16},  // 3
    {10.0f,  4},  // 4
    {0.0f,   8},  // 5
    {8.0f,   8},  // 6
    {5.0f,   8},  // 7
    {3.0f,   4},  // 8
    {12.0f, 16},  // 9
};

// dbl_62673C / dbl_626744 — both 0.5 (the cap halving before the % draw).
const double kCapHalf = 0.5;

// VIBE_Coord_ConvertX truncates the x87 value toward zero before the (int) cast;
// for the bound/amount conversions here that is exactly `(int)d` in C++ (which
// also truncates toward zero). Factored out to mirror the binary's pattern.
int truncToZero(double d) { return static_cast<int>(d); }

// Shared body of both pickers. `needIds` maps slot 0..3 -> need-id; `eligible`
// is the already-built 4-slot eligibility array; `applyField` installs the fresh
// magnitude into the proper field of the need word for a given need-id.
template <typename ApplyField>
u8 RestockPick(NeedAgent& a, NeedsCommandHook* hook, const int eligible[4],
               const u8 needIds[4], ApplyField applyField) {
    int slot = PickEligibleSlot(eligible, 4);  // RandNext draw #1
    if (slot == -1)
        return 0;

    u8 needId = needIds[slot];
    if (needId >= 10)            // *(_BYTE*)&v29[4] < 10 guard
        return 0;

    const NeedRestockRow& row = kNeedRestockTable[needId];

    // bound = (u16)(int)(cap * 0.5); if 0 -> no draw, bail.
    u16 bound = static_cast<u16>(truncToZero(static_cast<double>(row.cap) * kCapHalf));
    u16 mag = 0;
    if (bound)
        mag = static_cast<u16>(static_cast<int>(crt::RandNext()) % bound);  // draw #2
    if (!mag)
        return 0;

    // stock amount = (int)(mag * scale).
    int amount = truncToZero(static_cast<double>(mag) * static_cast<double>(row.scale));

    // Install the fresh magnitude into the chosen field of the need word.
    a.needWord = applyField(a.needWord, needId, mag);

    if (hook) {
        hook->EmitNeedDelta(a.id, a.needWord);   // BeginDeltaPacket + AppendDeltaField(+0x2C) + Queue
        hook->AdjustStock(a.type, -amount);      // VIBE_Building_AdjustStockAndNotify(type, -amount)
    }
    return needId;
}

} // namespace

// gilde.exe 0x58aea8 — VIBE_AiNeeds_PickRandomNeedAndClearGroup
//   (__usercall: al=, eax=person, ecx=)
u8 PickRandomNeedAndClearGroup(NeedAgent& a, NeedsCommandHook* hook) {
    // Eligibility seeds dword_5830E0 = {0,0,0,0}; slot set to 1 iff group bits set.
    int e[4] = {0, 0, 0, 0};
    u32 w = a.needWord;
    // The seed defaults to 0; a slot becomes eligible (==1) exactly when the
    // group's need bits are CLEAR — i.e. this picks a need to newly raise.
    if ((w & 0xF0u) == 0)    e[0] = 1;  // *(person+44) & 0xF0
    if ((w & 0xF00u) == 0)   e[1] = 1;  // *(person+45) & 0xF
    if ((w & 0x3000u) == 0)  e[2] = 1;  // *(person+45) & 0x30
    if ((w & 0x1C000u) == 0) e[3] = 1;  // *(person+44) & 0x1C000

    // dword_5830F0 = {2,3,4,5}.
    static const u8 needIds[4] = {2, 3, 4, 5};

    return RestockPick(a, hook, e, needIds, [](u32 word, u8 needId, u16 mag) -> u32 {
        switch (needId) {
            case 2: return (word & 0xFFFFFF0Fu) | ((mag & 0xFu) << 4);   // field 0xF0
            case 3: return (word & 0xFFFFF0FFu) | ((mag & 0xFu) << 8);   // field 0xF00
            case 4: return (word & 0xFFFFCFFFu) | ((mag & 0x3u) << 12);  // field 0x3000
            case 5: return (word & 0xFFFE3FFFu) | ((mag & 0x7u) << 14);  // field 0x1C000
            default: return word;
        }
    });
}

// gilde.exe 0x58b0cc — VIBE_AiNeeds_PickRandomNeedAndClearGroupB
//   (__usercall: al=, eax=person, ecx=)
u8 PickRandomNeedAndClearGroupB(NeedAgent& a, NeedsCommandHook* hook) {
    // Eligibility seeds dword_5830F4 = {0,0,0,0}.
    int e[4] = {0, 0, 0, 0};
    u32 w = a.needWord;
    if ((w & 0x1C000u) == 0)   e[0] = 1;  // *(person+44) & 0x1C000
    if ((w & 0xE0000u) == 0)   e[1] = 1;  // *(person+46) & 0xE  == needWord & 0xE0000
    if ((w & 0x700000u) == 0)  e[2] = 1;  // *(person+46) & 0x70 == needWord & 0x700000
    if ((w & 0x1800000u) == 0) e[3] = 1;  // *(person+46) & 0x180 (word) == needWord & 0x1800000

    // dword_583104 = {5,6,7,8}.
    static const u8 needIds[4] = {5, 6, 7, 8};

    return RestockPick(a, hook, e, needIds, [](u32 word, u8 needId, u16 mag) -> u32 {
        switch (needId) {
            case 5:  return (word & 0xFE7FFFFFu) | ((mag & 0x3u) << 23);  // field 0x1800000
            case 6:  return (word & 0xFFF1FFFFu) | ((mag & 0x7u) << 17);  // field 0xE0000
            case 7:  return (word & 0xFF8FFFFFu) | ((mag & 0x7u) << 20);  // field 0x700000
            case 8:  return (word & 0xFE7FFFFFu) | ((mag & 0x3u) << 23);  // field 0x1800000 (== need 5)
            default: return word;
        }
    });
}

} // namespace guild::ai
