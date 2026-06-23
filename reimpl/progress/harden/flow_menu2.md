# Harden sweep — MENU-2 (new-game / character-creation leaves)

Agent: GUI-flow hardening MENU-2. MCP live. No git used. Only owned files + their
tests edited. build/ untouched.

Owned files:
- src/play/sdl_charcreate_screen.cpp
- src/play/sdl_charintro_screen.cpp
- src/play/sdl_choosehistory_screen.cpp
- src/play/sdl_chooseplayer_screen.cpp
- src/play/sdl_credits_screen.cpp
- src/play/sdl_loadgame_screen.cpp
- src/play/sdl_options_screen.cpp
- src/play/newgame_apply.cpp

## Method
These SDL screens are the rule-3/4 backend-agnostic native equivalents of the
original Win32-forms screens (DirectDraw/Direct3D + forms.BIN). Their per-frame
render/input loops are NOT 1:1 binary functions. The 1:1 obligation lives in the
embedded MODEL logic and constants they carry provenance for. Every provenance'd
constant/function was diffed against the disasm/get_bytes.

## FIXED

### newgame_apply.cpp — NewGameProfessionTalents  (gilde.exe 0x52d9f9..0x52da0e)
OFF-BY-ONE in the talent byte selection.

Evidence — disasm of the copy loop inside VIBE_Menu_RunChooseHistory @0x52d9fe:
```
52d9f4 mov esi, esp                 ; record byref base = esp
52d9f9 call VIBE_Building_LookupTypeRecordA   ; writes 6-byte rec at esp+0..5
52d9fe xor eax, eax
52da00 inc eax                      ; eax = 1,2,3,4,5
52da01 mov dl, [esp+eax+...]        ; reads rec[1..5]  (SKIPS byte 0)
52da05 mov (122F4EC+3)[eax], dl     ; writes byte_122F4F0[0..4]
52da0b cmp eax, 5 ; jl 52da00
```
LookupTypeRecordA @0x589778 writes dword0(+0..3) + word4(+4..5) — a 6-byte record.
The loop copies record BYTES [1..5] into the 5 talents, i.e. it drops byte 0.

byte_649910 (get_bytes, 6 bytes/record):
- rec0 = 00 00 00 00 00 00 -> talents {00,00,00,00,00}
- rec1 = 69 69 BD 93 69 04 -> talents bytes[1..5] = {69,BD,93,69,04}
- rec2 = 3F 3F 93 69 3F 03 -> talents bytes[1..5] = {3F,93,69,3F,03}

Before (WRONG): took rec[0..4] -> rec1 gave {69,69,BD,93,69}.
After: out[0]=dword0>>8, out[1]=dword0>>16, out[2]=dword0>>24, out[3]=word4&0xFF,
out[4]=word4>>8.  -> rec1 {69,BD,93,69,04}, rec2 {3F,93,69,3F,03}. Matches binary.

### tests/unit/newgame_apply_test.cpp — talent goldens (rewritten to the binary)
- TalentsGoldenFromTypeRecordA: variant1 {69,BD,93,69,04}; variant2 {3F,93,69,3F,03}.
- The full-commit golden: r.talents[2] 0xBD -> 0x93 (record[3]).
Old goldens encoded the off-by-one; both source and goldens corrected to the binary.

### newgame_apply.cpp — comment fix (mission-slot gate address)
0x533a15 reads `dword_63C8F0+1` (byte 0x63C8F1), `sar eax,0x18`, `cmp -1; jg`.
Comment said 0x63C8F4 -> corrected to 0x63C8F1. Behavior (signed byte > -1) was
already correct.

## VERIFIED-1:1

### newgame_apply.cpp — ApplyNewGameParams (VIBE_Command_EnqueueInheritanceTransfer
@0x5336f0 + the InitOrLoadSession new-local block @0x533b85/0x533f35)
Diffed line-for-line against full decompile + disasm 0x533860..0x533a52:
- Arm gate BYTE2(dword_122F4A0) -> p.started.
- Player create EnqueueTradeRequest(-1,-1, SHIBYTE(122F4A0), 16, 122F4A4=wappen,
  122F4A8=gender, 122F4A9=faith, String, 122F4CA). args + order match.
