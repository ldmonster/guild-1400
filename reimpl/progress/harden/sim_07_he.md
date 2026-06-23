# Harden pass — sim "He" cluster (he_recon.cpp, he_recon3_worldpos.cpp)

Scope: every function carrying a `gilde.exe 0xADDR` provenance in
`src/sim/he_recon.cpp` and `src/sim/he_recon3_worldpos.cpp`. Each was
decompiled AND disassembled and diffed line-for-line against the
reconstruction. Disasm is authoritative.

## Per-function verdicts

### `He_UpdateHandlerWorldPos` @0x4c6cdc  — VERIFIED-1:1
- Control flow / branch conditions: matched exactly.
  - `record+8 == 0xFFFF` (word) -> early return, eax = record base (`mov esi,eax`,
    no eax overwrite). ✓
  - `536*row` via `shl4;add;shl2;sub;shl3` (= `(((r<<4)+r)<<2 - r)<<3` = 536r). ✓
  - id mismatch (`[esi+0Ch] != dword_12CE914[eax]`) OR `byte_12CE912[eax]==0x0F`
    -> RESET (loc_4C6D20). ✓
  - `byte_12CE918[eax]` (suppress): `jbe loc_4C6D39` taken only when suppress==0;
    suppress!=0 falls to return. Recon `if (r.suppress != 0) return;` ✓
- Switch on kind byte `[esi]` (loc_4C6D39): traced ALL cmp/jcc arms in disasm
  (unsigned compares). RESET set = exactly
  {0x16,0x18,0x19,0x1A,0x2D,0x2E,0x2F,0x35,0x36,0x37,0x38,0x41,0x45,0x46,0x47,
   0x4A,0x4B,0x5E,0x5F,0x60,0x69,0x6A,0x6F,0x7E} — identical to
  `He_WorldPosKindResets`. Verified 0x17 returns (not in set), boundary cases
  0x42-0x44, 0x48-0x49, 0x4C-0x5D, 0x61-0x68, 0x6B-0x6E all return. ✓
- RESET side effect: `[esi+0x70]=0xFFFFFFFE` (He_State +112) and
  movsd/movsd/movsd/movsw of qword_13CE852 (14-byte clock) into `[esi+0x52]`
  (He_ApptTime +82). Offsets confirmed against he.h. ✓
- Return value (al low byte): early=record base; table paths=`(u8)536*row`;
  switch paths=kind byte. All matched. ✓

### `He_ComputeEntityScore` @0x4c5030  — FIXED (missing real output param a5)
- Linear scan (45 stride, 23040 cap, `cmp eax,5A00h; jge`), action<0x1A check
  (`cmp al,1Ah; jge`, signed via movsx), `v12 = row[3]` zero-check, person
  resolve, marker store, officeRank(_,1), favorability(_,_,1), and the float
  formula `(fav*0.005 + 0.5)*(v12+rank)*1600` — all VERIFIED-1:1.
- Float constants confirmed via get_bytes @0x61E650:
  `0a d7 a3 3b`=0.005f, `00 00 00 3f`=0.5f, `00 00 c8 44`=1600.0f. ✓
- float->int store `*a4`: VERIFIED truncate-toward-zero. The disasm calls
  `VIBE_Coord_ConvertX` @0x5c6b08 BEFORE the `fistp` @0x4c512c. ConvertX sets
  FPU control-word high byte to 0x1F (RC bits 10-11 = 11 = round-toward-zero)
  and does `frndint` on st0 in place, then restores CW. The following bare
  `fistp` stores the already-integral value. Net effect = truncation, so the
  recon's `static_cast<i32>(v17)` is correct. (Comment improved to cite this.)
