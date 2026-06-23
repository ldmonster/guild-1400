# Hardening sweep — sim chunk 02: character_render.cpp + character_render2.cpp

MCP-verified each provenanced function against gilde.exe (decompile + disasm + get_bytes).
Disasm is the reference of record where Hex-Rays collapsed __usercall args.

Tests: character_render_test 78/78, character_render2_test 106/106,
character_render_e2e_test 21/21, character_render2_e2e_test 17/17,
character_render2_itest 15/15 — all pass after fixes.

## character_render.cpp

| addr | func | result |
|------|------|--------|
| 0x404860 | ComputeAttachOffset | VERIFIED-1:1 |
| 0x404964 | ApplyAttachOffset | VERIFIED-1:1 |
| 0x404998 | SetupAttachCamera | VERIFIED-1:1 |
| 0x4263fc | ApplyBoneTransform | VERIFIED-1:1 |
| 0x57c548 | ApplyHeadVariant | FIXED (return on gate-fail) + BOUNDARY (div-by-zero guard) |
| 0x5049d8 | FlagRedrawByMode | VERIFIED-1:1 |
| 0x43de74 | SetLowPoly | VERIFIED-1:1 |
| 0x489b8c | ResetStateIfMode3 | VERIFIED-1:1 |
| 0x48cf18 | HideAttachedActor | VERIFIED-1:1 (defensive null-guard noted) |
| 0x43da2c | Stop | VERIFIED-1:1 |
| 0x453358 | ResetAiTarget | VERIFIED-1:1 (SetGrayColorThunk render side-effect, no hook) |
| 0x4d4f78 | IsAccidentCandidate | VERIFIED-1:1 |
| 0x4047f0 | ToggleAniPlayback | FIXED (gate operand +112, not +52 mesh) |
| 0x43da5c | CmdKillCharacterAnimations | VERIFIED-1:1 (queue drain via hook model) |
| 0x43d524 | AttachItemToBone2 | VERIFIED-1:1 |

### FIXED — ApplyHeadVariant 0x57c548 (return value on gate-fail)
Disasm 0x57c59d–0x57c5a7: `mov eax, dword_62D080; cmp eax,[ecx+2Ch]; jz loc_57C5B6`;
on the not-equal fall-through the function returns `(char)dword_62D080`, NOT the computed
variant. `activeMeshId` models `dword_62D080` in our parameterization.
- before: `return variant;`
- after:  `return static_cast<u8>(activeMeshId);`
Golden fixed: `CharRenderHead.ModuloSmallCount` / `.MaskLargeCount` rewritten to observe the
variant via the gate-success SelectTextureSet path (the only place the variant is
externally observable) and to assert the `(u8)activeMeshId` fall-through return.

### BOUNDARY — ApplyHeadVariant div-by-zero
Disasm 0x57c591–0x57c593: `xor edx,edx; div ebx` with `ebx = headCount` and NO zero guard
when `headCount <= 4`. headCount is always ≥1 in the live tree (a head record always has
≥1 texture set), so the original never divides by zero. The reconstruction keeps an
`else if (headCount != 0)` guard (variant stays 0 for headCount==0) instead of reproducing
the `#DE` crash. Documented deviation; observable only in the never-occurring crash case.

### FIXED — ToggleAniPlayback 0x4047f0 (gate second operand)
Disasm: `cmp [eax+128h],0` (=+296 action) AND `mov esi,[eax+70h]; test esi` (=+112). The
gate's second operand is the +112 handle (esi), NOT the +52 mesh. The +52 mesh is read only
in the body (`[eax+34h]+1ECh+0F4h` = mesh+492+244) to derive the loop-flags target.
- before: `if (!a->action || !a->mesh) return;`
- after:  `if (!a->action || !a->handle112) return;`
Added `void* handle112; // +112` to `RenderActor` (character_render.h). Goldens updated in
character_render_test.cpp and character_render_e2e_test.cpp to set `handle112`.

### Constants verified via get_bytes
- dbl_6103AC @0x6103AC = 0xC02E000000000000 = **-15.0** (sit-height bump). ✓
- case-0 a3[1] = 1078530011 = 3.14159274f (pi); case-1/2 a3[0] = 1035122882 = 0.08726646f. ✓
- case-1 offset = 0xC1200000 (-10.0), 0x427C0000 (63.0). ✓
- Root translation reads mesh +84h/+88h/+8Ch (=132/136/140), all x87 fadd → offset vector
  (a4); no float→int truncation anywhere in the cluster. ✓

## character_render2.cpp

