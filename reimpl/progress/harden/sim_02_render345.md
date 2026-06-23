# Hardening sweep — sim cluster 02: character_render3 / render4 / render5

Scope: every provenanced `gilde.exe 0xADDR` function in
`src/sim/character_render3.cpp`, `character_render4.cpp`, `character_render5.cpp`
diffed line-for-line against the binary (Hex-Rays + disasm, constants verified via
get_bytes / get_global_value). Owned files only; tests under `tests/unit/`.

MCP live (module gilde.exe). Disasm is the reference of record where Hex-Rays
collapsed __usercall args / float→int sites.

## Result counts
- VERIFIED-1:1 : 28
- FIXED         : 4  (UpdateNeedsDecay clamp, ScreenToWorldRay divisor,
                      PlayFootstepSound no-terrain sample case, AttachAni convert order)
- BOUNDARY      : 5  (ResolveHeadBone scan+staff, QueryTerrainType tail, ComputeTargetTile
                      TileToWorld stamp, CountByType return/role-sign, FlushPendingMesh
                      hook arg — all documented data/render leaves)
- Golden tests fixed (encoded wrong behavior): 3
- Standalone test run: 171 checks, 0 failures (character_render3 + render5).

---

## character_render5.cpp

### FIXED — UpdateNeedsDecay 0x4521cc  (multiple constant/clamp bugs)
Evidence (get_bytes):
- `dbl_619110` @0x619110 = `00 00 00 00 00 40 8f 40` = **1000.0** (double).
- `dbl_619118` @0x619118 = `fc a9 f1 d2 4d 62 60 3f` = **0.002**.
- `flt_619120` @0x619120 = `00 00 48 c2` = **-50.0**.
- `dbl_619128` @0x619128 = `00 00 00 00 00 00 00 40` = **2.0**.

Binary 0x452217 clamp: `if (v3 >= 0.0 && v3 >= dbl_619110[=1000.0]) v11 = 1000.0;
else v11 = (v3>=0.0)?v3:0.0; *(a1+300)=v11;` — the ceiling compare uses **1000.0**,
not the 0.002 eps; the stored value is written verbatim (no extra 1.0 clamp).

Before (reconstruction): used `kNeedDecayEps` (0.002) as the ceiling THRESHOLD
(`prim >= 0.002 -> 1000`), and added a bogus `primClamped = (v11>=1.0)?1.0:v11`
clamp that does not exist in the binary. Same wrong threshold for case 3 / else
(`nv >= 0.002 -> 1000`). The active-need weighting used the bogus `primClamped`.

After: threshold = 1000.0 for primary, case-3 and else (`if (nv < 0.0 || nv < 1000.0)
clamp[0..] else 1000.0`, matching disasm 0x4524a6 / 0x452564); the stored value is
written verbatim; the active-need weighting reads the just-written `*(a1+300)` (==v11)
times `(2.0 - x*0.002)`; non-active dominant compare promotes to double
(`val >= (double)best`, 0x452594). x87-double accumulation modeled (`double`).

Golden fixed (tests/unit/character_render5_test.cpp NeedsDecayDominantFlip):
`need[12]` was asserted == 1000.0 (wrong, from the 0.002-threshold bug); the correct
value is 120 + 2.2 - 120*0.01 = **121.0** (below the 1000 ceiling). dominant still 12;
result 36; +300=0; +304=12 unchanged. Verified by standalone run.

### VERIFIED-1:1 — UpdateAllNeeds 0x452190
Walk persons (stride 536 == kPersonStride), skip marker == -1, call UpdateNeedsDecay,
return last result. Matches `for(i;i!=411648;i+=536)` + inner skip loop.

### VERIFIED-1:1 — CheckQueueReady 0x403474
`result=1; if(+112||+128){ v1=+112; if(!v1) return 0; v3=(*(+104+340)==-1)?+328:+340;
if(v3 > *(+112) && (*(+109)&0x20)==0) return 0;} return 1;`. Exact.

