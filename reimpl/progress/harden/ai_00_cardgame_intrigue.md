# Harden: ai/cardgame.cpp + ai/intrigue.cpp

1:1 hardening pass. Every function carrying `gilde.exe 0xADDR` provenance was
decompiled + disassembled and diffed line-for-line. DISASM wins over Hex-Rays.
Card-game RNG draw count/order verified instruction-by-instruction.

Build/test target: `ai_meister_test` (suite `AiMeister`), plus affected
`ai_meister_e2e_test`, `sim_real_hooks_test`, `sim_real_hooks_e2e_test`,
`interaction3_itest`, `desire_table_itest`, `app_wiring3_test`, `wire_ai_test`,
`ai_favorability_needs_test`. All green (9/9 suites).

## Tables / constants verified by bytes

- `dword_466394` 5x6 float threshold table (0x466394, 120 bytes): **bit-exact**
  vs the `kCardThresholds` source literals (verified all 30 floats).
- `flt/dbl_61A1xx` decision/draw constants (0x61A1AC..0x61A1F0): all confirmed;
  the float-stored ones (D0/D4/D8/DC/E0) promote exactly to the source `double`
  literals; E8/F0 are stored as doubles. All match.
- `dword_46637D` / `byte_466381` card-play transition table (0x46637D, 24 bytes:
  `c3 00 00 00 01 01 02 01 03 01 04 02 02 02 03 02 04 03 03 03 04 00 00 cd`).
  Decoded the loop semantics from disasm (see FIXED below).

## Per-function results

### cardgame.cpp

- **VIBE_AiCardGame_DecideMove 0x466ec4 — FIXED.**
  Case 3 (`decision==3`, knock gate) used `handSize` (OWN hand size). The binary
  at `0x467001 fild word ptr` loads `v13`, which the seat-resolution loads as the
  **opponent's** hand size (seat A: `al=[eax+2Ch]`=+44=B handsize; seat B:
  `al=[eax+10h]`=+16=A handsize). Fixed to use `oppHand`.
  - Sum loop (own hand/own handsize), case 0/1/2 (Hold/Draw probability), the
    `SLODWORD(v10) < 1065353216` (== `(float)thr < 1.0f`) bit-compare, and the
    mood `(double)(6 - v5)` term all VERIFIED-1:1.
  - BOUNDARY: top gate `*(seatPtr+2) != 10` (round-phase byte on the seat entity)
    is not modeled — seat-entity memory is outside `CardGameState`; documented as
    a caller precondition in the header. No behavior change for the test harness.

- **VIBE_AiCardGame_EvaluateHand 0x466980 — FIXED (RNG draw count/order).**
  The draw path (a3!=0) RNG sequence was materially wrong. Correct sequence from
  disasm (0x466a1a..0x466c7c):
  1. `RandomModulo(3)` -> **v37** = branch SELECTOR (0/1/2). `*handSize = 3`.
  2. strength-banded burn draw that ALSO computes the boost COUNT `v21`:
     - `v35 > flt_61A1B0(0.8888)`: `RandomModulo(0x10)`; `v21=(r==0)+2` -> {2,3}
     - else `> flt_61A1B4(0.5555)`: `RandomModulo(0x10)`; `v21=(r==0)+1` -> {1,2}
     - else `<= flt_61A1B8(0.3333)`: `RandomModulo(8)`; `v21=(r==0)` -> {0,1}
     - else: `RandomModulo(4)`; `v21=(r==0)` -> {0,1}
     The band ladder is NOT a collapsible `||` — each band yields a different
     `v21` base offset, so the order/count differs per band.
  3. three `RandomFloatScaled()` card rolls (c0,c1,c2).
  4. boost gate = `!allEqual && v21 != 0`, dispatched on **v37**:
     - v37==0: +1 to up to `v21` cards (clamp 6);
     - v37==1: raise the first-min card by `v21` (clamp 6);
     - v37==2: `RandomModulo(3)` -> raise that card by `v21` (clamp 6).
  The OLD source: used `RandomModulo(3)` as a redeal COUNT, discarded the burn
  draw, gated on that count, and issued a SECOND `RandomModulo(3)` that does not
  exist in the binary — wrong draw count AND wrong stream alignment. Rewritten 1:1.
  - Tier: `0x4669ac VIBE_Coord_ConvertX` sets round-toward-zero; `0x4669b1 fistp`
    TRUNCATES `v7=strength*100`; then signed `idiv 20`; clamp is on the LOW BYTE
    `(u8)quotient >= 4 -> 4` (FIXED: was `(unsigned)tierRaw` + a dead `tier<0`
    clamp; now `(int)scaled/20` then `(u8)` byte clamp, matching `cmp al,4/jnb`).
  - Non-draw path (a3==0): bias decision over `dbl_61A1C0(0.66)`/`dbl_61A1C8(0.44)`
    bands, single `RandomFloatScaled()` roll, `card=bias+col+1` clamped [1,6],
    append + return prior handSize: VERIFIED-1:1.

