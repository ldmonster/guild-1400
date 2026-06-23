# Harden sweep — src/gui/tooltip_content.cpp

1:1 verification of every provenance-carrying function against gilde.exe via IDA MCP
(decompile + disasm + get_bytes). DISASM is the reference of record.

## Per-function status

### `Tooltip_BuildBuildingContent` — 0x4f78e4 (VIBE_Tooltip_BuildBuilding) — FIXED (doc) + VERIFIED-1:1
- Color table dword_4F73F0 confirmed via get_bytes (8 dwords; qmemcpy copies 7 =
  `rep movsd ecx,7` at 0x4f78f9). Emission order/args all match: title 0x27
  (14c+1078, color), win2 icon, name 14c+1079, win3 desc 0x2A(color),
  price 0x28(salePrice), extra 0x29(record+579). All verified against disasm.
- **FINDING (original quirk, documented):** the header-icon `VIBE_Object_AddToWindow`
  @0x4f7986 takes its object id in EBX (proto @0x41ae10: a4@<ebx>), but the building
  path never loads it. EBX still holds the form-name string pointer set at 0x4f790c
  (`mov ebx, offset aTooltipTooltip_0`), preserved across the callee-saved
  RenderRichString/CenterChildWindows calls (both push/pop ebx). So the original
  passes a non-reproducible .rdata pointer (~0x620660) as the icon id — a latent bug.
  The object path @0x4f7b0c *does* load `lea ebx,[code+0xCE]`; the building path has
  no equivalent and no `+0x3F2`/1010 computation anywhere. The reconstruction's
  `code+1010` is therefore an unverified invention; it is the *intended* placeholder
  but NOT what the binary passes. Documented in-source with full disasm evidence.
  Left the value as code+1010 (the pointer cannot be carried through the integer
  AddIconObject edge, and the asserting test lives in an out-of-scope file).

### `Tooltip_BuildObjectContent` (+ helpers) — 0x4f7a10 (VIBE_Tooltip_BuildObject) — VERIFIED-1:1 / 1 FIX
- Form select (Avatar_LookupById → Waffen/Handelsgut), clamp block, window 2 icon
  (code+206 in ebx @0x4f7b0c), desc 2c+2152, name 2c+2151, win3 durability(0x21,
  u8) / price-pair 0x1F(cached,now) — all verified.
- Producer table @0x6496A9: get_bytes-confirmed. Walk @0x4f7d5a verified byte-exact:
  outer r<23 (cmp esi,17h), inner off 0..18 step 2 (var_4C 20→+21), id read
  `*(int*)(table+21r+off+2)>>16` == `RdI16(table+21r+off+4)`; building code
  `*(int*)(table+21r)>>24` = `(signed char)row[3]`; 461 (1CDh) suppression
  @0x4f7d8e. Icon code+1010, name 14c+1078. ✓
- Ingredient loop: word +46+2k (item, signed i16), +38+2k (count, u16), 4 rows,
  icon item+206, name 2*item+2151. ✓
- Weapon "used-by" mapping (Tooltip_WeaponUserCodes): every arm traced in disasm
  (0x4f7e32, 0x4f7fec..0x4f80df). Codes stored at byte 0x44+ (var_74; single at
  0x44, pair 24/25 at 0x44/0x45). Loop @0x4f7e63 reads icon `[esp+esi+0x41]>>24`
  and name `[esp+esi+0x3D]>>24 with ++esi` — both resolve to the SAME byte 0x44+esi
  per iteration, so codes[i] for both icon+name is correct. Arms: 449/450/451→{24,25};
  452/453/454→{30}; 464-466/445-448→{32}; 439-441/458-460→{30}; 461-463/442/443-444→
  {31}; 455 & others→none. ✓
- "+market" gate (Tooltip_ObjectMarketRow): full branch trace 0x4f7ebc→0x4f80e4→
  0x4f7ecd→0x4f7ed9→0x4f8125. Emits iff (id ∉{449..454} ∨ id==455) ∧ class∈{23,37}.
  Reconstruction matches exactly. Geometry 22*(rows+2)-4, icon 1039, text 1484. ✓
- No-rows → "$C" @0x4f8133. ✓
- **FIX — Tooltip_ObjectValueRatio (0x4f7c27..0x4f7c80):** disasm shows baseValue is
  loaded as a **QWORD** with high dword zeroed (`mov [var_54+4], esi(=0)` @0x4f7c4a;
  `mov [var_54], eax` @0x4f7c55; `fild [var_54]` qword @0x4f7c5d) → baseValue is
  treated as **unsigned 32-bit**, not signed. Reconstruction used
  `static_cast<double>(baseValue)` (signed i32). Changed to
  `static_cast<double>(static_cast<u32>(baseValue))`. Matches Hex-Rays `unsigned int
  v34`. Worker byte is `fild WORD` (signed i16) — already correct. ConvertX @0x5c6b08
  confirmed: sets RC=truncate (cw byte 0x1F) + frndint → truncation toward zero =
  `static_cast<int>`. Before/after differs only for baseValue ≥ 0x80000000.

