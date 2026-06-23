# effect-script (.esc) VM — Wave-18 (W18-ESC): verify 1:1 + fix the loop/branch core

MCP live. Decompiled the **whole** `.esc` effect-script subsystem (the C-like
source-text compiler + tree-walking VM + the effect command set) against the
existing reconstruction, confirmed the data layout/opcode tables byte-for-byte,
and **fixed two genuine control-flow bugs** the prior reconstruction's tests
never exercised (the chimney-smoke script's `exit()` teardown loop).

The brief's premise — that the `.esc` VM is still a documented hook — was **stale**:
a prior wave already landed the loader + lexer + compiler + executor + the effect
command set (`src/sim/script_{lexer,compiler,symbols,run,vm}.{h,cpp}` +
`src/sim/effect_script.{h,cpp}`), already wired into `render/building_fx.cpp`'s
`runSmokeScript` hook (`render::SmokeScriptToEmitters` / `RunSmokeScriptViaVm`),
and the real `effekte\Schornstein_dunkel.esc` already parsed+ran end-to-end. So
this wave is the rule-1 **verification + correctness** pass, not a fresh build.

---

## The `.esc` format (recovered) — it is C-like SOURCE TEXT, not bytecode

`.esc` files are tokenized C-like source. `.sbf` files are a DIFFERENT format
(sound-bank files under `sfx/`, audio — NOT script). The loader reads the source,
preprocesses it, then a one-pass compiler builds the symbol tables and a
tree-walking executor runs it.

`Schornstein_dunkel.esc` (the chimney smoke, 1277 bytes on disk):
```c
int emitter[2];
int i;
void main(int x, int y, int z) {
    #multistep
    emitter[0]=CreateEmitter(1,0,20,30,"rauch",4,1);
    SetParticlePos(emitter[0],x,y,z);
    ... SetEmitter* chain ...
    #singlestep
    Sleep(-1);
}
void exit(void) {
    i=0;
    while (i<2) { if (emitter[i]!=0) { KillEmitter(emitter[i]); } i++; }
}
```

## The load + run chain (decompiled, gilde.exe @0x400000)

| addr | symbol | role |
|---|---|---|
| 0x4424e0 | `VIBE_Script_LoadFromScriptDir(a1@eax)` | prepend `x:\engine\gfx\scripts\` then LoadScript |
| 0x4421f0 | `VIBE_Script_LoadScript` | VFS-open, read ≤0xFA00 (64000) B, StripCommentsAndWhitespace, claim a free slot (stride **2584**, **128** slots, base `dword_62E8A4`), store source + per-line debug map, stamp handle `+128 = dword_62E8D8++` |
| 0x441f30 | `VIBE_Script_StripCommentsAndWhitespace` | control chars→space, strip `/* */`, collapse space-runs, build a `-1`-terminated line-offset table in `dword_765450` |
| 0x441974 | `VIBE_Script_NextToken` | the tokenizer (see token/opcode tables below) |
| 0x4435d0 | `VIBE_Script_CompileBlock` | declaration/brace pass: vars (stride 48) + funcs (stride 304) tables, then compaction |
| 0x44396c | `VIBE_Script_RunMain` | CompileBlock, LookupFunction("main"), EnterFunction, mark runnable |
| 0x443a90 | `VIBE_Script_RunWithArgs(a1, argc, a3...)` | the smoke entry: bind `main(z,y,x)` args from the by-value arg block, then EnterFunction |
| 0x4431dc | `VIBE_Script_EnterFunction` | push a 144-byte call frame (16 max), bind params via DeclareLocal |
| 0x4450e0 | `VIBE_Script_Step` | per-step dispatch (depth stack `dword_7653CC`, max 32; gate on owner-frame/wait/flags) |
| 0x444bd0 | `VIBE_Script_ExecuteStatement` | the statement dispatcher (token-class switch) |
| 0x443ff0 | `VIBE_Script_EvaluateExpression` | precedence-climbing expression evaluator |
| 0x4445bc | `VIBE_Script_AssignVariable` | `=` / `++` / `--` (NO compound `+=`) |
| 0x44259c | `VIBE_Script_ParseWhileLoop` | runtime `while` (the only loop keyword) |
| 0x442ac8 | `VIBE_Script_DispatchTokenBranch` | the `if` branch (skip body on false) |
| 0x443f38 | `VIBE_Script_Finish` | terminate, or chain to the `exit` function if present |
| 0x445a28 | `VIBE_Script_DestroyContext` | free vars/funcs/source/stack + recurse children, zero the 2584 record |
| 0x442174 | `VIBE_Script_FindByHandle` | linear scan of the 128-slot context array by `+128` |
| 0x43c708 | `VIBE_Script_CmdSleep` | `Sleep(ms)` — yield; deadline = start + ms/14; `Sleep(-1)` = forever |

## Token classes (NextToken output byte 0) — VERIFIED-1:1

0=unknown, 1=operator (sub-code in +4), 2=variable, 3=function, 4=command,
5=int-literal, 6=float-literal, 7=string-literal, 8=type-keyword, 10=keyword
(sub-code in +4), 11=include, 12=EOF.

## Opcode tables (built at runtime by ConsoleParseLine @0x4453a4, byte-exact)

The two tables read as zeros in the cold IDB; they are filled at engine init by
`VIBE_Script_ConsoleParseLine` copying string literals into `byte_767450`
(keywords, stride 16) and `byte_767958` (operators, stride 5). The sub-code is the
1-based row index. Recovered **verbatim** from the ConsoleParseLine string copies:

**Keyword table `byte_767450` (stride 16):**
| code | keyword | | code | keyword |
|---|---|---|---|---|
| 0 | `do` (present but NEVER tokenized — NextToken's keyword loop starts at index 1) | | 5 | `#include` |
| 1 | `while` | | 6 | `#singlestep` |
| 2 | `if` | | 7 | `#normalstep` |
| 3 | `return` | | 8 | `#multistep` |
| 4 | `else` | | | |

**There is NO `for` keyword.** (The prior recon invented `for=2` and mis-numbered
`if=4`; see the divergence note below.)

**Operator table `byte_767958` (stride 5):**
| code | op | code | op | code | op |
|---|---|---|---|---|---|
| 1 `==` | 11 `,` | 21 `*` |
| 2 `=`  | 12 `(` | 22 `\|` |
| 3 `++` | 13 `)` | 23 `&` |
| 4 `--` | 14 `"` | 24 `\|\|` |
| 5 `+`  | 15 `//` | 25 `&&` |
| 6 `-`  | 16 `<=` | 26 ` ` |
| 7 `!=` | 17 `<` | 27 `[` |
| 8 `{`  | 18 `>=` | 28 `]` |
| 9 `}`  | 19 `>` | 29 `.` |
| 10 `;` | 20 `/` | |

Char-class table `byte_64A208` (256 bytes, REAL data, already reproduced verbatim
in `script_lexer.cpp` as `kCharClass`): `byte_64A208[(u8)(c+1)] & 0x20` = "begins
a numeric literal".

## Script-context struct (stride 2584 = 0xA18) — key offsets (decompiled)

+0 name/alive · +128 handle (`dword_62E8D8++`) · +132 owner-frame (`dword_62E8D4`)
· +136 vars ptr (stride 48) · +140 funcs ptr (stride 304) · +144 func-count ·
+148 var-count · +152 source cursor (compile) / line counter (run) · +156
SourceCode ptr · +160 SourceLen · +164 flags (bit0 active, bit1 terminating) ·
+168 call-frame array (16×144) · +2472 locals window · +2476 tokenCount · +2480
debugInfo · +2484/+2488 stack base/ptr · +2524 pendingCmd · +2528 yield flag ·
+2536/+2544/+2548/+2552/+2556 arg slots · +2564 stepMode (0 single / 2 run) ·
+2568 sleep-start tick · +2572 wait handle · +2576 saved universe slot · +2580
parent frame. (Full table recovered; see the wave notes / decompiles.)

## The effect command set (RegisterObjectCommands @0x440618) — VERIFIED-1:1

`ImportCommand(name, fn, returnsFlag, argc, argtype...)`; command records stride
**52** (name@+0, argc@+32, argtypes@+36, fn@+44, returnsFlag@+48), table
`dword_62E8AC`, max 256. The smoke commands + handlers (re-confirmed against the
existing `src/sim/effect_script.cpp` field/scale recovery — all match):

| cmd | fn | argc | fields written |
|---|---|---|---|
| CreateEmitter | 0x43fd24 | 7 | template fill + SpawnSystemByType; returns handle |
| SetParticlePos | 0x4405c4 | 4 | pos = (arg1, arg3, arg2) — **y/z swap** |
| SetEmitterAmplitude | 0x43fe9c | 6 | f[16..19]=arg*0.01, f[44]=arg5*0.01 |
| SetEmitterPhasespeed | 0x43ff30 | 6 | f[20..23]=arg*0.01, f[45]=arg5*0.01 |
| SetEmitterDirection | 0x43ffc4 | 4 | f[27..29]=arg*0.01 |
| SetEmitterSize | 0x44002c | 4 | f[24..26]=arg (NO scale) |
| SetEmitterAcceleration | 0x4400d4 | 4 | f[36..38]=arg*0.001 |
| SetEmitterTimeAndAlpha | 0x440234 | 5 | +184=(f)arg1, +188/192/196=arg2/3/4 |
| SetEmitterColor | 0x44028c | 5 | +202=r,+201=g,+200=b,+203=a (BGRA store) |
| KillEmitter | 0x43fe68 | 1 | free the emitter node |
| Sleep | 0x43c708 | 1 | yield (RegisterCommands @0x43c850, not the object set) |

Scales (get_bytes bit-exact): `flt_617748/774/7A0 = 0x3c23d70a = 0.01`,
`flt_617820 = 0x3a83126f = 0.001`. All already pinned in `effect_script.cpp`.

---

## FIXES applied (rule 1 — real divergences the old tests missed)

The prior reconstruction's tests only ran the smoke `main()` body (a flat
`CreateEmitter` + `SetEmitter*` chain). They never exercised the **`exit()`
teardown** — `while (i<2) { if (emitter[i]!=0) { KillEmitter(emitter[i]); } i++; }`
— which is the script's only loop/branch/array/`++` control flow. Driving that
real body through the executor exposed two bugs:

