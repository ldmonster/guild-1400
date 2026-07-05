# HARDEN play_05 — 1:1 verification report

Chunk: `src/play/wire_npc_actions.cpp`, `wire_npc_movement.cpp`, `wire_render_bridge.cpp`,
`wire_scene_bridge.cpp`, `wire_terrain_bridge.cpp`, `world_digest.cpp`, `world_render.cpp`
(+ their owned headers). All are additive BRIDGE/HARNESS modules: they carry no direct
`// gilde.exe 0x…` function bodies of their own; their 1:1 content is **call-site claims**
(byte offsets, register-arg orders, gating conditions, constants) against named binary
addresses. Every such claim was re-derived from the IDA decompile/disasm/bytes.

## Per-file verdicts

### wire_npc_actions.cpp / .h — VERIFIED-1:1
Verified against `VIBE_NpcAction_DailyRoutineStep` @0x4e7e88 (full decompile):
- Person parallel columns off `word_12CE910` (stride 536, 768 slots, loop bound
  411648 = 768*536): `byte_12CEA74`=+356 activeA, `byte_12CEA75`=+357 activeB,
  `dword_12CEA7C`=+364 homeBld, `dword_12CEA80`=+368 workBld, `dword_12CEA94`=+388
  destBld, `dword_12CEAD8`=+456 turnBits, `dword_12CE914`=+4 personId. All match
  the RealPersonRow / RealSetTurnBits / RealHomeIsProduction reads.
- turnBits masks used by the director: test 0x1000 / set BYTE1|=0x10, test 0x800 /
  set BYTE1|=8 — pass through the setTurnBits hook unchanged.
- Command emitters:
  - `VIBE_Command_RequestBuildOp77` @0x4956c8, one arg (eax = personId) — EmitOp77 ✓.
  - `VIBE_Command_RequestChrMoveToUniverse` @0x494dd8 `(a1=person@eax, a2=universe@edx,
    name@ecx, a4=obj@ebx)` (decompiled); the Hex-Rays-lost `v11` at the first case-0
    call site proven by raw opcode `0x4e8143: mov ecx, offset aDummyEingang_2
    ("dummy_EINGANG")` — EmitChrMove arg mapping ✓, tags "dummy_EINGANG"/"dummy_TUER" ✓
    (`aDummyTuer_3` used with obj=-1 in the evening path) ✓.
  - `VIBE_Command_QueueRequestString47` @0x494d90: binary passes `byte_61F514`;
    `get_bytes @0x61F514` = 00 … (empty string). Reimpl passes `nullptr`;
    `HeStrCopy(nullptr)` writes a single NUL — byte-identical packet. ✓ (no fix needed).
  - `VIBE_Command_QueueRequestNamedObject53` @0x494f0c call shape
    `(person, universe, 0, obj, flag, name)` with names aGoToWork / aGoToWirtshaus /
    aGoHome and the `obj==-1 → (…, -1, 1, aGoHome)` variant — EmitNamedObject53 ✓.
  - `VIBE_Command_QueueRequestArgs25` @0x494810 call `(person, 456, 2048, 4, 4096)`
    (offset re-derived: 0x12CEAD8-0x12CE910 = 456) — EmitArgs25 pass-through +
    director constants (sim/npc_daily.cpp, out of chunk) match ✓.
- Provider-routed helpers: `FindCarryTargetForChar` 0x4e786c, `FindInteractionTarget`
  0x4e79c0, `PickClosestByWeight` 0x4e7c3c — names/addresses confirmed via lookup_funcs.
- Building-type kind read in the director: `*(589 * type + dword_13CE294)` — confirms
  the 589 stride annotation used across the chunk.
- Note (documented adaptation, unchanged): the binary's +364/+388 columns hold record
  POINTERS (`*(home+97)` mesh deref); the portable reimpl stores ids and resolves via
  BuildingFindById — declared in the header, behaviorally routed through hooks.

### wire_npc_movement.cpp / .h — FIXED (comment only)
- `VIBE_CharAction_WalkStep` @0x4093b0 decompiled: `++*(a1+248)` waypoint-index
  advance ✓; segment durations 20.0 base / 40.0 mounted (`*(v5+4)&8`) written to
  anim+92 ✓.
