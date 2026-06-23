# FIX-SCRIPTVM — .esc / script-VM cluster (Wave-H1b)

Owned files: `src/sim/script_compiler.{cpp,h}`, `src/sim/script_lexer.{cpp,h}`,
`src/sim/effect_script.{cpp,h}` + their tests.

## Result — all 4 targets GREEN

| test | before | after |
|------|--------|-------|
| effect_script_test                | FAIL (10/67) | PASS |
| effect_script_real_esc_e2e_test   | FAIL (30/39) | PASS |
| sim_script_compiler_test          | FAIL (11/88) | PASS |
| sim_script_compiler_e2e_test      | FAIL (3/23)  | PASS |

No collateral: full `ctest -R script` = 24/24 PASS, plus the sibling enum-lock
tests `sim_script_test` and `script_vm_test` (which freeze `script_vm.h`'s enum
values) still PASS — I did NOT touch `script_vm.h`.

## Root causes (all stash-frozen divergences) and 1:1 fixes

### 1. Source regression: EvalExpression terminator sub-codes wrong
`ScriptExecutor::EvalExpression` (`script_compiler.cpp`) had stale terminator
cases. Recovered the real operator sub-codes from `byte_767958` (stride 5),
populated in `VIBE_Script_ConsoleParseLine @0x4453a4`: subcode = (dest-0x767958)/5.

| op | dest | subcode |
|----|------|---------|
| `;` | unk_76798A | 10 |
| `,` | unk_76798F | 11 |
| `)` | unk_767999 | 13 |
| `]` | unk_7679E4 | 28 (kOpStop) |

The frozen code mapped `kOpStop(28)`→`')'`, `29`(`.`)→`','`, `13`(`)`)→`';'`
and **never handled `;`(10)** — so an assignment RHS `total + i ;` ran off the
end of its statement. Fixed to the table-true mapping (`;`=10, `,`=11, `)`=13,
`]`=28). This fixed `WhileLoopComputesSum` (i==6, total==15).

### 2. Source regression: `if` keyword mis-dispatched as a for-loop
`ExecStatement` keyword switch routed sub-code 2 to `EnterLoop(2)` and the real
if-handler to `case kKwIf`(==4). Per `VIBE_Script_ExecuteStatement @0x444bd0`
class-10 switch and `byte_767450` (stride 16, subcode=offset/16):

```
1 while  -> ParseWhileLoop 0x44259c
2 if     -> 0x4427b4 (IDA-misnamed "ParseForLoop"; reads '(' cond ')' then a
           '{' block or single stmt — THE IF HANDLER)
3 return -> EvaluateExpression + Finish
4 else   -> DispatchTokenBranch 0x442ac8
5 #include-> ParseInclude
6/7/8    -> single/normal/multi step (ctx+2564)
```
There is NO `for` keyword (offset 0 is `do`, never matched; scan starts at
offset 16). `script_vm.h`'s enum is mislabeled: `kKwFor`==2 is really `if`,
`kKwIf`==4 is the `else`/branch code. Because `if` lexes as sub-code 2, the
frozen dispatch pushed a type-2 loop frame per `if`, leaking frames on a
single-statement `if` body (the `if ( i & 1 ) Beep ( i ) ;` case) so the while
exited after one iteration. Fixed: sub-2 → the if-statement handler, sub-4 →
`DispatchBranch`. This fixed `LoopIfCommandCall` (2 Beep emits) and the
effect-script .esc e2e (which uses the same executor over real assets).

### 3. Golden divergences in `sim_script_compiler_test.cpp` (test fixed to binary)
The unit-test goldens encoded the wrong table mappings. Corrected to the binary
spelling tables (evidence: `0x4453a4` dest addresses + evaluator `0x443ff0`
ladders) against `script_vm.h`'s LOCKED enum values:

- Operators (`byte_767958`): `|`→24 (`kOpOr`, eval `v3|=`), `&`→25 (`kOpAnd`,
  `v3&=`), `||`→22 (`kOpBitOr`, returns `a|b`), `&&`→23 (`kOpBitAnd`, `a&b`).
  Verified in the evaluator: subcode 22 `break`s to `return v38|v37`; 23
  `return v36&v35`; 24 `v3|=v2`; 25 `v3&=v2`. Test now asserts
  `find("|")==kOpOr`, `find("&")==kOpAnd`, `find("||")==kOpBitOr`,
  `find("&&")==kOpBitAnd`.
- Keywords (`byte_767450`): `while`=1, `if`=2, `return`=3, `else`=4,
  `#include`=5. No `for`; include is spelled `#include`. Test now asserts
  `find("if")==kKwFor` (mislabeled enum), `find("#include")==kKwInclude`,
  dropped the bogus `for`/`include` spellings.
- `(`/`)` sub-codes: `(`→unk_767994=12, `)`→unk_767999=13 (not 27/28, which are
  `[`/`]`). Test now checks 12/13.

The source lexer tables (`kOperatorTable`/`kKeywordTable` in `script_lexer.cpp`)
were already binary-correct; only the test goldens were stale.

### 4. Removed leftover debug instrumentation
Three `fprintf(stderr, "[ReadVar]/[WriteVar] ...")` debug lines had been frozen
into `ReadVar`/`WriteVar` in `script_compiler.cpp`. Removed (not 1:1; stderr
spam). Behavior unchanged otherwise.

## Notes
- `script_vm.h` (enum values) is locked by sibling tests `sim_script_test`
  (asserts kOpOr==24, kOpAnd==25, kOpBitOr==22, kOpBitAnd==23, kKwFor==2,
  kKwIf==4) and was left untouched. All divergences resolved in the owned
  files/tests, consistent with both the binary and the locked enum.
- `EnterLoop(2)` / the `type==2` frame path in the executor are now unreferenced
  (no `for` keyword, `if` uses the dedicated handler) but left in place; the
  `}`-pop only re-tests type-1 frames, matching the binary.
