# Hardening sweep — sim chunk 02 (morph / transport / tavern / social / universe)

MCP live (gilde.exe). Every provenanced function decompiled + disasm'd and diffed
line-for-line. Constants confirmed byte-for-byte via `get_bytes`. Float->int sites
checked against disasm (ConvertX @0x5c6b08 = `frndint` with RC=11 → **truncate
toward zero**; verified at 0x5c6b08).

## Constants (get_bytes, all VERIFIED)
| sym | addr | bytes | value | used as |
|-----|------|-------|-------|---------|
| dbl_610004 | 0x610004 | 7b 14 ae 47 e1 7a 94 3f | 0.02 | kFadeRate |
| dbl_61000C | 0x61000c | 00..00 e0 6f 40 | 255.0 | kFadeScale |
| flt_610014 | 0x610014 | 00 00 7f 43 | 255.0 | kFadeFull |
| flt_6102F0 | 0x6102f0 | 00 00 00 3f | 0.5 | kMorphHalfBias |
| flt_6101E0 | 0x6101e0 | 00 00 8c 42 | 70.0 | kSeventy |
| flt_6101E4 | 0x6101e4 | db 0f c9 40 | 6.2831855 | kTwoPi |
| dbl_6101EC | 0x6101ec | ..c0 | -5.0 | kHeightStepThresh |
| dbl_6101F4 | 0x6101f4 | ..40 08 | 3.0 | kDeflateHeightBias |
| flt_6101FC | 0x6101fc | 00 00 80 3e | 0.25 | kHeightLerp |
| flt_5CA2E0 | 0x5ca2e0 | 12×00 | {0,0,0} | zero pivot |

## morph (character_recon5_morph.cpp)

- **FadeComputeValue (0x4015e0..0x401676, inline ramp of FadeOutSlots): VERIFIED-1:1.**
  Branch dir/clamp order matches; v15 = v12*255; endpoint tests (|v15|==0, v15==255)
  match the disasm `test ...,7FFFFFFFh` / `fcomp flt_610014`.
- **FadeOutSlots (0x4015cc): VERIFIED-1:1.**
  Float->int packed value: disasm 0x40167a..0x40168c is `fld v15; call ConvertX; fistp`
  → truncate-toward-zero. Source `(int)v15` truncates identically. `LOBYTE` low-byte
  store is harmless for the mid-ramp range (1..254). Slot stride (v0+=3), endpoint
  flag-clear (&~0x40), ChangeTransparency(-1), SetVisible(0/1) order all match.
- **MorphComputeBlend (0x4038ed..0x403948): VERIFIED-1:1.**
  `v35 = last-first; v33 = (v35+next)/2` (signed idiv, truncates toward zero);
  `(double)v33 + 0.5`; ConvertX truncate; `(int)`. Source matches incl. odd-half
  truncation (golden MorphBlendOddHalfTruncates: 7/2=3, (int)3.5=3).
- **ReleaseMorphAni (0x403708): VERIFIED-1:1** (hook-modeled; control flow exact:
  guarded on +124, prune→FindFreeMeshSlot→ReleaseMeshData→clear +124).
- **CheckAniMorph (0x403764): VERIFIED-1:1** (hook-modeled state machine; the three
  phases — stale-primary teardown, finished-morph release, pending attach/morph —
  match the decompile; ConvertX site at 0x403943 is the same truncate as above).

## transport (character_recon5_transport.cpp)

- **MoveToUniverse (0x402d3c): FIXED (minor) + VERIFIED.**
  Transport-move now passes `ch->transport->object` directly (was a reinterpret_cast
  through an `int` sentinel). See struct fix below. All other flow (deflate-bit fixup
  from byte_62D010, IndexFromPointer arms, global-universe re-inflate) VERIFIED-1:1.
- **AttachTransport (0x402e40): FIXED.**
  - Matrix math VERIFIED via disasm (local {0,0,-70} through obj+396 affine; out
    x/y/z map to M396../M400../M404.. exactly; `ebp=0xC28C0000`=-70.0).
  - Flag fixups on the **transport object** (`[ecx+529/530/531]`) VERIFIED.
  - **FIXED:** `*(v3)=v8` stores the *transport object pointer* (ecx). Source had
    `rec->object = 1` (sentinel) losing the pointer. Changed `TransportAttach::object`
    from `int` to `TObject*` and store the real `transportObject`. Evidence: disasm
    0x402f3a `mov [esi], ecx` (ecx = transport object, the 2nd usercall reg arg).
