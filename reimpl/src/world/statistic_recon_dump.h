#pragma once
// guild::world — Statistics ROUND-DUMP text formatting (the "statistic dump in
// round %i" tab-separated NPC dump).  Reconstructed 1:1 from gilde.exe.
//
// Cluster (worklist VIBE_Statistic.txt):
//   0x594fd0 VIBE_Statistic_DumpRoundHeader       -> StatDumpRoundHeader
//   0x594ff8 VIBE_Statistic_DumpNpcIdentity       -> StatDumpNpcIdentity
//   0x5950a4 VIBE_Statistic_DumpNpcAttributes     -> StatDumpNpcAttributes
//   0x595168 VIBE_Statistic_DumpNpcNeeds          -> StatDumpNpcNeeds
//   0x5951ac VIBE_Statistic_DumpNpcTraits         -> StatDumpNpcTraits
//   0x595208 VIBE_Statistic_DumpNpcSkills         -> StatDumpNpcSkills
//   0x595260 VIBE_Statistic_DumpNpcInventory      -> StatDumpNpcInventory
//   0x5952dc VIBE_Statistic_DumpNpcRecord         -> StatDumpNpcRecord
//   0x5953bc VIBE_Statistic_DumpRoundFooter       -> StatDumpRoundFooter
//
// Each routine is a thin `sprintf` over an NPC record laid out exactly as the
// original 536-byte person record (the same record word_12CE910 indexes).  The
// originals read raw byte/word/dword fields at fixed offsets and look up several
// localized-string tables (dword_8C4774 .. dword_8C3E0C, dword_8C3EE4) that live in
// the loaded game-text subsystem (a coupled leaf here).  Those table lookups, the
// "current round" counter (qword_13CE852), and the two coupled aggregations
// (skills wealth/currency, inventory record-by-id) are routed through an injectable
// StatDumpEnv hook so the byte-exact formatting is testable in isolation; with the
// default env the table lookups return "" and the aggregations return 0, matching an
// unloaded/headless build.  NEVER faked: the math reproduced here (bitfield unpacks,
// fixed-point reads, offset arithmetic) is byte-for-byte the original.

#include "guild/common/types.h"

#include <cstddef>

namespace guild::world {

using guild::i32;
using guild::u8;
using guild::u16;
using guild::u32;

// ---------------------------------------------------------------------------
// Injectable environment for the coupled leaves the dump routines touch.
// All defaults are inert (empty strings / zero), so a headless build produces a
// well-formed dump line with placeholder fields.
// ---------------------------------------------------------------------------
struct StatDumpEnv {
    virtual ~StatDumpEnv() = default;

    // qword_13CE852 — the current statistics round counter (printed in every line).
    virtual i32 CurrentRound() const { return 0; }

    // aDummyNpc / dword_649CB4 — identity "kind" name.
    //   typeByte < 10 : &aDummyNpc[24*typeByte]  ("DUMMY_NPC" is record 0)
    //   typeByte >=10 : (const char*)dword_649CB4 (a dynamic name pointer)
    virtual const char* IdentityKindName(u8 typeByte) const {
        (void)typeByte;
        return "";
    }

    // dword_8C4774[i] : profession / class name (indexed by byte +9).
    virtual const char* ClassName(u8 idx) const { (void)idx; return ""; }
    // dword_8C4768[i] : home/origin name (indexed by byte +12).
    virtual const char* OriginName(u8 idx) const { (void)idx; return ""; }
    // dword_8C3AF0[i] : religion name (indexed by byte +13).
    virtual const char* ReligionName(u8 idx) const { (void)idx; return ""; }
    // dword_8C3B48[i] : status name (indexed by byte +356).
    virtual const char* StatusName(u8 idx) const { (void)idx; return ""; }
    // dword_8C3E0C[i] : location name (indexed by byte +357).
    virtual const char* LocationName(u8 idx) const { (void)idx; return ""; }
    // dword_8C3EE4[i] : trait name (indexed by trait bytes +358..+361).
    virtual const char* TraitName(u8 idx) const { (void)idx; return ""; }

