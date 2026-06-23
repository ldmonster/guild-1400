# Hardening sweep — render cluster 9 (surface present / stretch)

Files: `src/render/surface_present.cpp`, `src/render/surface_stretch.cpp`
(+ headers, + `tests/unit/surface_present_test.cpp`, `tests/unit/surface_stretch_test.cpp`)
MCP module: gilde.exe. Every provenance function decompiled AND disassembled, diffed
line-for-line. DISASM treated as reference of record where Hex-Rays mislabeled args.

Test targets: `surface_present_test`, `surface_stretch_test` — both GREEN
(plus the e2e + integration suites that reach these symbols: 5/5 pass).

## surface_present.cpp

### 0x4343E4 LockSurface — FIXED (minor)
- Desc offsets confirmed: var_7C=+0x10 (lPitch=v6), var_68=+0x24 (lpSurface=v7=ret),
  var_38=+0x54 (dwRGBBitCount=v8). Globals fill order matches.
- `dword_7626F8 = v6 / dword_7626E8` is **`div` (UNSIGNED)**. Recon used signed
  `i32 / i32`. Changed to unsigned `desc.pitch / (u32)bytesPerPx`. (Observable only
  for pitch >= 2GB; cheap to make exact.) Zero-guard kept (original has none — would
  fault; guard only prevents UB trap, never reached in practice).

### 0x434468 LockSurfaceWait — VERIFIED-1:1
- `LOWORD(a2) = a2 | 0x801` ⇒ `(flags & 0xFFFF0000) | ((flags|0x801) & 0xFFFF)` — exact.
- Same unsigned-div fix applied to its stride computation.

### 0x434508 BeginFrameLock — VERIFIED-1:1
- switch(byte_762721) arms 0 / 1,3 / 2,4 / default and the GDI fall-through (LABEL_4)
  all match; negated pitch/stride for the bottom-up DIB confirmed.

### 0x4345D4 AcquireBackBuffer — VERIFIED-1:1
- Same shape; DDraw arm uses LockSurfaceWait(primary,0). `goto LABEL_4` fall-through
  matches case 0 with no dibBase.

### 0x434680 UnlockBackBuffer — FIXED (behavioral)
- Disasm 0x43468a..0x4346a8: when a lock is held, `mov al, byte_762721; and eax,0FFh`
  makes the return register = `(u8)mode`. Cases 0/2/4 then `return eax` = the MODE
  byte, **not** the passthrough `status`. Recon returned `status`. Now returns
  `(u8)mode` for 0/2/4; Unlock() result for 1/3; incoming for the no-lock early-out;
  and `(status&~0xFF)|(u8)mode` for the >4 `ja` default (al-patched, hi bits kept).
- Golden `UnlockBackBufferLockModesClearOnly` encoded the wrong value (expected 7);
  fixed to expect 2 (= DDrawLockBlt). `UnlockBackBufferNoLock` (passthrough 99) and
  `...DDrawUnlocks` remain correct.

### 0x423050 CopyRegionRgb — VERIFIED-1:1
- UnpackColor(0x434F7C) params: r@edx, g@ecx, b@ebx. Call is `UnpackColor(px, out+0,
  out+2, out+1)` ⇒ out[0]=R, out[2]=G, out[1]=B. Recon `UnpackColor(fmt,px,o[0],o[2],
  o[1])` matches. Source stride dword_7626F8 in pixels, `result = 3*cols`. Exact.

### 0x5DE87C CopySurfacePixels — BOUNDARY
- Original is a DDraw dance: GetSurfaceDesc(dst)+Lock both surfaces+
  StretchSurfaceDispatch+self-recurse on attached desc+Unlock. The pure pixel effect,
  once both surfaces are system-memory-locked, is one StretchSurfaceDispatch (verified
  separately). DDraw lock/desc/recursion is the rules-3/4 boundary.

### 0x42E4EC ReportDDrawError — BOUNDARY
- ~200KB DDERR_* string switch (DirectDraw SDK table). Portable subset returns the
  symbolic name for the lock/present codes + hex fallback. SURFACELOST=0x887601C2
  matches kDDErrSurfaceLost.

## surface_stretch.cpp

### 0x435D88 StretchSurface8 — VERIFIED-1:1
- esi=eax=dst (write, ++ebx), edi=edx=src (read). i<dst.h, c<dst.w; src row =
  src.h*i/dst.h; src col = c*src.w/dst.w. Matches.

### 0x436488 StretchSurface8Up — FIXED (behavioral, read/write inverted)
- Disasm: edi=eax=a1=**DST written** (ebp = a1.pixels + a1.pitch*(a1.h*i/a2.h)),
  esi=edx=a2=**SRC read sequentially** (ebx = a2.pixels + a2.pitch*i, ++ebx). Loop
  bounds are the **SOURCE** dims (esi[2]/esi[0xC]). Write addr = ebp + a1.w*c/a2.w;
  value = *ebx; return al = last *ebx.
