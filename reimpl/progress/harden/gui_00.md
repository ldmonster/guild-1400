# HARDEN gui_00 — 1:1 full-tree verification sweep (MCP live)

Chunk: the 22 `src/gui/*.cpp` files listed in `chunks/gui_00.chunk`. For every function
carrying a `gilde.exe 0xADDR` / `@0xADDR` provenance, the original was decompiled AND
disassembled and diffed line-for-line against the C++ reconstruction. Disasm is the
reference of record where Hex-Rays is loose.

Module `gilde.exe`, imagebase 0x400000. Work fanned out across 7 parallel sub-agents
(rule 9); per-group detail in `_gui00_parts/part_*.md`. This file is the roll-up.

## Headline counts
- Functions audited (provenance-tagged): **63**
- VERIFIED-1:1: **57**
- FIXED (source + golden corrected to the binary): **5**
- DOCUMENTED DIVERGENCE / FLAGGED (rule 8 — surprising binary result, not guessed): **1**
- All BOUNDARY seams are legitimate rules-3/4/5 tech swaps (Vulkan/SDL/SDL-audio) or the
  command codec / heavy 3D-scene host hooks; none are cheap analogues.

All 14 of this chunk's test suites build and pass:
`buy_dialog_test, choosecharacter_intro_run_test, choosecharacter_run_test,
choosehistory_run_test, chooseplayer_run_test, gui_book_reader_test,
gui_building_upgrade_test, gui_charcreate_test, gui_choosecity_run_test,
gui_chronicle_window_test, gui_contact_loops_test, gui_credits_run_test,
gui_dialogs2_test, building_upgrade_window_test` — 14/14 green.

---

## FIXED (5) — divergence corrected to the binary

### 1. building_options_menu.cpp — OptionsMenu @0x54c608 `flag90 & 4` disable group
Disasm @0x54cd18 (by stack offset) disables exactly {OpenUpgradeWindow(5055), Renovate,
TearDown, SellPreview}. Source had been disabling {SellPreview, ConfirmSell, Renovate,
TearDown, SellLand} — ConfirmSell+SellLand wrongly included, OpenUpgradeWindow wrongly
omitted. Corrected to the four binary-true entries. Source + golden.

### 2. building_dialog.cpp — Renovate @0x54a908 spurious 1155 branch
Disasm 0x54aa97..0x54ac65: the loop only tests `dword_75BF38 == -1` then compares
`dword_62D22C` to the four child ids — there is NO 1155 (cancel) path (unlike
SellPreview/ConfirmSell/SellLand/ExtinguishFire which genuinely have one). Replaced the
bogus `kClickCancel` arm with `if (clickedId == -1) return false;` (keep-looping on no-hit).
Also tightened ExpandRoom progress fraction to the original's `(double)elapsed/(float)total`
typing.

### 3. charcreate.cpp — Talent_CanIncrease @0x52b088 inverted polarity + wrong threshold
Disasm: `cmp 0,budget; jge enable` (enable if budget<=0) then `fild value; fcomp
dbl_622E18; jnb enable` (enable if value>=126.0). Source had the INVERSE
`budget>0 && value>=0.0`. dbl_622E18 = 0x405F800000000000 = 126.0 (get_bytes). Fixed the
predicate, the header default (0.0 -> 126.0), and the golden.

### 4. choosecharacter_intro_run.cpp — IntroVariant commit @0x52e4e0 wrongly gated
Disasm 0x52e6c7 (`cmp esi,id4; jnz loc_52E64C`) proves the OK/Enter commit is
UNCONDITIONAL (sets eax=1 / dword_631614=1 even when no radio member matched); only the
radio VALUE (dword_63C744) is match-gated. Dropped the `if (variant>=0)` guard around the
commit. Source + golden (`OkWithoutSelectionDoesNotCommit` ret==0 was wrong -> renamed
`OkWithoutSelectionStillCommitsSeed`, ret==1, seed carried).

### 5. credits_run.cpp / credits.cpp — Menu_RunCreditsScroll @0x56e524 Window_Create arg
Disasm 0x56e594/0x56e59f loads ecx (a3) from `dword_69FFB8+2` then `sar ecx,0x10` = screenH,
NOT screenW (Hex-Rays mislabels it dword_69FFBC). Source passed st.screenW. Fixed to
st.screenH; fixed rec->windowW field + unit golden (wcW 800 -> 600).

