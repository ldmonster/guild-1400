# Wave-18 — the FAMILY-RECORD table (word_13C3110) reconstructed 1:1

Closes the wave-17 named deferral: the family table `word_13C3110` /
`VIBE_Person_GetFamilyRecord @0x58c408` were not reconstructed, so the
family-record name/dynasty gate inside `Person_CreateAndSpawn @0x58da70` and
`ExCreatePersonB @0x496714` (`GetFamilyRecord(rec) && rec[81] < 0`) never fired.
The whole subsystem is now in-tree and the gate fires.

## Decompile findings (reference of record)

### Accessor — `VIBE_Person_GetFamilyRecord @0x58c408` (`__usercall`, eax=a1)
```
v1 = *(BYTE*)(a1+2);                                   // kind
if ((v1 != 6 && v1 != 7 && v1 != 5) || *(char*)(a1+81) >= 0) return 0;
v2 = *(WORD*)(a1+80); HIBYTE(v2)=0; LOBYTE(v2)=v2&0xF; // v2 = word & 0xF
return &word_13C3110[82 * v2];
```
- Family kinds: **6** (player/head), **7** (relative), **5** (ancestor).
- A person's **family word** is at byte **+0x50** (word index 40 of the 536-byte
  person record). The **0x8000** bit marks "has a family slot"; the low nibble
  `& 0xF` is the table index 0..15.
- `rec[+81]` is the HIGH byte of that word; `(signed char) < 0` iff the 0x8000
  bit is set. So the `rec[81] < 0` gate == "the person owns a family slot".

### Table shape — from the reset `@0x5896fc` (ResetAllBuildings tail) + the save
writer `@0x5a45bc`
- `word_13C3110`: **16 records**, stride **82 words = 164 bytes**.
- Reset: per record `memset(rec,0,164)` then `rec.word[0] = -1` (free sentinel);
  `dword_647724=0; dword_64771C=0; dword_647720=0`.
- Field map (only the live-tree fields named; the rest is a save-only image):
  - **+0 (2)** family word (`idx | 0x8000`); `-1` == free.
  - **+2 (16)** dynasty / family name (StrNCopyPad, 15 + NUL).
  - **+128 (4)** float seed, stamped `-1.0f` (`0xBF800000` == `-1082130432`).

### Allocator — INLINE in `CreateAndSpawn @0x58ecf3` (no separate function)
```
if (dword_647720 < 16) {
    v14[40] = dword_647720 | 0x8000;            // person +0x50 = idx | 0x8000
    FamilyRecord = VIBE_Person_GetFamilyRecord(v14);
    if (FamilyRecord) {
        *((DWORD*)FamilyRecord + 32) = -1082130432; // +128 = -1.0f
        *FamilyRecord            = v14[40];          // +0   = family word
    }
    /* kind 7/5: wappen dedup scan over dword_12CE964 (caller-side) */
    ++dword_647720;
} else {
    return 0xFFFF;                               // table full -> create fails
}
```
The "allocator" the brief asked about is this inline block: it is the only writer
of `word_13C3110` records apart from the apply handler and the save loader.
`dword_647720` is the next-free family index.

### Apply-time stamp — `ExCreatePersonB @0x4967bf..0x49681c`
After `GetFamilyRecord(rec)` succeeds:
- `familyRec.word[0] = person.word[+0x50]` (`word_12CE960[...]`);
- `StrNCopyPad(familyRec+2, packet+53, 16)` — the dynasty name;
- `StrNCopyPad(rec+64,    packet+53, 16)` — the person's own family-name field;
- the **+0x60-linked spouse** (`dword_12CE970[...]`, `FindRecordById`) gets the
  same name at its +64;
- `familyRec[+128] = -1.0f`.

## Reconstructed (1:1, in-tree) — NEW module `src/sim/family_record.{h,cpp}`

- `g_familyTable[16 * 164]` (`word_13C3110`) + `g_familyCount` (`dword_647720`).
- `Person_GetFamilyRecord(personRec)` — `0x58c408` exactly (kind filter, +81 sign
  gate, `index = +0x50 & 0xF`).
