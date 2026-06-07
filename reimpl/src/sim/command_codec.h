#pragma once
#include "guild/common/types.h"
#include "sim/command.h"

// gilde.exe — Command CODEC delta encoder + request builders (guild::sim).
//
// This file is the payload-construction half of the Command codec. Two things:
//
//  1. The delta encoder (VIBE_Command_BeginDeltaPacket / AppendDeltaField /
//     AppendRawField / AppendCopiedField, and the AI-method variant
//     BeginAiMethodPacket / AppendAiMethodEntry). Entity field updates are
//     serialized as (width, count, offset, values) triplets; AppendDeltaField
//     writes (new - old) values relative to the live entity memory so packets
//     stay small. AppendRawField/AppendCopiedField write absolute values.
//
//  2. A representative set of the ~110 VIBE_Command_Queue*/Request* builders.
//     Each stages a 153-byte temp, writes the opcode at +0 and its fields into
//     the payload (which begins at +0x10), then calls EnqueuePacket. We port
//     enough to prove the encoder; the rest are mechanical clones.
//
// The delta encoder is determinism-critical: the wire bytes must equal
// new-minus-old exactly, and applying the delta to old must reproduce new.

namespace guild::sim {

// --- delta payload builder --------------------------------------------------
// Models the original's file-global delta cursor (unk_11AA3E5 payload buffer,
// dword_11AA460 cursor, byte_11AA3E4 field count, dword_11AA474 entity base).
// One instance per packet under construction. Field width must be 1, 2 or 4.
class DeltaWriter {
public:
    // Payload buffer max (the 0x77 cursor guard in the Append* functions).
    static constexpr u32 kMaxPayload = guild::sim::kMaxPayload; // 0x77

    DeltaWriter() { Reset(nullptr); }

    // VIBE_Command_BeginDeltaPacket @0x493a94 — start a new delta packet. In the
    // original this resolves the target entity by id (ResolveEntityById) and
    // stashes its base pointer in dword_11AA474; here the caller passes the live
    // entity memory base directly (and its id, kept for the header). Resets the
    // cursor and field count. The 119-byte payload buffer is zeroed (the
    // SetGrayColorThunk(0,119) call is a memset thunk).
    void BeginDeltaPacket(const void* entityBase, u32 entityId);

    // VIBE_Command_AppendDeltaField @0x493aec — append `count` values of `width`
    // bytes at entity offset `fieldOffset`. The wire value written is
    // (new - old) where `new` comes from `newValues` and `old` is read from the
    // entity base at fieldOffset. Returns 0 on success, 1 on bad width / overflow.
    int AppendDeltaField(u8 width, u8 count, u16 fieldOffset, const void* newValues);

    // VIBE_Command_AppendRawField @0x493c14 — append absolute values via memcpy
    // (no delta). Returns 0 on success, 1 on bad width / overflow.
    int AppendRawField(u8 width, u8 count, u16 fieldOffset, const void* values);

    // VIBE_Command_AppendCopiedField @0x493c90 — append absolute values copied
    // element-by-element (width-aware). Behaviorally equal to AppendRawField for
    // the supported widths; kept distinct to mirror the binary. Returns 0/1.
    int AppendCopiedField(u8 width, u8 count, u16 fieldOffset, const void* values);

    // Reset to an empty payload for a fresh packet (entityBase may be null until
    // BeginDeltaPacket runs).
    void Reset(const void* entityBase);

    u8        field_count() const { return field_count_; }  // byte_11AA3E4
    u32       cursor() const { return cursor_; }            // dword_11AA460
    u32       entity_id() const { return entity_id_; }      // dword_11AA3E0
    const u8* payload() const { return payload_; }          // unk_11AA3E5
    u8*       payload() { return payload_; }

