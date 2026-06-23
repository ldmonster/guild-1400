# Privilege BuildEvidenceEntry — FULL BODY RECONSTRUCTED, VERIFIED & WIRED — Wave 23

**Agent:** W23-EVIDENCE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Closes the wave-22 deferral: `VIBE_Privilege_BuildEvidenceEntry @0x56589c` (747 bytes,
188 instructions) was reduced to its return-code predicate (`PrivBuildEvidenceResult`),
with the judge/witness ENTITY SCAN + the SlotReset28 command emission cited as an
"engine-leaf boundary". That boundary was NOT genuine — the person/selection arrays,
`VIBE_ObjectSearch_FindOneByPaletteRange` (pathfind_map.cpp) and
`VIBE_Command_QueueRequestSlotReset28` (command_inherit.cpp) are all reconstructed in
the live tree. Wave-23 reconstructs the COMPLETE function body 1:1 and wires the
Evidence panels to it.

## Target — status

| addr | name | status |
|------|------|--------|
| 0x56589c | VIBE_Privilege_BuildEvidenceEntry | **FULL BODY RECONSTRUCTED 1:1 + WIRED** (`PrivBuildEvidenceEntry`) |

## What wave-22 had vs. what wave-23 added

- Wave-22: `PrivBuildEvidenceResult(judgeFound, witnessesFound)` return-code predicate
  only; the body surfaced as the inert `buildEvidenceEntry` hook (returns 16/64).
- Wave-23: the full control flow — judge person scan, 248-byte opcode-28 court-record
  build (every field offset), judge/accuser sourcing from the selection arrays (+ RNG
  fallback), the two palette-range witness searches (REAL leaf), and the slot-reset
  command emission (REAL leaf). The wave-22 predicate is REUSED verbatim (no ODR) for
  the exact return codes.

## Files owned / touched

| file | change |
|------|--------|
| `src/world/privilege_panels_b.h` | + `EvidenceBuildWorld` (the engine-global view), `EvidenceBuildSink` (248-byte record + real-leaf-routed witness/emit), `PrivBuildEvidenceEntry` decl, `RunBuildEvidenceEntry` bridge decl, `EvidenceWitnessSearchViaObjectSearch` decl; + `evidenceWorld` hook on `PrivilegePanelBHooks` |
| `src/world/privilege_panels_b.cpp` | + FULL `PrivBuildEvidenceEntry` body (0x56589c); + `EvidenceWitnessSearchViaObjectSearch` (routes to the REAL `ObjectSearchFindOneByPaletteRange`); + `RunBuildEvidenceEntry` bridge; routed both Evidence panel confirm sites through the bridge; + include `sim/pathfind_map.h` |
| `src/world/office_recon_privilege.h` | comment: marked the "entity scan deferred" note obsolete (body now reconstructed) |
| `tests/unit/privilege_panels_b_test.cpp` | + 10 FULL-body golden tests (now **212 checks, 0 failures**, +41 this wave) |
| `progress/evidence-scan-wave23.md` | this file |

## BuildEvidenceEntry @0x56589c — 1:1 control flow (decompiled + disassembled fresh)

```
0x5658c7  v27/v28 = dword_55272C = {0xFFFFFFFF, 0xFFFFFFFF}   // 4-word filter blob seed
0x5658d3  judge scan: QueryBegin(op 6 = "match any") iterates every live person;
0x5658ea    break on first person whose office byte aiTable[589*type] (dword_13CE294) == 15
0x5658f2  if no judge found -> return 16                       // PrivBuildEvidenceResult(false,*)
          --- build the 248-byte opcode-28 court record (&v16, stack frame) ---
0x5658f8  rec[+0x04] = 44                                       (v17)
0x565909  rec[+0x08] = actor.handle (a1+4)                      (v18)
0x565922  rec[+0x0C] = dword_12CE914[134 * word_63CC5C]         (v19 = active selection id)
0x565926  rec[+0x36] = 2                                        (v20)
0x56592d  rec[+0x58] = actor.handle                             (v21)
0x565940  rec[+0x5C] = target.handle (a2+4)                     (v22)
0x565944  rec[+0x60/+0x64/+0x68/+0x6C] = -1                     (v23..v26 judge/wit1/wit2/accuser)
0x56595e  v27.lo = target.id (*a2)
0x565968  JUDGE sourcing (a3 = mode):
            mode!=0: judge = (a1+12 ? dword_6498EC[0] : dword_6498E8); v23=judge+4; v10=judge id
            mode==0: scan byte_12CEA76 (stride 536) for office byte 13:
                       found -> v23 = dword_12CE914[byte off]; v10 = word_12CE910[byte off]
                       v23 still -1 -> RNG: RandomModulo(2)?dword_6498E8:dword_6498EC[0]
0x5659d7  v27.hi = v10 (judge id)
0x5659f6  ACCUSER sourcing (only if a2+358 != 17 && a1+358 != 17):
            scan byte_12CEA76 (stride 536) for office byte 17:
              found && a4(matchCount) > 2 -> v26(+0x6C) = dword_12CE914[byte off]
              v28.lo = word_12CE910[ (67*idx)*8 words ]            // disasm-exact stride
0x565a6b  FindOneByPaletteRange(a1, 0xC00, &v27, 0.0, 100.0, &v31)  miss -> return 64
0x565a7d    if v28.lo==0xFFFF: v28.lo=v31[0] else v28.hi=v31[0]
0x565aaf    v24(+0x64) = dword_12CE914[134 * v31[0]]               // witness1 handle
0x565ad0  FindOneByPaletteRange(...) again                         miss -> return 64
0x565b70    v25(+0x68) = dword_12CE914[134 * v31[0]]               // witness2 handle
0x565b76  QueueRequestSlotReset28(&rec)                            // opcode-28 emit
0x565adf  return 16
```