### VERIFIED-1:1 — CountByType 0x4b092c (counting logic)
A-then-B bucketing: `role==wantA -> ++out2 (and continue, skip B)`; else
`role==wantB -> ++out3`; skip on id==-1 / unresolvable. Matches 0x4b0996/0x4b09b6.
BOUNDARY notes (documented model, hook-resolved data): (a) engine `role = *(rec+354)>>24`
is a SIGNED arithmetic shift, so a high-bit role is negative and never matches an
unsigned byte wantA/wantB; the hook models role as u8 (positive). (b) engine `return a1`
(base ptr) — reconstruction returns `scanned` (deterministic model; callers ignore eax).
(c) def-table read `589*type + dword_13CE294`, +559/+560 — supplied via the hook arg.

### VERIFIED-1:1 — GetIndexThunk 0x4b1e08 -> GetSeasonFromDay 0x58339c
Thunk -> `*a1 % 4`. C `%` truncates toward zero == x86 idiv. Exact.

### VERIFIED-1:1 — SetCameraViewMode 0x43d8f0
StrCmp (case-sensitive) dispatch CLOSEUP/LEFT_SHOULDER/RIGHT_SHOULDER/EGO -> mode 0/1/2/3;
unknown -> return 0; null `*a1` -> ReportError("MoveCharacterCamera(): Invalid character")
+ return 1. Exact.

### VERIFIED-1:1 — CmdGetCharacterHandle 0x43d208
FindByName; miss -> sprintf "GetCharacterHandle(): character '%s' not found" + report;
returns found (null on miss; binary returns uninitialized ecx — null is the intent).

### VERIFIED-1:1 — CmdGetCharacterSubObjectHandle 0x43de10
`*a1` -> FindByHandle(mesh, 320, name, 0, extra); else
ReportError("GetCharacterSubObjectHandle(): invalid character") + return null. Const 320 ✓.

### VERIFIED-1:1 — StopSample 0x405894
`if(+140 & 0x10){ if(+116){ if(*(*(+52)+460)==0) TouchMeshFrames; if(*(*(+116)+104))
Prune(*(+52)+492+244); +116=0;} +140 &= ~0x10;}`. Reconstruction adds a null-mesh guard
(robustness; binary would deref null on invalid data). Behavior identical for valid data.

### VERIFIED-1:1 — QueryTerrainType 0x404650 (data path)
ResolveMesh; world {+76,+80,+84}; WorldToTileWithHeight; `code = *(signed char*)
(24*(mapStride*row + col) + base+36)`; `if(!force){ if(code==0) ret; if(code==13) ret;}`.
FIX: code is read as **signed char** (0x4046d0) — added `static_cast<signed char>` to the
terrainCodeAt hook result (matters for codes >= 0x80). BOUNDARY: the floor-pick /
SetPosition tail (uses dbl_6103A4) is a pure render side effect; it does not change the
returned code (inert in isolation, as documented).

### VERIFIED-1:1 — ComputeTargetTile 0x404f6c (integer/RNG path)
RNG draw order: tx (col offset) first, ty (row offset) second; each `(u16)RandomModulo(10)
- 4 + srcTile`. Clamp `min(mapDim-2, .)` then `max(2, .)` (0x404fe6.. / 0x405067 jumps).
TraceLineOfSight success -> outCol/outRow; failure -> srcCol/srcRow; TileToWorld(col,row).
mapDim read `*(map+32)`. BOUNDARY: engine resolves the map as `*(off_649D64+0xB0)` and
calls TileToWorld (render leaf) — reconstruction takes the map as a param and stamps the
chosen tile coords into outWorld (deterministic model; the render conversion is hooked).

