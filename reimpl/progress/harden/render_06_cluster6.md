# Hardening sweep — render cluster 6 (snow)

Files: src/render/snow.cpp, src/render/snow_recon.cpp, src/render/snow_update.cpp
Test targets: snow_recon_test, snow_render_test — both GREEN after fixes.

MCP module gilde.exe. Every `0xADDR` provenance below was decompiled + disassembled
and diffed line-for-line; every constant confirmed with get_bytes. Where Hex-Rays
collapsed `__usercall` args (e.g. the `div ecx`), the disasm was used as reference.

## Constants verified (get_bytes, all bit-exact)
SeedFlakes: flt_6117AC=0x38000100 (1/32767), 6117B0=2.0, 6117B4=0.5,
6117B8=0.01, 6117BC=-0.5, 6117C0=0.75, 6117C4=0.025.
Update: flt_6117F0=0.0025, 6117F4=2.0, 6117F8=13.5, 6117FC=3.0;
dbl_611804=-1.0, 61180C=2.0, 611814=-2.0; flt_61181C=-2.0.
Render: flt_611990=0.1, 611994=0.025, 611998=0.5; kSnowColor=1348756580=0x50646464;
rhw bits 1065353216=1.0f, 1056964608=0.5f.
Coverage: flt_6118A4=0.0004, 6118A8=255.0f; dbl_611894=0.002, 61189C=255.0.
ConvertX@0x5c6b08 = frndint with chop CW then caller fistp => trunc toward zero.

## Per function

### VIBE_Snow_Create / SeedFlakes  0x42a014 (seed loop) — VERIFIED-1:1
Six RandNext draws/flake in order px,py,pz,size,d0,d1. Disasm `xor ecx,ecx; do{...
inc ecx; cmp ecx,ebp; jl}` writes exactly `count` records (i=0..count-1). Math:
px/py/pz = (rand*1/32767 + -0.5)*2.0 ; size = rand*1/32767*0.5 + 0.75 ;
d0/d1 = rand*1/32767*0.01 + 0.025. Recon matches exactly.

### VIBE_Snow_UpdateFlake  0x42a644 — FIXED (large, multiple divergences)
The prior recon was a documented simplification ("fold sysVel into d0/d1",
`(void)drift2`) and had scrambled the drift/offset/gravity index mapping. Rewrote
1:1 from the disasm 0x42a662..0x42ab60.

Evidence + fixes:
- Euler input. Disasm 0x42a662: `fld [esi+112] fsub [eax+132]` => euler =
  prevAnchor(a1+112..) - camAnchor(cam+132..). Prior recon used cam.anchor-cam.eye.
  FIXED. off0=1.9*em[8], off1=1.9*em[9] (em[10] computed, unused).
- Statefulness. a1+112.. and a1+128.. are overwritten with current cam anchor/eye
  AFTER reading the deltas (0x42a6fd, 0x42a7d8). Added SnowSystem.prevAnchor[3] /
  prevEye[3] and the store-back. Prior recon had no such state.
- Drift input. Disasm 0x42a726: `fld [esi+132] fsub [eax+80]` => de = prevEye -
  camEye. Prior recon used cam.anchor - cam.eye. FIXED.
- Drift columns. matrix base ebx=v3+396; driftX=de.col0(+396/+412/+428),
  driftY=de.col1(+400/+416/+432), driftZ=de.col2(+404/+420/+436), all *0.0025.
  Prior recon paired the WRONG column with each axis. FIXED.
- Velocity basis. v33/v34/v35 = (sysVelX,0,sysVelZ) . col0/col1/col2. Prior recon
  dropped sysVel entirely and used cam.m[0..2]. Added SnowSystem.sysVelX/sysVelZ
  (a1+68/+72). FIXED.
- Gravity. v24/v25/v26 = -m[3]/-m[4]/-m[5] (= -(+412,+416,+420)). Prior recon used
  -m[1]/-m[4]/-m[7]. FIXED.
- Per-axis vel = d1*grav + d0*velbasis (v50/v51/v52). Structure kept; operands fixed.
- Axis integration pairing. X: drift v36(col0) + off v27(1.9*m[8]); Y: drift
  v37(col1) + off v28(1.9*m[9]); Z: drift v38(col2)*flt_6117F4(2.0), NO offset term.
  Prior recon used off2*kF4 on Z and the wrong drift on X/Y. FIXED.
