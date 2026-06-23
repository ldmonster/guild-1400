# Harden sweep — gui_04_windows

Chunk files: src/gui/{savebrowser,save_name_input,stammbaum_window,statistics_window,
storage_dialog,talent_dialog}.cpp (+ headers + dedicated tests).
Method: DECOMPILE + DISASM each provenance'd function, diff line-for-line, fix to binary.
DISASM is the reference of record. Tests built + green.

## Per-function verdicts

### save_name_input.cpp
- **0x56a700 Menu_RunSaveNameInput** — VERIFIED-1:1. Host-boundary model (frame loop +
  field widget via SaveNameInputHooks). ENTER(28) -> copy field DataPtr into buf (the
  2-byte-stride copy = strcpy), request close, v7=1; cancel (dword_672230 || !dword_62D328)
  -> close; return v7. Confirm/cancel/return semantics + side-effect order match.
  Inner blocks 0x56a71a..0x56a7f7 are sub-blocks of this fn (field register/color66/
  width272/x128/cb31 + the loop) — all accounted for.

### savebrowser.cpp
- **No provenance'd functions.** SaveBrowser_EnumerateSaveFiles / SaveBrowser_BuildSlots
  carry only descriptive comments (no `gilde.exe 0x..` / `@0x..` addr). Nothing to diff
  line-for-line in this sweep. NOTED.

### stammbaum_window.cpp
- **0x55ab84 Stammbaum_BuildLayout** — FIXED (3 divergences):
  1. Missing `+ nw/2` on EVERY node's stored x. DISASM 0x55ae0e-0x55ae32 self:
     `ax = nw/2 + center - nw - 90` (= anchor + nw/2). Same +nw/2 added at spouse(v101),
     parents(v86/v90), single-parent(v126+v25), and every child (v111+anchor / v135+anchor).
     Source used the bare anchor. Fixed: node x = anchor + nw/2.
  2. portraitX was `x + 2`; binary places the portrait sprite at `anchor + 2` (v146,
     0x55bbda) = `x - nw/2 + 2`. Refactored MakeNode(anchor, half): x = anchor+half,
     portraitX = anchor+2.
  3. Odd-children ORDER wrong. Binary reads anchors v103,v104,v105,... in memory order
     (`&v103 + v122`, v122=0,4,8). Source had child1=v105, child2=v104 (swapped).
     Fixed to memory order {v103,v104,v105,v106,v107}.
     Even children (v108[0..3]) order already correct.
  Goldens fixed (binary-derived): gui_dialogs2_test.cpp (self.x 70->90, spouse 290->310,
  parents 144/216->164/236, even children 140/220->160/240; single parent 180->200,
  odd children 180/260/100 -> 200/120/280), gui_dialogs2_e2e_test.cpp (w=600/nw=50:
  self->185, spouse->415, parents 259/341, odd children 300/210/390).
- **0x55ab84 Stammbaum_DispatchClick** — VERIFIED-1:1 (model). Resolves a clicked objectId
  to the node's entity (self/spouse/parents/children) or -1. The binary's table scan +
  FindRecordById + mode-gated recentre/info-window is the window/render boundary.

### statistics_window.cpp
- **0x57a61c Statistics_BuildGeneralPlan** — FIXED (3 divergences):
  1. Header sweep count. DISASM 0x57a667: edx=2096; render(edx); `inc edx`; while(edx!=0x84B).
     The inc PRECEDES the test, so rendered ids are 2096..2122 (27), NOT 2096..2123 (28);
     2123 is the exit sentinel, never rendered. Source emitted 28 incl 2123. Fixed -> 27.
  2. Geb Ratio div had a zero-guard. DISASM 0x57a733: `fld flt_641FD4; fdiv flt_641FD8;
     fstp <double>` — UNCONDITIONAL divide (5/0 -> +inf -> sprintf "inf"). Removed guard.
  3. Column loops. DISASM 0x57a7d7 `mov edx,10h`; loop fld flt[edx]; add edx,10h;
     cmp edx,1C0h; jnz. v6 STARTS AT 16 (byte-0 skipped) and runs to 432 -> 27 entries,
     reading stride-elements 1..27. Source did 28 from index 0. Fixed -> 27 entries,
     out[i] = col[i+1]. Header kStatGenColCount 28->27, +kStatGenColStart=16,
     +kStatGenHeaderCount=27.
  Goldens fixed: gui_statistics_window_test.cpp (header size 27/back 2122; ratio "inf";
  columns size 27, element-0-skipped mapping), _itest (colLeft[5]=col[6], colRight[26]=
  col[27]), _e2e (header 27, columns 27, colRight[10]=right[11]).
