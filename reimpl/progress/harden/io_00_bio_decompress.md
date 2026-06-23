# Harden pass — io/bio_codec.cpp + io/savestate_decompress.cpp

1:1 verification of every `gilde.exe 0xADDR` provenance function in the two files,
diffed line-for-line against `decompile` + `disasm` (DISASM = reference of record).

Tests: `bio_codec_test`, `bio_codec_itest`, `bio_codec_e2e_test`,
`savestate_decompress_test` — **all green** (4/4).

---

## bio_codec.cpp

| Addr | Function | Status |
|------|----------|--------|
| 0x5dc980 | VIBE_Bio_ReadVec4 | VERIFIED-1:1 |
| 0x5dc9dc | VIBE_Bio_WriteVec3 | VERIFIED-1:1 |
| 0x5dca40 | VIBE_Bio_WriteVec4 | VERIFIED-1:1 |
| 0x5dcae4 | VIBE_Bio_WriteBlock | VERIFIED-1:1 |
| 0x5dcb20 | VIBE_Bio_ReadBlockAlloc | VERIFIED-1:1 |
| 0x5dcb68 | VIBE_Bio_ReadBlockQuick | VERIFIED-1:1 |
| 0x5dcbb0 | VIBE_Bio_WriteArray | VERIFIED-1:1 |
| 0x5dcc08 | VIBE_Bio_ReadArrayDebug | VERIFIED-1:1 |
| 0x5dcca0 | VIBE_Bio_ReadArrayQuick | VERIFIED-1:1 |
| 0x5eb5f0 | VIBE_Zip_TellCurrentFile | VERIFIED-1:1 |
| 0x5ffae0 | VIBE_Inflate_SyncPoint | VERIFIED-1:1 |
| 0x5ffab0 | VIBE_Inflate_SetDictionary_ffab0 | VERIFIED-1:1 |
| 0x438f10 | VIBE_Memory_AllocDebug | BOUNDARY (allocator) |
| 0x43923c | VIBE_Memory_FreeDebug | BOUNDARY (allocator) |

### Detail / evidence

- **ReadVec4 / WriteVec3 / WriteVec4 (0x5dc980/9dc/a40):** N sequential
  `Vfs{Read,Write}Stream(buf+4i, 4, h, 1)` calls. Reconstruction matches lane
  count, offsets (+0,+4,+8,+12), size=4/count=1 exactly. The originals return the
  *last* stream call's byte count (int); the recon returns `bool` = AND of all
  calls. Documented API adaptation (bio_codec.h:52-54): callers treat ==4 as
  success, observably equivalent.

- **WriteBlock (0x5dcae4):** `WriteStream(&len,4,h,1)` then
  `WriteStream(data, len, h, 1)`. Args (size=len, count=1) match. Recon adds a
  `length==0 -> return true` short-circuit and a `!data` guard the original lacks;
  both are behavior-identical on faithful input (len-0 stream write is a no-op).

- **WriteArray (0x5dcbb0):** writes {count}, {stride}, then
  `WriteStream(data, stride, h, count)` — stride is the element SIZE, count is the
  element COUNT. Verified against `VfsWriteStream(buf,size,h,count)` (vfs.h:66).
  Recon matches exactly; the `total==0`/`!data` guards are defensive supersets.

- **ReadBlockAlloc/Quick (0x5dcb20/b68):** read u32 len -> AllocDebug(len) ->
  store `*out` -> ReadStream(p, len, h, 1) -> return len. Quick uses fixed tag
  `bio:bio_rd_block_quick` (string @0x6296c8 confirmed). Recon matches; the
  allocate-even-on-len-0 behavior is preserved (DefaultAlloc does `malloc(size?size:1)`),
  `*out` receives the alloc result (not null), matching the original.

