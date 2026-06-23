# Hardening sweep — sim/character_recon2_cmds.cpp

Chunk owner files:
- src/sim/character_recon2_cmds.cpp (+ .h)
- tests/unit/character_recon2_cmds_test.cpp

Method: decompiled each provenanced address via IDA MCP, diffed line-for-line vs
the reconstruction; verified every constant with get_global_value/get_bytes;
inspected the float->int sites at disasm level. Build + tests: green
(`character_recon2_cmds_test`, 111 checks, 0 failures).

Result: **all 13 functions VERIFIED-1:1. No source or golden changes required.**

---

## Per-function findings

### 0x43d260 CmdTakeObject — VERIFIED-1:1
Resume guard (execCmd->fn @+44 == self, *(char+296), ctx+2528 re-arm), validity
branches, CreateTakeObjectAction(*a1,*a2,*a3), stmt-mode tail (ctx+2564==1 ->
ctx+2528=self). Offsets 296/2528/2564/152 confirmed. Strings exact.

### 0x43d30c CmdTakeObjectLeft — VERIFIED-1:1
Same shape; forwards to CreateTakeObjectActionAlt (via createTakeObjectAlt hook,
already reconstructed in character_render4 — ODR-avoidance, documented in .h).

### 0x43d3b8 CmdTakeObjectScript — VERIFIED-1:1
InsertActionArgs(char, PlayAnimationScriptCallback, *a5|0x3100000000, *a4).
Field offsets confirmed against decompile: +240 (=*a2 dummy name), object inner
chain `*( *(*a3) + 492 )` -> `+260` copied to +304, +144 (=*a6 script name),
+380 = dword_62E8D4. 0x1EC=492, 0x104=260 ✓. Unrolled-2 narrow strcpy matches
the `mov al,[esi]/mov [edi],al/.../add esi,2` loop verbatim.

### 0x43d548 CmdDropObject — VERIFIED-1:1
Order confirmed: validity -> (*a2 && !FindByHandle(0,64,0,*a2,a1)) gate ->
resume guard -> CreateDropObjectAction(*a1,*a3,*a2). Note FindByHandle takes the
raw charPtr (a1), not *a1 — reconstruction passes charPtr. ✓

### 0x43d600 CmdDropObjectLeft — VERIFIED-1:1
Same order; CreateDropObjectActionAlt(*a1,*a3,*a2) via createDropObjectAlt hook.

### 0x43d0f8 CmdPlayAnimationScript — VERIFIED-1:1
Disasm confirms register binding the Hex-Rays `*v7` ambiguity:
- strlen target = `*edx` (scriptNamePtr); guard `>= 0x5F` -> error + return 1.
- +0xF0 (240) StrNCopyPad source = `*edx`, max **0x3F=63**.
- +0x90 (144) StrNCopyPad source = `*arg_0` (a4 extraNamePtr), max **0x5F=95**.
- +0x17C (380) = dword_62E8D4; +0x180 (384) = `*arg_4` (a5).
InsertActionArgs(char, LoadRunAndStoreResult, *ecx|0x2E00000000, *ebx). All match.

### 0x43d844 CmdSetCharacterCamera — VERIFIED-1:1
StrCmp(*a2, "CLOSEUP"/...) order CLOSEUP,LEFT_SHOULDER,RIGHT_SHOULDER,EGO with
modes 0/1/2/3; unknown -> return 0 noop. Arg order strCmp(name, literal) ✓.

### 0x43dbc8 CmdLookAtCharacter — VERIFIED-1:1
AngleToTargetSigned(charScene, targetScene+76); v={0,angle,0}; SnapVectorToAxis;
v += *(charScene+132/136/140); scaled = angle*flt_616EA4*flt_616EA8;
ConvertX; (int)scaled; pack HIDWORD=7/LODWORD=*a1; InsertActionVararg.
Constants: flt_616EA4=0x43340000=180.0, flt_616EA8=0x3ea2f983=1/pi. ✓
**Float->int site analysed** — see "ConvertX truncation" note below; (int) cast
is correct (truncate toward zero).

### 0x43dcb0 CmdLookAtObject — VERIFIED-1:1
PointThroughBoneChain(obj, obj+76, out); AngleToTargetSigned(charScene, out);
scaled = angle*flt_616EDC*flt_616EE0 (0x43340000=180.0, 0x3ea2f983=1/pi);
ConvertX; (int)scaled; pack/vararg as above. ✓

