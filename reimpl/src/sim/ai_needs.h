#pragma once
// ===========================================================================
// ai_needs.{h,cpp} — 1:1 reconstruction of the AI "needs/score" master table
// builder from gilde.exe (Die Gilde / Europa 1400).
//
//   gilde.exe 0x4764e8 — VIBE_AiNeeds_BuildScoreTable  (__usercall, eax = haveIni)
//
// This is the giant (7961-byte, 2605-instruction) registrar that defines the AI's
// 60-method decision catalog. For each method it:
//   1. zeroes a 148-byte stack record   (rep stos, memset 0);
//   2. writes the method's FIXED fields:
//        +0   (1)   id            (1..60, NOT strictly sequential — see table)
//        +1   (32)  name string   (NUL-terminated, copied from a code-literal)
//        +36  (4)   scorer  fn    (var_88)  — the per-method SCORE evaluator
//        +40  (4)   execA   fn    (var_84)  — the per-method "can/eval" action fn
//        +44  (4)   execB   fn    (var_80)  — the per-method "apply/perform" fn
//        +144 (2)   flag word     (var_1C)  — a per-method category/eligibility mask
//   3. calls VIBE_AiMethod_RegisterFromIni(record /*eax*/, haveIni /*edx*/) which
//      reads the method's [name] INI section ("ShortDesire%li"/"ShortChange%li" and
//      "LongDesire%li"/"LongChange%li", li=1..4) into the record's desire slots
//      (+48..+143) and commits the 148-byte record into the catalog byte_B57210
//      at [148*id];
//   4. if RegisterFromIni returns 0 (id-gate reject), ABORTS the whole build and
//      returns 0.  After all 60 succeed, returns 1.
//
// `haveIni` (the eax argument, forwarded verbatim as RegisterFromIni's edx) selects
// whether the desire slots are filled from the INI ([name] sections of
// f3_ai_method.ini) or left zero. The caller VIBE_AiMethod_LoadDataFile @0x468a40
// passes dword_63C7D8 here: when the f3_ai_method.ini authoring file is present it
// passes nonzero (build-from-INI, then WRITE the gzip catalog ai_data.dfn); the
// SHIPPED install has no f3_ai_method.ini, so it passes 0 (build the fixed fields,
// then READ the desire slots back from Resources/gamedata/ai/AI_DATA.DFN — a gzip
// stream of 61 x 73-byte records).
//
// SCOPE / FIDELITY
//   * GENUINE (reconstructed 1:1 here):
//       - the EXACT 60-method definition table (id, name, scorer/execA/execB fn
//         identity, flag word) — golden-pinned from the binary's code literals and
//         cross-checked against the shipped AI_DATA.DFN record names/order;
//       - the per-record fixed-field layout (offsets above) and the build loop's
//         "abort on first RegisterFromIni failure / return 1 on full success".
//   * REUSED (already reconstructed, not re-defined here):
//       - VIBE_AiMethod_RegisterFromIni (0x468f6c) and AiNeeds_LookupAttributeIndex
//         (0x4794e4) live in sim/aimethod_recon3_registry.{h,cpp}; the desire-slot
//         fill + commit + "has any effect" validation come from there.
//       - The INI parse is guild::io::IniProfile (a faithful kernel32 private-
//         profile reconstruction, NOT a tech boundary). The e2e wires an IniSource
//         on top of it. The DFN gzip stream is read via guild::compress::Gunzip.
//   * FN-PTR IDENTITY: the original stores live 32-bit code addresses at +36/+40/
//       +44. A portable build cannot store gilde.exe VAs as callable pointers, so
//       the catalog records the ORIGINAL gilde.exe addresses as opaque u32 tokens
//       (golden-pinnable, and the identity that drives the planner's tbl[9]/tbl[10]
//       dispatch). They are NOT serialized to the DFN (the DFN holds only id+name+
//       desire slots, 73 bytes/record — verified against the shipped file).
// ===========================================================================
#include "guild/common/types.h"
#include "sim/aimethod_recon3_registry.h"

