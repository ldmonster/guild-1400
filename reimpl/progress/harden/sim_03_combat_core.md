# Hardening sweep — sim_03 combat core

Chunk: `src/sim/combat.cpp`, `combat_action.cpp`, `combat_battle.cpp`, `combat_drivers.cpp`
(+ owned tests: `tests/unit/sim_combat_test.cpp`, `sim_combat_battle_test.cpp`,
`sim_combat_loop_test.cpp`). MCP-verified against gilde.exe (imagebase 0x400000).

## Counts
- VERIFIED-1:1: 17
- FIXED: 11 functions / constants (with golden updates where the golden was wrong)
- BOUNDARY: 3 (documented)

## combat.cpp
- **FIXED `kRandFloatScale` (CutsceneRng::RandFloat, RandInt) @0x4aca48 / 0x4ac9e8.**
  `flt_61D934` bytes = `00 01 00 38` == float 0x38000100 == **3.0518509447574615e-05**
  (float-rounded 1/32767, NOT 2^-15 == 0x38000000). get_bytes confirmed. Was
  `1.0f/32768.0f`. The disasm does `fild int; fmul flt_61D934` -> (double)int *
  (double)float. Updated source + golden `SimCombat.CutsceneRandFloatGolden` (the
  old golden encoded 2^-15; new goldens recomputed: 0.6552018793299794, ...).
  RandInt re-verified VERIFIED-1:1 (HIWORD(state)%0x7FFF % n; early-out returns n).
- VERIFIED-1:1 `Math_RandomModulo` @0x58b89c (RandNext()%n, 0 when n==0).
- VERIFIED-1:1 `FindUnitById` @0x486430 (linear scan, marker!=-1 && id match).
- VERIFIED-1:1 `RollMeleeDamage`/`ApplyMeleeHit` @0x4bfab4 (hp -= RandomModulo(60)+40).
- VERIFIED-1:1 `ApplyUnitDeath` @0x48c790 (ratio >= 0.05 -> survive; else alive=0;
  dbl_61B964 = 0x3FA999999999999A = 0.05 confirmed via get_bytes).
- VERIFIED-1:1 `DistanceXZ` @0x4864a0 (sqrt(dx*dx + 0 + dz*dz)).
- VERIFIED-1:1 `FindNearestEnemyUnit` @0x48ad54 (two identical class-2/else loops
  collapsed; weighted compare `ratio*d < best`, stores unweighted d — faithful).

## combat_action.cpp
- **FIXED `ResolveMeleeHit` @0x48c96c — banner/defeat inverted.** The weapon-366
  banner marker (`*(v1+8)=4`) fires ONLY when ApplyUnitDeath returned 0 (the unit
  SURVIVED the ratio gate) AND the swing landed (0x48cb3b). The reimpl set it when
  `killed` — opposite. Now `if (!killed && landed && weapon==366)`. Also added the
  attacker-alive gate (`*(v27+8)`) the original requires alongside victim-alive +
  inRange. Updated golden `ResolveMeleeBannerStabMarksDefeated` (the old golden used
  a FATAL ratio expecting killed+defeated; the binary requires a SURVIVING ratio).
- VERIFIED-1:1 `PerformAttackAction` @0x490a80 (slot gate, predicted-dmg scratch,
  melee/372/374 classification, hasHit delta, armed gates). Weapon consts
  372/374/366 confirmed.
- BOUNDARY: PerformAttackAction's shout-gate `Math_RandomModulo(10)` is GUARDED in
  the binary by an anim-op precondition (`!*(actor+296) || op.byte9 in {45,58}`);
  the rules model consumes it unconditionally (the +296 anim-op state is not in the
  rules tree). Documented; `PerformAttackConsumesShoutRoll` test reflects the model.

## combat_battle.cpp
- **FIXED `WeaponWeight` @0x485dc0.** The `v8` weight is a FLOAT slot; constants are
  float-precision (0.80000001, 0.69999999, 0.89999998, 0.30000001). Return
  float-rounded values. Updated golden `WeaponWeightTable` to compare against
  `(float)0.8` etc (old 1e-9 tol failed by ~1.2e-8).
- **FIXED `GetSoundRangeScale` @0x485dc0.** v6 = (float)(v7*v8) is a FLOAT multiply;
  reimpl did it in double. Now float intermediate, then * ComputeOutputRatio (double).
- **FIXED `ScoreUnitForRole` @0x48be60.** Cases 0,1,4,5 store through a FLOAT slot
  (v9/v11/v13) -> results are float-rounded; reimpl returned doubles. Now float-cast.
  0.40000001 (case 1) confirmed. Switch arms/mults verified.
- **FIXED `AssignUnitsToRoles` @0x48bfb0.** The chosen role index (v29) is STICKY
  across outer iterations: when no cumulative threshold covers the roll (v13 reaches
  6 -> break) v29 keeps its previous value. Reimpl reset to 5 each iteration. Now
  declared once before the loop. (First-iter no-cover is UB in the binary; seeded 5.)
  RandFloatScaled scale fixed (see below). Reassign roll short-circuit (role==0 skips
  RandFloat) preserved.
- **FIXED `RandomFloatScaled` scale @0x58b910.** `flt_62675C` bytes = `00 01 00 38`
  == float 0x38000100 (== float(1/32767), same as flt_61D934), and the multiply uses
  the FLOAT value (`fild; fmul flt_62675C`). Was double `1.0/32767.0`. Now float.
