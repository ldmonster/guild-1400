# Hardening pass — sim/map.cpp, sim/menu_actor.cpp, sim/lasttail_recon.cpp

Disasm-vs-Hex-Rays line-for-line verification of every `gilde.exe 0xADDR`
function in the three files. Disasm is authoritative. Two real divergences were
found and fixed in `map.cpp`; everything else verified 1:1.

## src/sim/map.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x486068 | VIBE_Map_IsTileWalkable | VERIFIED-1:1 |
| 0x404d10 | VIBE_Map_StampCollisionArea | VERIFIED-1:1 |
| 0x404b90 | VIBE_Map_BuildCollisionGrid | VERIFIED-1:1 |
| 0x404bf4 | VIBE_Map_ClearCollisionRegion | VERIFIED-1:1 |
| 0x4860c8 | VIBE_Path_FindNearestFreeTile | FIXED -> VERIFIED-1:1 (out-arg swap) |
| 0x4861d8 | VIBE_Path_FindNearestTileToPoint | FIXED -> VERIFIED-1:1 (float-round site) |
| 0x406f10 | VIBE_Map_TraceLineOfSight | VERIFIED-1:1 |
| 0x404ef8 | VIBE_Map_StampEntityCollision | VERIFIED-1:1 |
| 0x408740 | VIBE_Map_CheckPathWalkable | VERIFIED-1:1 |
| 0x577320 | VIBE_Map_FindNearestDoorCell (scan portion) | VERIFIED-1:1 |

### Detail / float->int + comparison audit

* **IsTileWalkable (0x486068):** disasm confirms `movsx ebx, byte ptr` (signed
  char load), inclusive bounds `x>size||x<0||y<0||y>size`, interior gate
  `x<size && y<size`, and `a3 && a3!=13`. The cell index `24*(x+size*y)` is built
  via the `lea ecx,[ecx*4] / sub / lea [eax*8]` = `*24` idiom. Matches.

* **StampCollisionArea (0x404d10):** Manhattan-disk fill, `v10 = (unsigned)v21 -
  abs(v23)`, interior clamp `[1, size-2]` for X and `[1, size-1)` for the row
  write guard, dirty-rect grow `min=a1-a5 / max=a5+a1+1` (and Y). All matched.
  No fistp/float sites. The reimpl's `!g.entries || size<=0` guard replaces the
  original's per-profile map resolution (external-resolution adaptation).

* **BuildCollisionGrid / ClearCollisionRegion:** flat index `x + y*size`
  (outer=row in Build, outer=X in Clear), `entries[24*flat]`. Clear's dirty-rect
  clamp order and reset-to-(size,0) sentinel matched.

* **FindNearestFreeTile (0x4860c8) — FIXED.** Disasm of the success store
  (0x4861bf): `var_28(=ebx=4th ptr arg a4) <- ecx (column v12)`;
  `var_2C(=ecx=3rd ptr arg a3) <- ebx (row v7)`. Caller 0x49163c confirms: input
  `cx`-slot receives the column, input `cy`-slot receives the row. The reimpl had
  the two output pointers SWAPPED (`*outY=v7(row); *outX=v12(col)`). Fixed to
  `*outX=v7(row); *outY=v12(col)` so the 3rd pointer arg gets the row and the 4th
  gets the column, exactly as the binary. (The header's "column into *outX" prose
  was wrong; the impl now matches the binary's pointer-to-value mapping.)
  My three targets do not assert this ordering; the symmetric sim_path /
  itest / e2e tests stay green (their centre-symmetric grids are swap-invariant).

* **FindNearestTileToPoint (0x4861d8) — FIXED.** dbl_61B12C = 0x403e000000000000
  = 30.0 (confirmed). Disasm 0x486332: `fsqrt; fst var_10` leaves the FULL 80-bit
  sqrt in st0 and stores the rounded float `v37` to var_10. The MIN compare
  (`fcomp var_18`) uses the full-precision st0 (`v14 < bestMinDist`), but BOTH
  `>30.0` gates AND the MAX compare (`fld var_10; fcomp var_14`) reload the
  ROUNDED float `v37`. The reimpl used the unrounded `dist` (double) everywhere.
  Fixed: compute `float v37=(float)dist`; min keeps `dist < bestMinDist` but its
  gate now uses `v37>30`; max compare + gate now use `v37`. Stores already used
  `(float)dist`. (The min `<` stays a double compare — the codebase uses
  double `std::sqrt`, not x87 long double; reproducing the 80-bit min compare
  portably is out of scope and consistent with the existing convention.)

* **TraceLineOfSight (0x406f10):** both modes verified. Best-approach: sum compare
  (0x40731a) `fadd; fst var_10(float v60); fcomp var_14(bestSum)` uses the
  full-precision st0 sum for the `<` test and stores the float — reimpl
  `sum<bestSum` then `bestSum=(float)sum` matches. Cell index `24*(col+row*size)`,
  bounds `row,col in [0,size)`, stores `*outCol=col(i)/*outRow=row(v26)`,
  walkable `c!=0 && c!=13`, bit4 short-circuit + ref-world, fallback to target
  cell — all matched. The unsigned-budget convention (`v27` unsigned) is preserved.

* **StampEntityCollision (0x404ef8):** data-flow verified: forwards `value` and
  `radius` to StampCollisionArea; the per-profile global map resolution
  (`dword_13ECF78[246*profile]`) is replaced by the externally-resolved `hm`
  (established pattern). StampArea's profile arg only selected the map (no geometry
  effect), so dropping it is behavior-identical given external resolution.