- **0x57a900 Statistics_BuildTaxPlan** — VERIFIED-1:1. DISASM 0x57a932: 17 rows (edx 0..16),
  round = qword_13CE852 - 0x10 + edx (= world::StatisticsTaxRowRound), amount =
  dword_12350E0[i] (4-byte stride). Matches. "Runde %i:" is the recoverable %i expansion;
  the trailing "   %T$N" are engine tokens (header-documented).

### storage_dialog.cpp
- **0x545fc8 StorageDialog_BuildNewSlot** — VERIFIED-1:1. single(*a1==278): cap[578]>a4
  -> width need; two-dim: cap[576]>a4 -> width (else 268), cap[577]>a3 -> depth (else 269).
  Branch conditions + cap/used comparisons match.
- **0x545fc8 StorageDialog_DispatchNewSlot** — VERIFIED-1:1 (model). clicked width/depth obj
  -> kWidth/kDepth; else cancel (loop-end on ChildObjectId/v5/1155 == 62D22C).
- **0x5461b0 StorageDialog_DispatchOptions** — BOUNDARY (command codec, Rule 8). Button-id
  -> intent (okObj enlarge 8000 / buyObj buy 12800, buy gated on 0x80 flag) is recovered;
  the full EnqueueCmd15 + packet-status poll + family-record update + SlotReset28 flow is
  the command-codec/engine boundary, modeled via StorageCommandSink.

### talent_dialog.cpp
- **0x546ce8 TalentDialog_PointsToLevel** — VERIFIED-1:1. Ladder >=0x2A/0x54/0x7E/0xA8/0xD2
  -> levels {1,2,3,5,7,10}. Confirmed against the v3 cmp chain.
- **0x546ce8 TalentDialog_Build** — FIXED (1 divergence). The 4803/4804 status discriminator.
  DISASM 0x5471c0: `cmp [var_34],0; jz -> 4804`. var_34 = v37 = the debug-adjust term
  (((DispatchByType & 2)==0) - 1), i.e. debugAdjust != 0 -> 4803, else 4804. Source used
  `hasTrainingFlag` (wrong; dword_12CE919 only selects the 525/560 arg base inside 4803).
  Fixed -> `(debugAdjust != 0)`. Golden in gui_dialogs2_test.cpp updated: drive 4803 via
  debugAdjust=-1 (requiredLevel 3->2, trained level 3->2). trainer(4806)/maxed>=0xFC(4805)/
  level>avail(4807) order verified.
- **0x546ce8 TalentDialog_Dispatch** — VERIFIED-1:1 (model). 1210+canTrain -> Train(building,
  level) [op90 -level via sink]; 1155 -> cancel.

## Counts
- Functions diffed: 11 provenance'd (across 5 files; savebrowser has 0).
- VERIFIED-1:1: 6   FIXED: 3 (Stammbaum_BuildLayout, Statistics_BuildGeneralPlan,
  TalentDialog_Build)   BOUNDARY: 1 (StorageDialog_DispatchOptions, command codec)
- Distinct divergence classes fixed: missing fixed-point centering term (+nw/2);
  portrait-sprite anchor; array read-order; pre-test increment loop-count (off-by-one);
  loop start offset (skipped element); unconditional x87 fdiv vs guarded; status-line
  discriminator (debugAdjust vs flag).
- Tests: gui_statistics_window_{test,itest,e2e}, gui_dialogs2_{test,e2e},
  gui_trade_dialogs_{test,e2e}, uipanels_wave20_test, world_stammbaum_tree_test — ALL GREEN.
