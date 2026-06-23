// gilde.exe — guild::render model/AGF chunk-I/O + resource-handle helpers.
// 1:1 reconstruction; see modelio_recon.h for the function/offset map.
//
// The original records interleave 4-byte pointer and i32 slots, which cannot be
// stored as raw host pointers on a 64-bit build without aliasing. Following the
// codebase convention (geometry_types.h), the helpers operate on typed C++ views
// of just the fields they touch; the original byte offsets are kept in comments
// and the strides are asserted to match the binary.
#include "render/modelio_recon.h"
#include "util/math.h"   // VIBE_Math_TriangleNormal @0x5cb824, VIBE_Math_VectorNormalize @0x5cb148

#include <cmath>
#include <cstring>

namespace guild::render {

// ===========================================================================
//  VIBE_Model_ComputeNormals @0x5f8eb0
// ===========================================================================
// gilde.exe 0x5f8eb0 — VIBE_Model_ComputeNormals (__usercall, eax = record)
// Pass 1: per-face triangle normal -> face.nx/ny/nz (face+44/+48/+52).
// Pass 2: per-vertex accumulate of referencing faces' normals -> normalize ->
//         vertex.nx/ny/nz (vertex+12/+16/+20).
//
// Original (a1 as _DWORD*): a1[16]=verts(+64), a1[17]=vertCount(+68),
//   a1[18]=faces(+72), a1[19]=faceCount(+76). Face indices @+24/+28/+32; the
//   normal output v4=(float*)(face+44). Per-face the binary computes
//   v6 = verts + 24*face[+32], v7 = verts + 24*face[+28], v8 = verts + 24*face[+24]
//   and calls TriangleNormal(v8, v7, /*out*/ v4, v6).
//   TriangleNormal(a1,a2,a3,a4): a3(out) = (a2-a1) x (a4-a1) (see 0x5cb824). With
//   the binary's call TriangleNormal(v8, v7, /*out*/v4, v6) that is
//   normal = (v7-v8) x (v6-v8). The reimpl TriangleNormal(a,b,c,out) computes
//   out = (b-a) x (c-a), so a=v8, b=v7, c=v6 reproduces it: TriangleNormal(v8,v7,v6,out).
void ModelComputeNormals(ModelRecord& rec) {
    i32 faceCount = rec.faceCount;          // v3 = *((_DWORD*)result + 19)
    i32 i18 = 0;                            // v18
    if (faceCount > 0) {
        i32 fi = 0;
        do {
            ModelFace& face = rec.faces[fi];
            ModelVertex* verts = rec.verts;             // v5 = *((_DWORD*)v1 + 16)
            float* v8 = &verts[face.v0].x;              // verts + 24*face[+24]
            float* v7 = &verts[face.v1].x;              // verts + 24*face[+28]
            float* v6 = &verts[face.v2].x;              // verts + 24*face[+32]
            float outNormal[3];
            // binary: TriangleNormal(v8, v7, /*out*/&out, v6) with a3 == output;
            // out=(a2-a1)x(a4-a1)=(v7-v8)x(v6-v8). reimpl out=(b-a)x(c-a), so
            // a=v8, b=v7, c=v6 reproduces it.
            guild::util::TriangleNormal(v8, v7, v6, outNormal);
            face.nx = outNormal[0];
            face.ny = outNormal[1];
            face.nz = outNormal[2];
            faceCount = rec.faceCount;       // v9 = *((_DWORD*)v1 + 19) reloaded
            ++i18;
            ++fi;
        } while (i18 < faceCount);
    }

    // Pass 2: per-vertex normal accumulation.
    for (i32 vi = 0; vi < rec.vertCount; ++vi) {        // for (i=0; i<v1[17]; ...)
        float acc[3] = {0.0f, 0.0f, 0.0f};  // v15/v16/v17
        i32 fi = 0;                         // v12
        i32 fcount = rec.faceCount;
        for (; fi < fcount; ++fi) {         // for (j=v1[18]; v12<v1[19]; j+=56)
            const ModelFace& face = rec.faces[fi];
            if (vi == face.v0 || vi == face.v1 || vi == face.v2) {
                acc[0] += face.nx;          // j+44
                acc[1] += face.ny;          // j+48
                acc[2] += face.nz;          // j+52
            }
            fcount = rec.faceCount;         // *((_DWORD*)v1+19) reloaded each iter
        }
        guild::util::VectorNormalize(acc);  // VIBE_Math_VectorNormalize(&v15)
        rec.verts[vi].nx = acc[0];          // v10[3]
        rec.verts[vi].ny = acc[1];          // v10[4]
        rec.verts[vi].nz = acc[2];          // v10[5]
    }
}

// ===========================================================================
//  VIBE_Model_ComputeBounds @0x5f8f98
// ===========================================================================
// gilde.exe 0x5f8f98 — VIBE_Model_ComputeBounds (__usercall, eax = record)
//   a1+64 verts (24-byte stride), a1+68 vertCount, a1+468 radius.
// Pass 1: radius = max sqrt(x^2+y^2+z^2). Pass 2: AABB min/max xyz; then write
// the 8 box-corner verts into the 8 slots after the real verts.
void ModelComputeBounds(ModelRecord& rec) {
    i32 vcount = rec.vertCount;             // v2
    float radius = 0.0f;                    // v35
    {
        i32 idx = 0;                        // v4
        if (vcount > 0) {
            do {
                const ModelVertex& v = rec.verts[idx];      // v5
                // Binary: fsqrt result stays 80-bit and `fcomp` compares it
                // directly against the float radius (var_1C) — no truncation to
                // float before the compare. Keep the sqrt as a double.
                double r = std::sqrt((double)(v.x*v.x + v.y*v.y + v.z*v.z));
                if (r > (double)radius) {
                    const ModelVertex& v10 = rec.verts[idx]; // reload
                    // Store truncates to float (var_1C is a 4-byte slot).
                    radius = (float)std::sqrt(
                        (double)(v10.x*v10.x + v10.y*v10.y + v10.z*v10.z));
                }
                ++idx;
            } while (idx < rec.vertCount);
        }
    }
    i32 v7 = rec.vertCount;
    rec.radius = radius;                    // *(float*)(a1 + 468) = v35
    if (v7 <= 0) return;

    // AABB accumulators: bb[0..5] = {minX,maxX,minY,maxY,minZ,maxZ}.
    // Original init loop: even index -> 1e10, odd index -> -1e10.
    float bb[6];                            // v27..v32
    for (int k = 0; k < 6; ++k) bb[k] = (k & 1) ? -1.0e10f : 1.0e10f;

    {
        i32 v11 = 0;
        if (rec.vertCount > 0) {
            i32 idx = 0;                    // v12
            do {
                const ModelVertex& p = rec.verts[idx];
                if ((double)p.x < (double)bb[0]) bb[0] = p.x;   // minX
                if ((double)p.x > (double)bb[1]) bb[1] = p.x;   // maxX
                if ((double)p.y < (double)bb[2]) bb[2] = p.y;   // minY
                if ((double)p.y > (double)bb[3]) bb[3] = p.y;   // maxY
                if ((double)p.z < (double)bb[4]) bb[4] = p.z;   // minZ
                if ((double)p.z > (double)bb[5]) bb[5] = p.z;   // maxZ
                ++v11;
                ++idx;
            } while (v11 < rec.vertCount);
        }
    }

    const float minX = bb[0], maxX = bb[1];
    const float minY = bb[2], maxY = bb[3];
    const float minZ = bb[4], maxZ = bb[5];

    // 8 corner verts written into slots [n .. n+7], in the binary's emit order:
    //   n+0:(minX,minY,minZ) n+1:(maxX,minY,minZ) n+2:(minX,maxY,minZ) n+3:(maxX,maxY,minZ)
    //   n+4:(minX,minY,maxZ) n+5:(maxX,minY,maxZ) n+6:(minX,maxY,maxZ) n+7:(maxX,maxY,maxZ)
    struct Corner { float x, y, z; };
    const Corner corners[8] = {
        {minX, minY, minZ}, {maxX, minY, minZ},
        {minX, maxY, minZ}, {maxX, maxY, minZ},
        {minX, minY, maxZ}, {maxX, minY, maxZ},
        {minX, maxY, maxZ}, {maxX, maxY, maxZ},
    };
    for (int c = 0; c < 8; ++c) {
        i32 n = rec.vertCount;              // reloaded before each corner in the binary
        ModelVertex& dst = rec.verts[n + c];
        dst.x = corners[c].x;
        dst.y = corners[c].y;
        dst.z = corners[c].z;
    }
}

// ===========================================================================
//  VIBE_Model_ComputeChunkSize @0x5f9430
// ===========================================================================
// gilde.exe 0x5f9430 — VIBE_Model_ComputeChunkSize (__usercall eax=rec, edx=aux).
// rec is read by word index (all i32 fields, no pointer aliasing): rec[17]=
// normVtxCnt, rec[19]=faceCnt, rec[120]=materialCount. aux+16 = material table
// base (224-byte stride; name strings @+0/+64/+128); aux+52 = subRecord count;
// aux+56 = subRecord base (88-byte stride; string @+0).
namespace {
inline i32 rd_i32(const u8* p, int off) { i32 v; std::memcpy(&v, p + off, 4); return v; }
inline u8* rd_ptr(const u8* p, int off) { u8* v; std::memcpy(&v, p + off, sizeof(u8*)); return v; }
} // namespace

guild::i32 ModelComputeChunkSize(const u8* rec, const u8* aux) {
    i32 v3 = rd_i32(rec, 17 * 4);          // rec[17]
    i32 v4 = 24;
    if (v3 > 0)
        v4 = 24 * (v3 + 8) + 24;

    i32 matCount = rd_i32(rec, 120 * 4);   // rec[120]
    i32 faceCnt  = rd_i32(rec, 19 * 4);    // rec[19]
    i32 v5;
    if (matCount <= 254)
        v5 = 49 * faceCnt;
    else
        v5 = 52 * faceCnt;
    i32 v6 = 6 * matCount + v5 + v4;

    if (matCount > 0) {
        i32 v7 = 0;
        i32 v8 = 0;                        // byte offset into material table
        const u8* matBase = rd_ptr(aux, 16);
        do {
            v6 += (i32)std::strlen((const char*)(matBase + v8 + 64)) + 1
                + (i32)std::strlen((const char*)(matBase + v8 + 128)) + 1
                + (i32)std::strlen((const char*)(matBase + v8)) + 1;
            ++v7;
            v8 += 224;
        } while (v7 < matCount);
    }

    i32 v9  = rd_i32(aux, 52);             // subRecord count
    i32 v10 = 0;
    i32 v11 = 24 * v9 + v6;
    if (v9 > 0) {
        const char* v12 = (const char*)(aux + 56);
        do {
            ++v10;
            v11 += (i32)std::strlen(v12) + 1;
            v12 += 88;
        } while (v10 < rd_i32(aux, 52));
    }
    return v11;
}

// ===========================================================================
//  Material-field Bio read thunks  (0x5e3fac .. 0x5e409c)
// ===========================================================================
// The parser context only ever has its i32 index (+8) and a single material
// table pointer (+16) read; those don't alias. The thunk computes
// 224*ctx[+8] + ctx[+16] + fieldOff and Bio-reads into it.
namespace {
ModelIoBioHooks g_bio{};
inline u8* mat_field(const u8* ctx, int fieldOff) {
    i32 idx = rd_i32(ctx, 8);
    u8* table = rd_ptr(ctx, 16);
    return table + 224 * (size_t)idx + fieldOff;
}
inline int do_byte(void* stream, const u8* ctx, int fieldOff) {
    if (!g_bio.readByte) return 0;
    return g_bio.readByte(stream, mat_field(ctx, fieldOff));
}
inline int do_dword(void* stream, const u8* ctx, int fieldOff) {
    if (!g_bio.readDword) return 0;
    return g_bio.readDword(stream, (u32*)mat_field(ctx, fieldOff));
}
} // namespace

void SetModelIoBioHooks(const ModelIoBioHooks& h) { g_bio = h; }
ModelIoBioHooks GetModelIoBioHooks() { return g_bio; }

// gilde.exe 0x5e3fac — VIBE_Bio_ReadByte(stream, &mat[+200])
int ModelIoReadFloatThunk (void* stream, const u8* ctx) { return do_byte (stream, ctx, 200); }
// gilde.exe 0x5e3fd4 — VIBE_Bio_ReadByte(stream, &mat[+201])
int ModelIoReadFloatThunk2(void* stream, const u8* ctx) { return do_byte (stream, ctx, 201); }
// gilde.exe 0x5e3ffc — VIBE_Bio_ReadDword(stream, &mat[+204])
int ModelIoReadDwordThunk (void* stream, const u8* ctx) { return do_dword(stream, ctx, 204); }
// gilde.exe 0x5e4024 — VIBE_Bio_ReadDword(stream, &mat[+208])
int ModelIoReadDwordThunk2(void* stream, const u8* ctx) { return do_dword(stream, ctx, 208); }
// gilde.exe 0x5e404c — VIBE_Bio_ReadDword(stream, &mat[+212])
int ModelIoReadDwordThunk3(void* stream, const u8* ctx) { return do_dword(stream, ctx, 212); }
// gilde.exe 0x5e4074 — VIBE_Bio_ReadDword(stream, &mat[+216])
int ModelIoReadDwordThunk4(void* stream, const u8* ctx) { return do_dword(stream, ctx, 216); }
// gilde.exe 0x5e409c — VIBE_Bio_ReadDword(stream, &mat[+220])
int ModelIoReadDwordThunk5(void* stream, const u8* ctx) { return do_dword(stream, ctx, 220); }

// ===========================================================================
//  VIBE_Resource_FindFreeSlot @0x40df94
// ===========================================================================
// gilde.exe 0x40df94 — linear scan; slot stride 740, "in use" = dword at +4,
// capacity bound 378140 (== 740 * 511). Returns slot index or -1 if full.
guild::i32 ResourceFindFreeSlot(const u8* table) {
    i32 idx = 0;     // v0
    i32 off = 0;     // v1
    while (rd_i32(table, off + 4) != 0) {
        off += 740;
        ++idx;
        if (off >= 378140)
            return -1;
    }
    return idx;
}

// ===========================================================================
//  VIBE_Resource_EvictOldestEntry @0x40decc
// ===========================================================================
// gilde.exe 0x40decc — VIBE_Resource_EvictOldestEntry (__fastcall).
// Entry stride 84; the original packs a 4-byte data ptr @+52 and i32 size @+56,
// which alias on a 64-bit host, so the cache entry is given a typed view here.
// Other touched fields: +64 lockCount, +68 byte flags (bit0=pinned), +72 ts.
namespace { ResourceFreeHook g_free{}; }
void SetResourceFreeHook(const ResourceFreeHook& h) { g_free = h; }
ResourceFreeHook GetResourceFreeHook() { return g_free; }

guild::i32 ResourceEvictOldestEntry(ResourceCache& cache) {
    i32 v1 = -1;                 // winning index
    u32 v2 = cache.floor;        // running-min timestamp (seeded from dword_62EB38)
    i32 v3 = 1;                  // current index (scan starts at entry 1)
    if (cache.count > 1) {
        do {
            const ResourceEntry& e = cache.entries[v3];
            if (v2 > e.timestamp                 // ts < running min  (+72)
                && e.lockCount <= 0              // (+64)
                && (e.flags & 1) == 0            // (+68) not pinned
                && e.data != nullptr             // (+52)
                && e.size != 0) {                // (+56)
                v1 = v3;
                v2 = e.timestamp;
            }
            ++v3;
        } while (v3 < cache.count);
    }
    if (v1 == -1)
        return 0;

    ResourceEntry& winner = cache.entries[v1];
    // VIBE_Memory_FreeDebug(winner.data, ...) — free the data pointer.
    if (g_free.freeData)
        g_free.freeData(winner.data);
    cache.usedMem -= (i32)winner.size;   // dword_62D20C -= entry size
    winner.data = nullptr;               // entry[+52] = 0
    return 1;
}

// ===========================================================================
//  VIBE_Resource_FreeEntryData @0x5d91d4
// ===========================================================================
// gilde.exe 0x5d91d4 — VIBE_Resource_FreeEntryData (__usercall eax=entry,
// edx=alsoClose). Faithful translation of the disassembly: the entry pointer is
// held in ecx (== the eax input) throughout; the Hex-Rays v5/v6 are that same
// pointer. The off_64A910/914/91C indirect calls (default null-stub /
// exit-handler-thunk) and the file/memory verbs are routed through hooks.
namespace { ResourceFreeEntryHooks g_freeEntry{}; }
void SetResourceFreeEntryHooks(const ResourceFreeEntryHooks& h) { g_freeEntry = h; }
ResourceFreeEntryHooks GetResourceFreeEntryHooks() { return g_freeEntry; }

guild::i32 ResourceFreeEntryData(ResourceEntryView& ent, bool alsoClose) {
    // if ( !*(_DWORD *)(a1 + 12) ) return -1;
    if (ent.infoWord == 0)
        return -1;

    i32 v4 = 0;                                  // esi — running status
    // if ( (*(_BYTE *)(a1 + 13) & 0x10) != 0 ) v4 = VIBE_File_FlushBuffer();
    if ((ent.flags2() & 0x10) != 0) {
        if (g_freeEntry.flushBuffer)
            v4 = g_freeEntry.flushBuffer();
    }
    // off_64A910(entry[+16])  (ds:off_64A910 — default null-stub)
    if (g_freeEntry.onMemoryRelease)
        g_freeEntry.onMemoryRelease(ent.fileHandle);
    // if ( VIBE_Memory_FreeBlock(entry) != -1 ) VIBE_File_FlushAndSeek(entry[+16], 0);
    int freeResult = -1;
    if (g_freeEntry.freeBlock)
        freeResult = g_freeEntry.freeBlock(ent.fileHandle);
    if (freeResult != -1) {
        if (g_freeEntry.flushAndSeek)
            g_freeEntry.flushAndSeek(ent.fileHandle, 0);
    }
    // if ( alsoClose ) v4 |= VIBE_File_CloseHandle(entry[+16]);
    if (alsoClose) {
        if (g_freeEntry.closeHandle)
            v4 |= g_freeEntry.closeHandle(ent.fileHandle);
    }
    // if ( (*(_BYTE *)(entry + 12) & 8) != 0 ) {
    //     VIBE_Memory_ReturnToFreeList(*(blockHdr + 8)); *(blockHdr + 8) = 0;
    // }
    if ((ent.flags1() & 8) != 0) {
        if (g_freeEntry.returnToFreeList)
            g_freeEntry.returnToFreeList(ent.spill);
        ent.spill = nullptr;                     // *(blockHdr + 8) = 0
    }
    // if ( (*(_BYTE *)(entry + 13) & 8) != 0 ) {
    //     VIBE_Vfs_BuildTempFileName(buf, blockHdr[+20]); VIBE_Vfs_CloseHandleThunk(buf);
    // }  — delete the temp file by id.
    if ((ent.flags2() & 8) != 0) {
        if (g_freeEntry.deleteTempFile)
            g_freeEntry.deleteTempFile(ent.tempId);
    }
    // off_64A914(entry[+16])  (ds:off_64A914 — default null-stub)
    if (g_freeEntry.onPostRelease)
        g_freeEntry.onPostRelease();
    // if ( alsoClose ) off_64A91C(entry[+16]);  (ds:off_64A91C)
    if (alsoClose) {
        if (g_freeEntry.onCloseExtra)
            g_freeEntry.onCloseExtra();
    }
    return v4;
}

} // namespace guild::render
