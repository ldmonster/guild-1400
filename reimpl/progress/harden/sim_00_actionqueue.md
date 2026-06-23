# Harden report — src/sim/actionqueue.cpp (+ tests/unit/actionqueue_boundary_test.cpp)

Verified against gilde.exe via IDA MCP (decompile + disasm). Compiles clean (g++ -std=c++17 -fsyntax-only, rc=0) for both the TU and the boundary test.

## VIBE_ActionQueue_DispatchCurrent @0x404768 — FIXED
Disasm (0x40476d..0x4047dc) shows the node ptr is held in edx across the step call;
the decompile types it as a 4-byte (fn-ptr) array, so its `v1[5]` is byte offset +0x14.

- FIX (early-bail offset): `cmp dword ptr [edx+14h],0 ; jz->return 1` is the **owner**
  field (+0x14), NOT the +0x08 `ready` byte. Source bailed on `!node->ready`; corrected
  to `!node->owner`. Evidence: disasm 0x40477c `cmp dword ptr [edx+14h],0`; ActionNode
  layout has owner @+0x14, ready @+0x08.
- FIX (latch update): tail does `mov al,[[ecx+70h]+6Ch]; mov [ecx+85h],al` — it COPIES
  the motion handle's +108 frame byte into char+133, it does NOT zero it. Source set
  `ch->phaseLatch = 0`; corrected to copy the motion's field (modeled via motion->ready,
  since +108 is an anim-frame byte out of tree). Evidence: disasm 0x4047d6..0x4047dc.
- VERIFIED: order is step(ch) -> chained=[edx+4] -> ++[edx+0Ch] -> `if(!chained) return 1`;
  motion gate `!motion || char+128 || char+133==motion+108 || (u8)node+8 > (int)*motion`.
- BOUNDARY (memory-safety): original re-uses saved edx even if the step freed/replaced the
  head (original use-after-free). Reconstruction bails on head change. Observationally
  identical on all reachable inputs (step keeps node live); documented in source.
- BOUNDARY (anim): char+112 is a render/anim motion handle; its +0 priority int and +108
  frame byte live in the anim subsystem (out of tree). Gate/latch modeled on available
  fields; headless behavior (no live handle => skip chain) matches.

## VIBE_ActionQueue_CheckDurationExpiry @0x40bdd8 — VERIFIED-1:1
`if(!callCount@+12) args[2]@+52 = tick; if (args[1]@+48 + args[2]@+52 < (unsigned)tick ||
abort@+400) Unlink, return 0; else return 1`. Unsigned compare and offsets all match.

## VIBE_ActionQueue_FinishSetVisible @0x40b974 — FIXED
Original: `if(!result[3]) { VIBE_Character_SetVisible(result[5], result[12]); Unlink(); }`.
4-byte stride: result[3]=+0x0C=callCount (gate), result[5]=+0x14=owner,
result[12]=+0x30=args[1] (visibility flag).
- FIX: source fired UNCONDITIONALLY and wrote `owner->visible=visible` inline. Corrected to
  gate on `callCount==0` (no-op otherwise) and removed the inline write — the original's
  SetVisible @0x401894 (which toggles flag bit 0x20 at +140 etc.) is the leaf; here the
  setVisible hook stands in for it (the hook applies visibility). Evidence: disasm/decompile
  of 0x40b974 (gate [result+0Ch]) and 0x401894 (no plain `visible` int; bit-0x20 toggle).
- GOLDEN FIX: tests/unit/actionqueue_boundary_test.cpp FinishSetVisibleTogglesAndFrees
  encoded the wrong premise (unconditional fire + inline ch.visible write). Updated:
  the recording HSetVisible hook now applies ch.visible (modeling the SetVisible leaf);
  added a callCount!=0 no-op case; DispatchNotReadyNode -> DispatchNoOwnerNode (owner gate);
  DispatchNullStepFn now sets owner so it exercises the +0x00 step gate.

## VIBE_Character_RunActionOrFree @0x406a00 — VERIFIED-1:1 (control flow) / BOUNDARY
Original: `result = StepMotionQueue(a1); if(result==-1) Unlink();`. The unlink-iff-(-1)
control flow is exact. StepMotionQueue @0x4041e8 is render/anim entangled (BOUNDARY); the
inline model returns -1 when idle/done/aborted, persists otherwise — observable result matches.

## Counts
VERIFIED-1:1: 2 (CheckDurationExpiry, RunActionOrFree control flow)
FIXED: 2 (DispatchCurrent owner-gate + latch-copy; FinishSetVisible callCount-gate + drop inline write)
BOUNDARY: 2 anim/memory-safety notes (DispatchCurrent motion gate, RunActionOrFree StepMotionQueue)
Golden updated: actionqueue_boundary_test.cpp (FinishSetVisible + dispatch gate tests).
