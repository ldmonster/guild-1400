# New-game commit — the chosen state reaches the world

Two halves, both landed 2026-06-11:

1. **Plumbing** — the native new-game chain no longer drops the player's
   choices: `play::NativeMenuResult` now carries the full `gui::NewGameParams`
   block (city, difficulty, history, identity, profession), populated through
   `EnterChooseCity` in `src/play/native_main_menu.cpp`.
2. **Commit** — `play::ApplyNewGameParams` (`src/play/newgame_apply.{h,cpp}`)
   reconstructs 1:1 what the original does with those values when the new game
   starts: `VIBE_Command_EnqueueInheritanceTransfer @0x5336f0` plus the
   adjacent new-single-player block of `VIBE_GameLogic_InitOrLoadSession
   @0x533a54` (clock seed + start gold).

---

## 1. The chain plumbing

- `gui::NewGameParams` (src/gui/newgame_setup.h) gained **`int difficulty`** —
  the `VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0` pick (0..4). It
  round-trips through `byte_12335BA` in the original and is copied into
  `dword_63C744` by the session bootstrap (`0x533ec5`); the start-gold formula
  reads it (`1250 - 250*d`, `0x5340e5`).
- `play::NativeMenuResult` (src/play/native_main_menu.h) gained
  **`gui::NewGameParams params`** (additive; `RunNativeMainMenu` signature
  unchanged — `apps/guild_run.cpp` compiles untouched).
- `EnterChooseCity` (src/play/native_main_menu.cpp) now threads everything:
  - city → `NewGame_ApplyCity(in, "stadt_"+displayName, CityFileBaseName(path))`
    — `cityName` is the `stadt_` map-marker id (`0x52ea33` prefix match),
    `cityFile` the `ReturnedString @0x122EE50` base name the session start
    formats into `"%s/%s.cty"` (`0x533d4b`);
  - difficulty → `in.difficulty = intro.variant` (`byte_12335BA`);
  - history flag, wizard identity (firstName/familyName/gender/faith/wappen)
    as before, profession (+variant) via the char-create step's
    `NewGame_ApplyProfession` + `NewGame_Commit` (arms `params.started` — the
    `BYTE2(dword_122F4A0) = 0x12` / `dword_122F528 = 1555` / `dword_631614 = 1`
    arm);
  - `result->params = cc.params` on confirm.

## 2. The commit — `play::ApplyNewGameParams(p, difficultyVariant, q, inputs)`

