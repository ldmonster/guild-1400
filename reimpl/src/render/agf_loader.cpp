#include "render/agf_loader.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "util/math.h"   // TriangleNormal / VectorNormalize (0x5cb824 / 0x5cb148)

// gilde.exe — .BGF "AGF" token-script reader (the shipped Objects.BIN format).
//
// 1:1 reconstruction of VIBE_ModelIo_ReadChunkTag @0x5E44B4 + VIBE_Script_*
// (ReadToken @0x5E3BD0, ParseBlock @0x5E3C44, FindTokenHandler @0x5E3C0C) and
// every leaf handler, parsing straight out of a flat decompressed buffer into a
// render::BgfModel. The on-disk values are little-endian; the production code
// streamed them one field at a time through VIBE_Bio_Read* (each a thin
// VIBE_Vfs_ReadStream over the file handle). We mirror that with a bounded LE
// cursor; every reader signals a short read so a truncated record aborts.
namespace guild::render {

namespace {

// A bounded little-endian read cursor (models VIBE_Vfs_ReadStream over memory).
struct Cursor {
    const u8* p;
    const u8* end;

    bool ReadBytes(void* dst, size_t n) {
        if (static_cast<size_t>(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    }
    // VIBE_Bio_ReadByte @0x5DC850.
    bool ReadByte(u8* v) { return ReadBytes(v, 1); }
    // VIBE_Bio_ReadDword / VIBE_Bio_ReadDwordSwapArgs @0x5DC894/0x5DC8B0 (LE, no swap).
    bool ReadU32(u32* v) {
        u8 b[4];
        if (!ReadBytes(b, 4)) return false;
        *v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
        return true;
    }
    bool ReadI32(i32* v) { return ReadU32(reinterpret_cast<u32*>(v)); }
    bool ReadF32(float* v) {
        u32 bits;
        if (!ReadU32(&bits)) return false;
        std::memcpy(v, &bits, 4);
        return true;
    }
    bool ReadVec3(float out[3]) {
        return ReadF32(&out[0]) && ReadF32(&out[1]) && ReadF32(&out[2]);
    }
    // VIBE_Bio_ReadString @0x5DC86C — bytes up to (and including) a NUL.
    bool ReadString(std::string& out) {
        out.clear();
        for (;;) {
            u8 c;
            if (!ReadByte(&c)) return false;
            if (!c) return true;
            out.push_back(static_cast<char>(c));
        }
    }
    // Skip n bytes; false on truncation.
    bool Skip(size_t n) {
        if (static_cast<size_t>(end - p) < n) return false;
        p += n;
        return true;
    }
};

// ---------------------------------------------------------------------------
// The parse struct — the in-memory model the handlers fill, mirroring the engine
// v145 struct (dword field indices map to the byte offsets in the header docs).
// We back the geometry with growing BgfModel arrays plus the running write/base
// indices the handlers maintain (the engine kept these as struct dwords +24/+40
// /+44 etc.). The 'ok' flag latches a truncated read so the whole parse fails.
// ---------------------------------------------------------------------------
struct ParseState {
    BgfModel* m = nullptr;
    bool ok = true;

    // material block (sub-table 0x64A360): counter = +8, count = +12.
    u32 matCounter = 0;   // current material index (+8); bumped by the '(' handler
    u32 matCount   = 0;   // declared material count  (+12)

    // poly block: per-block count = +20, write idx = +24, total = +28.
    u32 polyBlock = 0;    // +20 declared count of the current block
    u32 polyWrite = 0;    // +24 next poly slot to fill
    u32 polyTotal = 0;    // +28 accumulated total
    bool polyAllocated = false;

    // vertex block: per-block count = +36, write idx = +40, base = +44.
    u32 vtxBlock = 0;     // +36 declared count of the current block
    u32 vtxWrite = 0;     // +40 next vertex slot to fill
    u32 vtxBase  = 0;     // +44 running base added to each on-disk poly index
    bool vtxAllocated = false;

    BgfPolygon& poly(u32 i) {
        if (m->polygons.size() <= i) m->polygons.resize(i + 1, BgfPolygon{});
        return m->polygons[i];
    }
    BgfVertex& vert(u32 i) {
        if (m->vertices.size() <= i) m->vertices.resize(i + 1, BgfVertex{});
        return m->vertices[i];
    }
    BgfMaterial& mat(u32 i) {
        if (m->materials.size() <= i) m->materials.resize(i + 1, BgfMaterial{});
        return m->materials[i];
    }
};

// VIBE_Util_StrChr(name,'.') then *p=0 — strip a trailing ".ext".
void StripExt(std::string& s) {
    auto dot = s.find('.');
    if (dot != std::string::npos) s.erase(dot);
}

// =====================  LEAF HANDLERS  =====================
// Each consumes its binary payload from the cursor and writes into ParseState.

// 0x5E43F4 VIBE_ModelIo_ReadByteFromStream — reads 4 bytes (discarded slot).
void H_ReadByte4(Cursor& c, ParseState& s) { if (!c.Skip(4)) s.ok = false; }
// 0x5E4414 VIBE_ModelIo_ReadZeroedDword — ReadDword into a discarded local.
void H_ReadDword(Cursor& c, ParseState& s) { u32 v; if (!c.ReadU32(&v)) s.ok = false; }
// 0x5E43CC VIBE_ModelIo_ReadFlagBits — 1 byte split into two flag bits stored at
// parse-ctx+0 / ctx+1 (`*ctx = b & 1; ctx[1] = (b >> 1) & 1`). ctx+1 gates the
// vertex-dedup stage in LoadBgfFile @0x5d2348 (`if (!v145[1]) ...dedup...`).
void H_ReadFlagByte(Cursor& c, ParseState& s) {
    u8 b;
    if (!c.ReadByte(&b)) { s.ok = false; return; }
    s.m->parseFlag0 = b & 1;
    s.m->parseFlag1 = (b >> 1) & 1;
}
// 0x5E43A8 VIBE_ModelIo_ReadNameString — ReadString into a discarded local.
void H_ReadNameStr(Cursor& c, ParseState& s) { std::string t; if (!c.ReadString(t)) s.ok = false; }
// 0x5E43BC VIBE_ModelIo_ReadDwordField — ReadDword (discarded).
void H_ReadDwordF(Cursor& c, ParseState& s) { u32 v; if (!c.ReadU32(&v)) s.ok = false; }

// 0x5E4428 VIBE_ModelIo_ReadFrameNodes — dummyCount (+52) then 88B records:
// byte, ReadString(name@+0), byte, ReadVec3(pos@+64), byte, ReadVec3(rot@+76).
void H_ReadFrameNodes(Cursor& c, ParseState& s) {
    u32 n;
    if (!c.ReadU32(&n)) { s.ok = false; return; }
    s.m->dummyCount = n;
    for (u32 i = 0; i < n; ++i) {
        u8 b;
        std::string name;
        if (s.m->dummies.size() <= i) s.m->dummies.resize(i + 1, BgfDummy{});
        BgfDummy& d = s.m->dummies[i];
        if (!c.ReadByte(&b) || !c.ReadString(name) || !c.ReadByte(&b) ||
            !c.ReadVec3(d.pos) || !c.ReadByte(&b) || !c.ReadVec3(d.rot)) {
            s.ok = false; return;
        }
        std::memset(d.name, 0, sizeof(d.name));
        std::memcpy(d.name, name.data(),
                    name.size() < sizeof(d.name) - 1 ? name.size() : sizeof(d.name) - 1);
    }
}

// 0x5E40C4 VIBE_ModelIo_AllocMaterialBuffer — materialCount@+12, reset counter@+8.
void H_AllocMaterials(Cursor& c, ParseState& s) {
    if (!c.ReadU32(&s.matCount)) { s.ok = false; return; }
    s.matCounter = 0;
    s.m->materialCount = s.matCount;
    if (s.matCount > 0) s.m->materials.resize(s.matCount, BgfMaterial{});
}

// 0x5E42D8 VIBE_ModelIo_AllocPointBuffer — vtxBlock@+36; on FIRST alloc reset
// write idx (+40) and base (+44) to 0.
void H_AllocPoints(Cursor& c, ParseState& s) {
    if (!c.ReadU32(&s.vtxBlock)) { s.ok = false; return; }
    if (s.vtxBlock > 0 && !s.vtxAllocated) {
        s.vtxWrite = 0;
        s.vtxBase = 0;
        s.vtxAllocated = true;
    }
}

// 0x5E4310 VIBE_ModelIo_AllocPolyBuffer — polyBlock@+20; on FIRST alloc reset
// write idx (+24) and total (+28) to 0.
void H_AllocPolys(Cursor& c, ParseState& s) {
    if (!c.ReadU32(&s.polyBlock)) { s.ok = false; return; }
    if (s.polyBlock > 0 && !s.polyAllocated) {
        s.polyWrite = 0;
        s.polyTotal = 0;
        s.polyAllocated = true;
    }
}

// 0x5E4348 VIBE_ModelIo_ReadPolyVertices — read vtxBlock vertices (3 floats each)
// into vert[vtxWrite].pos, advancing vtxWrite.
void H_ReadPoints(Cursor& c, ParseState& s) {
    for (u32 i = 0; i < s.vtxBlock; ++i) {
        BgfVertex& v = s.vert(s.vtxWrite);
        if (!c.ReadVec3(v.pos)) { s.ok = false; return; }
        ++s.vtxWrite;
    }
}

// 0x5E40FC VIBE_ModelIo_ReadVertexTriple — poly[polyWrite].vtx[0..2] = on-disk
// index + vtxBase (+44).
void H_ReadPolyIdx(Cursor& c, ParseState& s) {
    BgfPolygon& p = s.poly(s.polyWrite);
    for (int k = 0; k < 3; ++k) {
        u32 idx;
        if (!c.ReadU32(&idx)) { s.ok = false; return; }
        p.vtx[k] = idx + s.vtxBase;
    }
}

// 0x5E4180 VIBE_ModelIo_ReadMatrixRows — 6 dwords (uv rows) into poly +0/+8/+16
// then +4/+12/+20, then 3 dwords discarded.
void H_ReadUvRows(Cursor& c, ParseState& s) {
    BgfPolygon& p = s.poly(s.polyWrite);
    // poly +0/+8/+16 = uv0[0..2]; +4/+12/+20 = uv1[0..2] (interleaved, as the
    // BgfPolygon struct lays them: uv0[3] then uv1[3]).
    if (!c.ReadF32(&p.uv0[0]) || !c.ReadF32(&p.uv0[1]) || !c.ReadF32(&p.uv0[2]) ||
        !c.ReadF32(&p.uv1[0]) || !c.ReadF32(&p.uv1[1]) || !c.ReadF32(&p.uv1[2])) {
        s.ok = false; return;
    }
    u32 discard;
    for (int k = 0; k < 3; ++k)
        if (!c.ReadU32(&discard)) { s.ok = false; return; }
}

// 0x5E41E0 VIBE_ModelIo_ReadFloatTriple — 3 dwords into a discarded local
// (the uv2 row the fast-chunk would store; AGF discards it).
void H_ReadFloatTriple(Cursor& c, ParseState& s) {
    u32 v;
    for (int k = 0; k < 3; ++k)
        if (!c.ReadU32(&v)) { s.ok = false; return; }
}

// 0x5E4200 VIBE_ModelIo_ReadMaterialIndex — poly.texId=-1; matIndex from a byte
// (0xFF -> -1) unless matCount>254 (then a full dword); then polyWrite++.
void H_ReadMatIndex(Cursor& c, ParseState& s) {
    BgfPolygon& p = s.poly(s.polyWrite);
    p.texId = -1;
    if (s.matCount > 254) {
        if (!c.ReadI32(&p.matIndex)) { s.ok = false; return; }
    } else {
        u8 b;
        if (!c.ReadByte(&b)) { s.ok = false; return; }
        p.matIndex = (b == 0xFF) ? -1 : static_cast<i32>(b);
    }
    ++s.polyWrite;
}

// 0x5E42CC VIBE_ModelIo_ReadCountThunk — ReadDword into +4 (a count slot the
// engine kept; not needed by the geometry, read to stay in sync).
void H_ReadCountThunk(Cursor& c, ParseState& s) { u32 v; if (!c.ReadU32(&v)) s.ok = false; }

// ---- material-field handlers (sub-table 0x64A360) ----
// 0x5E3D2C VIBE_Script_ReadSkipValue — ReadDword (discarded).
void M_Skip(Cursor& c, ParseState& s) { u32 v; if (!c.ReadU32(&v)) s.ok = false; }
// 0x5E3D40 name @+0 / 0x5E3DA0 mesh @+64 / 0x5E3E00 texture @+128 (ReadString + StripExt).
void M_Name(Cursor& c, ParseState& s) { std::string t; if (!c.ReadString(t)) { s.ok = false; return; } StripExt(t); s.mat(s.matCounter).name0 = t; }
void M_Mesh(Cursor& c, ParseState& s) { std::string t; if (!c.ReadString(t)) { s.ok = false; return; } StripExt(t); s.mat(s.matCounter).name1 = t; }
void M_Tex (Cursor& c, ParseState& s) { std::string t; if (!c.ReadString(t)) { s.ok = false; return; } StripExt(t); s.mat(s.matCounter).name2 = t; }
// 0x5E3E64 byte@+192 / 0x5E3E8C byte@+193.
void M_ByteA(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).flag = b; }
void M_ByteB(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).b1 = b; }
// 0x5E3EB4 flag-bits @+194..198 / 0x5E3F54 flag-bits @+196/+199 (one byte each).
void M_FlagBits1(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).b2 = b; }
void M_FlagBits2(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).b3 = b; }
// 0x5E3FAC byte@+200 / 0x5E3FD4 byte@+201.
void M_Byte200(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).b4 = b; }
void M_Byte201(Cursor& c, ParseState& s) { u8 b; if (!c.ReadByte(&b)) { s.ok = false; return; } s.mat(s.matCounter).b5 = b; }
// 0x5E3FFC..0x5E40C2 dword fields @+204/+208/+212/+216/+220 (discarded).
void M_Dword(Cursor& c, ParseState& s) { u32 v; if (!c.ReadU32(&v)) s.ok = false; }