    // DumpNpcSkills aggregations (coupled to the wealth/iterator subsystems):
    //   VIBE_Person_SumCurrencyHeld(record)       @0x59152c
    //   VIBE_Person_ComputeTotalWealth(index,rec) @0x591f7c
    virtual i32 SumCurrencyHeld(const u8* record) const { (void)record; return 0; }
    virtual i32 ComputeTotalWealth(u16 personIndex, const u8* record) const {
        (void)personIndex; (void)record; return 0;
    }

    // DumpNpcInventory record-by-id lookup: VIBE_Person_FindRecordById @0x58bc6c.
    // Returns the resolved record pointer (id at +4, name at +48) or nullptr.
    virtual const u8* FindRecordById(i32 id) const { (void)id; return nullptr; }
};

// ---------------------------------------------------------------------------
// gilde.exe 0x594fd0 — VIBE_Statistic_DumpRoundHeader.
//   sprintf(out, "++++++++++ Begin statistic dump in round %i +++++++++", round);
// Writes into `out` (caller-owned, originally a 1028-byte stack buffer).
// ---------------------------------------------------------------------------
int StatDumpRoundHeader(char* out, const StatDumpEnv& env);

// gilde.exe 0x5953bc — VIBE_Statistic_DumpRoundFooter (same shape, "End ...").
int StatDumpRoundFooter(char* out, const StatDumpEnv& env);

// gilde.exe 0x594ff8 — VIBE_Statistic_DumpNpcIdentity (__usercall eax=record,edx=out).
//   "%i\t%i\t%i\t%s\t%s\t%i\t%s\t%i\t%s\t%s\t%s\t%s\t"
//   round, id(+4), marker(+0), kind, name(+48), byte+8, class(+9),
//   word+10, origin(+12), religion(+13), status(+356), location(+357).
int StatDumpNpcIdentity(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x5950a4 — VIBE_Statistic_DumpNpcAttributes (__usercall eax=record,edx=out).
// Two appended sprintf calls: six 5.2f floats + an int, then nine packed bitfields
// from the dword at +44.  Appends to *out (the 2nd write starts at out+strlen(out)).
int StatDumpNpcAttributes(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x595168 — VIBE_Statistic_DumpNpcNeeds (__usercall eax=record,edx=out).
//   "%i\t%i\t%i\t%i\t%i\t" : bytes +128..+132.
int StatDumpNpcNeeds(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x5951ac — VIBE_Statistic_DumpNpcTraits (__usercall eax=record,edx=out).
//   "%s\t%s\t%s\t%s\t" : TraitName(byte +358..+361).
int StatDumpNpcTraits(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x595208 — VIBE_Statistic_DumpNpcSkills (__usercall eax=record,esi=out).
//   "%i\t%i\t%i\t%i\t%i\t%i\t%i\t%i\t" : six skill dwords (+0x190..+0x1A4),
//   then SumCurrencyHeld(record), then ComputeTotalWealth(marker, record).
// (Original evaluates ComputeTotalWealth first, then SumCurrencyHeld; both are
// pushed so argument order in the format is currency, wealth — preserved here.)
int StatDumpNpcSkills(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x595260 — VIBE_Statistic_DumpNpcInventory (__usercall eax=record,edx=out).
// Walks the 8 equipment-slot ids at +92, +96 ... (+92 + 4*k, k=0..7), resolves each
// via FindRecordById, formats "%i\t%s" (id +4, name +48; or -1/"NIEMAND" when
// unresolved) into a scratch buffer, and appends it to *out.  Returns the last byte
// copied (the original's `al`), preserved here.
int StatDumpNpcInventory(const u8* record, char* out, const StatDumpEnv& env);

// gilde.exe 0x5952dc — VIBE_Statistic_DumpNpcRecord (__usercall eax=record).
// If record && record.marker(+0)!=0xFFFF: zero-fills out[0..8120), then appends
// identity/attributes/needs/traits/skills/inventory (each at out+strlen(out)).
// `out` must hold >= 8124 bytes (the original's stack buffer).  Returns nonzero when
// a record was dumped (the original returns the low byte of `al`).
int StatDumpNpcRecord(const u8* record, char* out, const StatDumpEnv& env);

}  // namespace guild::world
