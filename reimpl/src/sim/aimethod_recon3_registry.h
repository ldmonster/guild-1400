// aimethod_recon3_registry.h — 1:1 reconstruction of the AI-method *registry*
// loaders from gilde.exe (Die Gilde / Europa 1400):
//
//   gilde.exe 0x4794e4 — VIBE_AiNeeds_LookupAttributeIndex (attribute-name -> index)
//   gilde.exe 0x468f6c — VIBE_AiMethod_RegisterFromIni      (build one method record)
//   gilde.exe 0x468a40 — VIBE_AiMethod_LoadDataFile         (serialize the 61-record
//                                                            method catalog)
//
// SCOPE / FIDELITY NOTE
// --------------------
// These three functions populate the AI-method catalog `byte_B57210` (148-byte
// stride x 61 records, modeled as guild::ai::MethodEntry in src/ai/types.h). They
// mix GENUINE GAME LOGIC with coupled platform leaves:
//
//   * GENUINE (reconstructed 1:1 here):
//       - the attribute-name -> index map (LookupAttributeIndex), exact string set
//         and ordinals;
//       - the per-record on-disk / serialized FIELD LAYOUT (the 8 desire/change
//         slots: 4 "short" + 4 "long", each {attrIndex:i8, change:f32});
//       - RegisterFromIni's id-range gate, the per-slot fill rule (missing key ->
//         attr=-1, change=0), the "method has no effect" validation loop, and the
//         MemMove(+80, +48, 32) prev-vector mirror;
//       - LoadDataFile's record count (61) and exact serialization field order.
//
//   * COUPLED LEAVES (NOT reconstructed; abstracted as caller-supplied inputs/hooks):
//       - INI reading (Win32 GetPrivateProfileStringA — see RULE-6 FLAG below);
//       - VFS streaming (VIBE_Vfs_OpenFile/ReadStream/WriteStream/CloseStream — the
//         game's own file layer, plumbing, not decision logic);
//       - VIBE_AiNeeds_BuildScoreTable (0x4764e8) and the loader-bar UI thunk.
//
// RULE-6 FLAG: VIBE_AiMethod_RegisterFromIni reads its data through Win32
//   GetPrivateProfileStringA (kernel32 .ini parsing). That is NOT one of the
//   pre-approved tech swaps (Win32->SDL covers window/input/timers, not the .ini
//   parser). The INI source is therefore exposed as a caller-provided callback
//   (IniSource) rather than being swapped to a third-party INI library on our own
//   judgment. Flagged for the user to decide the substitution.
//
// Every kernel carries its gilde.exe provenance (address + symbol).

#pragma once

#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;
using f64 = double;

// ---------------------------------------------------------------------------
// gilde.exe 0x4794e4 — VIBE_AiNeeds_LookupAttributeIndex(eax = name string)
//
// Case-insensitive (VIBE_Util_StrCmpNoCase) map from a need/attribute NAME to its
// catalog ordinal. The original tests the names in this exact order and returns the
// first 0-based match; an unmatched name logs a message and returns -1.
//
//   APS=0  UNVERSEHRTHEIT=1  WOHNUNG=2  GELD=3  BERUF=4  VERGNUEGEN=5
//   ANSEHEN=6  AMT=7  BILDUNG=8  RECHTSCHAFFENHEIT=9  GEMEINHEIT=10
//   SICHERHEIT=11  FORTPFLANZUNG=12  TRAEGHEIT=13   (else -1)
//
// Returned as a signed char in the binary (al). RegisterFromIni stores it into the
// record's attr-index byte slots and treats < 0 as "no attribute".
// ---------------------------------------------------------------------------
i8 AiNeeds_LookupAttributeIndex(const char* name);

// Number of recognized attribute names (0..13 => 14 names).
constexpr int kAiAttributeCount = 14;

