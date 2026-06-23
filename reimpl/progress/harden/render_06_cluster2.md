# Harden sweep — render cluster 2 (shape 16bpp converters + GrabBit decode)

Files: `src/render/shape_convert16.cpp`, `src/render/shape_recon_cluster.cpp`
MCP: gilde.exe live. Every provenance-tagged function decompiled + disassembled and
diffed line-for-line. Tests: `shape_convert16_test`, `shape_recon_cluster_test`
(+ `shape_convert16_e2e_test`) all green.

## shape_convert16.cpp

| addr | symbol | verdict |
|------|--------|---------|
| 0x5d7c0c | ShapeConvertRgbTo16 | VERIFIED-1:1 |
| 0x5d7924 | ShapeConvert8To16 | VERIFIED-1:1 |
| 0x5d80a8 | ShapeBankConvertNew | VERIFIED-1:1 (see free-temp boundary) |
| (helper) | ShapeConvertDarkMask / ShapeInitColorMasksImpl mask formula | VERIFIED-1:1 |

Detail of the checks that mattered:
- **word_1406944 mask formula** (0x5d4b64 disasm): per-channel
  `((1<<(7-down))-1)<<up` with B=(762719→down,76271D→up), G=(76271B,76271A),
  R=(76271C,76271E). `ShapeInitColorMasksImpl` and `ShapeConvertDarkMask` both
  reproduce it; 565 → 0x7BEF. VERIFIED.
- **Half-bright shift** raw + RLE branches: disasm 0x5d8046 / 0x5d7daa show
  `sar edx,1` after a zero-extended u16 load, top bit always 0 ⇒ == logical `>>1`;
  reimpl `(px>>1)&darkMask` (px promoted non-negative) is identical. VERIFIED.
- **RGB RLE skip re-encode** `2*skip/3u` (unsigned), 8bpp `2*skip`. VERIFIED.
- **8To16 LUT** stride 4 over the palette quads (disasm 0x5d79dd `add ecx,4`,
  decompile elided it), entries land at v24[1152..1407]; reimpl separate lut[256]
  is behavior-identical. VERIFIED.
- **0→(5,5,5) replacement** present only in the 24bpp RLE branch, absent in the
  raw branch and the 8bpp converter — matches reimpl comments. VERIFIED.
- **0x845 header copy** (disasm 0x5d8106 `mov ecx,845h`), offset table @+0x45 with
  4-byte stride, alloc sizes (fmt2: wc-3*pix+2*pix, fmt0: wc-pix+2*pix), shape
  count @+0x2A, trailing seq data @+62/+67. All VERIFIED.

BOUNDARY — ShapeBankConvertNew (0x5d80a8): the original's per-shape
`if(v10) FreeDebug(...)` is **dead** (v10≡0 from 0x5d812c onward), so gilde.exe
leaks every converted temporary. The reimpl `std::free(shp)` frees it. Blob
output is byte-identical; only the (non-observable) leak differs. Kept as a
deliberate memory-economy deviation, documented here.

## shape_recon_cluster.cpp

| addr | symbol | verdict |
|------|--------|---------|
| 0x434F30 | ColorPack | VERIFIED-1:1 |
| 0x434F7C | ColorUnpack | VERIFIED-1:1 (a2=r, a4=g, a3=b) |
| 0x4226BC | ColorNotEqualRgb | VERIFIED-1:1 |
| 0x4226E0 | ColorSetRgb | VERIFIED-1:1 (dst={r,b,g}) |
| 0x5D49A0 | ShapeBuildLightTable | VERIFIED-1:1 (sat-add edge cases, b<255-a1) |
| 0x5D4BCC | ShapeShouldFreeLightTable | VERIFIED (predicate model; free is alloc boundary) |
| 0x5D88C0 | ShapeSetMaskColor | VERIFIED (mask reg lo|hi<<16; b-channel boundary) |
| 0x41F4F4 | ShapeClassifyType | VERIFIED-1:1 (strcmp vs "SHAPBANK"\0 ≡ memcmp 8) |
| 0x5D8294 | ShapeBankSetPalette | VERIFIED-1:1 (stride 4, packedOut[1..256]) |
| 0x5D8F54 | ShapeBankSetSequenceData | VERIFIED-1:1 |
| 0x5D6160 | ShapeGrabBit8 | **FIXED** (Pass-2 mask scan) |
| 0x5D5908 | ShapeGrabBit16 | VERIFIED-1:1 |
| 0x5D5E7C | ShapeGrabBit16NoRle | VERIFIED-1:1 |
| 0x5D4C20 | ShapeGrabBit24 | **FIXED** (SetRgb arg order, ×2 sites) |
| 0x5D547C | ShapeGrabBit24NoRle | **FIXED** (SetRgb arg order, ×2 sites) |
| 0x5D68C4 | ShapeGrabByDepth | VERIFIED-1:1 (equivalent control flow) |
| 0x5D8E30 | ShapeAnimDrawAllSlots | VERIFIED-1:1 (slot +0 obj,+6 nr,+0x0D x,+0x0F y) |
| 0x5D8F1C/3C | ShapeAnimGetSlot/Table | VERIFIED (ptr model; thunk-copy boundary) |

