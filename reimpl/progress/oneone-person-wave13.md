# Wave-13 1:1 AUDIT — persons + lighting + meshes in the city view (W13-PERSON)

MCP DOWN: this is the MCP-free part of the 1:1 comparison — cross-check each
reconstructed function on the live person/lighting/mesh flow against in-tree
evidence (provenance comments + progress docs), PIN its recovered 1:1 values with
golden tests, and produce a confidence map. No source behavior was changed; only
golden TEST additions (constant-value pins the earlier suites used but never
asserted) plus this report. Build/ stays green.

## SEGMENT FILES AUDITED
- `src/sim/character_factory.{h,cpp}`
- `src/play/session_persons3d.{h,cpp}` (wave-3 module; the SpawnAtBuildingEntrance
  dummy resolution itself lives in city_view3d — a bind site I cannot edit)
- `src/render/object_light_shade.{h,cpp}`, `scene_lights.{h,cpp}`,
  `mesh_normals.{h,cpp}`, `anim_normals.{h,cpp}`, `env_map_walk.{h,cpp}`,
  `skeleton_pose_driver.{h,cpp}`

## INVENTORY (function -> gilde.exe address; all carry provenance — no RED FLAGS)

| Reconstruction | Address | Module | Provenance comment |
|---|---|---|---|
| CreateFromModel | 0x402d10 | character_factory | yes |
| CreateMesh | 0x4029c4 | character_factory | yes (block-by-block 0x4029ec..0x402c66) |
| DecomposeModelName | 0x402a4c | character_factory | yes |
| AllocSlot | 0x402254 | (REUSED from character_path.cpp) | yes — not redefined |
| FinalizeVertexShadeSoftware | 0x5c8218 (sw arm) | object_light_shade | yes |
| FinalizeVertexShadeLuma | 0x5c8218 (hw arm) | object_light_shade | yes |
| ApplyMaterialVertexShade | 0x5c7f04 | object_light_shade | yes |
| PointLightDiffuse | 0x5c7804 | object_light_shade | yes |
| LightMeshVertices (world-normal) | 0x5c8218 + 0x5c6f90 | object_light_shade | yes |
| LightMeshVertices (bone-matrix sun) | 0x5c6f90 @0x5c7129 | object_light_shade | yes |
| TransformVertexLightingNormal | 0x5c6f90 @0x5c71b5 | object_light_shade | yes |
| LightAffectsObject | 0x5c80a0 | scene_lights | yes |
| CollectObjectLights / CullForObject | 0x5c8218 (collect head) | scene_lights | yes |
| GenerateVertexNormals | 0x5D1A6C | mesh_normals | yes |
| BindInstanceNormals / Flatten | instVert+0x48 wiring | mesh_normals | yes (4 reader sites cited) |
| CalculateAnimNormals / QuantizeNormalByte | 0x5d0020 | anim_normals | yes |
| ComputeEnvMapVertexUvs | 0x5c9054 | env_map_walk | yes |
| UpdateSkeletonPose | 0x5cd1d8 | skeleton_pose_driver | yes (block-by-block) |
| WireSessionPersons3D / Update / Rebind / Unwire | session entry (over 0x57c8f0 path) | session_persons3d | yes |

## INTERNAL CONSISTENCY CHECK (source vs provenance/doc) — NO DRIFT FOUND

Verified the recovered constants in the SOURCE match the get_bytes values in the
progress docs (computed each LE float from its byte pattern):

| Constant | source value | bit pattern | doc (get_bytes) | match |
|---|---|---|---|---|
| kVertexLightAmbient (flt_64A074) | 200.0 | 0x43480000 | 200.0 | ✓ |
| kSunVertexIntensityScale (flt_628C30) | 0.009999999776482582 | 0x3c23d70a | 0.01 | ✓ |
| kSunNormalYScale (flt_628C24) | 20.0 | 0x41a00000 | 20.0 | ✓ |
| kRampIndexScale (flt_628C28/50) | -1023.0 | 0xc47fc000 | -1023.0 | ✓ |
| kLightIntensityScale (flt_628C4C) | 10.0 | 0x41200000 | 10.0 | ✓ |
| kMaterialShadeScale (flt_628C70) | 1/256 | 0x3b800000 | 0x3b800000 | ✓ |
| kLumaR/G/B | 0.30/0.59/0.11 | — | 0.30/0.59/0.11 | ✓ |
| kInitialScale | 1.0 | 0x3F800000 | 0x3F800000 | ✓ |
| kNodeAnimRate | 1.8 | 0x3FE66666 | 0x3FE66666 | ✓ |
| kNodeWord72 | 0x3000000 | — | 50331648 | ✓ |
| per-poly sun scale (flt_628C2C, NOT in my segment) | 0.001 | 0x3a83126f | 0.001 | ✓ (documented) |

Offsets cross-checked: base +304 / prefix +368 / model +5 / scale +416 (factory);
node flags 529/530/531/535/536/72/492 (factory); the cull's mixed-unit operands
(L.range linear +144 -> +484, O.cullRadius squared +484, linear distance) — all
agree with character-factory.md / scene-lights-wave8.md. The sun type tag 7 and the
0x10 sun-disable bit agree. The earlier note in anim-skeleton-pose-driver.md about
the +109-vs-+110 leg selector is RESOLVED in the source (leg selector + loop guards
read +109 mode&2; service gate + settle bit read +110) and is consistent with the
disasm finding documented there. NO contradictory constant/table/control-flow drift
was found, so NO source edit was required.

## GOLDEN PINS ADDED THIS WAVE (TEST additions only; values from the docs)

