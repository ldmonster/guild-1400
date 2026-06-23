#pragma once
#include "guild/common/types.h"
#include <cstdint>   // intptr_t (kind-19 table base address; see LookupKeyEntry)

// =============================================================================
// guild::drm::cipher — 1:1 reconstruction of the DRM cipher / key-table cores.
// =============================================================================
//
// SCOPE
//   This module reconstructs the crypto/decode kernels of gilde.exe's copy-
//   protection cluster (the SafeDisc/SecuROM-class layer living in the high
//   `0x140xxxx` overlay region). The user has explicitly approved reconstructing
//   this cluster 1:1 (overrides the usual rule-6 "ask first").
//
//   Translated byte-for-byte from the Hex-Rays decompile cross-checked against
//   disasm (disasm is ground truth in the encrypted >=0x140b000 overlay region):
//   exact bit operations, table bytes, XOR/rotate/add order, integer wraparound,
//   loop bounds and edge cases.
//
// FIDELITY NOTE (vs. an earlier draft)
//   An earlier draft "factored" DecryptKeyTable / DecryptOverlay into clean little
//   sub-passes (OverlayPass1/2/3, TransformKeyState, DecryptKeyBufferColumn). That
//   silently dropped real behavior (the interleaved inner byte loops, the patch-
//   exclusion overlay skips, the position windows, the signature-fold loop, the
//   second/third overlay buffers). Per rules 1 & 8 that is not a 1:1 clone. This
//   version reconstructs each function AS A WHOLE, preserving every loop, with the
//   OS-coupled leaves (file read/write, alloc, the int 3, the runtime patch overlay
//   and disc-state globals) routed through a Context / Hooks so the cipher math is
//   exercisable headlessly and is NEVER faked (rule 8).
//
// RELATIONSHIP TO src/drm/drm_stub.{h,cpp}
//   drm_stub.cpp provides INERT entry-point stubs (Main/VerifyDisc/DecryptOverlay/
//   DecryptKeyTable/CompareSignature in `guild::drm`) so the game boots headlessly.
//   THIS module is the faithful, exercisable cipher logic; to avoid any ODR clash
//   with those free functions it lives in the nested namespace `guild::drm::cipher`
//   with distinct names. The two coexist.
// =============================================================================