### 0x43dd38 CmdSetCharacterToDummy — VERIFIED-1:1
PointThroughBoneChain(dummy, dummy+76, pos); ApplyVisibilityState(char,1,pos);
rot={0,0,0}; RotateVectorByHierarchy(dummy, &flt_5CA2B0, out3);
rot[1]=VectorAngleBetween(&flt_5CA2B0, out3); SetWorldTranslation(*(char+52),
&rot). flt_5CA2B0 vector bytes confirmed = {0x0,0x0,0x3f800000} = {0,0,1} —
matches source kRefAxis. SetWorldTranslation target = *(char+52)=charScene. ✓

### 0x43debc CmdAttachObjectToBone — VERIFIED-1:1
Disasm-faithful: var_C (bone id) set to 1 only on LeftHand match; RightHand->2
and Head->3 call+return directly; **no match leaves var_C uninitialized** and
falls to the shared AttachItemToBone(*a1, var_C, *a3) tail. Source models the
uninitialized read with sentinel kUninitBone (rule-1 edge case, inspectable).
StrCmp arg order StrCmp(literal, bone) matches source eq(s)=strCmp(s,bone). ✓

### 0x43df30 CmdPlayCharacterAni — VERIFIED-1:1
LightSetGrayColorThunk(0,4,&v7); BYTE2(v7)&=~1 (=&~0x00010000); LOBYTE(v7)=*a2;
ChangeTransparency(charScene, *(charScene+460), v7, a2). transport=*(char+292);
if set -> ChangeTransparency(*transport, *(*transport+460), v7, a2). All bitops
and offsets match.

### 0x43d9a4 PreloadAnimation — VERIFIED-1:1
PreloadAniSet(*a1, 4, *a2, *a4, *a3, *a5) = (char,4,*a,*b,*c,*d) given register
binding a@edx, c@ecx, b@ebx, d@stack. Reconstruction order *a,*b,*c,*d ✓.

---

## Key cross-cutting note — VIBE_Coord_ConvertX @0x5c6b08 and the LookAt float->int

The brief flags ConvertX as a truncation. Verified by decompiling it: it does
`fstcw; set CW high byte = 0x1F (RC=11 = round toward zero); fldcw; frndint;
fldcw(restore)`. It rounds **st0 (the angle value still on the FPU stack) toward
zero** to an integral float. The subsequent `fistp` (default round-to-nearest)
then stores an already-integral value exactly. Net effect of the original code is
therefore **truncation toward zero**, which is exactly what the source
`static_cast<i32>(scaled)` produces. The disasm at 0x43dc8d / 0x43dd16 shows a
bare `fistp` (NOT a (int) cast inline), but combined with ConvertX the result is
truncation. **The reconstruction and the golden (test expects 114 for
2.0*180/pi=114.59) are both correct — no change.** This is the one place the
"fistp rounds-to-nearest" hazard was a false alarm because ConvertX pre-truncates.

## Constants verified (get_global_value / get_bytes)
- flt_616EA4 = 0x43340000 = 180.0f
- flt_616EA8 = 0x3ea2f983 = 0.31830987f (1/pi)
- flt_616EDC = 0x43340000 = 180.0f
- flt_616EE0 = 0x3ea2f983 = 0.31830987f
- flt_5CA2B0[3] = {0x0, 0x0, 0x3f800000} = {0.0, 0.0, 1.0}
- Field offsets: 296, 492(0x1EC), 260(0x104), 240, 304, 144, 380, 384, 132/136/140,
  52, 292, 460, 152, 2528, 2564, +44 — all confirmed.
- Opcode packs: 0x3100000000 (take script), 0x2E00000000 (play ani script),
  HIDWORD=7 (vararg). Pad lengths 63 / 95. strlen guard 0x5F. ✓

## Counts
- Functions reviewed: 13
- VERIFIED-1:1: 13
- FIXED: 0
- BOUNDARY (hook stand-ins, pre-existing & documented in .h, rule-8 compliant): the
  cross-module leaves (action-queue allocator, math/transform helpers, object
  lookup, scene-graph readers) routed through Recon2Hooks; the Left dispatchers'
  Alt builders already reconstructed in character_render4. No new boundaries.
- Source edits: 0. Golden edits: 0. Build: clean. Tests: 111 checks, 0 failures.
