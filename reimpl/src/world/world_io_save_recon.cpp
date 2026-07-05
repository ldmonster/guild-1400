// gilde.exe — guild::world  (MODULE: world / scene object & building WRITE serializers)
//
// 1:1 reconstruction of VIBE_WorldIo_WriteObject (@0x5e5ab4),
// VIBE_WorldIo_WriteBuildingData (@0x5e5f74) and
// VIBE_WorldIo_WriteObjectCallback (@0x5e61ec).
//
// Every field offset, bit-extraction, loop bound and the object-type switch are
// transcribed verbatim from the IDA decompilation cross-checked against the
// disassembly (the decompiler's register tracking was degraded around the type
// byte and the name-pointer selection; the disassembly recovers the exact source
// registers — see comments at the relevant sites).
//
// Boundary handling (rules 1/4/8): file I/O is the rule-4 boundary. The original
// streams through VIBE_Vfs_WriteStream via the VIBE_Bio_* primitives. Here the
// pure serialization LOGIC is reconstructed against a byte-buffer cursor
// (guild::world::WorldIoSink). The Bio_* primitives are reproduced 1:1 as static
// leaves (each is a trivial raw-write wrapper in the binary):
//   VIBE_Bio_WriteByte       @0x5dc8cc  — 1 raw byte
//   VIBE_Bio_WriteDword      @0x5dc918  — 4 raw bytes (LE)
//   VIBE_Bio_WriteDwordPair  @0x5dcac0  — stages two dwords, writes only the FIRST
//   VIBE_Bio_WriteString     @0x5dc8ec  — strlen+1 raw bytes (incl. NUL)
//   VIBE_Bio_WriteVec3       @0x5dc9dc  — three dwords, one stream call each
//   VIBE_Bio_WriteVec4       @0x5dca40  — four dwords, one stream call each
//   VIBE_Bio_WriteArray      @0x5dcbb0  — u32 count, u32 stride, then count*stride
// and the two string leaves the writer calls inline:
//   VIBE_Util_StrChr         @0x5d3ef0  — LAST occurrence of a char (strrchr-like)
//   byte_64A208              @0x64a208  — MSVC ctype table (only its &0x20 bit used)
//
// Cross-slab pointer follows stored *inside* a record (node+492 stock block,
// node+488 part block, node+468 event-name list, node+508 child / node+496
// sibling, building room[0] -> sub-struct) are dereferenced as host pointers: the
// records are reconstructed by value elsewhere and these slots hold real pointers.
// No engine behaviour is approximated — only serialization order is reconstructed.
#include "world/world_io_save_recon.h"

#include <cstring>

