# Hardening cluster: drag / edge-scroll / groundplan (gui01)

Owner files (all under `src/gui/`):
- `dragselect.cpp` · `dragtext.cpp` · `edgescroll.cpp` · `groundplan.cpp`

Tests: `dragselect_wave20_test`, `edgescroll_wave20_test`, `groundplan_test`,
`groundplan_recon_test` (the last covers the pure half in `src/world/`, not owned
here, but builds against the shared lib — kept green).

MCP: every `gilde.exe 0xADDR` function below was decompiled AND disassembled and
diffed line-for-line (control flow, branch conditions, switch arms, all float->int
sites, fixed-point shifts, constants via get_bytes, RNG (none here), struct
strides, side-effect order, return value).

Status legend: VERIFIED-1:1 / FIXED / BOUNDARY.

---

## dragselect.cpp

| addr | name | status |
|------|------|--------|
| 0x4be154 | DragSelect_DrawBox / BoxSegments | VERIFIED-1:1 |
| 0x4bdc3c / 0x4bdecc | ApplyToUnits / ApplyToSelection | VERIFIED-1:1 (orchestration) |
| 0x4ba2bc | UpdateUnitList | FIXED |
| 0x41fcbc | DragCursor_SetSprite | VERIFIED-1:1 |
| 0x41fa1c | DragCursor_Render (BadgeLayout) | VERIFIED-1:1 |

### 0x4be154 DrawBox — VERIFIED-1:1
Disasm-confirmed the four `DrawLineLocked(eax,ecx,ebx,[stack]=-1)` calls:
- call1 (0x4be176): eax=ax, ecx=edx=ay, ebx=bx -> (ax,ay,bx,-1)
- call2 (0x4be191): ecx=by then `inc ecx`->by+1, eax=bx, ebx=eax=bx -> (bx,by+1,bx,-1)
- call3 (0x4be1ab): eax=bx, ecx=edx=by, ebx=ax -> (bx,by,ax,-1)
- call4 (0x4be1c5): eax=ax, ecx=ay, ebx=eax=ax -> (ax,ay,ax,-1)
`DragBoxSegment{x1,y1,x2,color}` endpoints + the `inc ecx` (by+1) match exactly.
Gate `dword_11BC24C` (box.active). color = -1 (0FFFFFFFFh stack push).

### 0x4bdc3c / 0x4bdecc Apply — VERIFIED-1:1 (orchestration; math in play::)
Decompiled 0x4bdc3c fully. Structure matches: gate on dword_11BC24C; clamp the
dragged corner with the floor-to-lo / cap-to-(hi-1) idiom (play::DragClampX/Y);
normalize to v4=min(ax,bx) v23=max v24=min(ay,by) v22=max (play::DragSelectNormalize);
loop, per-unit gate (handle!=0, *(slot+8)!=0, inBox=0, flags&0x800, mesh present),
then 8-corner centroid (stride 20 floats, 8 iters) * weight, project, contain-test
`v4<=sx && v23>=sx && v24<=sy && v22>=sy`. The projection/normalize/clamp are
delegated to `play::` helpers (NOT owned here).
- NOTE (cross-module, flagged for play:: owner): `dbl_61E208`/`dbl_61E210` are
  **doubles** (`0x3FC0000000000000` = 0.125, get_bytes-verified), so the weight
  product `sumX*0.125` and `1.0/(0.125*sumZ)` stay in x87/double. The local helper
  `picksel_const::kEighth` is a `float` 0.125f. Whether the centroid hit-test keeps
  double precision is `play::DragUnitCentroidHit`'s concern (src/play, other owner).
- The binary loops a fixed `i != 32` over `dword_11BB6A0[]`; the reconstruction
  parameterizes `count` for testability (per-element logic identical). selFlag in
  the binary comes from `Building_ComputeSelectionFlags(...) & 0x800`; modeled as
  `unit.selFlag & 0x800` (kDragSelectFlagBit = 0x800, verified).

### 0x4ba2bc UpdateUnitList — FIXED
Coupled command-queue/AI driver; the state machine + scan order + 67-kind gate +
>4 early-out + tail flushes are reconstructed; the queue/character/query calls are
hook-routed coupled leaves.

Disasm-verified field semantics (Hex-Rays `v2` is `edi`, a persistent register):
- `dword_12CEA7C` = the recon's `owner` (= a1, compared to player `dword_11BC2F4`,
  stored in v31). `dword_12CEA8C` = the recon's `target` (= v2/edi, the kind-byte
  source `*v2==67`, and the Entity29 tail subject `*(v2+112), v2`).
