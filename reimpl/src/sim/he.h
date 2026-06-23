#pragma once
// "He" / AI-handler record — the per-NPC behavior state-machine context for the
// Guild simulation (gilde.exe). Every NpcAction/NpcEvent step function operates on
// a pointer to one of these records (the IDA decompiler shows them as raw
// `*(_TYPE *)(a1 + offset)` accesses off a base pointer in eax/result). The handler
// pool lives in the "He" cluster (VIBE_He_*; allocator VIBE_He_FreeHandlerEntry
// @0x4c6144); this header recovers the field layout the NpcAction/NpcEvent module
// reads and writes, byte-for-byte.
//
// Recovered offsets (from the step functions in this module — see provenance
// comments on each NpcAction/NpcEvent function in npcaction.cpp / npcevent.cpp):
//   +0x04 (+4)   : person/entity id (dword)            — Person record id mirror
//   +0x08 (+8)   : person/city index (word)            — index into Person array
//   +0x0C (+12)  : resolved target city id (dword)     — SetTargetCityRef
//   +0x44 (+68)  : SAVED/base GameTime (14 bytes)       — RestorePose/CopyTargetCoord src
//   +0x52 (+82)  : appointment GameTime (14 bytes)      — next "wake" time (deadline)
//   +0x60 (+96)  : 6-dword scratch / shuffle array      — InitTargetSlots
//   +0x70 (+112) : state dword                          — ResetToState0 / phase id
//   +0x78 (+120) : flag byte (0x02 needs-cmd29, 0x04 already-spawned)
//   +0x84 (+132) : last entity-request packet handle (dword)
//   +0xAC (+172) : counter / base text id (word, sometimes dword)
//   +0xB0 (+176) : deadline GameTime (14 bytes); +0xB4 (+180) reused as wait counter
//   +0xD4 (+212) : misc dword (-1 reset)
//   +0xDC (+220) : misc byte (cleared on reset / patrol lap counter)
// The appointment block at +82 is a `GameTime` (see types.h): day@+82 (dword),
// hour@+86 (word), minute@+88 (dword), second@+92 (dword) — total 14 bytes. The
// original copies the global clock into it as a `qword` (+82) plus a trailing
// `dword`(+90) and `word`(+94), which is exactly the 14-byte GameTime image.
#include "guild/common/types.h"
#include "sim/types.h"

#include <cstddef>  // offsetof

namespace guild::sim {

// ---------------------------------------------------------------------------
// He / handler record. Field offsets are pinned with static_asserts so the
// layout stays byte-faithful to the original raw-offset accesses.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct HeRecord {
    u8   pad0[4];        // +0x00  (kind/marker etc. — not touched by this module)
    i32  id;             // +0x04  person / entity id
    u16  cityIndex;      // +0x08  person/city array index
    u8   pad0A[2];       // +0x0A
    i32  cityId;         // +0x0C  resolved city/person id (SetTargetCityRef)
    u8   pad10[52];      // +0x10..+0x43
    GameTime savedTime;  // +0x44 (+68)  base/saved timestamp (RestorePose source)
    GameTime apptTime;   // +0x52 (+82)  appointment / next-wake GameTime
    i32  scratch[4];     // +0x60 (+96)  scratch region (96..111; shuffle/target slots)
    i32  state;          // +0x70 (+112) state / phase id
    u8   pad74[4];       // +0x74
    u8   flags;          // +0x78 (+120) flag byte
    u8   pad79[11];      // +0x79
    i32  reqHandle;      // +0x84 (+132) last entity-request packet handle
    u8   pad88[36];      // +0x88..+0xAB
    u16  counter;        // +0xAC (+172) counter / base text id
    u8   padAE[2];       // +0xAE
    GameTime deadline;   // +0xB0 (+176) deadline GameTime (+180 = wait counter)
    u8   padBE[2];       // +0xBE..+0xBF (pad to +0xC0)
    u8   padC0[20];      // +0xC0..+0xD3
    i32  misc212;        // +0xD4 (+212)
    u8   padD8[4];       // +0xD8
    u8   misc220;        // +0xDC (+220)
    u8   padDD[3];       // +0xDD  pad to +0xE0
    // The larger step machines (npcaction2.cpp / npctarget.cpp) reach further
    // fields off the same record base (member/packet arrays, plague cursor, the
    // office-category + sub-method bytes at +358..+361). Reserve the bytes so the
    // record is large enough for those raw-offset accessors (see He_* below).
    u8   tail[290];      // +0xE0..+0x201 (extends past +361 = office/sub-method)
} GUILD_PACKED;
GUILD_PACKED_END

