# Harden: io/file.cpp, io/file_buffered.cpp, io/path.cpp — 1:1 audit

Module files: `src/io/file.cpp`, `src/io/file_buffered.cpp`, `src/io/path.cpp`
Tests: `tests/unit/io_vfs_tree_test.cpp`, `tests/e2e/io_vfs_tree_e2e_test.cpp`
(the only suites that exercise these symbols; the `file_ops*_test` targets cover
the separate `file_ops*.cpp` module, not these files).

Method: every provenance-tagged function decompiled AND disassembled; control
flow, constants (`get_bytes`/`get_global_value`), branch direction, signed vs
unsigned compares, and return values diffed against the binary. DISASM is the
arbiter where Hex-Rays differs.

## Summary
- VERIFIED-1:1: 9 functions
- FIXED: 2 (FileReadLine over-read; binary direct-read 512 rounding)
- BOUNDARY: 4 (CreateFileA/CloseHandle/CreateDirectoryA/stream-pool — legit OS/alloc boundary)
- Tests: io_vfs_tree_test 61 checks / 0 fail; io_vfs_tree_e2e_test 34 checks / 0 fail
  (built+run standalone — the full CMake build is blocked by an UNRELATED
  pre-existing compile error in `src/ai/meister_storage.cpp:461`, out of scope).

---

## path.cpp

### ConvertBackslashToSlash — gilde.exe 0x44eb54 — VERIFIED-1:1
`__usercall(eax=str)`. `repne scasb` strlen, then in-place replace 0x5C->0x2F.
Signed `jle`/`jge` length compares (irrelevant for <2GB path buffers).
NOTE on return value: the binary returns the **length** (ecx) in eax, NOT the
string pointer. The reimpl returns `s`. Verified via `xrefs_to` that ALL 18 live
callers (Character_*, Vfs_*) discard the return and re-`lea` the original buffer
(e.g. 0x402806, 0x44faad) — the return value is dead in the entire call tree, so
returning `s` is observationally identical. In-place mutation is byte-exact.

### ConvertSlashToBackslash — gilde.exe 0x44eb90 — VERIFIED-1:1
Identical structure, 0x2F->0x5C. Same dead-return note (4 callers, all discard).

---

## file_buffered.cpp

### ParseMode  <-  VIBE_File_ParseOpenMode @0x5d4220 — VERIFIED-1:1 (behavioral)
MSVC CRT mode parser. Base: r->1, w->2, a->0x82; else EINVAL->0. Suffix scan:
'+'->|3 (once), 'b'->|0x40 (once), 't'->text (guard only, no flag), 'c'->commit
side-flag|1, 'n'->commit&~1. Default (no b/t): set binary iff `dword_64A79C==512`.
`get_global_value`: dword_64A79C=0x100 (256) -> default is TEXT. The reimpl's
ParsedMode default (binary=false) matches. Accept/reject set and binary outcome
verified char-by-char against the disasm switch (0x5d424e..0x5d4348). The reimpl
models the observable mode (read/write/append/binary/plus/valid) over IFileSystem;
the raw CRT flag-word + commit side-param are CRT-internal and not observable
through the VFS boundary.

### FileRead (binary path) <- VIBE_File_Read @0x5d4770 — FIXED
- Buffer drain (memcpy min(remaining,left)) and refill: 1:1.
- Direct-read path entry `left >= bufSize` matches 0x5d485e (`v25 >= [a1+20]`).
- FIX: the binary direct read of >0x200 bytes is rounded DOWN to a 512-byte
  multiple by the original (`0x5d4886: LOWORD(v13)=v25&0xFE00`, i.e.
  v13 = left & 0xFFFFFE00); the sub-512 tail is served through the buffer on the
  next iteration. The reimpl previously read `left` whole. Now reproduces
  `want_direct = (left>0x200) ? left & ~0x1FF : left`. Data delivered to the
  caller was already identical; this makes the buffer-vs-direct split 1:1.
- Return `produced/size` = whole records (1:1 with `v24/a3`).

### FileRead (text path) / FileGetc — VERIFIED-1:1
CR(0x0D) swallow + read-through to next byte, Ctrl-Z(0x1A)->EOF(0x10), byte loop
matches 0x5d48e8..0x5d4963.