Also in choosecharacter_intro_run.cpp the 1155 back-button branch @0x52e4d3 was force-setting
result=0; disasm shows the 1155 path only arms dword_631614 and leaves esi untouched, so in
the window-close + same-frame-1155 edge the return is -1, not 0. Fixed (counted within FIX #4's
file).

---

## DOCUMENTED DIVERGENCE / FLAGGED (1) — rule 8

### charcreate.cpp + choosecharacter_run.cpp — ChooseCharacter_ApplyActorClick / IsComplete @0x52bcd4
The reimpl models a six-slot dynasty fill (clicks fill slots 0..5, complete at 6). The
ORIGINAL does NOT. Disasm ground truth (0x52bf14..0x52bf3e, 0x52c084..0x52c0b4) + memory
aliasing:
- slot[i] = dword_122F258 + 4*i, so **slot4 == dword_122F268**.
- Init order @0x52bd1b/0x52bd25: `SetGrayColorThunk(0,44,dword_122F258)` is a memset of
  44 *bytes* (=11 dwords) to 0 (VIBE_Memory_FillDwordAlignedThunk a3 = byte count, verified
  @0x5f8160); THEN `for i=0..5: dword_122F254[i]=-1` writes 0x122F254 + slots 0,1,2,3,4 to -1.
  Net: slots 0..4 = -1, **slot5 = 0** (left at the gray-fill value).
- Free-slot walk @0x52bf14: v7=4; edi=slot4; if slot4==-1 -> v7 stays 4 (father open); else
  test slot5 (!= -1?) — since slot5 starts at 0, the walk reaches v7=6 right after the father
  is placed. The walk only ever inspects slots 4 and 5.
- Click writer @0x52c0b4 writes ONLY dword_122F258[v7], v7 in {4,5}, parity-gated (even=4
  males, odd=5 females). Grandparent slots 0..3 are NEVER written. xrefs confirm 0x52c0b4 is
  the sole click writer of dword_122F258.

Net observed binary behavior: the scene needs the FATHER (male) click into slot 4; the
mother slot 5 is pre-seeded to 0 by the gray-fill, so completion (v7>=6) fires immediately
after the father is placed — i.e. ~ONE meaningful click, NOT six, and the mother appears
non-selectable via the walk. This is a SURPRISING result that hinges entirely on the
gray-fill-vs-(-1) aliasing and on per-session reset of slot5/dword_122F268 that I could not
fully resolve statically. Per rule 8 (no guessing, ask rather than paper over) I have NOT
rewritten the fill/complete model across charcreate.cpp + choosecharacter_run.cpp + their two
tests on a surprising inference. The precise finding + evidence is recorded in the 1:1 NOTE
in `charcreate.h` (corrected this pass — the prior NOTE under-described the slot4/dword_122F268
aliasing and the gray-fill seeding). **Needs a user/orchestrator decision + a live-scene
trace before flipping the model.**

---

## VERIFIED-1:1 (57) — by file

- **action_dialog.cpp** (5): Sabotage 0x547264, Spy 0x547714, BeatUp 0x547a60,
  ConfirmAbduct 0x54891c, ConfirmFreePrisoner 0x548f30. Cost float constants byte-confirmed
  (0.02 / 0.05 / 0.005 / 0.0085); ConvertX @0x5c6b08 truncate + debug-27 signed `/2` confirmed;
  preflight orderings, kinds (25/24/26/46/53), seal stamps, body text ids (4972/5407) match.
- **action_target_pickn.cpp** (2): 0x548c0c, 0x5488ac (gray 40, flag 1024, kind 6, payload,
  textId 4962, edge-scroll).
- **bard_dialog.cpp** (1): PerformPoem 0x5495a8 (gate ladder 6657/6658/6659/6661; verdict
  1->6712 +446, 0->6713 +4810, else->6714).
- **book.cpp** (0 tagged; clamp model cross-checked vs VIBE_Book_TurnPage 0x4be60c, step=2).
- **book_reader.cpp** (7): 0x4be494, 0x4be3d0, 0x596ee8, 0x4be8d8, 0x4be960, 0x4be990,
  + the 0x4be1d0/0x4be588 wrappers. Re-show parity byte_631E60 boot=0, ±2 flip fields
  +616/+620, scroll sprite x∈{0,465} gfx=1618 confirmed via disasm/get_bytes.
- **chatconsole.cpp** (2): BuildWindow 0x4bfc48 (tag==7, cap 8, template 32×0xFF, persisted
  {1×8}, "$%iFF "/"$A"), DebugList_AppendId 0x53629c (cap 128).
