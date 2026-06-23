# world_buildingflag_util_recon2 — small VIBE_World / VIBE_Universe leaves

Status: DONE (2 functions translated, 8 tests / 33 checks passing).

Cluster sweep: VIBE_Util / VIBE_Math / VIBE_Env / VIBE_Universe / VIBE_Object /
VIBE_Location / VIBE_Time / VIBE_Mesh / VIBE_Game / VIBE_Anim / VIBE_World /
VIBE_Property / VIBE_Sky / VIBE_Surface. The vast majority were already present in
the reconstruction (verified by grepping each provenance address against src/).
Only the two leaves below were genuinely missing AND faithfully reconstructable.

## Translated

| addr        | name                              | size  | notes |
|-------------|-----------------------------------|-------|-------|
| 0x583990    | VIBE_World_LookupBuildingTypeFlag | 0x5e  | classify scene-type kind byte vs static set @0x582F20 (dword_582908+0x618): `01 03 04 0B 0D 0E 12 13 15 16 1A 1B 1C 1F 21 00`. Reads `dword_13CE27C[65*type]` (SceneTypeDef::kind, signed-char). |
| 0x5b371c    | VIBE_Universe_ApplyHiddenToggle   | 0x2d  | WalkAndInvoke(ToggleHiddenState, mode 6, state) then return Light_RefreshAllObjects(1). Self-contained via hook ptrs. |

Files:
- src/sim/world_buildingflag_util_recon2.{h,cpp}
- tests/unit/world_buildingflag_util_recon2_test.cpp

## Wiring

- 0x583990 caller: VIBE_Building_ComputeWorkstationFillRatio (0x58fc8c) — NOT yet
  reconstructed (only a documentation comment in src/sim/building.cpp). Wiring
  PENDING until that caller lands. The kind-record base matches the existing
  `g_sceneTypes` / `SceneTypeDefAt` convention (building_production.h, stride 65).
- 0x5b371c caller: VIBE_Render_ApplyGfxSettings (0x56be58) — not reconstructed.
  Leaf exposes SetUniverseHiddenToggleHooks; callees (SceneGraph_WalkAndInvoke
  0x5ac738, Object_ToggleHiddenState 0x5b3698, Light_RefreshAllObjects 0x5c886c)
  exist elsewhere under their own signatures. Wiring PENDING.

## Deferred / skipped (with reason)

- 0x4159dc VIBE_Property_Set — despite the name this is a font text-RENDER routine
  (`_FONT`, glyph advance, VIBE_Animation_Basic/Advanced, VIBE_Velocity_Apply).
  Heavily coupled to font globals + draw callees; not a clean leaf. OMITTED to
  avoid an unfaithful stand-in (rule 8). VIBE_Property_Get (0x4152cc) already done.
- 0x5d3fe0 VIBE_Util_ParseDoubleString / 0x5d4174 VIBE_Util_StrToDouble — CRT
  strtod internals (80-bit extended precision, errno). SKIP (CRT/libc).
- 0x5d4200 VIBE_Util_GetErrnoPtr — CRT errno. SKIP.
- Thunks <12 bytes (SKIP per rule): 0x44ec98 StrCmpThunk, 0x44eca0
  StrCmpNoCaseThunk, 0x44f6a4 StrCmpNoCaseDerefThunk, 0x527ddc NullSub,
  0x5dcd36 StrCmpNoCase_Thunk, 0x5f821c NullThunk, 0x5f8224 NullStub,
  0x5f8230 ExitHandlerThunk, 0x5fa620 NullStub2.
- All other cluster functions checked: already present in src/.