- **FIXED `BuildOrderForUnit` @0x48c24c.** The switch is entered ONLY when the unit
  is alive AND not captured (`*(a2+8) && actor[533]!=1`). Reimpl ran the switch for a
  DEAD unit (only gated `alive && captured`). Now `if (!alive || captured) None`.
  Role-0 ranged-target gate + class-2 threat-tile vs nearest verified.
- **FIXED `EvaluateAttack` @0x491688 (state 2 / LABEL_46).** Added the weaponClass==2
  short-circuit: `if (v87[88]==2 || (...roll...))` — class-2 weapons FIRE without
  rolling Math_RandomModulo(255), so the CRT LCG must NOT advance. New optional param
  `alwaysFires` (default false); `TickOrderSlot` passes `self.weaponClass==2`.
  Chance trunc (ConvertX, positive -> == (int)) and damage `(int)(rollSum*worth*0.01)`
  re-verified against goldens (=19, =144). dbl_61BBB4=0.01, dbl_61BBBC=900.0.
- **FIXED `AutoResolveBattle` @0x490014 + `SumStrength`.** (a) Strength is summed as
  an INT with a per-iteration ConvertX truncation (`v59 = (int)(scale + (double)v59)`),
  NOT a double sum truncated once — fixed SumStrength to int per-step. (b) The score
  formula: `Begin(atk) = atkSum + (u16)RandomModulo(30)` — there is NO outer u16
  truncation of the whole sum (the reimpl wrongly wrapped it in (u16)). Defender roll
  is FIRST, attacker SECOND; +128=attacker, +192=defender. Golden `AutoResolveGolden`
  still holds (single-unit case: 8 / 38, Defender).
- VERIFIED-1:1 `ComputeBalanceIndex` @0x48bba8 (mode 0..5 from flags+side, bucket via
  flt_61B8C4..D0 = 1.75/1.25/0.75/0.50 confirmed, defender flag-count override 1/0).
- VERIFIED-1:1 `EvaluateBattleOutcome` @0x48cf6c (count alive!=4 & !captured;
  d<=0->atk, a<=0->def; allFlagsAttacker override). +128=atk, +192=def confirmed.
- VERIFIED-1:1 `TickOrderSlot` skeleton (slot guards; attack rule via EvaluateAttack).
- BOUNDARY: TickOrderSlot non-attack states (move/march/capture/...) are modeled as a
  generic phase advance; the op78/80/81/85 + heightmap/path emission is deferred
  (command sink). The slot-clear extra `&& v87` (has-objectdef) condition is omitted.

## combat_drivers.cpp
- **FIXED `ComputeOrderSlot` @0x4883f8 and `DrivePursuitTargets` @0x48c4f7.** Both
  compute `(int)(ComputeOutputRatio * 100.0)`. dbl_61B264 / dbl_61B8EC = 100.0
  (double) confirmed via get_bytes. The original truncates the DOUBLE product via
  ConvertX (frndint RC=truncate-toward-zero, verified in disasm) with NO float store;
  the reimpl had a spurious `(float)` intermediate that can re-round (e.g.
  6.9999999->7.0) and change the truncated int. Now truncate the double directly.
- VERIFIED-1:1 `CountSurvivors`/`PickWinner`/`PickWinnerForced` @0x48e5b9/0x48e641/
  0x48e796 (alive!=4 & !captured; def<=atk->def; forced).
- VERIFIED-1:1 `ClassifyRankIndex` @0x48e945 (19->0,16->1,4->2,else prev).
- VERIFIED-1:1 `ValidateRoster` @0x49005b, `SumSideScore` @0x490146 (per-step int
  trunc — already correct), `AutoResolveWinner` @0x490232.
- VERIFIED-1:1 `DeploymentOfferAccepted`/`DeploymentAiDecision` @0x48e23a/0x48e371
  (RandomModulo(90)+10<=thr; force RandomModulo(5)<=force->hold; mode RandomModulo(4)
  -> 10/90/50). All consts confirmed (90/10/4/5/10/50/90).
- VERIFIED-1:1 `ResolveTeamRow` @0x48c166, `FindSelectedTarget` @0x488891 (bound 32),
  `FindFirstFreeFormationSlot`/`FormationModeOpcode` @0x489830 (slot0 fast-path; 1->342,
  2/3->344), `DrivePursuitTargets` attack/flee gate (scaled>=20 || RandomModulo(100)<=30).

## Tests
All owned + adjacent combat suites pass (unit + e2e):
sim_combat (293), sim_combat_battle (99), sim_combat_loop (71), combat_drivers (321),
sim_combat_strength/orders/escape/packets, combat_orders2/slots{,2..5}/recon_balance,
slice_combat, wire_combat{,2,bomb}, and the combat e2e suites — 0 failures.
(One pre-existing/unrelated failure `SetupResolveTeardownLifecycle` is in
`combat_loop.cpp::PickScenario`, outside this chunk.)

NOTE: a concurrent agent's in-progress edit to `src/gui/widget_layout.cpp` was
transiently breaking the full-library link during this sweep; the four combat
sources in this chunk each compile cleanly (verified via direct object-target builds).
