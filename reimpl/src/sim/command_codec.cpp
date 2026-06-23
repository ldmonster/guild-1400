#include "sim/command_codec.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// DeltaWriter — VIBE_Command_BeginDeltaPacket / Append{Delta,Raw,Copied}Field
// ===========================================================================

void DeltaWriter::Reset(const void* entityBase) {
    std::memset(payload_, 0, sizeof(payload_));
    cursor_      = 0;
    field_count_ = 0;
    entity_id_   = 0;
    entity_      = static_cast<const u8*>(entityBase);
}

// gilde.exe 0x493a94 — VIBE_Command_BeginDeltaPacket. The original resolves the
// entity by id; here the live base is passed in. SetGrayColorThunk(0,119) is a
// memset of the 119-byte payload region.
void DeltaWriter::BeginDeltaPacket(const void* entityBase, u32 entityId) {
    entity_id_   = entityId;          // dword_11AA3E0
    field_count_ = 0;                 // byte_11AA3E4
    std::memset(payload_, 0, kMaxPayload); // SetGrayColorThunk(0,119)
    cursor_      = 0;                 // dword_11AA460
    entity_      = static_cast<const u8*>(entityBase); // dword_11AA474
}

// gilde.exe 0x493aec — VIBE_Command_AppendDeltaField. Layout written per field:
//   [width:1][count:1][offset:2][ (new-old) values: width*count ]
// The "old" values are read from the live entity at fieldOffset.
int DeltaWriter::AppendDeltaField(u8 width, u8 count, u16 fieldOffset, const void* newValues) {
    if (width != 1 && width != 2 && width != 4)
        return 1;
    u32 v4 = static_cast<u32>(width) * count;          // payload value bytes
    if (v4 + cursor_ + 4 >= kMaxPayload)               // 0x77 guard
        return 1;

    u8* hdr = payload_ + cursor_;
    ++field_count_;
    hdr[0] = width;                                    // a1
    hdr[1] = count;                                    // a2
    // offset stored little-endian as a 16-bit word (the original writes *v6=a4).
    hdr[2] = static_cast<u8>(fieldOffset);
    hdr[3] = static_cast<u8>(fieldOffset >> 8);
    cursor_ += v4 + 4;

    const u8* oldBase = entity_ + fieldOffset;         // dword_11AA474 + a4
    u8* out = hdr + 4;
    const u8* in = static_cast<const u8*>(newValues);

    if (width == 1) {
        for (u32 i = 0; i < count; ++i) {
            u8 nv = in[i];
            u8 ov = oldBase[i];
            out[i] = static_cast<u8>(nv - ov);
        }
    } else if (width == 2) {
        for (u32 i = 0; i < count; ++i) {
            u16 nv = static_cast<u16>(in[2 * i] | (in[2 * i + 1] << 8));
            u16 ov = static_cast<u16>(oldBase[2 * i] | (oldBase[2 * i + 1] << 8));
            u16 d  = static_cast<u16>(nv - ov);
            out[2 * i]     = static_cast<u8>(d);
            out[2 * i + 1] = static_cast<u8>(d >> 8);
        }
    } else { // width == 4
        for (u32 i = 0; i < count; ++i) {
            u32 nv = static_cast<u32>(in[4 * i]) | (static_cast<u32>(in[4 * i + 1]) << 8)
                   | (static_cast<u32>(in[4 * i + 2]) << 16) | (static_cast<u32>(in[4 * i + 3]) << 24);
            u32 ov = static_cast<u32>(oldBase[4 * i]) | (static_cast<u32>(oldBase[4 * i + 1]) << 8)
                   | (static_cast<u32>(oldBase[4 * i + 2]) << 16) | (static_cast<u32>(oldBase[4 * i + 3]) << 24);
            u32 d  = nv - ov;
            out[4 * i]     = static_cast<u8>(d);
            out[4 * i + 1] = static_cast<u8>(d >> 8);
            out[4 * i + 2] = static_cast<u8>(d >> 16);
            out[4 * i + 3] = static_cast<u8>(d >> 24);
        }
    }
    return 0;
}

// gilde.exe 0x493c14 — VIBE_Command_AppendRawField. Absolute values via memcpy.
int DeltaWriter::AppendRawField(u8 width, u8 count, u16 fieldOffset, const void* values) {
    if (width != 1 && width != 2 && width != 4)
        return 1;
    u32 v5 = static_cast<u32>(width) * count;
    if (v5 + cursor_ + 4 >= kMaxPayload)
        return 1;

    u8* hdr = payload_ + cursor_;
    ++field_count_;
    hdr[0] = width;
    hdr[1] = count;
    hdr[2] = static_cast<u8>(fieldOffset);
    hdr[3] = static_cast<u8>(fieldOffset >> 8);
    cursor_ += v5 + 4;
    std::memcpy(hdr + 4, values, v5);
    return 0;
}