## Reused (real, in-tree) leaves — NOT redefined (no ODR)

- `guild::sim::ObjectSearchFindOneByPaletteRange` @0x47ae48 (src/sim/pathfind_map.cpp) —
  the witness palette-range search; routed via `EvidenceWitnessSearchViaObjectSearch`
  (refId = actor id, filter blob = &v27, range [0,100], strideIndex/probeStart = the
  live RandomModulo(16)/RandomModulo(768) draws supplied through the world view).
- `guild::sim::QueueRequestSlotReset28` @0x4948c8 (src/sim/command_inherit.cpp) — the
  opcode-28 slot-reset command; production binds `sink.emitReset` to it (it needs the
  session `CommandQueue`/`PendingState`, so the binding lives in the provider).
- `PrivBuildEvidenceResult` (world/office_recon_privilege.h) — the exact return codes.
- `PrivPerson`/`PrivEvent` views + `PrivilegePanelBHooks` (privilege_panels_b.h).

## Engine-global view (EvidenceBuildWorld)

Rather than hide the globals behind opaque hooks, the body reads an explicit value view
matching each global it touches, so the scan + record build are byte-pinnable:
person types + AI office-byte table (dword_13CE294, the judge scan), the 134/268/536-
stride selection tables (dword_12CE914 / word_12CE910 / byte_12CEA76), word_63CC5C, the
two court-party player records (dword_6498E8 / dword_6498EC[0]), and the injected RNG
draws (judge fallback + the two FindOne draws).

> Stride note (disasm-exact): the JUDGE id read is `word_12CE910[byteOff]` = word index
> 268*idx; the ACCUSER id read uses the `(67*idx)*8`-word form = word index 536*idx. The
> Hex-Rays output rendered the accuser as `268*v11`; the disassembly arithmetic
> (`((v11<<4)+v11)<<2 - v11` = 67*v11, then `[eax*8]`) is 536*idx — the disasm is taken
> as the reference of record. Both are reproduced faithfully.

## Wiring (rule 13)

The Evidence panels' confirm-click now calls `RunBuildEvidenceEntry`, which:
1. prefers a thin `buildEvidenceEntry` hook if installed (back-compat / tests), else
2. runs the FULL `PrivBuildEvidenceEntry` against the world+sink supplied by the new
   `evidenceWorld` hook (production binds it to the live globals + the two real leaves),
3. is a no-op returning 0 only when neither is wired (headless posture).

`EvidenceReview` (0x565f9c, mode 0), `EvidenceReviewAlt` (0x5667a0, mode 1) and
`EvidenceDetails` (0x565b88, mode 0) confirm arms all route through it. The dispatcher
seam (`PrivilegeDispatchPanelB` -> `sim::InvokePrivilegeLeaf` -> `g_privilegeHook`) is
unchanged; install count stays 10.

## Golden pins (this wave)

- no judge person -> 16, witness search never reached, no emission.
- judge + both witnesses found -> 16, command emitted, record byte-exact:
  +0x04=44, +0x08/+0x58=actor handle, +0x36=2, +0x5C=target handle, +0x60=judge handle,
  +0x64/+0x68=witness handles; filter[0]=target id, filter[1]=judge id.
- first/second witness miss -> 64, no emission (each pinned separately).
- mode!=0 judge from dword_6498E8 (bit12=0) / dword_6498EC[0] (bit12=1).
- mode==0 no kind-13 holder -> RNG fallback (RandomModulo(2)).
- accuser sourced only when a kind-17 holder is in the selection AND matchCount>2 AND
  neither party's office byte is 17; else +0x6C stays -1.
- RunBuildEvidenceEntry routes through evidenceWorld (full body) when no thin hook.
- the witness-search helper reaches the REAL ObjectSearchFindOneByPaletteRange.

## ConvertX / truncation audit

`BuildEvidenceEntry` has no float->int store (no ConvertX/fistp). The only float in the
body is the constant favourability range `[0.0, 100.0]` passed to FindOne (the search's
own gate); the 0xC00 filter-flag the original passes to FindOne is folded into the
filter blob the reconstructed leaf consumes. Nothing to truncate.

## Build / tests

- `privilege_panels_b.cpp` + `wire_privilege_panels_b.cpp` link into `libguild.a` clean
  (full `guild` target green — the wave-22 unrelated sibling blocker is resolved).
- **`privilege_panels_b_test`: 212 checks, 0 failures** (was 171; +41 this wave).
- **`wire_privilege_panels_b_itest`: 13 checks, 0 failures** (no regression).

## Residual

NONE genuine. The full body is reconstructed and wired; every callee is a reused in-tree
reconstruction. The session-level binding of `sink.emitReset` to the real CommandQueue/
PendingState is the provider's job (the SAME documented handoff as the rest of SET-B),
not a deferred reconstruction — the leaf itself (`QueueRequestSlotReset28`) is reused.

*Addresses are gilde.exe, imagebase 0x400000.*
