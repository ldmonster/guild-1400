// =============================================================================
// guild::render — VIBE_Animation_* / VIBE_Mesh_* loader + anim/AABB leaves.
// 1:1 reconstruction; see animation_mesh.h for the per-function provenance.
// =============================================================================
#include "render/animation_mesh.h"

#include "util/string_ops.h"   // guild::util::StrCmpNoCase (reconstructed)

#include <cstdio>   // snprintf  (VIBE_Crt_Sprintf_0)
#include <cstring>  // strlen / strchr

namespace guild::render {

// ---------------------------------------------------------------------------
// Hooks state (inert defaults defined here in the library .cpp).
// ---------------------------------------------------------------------------
namespace {
int  g_inflateCalls  = 0;
int  g_precacheCalls = 0;

int  g_frameProcessCalls = 0;

int  DefaultNormalizeDirPath(const char* /*dir*/)         { return 0; }
const char* DefaultResolvePath(const char* name)          { return name; }
char DefaultInflateGeometry(AabbNode* /*node*/)           { ++g_inflateCalls; return 0; }
void DefaultPrecacheTexture(int /*frameIndex*/)           { ++g_precacheCalls; }
void DefaultFrameProcess(void* /*frame*/)                 { ++g_frameProcessCalls; }

AnimMeshHooks g_hooks = {
    &DefaultNormalizeDirPath,
    &DefaultResolvePath,
    &DefaultInflateGeometry,
    &DefaultPrecacheTexture,
    &DefaultFrameProcess,
};

// gilde.exe inlined 2-byte strcpy (the `do { *dst=*src; if(!c)break; dst[1]=src[1];
// src+=2; dst+=2; } while(c)` idiom). Faithful to the original (copies the NUL).
char* InlinedStrCpy(char* dst, const char* src) {
    for (;;) {
        char c0 = *src;
        dst[0] = c0;
        if (!c0) return dst;
        char c1 = src[1];
        dst[1] = c1;
        src += 2;
        dst += 2;
        if (!c1) return dst - 2;
    }
}
} // namespace

void AnimMeshSetHooks(const AnimMeshHooks& hooks) {
    g_hooks = hooks;
    if (!g_hooks.normalizeDirPath) g_hooks.normalizeDirPath = &DefaultNormalizeDirPath;
    if (!g_hooks.resolvePath)      g_hooks.resolvePath      = &DefaultResolvePath;
    if (!g_hooks.inflateGeometry)  g_hooks.inflateGeometry  = &DefaultInflateGeometry;
    if (!g_hooks.precacheTexture)  g_hooks.precacheTexture  = &DefaultPrecacheTexture;
    if (!g_hooks.frameProcess)     g_hooks.frameProcess     = &DefaultFrameProcess;
}
void AnimMeshResetHooks() {
    g_hooks = {&DefaultNormalizeDirPath, &DefaultResolvePath,
               &DefaultInflateGeometry, &DefaultPrecacheTexture,
               &DefaultFrameProcess};
    g_inflateCalls = 0;
    g_precacheCalls = 0;
    g_frameProcessCalls = 0;
}
int AnimMeshInflateCalls()      { return g_inflateCalls; }
int AnimMeshPrecacheCalls()     { return g_precacheCalls; }
int AnimMeshFrameProcessCalls() { return g_frameProcessCalls; }

// ===========================================================================
// 0x5D89BC — VIBE_Animation_Advanced.
// ===========================================================================
int AnimationAdvanced(AdvancedClip& clip, unsigned frameIndex) {
    // if ( a5 > (int)*(u16*)(a3 + 42) ) return 0;  (signed compare in original)
    if ((int)frameIndex > (int)clip.frameCount)
        return -1;
    AdvancedFrame* f = clip.frames[frameIndex];
    if (!f)
        return -1;
    u8 saved = f->mode;       // v9 = *(v8 + 13)
    f->mode = 3;              // *(v8 + 13) = 3
    g_hooks.frameProcess(f);  // VIBE_FrameData_Process(...)
    int observed = f->mode;   // (the mode the processor saw, before restore)
    f->mode = saved;          // *(v8 + 13) = v10 (restore)
    return observed;
}

// ===========================================================================
// 0x5D9774 — VIBE_Animation_GetPtr.
// ===========================================================================
AnimRegistryEntry* AnimationGetPtr(const char* name, AnimRegistryEntry* table,
                                   u32 count) {
    // Inlined copy of the caller name into a 64-byte local (v9[76]); the copy
    // is only used as the comparison key. (Original copies into a stack buffer.)
    char key[76];
    InlinedStrCpy(key, name);

    if (count == 0)
        return nullptr;                  // dword_1406A80 == 0 -> return 0
    for (u32 i = 0; i < count; ++i) {
        AnimRegistryEntry* e = &table[i];
        // skip empties (frameCount <= 0) and mismatches (StrCmpNoCase != 0).
        if (e->frameCount > 0 && util::StrCmpNoCase(key, e->name) == 0)
            return e;
    }
    return nullptr;
}

// ===========================================================================
// 0x5D1020 — VIBE_Mesh_SetActiveTexturePath.  (dword_1406110 = CurTexSet ctx)
// ===========================================================================
namespace { int g_curTexSetCtx = 0; }

int MeshSetActiveTexturePath(const char* dir) {
    int result = g_hooks.normalizeDirPath(dir);
    g_curTexSetCtx = result;             // dword_1406110
    return result;
}

// ===========================================================================
// 0x5D1034 — VIBE_Mesh_BuildTexturePath.
//   tmp = "*" + a1 + a2 ; return ResolveAndBuildPath(tmp).
// ===========================================================================
const char* MeshBuildTexturePath(char* out, const char* a1, const char* a2) {
    // The "*" prefix lives at unk_628F14 ("*\0"); the original copies it first.
    char* p = out;
    p[0] = kTexPathPrefix;
    p[1] = '\0';
    // append a1 (inlined strcpy at out+strlen), then a2.
    InlinedStrCpy(out + std::strlen(out), a1);
    InlinedStrCpy(out + std::strlen(out), a2);
    return g_hooks.resolvePath(out);
}

// ===========================================================================
// 0x5D15FC — VIBE_Mesh_BuildLodFileName.
// ===========================================================================
char MeshBuildLodFileName(const char* base, const char* base2, char* out,
                          int lod, char* out2, signed char lodMode) {
    if (lod < 0) {
        // "base" request: only valid when lodMode itself is negative.
        if (lodMode >= 0)
            return 0;
        InlinedStrCpy(out, base);
        InlinedStrCpy(out + std::strlen(out), kLodBaseSuffix);   // "_s"
        if (base2 && out2) {
            InlinedStrCpy(out2, base2);
            InlinedStrCpy(out2 + std::strlen(out2), kLodBaseSuffix);
        }
        return 1;
    }

    int v6 = lodMode & 0x7F;
    if (lod == 0) {
        if (v6 == 2) {
            // Probe "%s_%i" indices 2..1 (v7-1 == 1..0) for an existing .bgf.
            char probe[272];
            for (int v7 = 2; ; ) {
                std::snprintf(out, 272, kLodFmt, base, v7 - 1);
                if (base2 && out2)
                    std::snprintf(out2, 272, kLodFmt, base2, v7 - 1);
                if (MeshBuildTexturePath(probe, out, ".bgf"))
                    return 1;            // resolved -> done
                if (--v7 < 0)
                    return 0;
                if (v7 > 0)
                    continue;            // try next lower index
                // v7 == 0: give up the numbered scheme, fall back to plain copy.
                InlinedStrCpy(out, base);
                if (base2 && out2)
                    InlinedStrCpy(out2, base2);
            }
        }
        // v6 != 2: plain copy of the base name(s).
        InlinedStrCpy(out, base);
        if (base2 && out2)
            InlinedStrCpy(out2, base2);
        return 1;
    }

    // lod > 0.
    if (v6 == 2)
        lod = 2 - lod;
    int idx = lod - 1;
    std::snprintf(out, 272, kLodFmt, base, idx);
    if (!base2 || !out2)
        return 1;
    std::snprintf(out2, 272, kLodFmt, base2, idx);
    return 1;
}

// ===========================================================================
// 0x5B4944 — VIBE_Mesh_MarkAllFramesDirty.
// ===========================================================================
void MeshMarkAllFramesDirty(FrameEntry* frames, int count) {
    if (!frames)
        return;
    for (int i = 0; i < count; ++i) {
        FrameEntry& f = frames[i];
        if (!f.texture)
            continue;                    // *(+100) == 0 -> skip
        if ((signed char)f.flags < 0)    // *(+104) >= 0 guard (bit7 already set)
            continue;
        g_hooks.precacheTexture(i);      // VIBE_Render_PrecacheTexture
        f.flags |= 0x80u;                // set dirty bit
    }
}

// ===========================================================================
// 0x429070 — VIBE_Mesh_ApplyTransformRecursive.
// ===========================================================================
char MeshApplyTransformRecursive(AabbNode* node) {
    char result = g_hooks.inflateGeometry(node);
    for (AabbNode* c = node->firstChild; c; c = c->nextSibling)
        result = MeshApplyTransformRecursive(c);
    return result;
}

// ===========================================================================
// 0x5F5628 — VIBE_Mesh_AccumulateMemoryCallback.
//   The original: if ComputeSceneMemorySize(node, kind, &tmp) then *acc += tmp.
//   The memory-sizing leaf is itself reconstructed (mesh_*.cpp); to keep this
//   library self-contained we model the accumulate step exactly: add the
//   per-node size into the running accumulator. Returns 1 (always).
// ===========================================================================
char MeshAccumulateMemoryCallback(int* accumulator, int nodeSize) {
    if (accumulator)
        *accumulator += nodeSize;
    return 1;
}

// ===========================================================================
// 0x4283AC — VIBE_Mesh_AccumulateAabbRecursive.
// ===========================================================================
void MeshAccumulateAabbRecursive(Aabb& box, AabbNode* node) {
    if (node->hasMesh) {
        for (int i = 0; i < 8; ++i) {
            const AabbCorner& v = node->corners[i];
            const float c[3] = {v.x, v.y, v.z};
            for (int a = 0; a < 3; ++a) {
                // mn = min(mn, c) — note the original tests `mn >= c ? c : mn`.
                if (box.mn[a] >= c[a]) box.mn[a] = c[a];
                if (box.mx[a] <= c[a]) box.mx[a] = c[a];
            }
        }
        for (AabbNode* ch = node->firstChild; ch; ch = ch->nextSibling)
            MeshAccumulateAabbRecursive(box, ch);
    }
}

// ===========================================================================
// 0x427820 — VIBE_Mesh_TestAabbOverlapRecursive (geometry subset).
// ===========================================================================
int MeshTestAabbOverlapRecursive(const Aabb& q, AabbNode* node, Aabb* outBox) {
    int result = 1;
    if (node->hasMesh) {
        // Compute this node's own AABB over its 8 corners. The original seeds
        // mn/mx with corner[0] then folds corners[1..7].
        Aabb b;
        b.mn[0] = b.mx[0] = node->corners[0].x;
        b.mn[1] = b.mx[1] = node->corners[0].y;
        b.mn[2] = b.mx[2] = node->corners[0].z;
        for (int i = 1; i < 8; ++i) {
            const float c[3] = {node->corners[i].x, node->corners[i].y,
                                node->corners[i].z};
            for (int a = 0; a < 3; ++a) {
                if (b.mn[a] >= c[a]) b.mn[a] = c[a];
                if (b.mx[a] <= c[a]) b.mx[a] = c[a];
            }
        }
        // The original tests this node's box against the query box q on every
        // axis (q.mx > b.mn AND q.mn < b.mx); on overlap it grows the running
        // outBox by b (per-axis min/max). On a miss the box is left untouched.
        bool overlap = q.mx[0] > b.mn[0] && q.mn[0] < b.mx[0] &&
                       q.mx[1] > b.mn[1] && q.mn[1] < b.mx[1] &&
                       q.mx[2] > b.mn[2] && q.mn[2] < b.mx[2];
        if (overlap && outBox) {
            for (int a = 0; a < 3; ++a) {
                if (outBox->mn[a] >= b.mn[a]) outBox->mn[a] = b.mn[a];
                if (outBox->mx[a] <= b.mx[a]) outBox->mx[a] = b.mx[a];
            }
        }
        result = 0;                      // a mesh node consumes the test -> 0
    }
    for (AabbNode* c = node->firstChild; c; c = c->nextSibling)
        result &= MeshTestAabbOverlapRecursive(q, c, nullptr);
    return result;
}

// ===========================================================================
// .TXS serialization (0x5D20DC writer / 0x5D2240 reader).
//   The Bio layer writes/reads big-endian dword-pairs; we model the byte stream
//   exactly: 3 dwords header (magic, rows, cols) then rows*cols 64-byte names.
// ===========================================================================
namespace {
// VIBE_Bio_WriteDwordPair / ReadDwordSwapArgs store dwords byte-swapped (BE).
void WriteBE32(u8* p, u32 v) {
    p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v);
}
u32 ReadBE32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
} // namespace

