# Harden verdict — src/sim/ai_meister_calc_wache.cpp

Verified directly against gilde.exe via IDA MCP (decompile + disasm + get_bytes).
Owned funcs: `VIBE_Ai_CalcMeisterWache` @0x455cd8, `VIBE_Ai_CalcMeisterAmbush` @0x4588d0
(both `__usercall`, eax=a1). Table `aSpAuflauerlege` @0x632275.

## aSpAuflauerlege @0x632275 — VERIFIED-1:1
get_bytes of 256 bytes confirms 8 entries x 32 bytes, zero-padded C strings, byte-exact:
NORDEN/OSTEN/SUEDEN/WESTEN/STADT_1..4. Matches `kAuflauerlegen[8][32]` literals exactly.
2-byte-at-a-time copy loop (`CopyAuflauerlegen`) matches 0x4560bb / 0x458b5c.

## VIBE_Ai_CalcMeisterWache @0x455cd8 — FIXED (1 divergence class, 2 sites)
Control flow diffed line-for-line against disasm:
- Weekly gate (hour<=6 / (id+hour)%3): 0x455d93 `cmp dx,6 jbe`, 0x455db3 div/test. Match.
- Staff-min guard `min(v8/2, v8-v8/2) > v79`: 0x455e50 `sar/sub/cmp jge`, 0x455e63 `cmp jg`. Match.
- Owner-kind index `byte_12CE912[536*owner]`: 0x455e78 `[edx+27h]` (kB_owner39=0x27) then
  shl4/add/shl2/sub/shl3 = 536*owner. Match.
- Mode dispatch: binary routes nobility/merchant non-emit paths to `loc_45635D` dispatch
  (==4 CalcAngriff / ==0 patrol / ==1 escort / else LABEL_139). Source `LABEL_76` +
  `DISPATCH_456367` reproduce this exactly (incl. the CalcAngriff!=0 → loc_456367 entry that
  skips the ==4 re-check so v78==4 falls to LABEL_139). Verified case-by-case.
- Merchant gate `RandomFloatScaled() fcomp 0.85 jbe; cmp ecx(idle),3 jl; 7-priceMode cmp day jg`
  (0x4562ed..0x456317). Match. RNG draw count/order verified (RandomModulo 3/2/4 + CalcAngriff).
- Count loop idle sub-condition: outer `*(ao+44)==*(emp+1)` already implies the idle re-check,
  so source `!busy && aoId==bldgId` == binary `!busy && actionObj && bldgId==*(ao+44)`. Match.
- Umland emit guard: binary `v67!=-1 && v28>=2` (first-worker-slot set AND count>=2); since
  count>=2 implies first slot set, source `collected>=2` is equivalent. Match.

**BUG FIXED — Hex-Rays pointer typing (class 1):** `VIBE_Building_FindStorableObject`
returns a `__int16*`-typed value, so decompile `*(ret+1)` is **byte offset 2**, not 1.
disasm confirms `mov eax,[eax+2]`:
- 0x456885 (escort cmd `extra0`): `rd32(storable,1)` → **`rd32(storable,2)`**.
- 0x4560fd (Umland cmd `targetId`): `rd32(storable,1)` → **`rd32(storable,2)`**.
All other `+1` reads are genuine byte +1: `*(bldgRec+1)` id `[ebp+1]`/`[eax+1]` (kB_id1=0x01),
`*(v16+1)` `[ebp+1]`, `*(v70+1)`/`*(v41+1)`. Left unchanged — correct.

## VIBE_Ai_CalcMeisterAmbush @0x4588d0 — FIXED (1 divergence class, 1 site)
- Nobility RandomModulo(2) @0x458a78; 2nd RandomModulo(2) drawn only when v65!=0 AND target!=-1
  (0x458a89/0x458a96 `jz loc_458CDC`=LABEL_51). Source matches incl. `v65==3 → LABEL_48`. RNG OK.
- LABEL_21 cmdType=0x48(72), mode=2: 0x458ad3 `mov ch,48h`, 0x458b27 `mov al,2`. Match.
- Ambush tile-danger loop is FLOAT: `fild` (signed load of zero-extended u16) + `fmul flt_6198A8`
  (0.125) + `fadd` + `fcomp/jbe` max-update + final `test eax,7FFFFFFFh jz → v73=1.0`
  (0x458d58..0x458e06). NO float->int truncation site (only fild loads + fcomp), so no
  ConvertX/fistp rounding concern. tileOff=24*col+192*row (0x458eac/0x458eb7). Match.
- Object sweep stride 169 (`add ecx,0A9h`), count 256 (`cmp esi,100h`); filters owner!=own,
  !=0xFFFF, `[ecx+5Ah]&1`==0 (+90), !=localPlayerWord, !IsProductionType (0x458e22..0x458e7d).
  Match. cmdType=0x62(98), mode=2 (0x458fb8/0x458ffb). Match.

**BUG FIXED — same FindStorableObject pointer typing:**
- 0x459048 (`v60[5]`/`extra1`): decompile `*(storable+1)`, disasm `mov eax,[eax+2]`.
  `rd32(storable,1)` → **`rd32(storable,2)`**.

## Field-offset spot checks vs disasm (all VERIFIED)
kM_bldgRec=0x16C, kM_target=0x1C0, kM_dayFlags=0x1B4 (`and byte [eax+1B4h],0DFh` @0x456225),
kP_employer=0x16C, kP_actionObj=0x184, `*(ao+44)`, `*(person+97)` (`[esi+61h]`),
kB_owner39=0x27, kB_id1=0x01, g_personIds = dword_12CE914 @ person+4 (stride 536). All match.

## Tests
Built `ai_meister_wache_test` and ran: **51 checks, 0 failures.** No golden corrections
needed — existing tests do not assert storable-derived field values, so the +1→+2 fix is
green without test edits. Owned TU and the test TU both compile with zero warnings.
(Whole-library link was transiently blocked by unrelated corrupt/WIP sibling objects —
sdl_menu / stammbaum_window truncated, character_recon5_transport cast error — not my files,
not touched; cleared the stale objects, library + test built and passed.)

## Counts
VERIFIED-1:1 functions: 0 (both had a divergence).
FIXED: 2 functions, **3 edit sites**, all one Hex-Rays `__int16*`-typed FindStorableObject
`+1`→byte `+2` bug (0x456885, 0x4560fd, 0x459048).
Golden/test changes: 0.