namespace guild::drm::cipher {

// -----------------------------------------------------------------------------
// Cipher constant tables (recovered byte-exact with get_bytes from gilde.exe).
// -----------------------------------------------------------------------------

// gilde.exe 0x145A790 — byte_145A790: 9-byte keystream table.
extern const u8 kKeyStream9[9];

// gilde.exe 0x142DD60 — dword_142DD60: the 9 live signature bytes (16 are
// addressable; the upper 7 are zero). VerifyDisc copies 9 of them out.
extern const u8 kSigSeed9[9];

// gilde.exe 0x142DD50 — byte_142DD50: 9-byte sector-offset table used by the
// VerifyDisc keystream generator (only the low 5 bits of each entry matter).
extern const u8 kSectorTab9[9];

// gilde.exe 0x145A77B — byte_145A77B[9]: per-step rotate-amount table used by
// DecryptKeyTable's inner byte-rotation.
extern const u8 kRotTab9[9];

// gilde.exe 0x145CAF0 — dword_145CAF0: the initial 9-byte key-table cipher state
// as it sits in the (already-decrypted) image.
extern const u8 kKeyTableSeed9[9];

// gilde.exe 0x142DD28 — aUkd54852000100: the title id "UKD_548520-001.001", used
// as a per-byte additive keystream (added modulo its length, dword_142DDB0 = 18).
extern const char kTitleId[];      // NUL-terminated; 18 visible chars
// gilde.exe 0x142DDB0 — dword_142DDB0: modulus for the title-id stream = 18.
constexpr u32 kTitleIdMod = 18u;

// Scalar cipher constants (recovered byte-exact).
constexpr u8 kRotBias    = 0x1A;   // gilde.exe 0x145A77F — byte_145A77F
constexpr u8 kRotSeedInit= 0x22;   // gilde.exe 0x145A124 — byte_145A124 (initial)
constexpr u8 kFinalBias  = 0x1A;   // gilde.exe 0x145BA17 — byte_145BA17

// =============================================================================
// Inert-default hook table for the irreducible OS-coupled leaves.
// =============================================================================
//
// gilde.exe 0x140b080 — VIBE_Vfs_ReadFile_Thunk (-> dword_145A540 -> ReadFile)
//   Read(handle, offset, dst, len): fill dst[0..len) with file bytes at offset.
// gilde.exe 0x145B904 — write-back thunk (overlay re-encrypt / write file)
//   Write(handle, offset, src, len): persist src[0..len) back to offset.
// gilde.exe 0x140ec03 — the DescrambleBlock `int 3` anti-debug trap (__debugbreak).
struct Hooks {
    // Read `len` bytes from `offset` of protected file `handle` into `dst`.
    // Default (nullptr): zero-fill (no file present).
    void (*Read)(int handle, u32 offset, u8* dst, u32 len) = nullptr;
    // Write `len` bytes of `src` back to `offset` of `handle`. Default: no-op.
    void (*Write)(int handle, u32 offset, const u8* src, u32 len) = nullptr;
    // The `int 3` trap DescrambleBlock executes on a parity violation. Default:
    // no-op (the original breaks into a debugger / crashes).
    void (*DebugBreak)() = nullptr;
};

// The process-wide hook table. Tests may swap it to feed deterministic I/O.
Hooks& GetHooks();

// -----------------------------------------------------------------------------
// 0x140ea90 — VIBE_Drm_DescrambleBlock  (__stdcall BOOL(_DWORD* a1), retn 4)
// -----------------------------------------------------------------------------
// Descrambles one 32-bit block in place and folds a parity nibble back into the
// disc-signature accumulator `sig9` (the original's dword_142DD60). Faithful to
// the decompile/disasm: returns nonzero only when v9==0 && v3==0.
//   * v10 = sum(byte[1..3]); byte[0] ^= v10
//   * byte[1..3] ^= kKeyStream9[(j + (block&0xF)) % 9]
//   * v9 = ((block>>20)&0xF) | ((block>>24)&0xF0)
//     v3 = ((block>>4)&0xF)  | ((block>>8)&0xF0)
//   * for k in [0,4): v9 -= block-nibble[k]; (k<3) v3 -= ...; else if(v3&0xFF) int3;
//     v8 |= block-nibble[k] << (4k)
//   * block = v8
//   * if v9: sig9[(v9+v3)%9u] ^= (u8)v8; return 0; else return !v3
// `sig9` must point to a 9-byte mutable buffer (the signature accumulator).
int DescrambleBlock(u32* block, u8* sig9);

// Convenience overload using a process-wide signature accumulator that starts
// from kSigSeed9 (mirrors the original's single global dword_142DD60).
int DescrambleBlock(u32* block);
// Reset the process-wide signature accumulator to kSigSeed9. Returns its buffer.
u8* ResetSignatureAccumulator();

// -----------------------------------------------------------------------------
// 0x140ec90 — VIBE_Drm_DecryptKeyTable  (void(), uses globals + file I/O)
// -----------------------------------------------------------------------------
// Faithful whole-function reconstruction. The original: allocs a key buffer,
// reads it from the protected file, then for each of 9 key-state columns (i) it
// (1) transforms the running key state dword_145CAF0 with the rotate schedule and
// (2) walks the buffer with two position cursors honoring the runtime patch-
// exclusion overlay (off_145F028), XOR-ing matching bytes with the column key and
// adding the title-id stream, then writes the buffer back. The OS-coupled leaves
// (read/write/alloc, off_145F028 contents, the destination/copy-out buffers,
// MEMORY[3]) are supplied by the Context; the cipher math is exact.
//
// The patch-exclusion overlay: in the original off_145F028[+1] is a flag and
// (WORD*)off_145F028 + index + 4 yields a 12-bit position to skip-by-4. Modeled
// here as a sorted list of (position & 0xFFF) skip points (`patchPos`), active
// only when `patchActive` is set — exactly matching the original's two predicates.
struct KeyTableContext {
    int   fileHandle  = 0;       // dword_1459FD8
    u32   fileOffset  = 0;       // dword_145D520 (already &= 0xFFFFF000 + low word)
    u8    keyState9[9] = {       // dword_145CAF0 running state (seeded from image)
        0x07, 0xa8, 0x2c, 0x8b, 0x0c, 0x24, 0xc9, 0xee, 0xbb };
    u8    rotSeed     = kRotSeedInit;  // byte_145A124 (threaded across columns)