- 0x4ba325-32d: `v32 = 67` AND `v2 = target` are set UNCONDITIONALLY on a valid
  first-touch (owner & target nonzero), BEFORE the `*(target)==67` kind check at
  0x4ba334. So a non-char first-touch still arms v32 (-> retry block) and v2
  (-> Entity29 tail); only the queue work is gated on kind 67.

**FIX 1** — `v2`/Entity29-tail tracking. Before: `v2cell` (the Entity29 tail gate)
was only set inside the kind==67 branches, and `lastOwner` was assigned `c.owner`.
After: `v2cell = i` and `lastTarget = c.target` are set unconditionally in the
first-touch valid block (matching v2=target/edi being assigned before the kind
check at 0x4ba327). Evidence: disasm 0x4ba327 `mov edi, dword_12CEA8C[esi]`,
0x4ba32d `mov [esp+var_1C], al(=67)`, kind check only at 0x4ba334.

**FIX 2** — retry-compare. Before: `c.target == lastOwner` (lastOwner held owner).
After: `c.target == lastTarget` where lastTarget == v2 == previous target.
Evidence: disasm 0x4ba4e4 `cmp ecx(=v31), dword_12CEA7C` (owner) AND 0x4ba4f0
`cmp edi(=v2), dword_12CEA8C` (target). Renamed `lastOwner`->`lastTarget`.

**FIX 3** — retry block no longer reassigns the Entity29 cell. The binary does NOT
reassign edi(=v2) inside the retry block, so the Entity29 tail keeps the FIRST
first-touch target; removed the stray `v2cell = i` from the retry block.

SlotReset28 tail (0x4ba602): binary passes `QueueRequestSlotReset28(&v21, a1)` where
`a1` is a recycled follow-slot register (genuine coupled garbage). Modeled with the
pending owner `v31`; only the call COUNT is observable (tested), not the arg value.

Constants: kCharKind=67 ('C'), kUpdateMaxBatch=4 (v1>3 -> stop), stride 536,
scan-limit 411648 (768 cells) — all verified.

### 0x41fcbc SetSprite — VERIFIED-1:1
a2!=0: if slot!=-1 free (ShapeAnim_GetSlot); state=State_Update(a2);
slot=ShapeAnim_RegisterSlot(x,y,0,state). a2==0: if slot==-1 mode=0; else free,
slot=-1, mode=0. (Original assigns v3 garbage then overwrites with RegisterSlot —
net free+reregister, as documented.)

### 0x41fa1c Render (BadgeLayout) — VERIFIED-1:1 (geometry)
Gate `!dword_62D314` (rs.hidden). Loop v11 in {0,3,..,15} (v11!=18, 6 entries,
stride 3 in dword_75B9F0[]); skip == -1.
`badgeY = (dword_69FFB0 + 26)*(v10/3) + cursorY` (0x41fb46);
`badgeX = 26*(v10%3) + cursorX + (*(int*)(84*dword_62D2C4+dword_62D204+78) >> 16)`
(0x41fb4e, the +78 x-bias pre-resolved into rs.xBias). Matches.
(The shape/label blits are the Vulkan/SDL boundary — rule 3.)

---

## dragtext.cpp

| addr | name | status |
|------|------|--------|
| 0x4ad508 | BeginDragText | VERIFIED-1:1 |

Side-effect order matches disasm exactly: SetSprite(this,0), ResetTable,
ResetMouseButtonState, then the 2-byte-stride strcpy byte_61D9C8 -> byte_11B6B20,
then `dword_631678 = dword_62EB38` (startTick=gameTick), SetTooltipText(byte_61D9C8),
`dword_62D0D4=1` (active), `dword_62D0C4 = (cursorX>>16)-8`, `dword_62D0CC=0`,
`dword_62D0D0=0`, `dword_62D0C8 = cursorY>>16`, return dword_62D0C8. The bounded
copy (kDragTextBufBytes=256) is a safe equivalent of the binary's NUL-terminated
unbounded copy.

---

## edgescroll.cpp

| addr | name | status |
|------|------|--------|
| 0x543994 | MapView_UpdateScrollState | VERIFIED-1:1 |
| 0x4bc07c | Hud_UpdateEdgeScroll | BOUNDARY (partial model) |

### 0x543994 MapView_UpdateScrollState — VERIFIED-1:1
Three blocks confirmed against disasm:
- Block 1 (press latch, 0x5439ad): anchor = cursor>>16 (sar), snapshot offsets
  (a1+600/a1+584) and dead-band (62D0C4/C8/CC/D0), reprogram band:
  loX=anchorX-snapOffX, hiX=worldW-512+anchorX-snapOffX, loY/hiY analogous
  (worldH-360), bandArmed=0, latched=1.