// =====================  OPEN '(' HANDLERS  =====================
// 0x5E42A0 VIBE_ModelIo_ResetBufferCounters — accumulate the per-block counts:
//   polyTotal(+28) += polyBlock(+20); polyBlock = 0;
//   vtxBase (+44) += vtxBlock (+36); vtxBlock = 0;
void Open_ResetCounters(Cursor&, ParseState& s) {
    s.polyTotal += s.polyBlock;
    s.polyBlock = 0;
    s.vtxBase += s.vtxBlock;
    s.vtxBlock = 0;
}
// 0x5E3D28 VIBE_Script_IncrementCounter — ++materialCounter(+8): advance to next material.
void Open_NextMaterial(Cursor&, ParseState& s) { ++s.matCounter; }

// =====================  DISPATCH TABLES  =====================
// Entries up to the first '(' are the matchable set (FindTokenHandler stops at
// '('); the '(' entry carries the open handler. Kinds: Leaf / Sub / Recur.
using Leaf = void (*)(Cursor&, ParseState&);
using Open = void (*)(Cursor&, ParseState&);

enum class EntKind { Leaf, Sub, Recur };
struct Entry {
    u8        tok;
    EntKind   kind;
    Leaf      leaf;   // when kind==Leaf
    int       sub;    // when kind==Sub: index into kTables
};

