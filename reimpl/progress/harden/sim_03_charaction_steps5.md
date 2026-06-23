# Harden sweep — sim/charaction_steps5.cpp (+ .h, + tests)

MCP-verified each provenance function against gilde.exe (imagebase 0x400000),
diffing decompile **and** disasm line-for-line. Float constants, struct offsets,
the float->int sites, the GameTime arg mapping, and the cmd-emit tail were all
re-derived from the binary bytes / disasm (not trusted from the existing source).

## Per-function status

| Addr | Function | Status |
|------|----------|--------|
| 0x4dc628 | FindPairedEntityReverse | VERIFIED-1:1 |
| 0x4de4b0 | ApplyTransportSpeed | FIXED (float constants) |
| 0x4db5a4 | InitOfficeGuardState | VERIFIED-1:1 |
| 0x4dda88 | CopyGoalToTargetDup | VERIFIED-1:1 |
| 0x4e0d54 | CopyGoalToTargetState2Dup | VERIFIED-1:1 (comment fix) |
| 0x4d1674 | RestorePosFinishAlt | VERIFIED-1:1 |
| 0x4d113c | StateReset24Alt | VERIFIED-1:1 |
| 0x4d1904 | StateReset0Alt | VERIFIED-1:1 |
| 0x4e1b74 | RunFollowTarget | FIXED (stamp order + give-up arg form) |
| 0x4dd1b0 | RunBuyObject | FIXED (hiword sar bug + missing FlagBlob32 tail) |

Counts: VERIFIED-1:1 = 6, FIXED = 4, BOUNDARY = 0 new (cross-cluster emits/queries
remain routed through the documented hook bridge, per rules 7/13; see notes).

## Fixes (addr + evidence)

### 1. ApplyTransportSpeed @0x4de4b0 — wrong float factors (header constants)
`get_bytes`:
- dbl_61F288 = `9A 99 99 99 99 99 E9 3F` = **0.8** (was 0.5 in header)
- dbl_61F290 = `66 66 66 66 66 66 E6 3F` = **0.7** (was 2.0 in header)

Fast tier multiplies base by 0.7; medium tier and the type-2 (+241==2) extra
multiply use 0.8. Header `kTransportSlowFactor`/`kTransportFastFactor` corrected;
unit goldens updated: FastTier 3.0*0.7=2.1f; Type2 4.0*0.8=3.2f.

### 2. RunFollowTarget @0x4e1b74 — give-up advance argument form
Disasm 0x4e1c3a `mov edx, 6` -> `Advance(rec+184, 6, 0, 0)` (edx=hours add). The
source did `GameTimeAdvance(...,0,0,6*60)` (addMinutes=360). Same final clock but
not 1:1 in arg form -> changed to `GameTimeAdvance(...,6,0,0)`. (GameTimeAdvance
@0x583150 mapping confirmed: edx=hours, ecx=seconds, ebx=minutes.)

### 3. RunFollowTarget @0x4e1b74 — clock-stamp ordering
Binary order: stamp +82; copy +82->+68; **ChangePlayerAction**; then stamp +96
(0x4e1c30, post-call). Source stamped +96 *before* ChangePlayerAction. Reordered
the +96 stamp to after the call (ChangePlayerAction receives a1=h, so a +96 read
inside it would observe the difference).

### 4. RunBuyObject @0x4dd1b0 — hiword counter (float->int adjacent: signed shift)
Disasm 0x4dd482 `mov ebx,[ebp+0AAh]` (dword @ **offset 170**), 0x4dd489
`sar ebx,10h` -> the QueueRequest17 hiword arg is `(*(i32*)(h+170)) >> 16`
**signed** (sar). Source had `(Cas5_IdA172(h) >> 16) & 0xFFFF` — wrong offset
(172 vs 170) AND wrong bytes (it took the high word of +172, i.e. bytes 174-175,
whereas the binary takes bytes 172-173 sign-extended). Fixed to
`LoadI32At(h, 170) >> 16`. Golden added: dword@170=0xFFFE0000 -> hiword == -2.

### 5. RunBuyObject @0x4dd1b0 — missing purchase-finalize emit
The apply phase (state 2) tail at loc_4DD37A (reached from BOTH the city-category
branch and its fall-through) was entirely missing: a second
`GameObject_QueryFind(*(self+93),1,1,a1[44])` then
`QueueRequestFlagBlob32(4, {*(i32*)(self+1), *(i32*)(obj+2)})` (0x4dd3f3) before
the free. The interleaved GameTime advance there (0x4dd3c0) writes the blob's low
dword which is then overwritten by `*(self+1)` (0x4dd3cf), so its result is
discarded — skipped. Added a `queuePurchaseFinalize32(buyerBlob, objBlob)` hook
(inert no-op default; byte offsets self+1 / obj+2 from disasm 0x4dd3cc/0x4dd3dd).

### 6. CopyGoalToTargetState2Dup @0x4e0d54 — comment only
`Advance(rec,2,0,0)` adds 2 to the **hour** count (wraps 24->day), i.e. +2 hours,
not "+2 days". Code was already correct (matched the existing golden hod=18);
fixed the misleading comment in both .cpp and .h.

## Notes / kept-as-is (verified, no churn)

- **FindPairedEntityReverse**: disasm confirms `[cand+4]==[cand+0xB0(176)]` then
  `[h+4]==[cand+0xAC(172)]`, return `candA ^ myId`. Matches source exactly.
- **RunBuyObject markObjectBought**: binary `or byte[seqBase+19],0xA0` is
  unconditional inside the category block; source guards `if(seqBase)` for
  crash-safety (binary would deref). Behaviorally identical when seqBase!=0.
- **RunBuyObject `*v8==301` / `!v8` guard** (0x4dd232 `cmp word[ecx],12Dh`,
  0x4dd228 `test ecx,ecx`): `ecx` is **spoiled by `Person_QueryBegin` (__spoils<ecx>)**
  between its set (`mov ecx,eax`=seqId) and these uses — the original relies on a
  clobbered register, so the source-level intent is indeterminate at the binary
  level. Kept the existing `*(u16*)(self)` class-word read and the `if(!self)`
  guard rather than introduce a `!seqBase` test off undefined data.
- **RunFollowTarget classByte** (0x4e1c12 `mov bx,[ecx]`): `ecx` likewise left by
  the preceding query calls; modeled as `self`'s first word (the plausible
  intent). Documented in-code.
- **Person_QueryBegin/GameObject_QueryFind arg collapse**: the originals are
  variadic selector-pair calls (a1@esi + count + (kind,value) pairs); the 4-arg
  hook bridge is a documented lossy approximation. The h-fields the binary reads
  in these calls (h+16 SceneCol as the selector value; Begin+93 / self+93 as the
  scene key) are preserved. Cross-cluster query iteration stays behind the hook
  per rules 7/13.

## Tests
- charaction_steps5_test (unit): **68 checks, 0 failures** (added hiword==-2 and
  finalize-call assertions; transport goldens corrected to 0.7/0.8).
- charaction_steps5_itest (integration): **17 checks, 0 failures**.
- charaction_steps5_e2e_test: **21 checks, 0 failures** (added finalize recorder).
- `cmake --build . --target guild` clean.