- **building_dialog.cpp** (5 of 6; Renovate fixed): 0x54a734, 0x54ad7c, 0x54bc50, 0x54c3c0,
  0x54be50. tech-category table dword_53B454, dbl_624350=0.3 / dbl_624328=0.25 /
  dbl_624330=0.5 byte-confirmed.
- **building_options_menu.cpp** (1 of 2; OptionsMenu fixed): 0x54d5b4.
- **building_upgrade_dialog.cpp** (2): 0x54b604, 0x54aeb0 (upgrade cap unsigned-byte compare,
  cost truncation).
- **buy_dialog.cpp** (3): PopulateObjectList 0x54d8fc (bound 2048, row==0 terminator),
  ApBuy 0x54d9d8 (text 6868, -1 latch, stride-2 bound 512), WineCellar 0x519b14
  (display math `sum>cellarDisp ? sum : cellarDisp`, cancel 1155, buy 1210).
- **charcreate.cpp** (5 of 6; CanIncrease fixed, ApplyActorClick flagged): tables 0x527258 /
  0x52739c / 0x527378 / 0x52749c byte-for-byte; talent maps 0x52b088; classification 0x52bcd4
  (ActorProfessionCode, ActorIsMale).
- **choosecharacter_intro_run.cpp** (2 fixed above; remaining control flow VERIFIED).
- **choosecharacter_run.cpp** (1): 0x52bcd4 run-loop control flow (close 672230, actor pick
  631720, chain Talent->Profession->Preview, return semantics).
- **choosecity_run.cpp** (3): 0x52e6d8, 0x52e797, 0x52ee38.
- **choosehistory_run.cpp** (1): 0x52d684 (word_63C740 |= 8).
- **chooseplayer_run.cpp** (1): 0x52ccd8.
- **chronicle_window.cpp** (2): DispatchMode 0x4fed20, FormatPage/BuildScrollPlan 0x4fe74c
  ("$C%s" / "$C$Z$[%s$]" byte-confirmed @0x620b14/0x620b1c; 1210 advance / 631614 exit).
- **contact_actions.cpp** (5): Smith 0x513954/handler masks, Stonemason 0x514c44,
  Brewery 0x518dc4, Perfumery 0x5148b0, Mixing 0x514ac8 (gfx 19/22/14/21/12, page split
  0x200/0x400).
- **contact_loops.cpp** (8 section-header tags): ErzAbbau / ProductionMetal / RobberHideout /
  Empty / Sabotage / GuildMaster / Tavern / WineCellar / CityTreasury — incl. RobberHideout
  GELAGE gfx14 + "TRANSPORT", ProductionMetal GELAGE gfx22, CityTreasury stride-536 gate,
  Tavern dice return discarded.
- **contact_menu.cpp** (2): StatusText_ResetEntries 0x4bcc4c (50-dword stride, +536 clear,
  return 6400), StatusText_Register 0x4bcc80 (dedup, v6>=32 cap, offsets +4/+67/+68/+72/+199).
- **credits.cpp** (4 of 5; one shared fn fixed above): 0x56e635 offset advance (signed idiv
  %), 0x56e669 completion `textBottom < textHeight*1.5+offset` (dbl_625324=1.5 confirmed),
  0x56e681 speed ramp (80.0f/30.0f thresholds), 0x529c30 window variant
  (Window_Create(32,96,400,620,21), block 5576, ESC==1).

---

## BOUNDARY (legitimate seams, unchanged)
Frame loop (VIBE_GameLogic_RunFrameLoop -> SDL pump), text engine
(RenderRichString/RenderFormattedMessage), audio (mp3 / volume -> SDL audio), heavy 3D-scene
host hooks (CreateMenuDummyActor, fade/sky, DecompressGameState re-show), and the command
codec (RequestBuildOp75/90) are forward-declared / routed through mockable hooks per rules
3-5. No data-not-in-tree gaps; no cheap analogues introduced.

## Notes
- No git commands were run. The build/ directory was never deleted/recreated; only this
  chunk's test targets were built.
- A transient out-of-scope build break (src/gui/tooltip_build.cpp `BuildingTooltipLayout`
  lacked `iconObjectId`, a concurrent agent's WIP) was observed by two sub-agents mid-run;
  it is resolved as of the final build — the `guild` library and all chunk test targets link
  and pass.