- Two parent creates: RandomModulo(4)+32 each (mother gender-flag 1 FIRST, father
  flag 0 second). var_14=mother(=v31), var_10=father(=v32) confirmed via the
  +0x5C spouse-link disasm (5338c6..5338d7).
- Family-field stamp order EXACT: father+0x40=family name (StrNCopyPad 16);
  mother +0x50/+0x54/+0x68 from player; mother first name = female table
  [RandomModulo(0x70)]; ResolveStaffModel(mother); father +0x50/+0x54/+0x68;
  father first name = male table[RandomModulo(0xBF)]; ResolveStaffModel(father);
  mother+0x5C=father id; father+0x5C=mother id; player+0x60=father id,
  +0x64=mother id; both parents +0x68=player id; player+0x1F0=avatar name;
  player+0x18C=portrait id (dword_122F528).
- RNG draw ORDER (RandomModulo) confirmed: 4, 4, 0x70, 0xBF, 0x200, 0x200.
- 6x QueueRequestCoord27(...,127) order: (P,F)(F,P)(P,M)(M,P)(F,M)(M,F). Matches.
- Parent purses EnqueueCmd15: father first then mother; amount = (RandomModulo
  (0x200)&0xFFFF)<<5 + 16000 (0x3E80). shl 5 / +0x3E80 / and 0xFFFF all match.
- Mission slot gate: signed byte 0x63C8F1 > -1 -> SlotRegister(player, LOBYTE
  dword_122F4EC).
- Talents written player+0x80..+0x84 (loop @0x533a29).
- New-local block: GameTime_Set(6,0,0)->QueueRequestFlagBlob32(3) FIRST; base =
  cheat?75000:1250-250*difficulty; person loop stride 536, alive byte +8, kind
  byte +2 in {6,7}, id dword +4 -> EnqueueCmd15(id,-1,MoneyMultiplyByRate(base,
  rate),rate). Offsets/stride match byte_12CE910 base (12CE912/14/18 = +2/+4/+8).

### sdl_charcreate_screen.cpp
- kBerufTable {1,2,3,4,5,6,7,11}: get_bytes @0x527604 = 01 02 03 04 05 06 07 0B.
  (It is a BYTE table; ChooseProfession @0x52c50c reads it byte-wise via
  `(byte)>>24` sign-extend — values confirmed.)
- gfx id = beruf + 1349 (0x52c693 match `(beruf)+1349 == clicked widget gfx`).
- Profession commit stores ComputeVariantIndex(beruf,1) (0x52c6a9) — applied by
  gui::NewGame_ApplyProfession (correctly wired, gui-owned).

### sdl_charintro_screen.cpp (difficulty — VIBE_Menu_ChooseCharacterIntroVariant
@0x52e4e0)
- 6 radio rows; rows 0..4 -> difficulty 0..4, row 5 = back (no assignment).
- Seed = byte_12335BA (Selection_Update). Model matches (seedVariant).

### sdl_choosehistory_screen.cpp (VIBE_Menu_RunChooseHistory @0x52d684 perspective)
- Row->History flag mapping (ChooseHistory_RowToFlag, owned header): row0->1,
  row1->2, row2->0. Matches SetActiveFlag(1/2/0) for v32/v33/v34 @0x52d7d7..0x52d91e.
- Seed index (gui ChooseHistory_SeedIndex): mode0->row2, mode1->row0, mode2->row1.
  Matches dword_12335AC switch Selection_Update(2/0/1) @0x52d767.

### sdl_chooseplayer_screen.cpp
Native identity-wizard front over gui::Menu_RunChoosePlayer @0x52ccd8 (the page
state machine is gui-owned and driven through hooks). Page set (name/family/
gender/faith/wappen) + the wappen grid (gfx 1342+i) match the model. No new
binary constants in the owned file. VERIFIED at the wiring level.