std::size_t SaveTextureSet(const MeshTextureSet& ts, u8* buf, std::size_t cap) {
    // Guard mirrors the original: rows>0 && cols>0 (else writes nothing).
    if (ts.rows <= 0 || ts.cols <= 0)
        return 0;
    std::size_t need = 12 + std::size_t(ts.rows) * ts.cols * 64;
    if (need > cap)
        return 0;
    WriteBE32(buf + 0, kTxsMagic);
    WriteBE32(buf + 4, u32(ts.rows));    // writes rows first (v1[121]) then cols
    WriteBE32(buf + 8, u32(ts.cols));    // (v1[120])
    std::size_t off = 12;
    for (int r = 0; r < ts.rows; ++r) {
        for (int c = 0; c < ts.cols; ++c) {
            std::memcpy(buf + off, ts.at(r, c), 64);
            off += 64;
        }
    }
    return off;
}

bool LoadTextureSet(const u8* buf, std::size_t len, MeshTextureSet& ts) {
    if (len < 12)
        return false;
    if (ReadBE32(buf) != kTxsMagic)
        return false;
    int rows = int(ReadBE32(buf + 4));
    int cols = int(ReadBE32(buf + 8));
    if (rows <= 0 || cols <= 0)          // original guard: i>0 && v11[0]>0
        return false;
    if (std::size_t(rows) * cols > 256)
        return false;                    // capacity bound (names[256][64])
    if (len < 12 + std::size_t(rows) * cols * 64)
        return false;
    ts.rows = rows;
    ts.cols = cols;
    std::size_t off = 12;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            std::memcpy(ts.at(r, c), buf + off, 64);
            off += 64;
        }
    }
    return true;
}

} // namespace guild::render