namespace guild::world {

// ---------------------------------------------------------------------------
// Byte-buffer sink (the rule-4 file-I/O boundary, modelled as a cursor). A real
// build routes the same bytes through VIBE_Vfs_WriteStream; the byte stream is
// identical.
// ---------------------------------------------------------------------------

namespace {

// The active sink for the current top-level serialize call. The original threads
// the stream handle (a1/EBX) through every Bio_* call; we thread a sink pointer.
struct Sink {
    guild::u8* buf;
    guild::u32 cap;
    guild::u32 pos;
    bool       ok;
};

inline void RawWrite(Sink* s, const void* p, guild::u32 n) {
    if (!s->ok)
        return;
    // Overflow-safe capacity check (the original streams to a file, so it never
    // overruns; here a malformed/huge count must NOT wrap s->pos + n past 2^32
    // and slip a memcpy past the buffer). Compare in 64-bit and against the
    // remaining capacity.
    if (static_cast<guild::u64>(s->pos) + n > s->cap) {  // VfsWriteStream short write -> fail
        s->ok = false;
        return;
    }
    if (n)
        std::memcpy(s->buf + s->pos, p, n);
    s->pos += n;
}

// --- VIBE_Bio_* leaves (1:1) ----------------------------------------------
inline void BioWriteByte(Sink* s, guild::u8 v) { RawWrite(s, &v, 1); }

inline void BioWriteDword(Sink* s, guild::u32 v) {
    guild::u8 b[4];
    std::memcpy(b, &v, 4);
    RawWrite(s, b, 4);
}

// VIBE_Bio_WriteDwordPair @0x5dcac0 — the original lays out {first, second} on the
// stack but writes only the FIRST (size 4, count 1). Preserved exactly.
inline void BioWriteDwordPair(Sink* s, guild::u32 first) { BioWriteDword(s, first); }

// VIBE_Bio_WriteString @0x5dc8ec — strlen(str)+1 bytes (the trailing NUL included).
inline void BioWriteString(Sink* s, const char* str) {
    RawWrite(s, str, static_cast<guild::u32>(std::strlen(str)) + 1);
}

inline void BioWriteVec3(Sink* s, const guild::u8* p) {
    // three contiguous dwords, each its own stream call in the original
    guild::u32 v;
    std::memcpy(&v, p + 0, 4); BioWriteDword(s, v);
    std::memcpy(&v, p + 4, 4); BioWriteDword(s, v);
    std::memcpy(&v, p + 8, 4); BioWriteDword(s, v);
}

inline void BioWriteVec4(Sink* s, const guild::u8* p) {
    guild::u32 v;
    std::memcpy(&v, p + 0,  4); BioWriteDword(s, v);
    std::memcpy(&v, p + 4,  4); BioWriteDword(s, v);
    std::memcpy(&v, p + 8,  4); BioWriteDword(s, v);
    std::memcpy(&v, p + 12, 4); BioWriteDword(s, v);
}

// VIBE_Bio_WriteArray @0x5dcbb0 — u32 count, u32 stride, then count*stride bytes.
// (The original passes count as the element count and stride as element size to
//  VfsWriteStream; total payload = count*stride.)
inline void BioWriteArray(Sink* s, guild::u32 count, guild::u32 stride, const guild::u8* data) {
    BioWriteDword(s, count);
    BioWriteDword(s, stride);
    // The payload size is count*stride. A malformed building blob can make this
    // product overflow 32 bits; compute it in 64-bit and fail the sink (rather
    // than wrapping to a small n and emitting a truncated payload / reading past
    // `data`). The original feeds (count,stride) to VfsWriteStream, which the
    // file boundary bounds; the byte-cursor reconstruction must bound it here.
    guild::u64 total = static_cast<guild::u64>(count) * stride;
    if (total > s->cap || static_cast<guild::u64>(s->pos) + total > s->cap) {
        s->ok = false;
        return;
    }
    RawWrite(s, data, static_cast<guild::u32>(total));
}

// Little-endian field readers out of a raw record (host-endian-agnostic).
inline guild::u32 RdU32(const guild::u8* p) { guild::u32 v; std::memcpy(&v, p, 4); return v; }
inline guild::i32 RdI32(const guild::u8* p) { guild::i32 v; std::memcpy(&v, p, 4); return v; }

// Intra-record pointer-slot resolution (32-bit-faithful). The original is 32-bit, so
// every pointer slot the serializer follows (child +508, sibling +496, sub +492,
// part +488, event-list +468, the sub-record name pointer at sub+260, and the
// building room/room-base pointers) is a 4-byte slot. To stay 4-byte (and avoid
// 64-bit host-pointer overlap with adjacent serialized fields), those slots hold a
// 32-bit LINK ID that is resolved to a host pointer through an installable resolver:
// id 0 -> null (matching the original null pointer == 0). The default resolver maps
// every non-zero id to null (so a node with no installed graph serializes its own
// fields with no child/sibling recursion). Tests / a real build install a resolver
// that maps ids to the live records. The serialized BYTES are byte-exact; only the
// in-memory slot encoding (id vs raw pointer) differs from the 32-bit original (see
// types.h: serialized paths store explicit 32-bit ids, not pointers).
guild::world::LinkResolver g_linkResolver = nullptr;

inline guild::u8* ResolveLink(const guild::u8* slot) {
    guild::u32 id = RdU32(slot);
    if (id == 0)
        return nullptr;
    return g_linkResolver ? g_linkResolver(id) : nullptr;
}

// VIBE_Util_StrChr @0x5d3ef0 — despite the name, the binary scans the WHOLE
// string and keeps overwriting the result on every match, so it returns the
// LAST occurrence of `c` (strrchr semantics):
//   v2 = 0; do { if (a2 == *a1) v2 = a1; } while (*a1++); return v2;
// (For c == 0 it returns the terminator pointer; the writer only passes '_'.)
inline char* UtilStrChr(char* s, int c) {
    char* v2 = nullptr;
    do {
        if (static_cast<char>(c) == *s)
            v2 = s;
    } while (*s++);
    return v2;
}

// byte_64A208 @0x64a208 — MSVC ctype table (first 128 entries; the writer only
// indexes it with (char+1)&0xFF where char is an ASCII tail byte, and tests &0x20).
const guild::u8 kCType64A208[128] = {
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x03,0x03,0x03,0x03,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x0a,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x0c,0x58,0x58,0x58,0x58,0x58,0x58,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x0c,0x98,0x98,0x98,0x98,0x98,0x98,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x0c,0x0c,0x0c,0x0c,
};

// ---- forward ----
void WrObject(Sink* s, guild::u8* node);

// VIBE_Event_WriteEventNames @0x5f4b60 — the trailing event-name list serializer.
// Owned by the event module (out of this slice). In the original WriteObject
// *returns* its result. The binary:
//   if (!list)  return Bio_WriteDwordPair(h, 0);           // 4-byte 0 count
//   count = #entries (7 entries, stride 132) whose +0 dword is nonzero;
//   Bio_WriteDwordPair(h, count);
//   for (i = 0; i < 7; ++i) if (entry[i].dword0)
//       { Bio_WriteString(EventTable_LookupIdToName(i)); Bio_WriteString(entry+4); }
// The default (no hook) reproduces the null / empty-list paths byte-exactly
// (a single 4-byte 0 count dword). A non-empty list needs the event module's
// VIBE_EventTable_LookupIdToName (cross-module) — the hook carries that; with
// no hook installed the sink is failed rather than emitting unfaithful bytes.
EventNamesHook g_eventNamesHook = nullptr;

guild::u32 WriteEventNames(Sink* s, guild::u8* listHead) {
    if (g_eventNamesHook)
        return g_eventNamesHook(s->buf, s->cap, &s->pos, &s->ok, listHead);
    if (!listHead) {                    // 0x5f4b60: if (!a2) WriteDwordPair(a1, 0)
        BioWriteDwordPair(s, 0);
        return s->ok ? 1u : 0u;
    }
    // count the nonzero +0 dwords over the 7 fixed 132-byte entries (+0..+924)
    guild::u32 count = 0;
    for (int i = 0; i < 7; ++i)
        if (RdU32(listHead + 132 * i) != 0)
            ++count;
    BioWriteDwordPair(s, count);
    if (count != 0)
        s->ok = false;  // names need VIBE_EventTable_LookupIdToName -> hook required
    return s->ok ? 1u : 0u;
}

// ---------------------------------------------------------------------------
// VIBE_WorldIo_WriteObject @0x5e5ab4
// ---------------------------------------------------------------------------
void WrObject(Sink* s, guild::u8* node) {
    const guild::u8 flags529 = node[529];                 // [ebp+211h]

    // first write: bit 1 of node[529]  ((u8)(x<<6))>>7
    BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(flags529 << 6) >> 7));

