# Script-VM front-end — Wave-19 (W19-SCRIPTVM)

**Agent:** W19-SCRIPTVM · **Module:** `src/script/script_vm.{h,cpp}` (NEW) ·
**Test:** `tests/unit/script_vm_test.cpp` · **MCP:** live (gilde.exe, imagebase 0x400000)

Reconstructs the **.esc script-VM front-end** — the parser/interpreter functions
that gate script execution — 1:1 from the Hex-Rays decompile, preserving the
original's struct-offset addressing on the 2584-byte per-script *context* record,
its exact control flow, constants and side effects.

This is a NEW module in `guild::script`. The pre-existing `src/sim/script_vm.*`
is a higher-level re-imagining that explicitly **deferred** these exact functions
(see its header's "Deferred" note: NextToken, CompileBlock/EnterFunction/
ParseWhileLoop, GetVariableAddress). No symbols are shared → no ODR clash; the
command table (`guild::sim::Commands()`/`ImportCommand`) and the event-token table
(`guild::sim::EventTokens()`) are **reused** (extern), not redefined.

---

## Reconstructed (1:1, with address provenance)

| addr | name | size | notes |
|------|------|-----:|-------|
| 0x4431dc | VIBE_Script_EnterFunction | 1010 | eval args → push 36-dword call frame → bind params as locals (DeclareLocal) → jump cursor to body. Depth-0 vs nested-call arg paths; per-type store switch (1/2/6/7). |
| 0x442d88 | VIBE_Script_ParseDeclaration | 762 | decl statement: scalar(sub 10) / '=' init(2) / array '['(27) / func-sig(12). Func path: ParseSymbolName + body-'{' scan + store body ptr & name into the func record. |
| 0x442b34 | VIBE_Script_ParseSymbolName | 346 | parse a signature's param list (type+name pairs), terminated by sub-code 13. **Accepts only param types 1/2/6 (int/byte/string)** — float(7) is rejected by the range test (verified: `(v6<1 || v6>2) && v6!=6`). |
| 0x441280 | VIBE_Script_DeclareLocal | 334 | push a local into the active frame's 8-slot table (frame +16..) + the +2484 local stack; per-type size (1=4,2=1,7=4,6=96); stack-overflow + too-many-locals errors. |
| 0x4416c4 | VIBE_Script_SkipBraceBlock | 194 | mode 1 forward / mode 2 backward brace-balance on the current-context cursor; src-range bounds. |
| 0x445a28 | VIBE_Script_DestroyContext | 325 | recursive free: var-storage blocks, 8 child contexts, var/func/src/line/local tables, then zero the 2584-byte record + state := -1. |
| 0x43dfb0 | VIBE_Character_RegisterScriptCommands | 1061 | 39 ImportCommand calls (38 statements + the final `return ImportCommand`); names/kind/argc/arg-types recovered exactly from the decompile. |

### Reconstructed callee leaves (also 1:1)

| addr | name | notes |
|------|------|-------|
| 0x442c90 | VIBE_Script_DefineVariable | append 48-byte var record; per-type elem size; alloc + zero storage; bump var count. |
| 0x4414d4 | VIBE_Script_LookupVariable | scan active-frame locals → context var table → global event-token table. |
| 0x4415f8 | VIBE_Script_LookupFunction | 304-byte-stride func record name walk. |
| 0x441788 | VIBE_Script_ParseTypeKeyword | int=1/float=7/void=5/string=6/{=8/}=9/byte,char=2/else=0; strips trailing ' ' then ')'. |

## Leaves kept as injectable hooks (`ScriptVmHooks`) — control flow that USES them is 1:1

These bodies live elsewhere / touch deep host state; the front-end calls them
exactly where the original does, but they are mockable so the parser/runtime is
testable headless:

- **0x441974 VIBE_Script_NextToken** — the source-text tokenizer. Its keyword
  (`byte_767450`) and operator (`byte_767958`) tables are **built at runtime**
  (static image bytes are zero — verified via get_bytes), not loadable golden
  tables, so it is a genuine leaf here. The decompile is documented in the header.