- **FIX**: the original's tail does `mov al,[esp+var_40]` (=low byte of v20[0] =
  `kHeScoreTable[action][0] & 0xFF`) @0x4c5120, `mov edx,[esp+arg_0]` @0x4c5123,
  then `mov [edx],al` @0x4c512f — a REAL output store to the function's 5th
  (stack) argument a5 on the success path. The header previously called this a
  "spurious / register-clobber ghost store" and omitted it; the disasm proves
  otherwise:
    - al is a deterministic load (not a clobber): ConvertX preserves eax (its
      only eax touch is `push eax` with `lea esp,[esp+8]` cleanup, no fpu/eax
      writes between).
    - a5 is a valid caller pointer in every live caller, e.g.
      `VIBE_AiPlayer_FindOpponentBuilding` @0x47dbf6 does `lea eax,[local]; push eax`
      immediately before the call (function is `retn 4`). Other callers:
      `VIBE_Location_TavernDarkCorner{Browse,Buy}` @0x518276/@0x5187ab also push a
      stack slot.
  - Before: `int He_ComputeEntityScore(table, a1, evalMarker, a3, a4, leaves);`
    (no a5; byte store dropped).
  - After: added trailing `u8* a5 = nullptr` (default keeps existing C++ callers
    / test compiling) and, on the success path, `if (a5) *a5 = (u8)row[0];`.
  - Test: `handle_recon_test.cpp` ComputeEntityScoreGoldenVector now passes
    `&rowByte` and asserts `rowByte == 0x02` (action 2 row[0]=0x302, low byte 0x02;
    confirmed via get_bytes @0x631ee0 = `02 03 00 00`). Score golden 3200 verified
    (0.5*4*1600).
  - NOTE: handle_recon_test could not be (re)built/run THIS pass because an
    unrelated, pre-existing compile error in another agent's file
    `src/render/raster_textured.cpp:245` (`RgbzRasterState` has no member
    `minYSeed`) breaks libguild.a linkage. My edits were verified to compile in
    isolation: `c++ -std=c++17 -fsyntax-only src/sim/he_recon.cpp` and
    `... tests/unit/handle_recon_test.cpp` both succeed (EXIT 0).

### `He_TutorialEventHandler` @0x4db8c8  — VERIFIED-1:1 (one documented modeling note)
- Gate `byte_63CC40==0` -> return result (=record base, eax unchanged). ✓
- State machine on `[edx+0x70]` (record+112), unsigned dispatch
  (`cmp eax,1; jnb`, `test;jz`, `jbe`, `cmp eax,2;jnz`): state0->OpenMainPanel,
  state1->ProcessDragDropClick, state2->terminal, state>2->return state. ✓
- Transitions: open ret -> state=2 else 1; drag ret -> state=2 (else unchanged);
  terminal: Close, ResetChapter, FreeHandlerEntry(record), dword_63CC30=1,
  return Hud_FindModeIndex(...). All return values & write order match. ✓
- Modeling note (NOT a divergence in observable state logic): the original takes
  dragdrop's arg from a2@ecx and CloseEventPanel's arg from a3@ebx (CloseEventPanel
  @0x597270 is `__usercall a1@ebx`). The recon collapses both to a single `arg`.
  At the live dispatch (`funcs_4C6EE9[*v4]()` in `VIBE_He_RunAllHandlers`
  @0x4c6e38) the handler is called with NO explicit arg setup — a2/a3 are ambient
  leftover registers. Faithfully splitting them would require modeling the full
  dispatch register state and is not exercisable headlessly; the state machine,
  gating, transitions and returns are all exact, so left as documented. Tests
  (State0/State1/State2/Inactive) cover all arms and pass.

## Deferred BOUNDARY blocks (confirmed still deferred, not implemented)
- `VIBE_He_AssignIconForHandler` @0x4c6964 (size 748) — BOUNDARY: world-icon GFX
  dispatcher, render-coupled (VIBE_He_CreateGfxInfo + Building/Person/Object
  layouts). Remains deferred per rule 8.
- `VIBE_He_ExecutePunishment` @0x4c4654 (size 1290) — BOUNDARY: 9-case
  command-packet builder coupled to command-queue packet structs (+0x130/+0x10C
  frames). Remains deferred per rule 8.

## Counts
- Functions with provenance reviewed: 3 (worldpos 0x4c6cdc, score 0x4c5030,
  tutorial 0x4db8c8) + helper `He_WorldPosKindResets` (part of 0x4c6d39..0x4c6e08).
- VERIFIED-1:1: 2 (worldpos, tutorial).
- FIXED: 1 (score — added missing a5 output param + byte store; corrected
  misleading header comment; corrected truncation-vs-rounding rationale).
- BOUNDARY (confirmed deferred): 2.
- Tests run green: he_recon3_worldpos_test, sim_he_test, sim_he_handlers_test,
  he_messaging_test (4/4 PASS). handle_recon_test blocked by unrelated
  raster_textured.cpp error; my edits syntax-checked clean in isolation.
