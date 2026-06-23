# Harden sweep — src/gui/gui_dialogs3.cpp

MCP-verified line-for-line against gilde.exe. 13 provenance'd functions + 1 local clone.

## Per-function results

- **StrNCopyPadLocal** (clone of VIBE_Util_StrNCopyPad @0x5d9360) — VERIFIED-1:1.
  Copy-until-NUL then zero-fill remainder, return original dst. No trailing NUL when
  the span is filled. Matches disasm exactly.

- **Gui_ClipRectToBuffers** @0x40e94c — FIXED.
  - Before: `x = x & 0xFFFE;` (full 32-bit AND).
  - After: `x = (x & ~0xFFFF) | (x & 0xFFFE);` — the binary at 0x40e9d4 is
    `LOWORD(a4) &= 0xFFFE`, which touches ONLY the low 16 bits; the high word of a4 is
    preserved and the field at +8 is a full DWORD store. Identical for x in [0,0xFFFF];
    divergent for larger/negative x. Rest (clamp top/bottom, flags-1, dual-buffer push,
    len++ via dword_62D2DC alias, return result*4=8) VERIFIED-1:1.

- **Gui_HitTestObject** @0x414d98 — VERIFIED-1:1 (with one noted boundary).
  All field offsets/branches confirmed: x(+16)/w(+20), y(+18)/h(+22), clipY0(+32)/
  clipY1(+34), disabledB(+56), renderPtr(+52), marker(+0), type==64 path → ownerWindow(+116),
  else groupLink(+44)/backWidget(+620), id(+8). RNG: none. BOUNDARY: the `+408`
  owner-state read (`v4 = *(v3->parentClip + 408)`) is a deref into a GameObject record
  not modeled in this module's data table; recon treats it as 0 (active). Data-not-in-tree.

- **Gui_HitTestWindow** @0x414ec8 — VERIFIED-1:1.
  High-water-down scan; field reads x(+16)/w(+20)/y(+18)/h(+22)/type(+24==64)/inUse(+4)/
  clipY0(+32)/clipY1(+34)/disabledB(+56)/id(+8); miss paths set g_hitTestSlot=-1. Matches.

- **Window_PositionAtCoord** @0x41d7e0 — VERIFIED-1:1.
  Disasm-checked. Signed w/2 (sar+correction == C++ int /2). Centered formula
  `(x+w/2) - centreRef/divisor + centreRef - w/2`; non-centered = (double)x(+4).
  Clamp maxX = (clipExt>>16) - w via fcomp/jnb. Float->int: VIBE_Coord_ConvertX @0x5c6b08
  sets x87 round-toward-zero (chop) then frndint+fistp == truncation == recon `(int)x`.
  backWidget at +620. accumulation exact in double for the integer/2.0 inputs used.

- **Window_PositionAtCoord_Thunk** @0x41d964 — VERIFIED-1:1.
  `PositionAtCoord(dword_676A60[171*a1 + 1], a2)` == form's window 0. Matches.

- **Window_ConsumeClickFlag** @0x41b870 — VERIFIED-1:1. read-and-clear, exact.

- **Widget_BlitClippedRows** @0x412668 — FIXED.
  - Before: `int lastRowBytes = 2*rowPixels;` initialized before the loop → returned 2*w
    even on zero iterations.
  - After: `int result = widgetIdx;` then `result = 2*rowPixels;` only inside the body.
    Evidence: result is `eax` (the widgetIdx arg); the binary only overwrites eax inside
    the loop body (0x412702 `mov eax,ecx` where ecx=2*w). On zero iterations
    (jge loc_412718 at 0x4126c9) eax is never reassigned, so the return is the original
    widgetIdx, NOT 2*w. Loop math (rows=h(+22), top=y(+18), clip1=clipY1(+34),
    clip0=clipY0(+32), src=widget[+120]+2*i*w, dst=ctx[+28]+2*(x+stride*(i+y)),
    stride=ctx[+16], copy=2*w) VERIFIED. Note: a2(edx) is the global render context
    dword_62D210 (caller 0x413ef1); recon decomposes ctx+16/ctx+28 into stride/dst args
    (faithful data-model split).

- **Widget_CreateRawBitmap** @0x412560 — VERIFIED-1:1.
  Disasm-recovered the 4th register arg h(ecx→+22) that Hex-Rays dropped (v9). Reg map:
  x(eax/di→+16), y(edx→+18), w(ebx/si→+20), h(ecx→+22). type=71(+24), id=-1(+8),
  +32=0,+28=0, +34=LOWORD(clipExt), +30=HIWORD(clipExt). Alloc=2*h*w, qmemcpy 2*h*w. Matches.

- **Widget_AddRawBitmapToWindow** @0x412618 — VERIFIED-1:1.
  CreateRawBitmap(x + win.x()(+4), y + win.y()(+6), w, h, pixels) then AttachToWindow. Matches.

- **Form_RefreshIfVisible** @0x41cc5c — VERIFIED-1:1.
  Guards v5[100](valid)/v5[102](shownFlag); null-arg → return 1; else Broadcast,
  SetButtonAnimations, Present(VIBE_DecompressGameState) in order, return 1. Hook edges
  are deferred subsystem boundaries (form present/broadcast/anim).

- **Gui_FreeString_Thunk** @0x411eac — VERIFIED-1:1.
  StrNCopyPad(widget +152, src, 63). Matches.

- **Widget_SetTooltipText** @0x421a24 — VERIFIED-1:1. unrolled byte-pair copy incl NUL. Exact.

## Counts
- VERIFIED-1:1: 11 (StrNCopyPadLocal, HitTestObject, HitTestWindow, PositionAtCoord,
  PositionAtCoord_Thunk, ConsumeClickFlag, CreateRawBitmap, AddRawBitmapToWindow,
  Form_RefreshIfVisible, FreeString_Thunk, SetTooltipText)
- FIXED: 2 — Gui_ClipRectToBuffers @0x40e94c (LOWORD-only AND);
  Widget_BlitClippedRows @0x412668 (zero-iteration return = widgetIdx, not 2*w)
- BOUNDARY: 1 — Gui_HitTestObject @0x414d98 `+408` owner-state read (data not in tree);
  plus the Form_RefreshIfVisible hook edges (form subsystem) remain hooks.

## Tests
gui_dialogs3_test / gui_dialogs3_itest / gui_dialogs3_e2e_test — all PASS (green).
Both fixes are edge cases not exercised by existing goldens, so no golden churn needed;
existing goldens already encode the binary-correct behavior for their tested paths.