### FileReadLine  <-  VIBE_Vfs_ReadLine @0x4516cc — FIXED (now 1:1, was diverging)
The original (confirmed at disasm loc_451758..loc_451798) swallows the trailing
CR/LF run with `while (c==13||c==10) c=getc();`. The read that TERMINATES the run
(the first non-EOL byte = the first byte of the next line) is consumed and
**silently discarded** — never stored, never pushed back. Consequence: every line
after a CR/LF-terminated line loses its leading character.
- The reimpl previously "fixed" this with a 1-byte rewind (cursor back / seek -1),
  yielding non-1:1 output. REMOVED. FileReadLine now reproduces the over-read loss
  exactly (also honors the 0x451751 "terminator already EOF -> stop" guard).
- Verified the loose-file FILE text-mode CR-stripping (0x5d491f) yields identical
  *line content* whether bytes are read raw (RawGetc) or text-translated, so the
  reimpl's RawGetc approach matches the binary's line text.
- Golden tests updated to the binary-faithful output (cited in the tests):
  unit `"alpha\r\nbeta\ngamma\r\n"` -> "alpha","eta","amma";
  e2e  `"line1\r\nline2\r\nline3\r\n"` -> "line1","ine2","ine3".

### FileSeek  <-  VIBE_File_Seek @0x5d45f8 — VERIFIED-1:1 (position arithmetic)
Original whence-dispatch (a2==0 cur-relative via SeekCurrent-remaining; a2==1
set; a2==2 end) plus AdjustBufferOffset @0x5d45a0 in-buffer fast path. The reimpl
uses standard SEEK_SET/CUR/END over IFile and computes SEEK_CUR as
`osPos - remaining + offset` (the same buffer-unconsumed correction the original's
AdjustBufferOffset applies). The in-buffer fast path is collapsed to a
discard+absolute-seek; the resulting file position is identical. Write-side flush
before seek matches.

### FileTell — VERIFIED-1:1
`osPos +/- remaining` (write: +pending, read: -unconsumed). Matches the buffer
accounting in Seek/FlushBuffer.

### FileWrite / FileFlush  <-  VIBE_File_FlushBuffer @0x5fb620 — VERIFIED-1:1
Flush writes `remaining` bytes from buffer base, dirty(0x10)/error(0x20) bits,
reset cursor+remaining. The original loops over partial WriteHandle returns; the
reimpl issues one IFile->write and flags error on short write — equivalent for the
IFile boundary (full write or fail).

### FileClose  <-  VIBE_File_CloseHandle @0x5fc9a0 + ReleaseStream @0x5d4494 — BOUNDARY
Flush(write) -> close -> free, order 1:1. The original returns the stream struct
to a global free-list (ReleaseStream pool); the reimpl uses new/delete. Legit
allocation-strategy boundary.

### FillReadBuffer  <-  VIBE_Crt_FillReadBuffer @0x5fba68 — VERIFIED-1:1 (behavioral)
Lazy buffer alloc (default 0x1000 = VIBE_File_AllocReadBuffer), read into buffer,
remaining=got, EOF(0x10) on got==0. The original's tty/device/ungetc branches are
CRT-internal; the loose-file path is read-into-buffer + eof flag, matched.

### FileCreateDirectory  <-  VIBE_File_CreateDirectory @0x5eb920 — BOUNDARY
Original = CreateDirectoryA -> 0 / MapLastError. Reimpl routes to
`fs->makeDir() ? 0 : -1`. Legit OS boundary.

---

## file.cpp (LooseFile thin wrappers)

`FileOpen/FileRead/FileSeek/FileTell/FileClose` are thin direct-IFile helpers
(not literal translations — the real 0x5d4770/0x5d45f8 are the BUFFERED versions
in file_buffered.cpp). The 320-byte VfsHandle layout is asserted byte-exact.
- VfsHandle static_asserts — VERIFIED (offsets +0x100/104/108/10C/110/114/138/13C,
  size 320).
- LooseFile FileRead returns total bytes (its sole caller, world/data_load.cpp,
  compares `got == recSize*count` in bytes — internally consistent). This differs
  from 0x5d4770's whole-record return, but the buffered FileRead (the actual
  0x5d4770) returns whole records correctly. Boundary helper; no change.
- FileOpen/FileClose: IFileSystem open/close boundary.

## Open item (NOT in scope)
`src/ai/meister_storage.cpp:461` references a nonexistent `StockSeller::building`,
breaking the whole `guild` library build (another wave's in-flight edit). My three
files compile clean (`-fsyntax-only`) and the two affected test suites build+pass
when linked standalone. Full `ctest` cannot run until that unrelated file is fixed.