### `Tooltip_BuildUpgradeContent` — 0x4f8154 (VIBE_Tooltip_BuildUpgrade) — VERIFIED-1:1
- class-29 gate → -1 (Tooltip_UpgradeApplies, REUSED) verified. Form/clamp/icon,
  desc 2c+2152, name 2c+2151 (`lea ecx,[ecx+867h]` @0x4f8246). win3 durability 0x21
  /price 0x23; baseValue 0x20 emitted unconditionally after, before owner gate. ✓
- Owner gate `v12 && *v11!=2 && *v11!=6`. 64-slot scan: word +35+2i with
  `HIBYTE &= ~0x80` (`& 0x7FFF`) == code; value=+483+i, kind=+419+i. ✓
- Effect: head 0x24; kind sar 24 (signed, `sar eax,18h` @0x4f8336); skip if k==-1 or
  k==255; textId 2k+3203; kind==3 → "+%a %s$N" else "%i%% %s$N"; args (value, textId).
  Verified against disasm 0x4f8330. ✓
- Scaled-price float→int is a BOUNDARY (ComputeMarketPrice + ConvertX), supplied
  pre-converted via env.scaledPrice. ✓

### `Tooltip_BuildPersonContent` — 0x4f84ac (VIBE_Tooltip_BuildPerson) — VERIFIED-1:1 / 2 FIXES
- Window order 1→4→2→3, header "$Z$[%1N3$]"(id), card(166,5) on ResolveStatusFlags,
  cash 0x2B, job/religion/wealth/family/spouse/class/children/skills — full emission
  order verified against decompile + disasm.
- **FIX — job line (0x4f854a..0x4f8568):** `sar edx,18h` @0x4f8560 → job code is a
  **signed byte**. Changed `p.jobCode + jobBase` → `(signed char)p.jobCode + jobBase`.
- **FIX — class line (0x4f8655):** `sar eax,18h` @0x4f8658 → class code is a **signed
  byte**. Changed `p.classCode + kPersonClassBase` → `(signed char)p.classCode + ...`.
- Religion (+13) and traits (+358/+361) are `movzx` (unsigned) — left as u8 ✓.
  Trait all-zero fallback hardcodes 0x20D=525 regardless of gender ✓.
- Child loop +104..+120 step 4, 5 entries; anyChild via ecx (0→1); skill loop 5 rows,
  label y=15i textId 4810+i, bar (100, 2+15i, i, 1162). All verified @0x4f8674. ✓
- Both fixes are no-ops for the realistic small-positive job/class enums (golden
  tests unaffected).

### `Tooltip_BuildContactContent` — 0x4f83e8 (VIBE_Tooltip_BuildContact) — VERIFIED-1:1
- Found-branch emission: form, center, win1 "$Z$[%s$]"(idx), win2 Text(idx+1),
  return. Matches decompile. The key-resolve / not-found −1 path is upstream (REUSED
  Tooltip_ResolveContact), correctly excluded. ✓

### `Tooltip_ClampToScreen` / `ClampFormToScreen` — 0x4f7aa5 / 0x4f81c2 — VERIFIED-1:1
- `(w>>16)+(x>>16) > (dword_69FFBC>>16)-16` → relayout x = screenW-16-w. Both clamp
  blocks identical in disasm; reconstruction byte-exact. Golden boundary test
  (624==624 no, 625>624 yes) confirms the strict `>`. ✓

## Constants / tables confirmed via get_bytes / get_global_value
- kTooltipProductionTable @0x6496A9 (23×21+3) — matches source bytes.
- dword_4F73F0 building colors — confirmed (8 dwords; 7 copied).
- ConvertX @0x5c6b08 — truncate-toward-zero (cw 0x1F + frndint), == (int) cast.

## Counts
- Functions/helpers reviewed: 9 (5 builders + 4 helpers/clamp).
- VERIFIED-1:1: 7  (BuildObject body, BuildUpgrade, BuildPerson body, BuildContact,
  ClampToScreen, WeaponUserCodes, ObjectMarketRow).
- FIXED: 3 sites — ObjectValueRatio (unsigned baseValue), Person job (signed byte),
  Person class (signed byte).
- DOCUMENTED QUIRK (boundary): 1 — BuildBuilding header icon id = leftover EBX
  (form-name pointer); non-reproducible, kept code+1010 placeholder with evidence.

## Build / test
- `cmake --build build --target session_panels_test gui_tooltip_dispatch_test -j` — clean.
- `ctest -R "session_panels|gui_tooltip_dispatch_test"` — 2/2 PASS.