// The structured view above has overlapping declared members at +0x60/+0x70 to
// document the regions; the byte-faithful accessors below are what the translated
// step functions use, so they address the record by explicit offset to stay exact.

// Raw byte base of a record (the original kept a `char*`/`int` base in eax).
inline u8* HeBytes(HeRecord* h) { return reinterpret_cast<u8*>(h); }

// ---------------------------------------------------------------------------
// Alignment note (WAVE-17 verdict — UBSAN clean, behaviour byte-identical):
//   The original gilde.exe touches this record with NATIVE UNALIGNED x86
//   accesses.  e.g. VIBE_NpcAction_StampTimeAndRequestEntity @0x4c9458 does
//       *(_QWORD*)(result + 82) = ...;   // appointment GameTime at ODD +82
//       *(_DWORD*)(result + 90) = ...;
//       *(_WORD*)(result + 94)  = ...;
//   All offsets here are in-bounds (sizeof(HeRecord) == 514) — they are unaligned
//   but valid x86 reads, NOT out-of-bounds.  To reproduce that exactly while
//   staying clean under `-fsanitize=alignment`, the reference accessors below go
//   through a 1-aligned (packed) pointer type: the emitted load/store is the same
//   unaligned access the binary performs, the lvalue interface (`He_X(h)=v`,
//   `++He_X(h)`, `&He_X(h)`) is unchanged, and every call-site value/offset is
//   byte-identical.  Converting the ~900 call sites to memcpy get/set would churn
//   dozens of lvalue write/compound sites across consumer files with no behaviour
//   change, so it is intentionally NOT done — this in-place packed-ref change
//   closes the UBSAN-alignment gap with zero consumer churn.
// ---------------------------------------------------------------------------
// Concrete 1-aligned typedefs (a template alias drops the attribute on GCC, so
// each used type gets its own packed alias).  `__attribute__((aligned(1)))` makes
// the load/store UBSAN-alignment clean while emitting the SAME unaligned access.
#if defined(__GNUC__) || defined(__clang__)
typedef i32      HeU_i32      __attribute__((aligned(1)));
typedef u16      HeU_u16      __attribute__((aligned(1)));
typedef GameTime HeU_GameTime __attribute__((aligned(1)));
#else
typedef i32      HeU_i32;
typedef u16      HeU_u16;
typedef GameTime HeU_GameTime;
#endif
template <class T> struct HeUnalignedSel;
template <> struct HeUnalignedSel<i32>      { using type = HeU_i32; };
template <> struct HeUnalignedSel<u16>      { using type = HeU_u16; };
template <> struct HeUnalignedSel<GameTime> { using type = HeU_GameTime; };
template <class T> using HeUnaligned = typename HeUnalignedSel<T>::type;

// Field accessors at the exact original byte offsets. These are the canonical
// way the translated functions touch the record (mirrors `*(T*)(base+off)`).
// The packed (aligned(1)) pointer type makes the read UBSAN-alignment clean while
// emitting the SAME unaligned access the original binary performs.
inline HeU_i32&      He_Id(HeRecord* h)        { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 4); }
inline HeU_u16&      He_CityIndex(HeRecord* h) { return *reinterpret_cast<HeUnaligned<u16>*>(HeBytes(h) + 8); }
inline HeU_i32&      He_CityId(HeRecord* h)    { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 12); }
inline HeU_GameTime& He_SavedTime(HeRecord* h) { return *reinterpret_cast<HeUnaligned<GameTime>*>(HeBytes(h) + 68); }
inline HeU_GameTime& He_ApptTime(HeRecord* h)  { return *reinterpret_cast<HeUnaligned<GameTime>*>(HeBytes(h) + 82); }
inline HeU_i32&      He_State(HeRecord* h)      { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 112); }
inline u8&       He_Flags(HeRecord* h)      { return *reinterpret_cast<u8*>(HeBytes(h) + 120); }
inline HeU_i32&      He_ReqHandle(HeRecord* h)  { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 132); }
inline HeU_u16&      He_Counter(HeRecord* h)    { return *reinterpret_cast<HeUnaligned<u16>*>(HeBytes(h) + 172); }
inline HeU_GameTime& He_Deadline(HeRecord* h)   { return *reinterpret_cast<HeUnaligned<GameTime>*>(HeBytes(h) + 176); }
// +180 is the hour field of the +176 GameTime; the engine reuses it as the
// idle-wait counter (BeginIdleWaitState / RestorePoseSetRandom).
inline HeU_u16&      He_WaitCounter(HeRecord* h){ return *reinterpret_cast<HeUnaligned<u16>*>(HeBytes(h) + 180); }
inline HeU_i32&      He_Scratch(HeRecord* h, int dwordIndex) {
    return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 96 + 4 * dwordIndex);
}

