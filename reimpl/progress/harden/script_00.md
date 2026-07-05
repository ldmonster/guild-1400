# Harden sweep — script_00

Chunk: `src/script/script_vm.cpp` (+ its header `src/script/script_vm.h`).
Method: each `// gilde.exe 0x…` function decompiled via the IDA MCP CLI and diffed
line-for-line; the 39-entry character command table diffed against the raw
`ImportCommand` call stream @0x43dfb0. Only proven divergences edited, with the
address as evidence.

## Per-function verdicts

| Addr | Function | Verdict |
|------|----------|---------|
| 0x442c90 | DefineVariable | VERIFIED-1:1 (see note A) |
| 0x441788 | ParseTypeKeyword | VERIFIED-1:1 (see note B) |
| 0x4414d4 | LookupVariable | VERIFIED-1:1 |
| 0x4415f8 | LookupFunction | VERIFIED-1:1 |
| 0x441280 | DeclareLocal | VERIFIED-1:1 |
| 0x4416c4 | SkipBraceBlock | VERIFIED-1:1 |
| 0x442b34 | ParseSymbolName | VERIFIED-1:1 |
| 0x442d88 | ParseDeclaration | **FIXED** — dropped `type` arg (see D1) |
| 0x4431dc | EnterFunction | **FIXED** — frame-push offset bug (see D2) |
| 0x445a28 | DestroyContext | VERIFIED-1:1 |
| 0x43dfb0 | RegisterScriptCommands / CharCommandTable | VERIFIED-1:1 (39 entries, all names/kind/argc/argTypes exact incl. misspelled "SetCharacterTransperancy") |

## Divergences fixed

### D2 — EnterFunction frame push wrote funcRec/localCursor 42 dwords too far
`0x443358–0x443381`. The image computes two distinct bases: `activeFrame =
ctx + 168 + 144*depth` (`[ecx+9A8h]`) and the frame window `v16 = ctx +
144*depth` (`eax` after `add eax,ecx`). It then stores `funcRec` at
`[eax+0A8h]` (v16+168 = **activeFrame+0 = frame[0]**) and `ctx[622]` at
`[eax+0B4h]` (v16+180 = **activeFrame+12 = frame[3]**). The reimpl used
`Dw(newActive,42)` / `Dw(newActive,45)` (relative to the active-frame base), i.e.
42/45 dwords past where the image writes — leaving frame[0]/frame[3] zero and
scribbling into the next frame's slot region.
Fix: `Dw(newActive,0) = fr;` and `Dw(newActive,3) = ctx[622];`.
Evidence: disasm 0x443364/0x443372/0x443381 (byte offsets 0xA8=42dw, 0xB4=45dw
taken from `eax = ctx+144*depth`, not the active-frame base).

### D1 — ParseDeclaration hardcoded `kVarInt` instead of the caller's type code
`0x442d88`. The function is `__usercall(ctx@eax, type@edx)`; the sole caller
`VIBE_Script_CompileBlock @0x443692` does `mov edx,[esp+var_83]; sar edx,18h`,
i.e. passes `token.value>>24` (the type-keyword code: int=1/byte=2/string=6/
float=7) in edx, and every declaration path calls
`DefineVariable(ctx, edx/type, …)`. The reimpl signature had dropped the arg and
passed `kVarInt` in all three paths, so `float`/`byte`/`string` scalars, `=`
inits and arrays were mis-sized (always 4 bytes) and stored a type-1 nibble.
Fix: added `u8 type` param (header + def), forwarded it to the three
`DefineVariable` calls (scalar 0x442f09, `=` 0x442f58, array 0x442e53). Updated
the 4 test call sites (all int decls → `kVarInt`) and added regression test
`scriptvm_parse_declaration_type_honoured` (declares `float f;`, asserts the
stored nibble == kVarFloat). No external caller besides the test uses this
symbol (wiring.cpp uses only RegisterScriptCommands), so the signature change is
contained.

## Notes (faithful accommodations, not changed)

- **A — DefineVariable alloc size clamp.** Image passes `elemSize*elemCount`
  unclamped to AllocDebug/memset; reimpl clamps `elemCount` to `>=0`. Only
  differs for a negative array literal (`int a[-3]`), which would make the image
  attempt a ~4GB alloc (host-unreproducible). Left as the documented value-
  semantics guard.
- **B — ParseTypeKeyword scan.** The image's space/`)` scan over the 2-byte-
  stride copy has quirks on odd-length/embedded-NUL lexemes; traced through, the
  final classification and the at-most `*cut=0` write land on/after the existing
  NUL, so the returned type code is identical to the image for all inputs.
- **EnterFunction top-level (depth 0) args.** Image copies argument cells from
  globals `dword_62E8B0` / `unk_7675E0` (the top-level arg buffers); these are
  not modeled in this front-end, so the reimpl zero-fills the arg scratch
  (documented deferral — reason: unmodeled host globals, not a transcription
  bug). Nested-call args (EvaluateExpression) are reconstructed 1:1.
- **ParseDeclaration `=` re-init.** When the variable already exists, the image
  writes the literal through a stale register pointer (`**(a2+44)` where a2 =
  old dword_62E8A8); reimpl guards with `&& rec`. Reproducing a wild pointer
  write is neither safe nor meaningful on host — left guarded.

## Tests

- Target built: `script_vm_test` (only; no whole-tree build).
- `GUILD_GAME_DIR=$PWD/europe_guild_1400_original ./build/script_vm_test`
  → **16 tests, 110 checks, 0 failures** (incl. new regression + the
  EnterFunction binding test that now exercises the corrected frame slots).