- **0x443ff0 VIBE_Script_EvaluateExpression** — operator-precedence evaluator
  (used by EnterFunction's nested-arg path). Decompiled & documented.
- **0x440f94 VIBE_Script_ReportError** / **0x443f38 VIBE_Script_Finish** —
  logging + re-entrant teardown leaves (Finish re-enters EnterFunction("exit")).
- **0x438f10 AllocDebug / 0x43923c FreeDebug** / **0x5d3f10 Util_StrCmp**.
- The 38 per-character **Cmd\*** leaves are stored by identity only (opaque
  tokens) — never called through here — exactly as the image stores the real
  Cmd* address at command-record +44 (matches script_import2/3's kFnCmd* pattern).

---

## Struct layout recovered (the instruction-set "ABI")

**ScriptContext** (image stride 2584 / 646 dwords) — dword indices:
`[32]` state(-1 destroyed), `[34]` varBase, `[35]` funcBase, `[36]` funcCount,
`[37]` varCount, `[38]` cursor, `[39]` srcBase, `[40]` srcLen, `[41]` flags,
`[42]` call-frame array (16 frames, 36-dword stride), `[618]` active-frame ptr,
`[619]` lineCount, `[620]` lineBase, `[621]` localBase (+1024 = overflow),
`[622]` localCursor, `[623..630]` 8 child-context ptrs.

**Var/local record** (48): +0 nibble type, +1 name, +36 elemCount, +40 scope
(1=local), +44 storage ptr (locals: in-line at +48).
**Func record** (304): +0 name, +32 body ptr, +36 paramCount, +37+i paramTypes
(EnterFunction reads the per-param bind type here via `*(fr+34)>>24`), +45+32·i
paramNames.

### 32→64-bit accommodation
The original is a 32-bit process packing every record pointer as a 4-byte field.
To run testably on a 64-bit host, every **context dword index** is scaled to
pointer width (`Dw()` accessor), and the var/func records widen their single
pointer field (storage / body) to a clean pointer-aligned slot. The index
arithmetic — including the image's frame-window overlaps — is reproduced exactly,
only at pointer stride. Integer fields keep their byte width. This is the
value-semantics accommodation already documented in `include/guild/common/types.h`.

---

## Wiring (rule 13)

`RegisterScriptCommands()` registers into the shared `guild::sim::Commands()`
table via the already-reconstructed `guild::sim::ImportCommand` (0x445bc8). The
engine-init caller (`VIBE_App_InitEngineAndScriptCommands @0x528560`,
`src/app/engine_init_app_recon.cpp:200`) invokes the
`EngineInitHooks::characterRegisterScriptCommands` hook (currently a no-op
default). **Handoff (one line, in the app-owned hook-population site):**

```cpp
hooks.characterRegisterScriptCommands = [] { guild::script::RegisterScriptCommands(); };
```

(The hook-population site is an app bind-site this agent does not own; the binding
line above is the exact wiring.) The other six functions are the script
loader/runtime's internal callees (Finish/Step/CompileBlock reach them); they are
exposed in the module header for the script-run cluster to call.

### Sibling handoff
A sibling background agent owns `sim/effect_script` (the `.esc` **effect** VM data
/ commands). This module owns the **general script-command parser/runtime
front-end**. The only shared state is the command/event-token tables, which are
extern-reused from `guild::sim` (not edited). No overlap on the 7 reconstructed
functions.

---

## Tests — `tests/unit/script_vm_test.cpp` (15 tests, 107 checks, 0 failures)

Driven by a synthetic scripted tokenizer hook + a synthetic context arena:
- `ParseTypeKeyword` golden classification table (incl. trailing ' '/')' strip).
- `DefineVariable` record layout + count bump + writable zero-init storage.
- `ParseDeclaration`: scalar / '=' init (value stored) / array (elem count) /
  array syntax-error reporting.
- `ParseSymbolName`: 2-param signature → types + names into the func record;
  verifies the **float-param rejection** (only 1/2/6 accepted).
- `DeclareLocal`: slot allocation, name, scope marker, next-slot; "too many
  locals" at the 9th.
- `SkipBraceBlock`: forward balance over nested `{}`; no-match returns 0.
- `EnterFunction`: null-func error; top-level entry binds an int param + jumps
  the cursor to the body.
- `DestroyContext`: frees var-storage + all tables, marks state -1.
- `RegisterScriptCommands`: 39-entry table all resolvable via FindCommandByName;
  arg-signature spot checks.

**Build note:** the whole-library link was blocked by a *concurrent* sibling
wave-19 agent's compile error in `src/sim/ai_meister_equip.cpp` (`v14` undeclared
+ ambiguous redecl) — unrelated to this module. `src/script/script_vm.cpp.o`
compiles cleanly within the `guild` target; the test was verified by linking the
script_vm object + the (already-built) script_import/util objects standalone:
all 15 tests pass. Once the sibling's file builds, the test runs in-tree with no
changes (CMake globs `src/**` + `tests/unit/*.cpp`).
```
107 checks, 0 failures
```