- `VIBE_CharAction_WalkUpdate` @0x40a0b8 decompiled: 225-master-tick morph delay
  (`v6 + 225 > dword_62D008`) ✓; playback speed anim+96 = mesh+416 × flt_6108F0..FC
  × tile factor 0.69999999 (or `dword_62D07C` when mesh+44==-1 && node+512) ×
  ramp mesh+420, keyed by TileAhead 6/11 and the cart bit ✓.
- Byte-verified constants: `flt_6108EC` = 0.01 ✓; `flt_6108F0..FC` =
  {2.2, 2.5, 1.7, 1.9} ✓.
- **FIXED**: header comment claimed the indoor duration scale is "x1.2"; `get_bytes
  @0x610814` = double **3.0** (and the WalkStep decompile multiplies the 20/40 duration
  by `dbl_610814` when the universe' indoor floor +172 is set). Comment corrected to
  x3.0 (dbl_610814). The implementation (sim/charaction_walk.cpp:40 `kIndoorDur = 3.0`)
  was already correct — no behavioral change anywhere.
- The +0x60..+0x74 record-pad motion fields are bridge-owned portable pad (declared
  stand-in, shared with play/input_command.h) — nothing to diff against the binary.

### wire_render_bridge.cpp / .h — VERIFIED-1:1
Verified against `VIBE_Character_ComputeAttachOffset` @0x404860 (decompile):
- After the 4-case attach-slot switch, `PointThroughBoneChainPivot(*(actor+52), a4, a4)`
  @0x4048c1 then `out += *(float*)(mesh+132/136/140)` @0x4048d1 (float idx 33/34/35) —
  HookPointThroughPivot / HookMeshRootTranslation exact ✓. Pivot leaf address
  0x5c8d0c confirmed.
- `VIBE_Character_ApplyHeadVariant` @0x57c548 (decompile): call at 0x57c5c5 is
  `SelectTextureSet(v3=mesh, v7=v4+244, 1, variant, v7)` gated on
  `dword_62D080 == *(node+44)` ✓ (header's call-shape comment exact).
- `VIBE_Object_SelectTextureSet` @0x5b3f54 (decompile): gates `a1 && a1+492 && a2`,
  short-circuit `return 1` when `variant == *(a2+381)`, range gate
  `variant >= *(rec+484) → return 0`, loop over `*(rec+480)` poly groups — matches the
  bridge's documented gate/sentinel model (real leaf in sim/object_lifecycle8, out of
  chunk) ✓.

### wire_scene_bridge.cpp / .h — FIXED (comment only)
Verified against `VIBE_SceneGraph_WalkAndInvoke` @0x5ac738 (decompile + raw disasm):
- child = node[127] (+508) ✓, sibling = node[124] (+496) ✓, sibling-chain terminator
  `*(next+528) & 1` returned-at-without-invoking ✓ (0x5ac81c..0x5ac825) — exactly the
  Counter::Reach semantics in WalkSceneTree.
- Type byte: raw opcode `0x5ac7ba mov eax,[esi+212h]; 0x5ac7c3 sar eax,18h` — the
  SIGNED byte at **+533** (HIBYTE of the dword at +530). The owned header already said
  +533; **FIXED** the .cpp accessor comment which loosely said "+530 type-byte".
  No code change (SceneDrawNode.nodeType was already documented +0x215/533).
- `VIBE_SceneGraph_TestNodeFlag` @0x5ac6d8: the 9-case type→mask-bit switch matches
  render::TestNodeFlag (scene_walk.cpp) case-for-case ✓.
- BeginUniverseFrame grounding: raw disasm 0x5b3a34 `mov ecx, offset
  VIBE_Render_ProcessSceneNode` (@0x5add1c confirmed), 0x5b3a4b root `off_649D64`,
  0x5b3a50 `push ebp(=0)` → userArg 0 ✓ (WalkSceneTree passes userArg=0).
- ProcessSceneNode @0x5add1c: per-poly stride 40 with the `*(p+20)` texture-ptr read
  and the `v19==0` (untextured) branch — supports the bridge's zeroed texSortId
  presentation for the software path ✓.
- Callback-result semantics: descend only when result nonzero and bit7 clear
  (`test bl,80h`); VtInvoke returns 1 ✓.

### wire_terrain_bridge.cpp / .h — VERIFIED-1:1
`VIBE_Render_BeginUniverseFrame` @0x5b3900 (decompile): `v4 = dword_64A028;
if (v4) { … VIBE_Floor_RenderTerrain(dword_64A028, a2); }` — the gate and the
(terrain, frameFlags) call shape the FrameHooks slot mirrors ✓.
`VIBE_Floor_RenderTerrain` @0x5bf22c confirmed via lookup_funcs.

### world_digest.cpp / .h — VERIFIED (harness; no binary counterpart)
Self-owned FNV-1a fold over reconstructed globals; the fold order is reimpl-defined
test infrastructure, not a reconstructed engine function. Address annotations
cross-checked where independently observable: `dword_13CE294` (building-type catalog,
stride 589) and `qword_13CE852` (game clock) both appear with those roles in the
0x4e7e88 decompile ✓. Remaining annotations belong to the owning modules
(world_03–06 / sim waves). No divergence.

### world_render.cpp / .h — VERIFIED (harness + verified walk-order claim)
DefaultPlacementHook / PlacementFromWorld are declared deterministic harness code (no
engine counterpart claimed). The one binary claim — scene-node emission in the
@0x5ac738 order (parent before children via childPtr, then the sibling chain, bit0
terminator ends a chain) — matches the WalkAndInvoke disasm verified above ✓. The
"+76 world pos / +396 frame matrix / +533 visibility-type" reads are owned by
play/object_transform (out of chunk), whose header already documents +533 as
HIBYTE(+530) consistent with the raw opcode.

## Out-of-chunk findings (NOT edited)
1. `src/sim/charaction_walk.h:187` — comment says the indoor segment-duration scale is
   "* 1.2 (dbl_610814)"; the binary value at 0x610814 is double **3.0** (get_bytes).
   The implementation (`charaction_walk.cpp:40 kIndoorDur = 3.0`) is already correct;
   only that header comment is wrong. (The distinct monolith-dispatcher 1.2 constant
   is dbl_61093C, correctly noted at charaction_walk.cpp:55.)

## Edits made (both comment-only, zero behavioral change)
- `src/play/wire_npc_movement.h`: "x1.2 indoors" → "x3.0 indoors (dbl_610814)".
  Evidence: get_bytes @0x610814 = 3.0; WalkStep @0x4093b0 `v42 * dbl_610814`.
- `src/play/wire_scene_bridge.cpp`: accessor comment "+530 type-byte" → "+533 type
  byte (HIBYTE of dword at +530)". Evidence: 0x5ac7ba `mov eax,[esi+212h]` +
  0x5ac7c3 `sar eax,18h`.

## Tests (built per-target; GUILD_GAME_DIR=$PWD/europe_guild_1400_original)
| target | result |
|---|---|
| wire_npc_actions_test | 14 checks, 0 failures |
| wire_npc_actions_itest | 17 checks, 0 failures |
| wire_npc_actions_e2e_test | 10 checks, 0 failures |
| wire_npc_movement_test | 37 checks, 0 failures |
| wire_npc_movement_itest | 13 checks, 0 failures |
| wire_npc_movement_e2e_test | 14 checks, 0 failures |
| wire_render_bridge_test | 17 checks, 0 failures |
| wire_render_bridge_itest | 10 checks, 0 failures |
| wire_render_bridge_e2e_test | 71 checks, 0 failures |
| wire_scene_bridge_test | 21 checks, 0 failures |
| wire_scene_bridge_itest | 19 checks, 0 failures |
| wire_scene_bridge_e2e_test | 14 checks, 0 failures |
| wire_terrain_bridge_test | 36 checks, 0 failures |
| wire_terrain_bridge_itest | 16 checks, 0 failures |
| wire_terrain_bridge_e2e_test | 15 checks, 0 failures |
| world_digest_test | 29 checks, 0 failures |
| world_digest_itest | 18 checks, 0 failures |
| world_digest_e2e_test | 4 checks, 0 failures |
| world_render_test | 32 checks, 0 failures |
| world_render_itest | 18 checks, 0 failures |
| world_render_e2e_test | 12 checks, 0 failures |

**Total: 21 targets, 437 checks, 0 failures.**