    if (flags529 & 2) {
        const guild::u8 subtype = node[533];              // [ebp+215h]
        // name-pointer selection (recovered from disasm):
        //   subtype==1 : switchVar = node[534]; name = node[0]=='!' ? node+1 : node
        //   subtype!=1 : switchVar = node[533]; name = node
        guild::u8  switchVar;
        const char* name;
        if (subtype == 1) {
            switchVar = node[534];                        // [ebp+216h]
            name = reinterpret_cast<const char*>(node[0] == 0x21 ? node + 1 : node);
        } else {
            switchVar = subtype;
            name = reinterpret_cast<const char*>(node);
        }
        BioWriteString(s, name);
        BioWriteDwordPair(s, RdU32(node + 512));                  // [ebp+200h]
        BioWriteDwordPair(s, static_cast<guild::u32>(RdI32(node + 532) >> 24)); // [ebp+214h] sar 24
        BioWriteDwordPair(s, static_cast<guild::u32>(static_cast<guild::i32>(
                                 static_cast<signed char>(switchVar))));        // movsx cl
        BioWriteByte(s, subtype == 1 ? 1 : 0);            // node[533]==1

        // switch ((unsigned)switchVar) with 9 cases (cmp 8 / ja default)
        switch (switchVar) {
        case 0:
            BioWriteByte(s, static_cast<guild::u8>(flags529 & 1));   // node[529]&1
            BioWriteDword(s, RdU32(node + 104));                     // [ebp+68h]
            BioWriteVec3(s, node + 76);                              // [ebp+4Ch]
            BioWriteVec3(s, node + 92);                              // [ebp+5Ch]
            BioWriteVec3(s, node + 144);                             // [ebp+90h]
            break;

        case 1:
        case 4: {
            // bit fields of node[528..530]
            BioWriteByte(s, static_cast<guild::u8>(node[528] >> 7));                       // bit7 of 528
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[528] << 2) >> 7)); // bit5 of 528
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[529] << 5) >> 7)); // bit2 of 529
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[530] << 4) >> 6)); // bits 2..3 of 530
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[530] << 3) >> 7)); // bit4 of 530
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[530] << 2) >> 7)); // bit5 of 530
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[530] << 1) >> 7)); // bit6 of 530
            BioWriteDwordPair(s, static_cast<guild::u32>(RdI32(node + 529) >> 24));        // [ebp+211h] sar24
            guild::u8* sub = ResolveLink(node + 492);                                      // [ebp+1ECh]
            if (sub) {
                BioWriteDwordPair(s, sub[2316]);                                           // [esi+90Ch] u8
                if (sub[2316]) {
                    // copy the wide-ish name pair-by-pair into a local buffer until a
                    // 0 byte, then trim a 2-char "_X" tail when X+1 is a ctype &0x20.
                    char tmp[276];
                    const guild::u8* src = ResolveLink(sub + 260);                         // [esi+104h]
                    char* dst = tmp;
                    for (;;) {
                        char c0 = static_cast<char>(*src);
                        dst[0] = c0;
                        if (!c0)
                            break;
                        char c1 = static_cast<char>(src[1]);
                        src += 2;
                        dst[1] = c1;
                        dst += 2;
                        if (!c1)
                            break;
                    }
                    char* us = UtilStrChr(tmp, '_');
                    if (us && std::strlen(us) == 2 &&
                        (kCType64A208[static_cast<guild::u8>(us[1] + 1)] & 0x20) != 0) {
                        *us = 0;
                    }
                    BioWriteString(s, tmp);
                }
            } else {
                BioWriteDwordPair(s, 0);
            }
        }
            // fall through to the case 2/3 body (LABEL_31)
            [[fallthrough]];
        case 2:
        case 3:
            BioWriteVec3(s, node + 76);                              // [ebp+4Ch]
            BioWriteVec3(s, node + 132);                             // [ebp+84h]
            BioWriteByte(s, static_cast<guild::u8>(flags529 & 1));   // node[529]&1
            if (flags529 & 1) {
                BioWriteVec3(s, node + 92);                          // [ebp+5Ch]
                BioWriteVec3(s, node + 144);                         // [ebp+90h]
            }
            // 10 iterations: write two vec3 per element (stride 24)
            for (int i = 0; i < 10; ++i) {
                BioWriteVec3(s, node + 156 + 24 * i);               // [ebp+9Ch] + i*24
                BioWriteVec3(s, node + 168 + 24 * i);               // [ebp+0A8h] + i*24
            }
            break;

        case 5:
        case 6:
        case 7:
        case 8: {
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[528] << 2) >> 7)); // bit5 of 528
            BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(node[529] << 3) >> 7)); // bit4 of 529
            BioWriteDword(s, RdU32(node + 144));                     // [ebp+90h]
            BioWriteDword(s, RdU32(node + 148));                     // [ebp+94h]
            BioWriteDword(s, RdU32(node + 152));                     // [ebp+98h]
            BioWriteVec3(s, node + 92);                              // [ebp+5Ch]
            BioWriteVec3(s, node + 76);                              // [ebp+4Ch]
            BioWriteVec3(s, node + 132);                             // [ebp+84h]
            guild::u8* part = ResolveLink(node + 488);               // [ebp+1E8h]
            BioWriteDword(s, RdU32(part + 412));                     // [eax+19Ch]
            BioWriteDwordPair(s, RdU32(part + 416));                 // [edx+1A0h]
            for (guild::u32 off = 0; off != 392; off += 56) {        // 0x188, stride 0x38
                BioWriteDword(s, RdU32(part + off + 40));            // +28h
                BioWriteDword(s, RdU32(part + off + 36));            // +24h
                BioWriteDword(s, RdU32(part + off + 44));            // +2Ch
                BioWriteVec3(s, part + off + 24);                    // +18h
                BioWriteVec3(s, part + off + 0);                     // +0
                BioWriteVec3(s, part + off + 12);                    // +0Ch
                BioWriteDword(s, RdU32(part + off + 48));            // +30h
                BioWriteDwordPair(s, RdU32(part + off + 52));        // +34h
            }
        }
            break;

        default:
            // VIBE_ErrorLog_ReportMessage("Unknown Object-Type in wr_object()!")
            // (diagnostic only; no bytes emitted)
            break;
        }
    }

    // child link: first descendant at node+508 whose +529 flag does NOT have bit 1
    guild::u8* child = ResolveLink(node + 508);                      // [ebp+1FCh]
    while (child) {
        if (child[529] & 2)       // loop continues while NOT set; break when set
            break;
        child = ResolveLink(child + 496);                            // [edi+1F0h]
    }
    BioWriteByte(s, child != nullptr ? 1 : 0);
    if (child)
        WrObject(s, child);

    // sibling link: first sibling at node+496 with (528&1)!=1 && (529&2)
    guild::u8* sib = ResolveLink(node + 496);                       // [ebp+1F0h]
    while (sib) {
        if ((sib[528] & 1) != 1 && (sib[529] & 2) != 0)
            break;
        sib = ResolveLink(sib + 496);
    }
    BioWriteByte(s, sib != nullptr ? 1 : 0);
    if (sib)
        WrObject(s, sib);

    WriteEventNames(s, ResolveLink(node + 468));                    // [ebp+1D4h]
}

