# HARDEN sim_11 — 1:1 verification report

Chunk: 22 files (`src/sim/` script cluster + terrain/universe/vegetation/trade/turn + wire glue).
Method: every provenance-carrying function decompiled via the IDA MCP and diffed
line-for-line; const tables byte-diffed with `get_bytes`. Fixes below cite binary
addresses as evidence. Everything else is VERIFIED-1:1 (no churn).

## FIXED — proven divergences

### src/sim/script_compiler.cpp + script_compiler.h (ScriptExecutor rewrite)
The live source-text executor was structurally re-verified against the binary and
several proven divergences fixed by rebuilding it on the binary's exact block-scope
frame machinery:

1. **`EvalExpression` — parenthesised subexpressions (0x443ff0 @0x444447→0x44455b).**
   Binary: class-1 sub-12 `(` recurses (`v2 = EvaluateExpression(v43)`) and folds the
   group's value as the operand at LABEL_18. Old code dropped the `(` token and
   returned early at the inner `)`, so `(2+3)*4` evaluated to 5 (binary: 20) and a
   parenthesised argument like `Cmd((1+2)*3, 4)` truncated the arg list. Fixed:
   sub-12 recursion + fold.
2. **`EvalExpression` — float literals (0x444331).** Binary: `cmp al,6 / jz` jumps a
   class-6 (float-literal) token straight to the fold ladder **without loading an
   operand** — the stale `v2` folds. Old code folded the float's IEEE bit pattern
   (`x = 3.14` gave 0x4048F5C3; binary gives 0). Fixed: operand (`v2`) is now
   loop-carried; class 6 loads nothing. (No real `.esc` in the shipped assets uses
   float literals — greps over `unpacked_resources/Scripts` found none.)
3. **`EvalExpression` — pending-operator carry (LABEL_22 `v45 = v1`).** Binary sets
   the pending fold operator to the *current* token's sub-code each iteration (0 after
   an operand). Old code reset to `kOpOperand` after each fold, so consecutive
   operands re-seeded the accumulator (binary keeps the first). Fixed; the fold `switch`
   now also has the binary's `default: no fold` arm.
4. **`EvalExpression` — array-index bounds check (0x443ff0 class-2 arm).** Binary
   reports "Array index out of bounce" and Finishes the script when
   `idx > count-1 || idx < 0`; old code silently clamped. Fixed: `finished_ = true; return 0`.
5. **`if`/`while` handlers (0x4427b4 / 0x44259c) — exact frame semantics.** Both now
   build 12-byte frame slots exactly like the binary: slot array indexed by count,
   pushed at `slots[count]` then `count++` (cap 8, "Too many looplevels" → count--);
   while-true slots are memset **after** the condition write (condition slot stays 0,
   0x44259c write order); if-slots keep type 2 + restart + condition; `restart` =
   source offset of the keyword itself (binary computes `cursor - strlen(keyword)`).
   if-false leaves the cursor **ON** the closing `}`/`;` (the `--cursor` at
   0x442958/0x442988) so the statement loop pops the frame.
