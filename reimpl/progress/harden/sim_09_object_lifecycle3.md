# Hardening: sim / object_lifecycle3 (VIBE_Object_* batch 3)

Module: `src/sim/object_lifecycle3.{cpp,h}` + `tests/unit/object_lifecycle3_test.cpp`
Method: MCP `decompile` + `disasm` line-for-line diff against gilde.exe (imagebase 0x400000).
Disasm wins over Hex-Rays. Every constant cross-checked.

Result: **1 FIXED, 1 BOUNDARY-extended (same fix), 20 VERIFIED-1:1, 0 open.**
Build target `object_lifecycle3_test`: PASS (1/1, all sub-TESTs green).

---

## Per-function verdicts

### 0x5af2e4 VIBE_Object_InvalidateCurrent — VERIFIED-1:1 (BOUNDARY)
Original walks `WalkAndInvoke(off_649D64,0,MarkDirtyFlag,511,al)` (node==0 → no-op),
then touches render-owned globals dword_64A7C8 (sky dome), dword_64A028+7280 (floor),
and the dword_649D68 "scroll node" cleanup (+492 draw-data, +252/+256, +528 dirty,
+531&~1, dword_1408A64/68=0). All of that is render scene state not modeled here;
source reproduces the observable boolean success return (1) and leaves the scroll-node
mutation as a hook-driven path. Faithful for the in-tree surface. BOUNDARY (render globals).

### 0x5af38c VIBE_Object_SetPosition — VERIFIED-1:1
`if (a1==dword_13FCD1C) InvalidateCurrent(1) else WalkAndInvoke(...,511,1)`;
`TraverseTree(...,ResetCasterTransforms,192)`; writes a1[19]/a1[20]/a1[21] (+76/+80/+84);
`result=a2[2]; a1[21]=result; return result`. Source mirrors control flow; returns
node->d(kPos+8) (== pos[2] bit pattern after the write). The dead capture of the old
+8 value is harmless. ✓

### 0x5af3ec VIBE_Object_SetPositionXYZ — VERIFIED-1:1
Packs (x,y,z) into a local vector, tail-calls SetPosition. ✓

### 0x5af418 VIBE_Object_SetScaleVector — VERIFIED-1:1
WalkAndInvoke(...,511,1); TraverseTree(...,192); writes a1[27]/[28]/[29] (+108). ✓

### 0x5af464 VIBE_Object_SetScaleVectorXYZ — VERIFIED-1:1  (thunk). ✓

### 0x5af490 VIBE_Object_SetPivotVector — VERIFIED-1:1
WalkAndInvoke(...,511,1); TraverseTree(...,192); writes a1[30]/[31]/[32] (+120). ✓

### 0x5af4e0 VIBE_Object_SetPivotVectorXYZ — VERIFIED-1:1  (thunk). ✓

### 0x5af50c VIBE_Object_SetWorldTranslation — VERIFIED-1:1
`v4=*(a1+533)`; float store a1+132, dword copies a1+136/+140; if v4==3:
if a1==current InvalidateCurrent(0), then `v7={-(+132),-(+136),-(+140)}`,
`MatrixFromEuler(v7,a1+396)` (NO shadow traverse). else MatrixFromEuler(a1+132,a1+396)
then TraverseTree(...,192). Source matches branch split + angle negation + no-shadow-on-pivot.
MatrixFromEuler is the math hook (return modeled as status 1). ✓

### 0x5af5cc VIBE_Object_SetWorldTranslationXYZ — VERIFIED-1:1  (thunk). ✓

### 0x43e724 VIBE_Object_KillObject — VERIFIED-1:1
`if (*a1) DetachAndRelease(*a1) else ReportError(...,"ecmd_KillObject...")`; ret 1. ✓

### 0x43e758 VIBE_Object_SetPos — VERIFIED-1:1
Reg-arg order edx=x, ecx=z, ebx=y. Writes (*a1)[19]=*x, [20]=*ebx(=y), [21]=*ecx(=z),
then SetPosition(*a1, *a1+19); else ReportError; ret 0. Source takes (x,y,z) in node
order and writes +76/+80/+84 accordingly — identical node layout. ✓

### 0x43e804 VIBE_Object_MoveObject — VERIFIED-1:1
v8=(float)*ecx(z), v7=(float)*ebx(y), v6=(float)*edx(x); passes v6,v7,v8 + world
angles *(a1+132/136/140) + a5(extra) + 0 to Sound3d_SetListenerOrientation; else
ReportError; ret 0. Source maps (x,y,z)->(fx,fy,fz), world from euler. The raw `(int)a4`
2nd positional arg is an internal pointer artifact (not observable). ✓

### 0x43ea48 VIBE_Object_ReplaceObject — **FIXED**
Original (0x43ea48): `RebindParentMesh(*a1, a2/*edi=prototype*/, *a3/*edx -> meshName*/)`
— **THREE** args. Verified RebindParentMesh @0x5b4420 prototype:
`(eax=node, edi=prototype, edx=const char* meshName)` (used in LoadOrFindByName /
AttachStockObjectLods). Source previously dropped the meshName string.
FIX: extended `rebindParentMesh` hook to `(node, prototype, meshName)` and
`ObjectReplaceObject(node, prototype, meshName)`; threaded *a3 through. Test updated to
assert the mesh string reaches the hook. else ReportError; ret 1. ✓

### 0x43f844 VIBE_Object_CmdSetObjectStateThunk — VERIFIED-1:1
`return Rain_Create(*a1, edi, esi)`. Source: rainCreate(node,a,b). ✓

