#pragma once
// Cutscene slot management + state stepping + cutscene-local RNG (gilde.exe).
//
// Namespace: guild::sim. Cutscenes (duel / execution / wedding / council /
// trial / auction / ...) are state machines driven from the per-frame loop via
// VIBE_Cutscene_ProcessActive (0x4ac31c). Each runs through a fixed table of
// 96 "slots" (276 bytes each). A slot pairs a cutscene id with its type, a
// "ready time" GameTime record, a participant list and a per-type step handler.
// The cutscene RNG is a SEPARATE LCG from the CRT rand (its own state word
// dword_11B4E38), seeded by VIBE_Cutscene_SetRandSeed and consumed by RandInt /
// RandFloat — used for deterministic duel/auction/event outcomes.
//
// Recovered byte-for-byte here:
//   * the CutsceneSlot layout (stride 276, the fields the slot scanners /
//     participant ops / state stepper address),
//   * the cutscene LCG (constants 1103515245 / 12345, the 0x7FFF reduction and
//     the 1/0x7FFF float multiplier),
//   * the slot-table management (find / alloc / remove / participant add/remove
//     / lowest-priority pick).
//
// Deferred (deeply-coupled, listed in the report): VIBE_Cutscene_ProcessActive
// (0x4ac31c) and VIBE_Cutscene_ExecMainFunc (0x4ab55c) full bodies — they pull
// in GameTime_Compare/Advance, the Person id columns (dword_12CE914 /
// dword_12CEB18 / word_63CC5C), per-type step-fn table dword_11AE5D0, the
// Command op-88 request and Universe state; we translate the slot/RNG/step
// substrate they sit on and expose a faithful single-slot step skeleton.
#include "guild/common/types.h"
#include "sim/types.h"     // GameTime
#include <vector>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Cutscene-local RNG  (gilde.exe state word dword_11B4E38 @0x11B4E38).
// A 32-bit LCG, SEPARATE from guild::crt's generator:
//   state = 1103515245 * state + 12345        (32-bit wraparound, imul 41C64E6Dh
//                                               + 3039h)
//   r15   = (state >> 16) % 0x7FFF            (15-bit reduced word)
// RandInt(range): return r15 % range. RandFloat(): return r15 * (1/0x7FFF).
// ---------------------------------------------------------------------------
class CutsceneRng {
public:
    // gilde.exe 0x4ac9a0 — VIBE_Cutscene_SetRandSeed(seed@<eax>)
    void SetSeed(i32 seed) { state_ = static_cast<u32>(seed); }
    // gilde.exe 0x4ac9c0 — VIBE_Cutscene_GetRandSeed
    i32 GetSeed() const { return static_cast<i32>(state_); }

    // gilde.exe 0x4ac9e8 — VIBE_Cutscene_RandInt(range@<eax>)
    //   if range == 0 -> return 0 (the original returns the zero range value).
    //   else advance the LCG and return ((state>>16) % 0x7FFF) % range.
    u32 RandInt(u32 range) {
        if (range == 0) return 0;
        state_ = 1103515245u * state_ + 12345u;          // intentional wrap
        u32 r = ((state_ >> 16) & 0xFFFFu) % 0x7FFFu;
        return r % range;
    }

    // gilde.exe 0x4aca48 — VIBE_Cutscene_RandFloat
    //   advance the LCG and return ((state>>16) % 0x7FFF) * (1/0x7FFF) in [0,1).
    double RandFloat() {
        state_ = 1103515245u * state_ + 12345u;          // intentional wrap
        u32 r = ((state_ >> 16) & 0xFFFFu) % 0x7FFFu;
        return static_cast<double>(r) * kMul;
    }

    // flt_61D934 @0x61D934 (IEEE-754 0x38000100) == 1/0x7FFF.
    static constexpr double kMul = 3.0518509447574615e-05;

private:
    u32 state_ = 0;
};

// ---------------------------------------------------------------------------
// Cutscene slot record  (gilde.exe table base dword_11AE6B0 @0x11AE6B0)
//   stride 276 / 0x114, 96 slots (6624 dwords / 69 dwords-per-slot). Heap data.
// Field offsets recovered from the slot scanners / participant ops / stepper:
//   +0   (dword) id            ; -1 == free slot (alloc/remove sentinel)
//   +4   (dword) finished flag ; set to 1 by ProcessActive when done
//   +8   (byte)  type code     ; selects per-type step fn (dword_11AE5D0[5*t]),
//                                also packed via "(dword@+5)>>24" reads
//   +12  (dword) master/owner person id ; -1 default (ProcessActive fills it)
//   +20  (dword) secondary id  ; FindSlotByTypeAndId match key (dword_11AE6C4)
//   +24  (GameTime, 14 B) "ready / timeout" time record (unk_11AE6C8)
//   +38  (byte)  state flags   ; bit0 = active-in-priority, bit1 = ready,
//                                bit2 = skip/done (byte_11AE6D6)
//   +40  (dword) started flag  ; set when the step handler first runs
//   +48  (byte)  participant count / "active" gate ; nonzero == live slot
//                                (byte_11AE6E0; FindSlotById's alive test)
//   +49  (byte)  priority/state byte ; alloc defaults it to 8 if zero
//   +52  (dword[16]) participant person ids (AddParticipant appends, max 16)
//   ... remaining bytes: per-type scratch / scene handles (untouched here)
// ---------------------------------------------------------------------------
constexpr int kCutsceneSlotStride = 276;   // 0x114
constexpr int kCutsceneSlotCount  = 96;    // 6624 / 69 (dword stride)
constexpr int kMaxParticipants    = 16;