- Recon had read/write inverted (read from the "src" param, wrote into the "dst"
  param sequentially) and the wrong loop bound. Rewrote body to the disasm and
  re-signed the prototype to `StretchSurface8Up(dst /*eax*/, src /*edx*/)`.
- Dispatch up-sample arm changed to `StretchSurface8Up(dst, src)` (eax=a1=dst).
- Golden `Surface8Upscale` had reversed args + verified the inverted result; rewritten
  to the binary (src col c -> dst col c*dstW/srcW, src-bound loop, return last src byte).

### 0x435E00 StretchAverage16 — VERIFIED-1:1
- i/j/k = trailing-zero pos of src R/G/B masks. Per-channel accumulate
  ((px&mask)>>pos), divide by count, repack `((G/c)<<j)|((R/c)<<i)|((B/c)<<k)`. Matches.

### 0x437814 Convert24To16 — FIXED (channel byte order)
- Disasm 0x43790e..0x437954: src byte **+0 -> R** channel (rPos/rBits), **+1 -> G**,
  **+2 -> B**. Recon had `R=s[2], B=s[0]` (swapped) and a "(B,G,R)" comment.
  Corrected to `R=s[0]; G=s[1]; B=s[2]`. Drops are 8-bits per DEST mask. The `sar`
  of a movzx byte (0..255) by >=0 equals an unsigned shift, so `>>` is faithful.
- Goldens `Convert24To16Golden` and `ConvertDispatch24To16` encoded the swapped
  result; recomputed to the binary: {10,20,30}=0x08A3, {40,50,60}=0x2987,
  {70,80,90}=0x428B, {100,110,120}=0x636F (last=0x636F). White/Black symmetric — kept.

### 0x437530 StretchSurfaceDispatch — FIXED (gate return value)
- The return register al = `[edx+54h]` = src.bpp at entry. On either gate-fail
  (depth mismatch, or not indexed/16/24/32) the function returns `(u8)src.bpp`, NOT 0.
  Recon returned 0. Fixed `status` to `(u8)src.bpp`.
- Size compares are unsigned (`jnb`); equal-size rowBytes (indexed=src.w, 16=2*dst.w,
  24=3*dst.w, 32=4*dst.w) and dst.w-bound copy loop match; returns dst.width.
- 24/32 average and 16/24/32 interpolate leaves remain BOUNDARY (not in tree); their
  return-register state is dst.width at the dispatch point, so deferred returns set to
  `(u8)dst.width` for faithfulness.
- Golden `DispatchDepthMismatch` expected 0; fixed to 8 (=(u8)src.bpp). DispatchEqual16
  / Dispatch8Downscale unaffected.

### 0x437980 BlitConvertDispatch — VERIFIED-1:1
- Requires equal w AND h. Depth-match/both-indexed → per-row copy (rowBytes from
  a3=src: indexed=src.w, 16/24/32=N*src.w, default=return status); loop src.height,
  returns src.height. 8->16 → Convert8To16Indexed(a1,a3,status). 24->16 →
  Convert24To16(a1,a3). Else return status. Matches.

### 0x56D6D4 BlitThumbnailToSurface — VERIFIED-1:1 (lock bracket BOUNDARY)
- 160x120 16bpp block; src read sequentially (word index ++). dst word =
  (surf+28) + (col + surf[16]*row). Loop col!=x+160, row!=y+120. Matches. The original
  brackets with DecompressState_Blob/Finalize (surface lock/unlock) and returns
  Finalize's value; modeled as explicit base/stride + row-count return (rules-3/4
  boundary).

## Counts
- VERIFIED-1:1: 8  (LockSurfaceWait, BeginFrameLock, AcquireBackBuffer, CopyRegionRgb,
  StretchSurface8, StretchAverage16, BlitConvertDispatch, BlitThumbnailToSurface)
- FIXED: 5  (LockSurface unsigned-div, UnlockBackBuffer return, StretchSurface8Up
  read/write inversion, Convert24To16 channel order, StretchSurfaceDispatch gate return)
- BOUNDARY: 2  (CopySurfacePixels DDraw dance, ReportDDrawError SDK string table)
- Golden vectors corrected (each cited above): Convert24To16Golden, ConvertDispatch24To16,
  Surface8Upscale, DispatchDepthMismatch, UnlockBackBufferLockModesClearOnly.
- Tests: surface_present_test PASS, surface_stretch_test PASS, +itest/e2e PASS (5/5).
