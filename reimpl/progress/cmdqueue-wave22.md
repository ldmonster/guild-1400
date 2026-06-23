# Wave-22 — command-queue request-builder family (W22-CMDQUEUE)

**Agent:** W22-CMDQUEUE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

## Mandate

Reconstruct 1:1 the command-queue request-builder family flagged still-missing in
[`coverage-audit-wave21.md`](coverage-audit-wave21.md):

| addr | size | name |
|------|-----:|------|
| 0x494910 | 178 | VIBE_Command_QueueRequestBuffer28 |
| 0x494d68 |  40 | VIBE_Command_QueueRequestQuad46 |
| 0x495070 |  39 | VIBE_Command_QueueRequestQuad54 |
| 0x495124 |  40 | VIBE_Command_QueueRequestQuad60 |
| 0x494b74 |  35 | VIBE_Command_QueueRequestPair35 |
| 0x494ca4 |  35 | VIBE_Command_QueueRequestPair42 |

## Finding: ALL SIX WERE ALREADY RECONSTRUCTED (no re-reconstruction; ODR-safe)

Per the brief's rule ("GREP FIRST; if the body already exists, ADD provenance + a
verifying test, do NOT re-reconstruct"), a grep of `src/sim/` showed every one of the
six targets is already implemented in **`src/sim/command_builders2.cpp`** with its
`gilde.exe 0xADDR` provenance already present in **`command_builders2.h`**. The wave-21
audit listed them as "missing" only because its range-cite classifier matched cited
addresses inside `[start,end)` and the .cpp bodies cited the helper-shared builders by
opcode comment, not by per-function address — so the addresses fell through.

I therefore did **not** touch the bodies. Instead I:

1. **Verified each body 1:1 against the binary** (MCP `decompile` + `disasm` +
   `get_bytes`).
2. **Added the missing per-function `gilde.exe 0xADDR` provenance comments** to the
   helper-dispatched builders in `command_builders2.cpp` (the headers already had them;
   the .cpp lines did not — this closes the range-cite gap so the audit counts them).
3. **Added a golden-pinned unit test** (`tests/unit/sim_command_builders2_test.cpp`) —
   command_builders2 previously had **no** test of its own.

## 1:1 verification (decompile + disasm + byte golden-pins)

All packet builders stage a 153-byte temp, write `opcode` at byte 0, write register-arg
fields starting at +0x10, then call `VIBE_Command_EnqueuePacket` (@0x49388c), which
stamps the header (`len`=ComputePacketSize, `cmdId`=ring slot, `count`=seq).

### Pair35 / Pair42 (0x494b74 / 0x494ca4)
Decompile: `v4=a1`@+0x10, `v5=a2`@+0x14, `v3[0]=op`. Bytes confirm
`mov bl,0x23`(35)/`0x2a`(42), `mov [esp+10],eax`, `mov [esp+14],edx`, `mov [esp],bl`.
Matches `EmitPair(q, op, a1, a2)`. ComputePacketSize: op 0x23/0x2A → **24**.

### Quad46 / Quad60 (0x494d68 / 0x495124) — all four args on wire
Decompile: `v6=a1`@+0x10, `v7=a2`@+0x14, `v8=a4`@+0x18, `v9=a3`@+0x1C. Bytes confirm
`mov byte[esp],0x2e`(46)/`0x3c`(60), `[esp+10]=eax`(a1), `[esp+14]=edx`(a2),
`[esp+18]=ebx`(a4), `[esp+1c]=ecx`(a3). Matches `EmitQuad4` (a3 last, at +0x1C).
ComputePacketSize: op 0x2E/0x3C → **32**.

