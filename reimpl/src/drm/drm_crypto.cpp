#include "drm/drm_crypto.h"
#include "util/util_misc.h"   // guild::util::RotateByte / AlignTo8 (already 1:1)
#include <cstring>            // std::memcmp / std::memcpy / std::memset
#include <cstdint>            // intptr_t

// =============================================================================
// guild::drm::cipher — 1:1 reconstruction of the DRM cipher / key-table cores.
// See drm_crypto.h for scope, provenance and the inert-hooks rationale. Every
// constant/table below was recovered byte-exact with get_bytes from gilde.exe;
// every transform is translated instruction-faithfully from the Hex-Rays
// decompile cross-checked against disasm (the encrypted-overlay ground truth).
// =============================================================================

namespace guild::drm::cipher {

// -----------------------------------------------------------------------------
// Constant tables (byte-exact — verified with get_bytes).
// -----------------------------------------------------------------------------

// gilde.exe 0x145A790
const u8 kKeyStream9[9]    = { 0xac, 0x35, 0xc3, 0x9d, 0x54, 0x10, 0xc6, 0x7d, 0xfa };
// gilde.exe 0x142DD60 (9 live bytes; upper 7 of the 16 are zero)
const u8 kSigSeed9[9]      = { 0x7e, 0xa6, 0xa8, 0x87, 0x8d, 0x46, 0xf1, 0x69, 0xcc };
// gilde.exe 0x142DD50 (9 live bytes)
const u8 kSectorTab9[9]    = { 0x79, 0x17, 0xc2, 0x60, 0xf2, 0x4f, 0xa0, 0xe2, 0x2d };
// gilde.exe 0x145A77B[9]
const u8 kRotTab9[9]       = { 0x00, 0x04, 0x05, 0x03, 0x1a, 0x02, 0x08, 0x00, 0x04 };
// gilde.exe 0x145CAF0 (9-byte key-table seed state)
const u8 kKeyTableSeed9[9] = { 0x07, 0xa8, 0x2c, 0x8b, 0x0c, 0x24, 0xc9, 0xee, 0xbb };
// gilde.exe 0x142DD28
const char kTitleId[] = "UKD_548520-001.001";   // 18 chars + NUL

// -----------------------------------------------------------------------------
// Inert-default hook table.
// -----------------------------------------------------------------------------
Hooks& GetHooks() {
    static Hooks g;
    return g;
}

// gilde.exe 0x140b080 — VIBE_Vfs_ReadFile_Thunk (-> dword_145A540). Inert default:
// leave the buffer as-is (the caller pre-loads it with the "file contents"); a
// real backend installs Read to populate it from the protected file. We do NOT
// zero-fill by default, so in-memory cipher vectors survive the read prologue.
static void HookRead(int handle, u32 offset, u8* dst, u32 len) {
    if (GetHooks().Read) GetHooks().Read(handle, offset, dst, len);
}
// gilde.exe 0x145B904 — overlay write-back thunk. Inert default: no-op.
static void HookWrite(int handle, u32 offset, const u8* src, u32 len) {
    if (GetHooks().Write) GetHooks().Write(handle, offset, src, len);
}

// =============================================================================
// 0x140ea90 — VIBE_Drm_DescrambleBlock  (__stdcall BOOL(_DWORD*), retn 4)
// =============================================================================
int DescrambleBlock(u32* block, u8* sig9) {
    u8* p = reinterpret_cast<u8*>(block);   // a1 viewed byte-wise (little-endian)

    // for (i = 3; i > 0; --i) v10 += p[i];   then  p[i] ^= v10   (i == 0 after loop)
    u8 v10 = 0;
    int i = 3;
    for (; i > 0; --i)
        v10 = static_cast<u8>(v10 + p[i]);
    p[i] = static_cast<u8>(p[i] ^ v10);     // i == 0

    // v7 = *a1 & 0xF;  for (j = 3; j > 0; --j) p[j] ^= byte_145A790[(j + v7) % 9];
    int v7 = static_cast<int>(*block & 0xF);
    for (int j = 3; j > 0; --j)
        p[j] = static_cast<u8>(p[j] ^ kKeyStream9[(j + v7) % 9]);

    // v9 = ((*a1 >> 20) & 0xF) | (HIBYTE(*a1) & 0xF0)
    int v9 = static_cast<int>(((*block >> 20) & 0xF) | ((*block >> 24) & 0xF0));
    // v3 = ((*a1 >> 4) & 0xF) | ((*a1 >> 8) & 0xF0)
    int v3 = static_cast<int>(((*block >> 4) & 0xF) | ((*block >> 8) & 0xF0));

    int v8 = 0;
    for (int k = 0; k < 4; ++k) {
        v9 -= static_cast<int>((*block >> (8 * k)) & 0xF);
        if (k >= 3) {
            v3 = static_cast<u8>(v3);           // (unsigned __int8)v3
            if (static_cast<u8>(v3)) {
                if (GetHooks().DebugBreak)      // original: __debugbreak() / int 3
                    GetHooks().DebugBreak();
            }
        } else {
            v3 -= static_cast<int>(((*block >> (8 * k + 4)) & 0xF0)
                                   | ((*block >> (8 * k)) & 0xF));
        }
        v8 |= static_cast<int>(((*block >> (8 * k)) & 0xF) << (4 * k));
    }
    *block = static_cast<u32>(v8);

    if (v9) {
        // sig9[(v9 + v3) % 9u] ^= (u8)v8     ((v9+v3) divided as unsigned — see disasm)
        u32 idx = static_cast<u32>(v9 + v3) % 9u;
        sig9[idx] = static_cast<u8>(sig9[idx] ^ static_cast<u8>(v8));
        return 0;
    }
    return v3 ? 0 : 1;                          // return !v3
}

// Process-wide signature accumulator mirroring the original single global.
static u8 g_sig9[9] = { 0x7e, 0xa6, 0xa8, 0x87, 0x8d, 0x46, 0xf1, 0x69, 0xcc };

u8* ResetSignatureAccumulator() {
    for (int n = 0; n < 9; ++n) g_sig9[n] = kSigSeed9[n];
    return g_sig9;
}

int DescrambleBlock(u32* block) {
    return DescrambleBlock(block, g_sig9);
}

// =============================================================================
// 0x140ec90 — VIBE_Drm_DecryptKeyTable  (faithful whole-function reconstruction)
// =============================================================================
// Helper mirroring the original's runtime patch-exclusion test:
//   *((_DWORD*)off_145F028 + 1) && j == (*((_WORD*)off_145F028 + v7 + 4) & 0xFFF)
static bool KtPatchHit(const KeyTableContext& ctx, int v7, int j) {
    if (!ctx.patchActive || !ctx.patchPos) return false;
    if (v7 < 0 || v7 >= ctx.patchCount)    return false;
    return j == (static_cast<int>(ctx.patchPos[v7]) & 0xFFF);
}

void DecryptKeyTable(KeyTableContext& ctx) {
    // ----- file-read prologue (OS-coupled; alloc/read via hook) ----------------
    // word_145F060 = byte_142D4D8 | byte_142D4D9<<8 ; v8 = alloc(word_145F060) ;
    // ReadFile(handle, dword_145D520, v8, word_145F060). Modeled by ctx.buf.
    if (ctx.buf && ctx.bufLen)
        HookRead(ctx.fileHandle, ctx.fileOffset, ctx.buf, ctx.bufLen);

    const int  v6 = static_cast<int>(ctx.skipBase);          // (unsigned __int16)v6
    const u16  v2 = ctx.preRoll;                             // dest-window base
    const int  payloadLen = static_cast<int>(ctx.payloadLen);// word_145F060

    // ----- the per-column key transform + interleaved byte-decrypt -------------
    for (int i = 0; i < 9; ++i) {                            /* 0x140ed78 */
        // *((BYTE*)&dword_145CAF0 + i) ^= byte_145A790[i];
        ctx.keyState9[i] = static_cast<u8>(ctx.keyState9[i] ^ kKeyStream9[i]);

        for (int j = 0; j < 9; ++j) {                        /* 0x140edaf */
            if (j == 4) {                                    /* 0x140edcf */
                ctx.rotSeed = static_cast<u8>(ctx.rotSeed - 21);     // byte_145A124 -= 21
                ctx.keyState9[i] = static_cast<u8>(
                    guild::util::AlignTo8(ctx.keyState9[i], kRotBias + 3));
            } else if (i) {                                  /* 0x140edd5 */
                ctx.keyState9[i] = static_cast<u8>(
                    guild::util::RotateByte(ctx.keyState9[i],
                                            j + kRotTab9[j]));
            } else {                                         /* 0x140ee1a */
                // RotateByte(state[0], (u8)(byte_145A124 + j + byte_145A77B[j] - (31*j+48)))
                u8 sh = static_cast<u8>(ctx.rotSeed + j + kRotTab9[j] - (31 * j + 48));
                ctx.keyState9[0] = static_cast<u8>(
                    guild::util::RotateByte(ctx.keyState9[0], sh));
            }
        }
        // state[i] = -119*(i+5) + byte_145BA17 + state[i] - byte_145A77F;
        ctx.keyState9[i] = static_cast<u8>(
            -119 * (i + 5) + kFinalBias + ctx.keyState9[i] - kRotBias);

        // ----- inner cursor 1 (skip the pre-roll) @0x140eede -------------------
        int j  = 0;                                          /* 0x140eec5 */
        int v7 = 0;                                          /* 0x140eecc */
        while (j < v6) {
            if (KtPatchHit(ctx, v7, j)) { ++v7; j += 4; }
            else                        { ++j; }
        }
        // ----- inner cursor 2 (decrypt) @0x140ef3e -----------------------------
        while (j < payloadLen + v6) {
            if (KtPatchHit(ctx, v7, j)) {
                // copy-out window (dword_145CA20) — OS-coupled dst, optional.
                int rel = j - v6 - static_cast<int>(v2);
                if (j - v6 >= static_cast<int>(v2) && j - v6 < static_cast<int>(v2) + 61) {
                    if (ctx.destMain && rel + 4 <= ctx.destCap && ctx.buf
                        && static_cast<u32>(j - v6) + 4 <= ctx.bufLen)
                        std::memcpy(ctx.destMain + rel, ctx.buf + (j - v6), 4);
                    // off_14604E0[rel..]=0 / off_145CBA0[rel..]=0 — pointer clears
                    // in the original; the dst windows here are bytewise & cleared
                    // lazily by the caller, so nothing to do for the cipher math.
                }
                ++v7; j += 4;
            } else {
                if (i == j % 9) {                            /* 0x140f051 */
                    int rel = j - v6 - static_cast<int>(v2);
                    if (j - v6 >= static_cast<int>(v2) && j - v6 < static_cast<int>(v2) + 64) {
                        if (ctx.destMain && rel >= 0 && rel < ctx.destCap && ctx.buf
                            && static_cast<u32>(j - v6) < ctx.bufLen)
                            ctx.destMain[rel] = ctx.buf[j - v6];
                    }
                    if (ctx.buf && static_cast<u32>(j - v6) < ctx.bufLen) {
                        // v8[j-v6] ^= state[j%9];  v8[j-v6] += titleId[j%18];
                        ctx.buf[j - v6] = static_cast<u8>(
                            ctx.buf[j - v6] ^ ctx.keyState9[j % 9]);
                        ctx.buf[j - v6] = static_cast<u8>(
                            ctx.buf[j - v6] + kTitleId[j % static_cast<int>(kTitleIdMod)]);
                    }
                    if (j - v6 >= static_cast<int>(v2) && j - v6 < static_cast<int>(v2) + 64) {
                        if (ctx.destSig && rel >= 0 && rel < ctx.destCap && ctx.buf
                            && static_cast<u32>(j - v6) < ctx.bufLen)
                            ctx.destSig[rel] = ctx.buf[j - v6];
                    }
                }
                ++j;
            }
        }
        // if (i == 8) MEMORY[3] = 24;  — the int 3 trap byte poke (anti-debug).
        if (i == 8 && GetHooks().DebugBreak)
            GetHooks().DebugBreak();
    }

    // ----- write-back epilogue (OS-coupled) ------------------------------------
    if (ctx.buf && ctx.bufLen)
        HookWrite(ctx.fileHandle, ctx.fileOffset, ctx.buf, ctx.bufLen);
}

// =============================================================================
// 0x140bc10 — VIBE_Drm_DecryptOverlay  (faithful whole-function reconstruction)
// =============================================================================
void DecryptOverlay(OverlayContext& ctx) {
    // ----- Buffer A: read, then Pass 1 @0x140bd03, then write back -------------
    if (ctx.bufA && ctx.lenA) {
        HookRead(ctx.fileHandle, 0, ctx.bufA, ctx.lenA);
        ctx.csumA = 0;                                       // *dword_145AE70 = 0
        for (i32 k = static_cast<i32>(ctx.lenA) - 2; k >= 0; --k) {  /* 0x140bd03 */
            ctx.csumA = static_cast<u16>(ctx.csumA + ctx.bufA[k]);
            ctx.bufA[k] = static_cast<u8>(
                ctx.bufA[k] + kTitleId[static_cast<u32>(k) % kTitleIdMod]);
            ctx.bufA[k] = static_cast<u8>(ctx.bufA[k] ^ ctx.bufA[k + 1]);
        }
        HookWrite(ctx.fileHandle, 0, ctx.bufA, ctx.lenA);
    }

    // ----- Buffer B: read, Pass 2 @0x140bec9, Pass 3 @0x140bf62, sig-fold ------
    if (ctx.bufB && ctx.lenB) {
        HookRead(ctx.fileHandle, 0, ctx.bufB, ctx.lenB);

        // Pass 2 @0x140bec9 (reverse): B[k] ^= B[k+1]; B[k] += 12 / (k % 5).
        for (i32 k = static_cast<i32>(ctx.lenB) - 2; k >= 0; --k) {  /* 0x140bec9 */
            ctx.bufB[k] = static_cast<u8>(ctx.bufB[k] ^ ctx.bufB[k + 1]);
            ctx.bufB[k] = static_cast<u8>(ctx.bufB[k] + 12 / (k % 5)); // traps if k%5==0
        }

        // Pass 3 @0x140bf62 (forward): csumB += B[k]; B[k] ^= key[k%4096].
        ctx.csumB = 0;                                       // *dword_145A568 = 0
        if (ctx.key4096) {
            for (u32 k = 0; k < ctx.lenB; ++k) {             /* 0x140bf62 */
                ctx.csumB = static_cast<u16>(ctx.csumB + ctx.bufB[k]);
                ctx.bufB[k] = static_cast<u8>(ctx.bufB[k] ^ ctx.key4096[k % 4096]);
            }

            // Sig-fold @0x140c038 (strided): B[p] ^= key[p%4096];
            //   sig[cnt%80] ^= key[p%4096]; ++cnt; p = (p ? p+37 : 34).
            if (ctx.sig80) {
                int cnt = 0;                                 // dword_145A560
                for (u32 p = 0; p < ctx.lenB; ) {            /* 0x140c038 */
                    u8 ks = ctx.key4096[p % 4096];
                    ctx.bufB[p]            = static_cast<u8>(ctx.bufB[p] ^ ks);
                    ctx.sig80[cnt % 80]    = static_cast<u8>(ctx.sig80[cnt % 80] ^ ks);
                    ++cnt;
                    if (p) p += 37; else p = 34;
                }
                // MEMORY[0] = 0; — null poke (anti-debug); routed to DebugBreak.
                if (GetHooks().DebugBreak) GetHooks().DebugBreak();
            }
        }
        HookWrite(ctx.fileHandle, 0, ctx.bufB, ctx.lenB);
    }

    // ----- Buffer C: third range @0x140c2a3 (forward XOR with a 4-byte skip) ---
    if (ctx.bufC && ctx.lenC && ctx.key4096) {
        HookRead(ctx.fileHandle, 0, ctx.bufC, ctx.lenC);
        for (u32 k = 0; k < ctx.lenC; ++k) {                 /* 0x140c2a3 */
            if (k == ctx.skipC)                              // dword_145A600
                k += 4;
            if (k >= ctx.lenC) break;
            // *dword_145BA18 += B[k]; (a 32-bit checksum, not surfaced here)
            ctx.bufC[k] = static_cast<u8>(ctx.bufC[k] ^ ctx.key4096[k % 4096]);
        }
        HookWrite(ctx.fileHandle, 0, ctx.bufC, ctx.lenC);
    }
}

// =============================================================================
// 0x140f5f0 — VIBE_Drm_LookupKeyEntry  (__stdcall, 15 args)  — faithful prototype
// =============================================================================
namespace {
// The parallel arrays the original indexed by [10*i] are exposed through KeyEntry.
// A process-wide table pointer lets the exact-signature function read them; tests
// install the table via SetKeyTable (mirrors the original's static globals).
const KeyEntry* g_keyTable = nullptr;   // dword_1451480.. parallel arrays
}
void SetKeyTable(const KeyEntry* table) { g_keyTable = table; }

int LookupKeyEntry(int* a1, int a2, int a3, int a4, int a5, int a6, int a7,
                   intptr_t a8,
                   int a9, int a10, int a11, int a12, int a13, int* a14, int* a15) {
    int v18 = 0;                                             /* 0x140f5f9 */
    *a1 = 32;                                                /* 0x140f603 */
    const KeyEntry* T = g_keyTable;
    for (int i = 0; i < 32 && !v18; ++i) {                   /* 0x140f609 */
        if (!T) break;
        const KeyEntry& e = T[i];
        if (e.active) {                                      /* dword_1451480[10*i] */
            switch (e.kind) {                                /* dword_145149C[10*i] */
                case 0:
                    if (e.key == a2 && (e.val1 == a3 || !e.mask1) && (e.val2 == a4 || !e.mask2)) {
                        *a14 = a3; *a15 = a4; v18 = 1;
                    }
                    break;
                case 1:
                    if (e.key == a3 && (e.val1 == a4 || !e.mask1) && (e.val2 == a5 || !e.mask2)) {
                        *a14 = a4; *a15 = a5; v18 = 1;
                    }
                    break;
                case 2:
                    if (e.key == a4 && (e.val1 == a5 || !e.mask1) && (e.val2 == a6 || !e.mask2)) {
                        *a14 = a5; *a15 = a6; v18 = 1;
                    }
                    break;
                case 3:
                    if (e.key == a5 && (e.val1 == a6 || !e.mask1) && (e.val2 == a7 || !e.mask2)) {
                        *a14 = a6; *a15 = a7; v18 = 1;
                    }
                    break;
                case 16:
                    if (e.key == a9 && (e.val1 == a10 || !e.mask1) && (e.val2 == a11 || !e.mask2)) {
                        *a14 = a10; *a15 = a11; v18 = 1;
                    }
                    break;
                case 17:
                    if (e.key == a10 && (e.val1 == a11 || !e.mask1) && (e.val2 == a12 || !e.mask2)) {
                        *a14 = a11; *a15 = a12; v18 = 1;
                    }
                    break;
                case 18:
                    if (e.key == a11 && (e.val1 == a12 || !e.mask1) && (e.val2 == a13 || !e.mask2)) {
                        *a14 = a12; *a15 = a13; v18 = 1;
                    }
                    break;
                case 19:
                    // for (j = 0; j < 48; j += 4): read *(j+a8+4), *(j+a8+8),
                    // *(j+a8+12). a8 is an address-by-value in the original; tests
                    // pass a real pointer cast to int. Dereference byte-faithfully.
                    for (int j = 0; j < 48; j += 4) {        /* 0x140f95d */
                        auto rd = [&](int off) -> int {
                            const char* base = reinterpret_cast<const char*>(a8);
                            return *reinterpret_cast<const int*>(base + j + off);
                        };
                        int k0  = rd(4);                     // *(j + a8 + 4)
                        int v17 = rd(8);                     // *(j + a8 + 8)
                        int v16 = rd(12);                    // *(j + a8 + 12)
                        if (e.key == k0 && (e.val1 == v17 || !e.mask1)
                                        && (e.val2 == v16 || !e.mask2)) {
                            *a14 = v17; *a15 = v16; v18 = 1;
                            break;
                        }
                    }
                    break;
                default:
                    break;
            }
            if (v18)                                         /* 0x140fa04 */
                *a1 = i;
        }
    }
    return v18;                                              /* 0x140fa1f */
}

// =============================================================================
// 0x1411eb0 — VIBE_Drm_CompareSignature
// =============================================================================
u32 CompareSignature(const u8* fresh, const u8* stored, bool patchActive, int* equalOut) {
    // VIBE_Mem_Compare (0x1422010) has plain memcmp semantics (0 == equal).
    u32 result;
    if (patchActive)
        result = static_cast<u32>(std::memcmp(fresh + 1, stored + 1, 0x3B)); // off_..+1, 0x3B
    else
        result = static_cast<u32>(std::memcmp(fresh, stored, 0x40));         // off_.., 0x40
    if (equalOut)
        *equalOut = (result == 0);                            // dword_145D688 = result == 0
    return result;
}

// =============================================================================
// 0x140c428..0x140c4ed — VerifyDisc's per-disc keystream generator (pure kernel).
// =============================================================================
int VerifyDiscKeyStream(i32 seed, const u8 sig9[9], i32* out, int outCap) {
    i32 acc = seed;                          // v18 = *(_DWORD*)off_1451A04
    int k = 0;                               // write cursor into dword_145B920
    for (int i = 0; i < 256; ++i) {          /* 0x140c428 */
        acc += (kSectorTab9[i % 9] & 0x1F) + 32;             /* 0x140c47f */
        for (int j = 0; j < 9; ++j) {        /* 0x140c485 */
            if (i == sig9[j]) {              // i == *((u8*)&dword_142DD60 + j)
                if (out && k < outCap)
                    out[k] = acc;
                ++k;
            }
        }
    }
    return k;
}

// =============================================================================
// 0x140c380 — VIBE_Drm_NopStub   /   0x140f210 — VIBE_Drm_NopStub2
// =============================================================================
void NopStub()  { /* gilde.exe 0x140c380 — empty (ret); address sled only. */ }
void NopStub2() { /* gilde.exe 0x140f210 — empty (ret); address sled only. */ }

} // namespace guild::drm::cipher