### FIXED #1 — ColorSetRgb call arg order in GrabBit24 (0x5d5223, 0x5d52ca) and GrabBit24NoRle (0x5d57ef, 0x5d5863)
Evidence: disasm/decompile call is
`VIBE_Color_SetRgb(px, dl=byte_1406946 (maskR), cl=SBYTE1(dword_1406947) (maskB),
bl=LOBYTE(dword_1406947) (maskG))`. The helper stores `dst[0]=a2(dl), dst[1]=a4(bl),
dst[2]=a3(cl)`, so memory becomes `[maskR, maskG, maskB]` — the same 3-byte order
the source pixels are stored in and the order the `&byte_1406946` mask is compared
against.
- BEFORE: `ColorSetRgb(px, st.maskR, st.maskG, st.maskB)` → helper writes
  `[maskR, maskB, maskG]` (channels swapped). WRONG for any asymmetric mask.
- AFTER: `ColorSetRgb(px, st.maskR, st.maskB, st.maskG)` → `[maskR, maskG, maskB]`.
Four call sites fixed. New golden `ShapeReconGrab.Bit24AnchorWritebackByteOrder`
uses an asymmetric mask {0x10,0x20,0x30} and verifies the relocated anchor pixels
read back `[0x10,0x20,0x30]` in memory.

### FIXED #2 — GrabBit8 Pass-2 mask scan reads/zeroes a5[row] only (0x5d63d1–0x5d6461)
Evidence: disasm shows `var_24` (v67) is loaded once per row (`mov [..var_24],eax`
@0x5d63ec where eax=v54, and v54 starts at a5 base, `inc` per row) and the inner
column loop (`loc_5D63F3`) reads `mov al,[edx]` with `edx=var_24` **never
re-indexed by the column counter var_6C**. So the binary inspects only the first
`h` bytes `a5[0..h-1]` (ignoring a1/a2/stride) and the a4-iteration is an idempotent
repeat. Decompile confirms `*v67` with no `+v49`.
- BEFORE: reimpl scanned every pixel `a5[col+a1+(a2+row)*a7]` (a "correct" but
  non-faithful per-pixel scan).
- AFTER: reimpl walks `p = &a5[row]` per row and runs the idempotent inner loop on
  that single byte, exactly matching the binary.
Existing golden `Bit8RleHeaderAndOpaqueCount` is invariant to this change (mask is
{0,0,0} mapping to the already-zero index-0 pixels), so it stays green and still
validates the RLE/bounds path; the fix is a behavioral-fidelity correction for the
out-of-row case.

## Counts
- Functions diffed: 19 provenance-tagged (3 in shape_convert16.cpp, 16 in
  shape_recon_cluster.cpp) + helpers.
- VERIFIED-1:1: 16
- FIXED: 3 functions (GrabBit8 Pass-2; GrabBit24 + GrabBit24NoRle SetRgb order)
  across 5 edited call/loop sites.
- BOUNDARY/documented model: ShapeBankConvertNew free-temp; ShapeShouldFreeLightTable
  / ShapeAnimGetSlot(Table) (allocator/thunk boundaries); ShapeSetMaskColor b-channel
  (caller-register-dependent, modelled b=0); ShapeGrabByDepth descriptor fields
  passed explicitly.
- Tests: shape_convert16_test ✓, shape_recon_cluster_test ✓ (+1 new regression),
  shape_convert16_e2e_test ✓. 3/3 ctest pass.
