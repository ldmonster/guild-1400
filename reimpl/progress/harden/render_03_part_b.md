# Wave-H1 hardening — render_03_part_b (mirror reflection math)

Files owned:
- src/render/mirror.cpp
- src/render/mirror_project.cpp
- src/render/mirror_silhouette.cpp

MCP module gilde.exe (imagebase 0x400000). Every provenance-tagged function was
decompiled AND disassembled, diffed line-for-line against the source.

## Constants re-verified via get_bytes (NOT trusted from literals)
- flt_62C3A0 (0x62c3a0) = `00 00 00 40` = 2.0f  ✓ (kReflectScale, mirror.cpp)
- flt_62C39C (0x62c39c) = `00 00 00 40` = 2.0f  ✓ (kReflectScale, mirror_project.cpp)
- dbl_62C2F0 (0x62c2f0) = `7B 14 AE 47 E1 7A 84 BF` = -0.01 (double) ✓ (kSilhouetteTol)
- flt_13FD168 / flt_13FCF3C = 0 at static (runtime depth-bound globals — correct as
  the running near/far accumulators).
- Depth gate 0x33D6E555 = 869711765 ✓ (AppendMirroredPolys near-gate).
- Float->int conversion sites: NONE in any of these functions. Every float compare is
  an x87 fcomp; every float store is fstp. No fistp / (int) cast anywhere. Nothing to
  fix in the ConvertX/truncation bug class.

## Per-function verdicts

### mirror.cpp

**ReflectPointAcrossPlane — VERIFIED-1:1** (decomposed from 0x5f6148 inner reflect)
The reflect math `t = -(P·n - d)*2.0 ; out = P + t·n` matches the disasm exactly
(0x5f61c7..0x5f620c): fld dot, fsub [esi+24h]=d, fchs, fmul flt_62C3A0, then fmul each
normal component + fadd P. Component write order x,y,z matches.

**ClipReflectedBox — VERIFIED-1:1** (the box clip-cull half of 0x5f6148)
- 8 corners (loop ecx 0->0x80 step 0x10 == 8 iters), source stride 80 bytes verified.
- Inside test: `n·P >= plane.d`. Disasm reads normal at plane+8/+12/+16 (v24=v20+2 floats)
  and d at plane+20 (v23[5]); plane stride 16 bytes (v23/v24 += 4 floats). With the
  8-byte MirrorClipPlaneList header this resolves to per-record {nx,ny,nz,d} at +0/+4/+8/+12
  — exactly the source's MirrorClipPlane. `break` on first inside corner; `v11 >= 8`
  (all outside) -> cull. ✓
- Depth-bound expansion: min into flt_13FD168, max into flt_13FCF3C, over reflected z. ✓

**AppendMirroredPolys — VERIFIED-1:1** (LABEL_22 of 0x5f637c)
- Capacity clamp `min(dword_13ECE80 - dword_13FC770, polyCount)`. ✓
- tex skip `v39(j[5]) != v50(mirrorViewTexId)`. ✓
- near gate `*(v+28) < 869711765` (OR of 3 verts) modelled as anyVertexNear. ✓
- no-cull bit `(*((u8*)j+38) & 4)`. ✓
- Reflected winding test `(x0-x2)*(y0-y1) > (x0-x1)*(y0-y2)` (v0=v37,v1=v38,v2=v40). ✓
- sortKey folded `((tex-dword_1406A84)>>7)+1` into caller's texSortId. ✓

### mirror_project.cpp

**ReflectAndProjectVertices (0x5F6084) — VERIFIED-1:1**
- Vertex stride 20 floats (v2 += 20). Output offsets verified against post-increment
  stores: screenX at +4 (*(v2-16)), screenY at +5 (*(v2-15)), t at +7 (*(v2-13)). ✓
- FPU eval order matches exactly: x updated -> vx=projX*x' -> y updated -> vy=projY*y'
  -> z updated -> invZ=1/z' -> t stored -> screenY=invZ*vy+offY -> screenX=vx*invZ+offX.
- Plane consts flt_1408A88/8C/90/94 + projX/Y/offX/Y are runtime globals, correctly
  parameterised (ProjectionParams / MirrorPlane).

**CreateClippingPlanes (0x5F5D08) — VERIFIED-1:1 (with documented rule-8 boundary)**
- Pass1 count `surfaceKey == *(poly+20)`; Pass2 collect back-facing
  `surfaceKey==key && (i8)*(poly+36) < 0`; alloc sizes 4*n, 24*kept, points+12*kept. ✓
- Gather v[0..2], dedup (null/eq/within-tol 0.001, insert at first null, count). ✓
- `uniqueCount>=3 && CreateOutline`, `outlinePtrs>=6`, `edgeCount = outlinePtrs>>1`. ✓
- Final buffer 16*total+8, *(buf)=total, buf[4]=1; per edge: n=TriangleNormal(0,a,b),
  record {nx,ny,nz at +0/+4/+8, d=-(n·a) at +12} (verified the d store goes to v63+20 ==
  record+12). Frustum planes memcpy'd after. ✓
