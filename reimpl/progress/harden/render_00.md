# HARDEN render_00 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files under `src/render/` (agf_*/anim*/bgf_loader/bmp*/bone_*/building_fx/camera*).
Method: decompile/disasm every provenance address, `get_bytes` every `@0x…` table,
diff line-for-line, fix only proven divergences. All covering tests rebuilt + green:
**48 targets, 7485 checks, 0 failures** (see test list at the bottom).

Legend: **VERIFIED** = already faithful, untouched. **FIXED** = proven divergence
corrected (binary evidence cited). **MODELED** = parameterized/hook reconstruction
whose observable math was verified (documented representation choice).

## anim.cpp
- 0x5ca2fc `VIBE_Math_VectorLerp` — **FIXED**: each component is one x87
  fld/fsub/fmul/fadd chain with a single fstp (disasm 0x5ca301..0x5ca32a); was three
  float roundings, now `(float)(((double)b-a)*t+a)`.
- 0x5cbc10 `VIBE_Anim_InterpolateBoneFrame` — **FIXED** (behavioral): lead factor is
  `(float)(1.0 - (double)curPhase/keys[fromFrame].dur)` (fild/fdivp/fld1/fsubrp,
  fstp @0x5cbd0a) and the trailing factor divides by `keys[toFrame].dur` with
  numerator `(from==to ? targetPhase-curPhase : targetPhase)` (branch @0x5cbe08);
  the reimpl had a single `phaseNum/segLen` pair plus an invented `segLen>0` guard.
  `BoneKeyframe` gained the +0x04 `dur` field; z-deltas stay 80-bit through their
  multiplies (fst @0x5cbd32 / fmulp @0x5cbea5) — modeled with double. Test
  `render_mesh_test` updated (same goldens + new from<to case).

## agf_anim.cpp / agf_anim.h
- 0x5c9394 `VIBE_Anim_ComputeMorphWeights` — **FIXED** (behavioral): full transcription
  of both branches. Forward (flags&2==0): `wTo = (float)(inv*offset + v)`,
  `v = (float)(phase * (float)(1.0/frames[from].dur))`; ease (flags&4) selects
  tiny-clip `(cos(v*pi)+1)*0.5` (fc<=2 && from+1>=fc-1), trailing `cos((v+1)*pi/2)+1`
  (fc-1<=to), leading `cos(v*pi/2)` (from<=0), and NO shaping for interior segments
  (branch 0x5c9528→0x5c94d9). Reverse (flags&2) uses `frames[to].dur` and yields
  `wFrom = v - inv*offset`. All compares signed (jg/jle/jl). The old single formula
  `(cos((v+1)*pi)+1)*0.5` matched none of the binary's three, and the old [0,1]
  clamp does not exist in the binary — test pin updated (old `wTo==1.0` at
  phase=4*seg → new `wTo==4.0`), plus new ease-branch goldens.
- 0x5ca2fc inline `VectorLerp` — **FIXED**: same x87 double-intermediate modeling.
- `LoadAnimation` (0x5e450c wrapper), `SamplePosedMeshSeg` / `SamplePosedMesh` —
  **MODELED** composite samplers over the verified constituents (the engine's
  quantized-byte blend itself is 0x5c953c, owned by morph_blend_walk).

## anim_load.cpp
- 0x5e450c `VIBE_ModelIo_LoadBinaryAnimation` — **FIXED** (3 divergences):
  1. v102 (token-25 vertex count) resets to 0 at EVERY sub-iteration of the v98
     loop, and its SURVIVING value (overridden by v100 when >=0: `if (v100 >= 0)
     v102 = v100`) — not hdr.vertexCount — drives the post-load AABB + quantize
     loop counts. The flag361 subtract keeps hdr.vertexCount (hdr+320). Now exact.
  2. bbExtX multiplies the UNROUNDED 80-bit `max-min` (v69 kept on stack) by
     flt_62BD40; Y/Z use the float-rounded diffs (v89/v90). Now modeled.
  3. (verified) start/end clamps, duration diff + last-copy, root-delta encode,
     tolerance of the ±1e10 seeds, ConvertX truncation — all match.
  - **DOCUMENTED DEVIATION** (pre-existing, unchanged): the reimpl keeps raw float
    points (the binary stores per-vertex deltas vs frame 0, zeroes frame 0's
    points, quantizes to bytes at frame+180 and FREES the float array). The exact
    quantized-blend path lives in morph_blend_walk; the float-sampler consumers
    (person_render/wire glue) fold the base positions instead. Also: token-25
    inconsistency / expected-token mismatches are log-only in the binary (skipped).
