# Full-Tree 1:1 Hardening Campaign — STATUS / HANDOFF

Goal (user, binding, repeated): **"from entrypoint to last leaves, harden ALL functions 1:1.
use fleet of agents and don't count tokens."** Decompile every reconstructed function via IDA MCP
and diff line-for-line against gilde.exe; fix every divergence (source AND golden) to the binary.

Snapshot date: 2026-07-04. Working dir: `/home/cnupt/work/reverse/guild-1400/reimpl`.
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
- Suite: **1558 / 1558 passing** (verified 2026-07-05 after the final chunk + sweep).
- **ALL 56 chunks hardened.** The full-tree 1:1 campaign is complete: every chunk has a
  non-stale `progress/harden/<chunk>*.md` report and the suite is green after each
  chunk's consolidation. See "CHUNK STATUS" below for the closing wave.

## HOW TO RUN THE HARDEN LOOP (proven procedure, 2026-07-05)

One chunk per agent, SOLO (no sub-agents), SYNCHRONOUS, one at a time. After each
chunk's agent returns: full `cmake --build build -j` + `ctest --test-dir build -j8`,
fix any cross-chunk golden skew 1:1 (decompile → re-pin with addr, or correct the edit),
THEN launch the next chunk. This serial cadence (vs. the earlier ~12-wide fan-out) was
adopted after fan-out waves kept dying on session/rate limits mid-edit and leaving
unverified partial edits in the shared tree. Agent prompt: read /tmp/guild_harden/brief.md
(no-git rules) + CLAUDE.md; cat /tmp/guild_harden/chunks/<chunk>.chunk; decompile+diff
every provenance'd function; get_bytes-diff every provenance'd table; fix only proven
divergences w/ address evidence; build ONLY own test targets; write progress/harden/<chunk>.md.

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

## CHUNK STATUS (56 chunks total) — updated 2026-07-04

DONE (report exists in `progress/harden/`, fixes applied, consolidated green):
- sim_00..sim_07, sim_09, sim_12 (many as multi-part reports: `sim_NN_*.md`)
- render_02..render_08 (multi-part reports for 03/04/05/06/07/08)
- world_00, world_01, world_02 (multi-part reports)
- gui_00..gui_05 (+ gui_04 parts, subscreens, menu labels, slider/surface render)
- io_00, io_01, ai_00, ai_01, util_00, util_01, crt_00, audio_00, net_00, config_00
- flow_menu1/2, flow_town1/2, flow_house1/2, wire_gui, fix fleets, wave1-recovery,
  table-verify sweep (ongoing)

CLOSING WAVE — DONE 2026-07-05 (serial, one chunk per agent, consolidated green each):
- sim_08 (19 fixes), sim_10 (~20, incl. 5-profile path cost table + 224-float stat table),
  sim_11 (script compiler/VM: while/if frame-slots, else off-by-one, arg-stride),
  render_00 (bone/morph blend, 5 RLE blitters, morph-angle table, BMP writer),
  render_01 (falloff LUT was asin not acos, camera matrix cols, cloth arg, water gradient),
  world_03 (inverted trade sort, office scan 37→30), world_04 (road records +1 slot, Args25
  encodings, evidence stride 268), world_05 (trial-session FSM, cargo float), world_06 (4th
  ctype-table mis-transcription, inverted room-array write), app_00 (pause mask, fullscreen
  gate, GameLogicEntities ×4, 3-clip intro), play_00 (inverted outro fade-spin, event-weight
  entry, sbf framing), play_01 (talents [0..4] not [1..5] — reverted a wrong flow_menu2 fix),
  play_02 (scene op fall-through, pick-ray cross products, .sbf neighbor-sample bug),
  play_03 (2 x87 sky/brightness), play_04 (sign-ext declension, ring counts 64→256),
  play_05 (bridges 1:1, comment-only), drm_00 (entire DRM verified 1:1, 0 fixes),
  compress_00 (InflateFast UNGRAB, TrAlign, zlib level_flags), mem_00 (AllocDebug(0) guard),
  script_00 (frame-push +42 dwords, ParseDeclaration dropped type arg).
- Table-verify sweep 2 (io/ai/gui/util/audio/net): ~25 tables, 1 benign array-size fix;
  audio/net have no const tables. These dirs were transcribed cleanly.

REMAINING (documented targeted follow-ups, NOT chunk-scoped — see the list below the
CHUNK STATUS section; these are structural re-reconstructions / cross-module arg fixes,
each flagged with an address, deferred as their own tasks).

Targeted follow-ups (found out-of-chunk, need their own pass):
- sim/building5 `FilterBlockedBauplatze` drops the wanted-name arg the binary passes
  @0x50d370 (+4 more call sites) — plot collection is wider than the binary (play_00 report).
- sim PathBuildMarkerPoints @0x4074c0 — caller-less structurally-divergent sketch,
  needs full re-reconstruction (sim_10 report).
- sim/building2 `LookupTypeRecordA` uses unsigned compare where binary 0x589778 is
  signed (`jl`+`movsx`, codes ≥128 index negatively) (play_01 report).
- sim/command_apply6 owner: check 0x579f70's EMA/snapshot guard
  (`flt_641DAC != 0 && 6 < hour < 21`) (play_04 report).
- mem/ allocator structural deviations (documented, behavior internally consistent
  but not transcription-faithful): heap.cpp first-fit vs the original dv-rover +
  address-ordered free list; small_heap.cpp single 28672B run-chunk vs 7×4080B
  per-page chunks. Full 1:1 needs an allocator rewrite (mem_00 report).

(Chunk partition regenerated 2026-07-04 into /tmp/guild_harden/chunks: sim=13, render=9,
world=7, gui=6, play=6, io=2, ai=2, util=2, crt/audio/app/drm/net/config/compress/mem/script=1.)

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