### VERIFIED-1:1 — FlushPendingMesh 0x426924
`if(dword_62D4E8){ v2=*(+45); dword_62D4E4=1; if(v2&0x20){ FreeObjAnimData; busy=0;
pending=0;}} if(dword_62D564){ if(off_649D64==*(+520)) UpdateDayCycle;}`. State
transitions exact. BOUNDARY: FreeObjAnimData's 1st arg is dword_13FCD1C in the binary,
not the pending ptr — modeled via the inert hook (call-count only).

---

## character_render3.cpp

### FIXED — ScreenToWorldRay 0x426850  (divisor bug)
Disasm 0x4268ff: `fdiv [esp+var_1C]`. var_1C is the rotated ray's **dir.y** (the 2nd
output of the 3x3 multiply, stored at var_20/var_1C/var_18 = dir.x/dir.y/dir.z), NOT the
focal length (focal lives at var_28). `v10 = (a5-a4)/var_1C; scaled = dir * v10`.
Before: `k = (zFar-zNear)/focal`. After: `k = (zFar-zNear)/dy`.
Base vectors verified: 0x425DB0 region = {0,0 | 0,0 | 1.0,1.0} so the {0,0,1} forward
basis is correct for ProjectRayDirection.
Golden fixed (ScreenToWorldRayGolden): scaled was `dir*(10/256)` — corrected to
`dir*(10/dir.y)` = {-13.33333206, 10.0, -14.22222137} (computed from the reconstruction;
scaled.y == 10.0 since dir.y*(10/dir.y)). Verified by standalone run.

### FIXED — PlayFootstepSound 0x40905c  (no-terrain-kind sample is lowercase)
Disasm 0x4090f5 -> loc_4091F3: when `*(*(a1[5]+136)+172) == 0` (no terrain kind), the
played sample is **aNormalS_0 @0x62d012 = "normal_s"** (lowercase). The terrain-kind path
seeds a buffer with aNormalS_1 @0x6107d4 = "Normal_s" (capital) then overwrites with the
suffixed sample. Before: reconstruction used the capital "Normal_s" for both. After: added
`kSndNormalLower = "normal_s"` and use it for the `!hasTerrainKind` branch. The terrain
dispatch (3/4/6/8/10/11 -> Erde/Wiese/Stein/Stein/Pfuetze/Stein, default -> "Normal_s")
is correct. No RNG draws (confirmed via disasm). Golden fixed (FootstepNoTerrainKind...:
now expects "normal_s").

### FIXED — AttachAni 0x404038  (convert ordering)
Binary 0x404054: `ConvertBackslashToSlash(name)` is called FIRST (in place), THEN the
path "character/%s/%s_%s.baf" is built from the normalized name, then the dlg "%s_%s" is
built and normalized. Reconstruction was missing the leading convert(name) and built the
path before normalizing the dlg. After: convert(name) first, then path, then dlg+convert.
(Mask, loop flag, FindFreeMeshSlot gate, a1+133=-1, a1+112=ch all already correct.)
mask = 0x20000 | (((8*(seq&1))|0xD0)<<8) — verified.

### VERIFIED-1:1 — AttachMotion 0x4032f8
path from base + motion-table name; mask **153600** (0x25800); always LoadStreamToStock
flag 1 when no free slot; ch+60 owner local?dword_62D090:-1. (The motion clip name is the
caller-resolved dword_66FCD0[19*idx+1]+1 entry — supplied as `motionName`, documented.)

### VERIFIED-1:1 — AttachMovementAni 0x4034c4
`if(+128) return 0;` convert(name); path; dlg+convert; FindFreeMeshSlot; loop = StrCmpNoCase
gehen/karren; +128 = slot? slot : LoadStreamToStock(path, loop); `if(!+128) return 0;`
+132 = dir; ++*(+128+332); return +128. Exact.

### VERIFIED-1:1 — DetachMorphAni 0x4035d0
hasMorphSource(result[28]) guard; `if(FindFreeMeshSlot()) return;` (binary body runs only
when slot==NULL); sprintf "morph_%i"; if meshGeom(*(v2+460)) CreateMorphAnim; if morph
AttachToBone(+492+244, **0x20000|(24<<8)** == 0x21800), result[31]=ch, +60 owner local?
dword_62D08C:-1; if computeDelta ComputeBoneDelta; Prune; result[28]=0. Mask verified.