#include <array>
#include <cstddef>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Catalog record layout (gilde.exe byte_B57210, 148-byte stride, 61 records).
// Slot 0 (id 0) is the unused/disabled slot; methods occupy ids 1..60.
// ---------------------------------------------------------------------------
constexpr int kAiNeedsRecordStride = 148; // 0x94 (== kAiMethodRecordStride)
constexpr int kAiNeedsRecordCount  = 61;  // includes the id-0 unused slot
constexpr int kAiNeedsMethodCount  = 60;  // defined methods (the build-loop count)

// Fixed-field byte offsets the builder writes (the desire slots at +48..+143 are
// filled by RegisterFromIni and are documented in aimethod_recon3_registry.h).
constexpr int kAiNeedsOffId      = 0;   // var_AC : id byte
constexpr int kAiNeedsOffName    = 1;   // 32-byte NUL-terminated name
constexpr int kAiNeedsOffScorer  = 36;  // var_88 : score eval fn (dword)
constexpr int kAiNeedsOffExecA   = 40;  // var_84 : eval/can-action fn (dword)
constexpr int kAiNeedsOffExecB   = 44;  // var_80 : apply/perform fn (dword)
constexpr int kAiNeedsOffFlag    = 144; // var_1C : category/eligibility word
constexpr int kAiNeedsNameMax    = 32;

// ---------------------------------------------------------------------------
// One row of the fixed method-definition table (the code literals at 0x4764fd..
// 0x4783e2). `scorer`/`execA`/`execB` are the ORIGINAL gilde.exe code addresses
// (opaque dispatch tokens). `flag` is the var_1C word.
// ---------------------------------------------------------------------------
struct AiNeedsMethodDef {
    u8          id;        // +0   method id (the explicit var_AC immediate)
    const char* name;      // +1   INI section name / record name
    u32         scorer;    // +36  gilde.exe VA of the score evaluator
    u32         execA;     // +40  gilde.exe VA of the eval/can-action fn
    u32         execB;     // +44  gilde.exe VA of the apply/perform fn
    u16         flag;      // +144 category/eligibility word
};

// The exact 60-entry definition table, in BUILD ORDER (the order the binary calls
// RegisterFromIni). Note the build order is NOT id order (e.g. "Objekt Kaufen
// (Markt)" id=12 is built late; "Buergermeister Kerker" id=60 is built mid-list).
extern const std::array<AiNeedsMethodDef, kAiNeedsMethodCount> kAiNeedsMethodDefs;

// ---------------------------------------------------------------------------
// The reconstructed catalog. Each entry mirrors a 148-byte record: the fixed
// fields the builder writes plus the desire slots RegisterFromIni fills.
// ---------------------------------------------------------------------------
struct AiNeedsCatalogEntry {
    u8           id   = 0;
    char         name[kAiNeedsNameMax] = {};
    u32          scorer = 0;
    u32          execA  = 0;
    u32          execB  = 0;
    u16          flag   = 0;
    // Desire slots (from RegisterFromIni): 4 short + 4 prev-mirror + 4 long.
    AiDesireSlot shortSlots[kAiMethodDesireSlots];
    AiDesireSlot prevSlots[kAiMethodDesireSlots];
    AiDesireSlot longSlots[kAiMethodDesireSlots];
};

// 61-entry catalog (index == id; index 0 is the unused slot).
using AiNeedsCatalog = std::array<AiNeedsCatalogEntry, kAiNeedsRecordCount>;

