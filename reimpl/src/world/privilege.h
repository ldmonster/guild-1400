#pragma once
// Privilege bit checks. The ~30 VIBE_Privilege_Panel* / VIBE_Panel_ShowPrivileges
// functions are GUI dialogs (Form/Text/network commands) and are DEFERRED (see
// the module report). The load-bearing, testable logic underneath them is the
// privilege-availability BIT TEST: a privilege action is gated on a flag bit in
// the player's privilege bitfield and/or the city's Privilegien[11] flags.
//
// Recovered bit-test patterns (from the panel dispatchers):
//   VIBE_Privilege_SendSimpleCmd 0x561700:  (*(person+457) & 4) != 0   -> blocked
//   VIBE_Panel_ShowPrivileges    0x55fe60:  (dword_12CEAD8[..] & 0x100) -> office
// The city privilege flags live in CityRecord::privileges[11] (gilde.exe +300).
#include "guild/common/types.h"

namespace guild::world {

// gilde.exe — privilege flag test (the `(flags & mask) != 0` idiom the panels
// use to enable/disable a privilege action). Returns true iff every bit in
// `mask` is set in `flags`. (Single-bit masks are the common case.)
inline bool PrivilegeHasFlag(u32 flags, u32 mask) {
    return (flags & mask) == mask;
}

// City privilege flags: CityRecord::privileges[11] (gilde.exe +300). Each entry
// is one privilege-group's flag byte. Tests whether privilege `index` (0..10) is
// enabled for a city given its 11-byte privilege array.
inline bool CityPrivilegeEnabled(const u8 privileges[11], int index) {
    if (index < 0 || index >= 11)
        return false;
    return privileges[index] != 0;
}

// The two concrete masks the dispatchers test, named for reuse/clarity:
//   kPrivBitBlackmailImmune == 4    : person+457 & 4 (immune-to-simple-cmd bit)
//   kPrivBitHoldsOffice     == 0x100: player flag dword bit 8 (holds an office)
constexpr u32 kPrivBitBlackmailImmune = 0x4;
constexpr u32 kPrivBitHoldsOffice     = 0x100;

// gilde.exe 0x561700 (portion) — VIBE_Privilege_SendSimpleCmd availability gate.
// Returns true iff the target may be acted on: the immune bit (+457 & 4) is
// CLEAR. (The original returns -127 "blocked" when the bit is set.)
inline bool PrivilegeSimpleCmdAllowed(u32 targetFlags457) {
    return !PrivilegeHasFlag(targetFlags457, kPrivBitBlackmailImmune);
}

// ===========================================================================
// SendSimpleCmd / SendBuildCmd dispatch return codes (gilde.exe 0x561700 /
// 0x561a74). Both privilege-command dispatchers share the same decision shape:
//   * If the dispatch SUBJECT is an "important" kind (record+2 == 6 or 7) the
//     panel opens the office-overview window (a different interaction); for
//     SendSimpleCmd this returns 2, and -127 if the picked office holder is
//     immune (+457 & 4); SendBuildCmd returns 1 (overview shown) or 0 (cancel).
//   * Otherwise the action targets a concrete person (record at +532). When that
//     person exists the command is enqueued (return 16); when it doesn't, the
//     dispatcher returns 96 ("no target").
// These are the exact return-code rules (the command enqueue + GUI are deferred).
// ===========================================================================
enum class PrivilegeKind : int { kSimpleCmd, kBuildCmd };

constexpr int kPrivRetCommitted   = 16;   // command enqueued against the target
constexpr int kPrivRetNoTarget    = 96;   // target person id resolved to nothing
constexpr int kPrivRetSimpleOffice = 2;   // SimpleCmd: office-overview path
constexpr int kPrivRetBuildOffice  = 1;   // BuildCmd:  office-overview shown
constexpr int kPrivRetBuildCancel  = 0;   // BuildCmd:  overview cancelled
constexpr int kPrivRetImmune      = -127; // SimpleCmd: picked holder is immune

// gilde.exe 0x561700 — SendSimpleCmd return code.
//   subjectKind 6/7 -> office path: officeHolderImmune ? -127 : 2.
//   else            -> targetExists ? 16 : 96.
int PrivilegeSimpleCmdResult(int subjectKindByte, bool officeHolderImmune,
                             bool targetExists);

// gilde.exe 0x561a74 — SendBuildCmd return code.
//   subjectKind 6/7 -> office path: overviewShown ? 1 : 0.
//   else            -> targetExists ? 16 : 96.
int PrivilegeBuildCmdResult(int subjectKindByte, bool overviewShown,
                            bool targetExists);

} // namespace guild::world