- Constants @0x628E60/0x628E64, magic 0xABCD0001, stride 192/dur+4 — **VERIFIED**
  bit-exact.

## animation_playback.cpp (+ unowned stubs)
- 0x5cc920 `SampleBoneTranslation` — **FIXED** (precision set): lead stays 80-bit
  (never stored, disasm 0x5cc9cd..0x5cca24); trail IS rounded to float (fstp
  @0x5ccb4e); z-deltas stay 80-bit through their multiplies; rotation rows are
  six-term faddp chains with one store (0x5ccc17..0x5ccc60) — modeled with double;
  non-delta branch: x uses the unrounded quotient, y/z its float-rounded copy
  (v48 = v15). NOTE: when segFrom>=toFrame the binary reads UNINITIALIZED stack
  accumulators (no zero-init, unlike 0x5cbc10) — reimpl zero-init retained as the
  only deterministic reconstruction.
- 0x5cba40 `ComputeBoneDelta` — **FIXED**: 3x3 rows modeled with double (decompile
  v11/v12/v16 are 80-bit chains with single stores). Flow verified exact.
- 0x5d0edc `UpdateTrackBlendWeight` — **FIXED**: `(to-from)*t+from` is one 80-bit
  chain with a single fstp — modeled with double. Guards/unsigned time math verified.
- 0x5d0e84 `FindHighestPriorityLayer` — **VERIFIED** (k=2..0, strict `>` promote-to-
  double compare, active@rel+132/weight@rel+92 of the v3 cursor).
- 0x5d0dcc `AnimClearLoopFlags` / 0x5d0e10 `AnimSetLoopFlags` — **VERIFIED** (3×116
  walk, +132 name / +138 &~2 / |2*(a2&1), tick stamp at *(obj+24)+64).
- 0x5cd184 `FindFirstActiveBone` — **VERIFIED** (a1[87]/a1[81] guards, names at
  +64, 64-byte stride).
- 0x5d0f7c `SeekToFrame` — **VERIFIED** (clamps against +340, phase = dur-1 on the
  last segment, AdvanceFrameIndex(BYTE1(a2[27]), …, idx85, idx84, idx82),
  PropagateDirtyFlag(obj,1)). Note: AnimHeaderView.frameCount and .lastFrame both
  mirror the SAME binary slot (+340 = endFrame, default fc-1) — callers must fill
  both identically (documented).
- 0x5d3f10 `AnimStrCmp` — **VERIFIED** (word-wise scan resolves to unsigned byte
  order, ±1/0 contract). Object_Set* remain documented placeholders (real 0x5af38c
  translation exists in sim/object_lifecycle3.cpp — cross-module rewiring flagged
  for the owning agent, outside this chunk).

## animation_decode.cpp
- 0x5D7420 `FrameDataInterpolate` — **FIXED**: early-outs return the (possibly
  left-clipped) X register a1, not y. Modes/clip/transparency verified; the
  last-blit dimension globals (dword_64A1A2/word_64A1A6) are not surfaced (noted).
- RLE preamble (all four blitters) — **FIXED**: the first-row byte offset DEFAULTS
  to 50 (payload); the row table is read ONLY when the top is clipped
  (`dword_64AAC0 = 50; if (clipY0-y > 0) … = rowTable[top]`). Was rowTable[0].
- 0x5FC200 `FrameTableValidate` — **FIXED**: modes 2/3/5 advance the source cursor
  by `2*(u16)n` (`mov cx,[esi-4]; shl; add`), not `2*n`. Fill dword_1406940 verified
  duplicated halves (0x5d88dd `result | (result<<16)`), so the double-u16 write is
  exact. Masks are the word_1406944/dword_1406930 globals ✓.
