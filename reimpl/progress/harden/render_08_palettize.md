# Harden — render/texture_palettize.cpp (VIBE_Quant_* 24-bit palettizer)

Line-for-line diff of every `gilde.exe 0xADDR`-provenanced function in
`src/render/texture_palettize.cpp` against the IDA Hex-Rays decompile + disasm
(module `gilde.exe`, imagebase 0x400000). DISASM wins on conflict. No divergences
found — every quantizer function is a faithful 1:1 translation.

Result: **12 VERIFIED-1:1, 0 FIXED, 0 BOUNDARY.**

---

## 0x602aa4 VIBE_Quant_InitLookupTables — VERIFIED-1:1
Bit-shuffle tables and squares table both match.
- rTab (`word_1409508`): `(i&8)>>1 | 2*(i&0x10) | 32*(i&0x40) | (i&0x80)<<7 | 8*(i&0x20)` — exact.
- bTab (`word_14090F0`): `(i&8)>>3 | (i&0x80)<<5 | 8*(i&0x40) | 2*(i&0x20) | (i&0x10)>>1`.
  Disasm uses `32*v2` (v2=i&0x80); `32*x == x<<5` — identical.
- gTab (`word_14092F0`): `(i&8)>>2 | (i&0x10) | 4*(i&0x20) | 16*(i&0x40) | (i&0x80)<<6` — exact.
- Index alias check: disasm writes rTab via `word_1409508[v0++]` then bTab/gTab at
  the already-incremented `[v0]` against bases `word_14090EE`/`word_14092EE`, i.e.
  `base+2*(i+1)` = `(base+2)+2*i` = `word_14090F0[i]` / `word_14092F0[i]`. The recon's
  `bTab/gTab` bases match. ✓
- Squares: disasm `do { v8=result*result; ++v6; ++result; dword_1409E0C[v6]=v8; }`
  with v6/result from -255 stores `d*d` at `dword_1409E0C[d+1]`, and
  `dword_140A210 = &dword_1409E0C[1]`, so `dword_140A210[d] = d*d`. Recon uses
  self-consistent `squares[d+255]=d*d`, `Sq(d)=squares[d+255]` — behaviour-identical,
  array size 511 covers d∈[-255,255]. ✓

## 0x602be0 VIBE_Quant_AllocColorNodes — VERIFIED-1:1
Level sizes {1,8,64,512,4096,0x8000} match `VIBE_Memory_AllocZeroed(24,N)` calls.
`dword_140A214 (palCount) = 0` after alloc. Recon allocation cannot fail (RAII);
the original's per-level NULL checks are vacuous here. ✓

## 0x602c8c VIBE_Quant_FreeColorNodes — VERIFIED-1:1
Frees the 6 level arrays + `dword_64ADB4` (heap) + `dword_64ADB8` (second scratch).
Recon manages all of these as std::vectors (RAII destruction / shrink_to_fit) —
no observable difference. The `dword_64ADB8` buffer corresponds to the second
dither row buffer, also a local std::vector in the recon. ✓

## 0x602f3c VIBE_Quant_HeapSiftDown — VERIFIED-1:1
Min-heap sift on node `+16` (subtree total). `half = dword_1409A08 >> 1` (unsigned),
loop gate `v1 <= half`, child select `2*v1 < leafCount` then compare
`heap[2*v1+1].total < heap[2*v1].total` (+6/+2 idx fields), `break` on
`key <= heap[c].total`, promote `heap[v1]=heap[c]; v1=c`, `while(c<=half)`, restore
`heap[v1]=saved`. All exact. ✓

## 0x602d2c VIBE_Quant_BuildHistogram — VERIFIED-1:1
- Heap alloc `0x20004` bytes (0x8001 4-byte slots, 1-based); recon `assign(0x8001+1)`
  — one extra slot of harmless padding.
- code = `rTab[p0]+gTab[p1]+bTab[p2]` (disasm sums bTab[v5[2]]+gTab[v5[1]]+rTab[*v5];
  same sum); `++l5[code].count` (+12). ✓
- De-interleave masks for r5/g5/b5 match exactly; `sum{R,G,B} = count*(channel+4)`
  (bucket-centre bias) — exact, including `total(+16)=count`. ✓
- Ancestor loop levels 4..0: `bit=v15&7; v15>>=3; levels[lvl][v15].total += c;
  levels[lvl][v15].mask |= 1<<bit` — exact (`v14` decremented from 4 to -1). ✓
- Heapify `for(i=leafCount; i; --i) SiftDown(i)`. ✓

## 0x603068 VIBE_Quant_HeapReduceColors — VERIFIED-1:1
`while(maxColors < leafCount)`: top=heap[1] (idx@+6, level@+4); parentIdx=idx>>3,
pl=level-1. If parent has count (+12) → `heap[1]=heap[leafCount]; --leafCount`,
else `heap[1]={pl,parentIdx}`. Parent count/sumR/sumG/sumB += child (levels[lvl][idx]);
`parent.mask &= ~(1<<(idx&7))`; SiftDown(1) as the loop increment (after body). ✓

## 0x603180 VIBE_Quant_TreeCollectPalette — VERIFIED-1:1
Children bit 7..0 first: `v5=7; v6=8*idx+7; do{ if((1<<v5)&mask) recurse(v6,level+1);
--v5;--v6;} while(v5>=0)`. Then if `count(+12)`: `palIdx(+21)=palCount`; per channel
`pal = (sum + (count>>1)) / count` (unsigned div, `v11` unsigned); palB written via
`byte_1409907[v12+1] == byte_1409908[v12]`. `++palCount`. Recon's `(u32)` casts make
the division unsigned to match. ✓