- Block 2 (release, 0x543b4c): restore the snapshot band, `dword_63D4F4/F8 = esi`
  where esi=dword_672238=pressFlag=0 (disasm 0x543a97 `mov esi, dword_672238`),
  bandArmed=1, latched=0, return 0. The ConvertY at 0x543b9b discards its result —
  correctly NOT modeled.
- Block 3 (held, 0x543abb): deltaX = trunc((dragOriginX>>16)-anchorX) via
  fild->ConvertX->fistp, deltaY likewise; newOffX=snapOffX+deltaX,
  newOffY=snapOffY+deltaY; changed = (newOffX!=offX || newOffY!=offY) — confirmed
  the X-then-Y short-circuit (disasm: `cmp ecx,ebp; jz loc_543BB2` checks X first,
  loc_543BB2 then checks Y). Both offsets written to a1+600 / a1+584.
ConvertX truncates toward zero; inputs are integer-valued (diff of two >>16 ints)
so truncation is identity here, but the call is preserved for fidelity.

### 0x4bc07c Hud_UpdateEdgeScroll — BOUNDARY (partial behavioral model)
The non-detached path is reconstructed faithfully (the two
VectorWithinTolerance(node±tol 0.1) gate -> settled => selReset-only;
not-settled => updateListener + selReset-if-live). The DETACHED path
(`dword_62D4E8`) is a DISTILLED model: the reconstruction does FreeObjAnim ->
clear E8/E4 -> (settled?setNodePose) -> updateListener -> selReset. The binary's
settled-detached path `goto LABEL_3`s into a block gated on the runtime frame
counters `dword_631610 > dword_631618 && dword_631610-dword_631614 <= dword_63161C`
and an aux-node callback through `dword_631728+468/+396`. Those counters/node are
coupled engine runtime state not in this reconstruction's tree (rule 8 boundary;
get_global_value returns null/BSS for 631610/614/618/61C/631728). Documented in
the header. Tests (the four EdgeScrollW20_Reattach cases) cover the modeled paths.

---

## groundplan.cpp

| addr | name | status |
|------|------|--------|
| 0x4aea5c | PickBlueprintName (switch) | VERIFIED-1:1 (one documented case-3 limit) |
| 0x4aea5c | LoadBlueprintBmp | VERIFIED-1:1 (engine-leaf routed) |
| 0x4af4a8 | ComputeClockHands (clock math) | FIXED |
| 0x4af4a8 | BuildInfoPanel | VERIFIED-1:1 (structure; coupled leaves routed) |
| 0x4ae678 | SetWidgetsVisible | VERIFIED-1:1 |
| 0x4ae828 | DestroyWidgets | VERIFIED-1:1 |
| 0x4ae3b8 | CreateWindow | VERIFIED-1:1 |
| 0x4af038 | RenderBlueprint | VERIFIED-1:1 (structure) |
| 0x4b0758 | FadeInScene | VERIFIED-1:1 (structure) |

### 0x4af4a8 ComputeClockHands — FIXED (the load-bearing math)
All FP constants get_bytes-verified: dbl_61DC88=0.8, dbl_61DC90=4.0,
dbl_61DC98=1/60 (0x3F91111111111111), dbl_61DCA0=2pi (0x401921FB54442EEA),
flt_61DCA8=1/2pi (0x3E22F983), dbl_61DCB0=48.0, dbl_61DCB8=0.5.

- minute hand (0x4afd35): `minutes = (qword>>32)%60` (= hi%60); cell =
  trunc(minutes*0.8). VERIFIED.
- moon hand (0x4aff8f): cell = trunc(48.0 + fmod(phase,2pi)*(1/2pi)*48.0 + 0.5)%48.
  VERIFIED.
- **hour hand (0x4afdd2) — FIX**: `secsTotal = 60*WORD1(qword) + HIDWORD(qword)`.
  WORD1(qword) is bits 16..31 of the 64-bit packed time = `(lo>>16)&0xFFFF`
  (the SECOND 16-bit word, in the LOW dword), NOT `(hi)&0xFFFF` (which is WORD2).
  Before: `word1 = (in.hi)&0xFFFF`. After: `word1 = (in.lo>>16)&0xFFFF`.
  Also preserved the x87 multiply ORDER `*4.0 *(1/60)` (was `*(1/60) *4.0`).
  cell = trunc(secsTotal*4.0*(1/60))%48.
  Evidence: decompile `v133 = 60*WORD1(qword_13CE854)+HIDWORD(qword_13CE854)` then
  `v69 = (double)v133 * dbl_61DC90 * dbl_61DC98`.

  GOLDEN FIX (groundplan_test.cpp `HourHandModulo48`): the old golden encoded the
  wrong WORD1 (claimed hi==1 -> WORD1=1). Corrected: hi=1,lo=0 -> WORD1=0,
  HIDWORD=1 -> secsTotal=1 -> trunc(0.0667)=0 -> %48 = 0 (was 4). Added
  `HourHandWord1FromLowDword` (lo=0x10000 -> WORD1=1, secsTotal=61 -> 4) to pin the
  low-dword source. Header comment corrected (WORD1 = (lo>>16)&0xFFFF).