// ---------------------------------------------------------------------------
// AI-method record (gilde.exe byte_B57210, 148-byte stride, 61 records).
//
// This view names the fields RegisterFromIni / LoadDataFile actually touch. It is a
// projection of guild::ai::MethodEntry (src/ai/types.h) — same 148-byte stride — but
// expressed with the desire/change slot structure the loaders use so the layout can
// be golden-tested independently. Offsets verified against the get_bytes / decompile
// field reads of both loaders.
//
//   +0   (1)   id
//   +1   (32)  name string (NUL-terminated)
//   +48  (1)   shortAttr[0]   +52 (4) shortChange[0]   (slot 0)
//   +56  (1)   shortAttr[1]   +60 (4) shortChange[1]   (slot 1)
//   +64  (1)   shortAttr[2]   +68 (4) shortChange[2]   (slot 2)
//   +72  (1)   shortAttr[3]   +76 (4) shortChange[3]   (slot 3)
//   +80..+111  prev-vector mirror = MemMove(+80, +48, 32)   (copy of the 4 short
//              slots; written by RegisterFromIni & LoadDataFile after fill)
//   +112 (1)   longAttr[0]    +116 (4) longChange[0]    (slot 0)
//   +120 (1)   longAttr[1]    +124 (4) longChange[1]    (slot 1)
//   +128 (1)   longAttr[2]    +132 (4) longChange[2]    (slot 2)
//   +136 (1)   longAttr[3]    +140 (4) longChange[3]    (slot 3)
// ---------------------------------------------------------------------------
constexpr int kAiMethodRecordStride = 148; // 0x94 (matches kMethodTableStride)
constexpr int kAiMethodRecordCount  = 61;
constexpr int kAiMethodDesireSlots  = 4;   // loop `while (v4 < 4)` (@0x469090)

// Per-slot {attribute index, change magnitude}. attrIndex == -1 means "unused".
struct AiDesireSlot {
    i8  attrIndex = -1;   // record byte (LookupAttributeIndex result; -1 = none)
    f32 change    = 0.0f; // record float (VIBE_Util_StrToDouble of "*Change%li")
};

// The fields of one catalog record that the loaders read/write.
struct AiMethodRecord {
    u8           id = 0;                          // +0
    char         name[32] = {};                   // +1 .. +32
    AiDesireSlot shortSlots[kAiMethodDesireSlots]; // +48 .. +79 (short desires)
    AiDesireSlot prevSlots[kAiMethodDesireSlots];  // +80 .. +111 (MemMove mirror)
    AiDesireSlot longSlots[kAiMethodDesireSlots];  // +112 .. +143 (long desires)
};