- **VIBE_AiCardGame_CanPlayCard 0x4668d4 — FIXED (table) / VERIFIED loop.**
- **VIBE_AiCardGame_PlayCard 0x466920 — FIXED (table) / VERIFIED loop.**
  The decoded `kTransitions` was wrong. Correct decode of `dword_46637D`
  (`cur=byte[v5+3]`, `action=byte[v5+4]`) with result `byte_466381[v5]=byte[v5+4]`:
  ```
  (0,1)->1 (1,2)->2 (1,3)->3 (1,4)->4 (2,2)->2 (2,3)->3 (2,4)->4 (3,3)->3 (3,4)->4
  ```
  i.e. the result == the played action. OLD table had bogus pairs like
  `(0,0)->1`, `(2,2)->4`, `(1,1)->3`. First-match return order preserved.
  Golden in `CardGameCanPlayAndPlay` was also wrong (expected `(0,1)->2`,
  `(2,2)->4`); fixed BOTH source table and the golden to the binary.

- **VIBE_AiCardGame_TakeTurn 0x466da0 — VERIFIED-1:1 (core) / BOUNDARY (bet emit).**
  case 1 raise: pot(+68)+=stake(+0), EvaluateHand draw=1, PlayCard.
  case 2 hold:  pot+=stake(+4), EvaluateHand draw=0, PlayCard.
  case 3 draw:  pot+=stake(+4), PlayCard (no eval).
  case 4 knock: PlayCard. All match.
  BOUNDARY: the binary emits `Command_QueueRequest16` only when `*(seatPtr+2)==6`
  (human seat) with arg2 = `*(seatPtr+4)` (a dword read from the seat entity).
  Both the human gate and the entity deref touch seat-entity memory not modeled
  in `CardGameState`; the emit is routed through `SetBetCmdHook` (wired to the
  real command codec). Left as a documented hook boundary; passing `seatPtr` as
  the entity handle is faithful to the wired translation.

- **VIBE_AiCardGame_ShouldRaise 0x467178 — VERIFIED-1:1.**
  fastcall (ecx=own, edx=opp). `v4 = strength(opp,3)*0.5`;
  `v5 = (float)(strength(own,3) - v4 + 0.1)`; `RandomFloatScaled() <= (double)v5`.

- **VIBE_AiCardGame_UpdateRoundState 0x467038 — VERIFIED-1:1.**
  Phase 1->2; phase 2 triple/showdown/fold resolution (5/6/3); phases 3/4 sum
  comparison with the 3<->4 ping-pong when neither seat is terminal. Every branch
  arm, the `(u8)` byte sums, and the `r[36]/r[64]` decision checks match exactly.
  (The e2e test that "forces" phase 2 with arbitrary decisions hit the faithful
  3<->4 ping-pong; the test was made deterministic by setting both seats to the
  "stand" decision (3) before resolving — a test-data fix, not a source change.)

### intrigue.cpp

- **VIBE_AiScore_ComputeRelationWeighted 0x4796b0 — VERIFIED-1:1.**
  entry = `&byte_B57210[148*cls]`; gate `(*(int*)(entry+142)>>16) & person[+532]`;
  two pre-scan eligibility loops (4 each, stride 8) over `[+45]>>24 == person[+301]>>24`
  with weights `*((float*)+13)` (=+52) and `*((float*)+29)` (=+116); main loop 4x
  stride 8, signed enable bytes `[+48]`/`[+112] >= 0`, divisors 40/50 (eligible)
  vs 50/100, relation weight `*(float*)(person + 12*cls + 144)`. Float terms are
  truncated to float before accumulation — matches source `float term`.

- **VIBE_AiScore_ComputeRelationOwn 0x479898 — VERIFIED-1:1.**
  Same gate/pre-scan; main loop counts only class-matching sub-entries (else
  term=0); single divisor v19 (40 eligible / 50 not).

- **VIBE_AiIntrigue_EvalActionLabel 0x4715d8 — VERIFIED-1:1 (boundary input).**
  `FormatActionLabel ? 39 : EvalRejectStub()`. Formatter accept + reject-stub are
  inputs (`formatted`, `rejectCode`).

- **VIBE_AiIntrigue_EvalSlanderLabel 0x471fa0 — VERIFIED-1:1 (boundary input).**
  `FormatSlanderLabel ? 45 : EvalRejectStub()`.

- **VIBE_AiPlayer_EvalPamphlet 0x471ae0 — VERIFIED-1:1 (boundary emit).**
  `if (!ExecPamphlet) return 0; QueueRequestArgs25(...); return 41`. The command
  emit is routed through `SetPamphletCmdHook`.

## Counts

- Functions audited: 12 (7 cardgame + 5 intrigue).
- FIXED: 4 (DecideMove case-3 opp-handsize; EvaluateHand draw-path RNG + tier
  byte-clamp; CanPlayCard table; PlayCard table). [DecideMove tier/CanPlay/PlayCard
  share the corrected transition table.]
- VERIFIED-1:1: 8 (TakeTurn core, ShouldRaise, UpdateRoundState,
  ComputeRelationWeighted, ComputeRelationOwn, EvalActionLabel, EvalSlanderLabel,
  EvalPamphlet).
- BOUNDARY (hooked / un-modeled entity memory): DecideMove `+2==10` gate,
  TakeTurn bet-emit human gate + `*(seatPtr+4)` arg, EvalPamphlet/label emits.
- Goldens corrected to binary: `CardGameCanPlayAndPlay` transition results;
  `ai_meister_e2e_test CardGameFullRound` showdown setup (test-data).