- 0x5FBC10 `FrameTableNext` — **FIXED**: 16-bit source advances (modes 2/3/5) and
  the skip→pixels conversion uses an UNSIGNED shift (`shr`). X-clip semantics
  (odd-pixel unclipped in mode 2, xCur += 1 per PAIR) verified as-is.
- 0x5FBFD4 `FrameTableBounds` — **FIXED** (3): mode-2 darken masks are the
  IMMEDIATES 0x3DEF/0x3DEFBDEF here (not the globals); 16-bit source advances;
  the default (8bpp index→16) path DOUBLES the skip field (0x5fc0c6
  `mov ecx,[esi]; shl ecx,1; add edi,ecx`) — skip counts pixels in that path.
  (The unguarded `shr/dec/jnz` pair loops would spin ~2^32 times for n==1 in the
  binary — kept guarded, noted.)
- 0x5FBB24 `FrameTableIndex` — **FIXED** (rewritten): this blitter addresses an
  8bpp destination — dest = surface + y*stride + x with NO doubling anywhere
  (`v8 = a1 + a4; v15 = v9*dword_64A1C8 + v8`), between-row step = stride-width
  BYTES. The old version reused the 16bpp preamble (x and stride scaled ×2).
- 0x5D781C `FrameDataProcess` — **FIXED** (rewritten dispatch): global disable gate
  (byte_140694B → `st.disabled`), RLE X early-outs (`x > clipX1` / `x+width <
  clipX0`), and the colorDepth dispatch: 0 → HIBYTE(dword_1406947)?Bounds:Index
  (`st.index16Sel`), 1 → x<clipX0?Next:(x+width<=clipX1?Validate:Next), 2 → return
  1 with no blit, >2 → 0. The old code ignored depth entirely. Synthetic e2e frame
  updated to carry depth 1 (documented old→new in the test).
- 0x5D85B8 `AnimationBasic` — **VERIFIED** (count guard at +42, offset table +69,
  returns 1 regardless of Process result; error sprintf log-only).

## agf_postprocess.cpp (stages of 0x5d2348)
- 0x5caa4c `VectorWithinTolerance` — **FIXED**: differences stay 80-bit through
  fabs and the compare — modeled with double.
- Morph constants @0x6290CC — **FIXED** (table): the shipped doubles are
  DECIMAL-ROUNDED, 0xBFF921FB54442EEA = -1.570796326795 and 0x400921FB54442EEA =
  3.14159265359 (true ±π/2/π end …2D18). Header now bit-exact.
- STAGE 1 vertex dedup (0x5d2425..) — **VERIFIED** + **FIXED** gate: the dedup runs
  ONLY when the token-0x02 flag bit at parse-ctx+1 is clear (`if (!v145[1])`).
  BgfModel gained `parseFlag0/parseFlag1` (from 0x5e43cc), agf_loader captures
  them, PostProcessModel honors the gate.
- STAGE 2 morph bake — **VERIFIED** (double algebra, float stores, order exact).
- STAGE 3 material dedup/compact/reorder — **VERIFIED**; `MaterialsEqual` **FIXED**:
  the 224-byte memcmp compares DECODED flag fields; the token-0x0b handler
  @0x5e3eb4 drops BIT 7 of its byte (bits 0,1,2-5,6 → +194/+197/+198/+195), so
  b2's bit 7 is masked out of the equality; token-0x0c @0x5e3f54 keeps all 8 bits.

## agf_loader.cpp
- Dispatch tables @0x64A4F8/0x64A420/0x64A360/0x64A4C8/0x64A480/0x64A444 —
  **VERIFIED bit-exact** via get_bytes (all tokens, handlers, subtables, '('
  entries 0x5e3d28/0x5e42a0).
- 0x5E3BD0 ReadToken / 0x5E3C44 ParseBlock / 0x5E3C0C FindTokenHandler —
  **VERIFIED** (EOF→43, >0x3A→39, '('-break with open-handler call, no-handler →
  recurse-same-table, 64-entry scan cap).