enum TableId { T_TOP, T_T3, T_T5MAT, T_T14, T_T17, T_T1C, T_COUNT };

struct Table {
    const Entry* entries;
    int          count;
    Open         open;   // the '(' open handler (or nullptr)
};

// byte_64A4F8 (TOP)
const Entry kTop[] = {
    {0x02, EntKind::Leaf, &H_ReadFlagByte, 0},
    {0x01, EntKind::Leaf, &H_ReadDword, 0},
    {0x00, EntKind::Leaf, &H_ReadByte4, 0},
    {0x03, EntKind::Sub,  nullptr, T_T3},
    {0x14, EntKind::Sub,  nullptr, T_T14},
    {0x37, EntKind::Leaf, &H_ReadFrameNodes, 0},
};
// 0x64A420 (tok3 block) — only tok4/tok5 are matchable (first '(' at index 2).
const Entry kT3[] = {
    {0x04, EntKind::Leaf, &H_AllocMaterials, 0},
    {0x05, EntKind::Sub,  nullptr, T_T5MAT},
};
// 0x64A360 (material block)
const Entry kT5mat[] = {
    {0x06, EntKind::Leaf, &M_Skip, 0},
    {0x07, EntKind::Leaf, &M_Name, 0},
    {0x09, EntKind::Leaf, &M_Mesh, 0},
    {0x08, EntKind::Leaf, &M_Tex, 0},
    {0x0a, EntKind::Leaf, &M_ByteA, 0},
    {0x26, EntKind::Leaf, &M_ByteB, 0},
    {0x0b, EntKind::Leaf, &M_FlagBits1, 0},
    {0x0c, EntKind::Leaf, &M_FlagBits2, 0},
    {0x0d, EntKind::Leaf, &M_Byte200, 0},
    {0x0e, EntKind::Leaf, &M_Byte201, 0},
    {0x0f, EntKind::Leaf, &M_Dword, 0},
    {0x10, EntKind::Leaf, &M_Dword, 0},
    {0x11, EntKind::Leaf, &M_Dword, 0},
    {0x12, EntKind::Leaf, &M_Dword, 0},
    {0x13, EntKind::Leaf, &M_Dword, 0},
};
// 0x64A4C8 (tok0x14 block): tok15->name, tok16->dword, tok17->sub T17.
const Entry kT14[] = {
    {0x15, EntKind::Leaf, &H_ReadNameStr, 0},
    {0x16, EntKind::Leaf, &H_ReadDwordF, 0},
    {0x17, EntKind::Sub,  nullptr, T_T17},
};
// 0x64A480 (tok0x17 block): tok18..1c then '(' (ResetCounters).
const Entry kT17[] = {
    {0x18, EntKind::Leaf, &H_ReadCountThunk, 0},
    {0x19, EntKind::Leaf, &H_AllocPoints, 0},
    {0x1a, EntKind::Leaf, &H_AllocPolys, 0},
    {0x1b, EntKind::Leaf, &H_ReadPoints, 0},
    {0x1c, EntKind::Sub,  nullptr, T_T1C},
};
// 0x64A444 (tok0x1c block): tok1d..20 then '('.
const Entry kT1c[] = {
    {0x1d, EntKind::Leaf, &H_ReadPolyIdx, 0},
    {0x1e, EntKind::Leaf, &H_ReadUvRows, 0},
    {0x1f, EntKind::Leaf, &H_ReadFloatTriple, 0},
    {0x20, EntKind::Leaf, &H_ReadMatIndex, 0},
};