Reconstruction of `gilde.exe 0x5336f0` (called by InitOrLoadSession at
`0x533e03` right after the `.cty` world load; inherit variant `0x533b1b`),
driven through the REAL reconstructed machinery: the packet builders
(`sim::EnqueueTradeRequest @0x494548`, `EnqueueObjectInteraction @0x4944f0`,
`QueueRequestCoord27 @0x494878`, `EnqueueCmd15 @0x494604`,
`QueueRequestFlagBlob32 @0x494ab4`), the lockstep `sim::CommandQueue`
(standalone flush+exec == the `MarkSyncRangeStart/End` +
`while(!CheckSyncRangeAcked()) Amt_RefreshGuildState()` barrier at
`0x533755`/`0x5337e4`), the apply handlers (`ExCreatePersonA @0x496614`,
`ExCreatePersonB @0x496714`, batch 2's opcode-0x0F stock transfer) and the
live `sim::g_persons` array.

What it does (exact order of `0x5336f0`):

| step | original | effect |
|---|---|---|
| gate | `0x5336fb` `BYTE2(dword_122F4A0)` | `p.started` (the wappen-commit arm) |
| player | `0x533749` opcode-12: variant=`SHIBYTE(122F4A0)`, 16, wappen `dword_122F4A4`(=1342+i), gender `byte_122F4A8`, faith `byte_122F4A9`, `String`, `byte_122F4CA` | kind-6 Person: name@+0x30, gender@+9, faith@+12, wappen@+0x54, variant@+356 |
| parents | `0x5337a2`/`0x5337d8` opcode-11 ×2: kind 9, prof word `RandomModulo(4)+32`, gender 1 (mother) / 0 (father) | two kind-9 Persons |
| family | `0x533817` | father+0x40 = family name (StrNCopyPad 16) |
| | `0x533824..0x533886` | parents +0x50 (family word) / +0x54 (wappen) ← player; +0x68 = player id |
| | `0x533839`/`0x53388e` | mother+0x30 = femaleTable[`RandomModulo(0x70)`], father+0x30 = maleTable[`RandomModulo(0xBF)`]; `Office_ResolveStaffModel(marker)` per parent |
| | `0x5338c6..0x5338d7` | spouse links: mother+0x5C = father id, father+0x5C = mother id |
| | `0x5338da..0x5338e7` | player+0x60 = father id, +0x64 = mother id |
| | `0x5338fb..0x53391f` | player+0x1F0 = `unk_122F4F5` (avatar model name; "" on the automatic-ancestors path) |
| | `0x53392a` | player+0x18C = `dword_122F528` (1555 on the profession path `0x52da1a`; model+1468 on the dynasty path `0x52bb46`) |
| relations | `0x533938..0x5339ae` | six `QueueRequestCoord27(a,b,127)` pairs: (P,F)(F,P)(P,M)(M,P)(F,M)(M,F) |
| purses | `0x5339df`/`0x533a10` | `EnqueueCmd15(parent, -1, 32*RandomModulo(0x200)+16000, byte_6477A1)` — father then mother |
| mission | `0x533a15..0x533a4d` | if signed `byte_63C8F4 > -1`: `Mission_SlotRegister(player, LOBYTE(dword_122F4EC))` (world::MissionSlotRegister; cold image 0xFE → skipped) |
| talents | `0x533a29..0x533a37` | player bytes +0x80..+0x84 = `byte_122F4F0[0..4]` |

Talent derivation (`NewGameProfessionTalents`): the RunChooseHistory profession
tail `0x52d9ef..0x52da0e` — `byte_122F4F0[0..4]` = the first five bytes of
`VIBE_Building_LookupTypeRecordA(professionVariant)` (table `byte_649910`,
already reconstructed in sim/building2). Golden bytes verified against
`get_bytes @0x649916`: variant 1 → `69 69 BD 93 69`.

The caller's adjacent new-single-player block (`(word_63C740 & 1) && !(&4)`):

- `0x533b9d..0x533bba` — clock seed: `GameTime_Set(06:00)` →
  `QueueRequestFlagBlob32(3, &t)` (applied by `ExSysMessage` case 3).
- `0x533f35..0x533f8f` — start gold: base = `dword_63C7B4` cheat ? 75000 :
  `1250 - 250*difficulty`; for every person with alive byte +8 ≠ 0 and kind ∈
  {6,7}: `EnqueueCmd15(id, -1, MoneyMultiplyByRate(base, byte_6477A1), rate)`.
  Reuses `app::NewGameStartGoldBase` / `app::MoneyMultiplyByRate` (session_init).
  Skipped for network games (`p.network`), exactly the `0x533b85` gate.

### Faithful completions of existing modules

- `sim/person_create.cpp` `DefaultPersonCreate`: now stamps **rec[8] = 100**
  (`0x58da70: *(v14+8) = 100`, byte_12CE918) — every created person is alive;
  the start-gold scan and round scans key on it.
- `sim/command_apply5.cpp` `ExCreatePersonB` (opcode 12, `0x496714`): added the
  missing identity writes — **wappen dword (packet +31) → rec+0x54**
  (`0x49683c`) and **faith byte (packet +36) → rec+12** (`0x496845`).

### Named gaps (hooks with inert defaults — rule 8)

- `VIBE_Office_ResolveStaffModel @0x57c1e8` (parents' staff-model/avatar
  resolve) → `NewGameApplyHooks::ResolveStaffModel` (no-op default).
- The first-name string tables `dword_8C4320` (female, 112) / `dword_8C400C`
  (male, 191), loaded from text resources by `0x530e50` →
  `NewGameApplyHooks::ParentFirstName` (default ""); the `RandomModulo` draws
  themselves are made so the RNG stream matches.
- `VIBE_Person_CreateAndSpawn @0x58da70` internals (stats/avatar/family-record
  allocation RNG) — the pre-existing person_create hook deferral. Consequence:
  the `Person_GetFamilyRecord @0x58c408` gate inside `ExCreatePersonB`
  (kind 6/7/5 && rec[81] < 0) stays false (family table `word_13C3110` not
  reconstructed), so the family-record name block is skipped — the original's
  own behavior when no family record exists.
- ~~The opcode-27 (relationship) apply handler~~ — **CLOSED 2026-06-11** (see
  `progress/command-apply-relation.md`). The handler was already reconstructed
  as `sim::ExComputeObjectCoords` (gilde.exe `0x49818C`, jump-table slot 27 ==
  opcode 0x1B, batch 6) but resolved its persons in a modeled side table; it
  now scans the LIVE `sim::g_persons` array (`dword_12CE914`/`word_12CE910`/
  `dword_12CEB1C` are the record fields at +4/+0/+0x20C of the same array), an
  unknown-mode divergence was fixed (modes outside 0..4 ack without mutating,
  the `0x498640` fall-through), and `app::wiring` registers batch 6 on the
  owned queue. The six new-game packets (mode 0, delta 127 — `xor ecx,ecx` /
  `mov ebx,7Fh` at `0x533930..0x5339ae`) now genuinely saturate the primary
  768x768 relation grid to 127 for all six directed player/father/mother
  pairs; verified in `newgame_apply_test` (RelationPacketsApplyThroughBatch6 /
  ...NoOpWithoutBatch6) and the guarded `newgame_apply_e2e_test` over the real
  AUGSBURG load (23 checks).
- `VIBE_Object_ResetState @0x538400` at function entry clears the CALLER's
  4608-byte scratch (`Light_SetGrayColorThunk(0, 4608)`) — no record effect,
  not performed.
- `dword_122F4EC` (the RunChooseHistoryDialog mission id) and `byte_63C8F4`
  (mission mode, cold image 0xFE) arrive as `NewGameApplyInputs` until the
  mission-dialog flow is wired.

### Wiring status (rule 13)

`RunNativeMainMenu` returns the filled `params` today (guild_run untouched).
The session-side call — invoke `ApplyNewGameParams(result.params,
result.params.difficulty, sessionQueue)` after `io::LoadWorld` in the city
session — is the wave-2 handoff (sdl_session is owned by another agent this
wave); `tests/e2e/newgame_apply_e2e_test.cpp` already exercises exactly that
sequence (real `.cty` load → commit) end to end.

## Tests

- `tests/unit/newgame_apply_test.cpp` — 7 tests / 104 checks: golden talent
  bytes (TypeRecordA vectors vs `get_bytes`), the armed gate, the full commit's
  record image (player + both parents, every offset in the table above), RNG
  determinism under `crt::Srand`, the start-gold scan (kind/alive filter) and
  formula (difficulty / cheat / network-skip), the mission-slot gate.
- `tests/integration/newgame_result_itest.cpp` — 2 tests / 23 checks:
  scripted full chain (menu → 2D-fallback city → difficulty → history → wizard
  → char-create) asset-free; asserts **every** `NewGameParams` field on
  `NativeMenuResult`; a cancelled chain leaves `started` unarmed.
- `tests/e2e/newgame_apply_e2e_test.cpp` — 1 test / 16 checks (guarded on the
  real install): real `AUGSBURG.cty` load → commit into the live populated
  `sim::g_persons`, identity + links + start gold verified.
- `tests/e2e/playable_flow_e2e_test.cpp` TEST A extended (+12 checks, guarded):
  the real-asset scripted flow asserts the carried params (the asserts the
  file's header had anticipated).

Full suite after the change: **1390/1390** (portable Debug, GUILD_BACKEND=OFF);
the guarded e2e suites pass against `europe_guild_1400_original`.