- Leaf handlers (0x5e43f4/0x5e4414/0x5e43cc/0x5e43a8/0x5e43bc/0x5e42cc/0x5e42d8/
  0x5e4310/0x5e42a0/0x5e40c4/0x5e4348/0x5e40fc/0x5e4180/0x5e41e0/0x5e4200/0x5e4428
  and the 0x64A360 material field handlers) — **VERIFIED** offsets/order/semantics;
  0x5e43cc now stores the two flag bits (see above).
- 0x5D1B54 `ComputeBoundingExtents` — **FIXED** (rewritten to the binary's
  outputs): radius2 (+468) = max|v| (double sqrt vs float accumulator), radius
  (+472) OVERWRITTEN by the AABB diagonal, ±1e10 seeds with the binary's mixed
  compare directions, centroid = Σ(8 corners)·0.125 (flt_628FC0 verified = 0.125)
  in slot order. BgfBounds gained radius2/centroid. (The slack-slot corner writes
  over the raw Mesh record live in mesh_postprocess.cpp — same address, verified
  consistent.)
- 0x5D1A6C `ComputeVertexNormals` — **FIXED** (rewritten): per-poly NORMALIZED face
  normals via util::TriangleNormal (0x5cb824 normalizes!), per-vertex gather of
  incident face normals, util::VectorNormalize (bit-test zero guard) — the old
  version summed unnormalized cross products with an invented 1e-12 epsilon.
- `LoadAgfModel` finalize `max(vtxBase, vtxWrite)` — equals the binary's ctx+40
  write-count for every successful parse (differs only for truncated streams that
  already fail) — **VERIFIED-equivalent**, noted.

## anim_morph.cpp
- @0x628E60/0x628E64 (1/255, 255.0) — **VERIFIED bit-exact**.
- `MorphQuantizeByte` — **FIXED**: the whole (delta-min)*255/range chain stays on
  the FPU stack for ALL THREE axes (disasm 0x5cf9ae..0x5cf9d4: fld/fsub/fmul/fdiv/
  ConvertX+fistp, no intermediate store) — now one double expression, single
  truncation. Old code rounded `(delta-min)*255` to float first.
- Scaled ranges — **FIXED**: the X scale multiplies the UNROUNDED 80-bit diff
  (v61 = (v103-v106)*flt_628E60); Y/Z multiply the float-stored ranges.
- 0x5cf150 `CreateMorphAnim` — **VERIFIED** flow (record layout, +228 zero
  overwrite, min/max compare-select idiom, 4-slot bone match, alt-path a5 copies).
  The a4[47]/a5[45] blob copies stay **MODELED** (the struct doesn't carry the raw
  control blobs; documented in-code).

## anim_normals.cpp / anim_relight.cpp
- 0x5d0020 `CalculateAnimNormals` — **VERIFIED** math core (TriangleNormal faces,
  per-vertex gather + VectorNormalize, ±1e10 bbox seeds, (f-1,f,f+1) neighbor bbox
  merge in the tail). The dequantize-input / quantized-normal-byte output
  (frame+180/+184) representation is the same documented float-surface model as
  anim_load. Bias vector @0x5CBA30 verified = (1,1,1); dbl_628F04/628F0C = 0.5/255.
- `QuantizeNormalByte` — **FIXED**: removed invented 0/255 clamps (binary stores the
  LOW BYTE with no clamp); double chain after the float `n+1.0` store.
- `RelightPosedFrame` — **MODELED** glue (constituents owned elsewhere).

## anim_object.cpp
- 0x5cf114 `FindFreeMeshSlot` — **VERIFIED** (case-insensitive scan, empty→0).
- 0x5d3858 `LoadStreamToStock` — **FIXED** (behavioral): the stock lookup AND the
  record name use the a3/edx KEY string (see caller 0x403408: eax = full
  "character/%s/%s_%s.baf" path, edx = short "%s_%s" key; LoadBinaryAnimation
  StrNCopyPads its a2 into the record name). The old code used the built
  "animations/"+name path for both. Signature + hook + test updated.
- 0x5cec00 `FreeObjAnimData` — **VERIFIED**.
- 0x5cef14 `CreateObjectAnim` — **VERIFIED** (class 3/4 gate, last-frame fixup
  BEFORE ×3, pose snapshot, looped AdvanceFrameIndex args, frameStamp set even for
  other classes). The +52 slot is constant-0 (merged field noted); ctrl45's bit1
  originates from uninitialized alloc memory in the binary — parameterized.
