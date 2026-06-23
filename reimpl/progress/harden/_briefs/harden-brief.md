# FULL-TREE 1:1 HARDENING SWEEP (MCP LIVE) — verify EVERY function against the binary

### ABSOLUTE RULES — READ FIRST (a prior wave's `git stash` destroyed work):
- NEVER run ANY git command. No git stash/checkout/clean/reset/add/commit. NONE. EVER.
- NEVER delete or recreate the build/ directory. Build ONLY your test targets:
  cmake --build build --target <your_test_target> -j
- Edit ONLY the .cpp/.h files in YOUR chunk list + their tests. Nothing else.
- NEVER edit progress/INDEX.md.

### Mission
From entry point to last leaf, every reconstructed function must be a verified 1:1 clone of
gilde.exe. You own ONE chunk = an explicit list of .cpp files (cat your chunk file). For EVERY
function in those files carrying a `gilde.exe 0xADDR`/`@0xADDR` provenance, DECOMPILE the original
and DIFF the reconstruction line-for-line; fix every divergence to match the binary.

Load MCP via ToolSearch select:mcp__ida-pro-mcp__decompile,mcp__ida-pro-mcp__disasm,
mcp__ida-pro-mcp__get_bytes,mcp__ida-pro-mcp__get_global_value,mcp__ida-pro-mcp__xrefs_to,
mcp__ida-pro-mcp__lookup_funcs,mcp__ida-pro-mcp__func_profile.

### Verify per function (the divergence classes earlier waves found):
- Control flow & branch conditions; exact switch arms; no inverted/dropped/extra branches.
- Constants & tables: get_bytes / get_global_value EVERY constant/table — confirm the bytes.
- ConvertX @0x5c6b08 TRUNCATES toward zero; bare fistp rounds-to-nearest-even; (int) cast
  truncates. Verify EVERY float->int site against the disasm. x87-80bit vs SSE on accumulations
  (a product/quotient kept in the x87 register stays 80-bit until the fstp; model with double).
- Fixed-point >>16 signed(sar) vs unsigned(shr); wraparound; signed/unsigned compares (movsx/jge).
- RNG: exact RandNext/RandomModulo draw COUNT and ORDER.
- Struct field offsets, stride, packed/aliased reads; side-effect order; return value incl edx.
- Where Hex-Rays is wrong (collapsed __usercall args, mislabeled strides/registers), DISASM is the
  reference of record.

### Discipline
- If a golden encodes WRONG behavior, fix BOTH source AND golden to the binary (cite addr +
  byte/disasm evidence). A correct reconstruction → VERIFIED-1:1 (no churn).
- Rule 8: only a genuine boundary (rules 3-5 tech swap, or data not in tree) stays a hook.
- Run your test targets to green: cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R <suite> --output-on-failure
- Write progress/harden/<chunk>.md: per function VERIFIED-1:1 / FIXED (addr+before/after+evidence)
  / BOUNDARY (addr+reason) + counts.