### Quad54 (0x495070) — a3 dropped (off-wire)
Decompile: `v9=a3`@`[ebp-4]` (a slot OUTSIDE the 153-byte staging buffer), `v6=a1`@+0x10,
`v7=a2`@+0x14, `v8=a4`@+0x18, `v5[0]=54`. Bytes confirm `mov cl,0x36`(54) and that ecx
(a3) is stored via `mov [esp],cl` only for the opcode — a3 never reaches the packet.
Matches `EmitQuad3` (a1,a2,a4; a3 dropped). ComputePacketSize: op 0x36 → **28**.
This is the only wire-level difference between the Quad3 family (37/43/52/54) and the
Quad4 family (46/60): whether a3 lands at +0x1C. Pinned by a differential test.

### Buffer28 (0x494910) — large speech/IO pending-block command
Decompile + disasm: early-out `-1` if `!VIBE_Person_FindRecordById(a1[2])`; else
`AllocDebug(bodyLen+0xF8,"tmp")`, copy **0xF8 (248)** header bytes (`mov ecx,0F8h;
repne movsd` = 62 dwords from a1) then `bodyLen` body bytes from a3,
`StagePendingBlock((bodyLen+0xF8)&0xFFFF, scratch)`, free scratch, `EnqueuePacket`
(opcode 28). ComputePacketSize: op 0x1C → **20**. Matches `QueueRequestBuffer28`, which
stages the 248+body block and enqueues via `EmitWithPendingBlock` (the EnqueuePacket
tail that links the header's +12 to the first generated opcode-7 fragment's Count).
The `kPendingMaxBlock` clamp in the reconstruction is a never-hit safety net for the
standalone layout; `StagePendingBlock` itself rejects `len > 0x3FE` exactly as the
original, so behavior is byte-identical for all in-range blocks.

## Tests

`tests/unit/sim_command_builders2_test.cpp` — **8 tests, 71 checks, all pass**
(compiled clean with `-Wall -Wextra`; linked against prebuilt `command.cpp.o`,
`command_pending.cpp.o` + `test_framework`):

- `Args26ThreeDwords` — op26 size 28, a1/a2/a3 @+0x10/+0x14/+0x18.
- `Pair35AndPair42` — op35/op42 size 24, a1@+0x10 a2@+0x14, ring slots 1/2.
- `Quad46AllFourOnWire` / `Quad60AllFourOnWire` — size 32, a4@+0x18 a3@+0x1C.
- `Quad54DropsThirdArg` — size 28, a4@+0x18, a3 (0xDEADBEEF) NOT on wire (+0x1C==0).
- `QuadFamilyA3Contrast` — differential: same args, a3 present(46)/absent(54) at +0x1C.
- `Buffer28PersonGuardEarlyOut` — personFound=false → return -1, nothing staged/enqueued.
- `Buffer28StagesHeaderPlusBodyAndEnqueues` — op28 size 20, staged cleared after the
  fragment chain is generated, header +12 link non-zero.

## Wiring (rule 13)

All six are top-level request emitters already declared in `command_builders2.h` and
defined in `command_builders2.cpp`; `EnqueuePacket`/`StagePendingBlock`/
`EmitWithPendingBlock` are reused (extern, ODR-clean). The original call sites are the
game-action dispatch paths (cm_Request*); those callers are reconstructed incrementally
elsewhere in the sim/play tree. No new dangling symbols introduced.

## Build note (NOT mine)

The shared `build/` currently fails to link the whole `guild` library because of an
**unrelated, uncommitted** in-flight edit by another agent in
`src/gui/gui_dialogs6.cpp` (`kPlantPriceHalf` used out of scope, line 597). That file
is outside this agent's ownership. My own sources compile and link cleanly and the new
test runs green when built against the prebuilt command objects.

## Provenance touched

- `src/sim/command_builders2.cpp` — added per-function `gilde.exe 0xADDR` comments to
  the helper-dispatched Pair/Quad builders (35/41/42/51, 37/43/52/54, 46/60). No body
  changes.
- `tests/unit/sim_command_builders2_test.cpp` — new golden test (this wave).

*All addresses gilde.exe, imagebase 0x400000. Reconstructed bodies pre-existed; this
wave verified them 1:1 and added provenance + the missing golden test.*