* **CheckPathWalkable (0x408740):** cap<2 -> seed; `hi=min(cap-2, curIdx+2)`;
  `i=max(curIdx,1)`; empty range -> seed; index `24*(size*row + col)` with
  col=p[0],row=p[1]; blocked (0 or 13) -> 0; else seed. Matched.

* **FindNearestDoorCell (0x577320), scan portion:** init best `(float)(2*size*size)`
  (int mul then float), scan when `doorCol<0 || doorRow<0`, offsets
  `rowOff=row-startRow / colOff=col-startCol`, door types 6/11, metric
  `(double)(rowOff^2+colOff^2)` compared `< best` then stored as float. Matched.
  The original's subsequent PathBuildWaypointList route step is intentionally left
  to the caller (documented scope: "scan portion"); the reimpl returns the door
  cell via pointers instead of by-value-local.

## src/sim/menu_actor.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x52af64 | VIBE_Character_CreateMenuDummyActor | VERIFIED-1:1 |

* flt_5CA2B0 bytes via get_bytes: `00 00 00 00 00 00 00 00 00 00 80 3f` = {0,0,1.0f}.
  Matches `kForwardZ`.
* Full control flow matched: FindByHandle(0,256,a1,0,a2) -> PointThroughBoneChain
  -> CreateFromModel -> rot{0,yaw,0} where yaw=VectorAngleBetween({0,0,1},
  RotateVectorByHierarchy(dummy,{0,0,1})) stored in the +Y slot (v14) ->
  SetWorldTranslation(node,&rot) -> QueryTerrainType(chr,0) -> node+512=chr ->
  PropagateDirtyFlag(node,1) -> PreloadAniSet(chr,1,"bewegung/gehen") ->
  node+72=0 -> +416=0x3F2AAAB3(1060320051) -> +508=-1 -> +44=1 -> +512(byte)=4 ->
  StatusText_Register(node,0) -> return chr. yaw stored as float (v14 is float).

## src/sim/lasttail_recon.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x5dcd36 | StrCmpNoCase_Thunk: StrCmpNoCase(a1,a2) | VERIFIED-1:1 |
| 0x44ec98 | StrCmpThunk: StrCmp(*a2,a1) | VERIFIED-1:1 |
| 0x44eca0 | StrCmpNoCaseThunk: StrCmpNoCase_Thunk(a1,*a2) | VERIFIED-1:1 |
| 0x44f6a4 | StrCmpNoCaseDerefThunk: StrCmpNoCase_Thunk(*a1,*a2) | VERIFIED-1:1 |
| 0x43c6fc | Script_FinishThunk: Script_Finish(*a1); return 0 | VERIFIED-1:1 |
| 0x43c788 | Script_FindByNameThunk: __fastcall FindByName(a1,a2) | VERIFIED-1:1 |
| 0x1426221 | CmdLine_SkipFirstArg | VERIFIED-1:1 |

* Every thunk forwards with the exact arg order / deref the disasm shows.
* SkipFirstArg: the `goto LABEL_12` (unquoted + unmatched-quote) lands AFTER the
  `do{++v0; LABEL_12:;}` increment, i.e. enters the trailing whitespace-skip at
  its `while(*v0 && *v0<=0x20)` test with no extra advance; the matched-quote path
  enters the do-loop at the top (++v0 past the quote, then skip). The reimpl's two
  LABEL_12 spellings reproduce both entries. Unsigned byte compares `>0x20`,
  the in-quote `IsSpace -> extra ++v0` quirk preserved. The lazy ctype init and
  Locale_IsSpace are the documented headless-default adaptation.

## Counts

* Functions audited (disasm + decompile diff): 18
  (map.cpp 10, menu_actor.cpp 1, lasttail_recon.cpp 7).
* Divergences found & fixed: 2 (both in map.cpp:
  FindNearestFreeTile out-arg swap; FindNearestTileToPoint float-round sites).
* VERIFIED-1:1: 18/18.
* Wrong goldens found: 0.

## Tests (all green)

Assigned targets: mapview_test, pathfind_map_test, env_map_walk_test,
menu_actor_test, lasttail_recon_test — 5/5 passed.
Regression check of code touched by the fix: sim_path_test, sim_path_query_test,
sim_path_query_itest, sim_path_query_e2e_test — 4/4 passed.