### VERIFIED-1:1 — PreloadAniSet 0x403c34
per name: skip empty; loop=StrCmpNoCase gehen/karren; path; dlg+convert; if(!FindFreeMeshSlot)
{ LoadStreamToStock(path, loop); SetGrayColorThunk(0,4); AttachToBone(*(+52)+492+244,
**153600**); Prune;}. Exact.

### VERIFIED-1:1 — PreloadAniSetByName 0x403f14
As PreloadAniSet but LoadStreamToStock flag **0** (never loop) and the global anim base.

### VERIFIED-1:1 — PreloadLowPolyAniSet 0x403da0
per name: skip empty; path "lowpolycharacter/%s/%s_%s_LOW.baf"; dlg "%s_%s_LOW.baf"+convert;
slot=FindFreeMeshSlot; SetGrayColorThunk; mask **133120** (0x20800); if(!slot) slot=
LoadStreamToStock(path, **1**); if(slot){ AttachToBone(*(+492)+492+244, 133120); Prune;}.
NOTE (struct-offset): engine reads the low-poly base NAME from `a1+496`, regular preload
from `a1+304`. CharActor3 models a single baseName; the path/mask/flag logic is exact and
the test asserts the two names coincide (the common case). Documented field-source detail.

### VERIFIED-1:1 — AttachItemToBone 0x4068c0
slot switch 1->+26("d3_LeftHand"), 2->+25("d3_RightHand"), 3->+27("d3_Head"); bracketed by
SwitchActiveSlot(IndexFromPointer(a1[34])) restored on every exit; name path:
detach-existing, AttachToUniverseNode(mesh, mat, name, off), node flag fixups
(+530|=0xC, +529&=~2, +531&=~4, +535=5 — comments on the opaque hook node), BuildObjectCache,
bone switch (default -> early restore+return), AttachToBone; null-name path: detach only.
Exact control flow.

### BOUNDARY — ResolveHeadBone 0x57c5d4  (data-table walk, non-deterministic staff divisor)
Two helpers model the two branches. Scan branch: walks `*(v3+480)` bones (stride 64, base
*(v3+516)) for the first present bone matching "kopf"/"abt_kopf", writes `a1[99] = v6+1468`
(0x5BC == 1468 verified). The engine's match index `v6` is a counter over the abt_kopf
name-alternation table (byte_640690), not the raw bone index — a data-table detail the
helper models as the bone index. Staff branch: `ResolveStaffModel(*v14)`, count valid
entries (`+36 != -1`, 1..4), `idx = *(v14+1) % divisor + 36; a1[99] = model[idx] + 1468`.
**The divisor (ecx) is NOT initialized in this branch** (disasm 0x57c70c `inc ecx` with no
preceding zero; ecx is the leftover from the prior ApplyHeadVariant call at the only spawn
caller 0x57c888) — the original's modulo is non-deterministic (UB). The helper models the
deterministic intent: `recordId % count` (count of valid bones, clamped >= 1). Documented.

### VERIFIED-1:1 — NoiseTimerUpdate 0x4049ec  (no RNG)
dbl_6103B4 @0x6103B4 = `9a 99 99 99 99 99 d9 3f` = **0.4** (kNoiseStep). flt_5CA2E0 triple =
{0,0,0}. dbl_6103AC = -15.0 (kSitOffsetY). dir = normalize(noisePos - boneWorld) * 0.4;
pivot = dir + meshPivot; SetPivotVector; world = pivot + meshOrigin; timer dec (saturating)
BEFORE the tolerance check; VectorWithinTolerance(world, target, 0.40000001) -> reset:
timer=0, SetPivotVector({0,0,0}), SetPosition(world). Order + constant exact.