### 1. Inner `if`-block `}` prematurely popped the enclosing `while` (the big one)
`ScriptExecutor::ExecStatement` treated **every** `}` (class-1 sub-9) as the
current loop's closing brace when `loopStack_` was non-empty, so the nested `if`
block's `}` re-tested the `while` at the wrong cursor and the loop ran once then
fell out. The original (`ExecuteStatement` @0x444bd0 / `dword_62E8E0` brace nesting)
distinguishes them by brace depth.
**Fix (`script_compiler.{h,cpp}`):** added a `braceDepth_` counter (the
`dword_62E8E0` analogue) + `LoopFrame::bodyDepth`; `{`/`if`/`while` deepen it via
the new `StepIntoBlock()`, and a `}` only re-tests/pops a loop when it returns the
depth to that loop's `bodyDepth-1`. Inner-block `}` just shallow the nesting.
The `while`-retest now re-enters via `EnterLoop(1)` (single re-evaluation path).

### 2. `++` / `--` were not lexed or executed
The lexer's operator table was missing `++` and `--` (so `i++` lexed as `i` `+`
`+` and the assignment path, expecting `=`, wrote `i=0` forever), and
`AssignVariable` only handled `=`.
**Fix (`script_lexer.cpp` + `script_compiler.cpp`):** added the binary's `++`
(code 3) and `--` (code 4) operator rows (longest-match, so `i++` lexes as `i`
`++`), and the assignment statement path now dispatches `=` / `++` / `--` by
operator text (`AssignVariable` @0x4445bc: the only three assignment operators —
there is no compound `+=`). Increment/decrement read-modify-write the (array)
cell.

