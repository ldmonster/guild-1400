# Hardening — src/sim/object_lifecycle4.cpp (VIBE_Object_* batch 4)

Line-for-line decompile+disasm diff of every provenance'd function in
`src/sim/object_lifecycle4.cpp` against `gilde.exe` (imagebase 0x400000), via
IDA Pro MCP. **DISASM is authoritative.**

Build/test: `object_lifecycle4_test` builds green; `ctest -R
'^object_lifecycle4_test$'` → 1/1 PASS (all golden vectors).

> Build note: the first `cmake --build` failed inside an unrelated, off-limits
> file (`src/render/raster_textured.cpp`: `RgbzRasterState::minYSeed`) — a STALE
> incremental-dependency snapshot in `build/` (the header already declares the
> field; a clean compile of that file passes). Recomputing deps on the next
> make invocation cleared it. No edits were made outside this module.

## Per-function verdicts

| Addr | Function | Verdict |
|------|----------|---------|
| 0x5b054c | Spawn | VERIFIED-1:1 |
| 0x5b0660 | DisposeResources | VERIFIED-1:1 (documented render-owned scratch-cache boundary) |
| 0x5b0a20 | LinkIntoScene | VERIFIED-1:1 |
| 0x5b0adc | UnlinkFromScene | VERIFIED-1:1 (null-guards = test isolation) |
| 0x5b23ec | UnlinkFromList | VERIFIED-1:1 |
| 0x5b0e18 | InitSubMeshEntry | VERIFIED-1:1 |
| 0x5b0c8c | FreeSubMeshData | VERIFIED-1:1 (mesh vtbl-release = hookless boundary) |
| 0x5b0da0 | FreeDrawData | VERIFIED-1:1 (return-value boundary, doc'd) |
| 0x5b0c10 | AllocPolysAndPoints | VERIFIED-1:1 (dead no-op return, doc'd) |
| 0x5b2964 | ChangeTransparencySubMeshes | VERIFIED-1:1 |
| 0x5b29b0 | ApplyTransparencyTree | VERIFIED-1:1 |
| 0x5c4634 | SetLowNibbleFlag | VERIFIED-1:1 behavior; return-value boundary (doc'd) |
| 0x567520 | MarkState2 | VERIFIED-1:1 behavior; thunk arg-1 detail (doc'd) |
| 0x538400 | ResetState | VERIFIED-1:1 |
| 0x53841c | ResetStateAlt | VERIFIED-1:1 (byte-identical to ResetState) |

## Verification detail (control flow / constants / signedness / order)

### 0x5b054c Spawn
- `cmp bl,5; jge` → kind>=5 allocs lightInfo `[edi+0x1E8]`(+488), size 0x1AC,
  tag "d3:SpawnObject(lightinfo)". Node alloc 0x21C, tag "d3:SpawnObject".
- First-char ladder exact: `cmp al,0x72(jnb)` → {==0x72→6, >0x72: ==0x73→8 else
  5}; <0x72: {==0x70→7 else 5}. force5 (`byte_649D54`) collapses 6→5.
- Type dispatch `cmp al,6 (jb/jbe)`: type6 → `[lightInfo+0x194]`(+404)=0;
  type8 → `[+0x211]`(+529)|=4. nodeTypeShadow(+534)=nodeType(+533).
  StrNCopyPad(node,name,64). universe(+520)=off_649D64.
- Constants get_bytes-verified: all 4 tag strings byte-exact (0x62814c/15c/
  6281ac/c0). Source `name?name[0]:0` adds a null guard the original lacks
  (test isolation); otherwise exact.

### 0x5b0660 DisposeResources
- type1&&shadow!=1 → type=shadow. drawData(+492) scratch: `[+908]`(2312)
  gate, free `[+900]`(2304)/`[+904]`(2308). Cache removals: type6 OR
  (type5 && name[0]=='r'(0x72)) → LightRemoveCache; walk mask 7; type8 → walk
  mask 64; flags529&4 → ShadowClearAll; tail `return FreeDrawData`.
- **Boundary:** the `dword_1408A70/74/78` render-scratch global triple is NOT
  reconstructed in source (render-owned global) — documented in the source
  header comment. The node-side `+900/904/908` scratch IS modeled (DrawData).

### 0x5b0a20 LinkIntoScene
- type3 && !current → current=node, InvalidateCurrent(0) (only under `if(eax)`),
  SetWorldTranslation(node, node+0x84=+132) UNCONDITIONALLY in the branch.
- type!=0 → prev(+496)=&sentinel(13FCF4C), next(+500)=oldHead(13FD140),
  oldHead.prev=node, flags528(+528)|=3, head=node. universe(+520) set at end.
- type==0 → only universe set + return (no list insert). Source matches.

### 0x5b0adc UnlinkFromScene
- jumptable, `cmp dl,8; ja default`(case 0 + >8): clear flags528 bit1 only.
- case 3: if node==current → current=0, fall through. cases 1,2,4-8 + 3-fallthrough:
  splice `*(prev+500)=next; *(next+496)=prev` (UNCONDITIONAL in binary;
  source guards prev/next null for test isolation), flags528 &= ~2.

### 0x5b23ec UnlinkFromList
- Two symmetric branches reduce exactly to source: prev?{prev.next=next;
  next?next.prev=prev : (parent.firstChild==node ? firstChild=prev)} :
  {next?next.prev=0 : (parent.firstChild==node ? firstChild=0)}. Then clear
  parent(+504)/next(+500)/prev(+496), flags528 &= ~2. Traced every goto/label.

### 0x5b0e18 InitSubMeshEntry
- +376=0,+0=0,+8=0,+4=0,+12=0,+16=0,+24=parent, +376 low byte=0xFF. Loop 3×
  (stride 116=0x74, end +348=0x15C): field32(-84)=0, extraPtr(+20)=0,
  field36(-80)=field32, field28(-88)=field32, meshData(+16 of cursor)=0,
  slotFlags(+22) &= 0xFD. Independent-field writes → order-insensitive. Exact.

### 0x5b0c8c FreeSubMeshData
- Transparency reset gate: `tag(+376)!=0xFF` (`jb`, tag u8) OR `flagsByte(+37A)
  &1` → ChangeTransparency(parentDraw,entry,255,entry). alpha=255 verified.
- polyCount(+8)>0 && points(+0) → free points, clear +0/+8 (signed `jle`).
- pointCount(+12)>0 && polys(+4) → free polys, clear +4/+12.
- texArray(+20): if mesh(+16): for i<`[mesh+0x1E0]`(+480) release non-null
  tex[i] (4-byte stride); free texArray; clear. signed `jge` loop bound.
- mesh(+16): vtbl `[mesh+0x1E8]`(+488)(255) then clear — vtbl call is the
  hookless boundary, field-clear is the observable effect (source). Happens
  BEFORE the LOD loop (binary `jmp loc_5B0D59`).
- LOD loop 3× (stride 116): `[+0x84]`(+132) meshData → AnimRelease, clear.

### 0x5b0da0 FreeDrawData
- drawData(+492): loop i<`[dd+0x90C]`(2316, u8 count) FreeSubMeshData(
  dd+0xF4+0x180*i = 244+384i); trailing FreeSubMeshData(dd+0x574=1396);
  `[dd+2316]=0`; FreeDebug(dd); `[+492]=0`; meshFrame(+0x1CC=460)=0.
- **Return boundary:** binary returns FreeDebug's eax (freed path) / input node
  (null path); FreeDebug is a void hook → eax unavailable; source returns 1.
  Doc'd; sole caller (DisposeResources) does not branch on it.

### 0x5b0c10 AllocPolysAndPoints
- gate polyCount(+8) && pointCount(+12). Order verified: free polys(+4) →
  alloc polys = AllocDebug(40*pointCount) [`lea *4 +; shl 3`] → free points(+0)
  → alloc points = AllocDebug(80*(polyCount+8)) [`+8; *4 +; shl 4`] → store.
  Tags "d3:AllocObjectPolys"/"d3:AllocObjectPoints" byte-verified.
- **Dead return note:** binary no-op path returns input `entry`; source returns
  `entry->points`. No caller uses no-op return. Active return faithful.

### 0x5b2964 ChangeTransparencySubMeshes
- drawData(+492). while `v5 < (u8)[dd+2316]` (unsigned `jb`): sub=dd+0xF4+
  0x180*idx, `inc esi` BEFORE call → idx=++v5, alpha=*a2. Returns 1.

### 0x5b29b0 ApplyTransparencyTree
- if node: walk(off_649D64, node, ChangeTransparencySubMeshes, 0x240=576,
  alpha), return lo byte; else return 0. mask 576 + arg=alpha verified.

### 0x5c4634 SetLowNibbleFlag
- `v5 = [+0x1C71](7281)&0xF`. if v5!=newNibble: FreeTileBuffers; hi=byte&0xF0;
  write hi; write `(newNibble&0xF)|hi`; AllocInflate; if rebuild→BuildTilePolys.
- **Return boundary:** changed-path return = eax of VIBE_Floor_AllocInflate
  Buffers (0x5bce10) / VIBE_Floor_BuildTilePolys (0x5bc45c) — NOT reconstructed,
  routed through void floor hooks → eax unavailable. Both live callers
  (0x4aa2e8 VIBE_Cutscene_LoadScene, 0x506d26 UpdateBuildingVisualState)
  DISCARD the return → observationally dead. Source returns the resulting
  +7281 byte as a stable testable stand-in; unchanged path returns newNibble
  (faithful). Doc'd in source.

### 0x567520 MarkState2
- disasm: `mov edx,2; mov eax,[eax+4]; call thunk` → thunk(`*(this+4)`, 2).
  Arg-1 is the pointer field at this+4 (a command/gameplay object, not an
  ObjNode4); edx=2. RequestBuildOp is a void hook; source passes `node`. Doc'd.
  Returns 1.

### 0x538400 / 0x53841c ResetState[Alt]
- Both: `ebx=0x1200(4608); eax=&byte_122FEC0; edx=0; call SetGrayColorThunk;
  return 1`. The two functions are byte-identical. Source models lightSetGray
  (0, 4608); constant 4608 verified; the &byte_122FEC0 buffer ptr is a fixed
  global folded into the void hook.

## Source changes made (this pass)
Comment-only fidelity corrections (no behavior change; file compiles, test
green):
1. SetLowNibbleFlag — replaced the inaccurate "returns the +7281 byte" note
   with the verified return-value boundary explanation (floor-leaf eax,
   dead in both callers).
2. MarkState2 — documented the `*(this+4)` thunk arg-1 (was implied as `this`).
3. FreeDrawData — documented the FreeDebug-eax return boundary.
4. AllocPolysAndPoints — documented the dead no-op-path return divergence.

No functional/golden change was required: every reconstructed control-flow path,
constant, table byte, stride, offset, signed/unsigned compare and side-effect
order matches the binary. The only divergences are RETURN values on code paths
that depend on un-reconstructed (hooked) leaves' eax and are dead at every live
call site — each now documented with address + evidence.