- `FamilyRecord_ResetAll()` — the `0x5896fc` family-table tail (per-record clear +
  `word[0]=-1`, `dword_647720=0`).
- `FamilyRecord_AllocForPerson(personRec, kind)` — the `0x58ecf3` inline allocator
  (`+0x50 = count|0x8000`, record `+128=-1.0f` / `+0=word`, `++count`, full =>
  nullptr so the caller returns `0xFFFF`).
- `FamilyRecord_StampName(fam, word, name)` — the `0x4967bf` apply stamp (word[0],
  name@+2 NUL-padded, `+128=-1.0f`).

## Wiring (rule 13)

- **`src/sim/person_create.cpp`** (`DefaultPersonCreate`, the family arm at
  `0x58f07f`): replaced the dead `allocFamilyRecord`-hook-into-a-throwaway-buffer
  with the real `Person_GetFamilyRecord(rec)` + the `+128=-1.0f` / `word[0]=famWord`
  stamps (`0x58ed08..0x58ed1f`). `s_familyCount` stays the create-path authority
  (golden-RNG-pinned); `g_familyCount` is held in lockstep. `ResetPersonCreate`
  now calls `FamilyRecord_ResetAll`. The RNG draw count/order is UNCHANGED (the
  family block makes no RNG draws), so all wave-17 RNG goldens stay green.
- **`src/sim/command_apply5.cpp`** (`ExCreatePersonB`): the `0x4967bf` family block
  now fires — `GetFamilyRecord(rec)` resolves the real record (the spawn leaf set
  the 0x8000 bit), `FamilyRecord_StampName` writes the dynasty name + seed, the
  person's +64 and the +0x60 spouse's +64 get the name.
- The `PersonCreateHooks::allocFamilyRecord` hook field is RETAINED (additive,
  for spies) but no longer consulted by the default path; the header documents this.

## No genuine leaf remains

The family-record subsystem is fully data-in-tree: the table is a plain global,
the accessor/allocator are pure logic, the only inputs are the person record and
the packet name string. Nothing here is a swapped tech (rules 3-5) or
genuinely-missing data — the wave-17 "named hook" is closed, not re-deferred.

## Tests

- **NEW `tests/unit/family_record_test.cpp`** — 5 tests / 236 checks:
  - `layout_and_reset` — 164/82/16 shape, `sizeof(g_familyTable)`, the reset image
    (every record `word[0]=-1` + zero body, counter 0).
  - `accessor_gate` — kind filter ({5,6,7} only), the +81 sign gate, and
    `index == +0x50 & 0xF` for all 16 indices x 3 kinds.
  - `allocator` — fills all 16 slots (`+0x50=idx|0x8000`, record `+0=word`,
    `+128=-1.0f`, counter), and the 17th returns nullptr with +0x50 untouched.
  - `create_and_spawn_fires_gate` — a kind-6 `Person_CreateAndSpawn` now allocates
    a real record (gate fires; +81<0; record stamped); a kind-3 person does NOT
    touch the table.
  - `ex_create_person_b_stamps_name` — opcode-0x0C apply stamps the dynasty name
    through the family record + the person's +64.
- Kept green (no observable-contract / RNG break): `person_create_w17_test` (47),
  `person_create_harden_test` (1546), `newgame_apply_test` (142),
  `sim_command_apply5_test` (92), `world_family_harden_test` (49),
  `sim_command_apply6_test` (113), `sim_command_inherit_test` (23),
  `command_apply7_test` (523), `newgame_diff_w15_test` (16),
  `newgame_result_itest` (23), `sim_command_apply5_e2e_test` (132),
  `newgame_apply_e2e_test` (guarded; skips cleanly).

## Build

`libguild.a` + every family/person/newgame target builds and links clean. A
pre-existing UNRELATED link failure in `straftat_table_itest`
(`guild::world::StraftatTableReset` / `g_crimeTable` — another agent's in-progress
edit to `src/world/straftat_*`) is outside this subsystem and ownership; it was
already dirty in the worktree at session start.
