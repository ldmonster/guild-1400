# Harden wave — book.cpp / book_reader.cpp / chatconsole.cpp

Scope: every provenance-tagged function in the three files, diffed line-for-line
against the decompiled/disassembled original. Result: **all VERIFIED-1:1, zero
source edits required.** All recovered constants/tables re-checked with get_bytes.

## src/gui/book.cpp
No `gilde.exe 0x` / `@0x` provenance comments in this file. It is the abstracted
logical `Book` model (clamp + flip math). Its clamp logic was cross-checked against
the real `VIBE_Book_TurnPage` @0x4be60c (see below) and matches.

- **Book_TurnPage (clamp model, ref 0x4be60c)** — VERIFIED-1:1.
  Original @0x4be60c clamp (disasm 0x4be62a/0x4be645/0x4be77d):
  `if (delta<0) reject when current-2<0; else reject when current+2>=pageCount;`
  then `*(u8*)(rec+624) += delta`. `kBookFlipStep = 2` confirmed (forward
  TurnPageForward @0x4be8b8 → TurnPage(a1,2); backward @0x4be8c8 → TurnPage(a1,-2)).
  The original's heavy mesh/audio/form work (baf anim names "Buch_vorblaettern" /
  "Buch_zurueckblaettern", VIBE_Character_*, VIBE_Audio_StartVoiceSample) is the
  Vulkan/SDL/asset boundary; the clamp+increment state transition is reproduced exactly.
- **Book_VisiblePages / Book_IsPageVisible** — derived helpers; visibility predicate
  `i>=current && i<=current+1` matches RefreshVisiblePages' hide test (inverted).
- **TurnPageForward/Backward wrappers** — VERIFIED-1:1 vs 0x4be8b8 / 0x4be8c8 (thin ±2).

## src/gui/book_reader.cpp

- **0x4be494 VIBE_Book_RefreshVisiblePages** — VERIFIED-1:1.
  Op order matches: hide-loop (i<cur || i>cur+1 → SetChildrenVisible 0) → prev-flip
  (`+620 != -1 && == dword_62D22C && dword_67221C` → TurnPage(-2)) → next-flip
  (`+616` → TurnPage(+2)) → re-show window `v9=cur+byte_631E60; while(v9<pageCount &&
  v9<=cur+byte_631E60)`. Parity `byte_631E60` boot=0x0 (get_bytes 0x631E60),
  C++ `s_showParity=0` ✓; `^=1` each call ✓. The original's re-show op is
  VIBE_DecompressGameState (a render/decompress boundary op, @0x41ceb4 — heavy
  per-entity world rebuild); C++ models it as host.SetPageVisible(...,1) per the
  BookHost boundary. prev=+620(−2)/next=+616(+2) field/sign mapping confirmed against
  Book_Close destroy order. BOUNDARY: SetChildrenVisible / DecompressGameState.
- **0x4be3d0 VIBE_Book_Close** — VERIFIED-1:1.
  Disasm-confirmed page loop (0x4be3ef) DOES increment (inc ecx / add edx,4 — the
  Hex-Rays missing-increment is an artifact). Per page: BroadcastClickResult(+552[i])
  + Form_Destroy(+552[i]) — both bundled into host.DestroyPageForm (documented in
  book_reader.h). Then nextButtonId(+616)≠-1→DestroyByType, prevButtonId(+620)≠-1→
  DestroyByType, savedZEnable(+626)→SetZEnable(1), StartVoiceSample(close), FreeDebug.
  C++ order matches. BOUNDARY: mesh/character/audio/widget teardown.
  (Nit: book_reader.h comment labels +626 "byte_64A351"; it is a record field at +626,
   offset is correct — not changed.)
- **0x596ee8 VIBE_Interaction_HandleBookPageTurn** — VERIFIED-1:1.
  `cursorZone==2 && book → TurnPageForward; ==3 && book → TurnPageBackward; else 0`.
  Original reads off_5953F0[15]=zone, off_5953F0[19]=book ptr (the global state is the
  boundary; the branch logic + return-0-always is reproduced exactly).