- Wrap loops: lower `>= -1.0` adds +2.0 (Z adds flt_6117F4=2.0); X/Y upper `>= 1.0`
  (double) adds -2.0; Z upper uses raw-bits signed `>= 1065353216` adds -2.0f. Kept.
- Center/depth: v45=(double)halfW, v46=(float)halfH, v44=max(v45,(double)v46),
  cx=x0+v45, cy=y0+v46, depthScale=v44*3.0. Setup (incl. stores) runs even when
  count<=0; loop guarded by count>0. Projection v20 = depthScale/(pz*2.0+3.0);
  sx=px*v20+cx; sy=cy-v20*py; tail=((1-pz)*13.5+1)*size; sx2=sx+tail; sy2=tail+sy.
  All match.

### VIBE_Snow_Render header  0x42b5c9..0x42b648  SnowRenderStepHeader — VERIFIED-1:1
Count ramp (A): unsigned `cEnd > now` gate; cTo*(now-cBeg)/span +
(i32)((cEnd-now)*cFrom)/span; else cTo. Direction ramp (B): dirX=tgtX*ela*inv +
oldX*rem*inv ; dirZ=ela*tgtZ*inv + inv*(rem*oldZ); else snap to target. dt (C):
delta=(u32)(now-last); dt=(double)delta*0.1; last=now. Exact match.

### VIBE_Snow_Render quad stream  0x42b689..0x42b7c8  SnowBuildQuads — VERIFIED-1:1
Cull rect: x0<=sx && x1>sx2 && y0<=sy && y1>sy2 (doubles). vcoord=(1-pz)*0.025.
v0: x=(sx+sx2)*0.5,y=sy,tu=0.5,tv=0; v1: x=sx2,y=sy2,tu=1,tv=1; v2: x=sx,y=sy2,
tu=0,tv=1. rhw=1.0(0x3F800000), diffuse=0x50646464, specular=0. All bit patterns
confirmed. (`cap` is a builder-only bound; original flushes the fixed 192-vert
buffer — acceptable abstraction for the pure builder.)

### Transparency decision  0x42a2cc tail  SnowResetSceneTexTransparency — VERIFIED-1:1
Disasm 0x42a32f `cmp dword_140809C,0; jnz` and 0x42a33c `cmp byte[ecx+0x7D],8; jbe`
then 0x42a346 `mov al,1`: flag = (winterFlag==0) && (texLevel > 8u). Match.

### Coverage threshold  0x42b3fc/0x42b1fc  SnowCoverageThreshold — VERIFIED-1:1
Disasm: `fld coverage; mov ecx,5; call ConvertX; fistp v` (trunc toward zero),
`lea eax,[v*4]; xor edx,edx; div ecx(=5)` => budget=(4v)/5u; `lea edx,[v-16];
cmp edx,eax; jnb` => if (v-16)<budget budget=v-16 (unsigned). Match. The `div`
divisor 5 was recovered from disasm (`mov ecx,5`), not the Hex-Rays `v6`.

### Accumulator/coverage helpers  0x42b3fc/0x42b1fc — VERIFIED-1:1
SnowAccumulatorStep: (double)rate*dt + acc. SnowCoverageFromAccumulator:
acc*0.0004, clamp at 255.0f. SnowCoverageFromTimer: (double)timer*0.002, clamp
255.0 (double). SnowBatchLimit: if total-per>=cap return total/per+cap else total
(signed idiv). All match. Note: the original keeps the pre-truncation double `v3`
alive between the accumulator store and the *0.0004 scale; the recon split this
into two standalone helpers (float in) — a decomposition seam, semantically equal
for the golden vectors (acc>=0, clamp identical).

## Other helpers (no extra ADDR provenance)
SnowMipLevel / SnowAccumulateMaskedCopy{8,16,32}: log2(srcDim/scale) and the
mask<threshold per-pixel copy from the 0x42ab78 lock/copy core; unchanged, the
DDraw lock/unlock around them is the rule-3 boundary.

## Counts
VERIFIED-1:1: 9 (SeedFlakes, RenderStepHeader, BuildQuads, TexTransparency,
CoverageThreshold, AccumulatorStep, CoverageFromAccumulator, CoverageFromTimer,
BatchLimit). FIXED: 1 (UpdateFlake — full rewrite + SnowSystem/SnowCamera struct
fields). BOUNDARY: DDraw lock/copy/unlock around the masked-copy core (rule 3).
Tests: snow_recon_test + snow_render_test both pass.