// gilde.exe 0x493c90 — VIBE_Command_AppendCopiedField. Element-by-element copy
// (width-aware). Identical wire result to AppendRawField for widths 1/2/4.
int DeltaWriter::AppendCopiedField(u8 width, u8 count, u16 fieldOffset, const void* values) {
    if (width != 1 && width != 2 && width != 4)
        return 1;
    u32 v5 = static_cast<u32>(count) * width;
    if (v5 + cursor_ + 4 >= kMaxPayload)
        return 1;

    u8* hdr = payload_ + cursor_;
    ++field_count_;
    hdr[0] = width;
    hdr[1] = count;
    hdr[2] = static_cast<u8>(fieldOffset);
    hdr[3] = static_cast<u8>(fieldOffset >> 8);

    const u8* in = static_cast<const u8*>(values);
    u8* out = hdr + 4;
    if (width == 1) {
        for (u32 i = 0; i < count; ++i)
            out[i] = in[i];
    } else if (width == 2) {
        for (u32 i = 0; i < count; ++i) {
            out[2 * i]     = in[2 * i];
            out[2 * i + 1] = in[2 * i + 1];
        }
    } else { // width == 4
        for (u32 i = 0; i < count; ++i) {
            out[4 * i]     = in[4 * i];
            out[4 * i + 1] = in[4 * i + 1];
            out[4 * i + 2] = in[4 * i + 2];
            out[4 * i + 3] = in[4 * i + 3];
        }
    }
    cursor_ += v5 + 4;
    return 0;
}

// Decode/apply: walk (width,count,offset,deltas) triplets, add each delta into
// the matching bytes of `entity`. This is what the Ex* apply handlers do on the
// receive side; provided here so the codec round-trips in isolation.
void DeltaWriter::ApplyDelta(const u8* payload, u8 fieldCount, void* entity) {
    u8* base = static_cast<u8*>(entity);
    const u8* p = payload;
    // HARDENING (wave-11): the payload region the encoder fills is bounded by the
    // 119-byte cursor guard (kMaxPayload) in Append{Delta,Raw,Copied}Field; every
    // caller passes either DeltaWriter::payload() (119 bytes) or the packet payload
    // window (a1+0x11, also 119 bytes to the 153-byte record end). A malformed
    // fieldCount/record can otherwise drive `p`/`vals` past that window (OOB read).
    // Valid payloads stay within the guard, so this is byte-identical on valid input.
    const u8* end = payload + kMaxPayload;
    for (u8 f = 0; f < fieldCount; ++f) {
        if (p + 4 > end)
            break;
        u8 width  = p[0];
        u8 count  = p[1];
        u16 off   = static_cast<u16>(p[2] | (p[3] << 8));
        const u8* vals = p + 4;
        if (vals + static_cast<u32>(width) * count > end)
            break;
        u8* dst = base + off;
        if (width == 1) {
            for (u32 i = 0; i < count; ++i)
                dst[i] = static_cast<u8>(dst[i] + vals[i]);
        } else if (width == 2) {
            for (u32 i = 0; i < count; ++i) {
                u16 ov = static_cast<u16>(dst[2 * i] | (dst[2 * i + 1] << 8));
                u16 dv = static_cast<u16>(vals[2 * i] | (vals[2 * i + 1] << 8));
                u16 nv = static_cast<u16>(ov + dv);
                dst[2 * i]     = static_cast<u8>(nv);
                dst[2 * i + 1] = static_cast<u8>(nv >> 8);
            }
        } else if (width == 4) {
            for (u32 i = 0; i < count; ++i) {
                u32 ov = static_cast<u32>(dst[4 * i]) | (static_cast<u32>(dst[4 * i + 1]) << 8)
                       | (static_cast<u32>(dst[4 * i + 2]) << 16) | (static_cast<u32>(dst[4 * i + 3]) << 24);
                u32 dv = static_cast<u32>(vals[4 * i]) | (static_cast<u32>(vals[4 * i + 1]) << 8)
                       | (static_cast<u32>(vals[4 * i + 2]) << 16) | (static_cast<u32>(vals[4 * i + 3]) << 24);
                u32 nv = ov + dv;
                dst[4 * i]     = static_cast<u8>(nv);
                dst[4 * i + 1] = static_cast<u8>(nv >> 8);
                dst[4 * i + 2] = static_cast<u8>(nv >> 16);
                dst[4 * i + 3] = static_cast<u8>(nv >> 24);
            }
        }
        p += static_cast<u32>(width) * count + 4;
    }
}