## 0x603a30 VIBE_Quant_FindClosestColor — VERIFIED-1:1
Query bucketed `q = (c&0xF8)+4` for R/G/B; `bestD = 200000`; distance
`Sq(palB[i]-qb)+Sq(palG[i]-qg)+Sq(palR[i]-qr)` (B/G/R order, via `dword_140A210`
squares base); strict `d < bestD`; returns best leaf index over `dword_1409A08`
leaves. ✓

## 0x6033b4 VIBE_Quant_MapImageToPalette — VERIFIED-1:1 (both arms)
**Undithered arm (a5==0):** 0x8000 closest-colour table over occupied cells using
the same r5/g5/b5 de-interleave; map loop `dst[i]=tbl[rTab[p0]+gTab[p1]+bTab[p2]]`
for `rows*width` pixels. ✓

**Dithered arm (serpentine Floyd–Steinberg):** verified store-by-store.
- Value clamp table (v45, vClamp=+256): [-256..-1]→0, [0..255]→identity,
  [256..511]→255. Disasm stores `v20[512]=-1` (byte 0xFF=255) — recon's `255` matches. ✓
- Error clamp table (v46, eClamp=+256): [-256..-21]→-20, [21..255]→20, [-20..20]→identity.
  The `v22=v46-20; *(++v22+255)=v53++` loop writes clampErr[236..276]=eClamp[-20..20]=e. ✓
- Cache (v57): 32768 i16 init -1; lazy fill via FindClosestColor. ✓
- Pointer choreography: forward `cur=bufA+3, nxt=bufB+3*width, dir=1`; reverse
  `cur=bufB+3, nxt=bufA+3*width, dir=-1, a1+=3w-3, a2+=w-1`; row turnaround
  `if(row%2==1){a1+=3w+3; a2+=w+1}`; `reverse=!reverse` per row. ✓
- Consumption `(acc+8)>>4` (i16, arithmetic >>16 for the high-word channels). ✓
- Cache key `(vB>>3) | ((vR&0xF8)<<7) | (4*(vG&0xF8))`. ✓
- Error weights — R: nxt[-3]=eR, nxt[3]+=3eR, nxt[0]+=5eR, cur[3]+=7eR;
  G (saved-before-advance): nxt[4]+=3eG, nxt[-2]=eG, nxt[1]+=5eG, then `cur+=3; nxt-=3`,
  cur[1]+=7eG; B (post-retreat): nxt[2]=eB, nxt[8]+=3eB, nxt[5]+=5eB, cur[2]+=7eB.
  Each derived from the disasm's `LOWORD(v29)=2*e` / `e*=3` / `2e+3e=5e` / `+7e`
  decomposition — exact. ✓

**Buffer sizing (W10-TEX hardening, behaviour-equivalent).** Disasm allocates each
row buffer at `edx = 6*width` bytes (`lea ecx,[eax*4]; sub ecx,eax` = 3*width;
`lea edx,[ecx+ecx]` = 6*width), i.e. **3*width i16**. The recon allocates
`3*(width+2)` i16. The original's inner loop writes up to `nxt[8]` after a `nxt-=3`
(max index `3*width+5`) and reads `cur` up to index `3*width+4`, both PAST its own
3*width allocation — the original relies on allocator slop. The recon's `+2` padding
(6 extra entries, indices 0..3*width+5) exactly bounds those accesses, so every
logical cell written/read matches the original while keeping the access in-bounds
under ASAN/UBSAN. No output difference (the slop cells are never read across rows
beyond `cur[3*width+4]`, which the padding covers). VERIFIED-1:1 with safe padding.

## 0x602a70 VIBE_Quant_CopyPaletteEntries — VERIFIED-1:1
256 entries planar: `out[k]=palR[k]`, `out[k+256]=palG[k]` (a1[256] pre-++),
`out[k+512]=palB[k]` (a1[511] post-++). ✓

## 0x6029f0 VIBE_Quant_BuildPalette — VERIFIED-1:1
Gate `dword_64ADB0` → InitLookupTables once; `Alloc && Histogram(width*height)`;
on success `HeapReduceColors(maxColors), TreeCollectPalette(0,0),
MapImageToPalette(src, dst, rows=height, width, dither)`, then FreeColorNodes +
CopyPaletteEntries → 1; on failure FreeColorNodes → 0. Arg order to
MapImageToPalette (height as rows, width) matches recon. ✓

---

## Float→int audit
No `ConvertX`/`fistp`/`(int)`-cast float conversions exist anywhere in this family.
The quantizer is pure integer arithmetic throughout (histogram, heap, tree average
`(sum+count/2)/count` unsigned integer division, squared-distance, FS error in
i16). Nothing to convert. N/A.

## Colour-key / PalettizeDecodedBmp note
The W5-CKEY index-0 reservation and the planar→interleaved palette copy are the
documented integration/colour-key design (option (a), progress/colourkey-
integration-wave5.md). The per-texel COLOUR math feeding it is the quantizer above,
which is verified 1:1; the colour-key reservation itself is left unchurned per the
task note.

## Tests
`render_texture_palettize_test`: **12 tests / 230 checks, 0 failures.**
- ctest -R texture_palettize: 1/1 Passed.
- Direct run: all 12 (6 core + 6 W10-TEX edge) green; 1024-colour reduce worst
  sqdist 1195 (well within bound).
No source or golden changes were required — the reconstruction was already faithful.