const Table kTables[T_COUNT] = {
    {kTop,   (int)(sizeof(kTop) / sizeof(Entry)),   nullptr},               // TOP   '(' ptr=0
    {kT3,    (int)(sizeof(kT3) / sizeof(Entry)),    nullptr},               // T3    '(' ptr=0
    {kT5mat, (int)(sizeof(kT5mat) / sizeof(Entry)), &Open_NextMaterial},    // T5mat '(' 0x5E3D28
    {kT14,   (int)(sizeof(kT14) / sizeof(Entry)),   nullptr},               // T14   '(' ptr=0
    {kT17,   (int)(sizeof(kT17) / sizeof(Entry)),   &Open_ResetCounters},   // T17   '(' 0x5E42A0
    {kT1c,   (int)(sizeof(kT1c) / sizeof(Entry)),   nullptr},               // T1c   '(' ptr=0
};

// VIBE_Script_FindTokenHandler @0x5E3C0C — linear scan; returns the entry whose
// token matches, or nullptr (the engine's '\'' "not found").
const Entry* FindHandler(const Table& t, u8 tok) {
    for (int i = 0; i < t.count; ++i)
        if (t.entries[i].tok == tok) return &t.entries[i];
    return nullptr;
}

// VIBE_Script_ParseBlock @0x5E3C44 — recursive, token-driven.
void ParseBlock(Cursor& c, int tableId, ParseState& s, int depth) {
    if (!s.ok || depth > 64) return;   // guard runaway recursion
    const Table& t = kTables[tableId];

    u8 tok = AgfReadToken(&c.p, c.end);
    if (tok == kAgfTokenBlockEnd) return;   // '\''

    while (tok != kAgfTokenBlockEnd2 && tok != kAgfTokenEnd) {  // '/' and '+'
        if (tok == kAgfTokenOpen) {            // '('
            if (t.open) t.open(c, s);
            break;
        }
        const Entry* e = FindHandler(t, tok);
        if (e) {
            switch (e->kind) {
                case EntKind::Leaf:  e->leaf(c, s); break;
                case EntKind::Sub:   ParseBlock(c, e->sub, s, depth + 1); break;
                case EntKind::Recur: ParseBlock(c, tableId, s, depth + 1); break;
            }
        }
        if (!s.ok) return;
        tok = AgfReadToken(&c.p, c.end);
        if (tok == kAgfTokenBlockEnd) return;  // '\''
    }
}

} // namespace

