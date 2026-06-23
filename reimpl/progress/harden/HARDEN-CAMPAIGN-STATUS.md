# Full-Tree 1:1 Hardening Campaign — STATUS / HANDOFF

Goal (user, binding, repeated): **"from entrypoint to last leaves, harden ALL functions 1:1.
use fleet of agents and don't count tokens."** Decompile every reconstructed function via IDA MCP
and diff line-for-line against gilde.exe; fix every divergence (source AND golden) to the binary.

Snapshot date: 2026-06-16. Working dir: `/home/cnupt/work/reverse/guild-1400/reimpl`.
Git root is the PARENT (`/home/cnupt/work/reverse/guild-1400`); we live in `reimpl/`.

---

## ⚠️ CRITICAL LESSON — NEVER let agents run git

A wave-H1 agent ran `git stash` in the SHARED working tree. Because the entire reconstruction is
UNCOMMITTED (HEAD is the bare `c8e19e5` baseline; rule: never commit), the stash reverted ~499
tracked files to HEAD and froze an inconsistent snapshot. Recovery cost the whole session.

**Every hardening agent prompt MUST forbid: git (stash/checkout/clean/reset/add/commit), deleting
or recreating `build/`, and building the whole tree (archive races corrupt `libguild.a`).** Agents
build ONLY their own test targets: `cmake --build build --target <suite> -j`. The no-git brief at
`/tmp/guild_harden/brief.md` already encodes this — keep using it.

There is a `stash@{0}` ("WIP on init-repo: c8e19e5"). It has been FULLY applied during recovery
(entire `reimpl/` subtree restored from its tree). Do NOT `git stash pop`/`drop` it casually — it
is the only fallback snapshot. Recovery procedure that worked:
`cd <gitroot> && git checkout stash@{0} -- reimpl` (restores all tracked files to the snapshot),
then rebuild, then fix any residual golden skews 1:1.

---

## CURRENT BUILD/TEST STATE

- `cmake --build build` : **clean** (guild lib + all targets compile).
- Suite: **1541 / 1550 passing — 9 FAILING** (fix these FIRST next iteration). They appeared after
  the partial wave-H2 (render_06's real 1:1 fixes changed shared render behavior; downstream
  goldens need updating to the binary values — same pattern as the H1b fix fleet). Triage the
  non-render ones too:
  - `effect_script_test`, `effect_script_real_esc_e2e_test`, `sim_script_compiler_test`,
    `sim_script_compiler_e2e_test`  (the .esc/script VM cluster)
  - `material_seasonal_resolve_test`, `render_leaves9_test`, `render_scene_load_test`  (render — likely render_06 downstream)
  - `meister_mgmt_recon_test`  (AI)
  - `sim_command_apply6_test`  (command)
  Fix method: run each with `ctest -R <name> --output-on-failure`, decompile the relevant
  gilde.exe function, set the source/golden to the binary value (cite addr+evidence). The H1b
  fix-fleet pattern (one agent per cluster, no-git) worked perfectly for the prior 15.

---

## GUI FLOWS — COMPLETE (hardened 1:1 + wired entrypoint→leaves)

All three GUI flows are now 1:1-hardened end to end AND confirmed wired into the live tree;
`guild_run --play --frames 60` boots → drives the real `Menu_RunMainMenu` → renders → exit 0.
Suite 1550/1550 green.

- **GUI leaves** (`src/gui/**`): chunks gui_00–05 all hardened (form parser stride, window
  layout/resize, tooltips sign-extension, chart axis, StrChr-returns-last, richtext %-dispatch,
  netfile extension-strip, …).
- **Main menu + leaves** (play spine): flow_menu1 (native_main_menu/sdl_menu/menu_recon_*/
  choosecity_scene — menu-music RNG draw fixed) + flow_menu2 (sdl_*_screen char/options/credits/
  load + newgame_apply — talent byte[1..5] off-by-one fixed, disasm-confirmed @0x52da00).
- **Town view GUI**: flow_town1 (city_view3d/scene_view/camera/pick/map — camera pan clamp fixed)
  + flow_town2 (hud_*/ui_recon4/5/session_hud/panels/select — Surface_DrawText RGB packing,
  Lender i32-wrap; HUD confirmed driven per-frame by the live SDL loop).
- **In-house/workshop GUI**: flow_house1 (scene_interior/interact_building/dialog_* — SelectRoom
  not-found fix; building-enter→dialog wired) + flow_house2 (slice_bank/church/council/estate/
  market/personnel/production/tavern — opcode-68/56 wire-packet offsets fixed).
- **Wiring** (WIRE-GUI): the GUI was already comprehensively wired; added the char render-bridge
  install; verified the spine routes widget/HUD/tooltip/panel/menu hooks to real reconstructions.
- Consolidation fixes: gui_dialogs4 drag-global ODR (renamed g_d4_*), netfile itest/e2e goldens,
  newgame_diff talent golden + session_save browsePath golden (both binary-verified).