- `tests/unit/object_light_shade_test.cpp` (+6 tests, +19 checks):
  ConstAmbientSeed200, ConstSunVertexScale001Bits (0.01 bit-exact AND distinct from
  the per-poly 0.001 — pins the "per-vertex 0.01 vs per-poly 0.001" distinction),
  ConstSunNormalYScale20, ConstRampIndexAndIntensity, ConstLumaWeightsAndScales,
  ConstFalloffIndexDomain. (105 checks total, 0 failures.)
- `tests/unit/scene_lights_test.cpp` (+3 tests, +13 checks): ConstSunTypeIs7,
  ConstSunDisableBitIs0x10 (only 0x10 disables, every other bit ignored),
  ConstMixedUnitCullQuirk (the linear-range + squared-bound vs linear-distance
  operand quirk pinned). (51 checks, 0 failures.)
- `tests/unit/character_factory_test.cpp` (+4 tests, +19 checks):
  ConstNameFieldOffsets (+304/+368/+5/+416), ConstLiteralValues
  (1.0f/1.8f bit-exact, 0x3000000), ConstNodeFlagOffsets,
  ConstCreatureRatNodeFlagClears (the RATTE branch's +529 0x08/0x04 clears the
  success-path test omitted). (610 checks, 0 failures.)

No existing golden was altered; all pre-existing checks remain byte-identical.

## CONFIDENCE MAP

GOLDEN-PINNED (constants + control flow tested -> high 1:1 confidence):
- object_light_shade: ALL kernels + both LightMeshVertices overloads + the normal
  transform. Constants now pinned bit-exact. The falloff LUT (flt_1405110 ==
  BuildFalloffLUT) is pinned in `light_band_test.cpp` (dependency, not my file).
- scene_lights: LightAffectsObject + CollectObjectLights; cull arms, the sun-first
  break, the mixed-unit quirk, type/flag constants all pinned.
- mesh_normals: GenerateVertexNormals (unweighted unit-face-normal average, cube
  golden) + the +0x48 instance binding (both engine conventions, 4 reader sites).
- anim_normals: CalculateAnimNormals (face/vertex normals + 3-frame bbox union) +
  QuantizeNormalByte (1->255, -1->0, 0->127).
- env_map_walk: ComputeEnvMapVertexUvs (skinned + non-skinned, gate, golden
  non-identity reflection, the 0.5 / 2/255 / -128 constants).
- character_factory: CreateMesh/CreateFromModel/DecomposeModelName — name split,
  all field/flag writes, creature probe (incl. rat node-flag clears), preload clip
  names, model-load failure, offsets + literals pinned.
- skeleton_pose_driver: UpdateSkeletonPose driver control flow — early-out, palette
  rebuild, reverse/forward/clamp ladders, bone iteration order, boundary prune +
  veg relight, forced-tick suppression, morph terminal free vs latch.
- session_persons3d (over the 0x57c8f0 path): the EINGANG>TUER entrance-dummy
  resolution, case-insensitive compare, composed dummy world seat
  (PointThroughBoneChain composition), +0x16C/+0x170 anchor columns, zero spawn
  rotation (dword_577A78), the 0x4f8e60 record gate, Wire/Update/Rebind/Unwire
  contract — all pinned in session_persons3d_test (the resolver/seat logic is in
  city_view3d, a bind site; its pins live in this suite, which passes).

UNDER-VERIFIED: none remaining in this segment after the additions above.

NEEDS-LIVE-MCP (1:1 fidelity NOT confirmable from in-tree evidence; decompile
targets for the binary-diff when MCP returns):
- `VIBE_Anim_UpdateSkeletonPose` @0x5cd1d8 — the driver control flow is golden but
  the LEAF math is routed through hooks (Object_SetPosition 0x5af38c,
  SampleBoneTranslation 0x5cc920, InterpolateBoneFrame 0x5cbc10, ComputeBoneMatrices
  0x5cc0d0, the attachment lerp @0x5cdae5, the morph Catmull-Rom per-axis blend
  0x5cdc6d..0x5ce8f4). The per-keyframe phase-rate fold (track +96 / morph rate) is
  host-supplied — a live-MCP pass should confirm the exact sign*rate*elapsed fold and
  the inner-ladder termination bound (currently a defensive frameCount*4+8 guard).
- `VIBE_Character_CreateMesh` leaves — Object_AttachToUniverseNode 0x5b3e30 (WIRED to
  object_lifecycle10 but its sub-leaves stay inert), IndexFromPointer 0x426724,
  QueryTerrainType 0x404650, Destroy 0x402120: hook'd; confirm the exact call
  arguments/order against the decompile when MCP returns.
- `VIBE_Light_BuildObjectCache` two-pass walk @0x5c8218 — the cull/collect payload is
  pinned but the actual VIBE_SceneGraph_WalkAndInvoke 0x5ac738 traversal +
  PointToBoneLocalSpace 0x5c8c40 reduction is modelled (world-delta for the
  single-bone city object); a live diff should confirm the bone-local transform is a
  no-op for the city/universe case.
- `VIBE_Mesh_ComputeVertexLighting` env-map @0x5c9054 — the walk is pinned; the
  reflective-material gate (texRec +104 & 1), the bone 3x3 source
  (ComputeBoneWorldMatrix 0x5c8fac) and the active skin keyframe
  (FindHighestPriorityLayer 0x5d0e84) are caller-supplied; confirm via MCP.

## STATUS
COMPLETE for the MCP-free pass. 13 golden tests / 51 checks added across 3 suites;
all 8 segment suites green (object_light_shade 105, scene_lights 51, mesh_normals
145, anim_normals 45, env_map_walk 37, character_factory 610, skeleton_pose_driver
39, session_persons3d 70 — 0 failures). No source behavior changed; no drift found
that required a fix.
