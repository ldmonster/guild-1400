# Wave-H1 1:1 Hardening — chunk sim_02

Owner: wave-H1 hardening agent. MCP (IDA Pro, gilde.exe) live. Every provenanced function
in the 22 chunk files was decompiled + disasm-diffed line-for-line against the binary per
/tmp/guild_harden/brief.md. Constants confirmed via get_bytes/get_global_value; every
float->int site checked against disasm (ConvertX @0x5c6b08 = round-toward-zero / truncate).

## Files in chunk (22)
character_recon2_cmds, character_recon4_avatar, character_recon4_flags,
character_recon5_morph, character_recon5_spawn, character_recon5_transport,
character_recon_tavern, character_render, character_render2, character_render3,
character_render4, character_render5, character_social, character_state,
character_universe, charaction, charaction_brawl, charaction_misc, charaction_motion,
charaction_npcaction_recon, charaction_npcaction_recon2, charaction_steps2.

## Per-group reports
- sim_02_recon2_cmds.md      — character_recon2_cmds (13 fns)
- sim_02_recon4_5.md         — recon4_avatar, recon4_flags, recon5_spawn
- sim_02_morph_transport.md  — recon5_morph, recon5_transport, recon_tavern, social, universe
- sim_02_render_render2.md   — character_render, character_render2
- sim_02_render345.md        — character_render3, render4, render5
- sim_02_charaction_state.md — charaction (queue ops), character_state
- sim_02_misc_brawl.md       — charaction_misc, charaction_brawl
- sim_02_motion.md           — charaction_motion
- sim_02_npcaction.md        — charaction_npcaction_recon, recon2
- sim_02_steps2.md           — charaction_steps2

## Rolled-up tally
- VERIFIED-1:1: ~155 functions
- FIXED (source diverged from binary): 26 fixes across 16 functions
- Goldens corrected to the binary: ~15
- BOUNDARY (render/data leaves, pointer-identity returns, non-portable div): ~20 documented

## Notable FIXED divergences (binary is truth; see group reports for addr+disasm evidence)
- SyncTurnState 0x531e60 — signed `>=10` (jge), not unsigned; `prev` re-reads same cell.
- SpawnAtBuildingEntrance 0x57c9a9 — pos/rot args were swapped.
- ApplyHeadVariant 0x57c548 — gate-fail returns (char)dword_62D080, not computed variant.
- ToggleAniPlayback 0x4047f0 — gate operand was +52(mesh), binary gates +296 && +112.
- ShowWithScale 0x401a24 — rotation ref axis is global flt_5CA2B0={0,0,1}, not caller vector.
- AttachTransport 0x402e40 — stores real object pointer (mov [esi],ecx), not int sentinel.
- UpdateTransportAttach 0x402f70 — tolerance-return value, transObj source, miss-branch SetPosition.
- UpdateIdleSocial 0x405148 — erroneous +140 &= ~8 clear removed (binary clears only in not-eligible branch).
- UpdateNeedsDecay 0x4521cc — clamp ceiling is dbl_619110=1000.0; removed fabricated 1.0 clamp.
- ScreenToWorldRay 0x426850 — divides by rotated dir.y (fdiv [var_1C]), not focal length.
- PlayFootstepSound 0x40905c — no-terrain branch uses lowercase "normal_s" (aNormalS_0).
- AttachAni 0x404038 — added leading ConvertBackslashToSlash.
- QueryTerrainType 0x404650 — sign-extend terrain code.
- RegisterHandlers 0x40be30 — type-7 anim name is "bewegung/dreh_90_rechts" (ecx-preserved).
- InsertActionAfter / InsertActionVararg 0x404470/0x40c1e4 — node[48] latch keyed on coerced
  type byte == 0, not the shared step predicate.
- Command_Dispatcher 0x40a4d4 (RotateStep) — heading wrap/snap carried in float, not double
  (fst/fstp narrowings + single-precision snap compares).
- GroupInteractStep 0x4d19c0 — x87 float narrowings at each fst/fstp in the leave-probability roll.
- npcaction BeginActionState7/20, GuildJoinStep, BeginCarryGoods (0x4ca938/0x4cbc20/0x4d1d40/
  0x4e55bc) — flag-4 short-circuit returns record pointer; corrected actor/mixed-id offsets;
  4-slot id loop (not 3); zero second-clock minute/second.
- RestorePosFinish 0x4d1384 — free branch returns FreeHandlerEntry result, not the RNG draw.
- DrinkInit/RotateInterpolate/FadeAlpha — ConvertX truncation confirmed; FadeAlpha(0.5) golden
  fixed 128->127 (round-toward-zero, not round-to-nearest).

## Build status
All 22 chunk .cpp files + all edited test TUs compile clean (`g++ -std=c++17 -fsyntax-only
-I. -Isrc -Iinclude -Ishim`). No git commits made; only chunk files + their tests touched.

## Cross-chunk handoffs (NOT in this chunk — do not fix here)
- src/gui/widget_layout.cpp — UNTRACKED WIP from a gui chunk; `w.ld<i32>(...)` references a
  Widget member that doesn't exist. Because all of src/** links into one library, this blocks
  the full-library link (it does NOT affect per-TU compilation of sim_02 files). Gui-chunk owner
  must fix.
- actionqueue.cpp DispatchCurrent — binary gates on owner(+20) (0x40477c `if(!v1[5])`), not
  node->ready(+8). sim_02 builders keep writing +8 to preserve the current internal contract;
  aligning DispatchCurrent to the disasm should precede dropping those +8 writes.
- charaction.h — CreateUseGateAction's 2nd gate-object-name buffer and `*(mesh+533)!=1`
  gate-open condition are subsumed into the worldToTile render boundary; expanding touches the header.