### sdl_credits_screen.cpp (VIBE_Menu_RunCreditsScroll @0x56e524)
- dbl_625324 == 1.5: get_bytes @0x625324 = 00..00 F8 3F (0x3FF8000000000000).
- Initial step v0=4 (0x56e549); initial offset window+584 = -(screenH) (0x56e5d2).
- Advance every `step` frames: `if(!(v5 % v0)) ++(window+584)` (0x56e635/641).
- Scroll-complete: (double)(window+580) < (double)(int16)(window+10)*1.5 +
  (double)(window+584)  (0x56e669). The gui::Credits_* helpers carry this; the
  screen drives them with the right operands.
- Speed ramp thresholds 30.0f/80.0f (0x41F00000/0x42A00000) -> step 1/2/4
  (gui::Credits_ScrollStep). Confirmed.

### sdl_loadgame_screen.cpp
Drives gui::Menu_RunLoadGame @0x56a270 + the browser leaves (0x569530/0x5a7af0/
0x569c50). Slot table stride 544, ids init -1 (+4/+8), row+12=1 on placement,
n clamp <=16 (0x569d64), partial-save drop (flag&2 @0x569e1a) — all carried in
LoadGameBuildSlotViews matching the cited addresses. No drifted constants found.

### sdl_options_screen.cpp (RunOptionsGame/Gfx/Sfx @0x56cc44/0x56c21c/0x56c808)
Native equivalent of the forms.BIN panels. Spot-verified the load-bearing gamma
arithmetic: SetValueOrText(min=0x32=50, max=0x64=100, value=0x64-[edx+8]) =
100 - savedByte (disasm 0x56c5c0..0x56c5df). Save-back 50-(live-50) == 100-live.
Row keys/ranges carry per-child provenance comments consistent with the
SetValueOrText calls. VERIFIED at the model level.

## CROSS-CHUNK FINDINGS (NOT edited — outside ownership)

1. BUILD BLOCKER (pre-existing, not mine): src/play/slice_council.h:89 uses
   `sim::CommandPacket` with no include/forward-decl for it ->
   slice_council.cpp + dialog_council.cpp fail to compile, breaking the whole
   `guild` library link. This prevents running ANY ctest currently. Owner of the
   play/council chunk must add the sim/command.h include (or a forward decl).

2. NEW-GAME CHAIN ORDER in src/play/native_main_menu.cpp (wire chunk, not mine).
   Reimpl runs: city -> difficulty(CharIntro) -> history -> player -> charcreate.
   Binary (VIBE_Menu_RunChooseHistory @0x52d684) runs: city -> history(outer)
   -> [mission dialog] -> RunChoosePlayer -> ChooseCharacterIntro(difficulty)
   -> ChooseProfession. So in the original, HISTORY precedes PLAYER which precedes
   DIFFICULTY which precedes PROFESSION; the reimpl puts difficulty first and
   before history/player. Also ChooseCharacterIntro return drives a 4-way branch
   (0->profession, 1->RunChooseCharacter dynasty, -1->back to history, else
   restart) the reimpl's flat chain does not reproduce. Recommend the wire owner
   reorder to history->player->difficulty->profession and add the v20 branch.

## Build / test status
- All 8 owned .cpp files pass `g++ -std=c++17 -Wall -Wextra -fsyntax-only`
  (chooseplayer emits only pre-existing -Wmisleading-indentation warnings; no
  -Werror in the build).
- ctest NOT runnable: the `guild` library link is broken by the unrelated
  slice_council/dialog_council compile errors (finding #1). My changes are
  source/golden edits with no new link dependencies; they will pass once the
  council chunk is fixed.

## Counts
- Functions/constants VERIFIED-1:1: newgame_apply commit + new-local block;
  charcreate beruf table + gfx + variant; difficulty rows/seed; history
  row->flag + seed; credits scroll model (4 constants/formulae); loadgame slot
  table constants; options gamma arithmetic. (8 files swept.)
- FIXED: 1 behavioral (talents off-by-one) + 2 golden assertions corrected +
  2 comment-address fixes.
- BOUNDARY: the SDL render/input loops are rule-3/4 native equivalents (declared).
