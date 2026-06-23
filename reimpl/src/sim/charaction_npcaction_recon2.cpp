// charaction_npcaction_recon2 — see charaction_npcaction_recon2.h for the module
// overview, the per-routine address map and the (re-audited) OMIT rationale.
//
// Each routine carries its gilde.exe address; record accesses use the He_* / HeR_*
// byte-faithful accessors. Cross-cluster leaves (the stat table, the handler-pool
// scan, the free-entry release, the real-time-mode global) go through the inert
// CharActionRecon2Hooks bridge. NpcClock() supplies the 14-byte global clock image
// (qword_13CE852); GameTimeAdvance is reused.
#include "sim/charaction_npcaction_recon2.h"

#include "sim/gametime.h"   // GameTimeAdvance
#include "sim/npcaction.h"  // NpcClock()

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent" / no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InFindFirstByFilter(int, int, i32) { return nullptr; }
HeRecord* InFindNextMatching()               { return nullptr; }
i32       InFreeHandlerEntry(HeRecord*)      { return 0; }
u8        InStatTableByte(i32, int)          { return 0; }
i32       InRealTimeModeFlag()               { return 0; }

const CharActionRecon2Hooks kInert{
    InFindFirstByFilter,
    InFindNextMatching,
    InFreeHandlerEntry,
    InStatTableByte,
    InRealTimeModeFlag,
};

const CharActionRecon2Hooks* g_hooks = &kInert;

// gilde.exe 0x61EB44 — dbl_61EB44 = 252.0 (the per-NPC "full" saturation cap).
constexpr double kDrinkSaturationCap = 252.0;
// gilde.exe 0x61EB4C — flt_61EB4C = 0x3CC30C31 ~= 1/42 (the duration scale).
constexpr float  kDrinkDurationScale = 0.0238095242f;

// Stamp the full 14-byte global clock image into an HeRecord clock slot.
inline void StampAppt(HeRecord* h)  { He_ApptTime(h)  = NpcClock(); }
inline void StampSaved(HeRecord* h) { He_SavedTime(h) = NpcClock(); }

// record+8 — the person/city row index addressing the 536-stride stat table.
inline u16 RowIndex(HeRecord* h) { return *reinterpret_cast<u16*>(HeBytes(h) + 8); }

// sub-method byte: HIBYTE of the packed dword at record+171  (== `*(int*)(h+171) >> 24`).
inline int SubMethod(HeRecord* h) {
    return *reinterpret_cast<i32*>(HeBytes(h) + 171) >> 24;
}

// record+172 (u8) action-type byte; record+173 (u8) duration byte.
inline u8& ActionTypeByte(HeRecord* h) { return *reinterpret_cast<u8*>(HeBytes(h) + 172); }
inline u8& DurationByte(HeRecord* h)   { return *reinterpret_cast<u8*>(HeBytes(h) + 173); }

} // namespace

void SetCharActionRecon2Hooks(const CharActionRecon2Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInert;
}
const CharActionRecon2Hooks& GetCharActionRecon2Hooks() { return *g_hooks; }

// ===========================================================================
// gilde.exe 0x4d21ec — VIBE_CharAction_DrinkInit
//   (__usercall: eax=record@a1, edi=a2, esi=a3)
// ===========================================================================
i32 CharRecon2DrinkInit(HeRecord* h) {
    const CharActionRecon2Hooks& hk = GetCharActionRecon2Hooks();

    const i32 row = RowIndex(h);             // v4 = *(u16*)(a1+8)
    const int sub = SubMethod(h);            // *(int*)(a1+171) >> 24

    // Gate 1: the NPC is already saturated ("full").
    //   if ((double)(u8)byte_12CE990[536*row + sub] >= dbl_61EB44) -> free + bail.
    if (static_cast<double>(hk.statTableByte(row, sub)) >= kDrinkSaturationCap)
        return hk.freeHandlerEntry(h);

    // Gate 2: at most one type-96 drinker per row. Scan the pool; skip ourselves;
    //   if any *other* matching handler exists, free this entry and bail.
    //   FirstHandlerByFilter = VIBE_He_FindFirstHandlerByFilter(2, 2, v4, 0, 96)
    HeRecord* it = hk.findFirstByFilter(2, 2, row);
    if (it) {
        // while (it == a1) it = FindNextMatchingHandler(); if (!it) goto LABEL_5;
        while (it == h) {
            it = hk.findNextMatching();
            if (!it) goto proceed;           // only match was ourselves -> proceed
        }
        return hk.freeHandlerEntry(h);       // another drinker present -> bail
    }
proceed:
    // v6 = (double)statByte * flt_61EB4C + 1.0; ConvertX truncates to (int)v6.
    //   (Re-reads the table with the same row/sub, exactly as the original does.)
    const double scaled =
        static_cast<double>(hk.statTableByte(row, sub)) * kDrinkDurationScale + 1.0;

    ActionTypeByte(h) = 21;                   // *(u8*)(a1+172) = 21
    He_State(h)       = 0;                     // *(dword*)(a1+112) = 0
    const i32 realTime = hk.realTimeModeFlag();    // v7 = dword_63C7B8
    DurationByte(h) = static_cast<u8>(2 * static_cast<int>(scaled) + 4); // +173

    StampAppt(h);                             // *(qword*)(a1+82) = clock ...

    int v8, v9;                               // advance operands
    if (realTime) { v8 = 1; v9 = 0; }
    else          { v9 = 5; v8 = 0; }
    // result = VIBE_GameTime_Advance(a1+82, v9 /*days*/, v8 /*seconds*/, 0 /*minutes*/)
    const i32 result = GameTimeAdvance(&He_ApptTime(h), v9, v8, 0);

    StampSaved(h);                            // *(qword*)(a1+68) = clock ...
    return result;
}

} // namespace guild::sim
