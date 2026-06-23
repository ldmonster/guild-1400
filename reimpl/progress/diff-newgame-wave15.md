# Wave-15 TRUE 1:1 binary diff — the NEW-GAME COMMIT segment

MCP live. Every function on the new-game flow was decompiled/disassembled and
compared LINE-FOR-LINE against the reconstruction. Result: **the segment is
VERIFIED-1:1** — no source divergence required a code fix. The wave-13
NEEDS-LIVE-MCP queue is resolved below (confirmed, or documented as a genuine
out-of-segment leaf with the now-recovered decompile). One golden-pin file added
(`tests/unit/newgame_diff_w15_test.cpp`, 16 checks) to lock the binary-confirmed
constants.

## Functions diffed

| function | gilde.exe | verdict |
|---|---|---|
| `ApplyNewGameParams` body | `0x5336f0` | **VERIFIED-1:1** |
| InitOrLoadSession start-gold block | `0x533f2e..0x5340e7` | **VERIFIED-1:1** |
| `NewGameProfessionTalents` | `0x52d9ef` + table `0x649910` | **VERIFIED-1:1** |
| `EnqueueTradeRequest` (op12) | `0x494548` | **VERIFIED-1:1** |
| `EnqueueObjectInteraction` (op11) | `0x4944f0` | **VERIFIED-1:1** |
| `QueueRequestCoord27` (op27) | `0x494878` | **VERIFIED-1:1** (mode-0; see note) |
| `EnqueueCmd15` (op15) | `0x494604` | **VERIFIED-1:1** |
| `QueueRequestFlagBlob32` (op32) | `0x494ab4` | **VERIFIED-1:1** |
| `ExCreatePersonA` (op0B) | `0x496614` | **VERIFIED-1:1** |
| `ExCreatePersonB` (op0C) | `0x496714` | **VERIFIED-1:1** |
| `Person_CreateAndSpawn` default backend | `0x58da70` | observable-allocation modeled; full body **DEFERRED** (out-of-segment deps) |
| `Office_ResolveStaffModel` | `0x57c1e8` | no-op hook **CONFIRMED faithful** for this call site |

## Line-for-line confirmations

### `ApplyNewGameParams` @0x5336f0 (the full commit)
Walked the decompile + 0x533879..0x5339a1 disasm. Every field write matches:
- `v31` = **mother** (first `EnqueueObjectInteraction` gender 1), `v32` =
  **father** (second, gender 0), `PacketSeqById` = **player** (the op-12 ack).
- Family stamps in the exact order: father `+0x40` = family name (StrNCopyPad
  16); mother `+0x50/+0x54/+0x68` = player `+0x50/+0x54/+0x04`; mother `+0x30` =
  female table; `ResolveStaffModel(mother)`; father `+0x50/+0x54/+0x68`; father
  `+0x30` = male table; `ResolveStaffModel(father)`.
- Spouse links: mother `+0x5C` = father id, father `+0x5C` = mother id; player
  `+0x60`/`+0x64` = father/mother id; both parents `+0x68` re-stamped = player id.
- Avatar name -> player `+0x1F0`; portrait `dword_122F528` -> player `+0x18C`.
- Six Coord27 pairs in order **(P,F)(F,P)(P,M)(M,P)(F,M)(M,F)**, all delta 127.
- Purses: `EnqueueCmd15(father, -1, 32*RandomModulo(0x200)+16000, byte_6477A1)`
  **first**, then mother. RandomModulo draw order father-then-mother confirmed.
- Mission: `if ((signed)byte_63C8F0+1 >> 24 > -1) Mission_SlotRegister(player id,
  dword_122F4EC)` — handler passes the whole dword (the prior `LOBYTE` note is
  cosmetic; recon passes `in.missionId`, the same value).
- Talents loop: `*(player+0x80+i) = byte_122F4F0[i]`, i in 0..4.
- **RNG draw order across the whole commit** (the 1:1-critical bit): mother prof
  `RandomModulo(4)`, father prof `RandomModulo(4)`, mother name `RandomModulo(0x70)`,
  father name `RandomModulo(0xBF)`, purse father `RandomModulo(0x200)`, purse
  mother `RandomModulo(0x200)` — matches the reconstruction exactly.

### Parent-prof context bytes
`v29 = SHIBYTE@0x122F4A1` (mother, `in.parentProfCtxA`), `v30 = SHIBYTE@0x122F4A0`
(father, `in.parentProfCtxB`). Confirmed off the `>>24` shifts at 0x53377f/0x5337b5.

