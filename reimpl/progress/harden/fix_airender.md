# Wave-H1b FIX-AIRENDER — 3 failing tests fixed 1:1

All three GREEN. In every case the SOURCE was binary-faithful and the GOLDEN test
encoded wrong behavior (consistent with the stash-accident note). Fixed the goldens
to the disasm-verified values; no source logic changed.

## 1. aiaction_recon_test — AiActionReconUpgrade.TierWeights
- Function: VIBE_AiAction_PlanGuildhallUpgrade @0x4757e8.
- Evidence (disasm):
  - 0x4758ce `mov ah,[esi+5Ch]` — level read into 8-bit `ah`.
  - 0x4758d1 `cmp ah,21h` / 0x4758d4 `jge` — SIGNED 8-bit compare (33).
  - 0x4759aa `cmp ah,42h` / 0x4759ad `jge` — SIGNED 8-bit compare (66).
  - returns: 0x4758da `mov al,32h`=50, 0x4759af `mov al,1Eh`=30, 0x4759b6 `mov al,0Ah`=10.
- So a high-bit byte (255 == i8 -1) takes the `< 33` path -> 50, NOT 10.
- Source `GuildhallUpgradeTierWeight(i8)` already correct.
- Fix: golden `GuildhallUpgradeTierWeight(255)` expectation 10 -> 50
  (`static_cast<i8>(255)`). tests/unit/aiaction_recon_test.cpp:48.

## 2. character_render3_test — CharRender3.ScreenToWorldRayGolden
- Function: VIBE_Character_ScreenToWorldRay @0x426850.
- Evidence (disasm):
  - 0x426874 `lea edx,[esp+var_20]` — outDir points at var_20/var_1C/var_18.
  - rotated dir stored: var_20=dir.x, var_1C=dir.y, var_18=dir.z (fstp [edx],[edx+4],[edx+8] @0x4268ec/ee/f1).
  - 0x4268f4 `fld arg_C` / 0x4268fb `fsub arg_8` / 0x4268ff `fdiv [var_1C]`
    => k = (zFar - zNear) / dir.y  (divisor is the ROTATED dir.y, NOT focal).
  - scaled = {var_20*k, var_1C*k, var_18*k}.
- Source `ScreenToWorldRay` already divides by dy (rotated dir.y). Correct.
- The golden used `/focal` (scaled = dir*(zFar-zNear)/256), which is wrong.
- Correct python golden (identity matrix, dir={240,-180,256} normalized):
  scaled = (-13.33333, 10.0, -14.22222); note scaled.y == zFar-zNear == 10.
- Fix: golden scaled[] values + comment. tests/unit/character_render3_test.cpp:564-567.

## 3. particle_render_wave6_test — ParticleRenderW6.GravityIntegrateStep
- Brief guessed a render_system_to_surface signature skew; that was incorrect — all
  render_system_to_surface tests already pass. The only failure was `p.vy`.
- Function: VIBE_Particle_UpdateGravity @0x42c42c.
- Evidence (disasm):
  - 0x42c653 `fild [var_40]` / 0x42c656 `fmul flt_611A68` — dt = (int)(now-birth)*kGrDt,
    kept in the 80-bit x87 ST register (no float round-trip).
  - 0x42c685 `fld st` duplicates dt; 0x42c687 `fmul flt_611A6C` (0.8) feeds the seed accum.
  - 0x42c69b `fsubr [ebx+eax+4]` — vy - dt with dt at full (un-rounded) precision.
  - 0x42c69f `fstp [ebx+eax+4]` — ONLY here is the RESULT rounded to float.
  => vy = (float)(vy - dt), dt is the double; NOT 1.0f - (float)dt.
- Source `UpdateGravity` (src/render/particle.cpp:350, not owned but verified correct)
  already does `p->vy = (float)(p->vy - dt)`.
- The golden computed `exp_vy = 1.0f - (float)dt`, rounding dt to 1.0f first -> 0.0,
  losing the precision the binary preserves. Correct value: (float)(1.0 - dt)
  = -4.7497451e-08.
- Fix: golden exp_vy + comment. tests/unit/particle_render_wave6_test.cpp:228.

## Build/run
- Built only my targets: aiaction_recon_test, character_render3_test, particle_render_wave6_test.
- `ctest -R ...` => 100% passed, 3/3.
- No git commands run. No source logic changed; only goldens (+ explanatory comments).
