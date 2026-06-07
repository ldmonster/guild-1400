#pragma once
// MeisterAi event-slot managers (gilde.exe 0x4c6f68..0x4c7164, 0x4c932c).
//
// The Guild-Master ("Meister") AI keeps two flat, fixed-size pools of timed
// "events" that the per-turn AI scans and ages out:
//
//   * ApEvent pool  (he_RegisterApEvent / ExpireApEventSlots /
//     SumApEventsByOwner / SumApEventsBySecondary) — 256 slots, 80 bytes each,
//     at gilde.exe 0x11C6560. Each slot records a primary value (a3), a
//     secondary value (a2), an owner key (a1), a short label, and a time-to-
//     live counter. ExpireApEventSlots decrements every live slot's TTL once
//     per call and frees the slot when it reaches <= 0. The two Sum* helpers
//     total the primary / secondary values across all live slots whose owner
//     key matches a query.
//
//   * EventSlot pool (RegisterEventSlot / ExpireEventSlots) — 256 slots, 164
//     bytes each, at gilde.exe 0x11CB620. Each slot has an id (a1), a TTL byte,
//     two payload ints (a2/a3), a small opaque payload blob and a 127-byte
//     label. ExpireEventSlots ages the TTL byte and frees on reaching 0.
//
// These are pure container operations over the slot records — no entity, world
// or graphics coupling — so they are modelled here with named POD structs and
// translated 1:1. The one external touch, the "too many ApEvents" diagnostic
// (VIBE_ErrorLog_ReportMessage), is exposed as an injectable callback so the
// code stays self-contained and testable.
#include "guild/common/types.h"
#include <cstddef>

namespace guild::ai {

// --- ApEvent pool -----------------------------------------------------------
// gilde.exe 0x11C6560: 256 records of 80 bytes (stride 20 dwords). Field byte
// offsets recovered from he_RegisterApEvent / ExpireApEventSlots:
//   +0x00 ttl       — time-to-live byte (also the "live" marker; 0 == free)
//   +0x04 ownerKey  — a1, the query key the Sum* helpers match against
//   +0x08 primary   — a3, summed by SumApEventsByOwner
//   +0x0C secondary — a2, summed by SumApEventsBySecondary
//   +0x10 label[63] — NUL-padded label (StrNCopyPad 63)
struct ApEventSlot {
    u8  ttl = 0;          // +0x00
    u8  pad0 = 0;         // +0x01
    u8  pad1 = 0;         // +0x02
    u8  pad2 = 0;         // +0x03
    i32 ownerKey = 0;     // +0x04
    i32 primary = 0;      // +0x08
    i32 secondary = 0;    // +0x0C
    char label[64] = {};  // +0x10 (63 chars + pad; original writes 63 bytes)
};

constexpr int kApEventSlotCount = 256;   // 5120 dword-steps / 20

// The original uses a single static 256-slot array (he_RegisterApEvent stores
// into byte_11C6560). The pool is passed explicitly here so tests are hermetic;
// the live game has exactly one instance.
struct ApEventPool {
    ApEventSlot slots[kApEventSlotCount];
};

// Optional diagnostic sink (mirrors VIBE_ErrorLog_ReportMessage). When null the
// message is dropped, exactly as if logging were disabled.
using ErrorLogFn = void (*)(const char* message);

// gilde.exe 0x4c703c — he_RegisterApEvent(a1=eax owner, a2=ecx secondary,
// a3=ebx primary, a4=edx label). Finds the first fully-empty slot (ttl,
// owner, primary, secondary all zero), initialises it (ttl=2, owner=a1,
// primary=a3, secondary=a2, label copied 63 bytes) and returns its index.
// On overflow (>= 256 live slots) reports "he_RegisterApEvent(): Too many
// ApEvents..." via `log` and returns -1.
int RegisterApEvent(ApEventPool& pool, i32 owner, i32 secondary, i32 primary,
                    const char* label, ErrorLogFn log = nullptr);

// gilde.exe 0x4c70c0 — ExpireApEventSlots(). Decrements every live slot's ttl;
// when a slot's ttl reaches <= 0 it is freed (owner/primary/secondary cleared
// and ttl forced to 0). Mirrors the original's in-place ageing over all 256
// slots.
void ExpireApEventSlots(ApEventPool& pool);

// gilde.exe 0x4c7104 — SumApEventsByOwner(a1=eax). Sums the `primary` field of
// every slot whose `primary` is non-zero and whose `ownerKey` equals a1.
int SumApEventsByOwner(const ApEventPool& pool, i32 owner);

// gilde.exe 0x4c7134 — SumApEventsBySecondary(a1=eax). Sums the `secondary`
// field of every slot whose `secondary` is non-zero and whose `ownerKey`
// equals a1.
int SumApEventsBySecondary(const ApEventPool& pool, i32 owner);

// --- EventSlot pool ---------------------------------------------------------
// gilde.exe 0x11CB620: 256 records of 164 bytes (stride 41 dwords). Field byte
// offsets recovered from RegisterEventSlot / ExpireEventSlots:
//   +0x00 id        — a1 (also the "live" marker; 0 == free)
//   +0x04 ttl       — time-to-live byte, initialised to 2
//   +0x08 kind      — initialised to 1
//   +0x0C payloadA  — a2
//   +0x10 payloadB  — a3
//   +0x14 payload[14] — opaque blob qmemcpy'd from the caller
//   +0x22 label[127]  — NUL-padded label (StrNCopyPad 127)
struct EventSlot {
    i32  id = 0;            // +0x00
    u8   ttl = 0;           // +0x04
    u8   kind = 0;          // +0x08 (original byte at dword index 2)
    i32  payloadA = 0;      // +0x0C
    i32  payloadB = 0;      // +0x10
    u8   payload[14] = {};  // +0x14 (overlaps below label start +0x22)
    char label[128] = {};   // +0x22 (127 chars + pad)
};

constexpr int kEventSlotCount = 256;   // 10496 dword-steps / 41

struct EventPool {
    EventSlot slots[kEventSlotCount];
};

// gilde.exe 0x4c6f68 — RegisterEventSlot(a1=eax id, a2=edx payloadA,
// a3=ecx payloadB, a4=ebx label, a5=payload ptr). The original takes the
// payload byte count in `ecx` (an unrecovered __usercall register arg, shown as
// the uninitialised `v13` in the decompile); it is surfaced here as the
// explicit `payloadSize` parameter (clamped to the 14-byte payload region).
// Finds the first slot with id==0, initialises it (ttl=2, kind=1, id=a1,
// payloadA=a2, payloadB=a3, label copied 127 bytes, payload blob copied) and
// returns the slot index. On overflow (>= 256 live slots) returns -1.
int RegisterEventSlot(EventPool& pool, i32 id, i32 payloadA, i32 payloadB,
                      const char* label, const void* payload,
                      std::size_t payloadSize);

// gilde.exe 0x4c7004 — ExpireEventSlots(). Decrements every live slot's ttl
// byte; when it reaches 0 the slot's id is cleared (freed). Mirrors the
// original's in-place ageing over all 256 slots.
void ExpireEventSlots(EventPool& pool);

// gilde.exe 0x4c932c — NullTick(). The empty tick handler the AI installs in
// unused dispatch slots; does nothing.
void NullTick();

} // namespace guild::ai