// ---------------------------------------------------------------------------
// gilde.exe 0x468f6c — VIBE_AiMethod_RegisterFromIni(eax = record*, edx = haveIni)
//
// The original walks one INI section (the section name is the record's `name`
// field) reading "ShortDesire%li"/"ShortChange%li" and "LongDesire%li"/
// "LongChange%li" for li in 1..4, mapping the desire string through
// LookupAttributeIndex and the change string through VIBE_Util_StrToDouble.
//
// Because INI parsing is a Win32 leaf (RULE-6 FLAG), the caller supplies the desire
// keys' raw values through IniSource; this function reconstructs the pure record-
// building behavior exactly:
//
//   1. id gate: if (id <= 0 || id >= 61) return 0;                 (@0x468f88)
//   2. if (haveIni):                                               (@0x468f99)
//        for slot in 0..3:
//          short: if the "ShortDesire%li" key is PRESENT:
//                   attr = LookupAttributeIndex(value); store attr;
//                   if attr >= 0: change = StrToDouble("ShortChange%li"); store
//                 else: attr = -1; change = 0.            (@0x468fe5)
//          long: same with Long* keys.                    (@0x46903a / @0x4691b6)
//        validation: a method must affect SOMETHING — scan the 4 slots: a slot
//          "counts" iff (attr != -1) AND (change has any non-sign bits set, i.e.
//          (bitcast<u32>(change) & 0x7FFFFFFF) != 0, so +0.0 and -0.0 don't count).
//          Either the short OR the long side of any slot satisfying this makes the
//          method effective. If NONE do, the engine logs:
//            "am_RegisterMethod(): Methode '%s' hat keinerlei Auswirkungen!!!"
//          (logging is a side effect; reported via the return flag here).  (@0x4690a8)
//        prev-mirror: MemMove(record+80, record+48, 32) — copy the 4 short slots
//          into the prev vector.                                   (@0x469107)
//   3. commit: qmemcpy(&catalog[148*id], record, 148).            (@0x46913c)
//   4. return 1.
//
// Returns: 0 if the id gate rejects the record; otherwise 1 (committed).
// `outHasEffect` (optional) receives the validation result (true == method affects
// at least one attribute; false == the "keinerlei Auswirkungen" case).
//
// IniSource abstracts the four key families. `Get*Desire(slot)` returns nullptr when
// the key is absent (mirrors GetPrivateProfileStringA returning 0 / the Default).
struct IniSource {
    // "ShortDesire%li"/"LongDesire%li": attribute NAME or nullptr if key absent.
    const char* (*getShortDesire)(int slot, void* ctx) = nullptr;
    const char* (*getLongDesire )(int slot, void* ctx) = nullptr;
    // "ShortChange%li"/"LongChange%li": parsed change magnitude (read only when the
    // matching desire key was present and its attribute resolved >= 0).
    f32         (*getShortChange)(int slot, void* ctx) = nullptr;
    f32         (*getLongChange )(int slot, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// Builds the record fields in-place. `haveIni` mirrors the original's a2 (edx): when
// false, only the id gate + commit happen (the slots keep their incoming values).
// Returns the commit flag (0 == id-rejected, 1 == committed).
int AiMethod_RegisterFromIni(AiMethodRecord& rec, bool haveIni,
                             const IniSource& ini, bool* outHasEffect = nullptr);

// Pure helper: the "method has any effect" validation loop (@0x4690a8..0x4690cd),
// exposed for golden testing. A slot counts if its attr != -1 AND its change has any
// non-sign bit set (so +/-0.0 do not count). Scans short side then long side of each
// of the 4 slots; returns true on the first effective slot, false if none.
bool AiMethod_HasAnyEffect(const AiMethodRecord& rec);

// Pure helper: the change-magnitude "is non-zero" test the validation uses
// ((bitcast<u32>(x) & 0x7FFFFFFF) != 0). Exposed for golden testing of the +/-0.0
// edge cases.
bool AiMethod_ChangeIsNonZero(f32 change);

// ---------------------------------------------------------------------------
// gilde.exe 0x468a40 — VIBE_AiMethod_LoadDataFile(thiscall)
//
// Serializes the whole 61-record catalog to/from "/gamedata/ai/ai_data.dfn" via the
// game VFS. Two modes (dword_63C7D8 selects which): when set it WRITES the catalog
// (mode "wb"); when clear it READS (mode "rb"). Both stream the SAME field order per
// record, 61 records, stopping at the first failed field op.
//
// The VFS calls themselves are plumbing (the game's own file layer, not a tech swap)
// and are abstracted behind a byte-stream callback. What IS genuine and reconstructed
// here is the EXACT per-record field schedule (offset, size) and the post-read
// prev-mirror MemMove. The schedule is identical for read and write.
//
// Field schedule (offset within the 148-byte record, byte size), in stream order:
//   (0,1) (1,32) (48,1) (52,4) (56,1) (60,4) (64,1) (68,4) (72,1) (76,4)
//   (112,1)(116,4)(120,1)(124,4)(128,1)(132,4)(136,1)(140,4)
// On READ, after the 18 fields succeed: MemMove(record+80, record+48, 32).
// 18 fields per record; loop bound 61 records (@0x468cde / @0x468f4b).
// ---------------------------------------------------------------------------
struct AiMethodFieldSpec { i32 offset; i32 size; };

// The exact serialization schedule (18 entries). offset is the byte offset into the
// 148-byte record; size is the field width in bytes.
extern const AiMethodFieldSpec kAiMethodFieldSchedule[18];
constexpr int kAiMethodFieldCount = 18;

// Byte-stream abstraction for the VFS leaf. `xfer` moves `size` bytes between the
// record buffer at `recordBase + offset` and the backing store; returns true on
// success (mirrors VIBE_Vfs_Read/WriteStream returning nonzero). Direction is the
// caller's (read vs write) — the schedule is identical either way.
struct ByteStream {
    bool (*xfer)(u8* recordBytes, i32 offset, i32 size, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// Drives the field schedule for one record over the stream. Returns the number of
// records-worth of fields completed: true iff all 18 fields succeeded. When
// `mirrorPrevOnSuccess` is set (the READ path), copies the 4 short slots into the
// prev vector (record+80 <- record+48, 32 bytes) after all fields succeed.
bool AiMethod_StreamRecord(u8* recordBytes /*148*/, const ByteStream& stream,
                           bool mirrorPrevOnSuccess);

} // namespace guild::sim