// ---------------------------------------------------------------------------
// VIBE_WorldIo_WriteBuildingData @0x5e5f74    (esi=h, edi=bld)
// ---------------------------------------------------------------------------
void WrBuildingData(Sink* s, const guild::u8* bld) {
    BioWriteString(s, reinterpret_cast<const char*>(bld + 6628));  // +0x19E4
    if (bld[6628]) {
        const guild::u32 n = RdU32(bld + 0);                       // [edi]
        BioWriteDwordPair(s, n);
        BioWriteArray(s, n, n, ResolveLink(bld + 16));             // stride n, data [edi+10h]
        BioWriteArray(s, n, 4 * n, ResolveLink(bld + 32));         // stride 4n, data [edi+20h]
        BioWriteByte(s, RdU32(bld + 24) != 0 ? 1 : 0);             // [edi+18h]
        if (RdU32(bld + 24)) {
            const guild::u8 roomCount = bld[7277];                 // [edi+1C6Dh]
            BioWriteDwordPair(s, roomCount);
            if (roomCount != 0) {
                // 0x5e6054: cmp byte [edi+1C6Dh],0 ; ja loc_5E6186 — a NONZERO
                // room count jumps to 0x5e6186, which writes the +24 array
                // (WriteArray(n, [edi+18h], n)) and then enters the room loop.
                // roomCount == 0 falls through to the loop init and writes nothing.
                BioWriteArray(s, n, n, ResolveLink(bld + 24));
            }
            const guild::u8* roomBase = ResolveLink(bld + 6624);    // [edi+19E0h]
            for (int i = 0; i < static_cast<int>(bld[7277]); ++i) {
                const guild::u8* room = roomBase + i * 344;         // ecx += 0x158
                guild::u8* roomPtr0 = ResolveLink(room + 0);        // *(ptr at base)
                guild::u8 v16;
                if (roomPtr0 == nullptr) {
                    BioWriteString(s, "");                          // byte_62BDEC (empty)
                    BioWriteByte(s, 0);
                    BioWriteByte(s, 0);
                    v16 = 0;
                } else {
                    BioWriteString(s, reinterpret_cast<const char*>(roomPtr0));
                    BioWriteByte(s, 0);
                    BioWriteByte(s, static_cast<guild::u8>(roomPtr0[0x72] & 0x0F)); // [eax+72h]&0F
                    v16 = static_cast<guild::u8>(roomPtr0[0x68] & 1);               // [eax+68h]&1
                }
                BioWriteByte(s, v16);
                BioWriteByte(s, room[8]);                           // [ecx+eax+8]
                BioWriteByte(s, room[9]);                           // +9
                BioWriteByte(s, static_cast<guild::u8>(room[10] & 1));             // +0Ah & 1
                BioWriteByte(s, static_cast<guild::u8>(static_cast<guild::u8>(room[10] << 6) >> 7)); // bit1 of +0Ah
                BioWriteDword(s, RdU32(room + 20));                 // +14h
                BioWriteDword(s, RdU32(room + 12));                 // +0Ch
                BioWriteDword(s, RdU32(room + 16));                 // +10h
                BioWriteVec4(s, room + 24);                         // +18h
                BioWriteVec4(s, room + 40);                         // +28h
                BioWriteDwordPair(s, static_cast<guild::u32>(RdI32(room + 337) >> 24)); // +151h sar24
            }
        }
    } else {
        BioWriteByte(s, 0);
    }

    BioWriteString(s, reinterpret_cast<const char*>(bld + 6692));   // +0x1A24
    if (bld[6692]) {
        const guild::u32 n = RdU32(bld + 0);
        BioWriteArray(s, n, n, ResolveLink(bld + 20));              // [edi+14h]
    }
    // 8 fixed strings at +6756 .. +7268, stride 64
    for (const guild::u8* p = bld + 6756; p != bld + 7268; p += 64)
        BioWriteString(s, reinterpret_cast<const char*>(p));

    BioWriteDword(s, RdU32(bld + 160));                            // [edi+0A0h]
    BioWriteDword(s, RdU32(bld + 196));                            // [edi+0C4h]
    BioWriteVec3(s, bld + 144);                                    // [edi+90h]
}