// gilde.exe 0x5E3BD0 — VIBE_Script_ReadToken.
u8 AgfReadToken(const u8** p, const u8* end) {
    if (*p >= end) return kAgfTokenEnd;       // EOF -> '+'
    u8 b = **p;
    ++*p;
    if (b > kAgfTokenNameThresh) return kAgfTokenBlockEnd;  // > 0x3A -> '\''
    return b;
}

// gilde.exe 0x5E44B4 / 0x5E3C44 — LoadAgfModel.
bool LoadAgfModel(const u8* data, size_t size, BgfModel& out) {
    out = BgfModel{};
    if (!data || size < 5) return false;
    if (!(data[0] == 'B' && data[1] == 'G' && data[2] == 'F' && data[3] == 0))
        return false;

    Cursor c{data + 4, data + size};
    // VIBE_ModelIo_ReadChunkTag: token must be '.' (0x2E), then a version dword.
    u8 tok = AgfReadToken(&c.p, c.end);
    if (tok != kAgfTokenVersion) return false;
    u32 version;
    if (!c.ReadU32(&version)) return false;

    ParseState s;
    s.m = &out;
    ParseBlock(c, T_TOP, s, 0);
    if (!s.ok) return false;

    // Finalize the model counts from the accumulated state. polyTotal/vtxBase are
    // the running accumulators; the open handler folds the final block's counts in
    // only when a '(' is seen, so use the larger of the accumulator and the write
    // cursor (the last block may end on '/' without a closing '(').
    out.vertexCount = (s.vtxBase > s.vtxWrite) ? s.vtxBase : s.vtxWrite;
    out.polyCount   = (s.polyTotal > s.polyWrite) ? s.polyTotal : s.polyWrite;
    out.materialCount = s.matCount;
    // Trim the arrays to the logical counts (handlers may have over-grown by one).
    if (out.vertices.size() > out.vertexCount) out.vertices.resize(out.vertexCount);
    if (out.polygons.size() > out.polyCount)   out.polygons.resize(out.polyCount);
    return true;
}

