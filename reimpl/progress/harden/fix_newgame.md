# Wave-H1b — FIX-NEWGAME (newgame_apply + building2_e2e)

Both targets now GREEN. Two stash-frozen golden divergences; in both cases the
SOURCE was already 1:1 with the binary and the TEST golden encoded wrong behavior.
Fixed the goldens to the binary (no source changes needed).

## 1. building2_e2e_test — `Building2_E2E.TypeRecordAndStateMapping`

Failing asserts (tests/e2e/building2_e2e_test.cpp:159,161):
- `Building_MatchProfessionCode(24, 2, profCodes) == 1`
- `Building_MatchTypeCode(24, 1, typeCodes) == 1`

Root cause: the test bound the profession/type bytes into `buildingTypeBase`
(stride 589) at `589*24 + 358/361/356`, but the two matchers read a DIFFERENT array.

Evidence (decompile):
- `VIBE_Building_MatchProfessionCode` @0x5898e8 (0x589906): `v3 = &word_12CE910[268*a1]`
  — the person/family array (byte stride 536). Compares `*(rec+358)` / `*(rec+361)`
  against each code (unsigned byte). Source `building2.cpp` reads `PersonRec` (536) — CORRECT.
- `VIBE_Building_MatchTypeCode` @0x589960 (0x589998): `qmemcpy(v8, &word_12CE910[268*a1], 536)`,
  match field `*(int*)&v8[353] >> 24` == byte +356. Person/family array (536). Source reads
  `PersonRec` (536) — CORRECT.
- `VIBE_Building_MapTypeToState` @0x592a5c reads `buildingTypeBase[589*typeCode + 0]` — 589 (the
  state-mapper part of the test was already right and passed).

Fix (test only): added a second binding `b.personFamilyBase = pf.data()` (stride 536) and moved
the +358/+361/+356 writes into `pf[536*24 + ...]`. State-mapper writes stay in `tt` (589).

## 2. newgame_apply_test — `NewGameApply.OversizedNamesCapToFieldWidth`

Failing assert (tests/unit/newgame_apply_test.cpp:392): `CHECK(fn.size() < 16)` where
`fn = RStr(pl, 0x30)` (player first name, 200-char input).

Root cause: the golden assumed the player first name at +0x30 is NUL-terminated to `< 16`
chars. The binary uses `StrNCopyPad(..., 16)` which fills exactly 16 bytes for an oversized
source and adds NO NUL inside the field. The field is exactly the 16-byte truncation, in-bounds.

Evidence (decompile):
- `VIBE_Command_HandleCreatePersonB` @0x496714 (0x4967b6):
  `StrNCopyPad(v15+24 == record+0x30, packet+37, 16)` — player first name, 16-wide.
  (0x4967f6): `StrNCopyPad(v15+32 == record+0x40, packet+53, 16)` — person's own family-name
  field, 16-wide. So player +0x40 holds the 16-byte truncation of familyName ('M'×16), not 0.
- `VIBE_Util_StrNCopyPad` @0x5d9360: copies up to n src bytes (break on NUL), then zero-pads
  the REMAINDER of n. For a 200-char src and n=16 it writes 16 chars, zero-pad count = 0 ⇒
  no terminator within the field.
- Reimpl source already 1:1: `command_apply5.cpp:362` `StrNCopyPad(rec+48, pkt.bytes+37, 16)`
  (player first name +0x30), `:378` `StrNCopyPad(rec+64, pkt.bytes+53, 16)` (own family name
  +0x40). Brief's note verified: dynasty-name dst is rec+0x40 (`:378`), firstName via NameAt
  path (person_create.cpp:489-507). Correctly applied.

Fix (test only): replaced `CHECK(fn.size() < 16)` with the real 1:1 outcome — +0x30 is exactly
16 'F' bytes, +0x40 is exactly 16 'M' bytes (the family-name truncation), no overrun into the
+0x50 family word.

## Files touched
- tests/e2e/building2_e2e_test.cpp (golden: bind personFamilyBase @536 for the matchers)
- tests/unit/newgame_apply_test.cpp (golden: 16-byte StrNCopyPad truncation, not NUL-cap)

Source files (newgame_apply.{cpp,h}, building2.{cpp,h}) NOT changed — already 1:1.

## Result
`ctest -R 'newgame_apply_test|building2_e2e_test' --output-on-failure` → 100% (2/2) pass.
newgame_apply_test: 159 checks, 0 failures. building2_e2e_test: 50 checks, 0 failures.
No git commands run.