    u16   payloadLen  = 0;       // word_145F060 (bytes read into the key buffer)
    u16   preRoll      = 0;      // v2 = byte_142D4D0|byte_142D4D1<<8 (dest window base)
    u16   skipBase     = 0;      // (unsigned __int16)v6 = byte_142D4DA|..DB<<8

    u8*   buf         = nullptr; // v8 — the key buffer (length preRoll? ) see .cpp
    u32   bufLen      = 0;       // capacity of `buf`

    // Patch-exclusion overlay (off_145F028).
    bool        patchActive = false;          // *((_DWORD*)off_145F028 + 1)
    const u16*  patchPos    = nullptr;        // (WORD*)off_145F028 + idx + 4
    int         patchCount  = 0;

    // Copy-out destinations (dword_145CA20 / off_14604E0 / off_145CBA0). May be
    // null to disable the 0x40-byte copy-out window; the in-place cipher still runs.
    u8*   destMain  = nullptr;   // dword_145CA20 (64-byte window dst)
    u8*   destSig   = nullptr;   // off_145CBA0   (64-byte signature window dst)
    int   destCap   = 0;
};
void DecryptKeyTable(KeyTableContext& ctx);

// -----------------------------------------------------------------------------
// 0x140bc10 — VIBE_Drm_DecryptOverlay  (void(), uses globals + file I/O)
// -----------------------------------------------------------------------------
// Faithful whole-function reconstruction of the overlay decryptor's PURE byte-
// transform core. The original reads three encrypted .text ranges from the EXE,
// runs the transforms below, folds part of the result into the disc signature,
// and writes the plaintext back; the file reads/writes/allocs and the EXE range
// bounds (the NopStub address sled) are the OS-coupled remainder supplied by the
// Context. Every transform loop is reproduced exactly.
//
//   Buffer A (len = a604+0520):
//     Pass 1 @0x140bd03 reverse len-2..0: csumA += A[k];
//                       A[k] += kTitleId[k%18]; A[k] ^= A[k+1].
//   Buffer B (len from NopStub4-NopStub-10), key = 4096-byte keystream:
//     Pass 2 @0x140bec9 reverse len-2..0: B[k] ^= B[k+1]; B[k] += 12/(k%5).
//       NOTE: when k%5==0 the original integer-divides by zero (a real x86 trap).
//       We reproduce the exact arithmetic; the original's data avoided k%5==0 at
//       runtime. Callers exercising k>=5 must likewise avoid k%5==0. See .cpp.
//     Pass 3 @0x140bf62 forward 0..len-1: csumB += B[k]; B[k] ^= key[k%4096].
//     Sig-fold @0x140c038 strided walk: B[p] ^= key[p%4096];
//       sig[cnt%80] ^= key[p%4096]; ++cnt; p = (p? p+37 : 34).
//   The third overlay buffer (off_145E1EC range) decrypt @0x140c2a3 mirrors pass3
//   with a single 4-byte skip at dword_145A600 and is included.
struct OverlayContext {
    int   fileHandle = 0;        // dword_1459FD8

    // Buffer A.
    u8*   bufA = nullptr;        // dword_14605B0
    u32   lenA = 0;              // dword_145A544 = a604 + 0520

    // Buffer B + its 4096-byte keystream.
    u8*   bufB = nullptr;        // dword_14605B4 / dword_14605E8
    u32   lenB = 0;              // dword_145D530
    const u8* key4096 = nullptr; // dword_145AD20 (read from file; required for B)

    // Signature accumulator for the strided fold (off_145D684 -> dword_142DD60).
    u8*   sig80 = nullptr;       // 80-byte signature window; null disables the fold.

    // Buffer C (third range).
    u8*   bufC = nullptr;        // second dword_14605B4
    u32   lenC = 0;              // second dword_145D530
    u32   skipC = 0;             // dword_145A600 (4-byte skip point inside C)