| addr | func | result |
|------|------|--------|
| 0x4014f8 | ResolveMeshSelf | VERIFIED-1:1 |
| 0x40194c | TouchMeshFrames | VERIFIED-1:1 |
| 0x42644c | RunMeshCallback | VERIFIED-1:1 (480/96 limit; accumulator via hook) |
| 0x42664c | UpdateSubMeshes | VERIFIED-1:1 (3 slots, stride 116; StrCmpNoCase==0) |
| 0x4266b0 | DrawSubMeshes | VERIFIED-1:1 |
| 0x4019cc | ApplyVisibilityState | VERIFIED-1:1 (terrain/transport/lowpoly = other funcs) |
| 0x401a24 | ShowWithScale | FIXED (reference axis is fixed global, not placeRot) |
| 0x4b5d98 | AttachFlag | VERIFIED-1:1 |
| 0x4b5e9c | ShowFlag | VERIFIED-1:1 |
| 0x4b5ef8 | RefreshFlagAnimation | VERIFIED-1:1 (+496 guard = hook concern) |
| 0x4b62c0 | CollectFlagNodes | VERIFIED-1:1 (append-on-match polarity; cap<32) |
| 0x4b62fc | RemoveFlagNodes | VERIFIED-1:1 (cap-32 read guard noted) |
| 0x4b63c0 | UpdateAllFlags | VERIFIED-1:1 (person iteration = caller span boundary) |

### FIXED — ShowWithScale 0x401a24 (rotation reference axis)
Disasm/decompile: the function rotates the **fixed global flt_5CA2B0** through the hierarchy
and measures the yaw against that SAME global — it never reads a caller-supplied rotation
vector:
```
RotateVectorByHierarchy(mesh, &flt_5CA2B0, v9);
v7 = VectorAngleBetween(&flt_5CA2B0, v9);
```
get_bytes @0x5CA2B0 = `00 00 00 00 / 00 00 00 00 / 00 00 80 3F` = **{0.0, 0.0, 1.0}** (+Z axis).
The earlier model threaded the `placeRot` parameter through both the rotate input and the
angle reference, which the binary never does.
- before: `float refAxis[3] = {placeRot[0],placeRot[1],placeRot[2]};` (rotate + angle ref)
- after:  `float refAxis[3] = {kRefAxisZ...};` (kRefAxisZ == {0,0,1}); `placeRot` now unused.
Added `const float kRefAxisZ[3] = {0,0,1};` constant. `placeRot` retained in the signature
for the call-site shape (binary's place input is a2+19; the rotation axis is the constant).
No float→int in this function — yaw stays float (binary `fstp dword`), `(float)` cast matches.
Golden fixed: `CharRender2_ShowWithScale.RunsFullPathWorldPosFromBoneChain` now sets the
rotate mock to return {0,0,1} so it is coincident with the new {0,0,1} reference (yaw==0,
as VectorAngleBetween returns 0.0 for coincident inputs). The e2e (call-count only) and
itest were unaffected.

### Texture-index / table strides verified
- AttachFlag/ShowFlag: `word_12CE910[268*cityId+42]` low byte − 62. word_12CE910 is a WORD
  array; index (268*city+42) in WORDs = byte (536*city+84). Person stride **536**, texByte
  at **+84**, bias **62**. Matches header (kPersonStride=536, texIndex=+84, kFlagTexBiasA=62). ✓
- RefreshFlagAnimation: `byte_12CE912[536*cityId]` = type byte at person+2 (12CE912 = base+2);
  flag kinds 5/6/7. Matches IsFlagKind / typeByte=+2. ✓
- AttachFlag +pi yaw matrix: `v12 = 1078530011` is the **2nd** Euler float → angles {0,pi,0}. ✓

### Notes / boundaries
- CollectFlagNodes calls inlined loc_5CB930 (a StrCmpNoCase variant) and appends when it is
  truthy; resolved (per RemoveFlagNodes' intent) as append-on-match, i.e. StrCmpNoCase==0.
  Kept the existing append-on-match model + cap<32 guard. VERIFIED.
- RemoveFlagNodes loops `v5 < v9[0]` (collected count) with no in-loop 32-cap; CollectFlagNodes
  only stores indices < 32, so the recon caps the detach loop at 32 to avoid OOB. Behaviour
  identical for the only reachable counts (≤32); defensive cap documented.
- UpdateAllFlags wraps the person loop in VIBE_Universe_SwitchActiveSlot(0)/restore and uses
  VIBE_Person_QueryBegin/IterNext + the dword_6498E4 sentinel. Modeled as a caller-supplied
  person span with a null lookup covering the sentinel (per the existing hook design). The
  per-person gate (flag kind, +90 bit0 clear) and dispatch order (Remove then Refresh) are 1:1.

## Handoffs
None — all edits confined to character_render.{h,cpp}, character_render2.{h,cpp} and their
unit/e2e/integration tests. No shared symbols changed; no files outside the chunk touched.
(Unrelated pre-existing truncated object `npcaction5.cpp.o` was rebuilt to relink libguild.a;
no source change.)

## Counts
- character_render.cpp: 15 functions — 13 VERIFIED-1:1, 2 FIXED (one with an added BOUNDARY note).
- character_render2.cpp: 13 functions — 12 VERIFIED-1:1, 1 FIXED.
- Total: 28 functions — 25 VERIFIED-1:1, 3 FIXED, 1 documented BOUNDARY.