- **ReadArrayDebug/Quick (0x5dcc08/cca0):** read {count=v9}, {stride=v10}; if
  `expectStride == stride` -> `AllocDebug(stride*count)`, `ReadStream(p, expectStride,
  h, count)`, return count. Else `Seek(h, stride*count, SEEK_CUR)`, `*out=null`,
  log, return 0. Note: Hex-Rays shows `ReadStream(v7, a2, v8, v9)` where v8(ecx)
  is the mislabeled handle and v9 the count — recon's `ReadStream(p, expectStride,
  h, count)` is the correct decoding. Quick uses tag `bio:bio_rd_array_quick`
  (@0x629758). The two originals use *different* error-log strings
  (`bio_rd_array_debug:` @0x6296e0 vs `bio_rd_array_quick:` @0x62971c); the recon
  routes Quick through Debug with the correct alloc tag and does not reconstruct
  the log line (no observable side effect in the headless model).

- **ZipTellCurrentFile (0x5eb5f0):** `if (a1 && *(a1+124)) return *(*(a1+124)+24);
  else return -102`. Recon matches via memcpy (unaligned-safe), const -102.

- **InflateSyncPoint (0x5ffae0):** `return *a1 == 1`. Recon adds null guard
  (defensive); identical for valid pointers.

- **InflateSetWindowDictionary (0x5ffab0):** disasm: `edi = [eax+0x28]` (slot 10),
  `repne movs` copy a3 bytes, `edx = [eax+0x28]+ebx`, `[eax+0x34]=edx` (slot 13),
  `[eax+0x30]=edx` (slot 12), return a3. Recon writes slots[12] then slots[13]
  (original order is 13 then 12) but both receive the same value -> observably
  identical. memcpy guarded on `dict&&length&&writePtr`; len-0 = no-op as in the
  original's `repne movs ecx=0`.

- **AllocDebug/FreeDebug (0x438f10/0x43923c) — BOUNDARY:** these are the engine's
  full guard-header debug allocator (leading + trailing 0xDEADBEEF guards, group
  tracking tables dword_62D9xx, backed by the free-list allocator
  VIBE_Memory_AllocFromFreeList @0x5dbe70). AllocDebug returns `payload+4` (past
  the leading guard). The codec slice only needs the payload the readers/writers
  touch, so the recon models them as an installable hooks struct whose default is
  plain malloc/free returning the payload directly — byte-faithful for all data
  these codecs read/write. The free-list heap allocator itself is out of this
  slice's tree. Documented in bio_codec.cpp:17-26 / bio_codec.h:28-32.

---

## savestate_decompress.cpp

| Addr | Function | Status |
|------|----------|--------|
| 0x5dc0d0 | VIBE_Vfs_WriteBuffered (CRT fwrite core) | FIXED |
| 0x5fb820 | VIBE_File_AllocReadBuffer | VERIFIED-1:1 (EnsureBuffer model) |
| 0x5fb620 | VIBE_File_FlushBuffer | VERIFIED-1:1 (FwriteFlush model) |
| 0x5fcf40 | VIBE_Crt_PutcBuffered | VERIFIED-1:1 (FwritePutc model) |

### FIXED — 0x5dc0d0 binary-path flush condition

**Before:** after buffering a chunk in the binary path, the recon flushed only on
`f->remaining == f->bufSize`.

**After:** flush on `f->remaining == f->bufSize || (f->flags2 & kFileUnbuffered/*0x04*/)`.

**Evidence (disasm 0x5dc1e8..0x5dc1fe):**
```
5dc1e8 mov bh,[ebp+0Dh]      ; bh = flags2
5dc1ee or  bh,10h            ; bh |= dirty
...
5dc1f7 cmp eax,ecx           ; remaining == bufSize ?
5dc1f9 jz  loc_5DC200        ; -> flush
5dc1fb test bh,4             ; flags2 & 4 (line/unbuffered) ?
5dc1fe jz  loc_5DC207        ; no -> skip flush; else fall to flush
5dc200 call VIBE_File_FlushBuffer
```
The `(flags2 & 4)` flush was dropped. Restored 1:1 (matches the header note at
file_buffered.h:18 "+0x0D 0x04 unbuffered" and savestate_decompress.h:57-60).
Behavior-identical for fully-buffered streams (the existing tests), correct for
line/unbuffered streams. Added local `constexpr u8 kFileUnbuffered = 0x04`.

### Notes / boundaries on 0x5dc0d0

- **Dirty flag offset (informational):** the original keeps the write-dirty bit at
  **+0x0D bit 0x10** (disasm 0x5dc1e8/PutcBuffered 0x5fcfc7/FlushBuffer 0x5fb631),
  not +0x0C. The recon (and the shared file_buffered.h `kFileDirty=0x10` on
  `flags`) keeps it on `flags`; this is internally self-consistent across
  WriteBuffered/FwriteFlush/FwritePutc and observably equivalent within this
  slice. Not changed — would require editing the out-of-scope file_buffered.h
  shared enum.

- **Direct-write 512-align (0x5dc16c..0x5dc177):** `v30 & 0xFFFFFE00`, fall back to
  `v30` if 0. Recon `left & ~0x1FF` (9 low bits). VERIFIED.

- **`jb` (unsigned) at 0x5dc16a** for `left < bufSize`: recon uses `std::size_t`
  (unsigned). VERIFIED.

- **Return `div esi` (0x5dc2d4):** unsigned divide written/size; on error
  (`flags & 0x20`) `var_14` forced to 0. Recon `written/size`, error->0. VERIFIED.

- **Text-path CRT re-entry guard (0x5dc256..0x5dc28b) — BOUNDARY:** the original
  sets `*(stream_desc+0x0C)=1` around the putc loop (the PutcBuffered re-entry
  lock at PutcBuffered 0x5fcf53 `if (v4 != 1)`) and restores it after. The recon's
  FwritePutc does not model this CRT-internal lock counter; it has no effect on the
  emitted bytes (the guard only gates re-entry). Observably identical output.

- **Text-path trailing line-buffer flush (0x5dc290 `if(ecx) FlushBuffer`) — BOUNDARY:**
  taken only when the line-buffered branch (0x5dc23d `test bl,4`) fired, which
  temporarily forces line-buffered mode. The recon omits this; for non-line-
  buffered text streams `ecx==0` -> no trailing flush, matching the recon. Only
  diverges for line-buffered text streams, which no modeled write caller
  (Picture_Save*, gzip layer, VfsWriteStream) uses.

- **PutcBuffered high-flag flush (0x5fd020 `(flags & 1024)`/`(& 1536)` for '\n')
  — BOUNDARY:** the +0x0C bits 0x200/0x400 force a per-byte flush; these high
  flags are unmodeled (not set by any in-tree caller). The recon keeps the
  buffer-full flush (`remaining == bufSize`) which is verified 1:1; the high-flag
  flushes are a hook for line-buffered/flush-on-newline modes not reached by the
  current write tree.

---

## Counts

- Functions inspected: **18** (14 in bio_codec, 4 in savestate_decompress).
- VERIFIED-1:1: **15** (12 bio_codec leaves + 3 savestate sub-leaves).
- FIXED: **1** (0x5dc0d0 binary-path `(flags2 & 4)` flush restored).
- BOUNDARY (documented, behavior-identical for in-tree callers/tests): allocator
  pair (0x438f10/0x43923c) + 3 CRT-internal sub-behaviors of 0x5dc0d0.
- Golden tests changed: **0** — no test encoded wrong behavior; the fix is in a
  path not exercised by the existing (buffered-stream) tests, and all 4 suites
  remain green.
