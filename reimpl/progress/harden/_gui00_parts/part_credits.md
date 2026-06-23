# Hardening — gui/credits.cpp + gui/credits_run.cpp

Scope: src/gui/credits.cpp, src/gui/credits_run.cpp (+ headers + tests).
Method: decompile + disasm (reference of record) every provenance address; diff line-for-line.

## Functions verified

### credits_run.cpp — Menu_RunCreditsScroll @0x56e524 — FIXED
- Fade-in spin `while((*rec&4)==0) RunFrameLoop` — VERIFIED (disasm 0x56e55a..0x56e56f).
- `if(rec){Unregister; Register(...,60,10)}` — VERIFIED (0x56e574/0x56e68d).
- RenderEntityScene / RenderEntityList(1792=0x700) — VERIFIED (0x56e57c/0x56e581/0x56e58b).
- **Window_Create args — FIXED.** Disasm 0x56e590..0x56e5a2:
  a1@eax=0x64=100 (x), a2@dx=0 (y), a3@cx=`dword_69FFB8>>16`=screenH (0x56e594 `mov ecx,dword_69FFB8+2`; 0x56e59f `sar ecx,10h`), a4@bx=0x258=600 (0x56e586), a5(stack)=0x10=16 (kind).
  Hex-Rays pseudocode mislabels a3 as `dword_69FFBC` (screenW). Source passed `st.screenW`;
  corrected to `st.screenH`. Golden `gui_credits_run_test.cpp` `wcW` 800->600 updated with
  cited evidence; `rec->windowW` set from `st.screenH`.
- PositionAtCoord(win,1) — VERIFIED (0x56e5a7/0x56e5b0).
- Text_RenderRichString(0x1BDF=7135) — VERIFIED (0x56e5b5/0x56e5ba).
- Initial offset `*(win+248h)= -(screenH)` — VERIFIED (0x56e5bf `dword_69FFB8+2`; sar 10h; neg).
  Credits_InitialOffset(screenH) = -screenH matches.
- scrollActive dword_62D314=1 / =0 on exit — VERIFIED (0x56e5f7 / 0x56e7b2).
- Crawl ESC/click: `if(dword_672230||byte_67225C) dword_631614=1` checked at loop top — VERIFIED
  (0x56e618/0x56e6bd/0x56e6c3/0x56e624).
- Offset advance: `idiv edi`(SIGNED frame/step); `test edx,edx; jnz; inc [ecx+248h]` i.e.
  `if(frame%step==0)++offset` — VERIFIED (0x56e632..0x56e641). C++ signed `%` matches.
- Completion predicate: `fild word[+0Ah]`(textHeight i16) * `dbl_625324` + `fild dword[+248h]`(offset i32),
  `fcompp` vs `fild dword[+244h]`(textBottom i32), `jnb` skips -> close set when
  `textBottom < textHeight*scale + offset` — VERIFIED (0x56e64c..0x56e66b). Source predicate identical.
  x87 80-bit accumulation modeled with double: exact (scale=1.5 exact, small-int operands).
- Ramp `step`: `cmp edx,42A00000h(80.0f) jl` / `cmp edx,41F00000h(30.0f) jl` -> {>=80:4, >=30:2, else:1}
  — VERIFIED (0x56e675..0x56e6eb / 0x56e75c..). Signed cmp on float bits == float order for >=0. Matches.
- Fade-out loop (0x56e70d): Register(...,60,1); `for(;(*rec&4)==0;++v5){RunFrameLoop; advance; ramp;}`
  — no ESC/completion check inside — VERIFIED (0x56e719..0x56e76f). Source matches.
- Exit: WindowRemoveIfActive(win); RenderEntityList(1773=0x6ED); if(rec2)Unregister; Register(...,60,10)
  — VERIFIED (0x56e779/0x56e783/0x56e78a/0x56e7ab).

### credits_run.cpp — Menu_RunCreditsWindow @0x529c30 — VERIFIED-1:1
- Scene flags 69FF88=0, 69FF80=1, 69FF84=1, 69FF8C=2 — VERIFIED (0x529c4d..0x529c69).
- Window_Create: eax=0x20=32, dx=0x60=96, cx=0x190=400, bx=0x26C=620, stack=0x15=21 — VERIFIED (0x529c6f).
- Text_RenderCreditsBlock(0x15C8=5576, win) — VERIFIED (0x529c80).
- Loop: `while(RunFrameLoop)` then `cmp byte_67225C,1; jnz` -> close only when ESC==1 (exact,
  NOT click; window variant ignores dword_672230) — VERIFIED (0x529c9a/0x529ca1/0x529ca3).
- Exit: WindowRemoveIfActive — VERIFIED (0x529cad). (No frame counter in original; source adds
  winVarFrames for tests only — harmless.)

### credits.cpp — standalone model (host-based isolation API)
- Credits_ScrollStep @0x56e681/0x56e768 — VERIFIED-1:1 (ramp thresholds 80/30; bytes 42A00000/41F00000).
- Credits_AdvanceOffset @0x56e635 — VERIFIED-1:1 (signed %, step!=0 guard never triggers; div-by-0 n/a).
- Credits_ScrollComplete @0x56e669 — VERIFIED-1:1 (textBottom < textHeight*scale + offset).
- Credits_InitialOffset @0x56e5d2 — VERIFIED-1:1 (-screenH).
- Credits_RunWindow @0x529c30 / Credits_RunScrollLoop @0x56e524 — host-loop wrappers
  (RunFrame/CloseRequested are genuine boundaries: RunFrameLoop frame pump + fade poll).
  Loop shape (advance-then-complete-then-ramp, close arms next RunFrame) matches original. BOUNDARY.

## Constants verified via MCP
- dbl_625324 = 0x3FF8000000000000 = **1.5** (get_bytes/get_global_value) -> kCreditsLineScale=1.5. OK.
- 0x42A00000 = 80.0f, 0x41F00000 = 30.0f (ramp thresholds in disasm). OK.
- RenderEntityList ids: 1792=0x700 (build), 1773=0x6ED (exit). OK.
- Text ids: 0x1BDF=7135 (rich crawl), 0x15C8=5576 (block). OK.

## Float->int sites
- No float->int truncation/round sites in these functions. All numeric flow is int->double via
  `fild` for the completion predicate (no fistp / no ConvertX). No divergence risk.

## Build / test
- credits.cpp + credits_run.cpp: g++ -std=c++17 -fsyntax-only clean.
- gui_credits_run_test (Test #236): PASS (1/1) after golden fix.
- gui_credits_run_itest / e2e could not be linked: pre-existing UNRELATED build error in
  src/gui/tooltip_build.cpp (BuildingTooltipLayout::iconObjectId) — another agent's file,
  outside this scope. My two source files and their unit test build/pass.

## Summary
2 functions FIXED (1 real arg-mapping bug: Window_Create a3 = screenH not screenW, Hex-Rays
mislabel; source + golden corrected with disasm evidence). Window variant + all four model
functions VERIFIED-1:1. 1 genuine host boundary (frame pump / fade poll) retained.
