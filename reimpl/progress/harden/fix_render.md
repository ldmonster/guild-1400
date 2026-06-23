# FIX-RENDER — 3 failing render tests fixed 1:1

Agent: FIX-RENDER. All three assigned targets GREEN (plus related itest/e2e verified).
No git commands run. build/ not deleted/recreated. Only the assigned test targets built.

## 1. render_leaves9_test — `RenderLeaves9.ShiftAccumulateZeroUnchanged` (GOLDEN fixed)

- Function: `gilde.exe 0x5fa684 — VIBE_Math_ShiftAccumulate`.
- Evidence (disasm 0x5fa684):
  - `sub esi, esi` zeroes esi (the exponent OUTPUT register).
  - `or esi,eax / or esi,edx / or esi,ebp` then `jz locret_5FA6C4` — on the
    all-zero input the function jumps straight to `retn` with **esi == 0**.
  - `mov esi, edi` (copy the base exponent into the output) is at `loc_5FA6C2`,
    reached ONLY on the nonzero path.
- Therefore for all-zero input the exponent output is **0x0000**, NOT the input
  base 0x405E. The SOURCE (`src/render/render_leaves9.cpp` ShiftAccumulate) was
  already binary-correct (its comment explicitly documents this). The TEST golden
  was stale.
- Fix: `tests/unit/render_leaves9_test.cpp:220`
  `CHECK_EQ(expOut, (u16)0x405E)` → `CHECK_EQ(expOut, (u16)0x0000)` (with a comment
  citing 0x5fa684). hi/mid/ret (all 0) were already correct.

## 2. render_scene_load_test — `ScriptRun.LoadCompileRunMain` (NO code change — build artifact)

- Symptom: `Beep(result)` emitted arg 0 instead of 7 (`result = 7` not read back).
- Root cause: NOT a logic bug. `libguild.a` had a **truncated object file**
  (`session_save.cpp.o`) from a sibling agent's concurrent build, so the test
  linked stale/inconsistent code. Confirmed by instrumenting WriteVar/ReadVar:
  with a freshly-relinked library the VM correctly stores storage[0]=7 and reads
  it back (`CMD Beep args=1: 7`).
- Fix: removed the stale `CMakeFiles/guild.dir/src/play/session_save.cpp.o` and
  rebuilt the target. The script VM source (`src/sim/script_compiler.cpp`
  ReadVar/WriteVar @0x4445bc/0x4414d4) is correct as-is. Test now passes.
- Source left unchanged (debug prints added during investigation were removed; the
  transient `#include <cstdio>` was also removed — file restored to pristine logic).

## 3. material_seasonal_resolve_test — Set3/Set1 seasonal binding (SOURCE fixed)

- Regression from a sibling render-hardening agent's 1:1 fix to
  `render::IsFoliageMeshName` (`src/render/texture_set_table.cpp:57`), which now
  does a **byte-0 prefix compare** matching the binary.
- Binary evidence: `gilde.exe 0x506388 — VIBE_Object_HideFoliageDecor` calls
  `VIBE_Util_StrncmpN(a1, "pfl_"/4 | "vg_"/3 | "!vg_"/4)` where `a1` is the scene
  NODE name pointer at BYTE 0 — a BARE mesh name, no path. The sibling's byte-0
  `IsFoliageMeshName` is binary-correct (verified via decompile of 0x506388 +
  StrncmpN @0x5e9ee0).
- The bug: `RealTextureSource::BuildTableFor` passed its `key` — a full archive
  member PATH ("Vegetation/X/vg_TESTBAUM_KRONE.bgf") — to `IsFoliageMeshName`. A
  byte-0 compare against the path never matches "vg_", so the season was never
  applied (effSet stayed 0; Set3 didn't bind `_W`, Set1's appliedTextureSet was 0).
- Fix (`src/play/real_texture_source.cpp` BuildTableFor): apply the foliage gate to
  the **basename** of `key` (strip leading directories), reproducing the binary's
  byte-0 prefix test against the bare node name the engine actually compares. The
  TXS-path derivation (TxsFor, dir-preserving) and cache key are unchanged.
- This is 1:1: the engine sees a bare name at byte 0; the reimpl's key is a path,
  so the basename is the faithful equivalent of the binary's `a1`.

## Verification

```
cmake --build build -j --target material_seasonal_resolve_test render_leaves9_test render_scene_load_test
GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R '<the 3> | real_texture_source_itest | real_texture_source_e2e_test | object_mesh_render_itest | math_leaves_itest | math_leaves_e2e_test'
=> 100% tests passed, 0 failed out of 8
```

All three assigned tests pass, and the related integration/e2e tests that exercise
the same source files (real_texture_source, math/leaves) remain GREEN.