enum CutsceneSlotField : int {
    kCsId         = 0,    // -1 == free
    kCsFinished   = 4,
    kCsType       = 8,
    kCsMaster     = 12,
    kCsSecondId   = 20,
    kCsReadyTime  = 24,   // GameTime (14 bytes)
    kCsStateFlags = 38,
    kCsStarted    = 40,
    kCsPartCount  = 48,   // also the alive gate
    kCsPriority   = 49,   // alloc default 8
    kCsPartIds    = 52,   // dword[16]
};
constexpr u8 kCsFlagActive = 0x01;  // byte+38 bit0
constexpr u8 kCsFlagReady  = 0x02;  // byte+38 bit1
constexpr u8 kCsFlagSkip   = 0x04;  // byte+38 bit2

GUILD_PACKED_BEGIN
struct CutsceneSlot {
    i32 id;                       // +0   (-1 == free)
    i32 finished;                 // +4
    u8  type;                     // +8
    u8  pad9[3];                  // +9
    i32 master;                   // +12
    u8  pad16[4];                 // +16
    i32 secondId;                 // +20
    GameTime readyTime;           // +24  (14 bytes, occupies +24..+37)
    u8  stateFlags;               // +38  state-flags byte (byte_11AE6D6)
    u8  pad39;                    // +39
    i32 started;                  // +40
    u8  pad44[4];                 // +44
    u8  partCount;                // +48  participant count / alive gate
    u8  priority;                 // +49  alloc default 8
    u8  pad50[2];                 // +50
    i32 partIds[kMaxParticipants];// +52  participant person ids (16 * 4 = 64)
    u8  pad116[160];              // +116..+275  per-type scratch / scene handles
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(CutsceneSlot) == kCutsceneSlotStride,
              "CutsceneSlot stride must be 276");

// The slot table. The original is a flat 96*276 byte array scanned linearly;
// we keep the same geometry so the scanners are byte-faithful.
class CutsceneTable {
public:
    CutsceneTable() { Clear(); }

    // Reset every slot to free (id == -1), matching the cold table state.
    void Clear() {
        for (auto& s : slots_) { std::memset(&s, 0, sizeof(s)); s.id = -1; }
    }

    CutsceneSlot* Slots() { return slots_; }
    const CutsceneSlot* Slots() const { return slots_; }

    // gilde.exe 0x4ac750 — VIBE_Cutscene_FindSlotById(id)
    //   linear scan; a slot matches when its alive gate (+48) is nonzero AND its
    //   id (+0) equals `id`. Returns nullptr if none.
    CutsceneSlot* FindById(i32 id);

    // gilde.exe 0x4ac708 — VIBE_Cutscene_FindSlotByTypeAndId(type, secondId)
    //   matches alive slots whose type (+8) and secondary id (+20) both equal.
    CutsceneSlot* FindByTypeAndId(u8 type, u32 secondId);

    // gilde.exe 0x4ac7e4 — VIBE_Cutscene_AllocSlot(templateRec)
    //   find the first free slot (id == -1), copy 276 bytes from the template,
    //   set its id from the new-id source `newId`, and default its priority byte
    //   (+49) to 8 if zero. Returns nullptr if the table is full.
    CutsceneSlot* AllocSlot(const CutsceneSlot& tmpl, i32 newId);

    // gilde.exe 0x4ac860 — VIBE_Cutscene_RemoveById(id): clear the matching slot
    //   (zero its 276 bytes) and mark it free (id = -1). Returns true if removed.
    bool RemoveById(i32 id);

    // gilde.exe 0x4ac888 — VIBE_Cutscene_RemoveAll: remove every non-free slot.
    void RemoveAll();

    // gilde.exe 0x4ac630 — VIBE_Cutscene_FindLowestPriority: among alive slots
    //   with state-flag bit0 set, return the one with the smallest id. nullptr
    //   if none. (The original tracks min id then re-finds by id.)
    CutsceneSlot* FindLowestPriority();

    // gilde.exe 0x4ac8d0 — VIBE_Cutscene_AddParticipant(id, person)
    //   append `person` to the slot's participant list (no duplicates, max 16).
    //   Returns 1 on success, 0 if slot missing / duplicate / full.
    int AddParticipant(i32 id, i32 person);

    // gilde.exe 0x4ac928 — VIBE_Cutscene_RemoveParticipant(person)
    //   NOTE: the original looks the slot up by a context id then removes the
    //   matching participant. We expose the slot-targeted form used by the
    //   engine: remove `person` from slot `id`'s list (sets the entry to -1).
    //   Returns 1 if the slot exists.
    int RemoveParticipant(i32 id, i32 person);

private:
    CutsceneSlot slots_[kCutsceneSlotCount];
};

} // namespace guild::sim
