// gilde.exe — guild::io  (MODULE: savegame city/person/character table writer)
//
// 1:1 reconstruction of VIBE_Save_WriteCityAndPersonTables @0x5a57f4. Counts,
// iteration order, strides, pointer<->index fixups and the per-character field set
// are transcribed from the disassembly (the decompiler's register tracking was
// degraded around the copy-path probe and the staging buffer; the disassembly at
// 0x5a594f..0x5a5c14 recovers them exactly).
#include "io/save_world_tables_save_recon.h"

#include <cstring>

namespace guild::io {

namespace {

inline bool WR(SaveTableSink* s, const void* p, guild::u32 n) {
    if (!s->ok)
        return false;
    if (s->pos + n > s->cap) {        // VfsWriteStream short write -> failure (test eax,eax; jbe)
        s->ok = false;
        return false;
    }
    if (n)
        std::memcpy(s->buf + s->pos, p, n);
    s->pos += n;
    return true;
}
inline bool WRu32(SaveTableSink* s, guild::u32 v) { return WR(s, &v, 4); }

inline guild::u32 RdU32(const guild::u8* p) { guild::u32 v; std::memcpy(&v, p, 4); return v; }
inline void       WrU32(guild::u8* p, guild::u32 v) { std::memcpy(p, &v, 4); }
inline guild::i16 RdI16(const guild::u8* p) { guild::i16 v; std::memcpy(&v, p, 2); return v; }
// follow a stored pointer slot (host pointer in this by-value reconstruction)
inline guild::u8* RdPtr(const guild::u8* p) { guild::u8* v; std::memcpy(&v, p, sizeof(v)); return v; }

} // namespace

SaveTableSink SaveTableSinkOpen(guild::u8* buffer, guild::u32 capacity) {
    return SaveTableSink{buffer, capacity, 0, true};
}

bool SaveWriteCityAndPersonTables(SaveTableSink* sink, const SaveTableEnv& env,
                                  WriteObjectRecordFn writeObjRec,
                                  CharProbeHook probe, void* user) {
    SaveTableSink* s = sink;

    // ---- 1. PERSON TABLE -------------------------------------------------
    guild::u32 count = 0;
    for (guild::u32 off = 0; off != kSwt_PersonSpan; off += kSwt_PersonStride) {
        if (RdU32(env.personBase + off) != 0)   // dword_12CEA94[i] != 0
            ++count;
    }
    if (!WRu32(s, count))
        return false;
    for (guild::u32 off = 0; off < kSwt_PersonSpan; off += kSwt_PersonStride) {
        const guild::u8* entryPtr = RdPtr(env.personBase + off);   // dword_12CEA94[i]
        if (entryPtr) {
            const guild::i32 link =
                static_cast<guild::i32>(RdU32(env.personLink + off)); // dword_12CE914[i]
            if (!writeObjRec(s, entryPtr, link, user))
                return false;
        }
    }

    // ---- 2. CITY-TOWER TABLE --------------------------------------------
    count = 0;
    for (guild::u32 off = 0; off != kSwt_CityIndexSpan; off += kSwt_CityIndexStride) {
        const guild::i16 idx = RdI16(env.cityIndexBase + off);     // *(i16*)(off + dword_13CE290)
        if (env.cityRecordBase[kSwt_CityRecordStride * static_cast<guild::u32>(static_cast<guild::i32>(idx))]
                == kSwt_CityLiveTag)                                // record[0]==29
            ++count;
    }
    if (!WRu32(s, count))
        return false;
    for (guild::u32 off = 0; off < kSwt_CityIndexSpan; off += kSwt_CityIndexStride) {
        const guild::u8* ent = env.cityIndexBase + off;
        const guild::i16 idx = RdI16(ent);
        if (env.cityRecordBase[kSwt_CityRecordStride * static_cast<guild::u32>(static_cast<guild::i32>(idx))]
                == kSwt_CityLiveTag) {
            const guild::u8* recPtr = RdPtr(ent + 0x3B);           // *(char**)(idx+59)
            const guild::i32 link = static_cast<guild::i32>(RdU32(ent + 0x02)); // *(DWORD*)(idx+2)
            if (!writeObjRec(s, recPtr, link, user))
                return false;
        }
    }

    // ---- 3. CHARACTER SLAB ----------------------------------------------
    guild::u8* base = env.charBase;

    // 32-bit-faithful slab-link encoding (see header "Pointer-slot width caveat").
    // The original is 32-bit, so slots +0x14/+0x24/+0x28 are 4-byte. We model the
    // slab LINK slots (+0x24/+0x28) as a 32-bit byte-offset-into-the-slab where 0 ==
    // null (matching the original null pointer == 0); the pre-pass converts that to
    // the loader's record index (offset/stride) or -1, exactly as the binary's
    // (ptr-base)/stride / -1. The handle slot (+0x14) holds the 32-bit handle index
    // directly; the original's `*(DWORD*)slot` deref of a pointer-to-index is the
    // identity in this id-based model. All slot I/O is 4-byte (no host pointers are
    // stored in these slots, so there is no 64-bit overlap).

    // PRE-PASS: convert link slots to the serialisable record-index form.
    for (guild::u32 off = 0; off != kSwt_CharSpan; off += kSwt_CharStride) {
        guild::u8* rec = base + off;
        if (rec[9] == 0) {
            std::memset(rec, 0, kSwt_CharStride);                  // dead slot -> zero
            continue;
        }
        // slot+0x24: link byte-offset -> record index, or -1 (0xFFFFFFFF) when null
        guild::u32 o24 = RdU32(rec + 0x24);
        WrU32(rec + 0x24, o24 ? o24 / kSwt_CharStride : 0xFFFFFFFFu);
        // slot+0x28: same
        guild::u32 o28 = RdU32(rec + 0x28);
        WrU32(rec + 0x28, o28 ? o28 / kSwt_CharStride : 0xFFFFFFFFu);
    }

    // WRITE LOOP: per record emit a leading byte + nine fixed fields from a
    // zeroed-then-maybe-copied 404-byte staging buffer.
    for (guild::u32 off = 0; off != kSwt_CharSpan; off += kSwt_CharStride) {
        guild::u8 staging[kSwt_CharStride];
        std::memset(staging, 0, sizeof(staging));                  // SetGrayColorThunk(0,404,esp)
        guild::u8 leading = 0;                                     // var_1C

        guild::u8* rec = base + off;
        // ecx = handleTable[rec[+0x14]] ; IsValidPointer(ecx) else int3
        const guild::u8* handle =
            env.handleTable ? env.handleTable[RdU32(rec + 0x14)] : nullptr;
        // copy-path probe (IsValidPointer + *(*(handle+0x34)+0x200)!=0)
        const bool copyPath = handle && probe && probe(handle, user);
        if (copyPath) {
            leading = rec[9];                                      // [esi+9]
            std::memcpy(staging, rec, kSwt_CharStride);            // repne movsd/movsb
        }

        if (!WR(s, &leading, 1))
            return false;
        if (!WR(s, staging + 12, 4)  || !WR(s, staging + 20, 4)
            || !WR(s, staging + 48, 32) || !WR(s, staging + 80, 32)
            || !WR(s, staging + 240, 160) || !WR(s, staging + 400, 1)
            || !WR(s, staging + 36, 4)  || !WR(s, staging + 40, 4))
            return false;
    }

    // POST-PASS: restore the link slots to their byte-offset (in-memory) form.
    // (The original restores live pointers: index*stride + base / 0; in our offset
    //  model that is index*stride / 0, with +0x14 left as the handle index.)
    for (guild::u32 off = 0; off != kSwt_CharSpan; off += kSwt_CharStride) {
        guild::u8* rec = base + off;
        if (rec[9] == 0)
            continue;
        // slot+0x24: record index -> byte offset (index*404), or 0 when index == -1
        guild::u32 i24 = RdU32(rec + 0x24);
        WrU32(rec + 0x24, i24 == 0xFFFFFFFFu ? 0u : i24 * kSwt_CharStride);
        // slot+0x28: same
        guild::u32 i28 = RdU32(rec + 0x28);
        WrU32(rec + 0x28, i28 == 0xFFFFFFFFu ? 0u : i28 * kSwt_CharStride);
    }

    return true;
}

} // namespace guild::io