### 0x4aea5c PickBlueprintName — VERIFIED-1:1 (one documented case-3 limit)
Switch on MapTypeToCategory(typeByte) confirmed:
- case 1: typeByte==22 wirtshaus; roomByte==14 parfuemerie; roomByte==8 tinkturei;
  else break -> generic handwerksbetrieb.
- case 3: typeByte==15 rathaus; else typeByte==1 Arbeiterunterkunft; then `goto
  LABEL_9` ALWAYS.
- case 5: zunfthaus.
- default: typeByte==5 geldleihe; roomByte==9 lagerhaus; roomByte==7 kirche;
  roomByte==19 stadtwache; else break -> generic.
roomByte = *(BYTE*)(589*typeByte + dword_13CE294).
LIMIT (documented): the binary's case-3-no-match keeps the PRIMARY path
`riss_<subtype>.bmp` (the first sprintf, derived from the engine string table at
v32+1) rather than the generic name; the isolated pure helper has no access to that
engine string and returns "riss_handwerksbetrieb.bmp". This is the only path the
standalone helper cannot reproduce (data-not-in-tree, rule 8). The test golden
encodes the helper's value, not the binary's, and is noted as such.

### 0x4aea5c LoadBlueprintBmp / 0x4af4a8 BuildInfoPanel / 0x4af038 RenderBlueprint
/ 0x4b0758 FadeInScene — VERIFIED-1:1 (control structure)
Decompiled in full. The reconstructable structure (path selection, the channel-swap
pixel copy set(j,i, rgb[1],rgb[0],rgb[2]), the `_C.bmp` collision-map name derive,
the 15-room hotspot scan, the dirty-rebuild gate, the teardown/recreate branch on
`rebuild&1`, `result = 67*selectedPlot`) is reproduced; the surface lock/blit,
shape-grab, decompress, property/text leaves are engine clusters routed through the
backend (Vulkan/SDL boundary + data not in tree).

### 0x4ae678 SetWidgetsVisible — VERIFIED-1:1
Call order: Form_SetObjectsVisible(631768) -> EventPanel_SetBarVisible ->
Form again -> SetVisibleRecursive(6316E4,E8,EC,F0,F4,F8,FC) -> 6317AC ONLY if !vis
-> 631704 -> 631708 -> 631700(table) -> 15 room slots (11BC1F0). Matches.

### 0x4ae828 DestroyWidgets — VERIFIED-1:1
Form(631768,0) -> destroy 6316E4,E8,EC,F0,F4,F8,FC,631704,631708 (each !=-1, set -1)
-> if 6317AC!=-1 SetVisibleRecursive(6317AC,0). Matches.

### 0x4ae3b8 CreateWindow — VERIFIED-1:1
Coord_Push(0,0,screenW,screenH) -> Hotspot_Register(screenH-80,17,48,48,0) ->
Window_Create(4,10,48,648,0) -> PositionAtCoord(win,1) -> (FreeDebug backdrop) ->
clear 15 slots (11BC1F0[i]=-1) -> sceneFlag 631714=a1 -> Surface_Create(.,110,3) +
ColorFill (631644) -> CreateObject_Thunk 6317AC, set +72=1, hide. The backdrop
shape-grab and the city-rect globals (69FF80-8C = 63CC4C/50/58/54) are scene leaves.

---

## Counts

- Binary-addressed functions verified/fixed: **17**
  - VERIFIED-1:1: 14
  - FIXED: 2 (UpdateUnitList 0x4ba2bc; ComputeClockHands hour-hand 0x4af4a8)
  - BOUNDARY: 1 (Hud_UpdateEdgeScroll 0x4bc07c — coupled frame-counter path)
- Documented limit (data-not-in-tree): PickBlueprintName case-3-no-match primary path.
- Source fixes: 2 functions (dragselect.cpp UpdateUnitList; groundplan.cpp hour-hand)
- Golden/test fixes: groundplan_test.cpp HourHandModulo48 (4 -> 0) + new
  HourHandWord1FromLowDword.
- Header comment fixes: groundplan.h (WORD1 semantics), dragselect.cpp inline docs.

## Test status (all green)
```
dragselect_wave20_test ... Passed
edgescroll_wave20_test ... Passed
groundplan_recon_test .... Passed
groundplan_test .......... Passed
```
(`GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R ... --output-on-failure`)