// ===========================================================================
// AiMethodWriter — VIBE_Command_BeginAiMethodPacket / AppendAiMethodEntry
// ===========================================================================

// gilde.exe 0x493d64 — VIBE_Command_BeginAiMethodPacket. Resets the 5-byte-entry
// payload and stashes the target person id (the original reads *(a1+4)).
void AiMethodWriter::Begin(u32 personId) {
    person_id_ = personId;            // dword_11AA360
    count_     = 0;                   // byte_11AA364
    std::memset(payload_, 0, kMaxPayload);
    cursor_    = 0;                   // dword_11AA45C
}

// gilde.exe 0x493d9c — VIBE_Command_AppendAiMethodEntry. Each entry is 5 bytes:
//   [statId:1][delta:4]   (delta little-endian)
int AiMethodWriter::AppendEntry(u8 statId, i32 delta) {
    if (cursor_ + 5 >= kMaxPayload)   // 0x77 guard
        return 1;
    u8* out = payload_ + cursor_;
    cursor_ += 5;
    ++count_;
    out[0] = statId;
    u32 d = static_cast<u32>(delta);
    out[1] = static_cast<u8>(d);
    out[2] = static_cast<u8>(d >> 8);
    out[3] = static_cast<u8>(d >> 16);
    out[4] = static_cast<u8>(d >> 24);
    return 0;
}

// ===========================================================================
// Request builders (representative set). Each stages a 153-byte temp, sets the
// opcode at +0 and its fields in the payload (>= +0x10), then EnqueuePacket.
// ===========================================================================

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void put16(CommandPacket& p, u32 off, i16 v) { p.put16(off, static_cast<u16>(v)); }
} // namespace

// gilde.exe 0x494630 — VIBE_Command_QueueRequest16 (opcode 16).
i32 QueueRequest16(CommandQueue& q, i32 a1, i32 a2, i32 a3, u8 a4) {
    CommandPacket p{};
    p.opcode() = 16;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    p.bytes[0x1C] = a4;
    put32(p, 0x1D, a3);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49465c — VIBE_Command_QueueRequest17 (opcode 17).
i32 QueueRequest17(CommandQueue& q, i32 a1, i32 a2, i32 a3, i16 a4, u8 a5, i32 a6) {
    CommandPacket p{};
    p.opcode() = 17;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put16(p, 0x18, a4);
    p.bytes[0x1E] = a5;
    put32(p, 0x1F, a3);
    put32(p, 0x23, a6);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494810 — VIBE_Command_QueueRequestArgs25 (opcode 25).
i32 QueueRequestArgs25(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4, i32 a5) {
    CommandPacket p{};
    p.opcode() = 25;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    put32(p, 0x1C, a3);
    put32(p, 0x20, a5);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494878 — VIBE_Command_QueueRequestCoord27 (opcode 27). The
// original converts a world position to packed coords via VIBE_Coord_ConvertX;
// here the caller supplies the two packed i32 components directly. Field +0x20
// (the global flt_62EB94 stash in the original) is left zero.
i32 QueueRequestCoord27(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 coordX, i32 coordY) {
    CommandPacket p{};
    p.opcode() = 27;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a3);
    put32(p, 0x1C, coordX);
    put32(p, 0x24, coordY);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4944f0 — VIBE_Command_EnqueueObjectInteraction (opcode 11).
i32 EnqueueObjectInteraction(CommandQueue& q, u8 a1, i32 a2, i16 a3, i32 a4,
                             i32 a5, u8 a6, u8 a7, u8 a8) {
    CommandPacket p{};
    p.opcode() = 11;
    p.bytes[0x14] = a1;
    put32(p, 0x15, a2);
    put32(p, 0x19, a4);
    put16(p, 0x1D, a3);
    put32(p, 0x1F, a5);
    p.bytes[0x23] = a6;
    p.bytes[0x24] = a7;
    p.bytes[0x25] = a8;
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494750 — VIBE_Command_QueueRequestState22 (opcode 22). The
// original qmemcpy's 124 bytes from the delta-build global region into the
// payload (the field-count byte + the field triplets). We serialize the
// DeltaWriter the same way: field-count at +0x10, payload triplets following.
i32 QueueRequestState22(CommandQueue& q, const DeltaWriter& dw) {
    CommandPacket p{};
    p.opcode() = 22;
    // Mirror the global block layout: [fieldCount byte][... payload ...].
    p.bytes[0x10] = dw.field_count();
    u32 n = dw.cursor();
    if (n > 123) n = 123;
    std::memcpy(p.bytes + 0x11, dw.payload(), n);
    return q.EnqueuePacket(p);
}

} // namespace guild::sim