// Flag bits in He_Flags (+120).
enum HeFlag : u8 {
    kHeNeedsCmd29   = 0x02,  // request a cmd29 entity packet when stamping
    kHeAlreadySpawned = 0x04 // record already materialised (skip setup)
};

// ---------------------------------------------------------------------------
// Additional He fields used by the larger NpcAction/CharAction step state
// machines (npcaction2.cpp / npctarget.cpp). These extend the layout above
// additively; offsets are recovered from the step decompilations:
//   +0x8C (+140) : 4-slot member/participant person-id array (dword each, -1 free)
//                  AttackTargetStep reads it as dword index 35..38 off the base;
//                  the same 16 bytes the Plague step reads at +188 (see below).
//   +0xAC (+172) : remaining-iteration counter (dword) — Plague decrements it.
//   +0xB0 (+176) : target object/building id OR scan cursor (dword).
//   +0xB4 (+180) : Plague scan step (coprime-to-256 stride) / packet handle slot.
//   +0xB8 (+184) : resolved successor/seq id (dword).
//   +0xBC (+188) : 4-slot member packet/person-id array (dword each).
//   +0xCC (+204) : current plague target object id (dword).
//   +0xD0 (+208) : plague source/home object id (dword).
//   +0x166 (+358): office category byte A (NpcTarget).
//   +0x167 (+359): office category byte B (NpcTarget).
//   +0x168 (+360): office category byte C (NpcTarget).
//   +0x169 (+361): NPC sub-method/class byte (30/31/32/33 = combat/move kinds).
// Accessors below address the record by explicit offset, byte-faithful to the
// raw `*(T*)(base+off)` accesses in the originals.
inline HeU_i32&      He_MemberId(HeRecord* h, int slot)  // +140 + 4*slot (slots 0..3)
    { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 140 + 4 * slot); }
inline HeU_i32&      He_Counter172(HeRecord* h) { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 172); }
inline HeU_i32&      He_TargetObjId(HeRecord* h){ return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 176); }
inline HeU_i32&      He_ScanStep(HeRecord* h)   { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 180); }
inline HeU_i32&      He_SeqId(HeRecord* h)      { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 184); }
inline HeU_i32&      He_PacketId(HeRecord* h, int slot) // +188 + 4*slot (slots 0..3)
    { return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 188 + 4 * slot); }
inline HeU_i32&      He_PlagueTarget(HeRecord* h){ return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 204); }
inline HeU_i32&      He_PlagueSource(HeRecord* h){ return *reinterpret_cast<HeUnaligned<i32>*>(HeBytes(h) + 208); }
inline u8&       He_OfficeCatA(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 358); }
inline u8&       He_OfficeCatB(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 359); }
inline u8&       He_OfficeCatC(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 360); }
inline u8&       He_SubMethod(HeRecord* h)   { return *reinterpret_cast<u8*>(HeBytes(h) + 361); }

static_assert(offsetof(HeRecord, id) == 4, "He.id @+4");
static_assert(offsetof(HeRecord, cityIndex) == 8, "He.cityIndex @+8");
static_assert(offsetof(HeRecord, cityId) == 12, "He.cityId @+12");
static_assert(offsetof(HeRecord, savedTime) == 68, "He.savedTime @+68");
static_assert(offsetof(HeRecord, apptTime) == 82, "He.apptTime @+82");
static_assert(offsetof(HeRecord, state) == 112, "He.state @+112");
static_assert(offsetof(HeRecord, flags) == 120, "He.flags @+120");
static_assert(offsetof(HeRecord, reqHandle) == 132, "He.reqHandle @+132");
static_assert(offsetof(HeRecord, counter) == 172, "He.counter @+172");
static_assert(offsetof(HeRecord, deadline) == 176, "He.deadline @+176");
static_assert(offsetof(HeRecord, misc212) == 212, "He.misc212 @+212");
static_assert(offsetof(HeRecord, misc220) == 220, "He.misc220 @+220");

} // namespace guild::sim