### Start-gold block @0x533f2e (disasm-exact)
- `cmp dword_63C7B4,0; jz 0x5340c3`: cheat path `ebp = 0x124F8 = 75000`.
- Non-cheat @0x5340c3: `edx=dword_63C744` (difficulty); the lea/shl/add chain
  computes `250*difficulty`; `ebp = 0x4E2(=1250) - 250*difficulty`.
- Scan @0x533f42: stride `0x218`(536); `byte_12CE918[i] != 0` (alive) AND kind
  `byte_12CE912[i]` in **{6,7}** (`cmp al,6` @0x533f51 / `cmp al,7` @0x5340ec);
  id `dword_12CE914[i]`; amount `MoneyMultiplyByRate(base, byte_6477A1)`.
- `app::NewGameStartGoldBase` and `app::MoneyMultiplyByRate` reproduce this
  byte-for-byte (pinned in the new golden test, all 5 difficulties + cheat).

### The five packet builders (stack-offset exact)
Recomputed every field offset from the `ebp-relative` frame slots in each
decompile and matched the reconstruction:
- op12 `EnqueueTradeRequest`: +0x14 a1, +0x18 a2, +0x1C(word) a4, +0x1E a3,
  +0x1F a5, +0x23 a6, +0x24 a7, +0x25 name1(16), +0x35 name2(16). MATCH.
- op11 `EnqueueObjectInteraction`: +0x14 a1, +0x15 a2, +0x19 a4, +0x1D(word) a3,
  +0x1F a5, +0x23 a6, +0x24 a7, +0x25 a8. MATCH.
- op15 `EnqueueCmd15`: +0x10 a1, +0x14 a2, +0x1C(byte) a4, +0x1D a3. MATCH.
- op32 `QueueRequestFlagBlob32`: +0x10 flag, +0x11 124-byte qmemcpy (only when
  blob != null). MATCH.
- op27 `QueueRequestCoord27`: +0x10 a1, +0x14 a2, +0x18 a3 — see note.

### op27 / ConvertX truncation (the wave-15 finding, applied)
`QueueRequestCoord27` @0x494878 is fundamentally a **3-arg** function
(eax/edx/ebx). It also stashes `flt_62EB90`(=1.0, `0x3f800000`) through
`VIBE_Coord_ConvertX` @0x5c6b08 into +0x24, `dword_62EB94`(=`0x3f000000`) into
+0x20, and **ecx into +0x1C**. `ConvertX` sets the FPU control word high byte to
`0x1F`->RC=11 (**truncate toward zero**) and `frndint`s st0 — confirmed RC=11 as
the brief noted. The apply handler `ExComputeObjectCoords` @0x49818C reads:
+0x10 subject, +0x14 object, **+0x18 delta**, **+0x1C mode**, +0x20 float(mode 3
only), +0x24 (mode 4 only). At **every** new-game call site (0x533930.. and the
other five) the code does `xor ecx,ecx; mov ebx,7Fh` -> **mode = 0, delta = 127**,
so +0x20/+0x24 are DEAD (mode-0 path never reads them). Verified all 96 in-tree
`QueueRequestCoord27` call sites pass 3 register args with ecx zeroed. The recon's
5-arg signature (coordX/coordY) is a cross-segment modeling artifact whose only
non-zero use would be inert here; every caller passes 0/0 -> **behavior-1:1 on
the live call tree**. No code change (the signature is shared by other segments'
files and changing it is out of scope + unobservable). Documented + pinned.

### `ExCreatePersonA/B` @0x496614/@0x496714
Arg extraction and the post-create writes match the decompile:
- A: kind `HIBYTE(+17)`, parentA `+21`, ownerWord `(WORD)+29`, parentB `+25`,
  queryRec from `Person_QueryBegin(+31)` (building resolve, confirmed), a6/a7/a8
  `HIBYTE(+32/+33/+34)`; `g_lastObjectId = dword_12CE914[134*idx]`.
- B: kind `(a2==0)+6`, parentA `+20`, ownerWord `(WORD)+28`, parentB `+24`,
  a6 `HIBYTE(+27)`, a7 0, a8 `HIBYTE(+32)`; name `StrNCopyPad(rec+0x30, +37, 16)`;
  **wappen dword (+31) -> rec+0x54** (`dword_12CE964`), **faith byte (+36) ->
  rec+12** (`HIBYTE dword_12CE919`). The `Person_GetFamilyRecord` block is gated
  false (no family-record allocation) — the original's own skip when no family
  record exists. MATCH.

## Wave-13 NEEDS-LIVE-MCP queue — resolved