Both fixes are behavior-preserving for the existing suite
(`sim_script_compiler_test` 88/88 still green) and make the real `exit()` teardown
run 1:1.

---

## Documented divergence handed off (NOT fixed — out of ownership, no observable effect)

The shared `src/sim/script_vm.h` keyword/operator **sub-code constants** diverge
from the binary's tables above:
- `kKwIf = 4` (binary: **2**); a phantom `kKwFor = 2` (binary slot 2 is `if`;
  there is **no `for`**); code 4 is `else` (unmodeled); the `#`-prefixed step
  directives are modeled as `kKwModeA/B/Loop = 6/7/8` (binary `#singlestep`/
  `#normalstep`/`#multistep` = 6/7/8 — these match).
- `kOpAdd = 4` (binary `+` is **5**; code 4 is `--`).

These are **provenance** mismatches with **no behavioral effect**: the lexer and
the executor reference the same named constants, and `[`/`]`/`(`/`)` correctly
terminate tokens, so `if`/`while`/array-subscript/mode-directives all execute
faithfully. Renumbering the constants to be byte-exact touches `script_vm.cpp`,
`command_apply.cpp`, and several non-owned tests (`sim_script_test.cpp` pins
`kKwFor==2`/`kOpAdd`), so it is left to the owner of the `sim/script_{vm}`
cluster. Recommended: adopt the table above (do=0,while=1,if=2,return=3,else=4,
#include=5,#singlestep=6,#normalstep=7,#multistep=8 ; ==1,=2,++3,--4,+5,-6,!=7,
{8,}9,;10,,11,(12,)13,"14,//15,<=16,<17,>=18,>19,/20,*21,|22,&23,||24,&&25,
[27,]28,.29) and add an `else`-branch + the missing `++`/`--` aliases.