- 0x5d0b64 `AttachToBone` — **FIXED**: flags +110/+109 PRESERVE existing bits
  ((old&0xA7)|0x10 @0x5d0c3b/0x5d0c4e; &~0x20 @0x5d0c4b) — the old code zeroed
  them; channels are reused across attach/prune cycles so stale bits matter (the
  +109&2 loop test). Rest verified (mesh-type gate, refcount, normals-build,
  weightCur from +361/+336, looped hiStop = dur-1).
- 0x5d0d38 `PruneExpiredAttachments` — **FIXED**: ReleaseMeshData decrements the
  refcount UNCONDITIONALLY (old code guarded `>0`); noted that the full
  budget/eviction path lives in anim_recon4_mesh_lru over the raw arena.

## anim_recon4_mesh_lru.cpp
- 0x5cfd24 `ComputeMeshMemorySize` — **VERIFIED** (first-frame probe, formula).
- 0x5cfdb8 `EvictMeshesForBudget` — **FIXED**: the eviction call passes a2 = 1
  (`mov edx, 1` @0x5cfe10) — unlink+free, not 0. LRU select (unsigned >=,
  refcount==0) verified.
- 0x5cfe30 `ReleaseMeshData` — **VERIFIED** (unconditional decrement, unlink
  next/prev fix-ups, C/A/B free order, budget loop with a2=1 retry).
- 0x5cbfc0 `AssignSubMeshBones` — **VERIFIED** (3 channels, 4 bone bytes 0xFF
  reset, 64-stride names, child list +508/+496, unbounded v4++ writes preserved).
- 0x5b2ef8 `FreeAttachedBuffers` — **VERIFIED** (4×384 sub-records, +620/+622
  transparency gate, +460 clear, conditional scene walk).
- 0x43fc38 `FreeObjAnimDataAndReset` — **VERIFIED** (dword_62D4E8/E4 get an
  UNINITIALIZED edx in the binary; modeled 0 documented).
- 0x5d3f10 `UtilStrCmp` — **VERIFIED**.
- 0x41dc74 `AnimationFlagsCompute` — **FIXED**: the binary guards `if (v7)` on the
  SUM st+off (nonzero whenever st!=0), so the +6/+10 read happens even for a zero
  table offset — removed the invented `if (kfOff)` skip.
- 0x5d367c `LoadObjectAnimation_ApplyTransform` — **VERIFIED** (fixup→xform→flags
  →×3→loop-setup; AdvanceFrameIndex args (b1, last, fc-1, 0, fc)).
- 0x41f5e0 `Shape_RegisterLoaded` — **FIXED** (return semantics): the tail stores
  dword_62D208 = v38, which stays at startIndex for single-frame shapes and
  advances by frames-1 for multi-frame — NOT startIndex+frames. Fill string
  @0x611284 verified = "...". Test pins updated (start+1→start, 1→0,
  start+3→start+2), documented old→new.

## animation_mesh.cpp
- 0x5D89BC `AnimationAdvanced` — **FIXED**: returns 1 on success / 0 on
  out-of-range (mode-3 swap + restore verified); the old -1/observed-mode returns
  were invented. Test pins updated.
- 0x5D9774 `AnimationGetPtr` — **VERIFIED** (128-byte stride, frameCount@+64 > 0,
  case-insensitive).
- 0x5D1020 / 0x5D1034 — **VERIFIED** ("*" prefix + a1 + a2 → resolve).
- 0x5D15FC `MeshBuildLodFileName` — **FIXED** (loop bug): after the v7==0 plain-copy
  fallback the binary only RE-TESTS (`goto LABEL_5` is taken only while v7>0); the
  old loop re-sprintf'd "%s_-1" over the plain name before testing.
- 0x5B4944 `MeshMarkAllFramesDirty` — **VERIFIED** (+100 texture / +104 sign gate /
  |0x80).
- 0x429070 / 0x5F5628 — **VERIFIED** (recursion + accumulate step; scene-size
  callee is hook-side).
