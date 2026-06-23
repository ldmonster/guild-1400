# character_recon5 — Transport / Morph / Spawn cluster

Status: DONE (reconstructed, tested headless). Wave: recon5.
Files: `src/sim/character_recon5_{transport,morph,spawn}.{h,cpp}` +
`tests/unit/character_recon5_{transport,morph,spawn}_test.cpp`.

A prior wave deferred these as "object/mesh-coupled". This wave reconstructs the
PURE control flow + position/interpolation/blend MATH 1:1, with the genuinely
engine-coupled leaves routed through INERT-DEFAULT hook structs.

## Reconstructed (addr — name)

| Addr | Name | File | Notes |
|------|------|------|-------|
| 0x402d3c | MoveToUniverse | transport | reparent + deflate-bit (f529 bit3) + re-inflate on global universe |
| 0x402e40 | AttachTransport | transport | local (0,0,-70)·affine attach point + flag fixups (f529/f530/f531) |
| 0x402f70 | UpdateTransportAttach | transport | 70-unit trailing, yaw snap (−(2π−angle)), height lerp 0.25, terrain step −5/+3 |
| 0x403708 | ReleaseMorphAni | morph | morph-handle release guard + clear |
| 0x403764 | CheckAniMorph | morph | morph state machine + key-blend (delta+next)/2 + 0.5 bias |
| 0x4015cc | FadeOutSlots | morph | per-slot ramp (now−start)/50, clamp[0,1]·255, endpoint visibility toggle |
| 0x50650c | PreloadSceneAnimations | spawn | type-row lookup + scene-object table walk (key/secondary match) |
| 0x57c744 | SpawnOfficeStaffActor | spawn | switch slot → resolve model → create → bone xform → 2-byte name copy → restore |
| 0x57c8f0 | SpawnAtBuildingEntrance | spawn | person/building query → which avatar → spawn → preload walk → visibility |

## Constants recovered (get_bytes)
- 0x6101E0 = 70.0 (kSeventy)  · 0x6101E4 = 6.2831853 (2π)
- 0x6101EC = −5.0 (height step thresh) · 0x6101F4 = 3.0 (deflate height bias)
- 0x6101FC = 0.25 (height lerp) · 0x610004 = 0.02 (fade rate 1/50)
- 0x61000C / 0x610014 = 255.0 (fade scale/full) · 0x6102F0 = 0.5 (morph half bias)
- 0x5CA2E0 = {0,0,0} pivot · dword_577A68 / dword_577A78 = all-zero spawn defaults

## Coupled leaves → INERT-DEFAULT hooks (not omitted; structure preserved)
- Object: MoveBetweenUniverses 0x5b51e0, SetPivotVector 0x5af490, SetPosition
  0x5af38c, SetWorldTranslation 0x5af50c, ChangeTransparency 0x5b2710,
  FindByHandle 0x5b7be4.
- Mesh/terrain: ResolveMesh 0x4013fc, Heightmap_WorldToTileWithHeight 0x5c6644,
  Floor_PickTileAtPoint 0x5c2ddc, Transform_PointThroughBoneChain 0x5c8b38.
- Anim stream: PruneExpiredAttachments 0x5d0d38, FindFreeMeshSlot 0x5cf114,
  ReleaseMeshData 0x5cfe30, LoadStreamToStock 0x5d3858, AttachToBone 0x5d0b64,
  CreateMorphAnim 0x5cf150, ComputeBoneDelta 0x5cba40, ComputeBoneMatrices 0x5cc0d0.
- Spawn: ResolveStaffModel 0x57c1e8, CreateFromModel 0x402d10,
  Universe_SwitchActiveSlot 0x5b4a24, Person/GameObject queries, PickWaitAnimation
  0x406344, PreloadAniSet 0x403c34, CollectByOwner 0x4b99ac.
- IndexFromPointer 0x426724, AngleToTargetSigned 0x5b6d1c, SetVisible 0x401894,
  TouchMeshFrames 0x40194c.

## Reused (not redefined — ODR)
- EnsureBuildingAvatar 0x505134 / EnsureObjectAvatar 0x505074 live in
  `character_recon4_avatar.{h,cpp}`; SpawnAtBuildingEntrance reaches them via
  EntranceHooks (no duplication).

## Omitted / deferred
- None omitted. Every function has extractable in-scope math/orchestration.
- The anim-attachment bit predicates (attachment[+109]&0x20 etc. in CheckAniMorph)
  read not-yet-reconstructed anim-record state; the surrounding control flow and
  handle bookkeeping are reconstructed, with those predicates gated behind the
  liveness the hooks supply when wired.

## Rule flags
- Rule 6 (non-pre-approved tech): NONE. No third-party deps introduced.

## Wiring notes (real callers)
- UpdateTransportAttach is called from the character render path (see
  `character_render2.cpp` comment at +292 guard). AttachTransport seeds ch+292.
- SpawnAtBuildingEntrance is reached from `command_apply3.cpp` (entry-spawn) and
  `combat_slots4.cpp`; npc_daily issues the RequestChrMoveToUniverse that
  MoveToUniverse ultimately services.
- FadeOutSlots is a per-frame sweep over the global fade table (dword_66F010..0D0).
- CheckAniMorph runs from the character anim tick after queue ready
  (`charaction_misc.cpp` comment).

## Tests
- 36 tests, 89 checks, 0 failures (headless, no backend, no assets).
- Prefixes: CharacterRecon5Transport / CharacterRecon5Morph / CharacterRecon5Spawn.