6. **`}` pop (0x444bd0).** Every `}` pops the top frame; a popped type-1 (while)
   frame with a live restart rewinds the cursor so the `while` keyword is re-read and
   re-tested. Replaced the previous braceDepth/bodyDepth-matching model (which was an
   approximation added when if-statements didn't push frames).
7. **`;` frame pop (0x444bd0 LABEL_6 / 0x443ff0 LABEL_23).** New `PopFrameAtSemicolon`:
   when `count > 1` and the top frame is a single-statement (noBrace) frame, `;` pops
   it; a popped while frame rewinds. This makes non-braced `while (c) x = x+1;`
   bodies re-test exactly as the binary does (and exactly *not* at top level, where
   the binary's `count > 1` gate also fails). Raw out-of-band `;` consumption after
   command calls / `++`/`--` was removed — the `;` now flows through the statement
   loop as in the binary.
8. **`else` — DispatchTokenBranch (0x442ac8).** Binary reads
   `frames[count+1].condition` — one slot **above** the just-popped frame (disasm
   0x442ae7: `cmp [edx + 12*(count+1) + 52], 0` vs pushes writing `12*count + 52`;
   an original off-by-one, preserved). Condition 0 → return, the else body
   **executes**; nonzero → skip: `SkipBraceBlock` only when the next token is the
   keyword `else` (10,4) — dead in practice — otherwise `SkipToSemicolon` (one
   statement). Old code unconditionally skipped the else body. Slots persist after
   pops (zero-initialised — the deterministic stand-in for the binary's
   uninitialised heap slots).
9. **`AssignVariable` dispatch by sub-code (0x4445bc).** `++`/`--`/`=` are sub-codes
   3/5/2; the token after the variable must be class 1; any other operator is a no-op
   return. Old code matched on token *text* and treated any non-`++/--` operator as `=`.
10. **Runtime declarations — ParseDeclaration (0x442d88).** `kTokDeclare` at statement
    position now performs the binary's runtime declaration (name must be class-0;
    `;` scalar / `=` with class-5 int-literal initialiser stored / `[n]` array /
    `(` function-def → cursor restored before the `(`), instead of skip-to-`;`.
    All real-asset initialisers are `= 0` (grep), matching the zeroed arena either way.
11. **`SkipBraceBlock` (0x4416c4-exact).** Raw char scan counting from the cursor:
    `{`++ / `}` that drops depth ≤ 0 stops just past it (old version pre-scanned to
    the first `{`, which mis-handled a stray `}` before it).

Deliberate deviations (documented in-code): the `/ 0` fold guard (binary raises x86
#DE; C++ UB), and class-0 "Unknown symbol" tokens are tolerated instead of
ReportError+Finish because the standalone lexer resolves fewer names than the live
engine (include refs / unregistered commands) — a faithful kill would end scripts the
binary runs. Class-7 string operands read 0 (binary: pointer into the UTF-16 scratch
`unk_7674E0`; no flat address space here).

### src/sim/script_vm.cpp — ScriptVm::EvalExpression (0x443ff0)
Same evaluator-mechanics fixes applied to the token-model VM: loop-carried operand,
class-6 no-load (0x444331), `pendOp = cur` carry, sub-12 paren recursion, terminator
sub-codes 10/11/13 (return acc; previously fell into the fold), class-0xB exact
semantics (0x444350: `dword_62E8C8 = value`, then LABEL_21 seeds acc from the STALE
operand with `v45 = 2` — previously used the label id as the operand), and class-12
fold-then-return.

### src/sim/script_import4.cpp — RunWithArgs (0x443a90)
String-arg scratch placement: the binary advances the `unk_7675E0` cursor by 96 bytes
on **every** argument (`v16 += 96` in the 0x443b06 loop tail), so a string arg lands
at `96 * argIndex` — matching EnterFunction's per-arg 96-stride readback (0x4431dc).
The reimpl packed strings densely (`96 * stringIndex`), which would mis-slot a string
that isn't the first argument. Fixed old→new: `strScratch + 96*slot` → `strScratch + 96*v15`.

## VERIFIED-1:1 (no changes needed)

| File | Functions (address — verdict) |
|---|---|
| recruit.cpp | 0x55d5c0 CheckRecruitProximity — exact (byte +8 gates, +0x5C relation, zero-extended `mov ax,[reg]` rank args, abs<5). |
| result_blit_recon.cpp | 0x423fbc Validate, 0x423fd0 Broadcast (clip [1]/[2]/[9]..[12]), 0x423980 Result_Broadcast (clip order, v13/v14 write order, dual finalize/blob tail) — exact. |
| script_lexer.cpp | kCharClass @0x64A208 — **256/256 bytes byte-diffed, match**. Operator table (29 rows) + keyword table (8 rows) verified against ConsoleParseLine @0x4453a4 dest addresses and the source strings @0x6184C0../0x618014/0x6183D4/0x618528.. (all spellings + sub-codes match). ClassifyTypeKeyword @0x441788 (int=1,float=7,void=5,string=6,{=8,}=9,byte/char=2) exact. NextToken @0x441974 classification chain + `3.14` digit-dot exception verified. |
| script_symbols.cpp | 0x442c90 DefineVariable (elem sizes 4/1/96/4; `void` has no case in the binary — register-garbage size; 4 is the deterministic stand-in), 0x4414d4/0x4415f8 lookups (linear StrCmp; the dword_62E8C8 include-ctx override and call-record local slots are documented boundaries), 0x4459bc GetVariableAddress (`+= 4*count` for every type) — verified. |
| script_run.cpp | 0x441f30 StripCommentsAndWhitespace (ctrl-char classes 9/0A/0D/10→space, 0x0E/0x0F/<9 kept, `/*..*/` blank-then-collapse ≡ single space, space-run collapse), 0x4424e0 prefix `x:\engine\gfx\scripts\` (bytes @0x62e7a4 checked), 0x4421f0 read cap 0xFA00, 0x44396c/0x4aa01c wrappers — verified. |
| script_console.cpp | 0x4453a4 ConsoleParseLine (alloc sizes 0x3400/0x20/0x400/0x1080/0x50C00, per-ctx `-2456` handle=-1 stamp, 39 token copies), 0x444774 CallFunction (cursor++ at entry, per-arg eval + typed slots, `argc && last != ')'` error, zero-arg second ++, Sleep/stmt-1 yield snapshot + type-6 freelist copy), 0x4450e0 Step (depth stack, eligibility ladder, slot switch, invoke-vs-statement loop), 0x4445bc AssignVariable raw form, 0x4429c4 ParseInclude (8 slots @+2492), 0x4415bc LookupInclude — all verified against decompiles. |
| script_import.cpp | 0x5e3bd0 ReadToken (43 EOF / >0x3A→39), 0x5e3c0c FindTokenHandler (12-byte stride, 40-sentinel, <64), 0x5e3c44 ParseBlock (recurse condition `!fn || child` ≡ reimpl `fn && !child` leaf; note: the binary's +4 field is a leaf-fn/child-table UNION selected by +8 — reimpl splits it into two fields, equivalent when exactly one is set; open-token(40) leaf return value propagates to AL in the binary, discarded here — callers only compare terminators), 0x5e3d40/0x5e3da0/0x5e3e00 string fields (+0/+64/+128, dot-cut), 0x5e3e64/0x5e3e8c bytes (+192/+193), 0x5e3eb4/0x5e3f54 flag unpackers (bit maps exact, stride 224), 0x442174/0x4421b8/0x445b70 scans (strides 2584/52, bounds 330752/13312), 0x44191c FindLogicalOperator, 0x43c650/0x43c658/0x442598 stubs, 0x445d7c reset — verified. |
| script_import2.cpp | 0x43c708 CmdSleep (+2568 wake vs dword_62EB38, /14, -1 forever), 0x43c790 KillLocalScripts (handle≠-1 & runnable & same owner & ≠self), 0x43c7ec/0x43c81c CallUserFunction[Extended], 0x43c9c0 CreateCharacter, 0x43cb28..0x43cd18 WalkToDummy family (resume via `*(dword_62E8CC+44)==fn`, +296 busy, stmtMode==1 arm), 0x43cdc8..0x43cfb0 PlayCharacterAni family (0x2E00000000 arg, +240/+304 wide copies, +240/63 +144/95 pads, +380 = dword_62E8D4) — verified. |
| script_import3.cpp | 0x445bc8 ImportCommand (0x1F name cap, dup reject, +44 slot scan, +32/+36/+48 writes), 0x441140 AddEventToken (`count+48 > capBytes` grow test, +768 cap bump, field order incl. the pre-clear of +0), **all 86 registered commands** (0x43c850/0x440df0/0x440618) programmatically diffed against the decompile — kind/argc/arg-type tuples: 0 mismatches; 8 AddEventToken SND_* handlers match. 0x43ca0c CreateCharacterAtDummy (bone-chain pos, +5/+52 name copies, yaw orient) — verified. |
| script_import4.cpp | (RunWithArgs FIXED above.) 0x442a98/aa8/ab8 mode setters, 0x5e3d2c ReadSkipValue, 0x445cfc RunByHandle, 0x445288 ShutdownEngine (free order), 0x44250c ProcessStringLiteral (`"`/`;` scans, `v8 < v7` missing-quote test, StrNCopyPad+NUL, return quote+1), 0x4413d0 ParseDeclaration (local form), 0x443084 HandleExitKeyword ("exit" StrCmp gate, 144-byte call-record scan from index 15, +2488/+164 clears, parent cursor restore), 0x443c88 ParseAndRunCall (16×64 token split, digit→ParseInt else pointer, argc switch) — verified. |
| script_recon_purchase.cpp | 0x43c690/0x43c6ac LoadAndRun wrappers — disasm-confirmed `mov eax,edx` returns the LOADED SCRIPT pointer (RunMain/RunWithArgs result discarded). 0x50588c RunPurchaseLocationScript — slot scan (32×4), state byte >1, partner = *(v6+0x184), key match a2+2 vs bld+0x30, `589*i8 + dword_13CE294` / `65*i16 + dword_13CE27C` (+1 = name), Einkauf path fmt, prev-handle Finish, RunWithArgs(script,2,bld) — verified. Boundary note: the binary's post-run stores (`ctx+2580 = bld`, `bld+0x28 = handle`, `dword_634494 = handle`) live behind the `runWithArgs2` hook contract (dead `sp_%i` sprintf omitted — its buffer is unused). |
| script_vm.cpp | 0x445250 StepAllActive / 0x445d40 CountActive / 0x445370 FreeFinished — exact predicates ((+164&1); (+0)||(+164&1); (+0)||(+128≠-1)). (Return-value of FreeFinished modeled as a count vs the binary's last-result register — callers ignore it.) 0x444f4c InvokeCommand / 0x444bd0 ExecStatement token-models: documented host-boundary models. |
| terrain_collision.cpp | 0x426e2c ScanLineHeightRange — projection (80-bit `float-origin` widened to double before ×1/scale, ConvertX truncation), 3-swap row sort, clip tests, degenerate min/max (int→float→trunc round-trip noted; identical below 2^24), flat-top pre-step of BOTH edges, general-case `k -= dBC` pre-step + bottom-step, lo/hi seed 255/0, `*out = lo*scale(+196) + base(+148)` — all verified against decompile (arg order into ScanRowHeightRange is span-normalised there, 0x426da0). 0x427370 ScanSegmentHeightRange (two triangles, clip-merge). 0x427b60 ResolveMeshAgainstTerrain — +504 parent / +460 mesh / scratch 76..140 / flt_6115B8=0.1 lerp / ±1e35 no-terrain seeds / `newY = surf - meshLow + curY` at +80 with +528 |= 4 / 500/496 sibling walk — verified. |
| trade_sell.cpp | 0x46bff0 RequestSellObjekt (kind==2 gate, ConvertX-truncated cached price — computed twice in the binary, once here; returns 14), 0x496b90 SellObjekt resolve gates (hook-phased), 0x497538 ComputeSellableAmount — ingredient gate `v4 = min(v4, effStock/ratio)` (miss→0), free = ComputeFreeCapacity(outCount*v4)/outCount, proceeds = trunc(price × **craft count v15**) (0x4976c8 fild/fmul/ConvertX confirmed), consume `ratio*v15`, add `outCount*v15` — verified. |
| turn_driver.cpp | 0x533188 BeginPlayerRound — full pass ORDER verified against the decompile (wealth grid → 768-slot flag sweep `& 0xE0874703` + kind-6 → word_63CC5C → MeisterAi ticks → expire×2 → plant growth per kind-30 → recalc production → snapshot → heavy branch `(byte_63CC28&8)||dword_764CE0==-1`: Straftat sync, per-slot (marker≠-1 && +8) ProcessPlayerTurn, Amt cascade 3/loans/wages/offices, building tasks, news, AI broadcast, city tick, city-state −1 ? sync : advance-timer, building needs, Coord27 per person (id @+4, stride 134 dwords) + (−1,−1) sentinel; else light tax(2) → accumulator clear dword_12CE8E0 stride 134 → gated SyncAllTurnStates). UI/net/voice leaves documented DEFERRED. |
| universe.cpp | Frustum weights @0x62845C byte-checked = {0.59f, 0.30f, 0.11f}; 0x5b4a24 SwitchActiveSlot formula `flt_64A070 = fov0*W0 + clipNear*W1 + fov1*W2` and the 246-dword-stride slot save/load field map verified; 0x5b5f48 default cams, 0x5b46dc InitCameraNode (near 4 / far 15, head=tail=cam), 0x5b44c4 ResetCurrentSlot (200.0f defaults), 0x5b43f0 (walk, 1023, mode), 0x5b2cd8 — verified (render leaves hooked). |
| vegetation.cpp | 0x56ec14 LoadVegetationModel — level `(stagePacked>>24)/u8@+0x43` min 1, format strings @0x625334/0x625354 byte-checked, `_ID%i` suffix, +0x218=1, placement basis offsets 0x90..0xC8 all match (144..200), map byte `*(width*B + A + mapBase)`, per-term `((double)(i16)v * coef) + acc` x87 model — verified. |
| wire_actionops.cpp | Glue; FindNearbyInRadius @0x40507c 16-hit cap (`v6 < 64` bytes) confirmed. |
| wire_ai.cpp | Glue; RandNext/MoneyConvert/MoneyMulByRate bindings — address claims consistent. |
| wire_apply_input.cpp | Glue; StrncmpN + gray-broadcast bindings (ABI notes documented). |
| wire_charaction2.cpp | Glue; RandomModulo @0x58b89c AX-truncation confirmed by disasm (`test bx,bx` / `mov cx,bx` / `idiv ecx`). |

## Test targets run (all green)

sim_script_compiler_test 87 · sim_script_compiler_e2e_test 28 · effect_script_test 67 ·
effect_script_real_esc_e2e_test 39 · scene_script_real_e2e_test 8 · sim_script_test 207 ·
script_vm_test 107 · script_import_test 69 · script_import2_test 78 · script_import3_test 64 ·
script_import4_test 73 · script_console_test 42 · script_import_e2e_test 11 ·
script_import2_e2e_test 20 · script_import3_e2e_test 21 · script_import4_e2e_test 15 ·
script_console_e2e_test 9 · script_import2_itest 17 · script_import3_itest 46 ·
script_import4_itest 11 · script_console_itest 8 · terrain_collision_test 27 ·
turn_driver_test 18 · turn_driver_e2e_test 21 · vegetation_test 27 · wire_actionops_test 18 ·
wire_ai_test 16 · wire_apply_input_test 16 · wire_charaction2_test 51 · wire_script_test 12 ·
script_recon_purchase_menu_test 69 · app_wiring_universe_e2e_test 11 ·
buildingtype_callers_test 152 · sim_person_test 61 · sim_person_e2e_test 14 ·
gametick_test 71 · gametick_e2e_test 108 · sim_inventory_test 67 · sim_inventory_e2e_test 24 ·
sim_personnel_test 98 · sim_personnel_e2e_test 24 · slice_personnel_e2e_test 15 ·
slice_personnel_itest 16 · render_leaves4_test 43 · render_leaves4_itest 19 ·
handle_recon_test 99 · real_scene_driver_test 17 · real_scene_driver_itest 10.

Total: 49 targets, 2093 checks, 0 failures. No goldens needed re-pinning (the executor
fixes are behavior-compatible with every existing pinned scenario; new behavior only
on paths no test pinned — parens-as-groups, float literals, else bodies, non-braced
while re-test).