// ---------------------------------------------------------------------------
// IniSourceFactory: per-method the builder needs an IniSource bound to that
// method's INI section (RegisterFromIni reads keys from the [method name] section).
// The factory returns the IniSource to use for `def`. For the shipped (no-INI)
// path pass a factory that returns an all-null IniSource (or use the
// `haveIni=false` overload below).
//
// `ctx` is opaque caller state (e.g. a guild::io::IniProfile + scratch buffers).
// ---------------------------------------------------------------------------
struct AiNeedsIniBinder {
    // Bind `ini` to read `def`'s section. Return false to skip INI for this method
    // (treated as "no keys present"); true on success.
    bool (*bind)(const AiNeedsMethodDef& def, IniSource& ini, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// ---------------------------------------------------------------------------
// gilde.exe 0x4764e8 — VIBE_AiNeeds_BuildScoreTable.
//
// Builds the whole catalog in `out`. `haveIni` is the eax argument (forwarded to
// RegisterFromIni's edx): when true, `binder` is used to fill each method's desire
// slots from its INI section; when false, the desire slots stay zero (slots are
// later overwritten from the DFN by VIBE_AiMethod_LoadDataFile).
//
// Returns 1 on full success (all 60 RegisterFromIni calls returned 1), 0 if any
// method's RegisterFromIni rejected the record (the build aborts there — matching
// the binary's early `return 0`). On abort, `out` holds the partially-built
// catalog (entries up to and including the failing id's fixed fields).
//
// NOTE: with the genuine table all ids are in 1..60, so RegisterFromIni never
// rejects on the id gate; the function returns 1 over the real data.
// ---------------------------------------------------------------------------
int AiNeeds_BuildScoreTable(bool haveIni, const AiNeedsIniBinder& binder,
                            AiNeedsCatalog& out);

// Convenience: the no-INI shipped path (haveIni == false). Equivalent to calling
// the above with a binder that supplies no keys.
int AiNeeds_BuildScoreTable(AiNeedsCatalog& out);

// ---------------------------------------------------------------------------
// DFN overlay (the VIBE_AiMethod_LoadDataFile READ path, 0x468a40).
//
// After BuildScoreTable has written the fixed fields, the shipped loader reads the
// 61 x 73-byte records from the (gzip-decompressed) ai_data.dfn and overlays the
// id, name and 4 short + 4 long desire slots onto the catalog (then mirrors the 4
// short slots into the prev vector — the MemMove(+80,+48,32) at 0x468f39). The
// fixed fn-ptr/flag fields are NOT in the DFN and are preserved.
//
// `dfn` is the DECOMPRESSED catalog bytes (61*73 = 4453). Returns the number of
// records overlaid (stops at the first short read; 61 on a complete file).
// ---------------------------------------------------------------------------
constexpr int kAiNeedsDfnRecordBytes = 73; // 1 + 32 + (1+4)*4 short + (1+4)*4 long
constexpr int kAiNeedsDfnTotalBytes  = kAiNeedsDfnRecordBytes * kAiNeedsRecordCount;

int AiNeeds_OverlayFromDfn(const u8* dfn, std::size_t dfnLen, AiNeedsCatalog& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x468a40 — VIBE_AiMethod_LoadDataFile (the SHIPPED read path, the only
// path reachable on a real install since f3_ai_method.ini is not present).
//
// Reconstructed control flow of the dword_63C7D8 == 0 branch:
//   1. open "/gamedata/ai/ai_data.dfn" "rb" via the VFS;
//   2. VIBE_AiNeeds_BuildScoreTable(0)  — build the fixed-field catalog;
//   3. stream 61 records back into the catalog (the field schedule + MemMove
//      prev-mirror) — here, AiNeeds_OverlayFromDfn over the DECOMPRESSED bytes;
//   4. return 1 on a complete load, 0 on open/read failure.
//
// The VFS open + gzip-decompress is the caller's job (the game's file layer is the
// VFS; the .dfn is a gzip stream — use guild::compress::Gunzip). This helper takes
// the already-decompressed DFN bytes, runs steps 2-4, and reports success.
//
// `out` receives the populated catalog. Returns 1 iff BuildScoreTable returned 1
// AND all 61 records overlaid (matching the binary's loop bound of 61); else 0.
//
// HANDOFF (rule 13): VIBE_GameLogic_InitGuardState (0x4520d0, in
// world/guildstate_recon.cpp) calls LoadDataFile and passes its return as
// `aiDataFileLoaded` into GameLogicInitGuardState. The engine-init seam in
// app/engine_init_app_recon.cpp routes InitGuardState through the
// `gameLogicInitGuardState` std::function hook. To complete the live wiring, that
// hook should: gunzip Resources/gamedata/ai/AI_DATA.DFN via the VFS, call
// AiNeeds_LoadDataFile, and feed the boolean to GameLogicInitGuardState. Both files
// are owned by other modules; this entry point is the integration point.
// ---------------------------------------------------------------------------
int AiNeeds_LoadDataFile(const u8* decompressedDfn, std::size_t dfnLen,
                         AiNeedsCatalog& out);

} // namespace guild::sim