// ---------------------------------------------------------------------------
// VIBE_WorldIo_WriteObjectCallback @0x5e61ec
// ---------------------------------------------------------------------------
void WrObjectCallback(Sink* s, guild::u8* node) {
    BioWriteByte(s, 1);
    const guild::u32 savedSib = RdU32(node + 496);                 // esi = [ecx+1F0h]
    guild::u32 zero = 0;
    std::memcpy(node + 496, &zero, 4);                             // [ecx+1F0h] = 0
    WrObject(s, node);
    std::memcpy(node + 496, &savedSib, 4);                         // restore
}

// ---- VfsHandle* facade: drive the sink off the handle's backing buffer -----
// The public API matches the rest of guild::io/world (a VfsHandle*). For the
// portable reconstruction the handle is treated as carrying a (buf,cap,pos)
// triple; tests use the sink-based path directly via the test shim below.

} // namespace

// ---------------------------------------------------------------------------
// Public, sink-based entry points (self-contained; no VFS runtime needed).
// ---------------------------------------------------------------------------
WorldIoSink WorldIoSinkOpen(guild::u8* buffer, guild::u32 capacity) {
    return WorldIoSink{buffer, capacity, 0, true};
}

void WorldIoSetEventNamesHook(EventNamesHook hook) { g_eventNamesHook = hook; }

void WorldIoSetLinkResolver(LinkResolver resolver) { g_linkResolver = resolver; }

bool WorldIoWriteObjectS(WorldIoSink* dst, guild::u8* node) {
    Sink s{dst->buf, dst->cap, dst->pos, dst->ok};
    WrObject(&s, node);
    dst->pos = s.pos;
    dst->ok  = s.ok;
    return s.ok;
}

bool WorldIoWriteBuildingDataS(WorldIoSink* dst, const guild::u8* bld) {
    Sink s{dst->buf, dst->cap, dst->pos, dst->ok};
    WrBuildingData(&s, bld);
    dst->pos = s.pos;
    dst->ok  = s.ok;
    return s.ok;
}

bool WorldIoWriteObjectCallbackS(WorldIoSink* dst, guild::u8* node) {
    Sink s{dst->buf, dst->cap, dst->pos, dst->ok};
    WrObjectCallback(&s, node);
    dst->pos = s.pos;
    dst->ok  = s.ok;
    return s.ok;
}

} // namespace guild::world
