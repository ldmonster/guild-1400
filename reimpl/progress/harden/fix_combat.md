# Wave-H1b FIX-COMBAT — sim_combat_loop_e2e_test

## Status: GREEN (1/1, 34 checks, 0 failures)

## Failing test
`tests/e2e/sim_combat_loop_e2e_test.cpp:170`
`SetupResolveTeardownLifecycle`:
`CHECK_EQ(static_cast<int>(sp.scenario), static_cast<int>(CombatScenario::Bergpass))`

The test called `PickScenario(b.modeFlags = kBattleAttack /*0x02*/, false, 0, 0, 0)`
and expected `Bergpass`. It got `None`.

## Root cause: the GOLDEN was wrong, not the source

`PickScenario` in `src/sim/combat_loop.cpp` is the scenario switch from
`VIBE_Combat_LoadScenarioAssets @ gilde.exe 0x489ba8`. I decompiled and disassembled
the branch at 0x489bcc to recover the exact bit tests:

```
0x489bc9  test dl, 1          ; modeFlags & 0x01  (raid)
0x489bcc  jz   loc_48A253
0x489bd2  test dl, 8          ; modeFlags & 0x08  (indoor)
0x489bd5  jz   loc_48A249     ; -> "Kampfszenario_BERGPASS.ed3"
0x489bdb  mov  eax, aKampfszenarioS   ; -> "Kampfszenario_STADT.ed3"
...
0x48A253  test dl, 4          ; modeFlags & 0x04  (defend)
0x48A256  jz   loc_48A262     ; -> fall through to economic-raid (person kind switch)
0x48A258  mov  eax, aKampfszenarioB   ; -> "Kampfszenario_BERGPASS.ed3"
```

Evidence (string refs from decompile of 0x489ba8):
- `aKampfszenarioB` @0x61b338 = "Kampfszenario_BERGPASS.ed3"
- `aKampfszenarioS` @0x61b320 = "Kampfszenario_STADT.ed3"

So the binary's scenario branches are exactly:
- `(flags & 0x01)`: `(flags & 0x08)` ? STADT : BERGPASS
- `(flags & 0x04)`: BERGPASS
- **bit 0x02 has NO scenario branch** — it falls through to the economic-raid
  person-kind switch, which for `(false,0,0,0)` yields `None`.

Project flag constants (`src/sim/combat_types.h:175-177`):
`kBattleRaid=0x01`, `kBattleAttack=0x02`, `kBattleDefend=0x04`.

`combat_loop.cpp::PickScenario` already encodes this 1:1: it branches on
`kBattleRaid` (0x01, STADT/BERGPASS via the 0x08 indoor bit) and `kBattleDefend`
(0x04, BERGPASS), and deliberately does NOT branch on 0x02 (a comment in the
source already documents this). **The source matches the binary.**

The test's golden was the divergence: it used `kBattleAttack` (0x02) and expected
Bergpass, but the binary maps Bergpass to bit 0x04 (`kBattleDefend`), not 0x02.

## Fix (test golden only — source untouched)
`tests/e2e/sim_combat_loop_e2e_test.cpp`:
- `b.modeFlags = kBattleAttack;` -> `b.modeFlags = kBattleDefend;` (0x04, the
  flag the binary actually maps to BERGPASS at 0x48a253), with a citing comment.
- Updated the two nearby comments to reflect the binary's `test dl, 4` -> BERGPASS.

Changing the flag to 0x04 is safe for the rest of the test: the roster-spawn and
command-sync paths in 0x489ba8 gate only on `(modeFlags & 1)` (0x489ef4 / 0x48a572),
which is clear in both 0x02 and 0x04; roster spawning (0x489f3c+) is unconditional.
`LoadScenarioAssets` in this test stubs assets, so the scene-name choice has no
side effect on the rest of the lifecycle (roster counts 2/1, outcome, payout,
teardown all unchanged and still pass).

## Build/test
- `cmake --build build -j --target sim_combat_loop_e2e_test`
- `cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R sim_combat_loop_e2e --output-on-failure`
- Result: 100% passed (both `SyntheticBattleToResolution` and
  `SetupResolveTeardownLifecycle`).

No git commands run. Source file `src/sim/combat_loop.cpp` not modified.