Also hardened this session (leaf subsystems, disjoint): io_00/01, ai_00/01, util_00/01,
audio_00, net_00, render_06, sim_12 + the fix fleets.

## METHODOLOGY (proven)

- Partition: `/tmp/guild_harden/chunks/<dir>_NN.chunk` — each is an explicit ~22-file list; one
  agent owns one chunk (disjoint, no collisions). **If /tmp was cleared, regenerate:**
  ```
  mkdir -p /tmp/guild_harden/chunks
  for d in sim render world gui play io ai util crt audio app drm net config compress mem script; do
    find src/$d -name '*.cpp' | sort | split -l 22 -d --additional-suffix=.chunk - /tmp/guild_harden/chunks/${d}_
  done
  ```
  (Exclude `shim_impl` — it's the real Vulkan/SDL backend, not 1:1-reconstructed.)
- Brief: `/tmp/guild_harden/brief.md` (full-tree harden, no-git). Re-create from this doc's rules
  if /tmp cleared.
- Agent prompt template: "read /tmp/guild_harden/brief.md (no-git rule at top) + CLAUDE.md; cat
  /tmp/guild_harden/chunks/<chunk>.chunk (your files); decompile+diff every provenance'd function;
  fix to binary; build ONLY your test targets; NEVER git; write progress/harden/<chunk>.md."
- Wave size ~12 agents. **Consolidate after EACH wave**: full `cmake --build build` + `ctest`,
  fix any cross-chunk breakage, THEN next wave. (Earlier waves hit session/rate limits at ~12-14
  concurrent — if limited, drop to ~8/wave.)

---

## CHUNK STATUS (57 chunks total; ~56 to harden)

DONE & APPLIED (fixes in the tree now):
- `render_06` — 22 fixes (SnowUpdateFlake full rewrite, TileIsUniform signed-byte, Convert24To16
  channel order, x87-80bit rounding in skeleton/sky/billboard, 2 inverted pose-driver gates, …).
- `sim_12` — verified VERIFIED-1:1, no edits needed (mostly wire_* glue).
- H1b fix-fleet — 15 corrupted goldens fixed 1:1 (charaction/combat/command_apply/ai/render/newgame).

ROLLED BACK by the git-stash recovery — fixes LOST, reports exist, MUST RE-RUN:
- `sim_00, sim_02, sim_03, sim_05, render_02, render_03, world_00, world_01, world_02`
  (their `progress/harden/<chunk>*.md` reports document the divergences they found — re-running is
  guided, but the source edits are gone; the source is back at the recovered baseline.)

NEVER RUN (pending):
- sim_01, sim_04, sim_07, sim_08, sim_09, sim_10, sim_11
- render_00, render_01, render_04, render_05, render_07, render_08
- world_03, world_04, world_05, world_06
- gui_00..gui_05, play_00..play_05
- io_00, io_01, ai_00, ai_01, util_00, util_01, crt_00, audio_00, app_00, drm_00, net_00,
  config_00, compress_00, mem_00, script_00

(Verify exact chunk count per dir: sim=13, render=9, world=7, gui=6, play=6, io=2, ai=2, util=2,
crt=1, audio=1, app=1, drm=1, net=1, config=1, compress=1, mem=1, script=1.)

---

## KEY 1:1 RULES THE AGENTS APPLY (recurring divergence classes found)

- **ConvertX @0x5c6b08 TRUNCATES toward zero** (RC=11 frndint); bare `fistp` rounds-to-nearest-
  even; `(int)` cast truncates. Float32 consts that are "just under" a fraction (e.g. 0.01f =
  0.00999999977) make products truncate one short — pin the truncated value.
- **x87 80-bit intermediates**: a product/quotient kept on the FPU stack stays 80-bit until the
  `fstp`; model with `double` (e.g. `(float)(a - dt)` not `a - (float)dt`).
- **Disasm is the reference of record** where Hex-Rays is wrong: collapsed `__usercall` register
  args, mislabeled strides (e.g. accuser stride 536 not 268), signed vs unsigned compares
  (`movsx`/`jge` = signed byte), `int*`/`__int16*` byte-offset traps.
- Person/family array `word_12CE910` stride **536**; building-type table `dword_13CE294` stride
  **589**; flag table `dword_13CE27C` stride **65**.
- Most SOURCE is already binary-faithful; the high-value finds are real divergences — verify, don't
  churn correct code (mark VERIFIED-1:1).

---

## NEXT ITERATION — ORDERED PLAN

1. **Fix the 9 failing tests** (small no-git fix fleet, one agent per cluster: esc/script-VM;
   render downstream goldens; meister; command_apply6). Get to GREEN (1550/1550).
2. **Resume hardening waves** over the PENDING chunks (and RE-RUN the rolled-back 9), ~12/wave,
   no-git, consolidate (build+ctest) after each wave; fix cross-chunk golden breakage each time.
3. Track which chunks are done by the presence of a non-stale `progress/harden/<chunk>.md` AND a
   green consolidation.
4. When all chunks are VERIFIED-1:1 and the suite is green, write the campaign summary + an
   INDEX.md row (orchestrator only).