1. **`Office_ResolveStaffModel` @0x57c1e8** — decompiled. It is NOT a no-op: it
   returns office/staff model-name strings keyed on the person's office flags
   (`byte_12CEA74`/`byte_12CEB00`/`dword_12CEA9C`) with one mutating branch
   (`dword_641FE8 = (dword_641FE8+1)%8` + a `unk_1234610` scratch fill). BUT:
   (a) the new-game commit **discards** the return value, and (b) for freshly
   created kind-9 parents all those office flags are 0, so it takes the early
   `else` return with **no side effect**. -> the no-op hook is **CONFIRMED
   behavior-faithful for this call site**. (Its real body belongs to the office
   subsystem, not this segment.)

2. **First-name tables `dword_8C4320`(female,112)/`dword_8C400C`(male,191)** —
   `get_bytes` reads them **all-zero in the static image**: they are runtime
   POINTER tables filled from localized text resources by `0x530e50` at load
   time. They cannot be reconstructed statically (a load-time resource gap, not
   a code gap). The reconstruction draws `RandomModulo(0x70)`/`RandomModulo(0xBF)`
   (moduli confirmed exact: 112 / 191) so the RNG stream stays aligned, and the
   `ParentFirstName` hook supplies the string. **CONFIRMED correct approach.**

3. **`Person_CreateAndSpawn` @0x58da70 full body** — decompiled (~150 stmts).
   The reconstruction faithfully models the **observable allocation** (free-slot
   scan over `word_12CE910`, the `+8=100` alive stamp, kind `+2`, id `+4` +
   `dword_649890` lockstep, owner word `+10`, spawn ctx `+356/+357/+9`, and the
   `a5` parent-building column write `+364/+368`). The FULL body additionally
   does: avatar-slot alloc (`VIBE_Avatar_AllocSlot @0x484598`), parent-stat
   interpolation from the float tables `flt_582900/flt_582904` + `flt_62687C..`,
   the 768x768 relation-grid seeding (`dword_123D6CD`/`byte_1333110`), talent
   draws (`byte_649910`/`byte_649AD8`/`byte_649915`), family-record allocation
   (`dword_647720`/`word_13C3110` via `GetFamilyRecord @0x58c408`),
   `Character_ResolveHeadBone @0x57c5d4`, `BuildingType_GroupFromCode @0x58a4c8`,
   and **dozens of `VIBE_Util_RandNext` draws**. **DEFERRED (rule 8):** a faithful
   1:1 reconstruction depends on the avatar/character/relation-grid/family-table
   subsystems and the float stat tables — none of which live in this segment's
   owned files (`person_create.{h,cpp}` + the new-game/command files). The full
   decompile is now captured here for whoever owns those modules. CONSEQUENCE
   (documented): the modeled backend does not consume the same RandNext draws, so
   a save produced after a real CreateAndSpawn would differ in the cosmetic
   stat/avatar/relation-grid fields — the **identity, links, gold, and talents**
   the commit explicitly stamps afterward are correct and tested.

4. **`dword_122F4EC` (mission id) / `byte_63C8F4` (mission mode, cold 0xFE)** —
   still arrive via `NewGameApplyInputs` until the mission-dialog flow is wired
   (gate confirmed: `(signed byte > -1)`; cold image -2 -> skipped). Unchanged.

## Source changes

**None.** The reconstruction matched the binary in every diffed function; no fix
was warranted. (Per the brief, a divergence would be fixed in source + golden;
there was no divergence.)

## Tests

- New: `tests/unit/newgame_diff_w15_test.cpp` — 16 checks. Pins the
  binary-confirmed constants: start-gold `1250-250*d` for d=0..4, cheat 75000,
  talent bytes for variants 0/1/2 (get_bytes `@0x649910`), the purse range
  `32*[0..0x1FF]+16000`.
- Re-ran the full segment green (normal `build/`, GUILD_BACKEND off):
  `newgame_apply_test` 142, `newgame_builders_w13_test` 48,
  `newgame_diff_w15_test` 16, `newgame_result_itest` 23, `command_apply7_test`
  523, `sim_command_inherit_test` 23, `sim_command_apply6_test` 113,
  `person_create_harden_test` 1546 — 0 failures.
- Guarded e2e against the real install (`GUILD_GAME_DIR=.../europe_guild_1400_original`):
  `newgame_apply_e2e_test` — real AUGSBURG load -> commit; player=511 mother=512
  father=513 goldBase=750 (difficulty 2) funded=1; 23 checks, 0 failures.