- **0x4be8d8 VIBE_Scroll_Open** — VERIFIED-1:1.
  `if form!=-1 return -1; form=OpenForm("misc\\scroll_perga"); CenterChildWindows;
   if form==-1 return -1; SelectWindow(form,0); left=AddSprite(0,1618);
   right=AddSprite(465,1618); return form`. Disasm 0x4be924/0x4be93d confirms the two
   AddToWindow calls: edx=0/0x1D1(465)=x, ebx=0x652(1618)=gfx, ecx=window. So
   kScrollRightX=465 ✓, kScrollSpriteGfx=1618 ✓ (in ebx, not dropped). Asset string
   bytes @0x61e2c8 = "misc\\scroll_perga" ✓.
- **0x4be960 VIBE_Scroll_Close** — VERIFIED-1:1.
  `if form!=-1 { Form_Destroy(form); form=left=right = (discarded ecx, =-1 sentinel) }`.
  C++ sets all three to -1 ✓.
- **0x4be990 VIBE_Scroll_UpdateAnimation** — VERIFIED-1:1.
  `if form==-1 return; SelectWindow(form,1); v0 = *(dword_62D298+584)/6%12; if v0<0 v0=0;`
  set both sprites' frame field (+116). Signed `/6 %12` then clamp; kScrollAnimDivisor=6,
  kScrollAnimFrames=12 ✓. Timer source dword_62D298+584 is the boundary input.

## src/gui/chatconsole.cpp

- **0x4bfc48 VIBE_ChatConsole_BuildWindow** — VERIFIED-1:1 (state model).
  Build loop: scene table byte_12CE912 stride 268, end at 205824 (768 entries), tag
  `==7` → toggle, capacity v29[8]=kChatChannels ✓. Template dword_4AD48C qmemcpy →
  get_bytes 0x4AD48C = 32×0xFF = 8 dwords −1 → kChatChannelEmpty fill ✓. Persisted
  toggles byte_631E80[8] get_bytes 0x631E80 = {1,1,1,1,1,1,1,1} → g_chatRecipientState ✓.
  Colour fmt "$%iFF " bytes @0x61e494 ✓, suffix "$A" @0x61e49c ✓ (kChatLineSuffix),
  colour value `dword_12CE964[134*word_63CC5C]-1342` (player table, −1342 documented) is
  the boundary input (hooks.speakerColour). Close-persist loop (0x4bfd4d) writes all 8
  unconditionally `if v29[i]!=-1`. C++ structure matches. BOUNDARY: GameTick_Finalize
  form create, Window/Form/Object/Widget GUI ops, RunFrameLoop @0x4c09a0 (SDL pump),
  RequestBuildOp75 command sink, the 25-dword recipient blob.
- **0x53629c VIBE_DebugList_AppendId** — VERIFIED-1:1.
  `if (count >= 128) return 0; ids[count++] = a1; return 1;` (dword_63CD40 counter,
  dword_122FCC0[] storage). kDebugListCapacity=128 ✓.
- **ChatConsole_AssembleLine / ChatConsole class** — VERIFIED. AssembleLine reproduces
  the "$%iFF " + text + "$A" assembly used at 0x4bfed2/0x4bff09. The ChatConsole class
  (visible-rows / scrollback) is a portable view-model around the line buffer.

## Build / tests
- All three target files **compile clean** (g++ -std=c++17 -Isrc -Iinclude -Ithird_party
  -fsyntax-only → 0 errors each). Object files present and current in build/.
- **No source or golden edits were made** — every function was already faithful 1:1.
- Test suites covering these files: gui_book_reader_test/itest/e2e_test,
  chattrade_wave22_test, gui_panels_test, app_wiring4_test (+ e2e variants).
  NOTE: at run time the shared `guild` library fails to relink due to a PRE-EXISTING,
  OUT-OF-SCOPE break in src/gui/tooltip_build.cpp (`BuildingTooltipLayout` has no member
  `iconObjectId` — a concurrent agent's WIP). This is unrelated to book/book_reader/
  chatconsole; my files are not involved. Once tooltip_build.cpp is fixed by its owner
  the suites link with no changes needed from this wave.

## Constants/tables re-verified via get_bytes
- 0x4AD48C: 32 bytes 0xFF (chat channel template, 8× −1)
- 0x631E80: {1,1,1,1,1,1,1,1} (chat recipient persisted state boot)
- 0x631E60: 0x00 (book re-show parity boot)
- 0x61e494: "$%iFF " ; 0x61e49c: "$A" ; 0x61e2c8: "misc\\scroll_perga"
- Scroll AddToWindow args (disasm 0x4be8d8): x∈{0,465}, gfx=1618