    // Decode helper (the receive/apply side): walk the (width,count,offset,vals)
    // triplets in `payload` for `fieldCount` fields, adding each delta to the
    // matching bytes of `entity`. Mirrors what the Ex* apply handlers do.
    static void ApplyDelta(const u8* payload, u8 fieldCount, void* entity);

private:
    u8  payload_[128];          // unk_11AA3E5 region (>= 0x77)
    u32 cursor_ = 0;            // dword_11AA460 — write cursor (bytes used)
    u8  field_count_ = 0;      // byte_11AA3E4 — number of fields appended
    u32 entity_id_ = 0;        // dword_11AA3E0 — target entity id (header)
    const u8* entity_ = nullptr; // dword_11AA474 — live entity memory base
};

// --- AI-method need-delta builder -------------------------------------------
// VIBE_Command_BeginAiMethodPacket @0x493d64 / AppendAiMethodEntry @0x493d9c.
// Each entry is a 5-byte record: a stat id byte + a 4-byte delta value.
class AiMethodWriter {
public:
    static constexpr u32 kMaxPayload = guild::sim::kMaxPayload; // 0x77

    void Begin(u32 personId);              // BeginAiMethodPacket
    int  AppendEntry(u8 statId, i32 delta); // AppendAiMethodEntry; 0 ok / 1 full

    u8        entry_count() const { return count_; }  // byte_11AA364
    u32       cursor() const { return cursor_; }      // dword_11AA45C
    u32       person_id() const { return person_id_; } // dword_11AA360
    const u8* payload() const { return payload_; }     // unk_11AA365

private:
    u8  payload_[128];
    u32 cursor_ = 0;
    u8  count_  = 0;
    u32 person_id_ = 0;
};

// --- representative request builders ----------------------------------------
// Each fills a staged 153-byte packet and enqueues it on `q`. Field layouts are
// recovered from the original stack-temp builders (payload region starts at
// +0x10). Returns the assigned ring slot index (EnqueuePacket's return).

// VIBE_Command_QueueRequest16 @0x494630 — opcode 16. Payload: a1@+0x10, a2@+0x14,
// flag byte a4@+0x1C, a3@+0x1D.
i32 QueueRequest16(CommandQueue& q, i32 a1, i32 a2, i32 a3, u8 a4);

// VIBE_Command_QueueRequest17 @0x49465c — opcode 17. a1@+0x10, a2@+0x14,
// a4(word)@+0x18, a5(byte)@+0x1E, a3@+0x1F, a6@+0x23.
i32 QueueRequest17(CommandQueue& q, i32 a1, i32 a2, i32 a3, i16 a4, u8 a5, i32 a6);

// VIBE_Command_QueueRequestArgs25 @0x494810 — opcode 25 ("RequestPerm"-class
// arg packet). a1@+0x10, a2@+0x14, a4@+0x18, a3@+0x1C, a5@+0x20.
i32 QueueRequestArgs25(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4, i32 a5);

// VIBE_Command_QueueRequestCoord27 @0x494878 — opcode 27. a1@+0x10, a2@+0x14,
// a3@+0x18, then a converted world coord pair (we accept the two i32s directly).
i32 QueueRequestCoord27(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 coordX, i32 coordY);

// VIBE_Command_EnqueueObjectInteraction @0x4944f0 — opcode 11 (e.g. SellObjekt-
// class object interaction). a2@+0x15, a4@+0x19, a3(word)@+0x1D, a5@+0x1F,
// a6@+0x23, a7@+0x24, a8@+0x25, a1(byte)@+0x14.
i32 EnqueueObjectInteraction(CommandQueue& q, u8 a1, i32 a2, i16 a3, i32 a4,
                             i32 a5, u8 a6, u8 a7, u8 a8);

// VIBE_Command_QueueRequestState22 @0x494750 — opcode 22. Copies the current
// delta-build buffer (124 bytes from the global region) into the payload. Here
// the caller supplies a DeltaWriter whose payload is serialized.
i32 QueueRequestState22(CommandQueue& q, const DeltaWriter& dw);

} // namespace guild::sim