### 0x43f84c VIBE_Object_CmdResetObjectThunk — VERIFIED-1:1
`Rain_Destroy(*a1); return 0`. ✓

### 0x5b3698 VIBE_Object_ToggleHiddenState — VERIFIED-1:1
`if (dl && *(a1+533)==5 && *(BYTE)a1==114) { *(a1+533)=6; *(a1+64)=dword_62EB38;
WalkAndInvoke(...,511,0); ret 1 }`; `if (dl || *(a1+533)!=6) ret 1`;
`*(a1+533)=5; Light_RemoveCacheEntry(a1,0,MarkDirtyFlag,edi); WalkAndInvoke(...,511,0);
ret 1`. Source matches all three exits; frameStamp = dword_62EB38; Light_RemoveCacheEntry
is the render leaf folded into the dirty walk. ✓

### 0x583a70 VIBE_Object_FindObjectById — VERIFIED-1:1 (factored)
`v2=0; while(!*(WORD)(base+v2) || id != *(DWORD)(base+v2+2)) { v2+=67; if (v2>=548864)
return 0 } return base+v2`. 548864 = 67*8192. First entry tested before increment.
Source factors to `(table, entryCount=8192, stride=67, id)` returning the index (-1 on
miss) — same scan: word marker @+0, dword id @+2, marker!=0 && id==entryId. ✓

### 0x5b7b7c VIBE_Object_MatchHandleCallback — VERIFIED-1:1
Case-sensitive. v2=node; if (*a2/*queryStr*/) { if (*node==33 /*'!'*/) node++;
if (a2[1]/*queryNode*/) { if (!StrCmp(*a2,node) && v2==a2[1]) a2[2]=a2[1] }
else if (!StrCmp(*a2,node)) a2[2]=v2; return v2!=a2[2]; }
else { if (node!=a2[1]) return v2!=a2[2]; a2[2]=a2[1]; return node!=a2[2]; }
StrCmp arg order (queryStr,name) verified vs VIBE_Util_StrCmp@0x5d3f10 (==0 semantics
match; the SIMD word-at-a-time impl is equal-iff-0). ✓

### 0x5b7c48 VIBE_Object_MatchNameCallback — VERIFIED-1:1
Identical shape, case-INSENSITIVE, compare order (node, queryStr) verified vs
VIBE_Util_StrCmpNoCase@0x5cb8f0 (lowercases A–Z by +32 on both sides; ==0 semantics
match the source LowerAscii loop). ✓

### 0x4b0ee8 VIBE_Object_IsNearDoorAlt — VERIFIED-1:1 (BOUNDARY core)
`v3=*(a1+59); v4=*(a2+97); v8=650.0; if (v4&&v3){ v5=FindByHandle(v4,256,"dummy_TUER",
0,v3); if (v5) PointThroughBoneChain(v5,v5+19,v7) else { PointThroughBoneChain(*(a2+97),
*(a2+97)+76,v7); v8=3250.0 } if (VectorWithinTolerance(*(v3+52)+76,v7,v8)) return 1 }
return v3 && *(v3+44)==*(a2+1)`. Bone-chain transform + FindByHandle + tolerance test are
elsewhere-owned; source's `ObjectIsNearDoorAltCore` reproduces the control flow, the
650/3250 tolerance selection, the per-axis |a-b|<=tol test, and the owner-id fallback.
BOUNDARY (caller supplies resolved geometry). ✓

### 0x4b0f78 VIBE_Object_GetTypeMessageId — VERIFIED-1:1
Full 181-instruction disasm diffed. Unsigned type byte `*(*(a1+0x17C))` switch.
Constants confirmed: 0x4AA=1194 0x4AB=1195 0x4AC=1196 0x4AD=1197 0x4AE=1198 0x4AF=1199
0x4B0=1200 0x4B1=1201 0x4B2=1202 0x4B3=1203 0x4B4=1204 0x4B5=1205 0x4B9=1209; +0xCE=+206.
"item high word" appends use `sar reg,10h` (arithmetic >>16) from THREE distinct struct
offsets per arm — v4+0xAA (170: types 3/4/8), v4+0xAC (172: types 20/21),
v4+0xBA (186: type 40); the buildingKind index = `589 * (signed char)**(a1+0x16C)`
into dword_13CE294 (7→1203, 8/14→1201, else 1194). FINISH appends combat-def
`*(WORD)FindObjectDef + 206` at out[v5] (movsx). All 26 golden vectors traced
arm-by-arm against disasm and match. The three high-word offsets and the buildingKind
table lookup are caller-resolved inputs (documented in TypeMessageInputs); the
in-function classification is exact. ✓

---

## Notes / boundaries
- Render scene-graph leaves (WalkAndInvoke/TraverseTree/ResetCasterTransforms/
  Light_RemoveCacheEntry/MatrixFromEuler), the floor/sky/scroll globals, Sound3d move,
  Rain create/destroy, DetachAndRelease, RebindParentMesh, and FindByHandle/bone-chain
  geometry are routed through `ObjLife3Hooks` so the node-field arithmetic stays exactly
  1:1 and independently testable. These are pre-existing, documented hooks; only
  `rebindParentMesh` was changed (3rd arg added) to restore the 1:1 call shape.
- No external callers of `ObjectReplaceObject`/`rebindParentMesh` in src/ or tests/
  (grep clean) — signature change is self-contained.

## Test counts
`object_lifecycle3_test`: 17 TEST cases, all PASS. GetTypeMessageId golden = 26 vectors.
Added assertion: ReplaceObject mesh-name string reaches the hook.