A SECOND, independent VM reconstruction is in progress in `src/script/`
(namespace `guild::script`, untracked, actively edited by a concurrent wave-18
agent). Not touched.

---

## Tests (owned)

`tests/unit/effect_script_test.cpp` (suite `EffectScript`) — **67 checks, 0
failures** (was 58; +2 new tests, +9 checks):
- **ExitTeardownLoopKillsBothEmitters** (NEW): runs the real `exit()` teardown
  loop body through the VM (ParseWhileLoop/DispatchTokenBranch/EvaluateExpression
  `[idx]` read/AssignVariable `++`) — with two live handles KillEmitter fires
  exactly twice and `i` advances to 2. (Pins the brace-depth + `++` fixes.)
- **ExitTeardownSkipsZeroHandles** (NEW): a zero handle is skipped by the `if`
  guard — exactly one kill. (Pins DispatchTokenBranch false-skip.)
- (existing 13: command-set coverage, the per-command field/scale golden pins,
  KillEmitter invalidation, the smoke-shaped end-to-end, and the render bridge.)

`tests/e2e/effect_script_real_esc_e2e_test.cpp` (suite `EffectScriptRealEsc`,
pre-existing) — **39 checks, 0 failures**, parsing the **real on-disk**
`unpacked_resources/Scripts/Effekte/Schornstein_dunkel.esc` (1277 B): two
emitters, every SetEmitter* param at the recovered scale/offset, SetParticlePos
y/z swap, `Sleep(-1)` resident, no main-time kills. Guards on the asset (inline
fallback when absent).

Cross-checks: `sim_script_compiler_test` **88/88** still green with the modified
executor; `command_apply.cpp` / `sim_script_e2e_test.cpp` / `sim_script_test.cpp`
all still compile clean against the modified lexer (the `kOp*` values are
unchanged; only `++`/`--` rows added).

## Build note

The shared `build/` is currently **red from two OTHER concurrent wave-18 agents'
in-progress files** — `src/sim/ai_meister_equip.cpp` (`v14` undeclared, ambiguous
redecl) and `src/script/script_vm.cpp` (`kOff_State`/`Dword` undeclared) — both
untracked, neither mine, neither in my dependency chain. All of W18-ESC's files
compile clean (`-fsyntax-only`) and the owned tests pass via a standalone link of
the effect-script TU cluster + its real util/io/compress/render deps.

## Files

- `src/sim/script_compiler.{h,cpp}` — brace-depth loop/branch fix + `++`/`--`
  assignment (the executor / VM core; **edited**, behavior-preserving + fixes).
- `src/sim/script_lexer.cpp` — `++`/`--` operator rows (**edited**).
- `src/sim/effect_script.{h,cpp}` — the effect command set (re-verified; unchanged).
- `tests/unit/effect_script_test.cpp` — +2 exit()-teardown tests (**edited**).
- `tests/e2e/effect_script_real_esc_e2e_test.cpp` — real-asset e2e (re-verified).