- **UpdateTransportAttach (0x402f70): FIXED (3 divergences).**
  1. **Return value.** Binary reuses `result`: `result = VectorWithinTolerance(...);
     if(!result){...}` → within-tolerance returns the *tolerance result* (nonzero),
     not the resolved-mesh ctx (0x402fa9/0x402fb0). Source returned `meshCtx`.
     **Fixed source + golden** (UpdateWithinToleranceNoMove now expects 1, citing
     0x402fa9). No-transport path still returns ResolveMesh (0x402f90) — unchanged.
  2. **transObj = `*(a1[73])` = tr->object** (the transport's own scene object,
     disasm 0x402fa6 `mov v7, *(v6-1)`). Source used `ch->object` proxy. Now uses
     `tr->object`. Golden UpdateMovedUpdatesAnchor updated to supply a transObj.
  3. **Miss-case SetPosition.** Binary calls `SetPosition(v7, v7+76)` in BOTH the
     WorldToTileWithHeight hit (post-blend) and miss (loc_4032ED) branches; only the
     walkable/no-floor sub-case (loc_4032CB→loc_40329D) skips it. Source previously
     called SetPosition only inside the floor branch. Rewrote with `skipSetPos` gated
     on `(v34∈{0,13}) && floorHandle==0`; added `floorHandle` hook = `*(a1[34]+172)`
     (the universe-record floor handle used both as the skip gate and PickTileAtPoint
     ctx). Height-blend math (lerp 0.25, +3.0 deflate bias, >=-5.0 step) VERIFIED.

## tavern (character_recon_tavern.cpp)

- **FindTavernTargetSlot (0x4d5c60): VERIFIED-1:1.**
  Validation ladder (markerWord==0xFFFF / !enabled@+8 / type@+2!=0 / !target@+0x184),
  IsObjectForTurn predicate, slot stride (+12 / `v5+=3`), match (`target+0x2C ==
  entry+1` && `entry+0x27 word != 0xFFFF`), 16-cap and null-terminate all match the
  decompile. By-value struct is a semantic view (named fields), not byte-exact —
  faithful since the binary's reads are reproduced by member semantics.

## social (character_social.cpp)

- **VectorWithinTolerance (0x5caa4c): VERIFIED-1:1** (`fabs(b-a) <= tol`, all 3 axes;
  2nd/3rd compares promote tol to double — immaterial).
- **FindNearbyInRadius (0x40507c): VERIFIED-1:1.**
  group/world match (avatar[34]/[11]), busy-gate (action+9==45), mesh gate
  (`*(obj+533)!=1 || (sec=*(+492))&&*(sec+533)!=1`), VectorWithinTolerance on
  obj+76, append at byte cursor capped at 64, loop while i<512 && cursor<64. The
  in-loop cap guard in the source is equivalent (loop already exits at cursor>=64).
- **UpdateIdleSocial (0x405148 idle branch): FIXED.**
  **Divergence:** source cleared `+140 &= ~8` in the eligible-but-no-neighbour case.
  The binary clears the flag ONLY in the NOT-eligible branch (0x4054a9); the
  `else if (FindNearbyInRadius)` has no else — no-neighbour falls through with no flag
  clear, and a found-neighbour-but-tile-miss likewise just falls through. Removed the
  erroneous trailing flag clear. Eligibility predicates and the talk-action spawn
  (type 45, col/row from WorldToTileWithHeight, scene id) VERIFIED. (No unit test
  module for this file — verified against disasm only.)

## universe (character_universe.cpp)

- **Move2UniverseActionUpdate (0x4063c8): VERIFIED-1:1.**
  Actor = `*(a1+20)`; data0/1/2 = +48/+52/+56. Validation ladder (data0<0;
  combo `(!d0&&d1!=-1)||(d0&&d1==-1)`; MoveToUniverse fail), saved slot v25,
  SwitchActiveSlot(data0), two texture-set arms (`!d0 && 62D080==ch+44` → outer
  indoor=0; `62D080==ch+44` → inner indoor=ch+506), StopSample, entry-dummy chain
  (a1+380 skip / a1+240 name / FindByHandle fallback to "dummy_EINGANG"), field
  bookkeeping (ch+44=d1, ch[14..16]=0, ch[12]=d2), SetVisible(1), slot-mismatch hide
  (`v25==d0 && ((ch+44!=-1 && 62D080!=ch+44) || (62D084!=-1 && 62D084!=ch+48))`),
  next-is-hide (`*(a1+40)+9==56`), SwitchActiveSlot(v25), UnlinkEntry — all match.
  Render/universe/action-queue leaves are hooks (genuine boundaries).

## Handoffs
None. Only owned files + the transport struct (no external dependents of
`TransportAttach`/`AttachTransport`/`UpdateTransportAttach` beyond comments — grep
confirmed; character_render2.cpp references only the function names in comments).

## Counts
- Functions checked: 11 (5 morph/fade, 3 transport, 1 tavern, 2 social, 1 universe = 12 logical units).
- VERIFIED-1:1: 9
- FIXED: 4 (AttachTransport pointer-store; UpdateTransportAttach return+transObj+miss-SetPosition;
  MoveToUniverse transport-move typing; UpdateIdleSocial no-neighbour flag clear) — one golden
  corrected (UpdateWithinToleranceNoMove) + one golden updated (UpdateMovedUpdatesAnchor transObj).
- BOUNDARY: render/anim/heightmap/object/action-queue leaves routed through inert hooks
  (object reparent, mesh resolve, anim attach/release, transparency, heightmap sample,
  SwitchActiveSlot, FindByHandle, UnlinkEntry) — not in tree, rule-8 compliant.
- Tests: transport 33 checks / morph 45 / tavern 30 — all pass. libguild builds clean.