- BOUNDARY: vertex outcode at vertex+76 (`(v[0]&v[1]&v[2]&0x3F) cull` + OR into `orient`)
  is a runtime view-clip field, modelled as 0 (all polys collect, orient=0) — the engine's
  path when nothing is frustum-clipped. The per-orientation frustum table dword_13DB398
  (26-dword stride, all-zero at static) is reached via the documented rule-8
  MirrorFrustumTableHook (default 0 planes). Math of everything reconstructed is faithful.

**PrepareReflectionNode (0x5F676C) — FIXED**
Diffed every branch; all flag arithmetic, the v3!=v9 poly match, the rebuild block
(bit3 test `2*((u8)(16*flag)>>7)`, back-facing+key re-match, plane normal rotate hook,
plane d = n·v0, CreateClippingPlanes(node, surfaceKey), publish via dword_649D6C) verified
1:1.

FIXED — not-found return value (0x5f67b3):
  before: when no reflective child found, returned `result` == the node.
  after:  the engine sets `result = *v6++` on EVERY child-loop iteration, so on
          exhaustion it returns the LAST child examined (children[childCount-1]), not the
          node. Source now tracks `result = child` each iteration to match.
  evidence: 0x5f67b3 `result = *v6++`; 0x5f6918 `if (++v4 >= v5) return result`.
  Note: the SOLE caller VIBE_Render_ProcessSceneNode (0x5adf3a) DISCARDS eax
  (`cmp esi, dword_649D68` uses the node in esi, not the return) — so this is a
  correctness-of-record fix, behaviorally unobserved. Test
  MirrorPrepareNode.NonReflectiveNodeNotPublished asserts only prepared/clipPlanes, not
  the return — still passes.
BOUNDARY (unchanged, documented): RotateVectorWithFrame (0x5c8ab4, camera-frame coupled)
  via MirrorRotateNormalHook; VIBE_Memory_AllocDebug/FreeDebug (0x438f10/0x43923c) via
  MirrorAllocHook. Plane-distance math (n·v0) fed in is faithful.

### mirror_silhouette.cpp

**BuildSilhouettePoints (0x5f5740) — VERIFIED-1:1**
- Outer j / inner i with conn row base j*count (connRowJ[i]) and column base j stride
  +count (connColJ) verified against v25/v17 (conn+j*count) and v27 (conn+j, +=count). ✓
- Skip `i!=j && conn[j*count+i]==0`; TriangleNormal(0, pt[j], pt[i]) (arg reorder vs util
  confirmed against 0x5cb824). ✓
- k-loop: break when `k!=j && k!=i && dot < dbl_62C2F0`; emit on k==count:
  outPairs[++]=pt[j], outPairs[++]=pt[i], conn[i*count+j]=1, conn[j*count+i]=1. ✓

**CreateOutline (0x5F58FC) — VERIFIED-1:1**
Full chain-tracer diffed against the disasm:
- Alloc sizes 32*count (pairs, 8 slots/count), count*(count+8) (conn), 4*pairPtrCount (out)
  — slot semantics preserved at host pointer width. ✓
- emitted/visited parallel byte marks at conn+0 / conn+pairPtrCount; zero of first
  2*pairPtrCount; visited[0]=emitted[0]=1; initial dir = normalize(b-a). ✓
- Forward continuation (`!emitted[i] && b==pair.a`, collinear ±0.001) -> stamp
  emitted[i],visited[lastFwd]; lastFwd=i; b=pair.b. ✓
- Reverse continuation (`!emitted[i] && a==pair.b`, collinear) -> break -> stamp
  emitted[i],visited[lastRev]; lastRev=i; a=pair.a. ✓
- emitEdge: dedup against out[] (both orders), append (a,b); restart from first unvisited
  pair (clear emitted, set visited/emitted, reset dir, lastFwd=lastRev=vp); the
  end-of-visited path leaves a=0 -> loop exits. ✓ (Hex-Rays v38 artifact = &d2 confirmed.)

## Counts
- VERIFIED-1:1: 7  (ReflectPointAcrossPlane, ClipReflectedBox, AppendMirroredPolys,
  ReflectAndProjectVertices, CreateClippingPlanes, BuildSilhouettePoints, CreateOutline)
- FIXED: 1  (PrepareReflectionNode — not-found return value)
- BOUNDARY (documented rule-8 hooks, math faithful): allocator (0x438f10/0x43923c),
  RotateVectorWithFrame (0x5c8ab4), frustum table dword_13DB398 + vertex outcode (+76).

## Handoffs
None. All edits confined to src/render/mirror_project.cpp. util/math.{h,cpp}
(TriangleNormal/VectorNormalize/VectorWithinTolerance arg-reorder) cross-checked and
consistent with the binary — no change needed.

All three files compile clean (g++ -std=c++17 -fsyntax-only -Iinclude -Isrc -Ishim).