- 0x4283AC `MeshAccumulateAabbRecursive` — **VERIFIED** fold directions (>=/<=,
  80-byte corner stride); the AssignMeshData/PropagateDirtyFlag side calls are
  hook-side (noted).
- 0x427820 `MeshTestAabbOverlapRecursive` — **FIXED** (2): the binary tests FIVE
  axes only — the `q.mn[1] < b.mx[1]` compare is ABSENT (evidence: the 5-term
  condition at the decompile's overlap if); children receive the same query/out
  record (was nullptr). The per-polygon Y-range fold over triangle vertices
  (ComputeAabbExtents walk) remains a **documented subset** of this AabbNode model
  (stated in-code; the reimpl folds the node box's extent instead).
- 0x5D20DC/0x5D2240 `.TXS` writer/reader — **VERIFIED** (magic 0x23F209AE, BE
  dwords rows-then-cols, 64-byte names; reader bounds are memory-safety).

## bgf_loader.cpp (fresh campaign fixes — verify only)
- 0x5F86FC `BgfFindChunkStart` — **VERIFIED** (magic 0xFAB50005, '-'/'+'/other
  token skip logic, seek size-4 rewind).
- 0x5F87B8 `LoadFastChunk` — **VERIFIED** (count order matCount/vtx/poly, +8 slack
  vertex pairs gated on count>0, objectFlags +468, poly scatter +0/8/16 //
  +4/12/20 // +44, texId -1, ≤254 byte matIndex, positional material name slots,
  raw 6 material bytes — decode equivalence established with the AGF path, dummy
  tail). No churn.

## bmp.cpp / bmp_avgcolor_misc_recon.cpp
- 0x5f0c10 `BmpReadHeaderInfo` — **VERIFIED** (planes==1, bpp 8/24, comp<=1,
  abs(height)).
- 0x5f1664 `BmpSaveIndexed` — **FIXED** (palette layout): the decompile's `v26`
  alias is `v27-2` and `v17 += 4` runs BEFORE the v26 writes, so entry i is the
  STANDARD quad pal[4i..4i+3] = {src[2], src[1], src[0], 0}; the old transcription
  put B two bytes BACK (corrupting every entry's B channel).
- 0x5f18f4 `BmpSave24Bit` — **FIXED**: dataOffset = 58 and the info block is
  written as 44 bytes (40-byte header + 4 zero pad; `WriteStream(v29, 44)`),
  making the old 54-offset/`3wh+58`-size pair internally inconsistent. NOTE: the
  binary's own loader reads 24-bit data at 0x36=54 — saver/loader are asymmetric
  IN THE ORIGINAL; the old roundtrip test was replaced with on-disk layout pins.
- 0x5f0ce4 `BmpLoadBuffer` — **VERIFIED** focused reconstruction (palette decode
  R=src[2].., data seek 4*npal+54, RLE8, 24-bit at 0x36 with in-place swap +
  bottom-up flip); flag modes beyond the two used are documented reductions.
- 0x5f1af4 `Bmp_GetAverageColor` — **FIXED**: the RGB accumulators are FLOATS
  (v28/v29/v30) — each 80-bit term `(i16)pal*count/total` is added and stored to
  float per iteration; old code accumulated in double. ConvertX truncation ✓.

## bone_sample.cpp / bone_palette.cpp
- 0x5cc850 `GetBonePosition` / 0x5ccea0 `GetBoneFramePose` — **VERIFIED** exact.
- 0x5cc0d0 `ComputeBoneMatrices` — **FIXED** (behavioral, both accumulation paths):
  NEW record: accT = weight*(lerp - refT) THEN accT += refT (the binary re-adds
  refT: v52 = accT+refT) → accT = refT + w*(lerp-refT); EXISTING record: delta =
  lerp - CURRENT accT (+68, not refT), accT += w*delta (80-bit chain, single
  store). Rows lerp/accumulate, 1/count averaging (first 3 of each 16-byte row),
  MatrixToEuler + name-matched child push verified. Test pins updated with
  addresses (head (4,5,6)→(5,6,7); dedupe 8→4; e2e (2,3,4)→(3,4,5)).

## building_fx.cpp
- Tables @0x6476FC/@0x64770C — **VERIFIED bit-exact** ((8,7,8,9)/(20,21,20,19)).
- 0x4b60a0 `SpawnChimneySmoke` — **VERIFIED** (type-gate 589-stride byte != 7,
  slot-scan conditions, +200 payload check, window compares (< start, >= end),
  spawn path order (load-then-position modeled via hook, documented), z/y/x
  ConvertX truncation, expire path rec+149). MODELED hooks (slot table, script VM).
- 0x504910 smoke arm — **VERIFIED** (QueryBegin(1,6), node+530 = (b&0xF3)|4,
  SpawnChimneySmoke per record; the octree/light/terrain tail is other modules).

## camera.cpp / camera_control.cpp
- 0x407428 `ProjectPoint` — **FIXED**: the products are STORED to float (v8/v10)
  before the +0.5/ConvertX truncation; the reciprocal 1.0/camera[4] stays 80-bit.
- 0x407488 `ProjectFramePoint` — **VERIFIED**.
- 0x5ACCD0 frustum build + 64-entry table — **VERIFIED** (plane stamps/sign
  pattern, eps = 0x361A6E4F bit-exact, popcount+bit-ordered plane copy, 26-dword
  entry stride, near/far rows, theta/phi = Atan2(look)/Atan2(cos,sin)).
- 0x5E9024 `ComputeOrbitRates` — **FIXED** (exact float-store sequence): pan uses
  true double DIVISIONS (`dx/width*sens`), the orbit X side uses the FLOAT-stored
  reciprocal v23 = (float)(1/width) for both v22 and v29, the Y side DIVIDES for
  v21 but uses v19 = (float)(1/(float)height) for v25; every intermediate
  (v22/v29/v28/v21/v25/v27) is a float store and the final v26/v31 products round
  once. Old code used shared double reciprocals throughout.

## Test targets run (all green)
render_mesh_test, agf_anim_{test,itest,e2e_test}, agf_loader_test,
agf_real_mesh_e2e_test, agf_postprocess_{test,itest,e2e_test},
animation_playback_{test,itest,e2e_test}, character_render5_itest,
render_shape_blit_{test,e2e_test}, session_hud_e2e_test, gui_surface_render_test,
anim_normals_{test,e2e_test}, anim_object_test, anim_recon4_mesh_lru_test,
weather_wave21_test, render_skeleton_{test,e2e_test},
render_skeleton_pose_{test,e2e_test}, bmp_avgcolor_misc_recon_test,
render_bmp_test, render_camera_{test,e2e_test}, camera_project_test,
camera_control_test, render_cull_{e2e_test,itest}, terrain_render2_test,
animation_mesh_{test,itest,e2e_test}, render_mesh_asset_test, building_fx_test,
effect_script_test, scene_main_loop_test, person_model_resolve_test,
wire_char_anim_e2e_test, texture_bin_test, render_surface_{test,e2e_test},
tile_textures_e2e_test — **48 targets, 7485 checks, 0 failures**
(GUILD_GAME_DIR honored; asset-dependent suites ran against the real game dir).

## Updated golden pins (old→new, all with binary evidence)
- agf_anim_test: wTo at phase=4*seg 1.0→4.0 (no clamp @0x5c9394).
- render_mesh_test: BoneKeyframe gained dur; call signature (curPhase,targetPhase).
- render_shape_blit_e2e_test: synthetic frame colorDepth 0→1 (depth dispatch
  @0x5D781C).
- anim_recon4_mesh_lru_test: Shape_RegisterLoaded start+1→start, 1→0,
  start+3→start+2 (dword_62D208 = v38 @0x41f5e0 tail).
- render_skeleton_pose_test/e2e: accT pins per the 0x5cc0d0 refT re-add /
  current-accT delta.
- animation_mesh_test: AnimationAdvanced returns 1/0 (@0x5D89BC).
- render_bmp_test: Save24 roundtrip → on-disk layout pins (dataOffset 58, 44-byte
  info write @0x5f18f4; the binary's loader reads at 54 — asymmetric by design).