    // Outputs (the original's *dword_145AE70 / *dword_145A568 16-bit checksums).
    u16   csumA = 0;
    u16   csumB = 0;
};
void DecryptOverlay(OverlayContext& ctx);

// -----------------------------------------------------------------------------
// 0x140f5f0 — VIBE_Drm_LookupKeyEntry  (__stdcall, 15 args; loop i<32)
// -----------------------------------------------------------------------------
// Faithful to the original __stdcall prototype and the parallel-array key table:
//   dword_1451480 (+0 active), dword_1451488 (+2 key), dword_145148C (+3 mask1),
//   dword_1451490 (+4 val1),   dword_1451494 (+5 mask2), dword_1451498 (+6 val2),
//   dword_145149C (+7 kind), stride 10 dwords. The original used 32 fixed rows.
struct KeyEntry {                  // gilde.exe parallel arrays @0x1451480, stride 10 dwords
    i32 active;   // +0  dword_1451480
    i32 pad1;     // +1  dword_1451484 (unused by the lookup)
    i32 key;      // +2  dword_1451488
    i32 mask1;    // +3  dword_145148C: if 0, val1 is wildcard
    i32 val1;     // +4  dword_1451490
    i32 mask2;    // +5  dword_1451494: if 0, val2 is wildcard
    i32 val2;     // +6  dword_1451498
    i32 kind;     // +7  dword_145149C: selector (0..3, 16..19)
    i32 pad8;     // +8  dword_14514A0 (unused)
    i32 pad9;     // +9  dword_14514A4 (unused)
};

// Install the 32-row parallel-array key table (the original's static globals at
// dword_1451480..). Must be set before LookupKeyEntry; null table => no match.
void SetKeyTable(const KeyEntry* table);

// Exact __stdcall arg order. a1=*outIndex (set to 32, then the match index).
// a2..a13 are the sliding-window args; a8 is NOT in any window — in the original
// it is used ONLY by kind 19 as the base ADDRESS of an inline table (rows read at
// a8+4/+8/+12 over j=0..44 step 4). The 32-bit original stored that address in an
// int; in this 64-bit-safe reconstruction a8 is widened to intptr_t so a real
// pointer survives (its windowed role is unchanged — there is none). a14/a15
// receive the two companion values on match. Returns 1 (found) / 0 (not found).
int LookupKeyEntry(int* a1, int a2, int a3, int a4, int a5, int a6, int a7,
                   intptr_t a8,
                   int a9, int a10, int a11, int a12, int a13, int* a14, int* a15);

// -----------------------------------------------------------------------------
// 0x1411eb0 — VIBE_Drm_CompareSignature  (unsigned int())
// -----------------------------------------------------------------------------
// Compares the freshly-decrypted signature block against the stored one. Length
// and start offset depend on whether the patch overlay (off_145F028[+1]) is active:
//   active  -> compare 0x3B bytes starting at +1
//   absent  -> compare 0x40 bytes starting at +0
// Returns memcmp() (0 == equal); *equalOut (dword_145D688) = (result == 0).
u32 CompareSignature(const u8* fresh, const u8* stored, bool patchActive, int* equalOut);

// -----------------------------------------------------------------------------
// 0x140c428..0x140c4ed — VerifyDisc's per-disc keystream generator (the only
// PURE-crypto kernel inside the ~700-line SCSI/ASPI/timing VerifyDisc @0x140c3a0;
// the hardware paths are OMITTED — they live behind drm_stub.cpp's VerifyDisc).
// -----------------------------------------------------------------------------
//   acc = seed;
//   for i in [0,256): acc += (kSectorTab9[i % 9] & 0x1F) + 32;
//                     for j in [0,9): if (i == sig9[j]) out[k++] = acc;
// `sig9` is the 9-byte signature (dword_142DD60). Writes up to `outCap` entries;
// returns the total count produced (the original filled dword_145B920).
int VerifyDiscKeyStream(i32 seed, const u8 sig9[9], i32* out, int outCap);

// -----------------------------------------------------------------------------
// 0x140c380 — VIBE_Drm_NopStub   /   0x140f210 — VIBE_Drm_NopStub2
// -----------------------------------------------------------------------------
// Inert in the running flow but their address span carries the EXE byte-range
// bounds the overlay decryptor consumes (a `mov edi,<lo>; mov esi,<hi>` sled).
// The functions themselves are empty (a single `ret`); reproduced for completeness.
void NopStub();
void NopStub2();

} // namespace guild::drm::cipher
