# Wave-17 — VIBE_Person_CreateAndSpawn @0x58da70 gap closure

The pre-existing deferral (diff-newgame-wave15 item 3, newgame-apply.md "Named
gaps"): `src/sim/person_create.cpp` `DefaultPersonCreate` modeled only the
observable allocation (free-slot scan, `+8 == 100`, kind `+2`, id `+4`, owner
word `+10`, spawn ctx `+356/+357/+9`, the parent-building column). The full body
— the deterministic field stamps, the stat/talent init from the in-binary
tables, and the **dozens of RandNext draws** — was DEFERRED, with the documented
consequence that a save produced after a real CreateAndSpawn would differ in the
cosmetic stat/appearance fields AND the RNG stream would drift off the original.

This wave decompiled the whole function (~0x900 bytes) via MCP and closed every
part whose data is in the tree, including the **exact RandNext draw order**, so
the new-game commit's downstream RandomModulo sequence now stays in lockstep.

## Decompile findings (the reference of record)

`int __userpurge @<eax>(al=kind, edx=parentAId, ecx=ownerWord, ebx=parentBId,
char* a5=parentBuildingRec, a6, a7=parentProfCtx, a8=gender/seed)`.

Control flow:

1. **Free-slot scan** (`0x58da9c`): walk `word_12CE910` for the first marker !=
   -1 (cap 768). If full, scan `byte_12CE912` for a kind-15 (dead) slot to reuse,
   `--dword_647724`. Full => return `0xFFFF`.
2. **Deterministic stamps** (`0x58db73..0x58dc63`): clear 536 bytes; seed the 8
   relation dwords (+0x58..+0x74) to -1; `+2`=kind, `+4`=id (`dword_649890++`),
   `+8`=100, `+10`=ownerWord, `+356/+357`=a6/a7, `+0x190`=4, `+0x194`=0,
   `+396/+520/+524`=-1, `+0x1E0`=1.0f, `+0x214`=1, `++dword_647724`.
3. `ownerWord∈{3,16,19}` => `+0x1E4`=1, `+13`=2 (the playermode arm).
4. **kind-16** (menu-dummy) => `VIBE_Avatar_AllocSlot @0x484598`; if a slot is
   free, copies fields out of the 218-byte avatar record (3 RandNext draws) and
   returns early. NEVER taken by the new-game commit.
5. **parent-building column** (`0x58dc8c`): `*(589*type + dword_13CE294)` class
   byte => `+368` (class 1) or `+364` (class 4..22 minus {10,15,17}).
6. `+432` = `RandNext()%8` for kind not in {6,7} and < 10; gender `+9`
   (`a8==2 -> RandNext()%2` else `a8`); profession gender override for kind<4 ||
   ==11 via `GroupFromCode`.
7. `+0x54` wappen default 1342.
8. **Parent resolve** (`0x58dd77`): `VIBE_Person_FindRecordById` returns the
   record in eax AND the slot index in edx. The function branches to the
   **no-parents path** (`loc_58EA1A`) when EITHER index is 0 OR a record is null.
   **For the new-game commit the parents are not yet created, so both resolve to
   null => the no-parents path is ALWAYS taken.**

### The no-parents path (loc_58EA1A) — RNG draw order (the load-bearing bit)

| step | addr | draws |
|---|---|---|
| `v109 = RandNext()%8` (personality) | 0x58ea4d | 1 |
| stat seed loop, 14 triples: per triple `RandNext()` + `RandNext()%10` | 0x58ea8a | 28 |
| fitness scalar | 0x58eb34 | 1 |
| relation-grid loop, 768 iters: 2 `RandNext()%64-32` each | 0x58eb74 | 1536 |
| handedness `RandNext()%2` (+12) | 0x58ebf6 | 1 |
| height scalar (+0x1CC) | 0x58ebfe | 1 |
| talent loop, 5 slots: 1 draw/slot (table-hit or `%42+21`) | 0x58ec33 | 5 |
| face scalar `v154` | 0x58ed7c | 1 |
| **conditional** `RandNext()%hi` (only when the truncated `hi != 0`) | 0x58ee2d | 0..1 |
| first name `RandNext()%112 / %191` (or `%149` dynasty-female) | 0x58e339 | 1 |
| kind∈{3,4,2,19} => `RandNext()%3` (+130 boost) | 0x58e44e | 0..1 |

Total ≈ **1576–1578** draws per no-parents create.

### Recovered constants (get_bytes / get_global_value)

- RNG: LCG `state = 1103515245*state + 12345; return (state>>16)&0x7FFF`
  (`crt::RandNext`, already in tree). Scale `flt_62687C` = 1/32767.
- Float consts `flt_626880..flt_6268D4` (0.5, 0.25, 2.0, -1.0, 7.0, 30.0, 0.33,
  0.2, 0.1, 0.001, 2.0, 8.0, 900.0, 0.13, 42.0) — all reconstructed.
- Stat seed tables `flt_582900`/`flt_582904` (class-0 floats `{2.0,3.0,…}`; the
  rest of the cold image is zero-filled).
- Talent tables `byte_649910` (row 1 = `69 69 BD 93 69 04`) / `byte_649AD8` /
  `byte_649915` (== `byte_649910 + 5`). All `get_bytes`-verified.

## What CLOSED (reconstructed 1:1, in-tree)

`src/sim/person_create.{h,cpp}` `DefaultPersonCreate` now performs:

- The **full deterministic field-stamp image** (every constant at every offset
  above), `dword_647724` post-increment (`g_personLiveCount`), kind-15 slot
  reuse, and the `0xFFFF` full / family-table-full returns.
- The **no-parents-path stat seed loop** (14 triples from the `flt_582900/904`
  tables; `+0x88`/`+0x90`/`+0x80`), fitness + face scalars (`+0x1C`=384f,
  `+0x20`=12f, `+0x14`, `+0x10`, `+0x18`), handedness (+12), height (+0x1CC).
- The **talent bytes** `+0x80..+0x84` from `byte_649910`/`byte_649AD8` with the
  per-slot `% (base<42?6:11)` jitter and the `byte_649915` `+0xD` overlay.
- The **personality byte** `+531` (the `GroupFromCode`/`v109` branch ladder).
- The relation-id array clear (+0x58..+0x74 = -1, +0x58 = 1, +0x64 = parentBId).
- The **guild slot** (`+0x50 = dword_64771C++` for kind<10 non-family) and the
  **family slot word** (`+0x50 = dword_647720 | 0x8000`, `++dword_647720`,
  table-full => `0xFFFF`) for kinds 6/7/5, plus the kind-7/5 wappen dedup scan.
- **The exact RandNext draw count and order** — so the new-game commit's
  downstream `RandomModulo` draws (parent prof, names, purses) land on the same
  stream values as the original (verified: post-create LCG state pinned).

## What stays a NAMED HOOK (rule 8 — genuinely-missing data)

Routed through `PersonCreateHooks` (header), each with the address + reason. The
RandNext index draws those leaves would consume **ARE STILL MADE** so the stream
stays in sync:

- **Avatar slot** — `VIBE_Avatar_AllocSlot @0x484598` (kind-16 menu path only,
  never new-game). `allocAvatarSlot`, default null => the branch is skipped
  exactly as the original does when no avatar slot is free.
- **First-name string tables** — `dword_8C400C` (male,191) / `dword_8C4320`
  (female,112) / `dword_8C4508` (dynasty-female,149), runtime POINTER tables
  filled from localized text resources by `0x530e50`; **all-zero in the static
  image** (a load-time resource gap, not a code gap). `firstName`, default "".
- **Family record** — `VIBE_Person_GetFamilyRecord @0x58c408` over the family
  table `word_13C3110` (NOT reconstructed). `allocFamilyRecord`, default false
  => the `FamilyRecord[+128]=-1082130432f` / `FamilyRecord[0]=word` writes are
  skipped (the original's behavior when no record slot is available — matches the
  `ExCreatePersonB` family gate analysis). The `+0x50 = count|0x8000` slot word
  and the counter increment ARE done (they don't need the table).
- **Head-bone resolve** — `VIBE_Character_ResolveHeadBone @0x57c5d4`
  (scene-graph leaf). `resolveHeadBone`, default no-op.

## Genuine boundaries (consumed, not stored)

- **Relation grid** `dword_123D6CD` / `byte_1333110` / `byte_133310F` (the
  768×768 relation matrix) is a SEPARATE global subsystem (the new-game relation
  packets go through batch-6 `ExComputeObjectCoords`, already closed). The 1536
  per-create grid draws are CONSUMED so the stream stays aligned; the grid array
  itself is the relation-grid module's, not person_create's owned files.
- **Two-parent path** (`0x58dda1..0x58e336`, genetic stat interpolation +
  parent-name inheritance + grid seeding) is the in-game breeding path, never
  reached by the new-game commit (parents resolve to null at create time). The
  no-parents fallback is taken; the two-parent path is a future target for the
  breeding subsystem (it also depends on the relation grid).

## Observable contract preserved

`Person_CreateAndSpawn` / `PersonSpawnArgs` / `SetPersonSpawnHook` /
`g_personNextId` / `ResetPersonCreate` unchanged (additive only:
`g_personLiveCount`, `PersonCreateHooks`). The apply handlers
(`ExCreatePersonA/B`) and `ApplyNewGameParams` still call the default backend and
overwrite their own identity fields after create; the commit's golden record
image (names, links, gold, talents) is unaffected.

## Tests

- New: `tests/unit/person_create_w17_test.cpp` — 5 tests / 47 checks:
  deterministic field-stamp image; the **RNG draw count (1570–1600) + order**
  pinned via the post-create LCG state (a fingerprint of count AND order) +
  seed-determinism; the class-0 stat triple from `flt_582900/904`; the talent
  bytes from `byte_649910` row 1 + the `+0xD` overlay; the kind-6 family slot
  word `0x8000` + `dword_647724` / `dword_647720` counters.
- Kept green (no observable-contract break):
  `person_create_harden_test` (1546), `newgame_apply_test` (142),
  `newgame_diff_w15_test` (16), `newgame_result_itest` (23),
  `command_apply7_test` (523), `sim_command_apply6_test` (113),
  `sim_command_inherit_test` (23), the personnel/recruit/person suites.
- `tests/e2e/sim_command_apply5_e2e_test.cpp` SeedWorld now `crt::Srand(0)`s
  (one line + comment): with RNG-faithful create, the lockstep determinism
  replay requires a seeded generator — exactly what the original does at session
  init. 132 checks, 0 failures.
- Full suite swept: **1250 test binaries, 0 failing** (portable Debug,
  GUILD_BACKEND=OFF). The guarded real-asset e2e suites skip cleanly when no
  install is present.
