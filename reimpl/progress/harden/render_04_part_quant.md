# Harden wave — render_04 part: colour-quantizer + present (1:1 diff)

Scope: `src/render/quant.cpp`, `src/render/quant_octree.cpp`, `src/render/present.cpp`
(+ matching headers, + their tests). Every function carrying `// gilde.exe 0xADDR`
provenance was decompiled AND disassembled via IDA Pro MCP (module gilde.exe,
imagebase 0x400000) and diffed line-for-line against the C++. No divergences found;
no source edits were required.

## Per-function status

### src/render/quant.cpp
| addr | function | status | notes |
|------|----------|--------|-------|
| 0x602aa4 | VIBE_Quant_InitLookupTables | VERIFIED-1:1 | spread tables: bit-spread expressions match decompile exactly. sq LUT verified against disasm: `mov edx,0FFFFFC04h` (-1020 = -255*4 byte offset), `mov eax,0FFFFFF01h` (-255); loop `imul/add edx,4/inc eax/mov dword_1409E0C[edx],ecx` writes `sqBase[d]=d*d` for d in [-255,255] with `dword_140A210 = &dword_1409E0C[1]`. C++ `sq[d+255]=d*d; sqBase=&sq[255]` is identical. The mid/lo +1-word alias is documented and baked into the consumer bucket tables (quant_octree). |
| 0x603a30 | VIBE_Quant_FindClosestColor | VERIFIED-1:1 | bias `(c&0xF8)+4`; squared-dist via `sqBase[palX[i]-bias]` with `(unsigned __int8)` palette loads; sentinel best=200000 (> max possible 3*255^2=195075, so first entry always sets `best`); sum order B+G+R matches; loop bound `dword_1409A08`. C++ default `best=0` is harmless (first iteration always hits). |
| 0x602a70 | VIBE_Quant_CopyPaletteEntries | VERIFIED-1:1 | `*a1=R; a1[256]=G; ++a1; a1[511]=B` == planar out[i]=R, out[i+256]=G, out[i+512]=B. |

### src/render/quant_octree.cpp
| addr | function | status | notes |
|------|----------|--------|-------|
| 0x602BE0 | VIBE_Quant_AllocColorNodes | VERIFIED-1:1 | 6 levels 1/8/64/512/4096/32768, zeroed; palCount=0. (C++ also pre-sizes the heap vector to 32768+1; behaviour-neutral container detail.) |
| 0x602D2C | VIBE_Quant_BuildHistogram | VERIFIED-1:1 | Pass1 bucket = spreadHi(R)+word_14092F0(G)+word_14090F0(B) with R=p0,G=p1,B=p2; matches `bucketR/bucketG(+1)/bucketB(+1)`. Pass2 centroid seeds (CenterR/G/B+4)*cnt, reduce=cnt; up-propagation `idx>>=3` then index, mask bit = pre-shift `idx&7`. Heapify `for i=heapCount..1 SiftDown(i)`. |
| 0x602F3C | VIBE_Quant_HeapSiftDown | VERIFIED-1:1 | min-heap on node[+16]; left=2*v1, smaller-child select, `key<=child` break, `while(child<=half)`. |
| 0x603068 | VIBE_Quant_HeapReduceColors | VERIFIED-1:1 | loop while heapCount>target (unsigned); parent.count!=0 → drop entry `heap[1]=heap[old end]`, `heapCount--`; else repoint root to parent; fold count/r/g/b sums; clear child mask bit `~(1<<(v8&7))`; SiftDown(1) as loop step. |
| 0x603180 | VIBE_Quant_TreeCollectPalette | VERIFIED-1:1 | child DFS high-bit-first (slot 7..0, v6=8*a1+7 down); centroid = `(sum+(W>>1))/W` signed int div (sums positive → floor); palB stored via byte_1409907[palCount+1] alias == palB[palCount]; palIdx=palCount then ++palCount. |
| 0x6033B4 (a5==0) | VIBE_Quant_MapImageToPalette | VERIFIED-1:1 | non-dithered arm only. bucketMap[k]=FindClosestColor(CenterR/G/B(k)) for occupied leaves; per-pixel out[i]=bucketMap[Bucket]. NOTE: binary's FindClosestColor uses `dword_1409A08` (heapCount) as palette size; C++ passes `q.palCount` (dword_140A214). These are provably equal after the pipeline (every surviving heap entry == exactly one count!=0 node assigned a palette index in TreeCollectPalette), so the substitution is behaviour-identical. The dithered arm (a5!=0) lives in texture_palettize.cpp — OUT OF SCOPE for these files. |
| 0x6029F0 | VIBE_Quant_BuildPalette (driver) | VERIFIED-1:1 | order Alloc→BuildHistogram(w*h)→HeapReduce(target)→TreeCollect(0,0)→Map→(Free)→CopyPalette. |

### src/render/present.cpp
| addr | function | status | notes |
|------|----------|--------|-------|
| 0x4349e4 | VIBE_Render_PresentFrame | VERIFIED-1:1 (math) | 5 modes (byte_762721 0..4) routed onto IGraphicsDevice. The DDraw Blt/Flip/Lock/Unlock and GDI BitBlt are the Rule-8 GPU boundary (hook = `dev.present()` / `dev.backbuffer()`). The reconstructable MATH is the case-2/4 qmemcpy decomposition: `v16=(3*lowbyte(lock))&3` head, `4*v20` body words, `v17&3` tail — total == `dword_7626B8`. This collapses exactly to `memcpy(lockptr, ppvBits, dword_7626B8)`; C++ does `memcpy(bb->pixels, st.framebuffer, st.copyBytes)` with copyBytes mirroring dword_7626B8. Case mapping (GDI/Blt/Flip → present(); Lock+Blt / Lock+Flip → backbuffer()+copy+present()) is faithful. The SURFACELOST retry loops and dirty-rect (GetClientRect/ClientToScreen/OffsetRect) are part of the DDraw present hook in the richer surface_present layer (render/surface_present.*), not duplicated here. |

## Build / test

- All three TUs compile clean (verified with `-fsyntax-only`; quant/quant_octree under
  the project include set, present under `-I.` so `shim/IGraphicsDevice.h` resolves).
- `render_texture_palettize_test` (#511) — PASS (golden vectors exercise the full
  quant_octree pipeline + quant.cpp FindClosestColor/CopyPaletteEntries):
  SolidColorYieldsBucketCenter, CollectOrderIsHighChildFirst,
  UnditheredArmMapsThroughHistogramTable, SerpentineDitherRegressionVector,
  ManyColorsReduceTo256, PalettizeDecodedBmpFillsIndices, + 6 edge/UBSAN tests.
- `surface_present_test` (#658) — PASS (present-area regression).
- No source edits made (everything was already 1:1); counts above reflect the
  pre-existing, now-verified binaries.

## Out-of-scope blocker (NOT mine, NOT touched)

A full link of any test target currently fails to BUILD in `src/sim/object_lifecycle3.cpp:245`
(`rebindParentMesh` called with 2 args; header @0x43ea48 now declares 3 args
node/prototype/meshName). This is another wave's in-progress edit in `src/sim/`,
outside my edit allowlist. It blocks the link step only — my three files compile and
the pre-built verified test binaries pass. Left untouched per the wave rules.