// gilde.exe 0x5D1B54 — VIBE_Mesh_ComputeBoundingExtents.
// Faithful transcription of the binary's observable outputs over the BgfModel
// view (the slack-slot corner writes over the raw engine Mesh record live in
// render/mesh_postprocess.cpp — same address, same math, verified together):
//   pass 1: radius2 (+468) = max over vertices of sqrt(x²+y²+z²) (double sqrt
//           compared against the float accumulator; float store on update);
//           radius (+472) initially the same value; centroid zeroed.
//   pass 2 (count > 0): AABB accumulators seeded ±1e10 with the binary's mixed
//           `<` / `>=` / `<=` compare directions; radius (+472) OVERWRITTEN with
//           the AABB diagonal; centroid = sum of the 8 corners * 0.125
//           (flt_628FC0), i.e. the AABB midpoints.
BgfBounds ComputeBoundingExtents(const BgfModel& m) {
    BgfBounds b;
    const u32 n = m.vertexCount ? m.vertexCount : (u32)m.vertices.size();
    const u32 lim = (n < m.vertices.size()) ? n : (u32)m.vertices.size();

    // ---- pass 1: max-|vertex| radius (i seeded 0.0) ----
    float maxR = 0.0f;
    for (u32 i = 0; i < lim; ++i) {
        const float* v = m.vertices[i].pos;
        double r = std::sqrt((double)v[0] * v[0] + (double)v[1] * v[1]
                             + (double)v[2] * v[2]);
        if (r > (double)maxR) maxR = (float)r;
    }
    b.radius2 = maxR;    // +468 = +472 (copy)
    b.radius  = maxR;    // +472 before the diagonal overwrite
    b.centroid[0] = b.centroid[1] = b.centroid[2] = 0.0f;   // +104/108/112

    if (lim == 0) return b;

    // ---- pass 2: AABB (accumulators seeded ±1e10; binary compare directions) --
    float minX = 1.0e10f, minY = 1.0e10f, minZ = 1.0e10f;    // v38/v40/v42
    float maxX = -1.0e10f, maxY = -1.0e10f, maxZ = -1.0e10f; // v31/v33/v35
    for (u32 i = 0; i < lim; ++i) {
        const float* v = m.vertices[i].pos;
        minX = (minX < (double)v[0]) ? minX : v[0];
        minY = (minY >= (double)v[1]) ? v[1] : minY;
        minZ = (minZ >= (double)v[2]) ? v[2] : minZ;
        maxX = (maxX <= (double)v[0]) ? v[0] : maxX;
        maxY = (maxY <= (double)v[1]) ? v[1] : maxY;
        maxZ = (maxZ <= (double)v[2]) ? v[2] : maxZ;
    }
    b.min[0] = minX; b.min[1] = minY; b.min[2] = minZ;
    b.max[0] = maxX; b.max[1] = maxY; b.max[2] = maxZ;

    // ---- diagonal overwrites +472 (float subs stored, double sqrt, fstp) ----
    float dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;  // v32/v34/v36
    b.radius = (float)std::sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);

    // ---- centroid = (corner sum) * 0.125 ----
    // The binary sums the 8 written corner vertices IN SLOT ORDER on the FPU
    // stack (80-bit, no intermediate stores) and multiplies by flt_628FC0
    // (0.125) with one float store per axis — modeled with double accumulation
    // in the same order. Corner slot k has x = (k&1 ? max : min), y = (k&2 ?
    // max : min), z = (k&4 ? max : min).
    double sx = 0.0, sy = 0.0, sz = 0.0;
    for (int k = 0; k < 8; ++k) {
        sx += (k & 1) ? (double)maxX : (double)minX;
        sy += (k & 2) ? (double)maxY : (double)minY;
        sz += (k & 4) ? (double)maxZ : (double)minZ;
    }
    b.centroid[0] = (float)(sx * 0.125);
    b.centroid[1] = (float)(sy * 0.125);
    b.centroid[2] = (float)(sz * 0.125);
    return b;
}

// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals.
//   Pass 1: per polygon, VIBE_Math_TriangleNormal @0x5cb824 computes the
//   NORMALIZED face normal into the poly record (+44/+48/+52).
//   Pass 2: per vertex, sum the (already normalized) face normals of every
//   referencing polygon (float adds in poly order), then VIBE_Math_VectorNormalize
//   @0x5cb148 (bit-test zero guard + reciprocal multiply) into the vertex normal.
// (The engine-Mesh-record form of the same address lives in
// render/mesh_postprocess.cpp; this is the BgfModel view of the same math.)
void ComputeVertexNormals(BgfModel& m) {
    const u32 nv = m.vertexCount ? m.vertexCount : (u32)m.vertices.size();
    if (nv == 0 || m.vertices.empty()) return;
    const u32 lim = (nv < m.vertices.size()) ? nv : (u32)m.vertices.size();

    // ----- pass 1: per-poly NORMALIZED face normals (poly +44/+48/+52) -----
    std::vector<std::array<float, 3>> face(m.polygons.size(), {0.0f, 0.0f, 0.0f});
    for (size_t n = 0; n < m.polygons.size(); ++n) {
        const BgfPolygon& p = m.polygons[n];
        if (p.vtx[0] >= m.vertices.size() || p.vtx[1] >= m.vertices.size() ||
            p.vtx[2] >= m.vertices.size())
            continue;   // memory-safety only; parsed models are in range
        float a[3] = {m.vertices[p.vtx[0]].pos[0], m.vertices[p.vtx[0]].pos[1],
                      m.vertices[p.vtx[0]].pos[2]};
        float b[3] = {m.vertices[p.vtx[1]].pos[0], m.vertices[p.vtx[1]].pos[1],
                      m.vertices[p.vtx[1]].pos[2]};
        float c[3] = {m.vertices[p.vtx[2]].pos[0], m.vertices[p.vtx[2]].pos[1],
                      m.vertices[p.vtx[2]].pos[2]};
        guild::util::TriangleNormal(a, b, c, face[n].data());
    }

    // ----- pass 2: per-vertex gather + VectorNormalize ----------------------
    for (u32 i = 0; i < lim; ++i) {
        float acc[3] = {0.0f, 0.0f, 0.0f};   // v15/v16/v17
        for (size_t n = 0; n < m.polygons.size(); ++n) {
            const BgfPolygon& p = m.polygons[n];
            if (i == p.vtx[0] || i == p.vtx[1] || i == p.vtx[2]) {
                acc[0] += face[n][0];
                acc[1] += face[n][1];
                acc[2] += face[n][2];
            }
        }
        guild::util::VectorNormalize(acc);
        m.vertices[i].normal[0] = acc[0];
        m.vertices[i].normal[1] = acc[1];
        m.vertices[i].normal[2] = acc[2];
    }
}

} // namespace guild::render