### VERIFIED-1:1 — ProjectRayDirection 0x426764
base forward {0,0,1} (0x425DB0 region bytes verified); local = base * -dist; rotate by
upper-left 3x3 (v6[0/4/8],[1/5/9],[2/6/10]); out = eye(+76/80/84) + rotated. Exact.

---

## character_render4.cpp  (all VERIFIED-1:1)

The brief's queue-insert/unlink interaction confirmed against the binary:
- QueueInsertEntry 0x40c15c: GetFreeEntry; SetGrayColorThunk(0,404,e); link to tail of
  owner+296 list via +40(next)/+36(prev); clears node +12=0, +0=0, +40=0, +16(byte)=0,
  +20=owner; ValidateLinks. Returns node / 0 on exhaustion. The builders route this + the
  unlink through CharRender4Hooks into the genuine guild::sim siblings.
- UnlinkEntry 0x404370: head fixup at owner+296 + doubly-linked unlink via +36/+40.

### VERIFIED-1:1 — CreateSoundActionEx 0x4056d8
+0=SoundActionUpdate, +9=46, +376=1.0f, +40=0, +16=0, +48=param, name->+240
(stride-2 pairwise copy), +376=speed. Does NOT clear +12. Field writes match.

### VERIFIED-1:1 — CreateTakeObjectAction 0x405da8 / Alt 0x405e68
+12=0, +0=Take, +9=49, +40=0, +16=0, +52=0, +56=hand(2 normal / 1 alt), +20=owner,
name->+240; if src model present (src+492 -> +260): copy obj name -> +304, return 1; else
UnlinkEntry + return 0. NOTE: binary writes `+48 = <uninit ecx>` (Hex-Rays artifact / dead
garbage) — reconstruction omits it (faithful: value is non-deterministic). Matches.

### VERIFIED-1:1 — CreateDropObjectAction 0x40602c / Alt 0x4060dc
+12=0, +0=Drop, +9=50, +40=0, +16=0, +52=0, +56=hand, +20=owner, +48=param;
name? copy->+240 : +240=0; objName? copy->+304 : +304=0; always return 1. Matches.

### VERIFIED-1:1 — CmdSitDown 0x43d6b8 / CmdGetUp 0x43d7c4 / CmdSitDownAtOnce 0x43d738
Re-entry guard: `if(execCmd && *(execCmd+44)==self){ if(*(actor+296)) latch; return 0;}`;
null actor -> ReportError + return 1; else create (SitDown->CreatePlaySampleAction,
GetUp->CreateSampleLoopAction, AtOnce->CreatePlaySampleAction then node+396|=1);
`if(*(ctx+2564)==1) latch(*(ctx+2528)=self)`; return 0. Error strings:
"SitDown(): Invalid character" (SitDown + AtOnce), "GetUp(): Invalid character". seek
flag offset +396 verified. Exact.

### VERIFIED-1:1 — CmdCharacterCount 0x43db90
Iterate 512 slots (dword_66F0D0), count slot && *(slot+136)==off_649D64. Reconstruction
parameterizes the table + count + active tag; count logic exact.

### VERIFIED-1:1 — CopyNamePairwise (stride-2 byte-pairwise copy)
Matches the verbatim do/while loop used by every builder (even byte = guard, odd byte
copied unconditionally then re-tests). Confirmed identical in all 5 binary copy sites.

---

## Handoffs / notes
- A pre-existing, UNRELATED build break exists in `src/gui/widget_layout.cpp`
  (`Widget::ld<>` member missing — another agent's in-progress work) which prevents the
  full CMake `guild` library from linking. My three TUs + both modified test TUs compile
  cleanly in isolation (`-fsyntax-only`) and the render3/render5 unit tests run green
  (171 checks, 0 failures) in a standalone harness with stubbed util/render externs.
- No files outside the owned set were edited.
- Golden fidelity vs uninitialized-register artifacts (Take +48, ResolveHeadBone staff
  divisor) is documented above; these are genuinely non-deterministic in the original.
