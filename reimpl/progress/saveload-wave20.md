# Wave-20 — W20-SAVELOAD: "savegame compression + buffered VFS write"

**Agent:** W20-SAVELOAD · **MCP:** live (`gilde.exe`, imagebase 0x400000) · **Date:** 2026-06-16

## TL;DR

The three brief'd addresses do **not** form a savegame-compression cluster. The
"Decompress" names are IDA symbol artifacts. Decompiling all three plus their
callees down to genuine leaves shows:

| addr | brief name | what it ACTUALLY is | disposition |
|------|-----------|---------------------|-------------|
| 0x5dc0d0 | VIBE_Vfs_WriteBuffered | **MSVC CRT `fwrite`** over the engine's buffered FILE clone | **reconstructed 1:1** (this module) |
| 0x41ceb4 | VIBE_DecompressGameState | **per-frame world/entity scene-update + DDraw back-buffer lock** | **handoff** (other wave-20 agents) |
| 0x40e50c | VIBE_Decompressor_Init | **result-handler dispatch** over a 10240-byte handler table | **handoff** (subtree of 0x41ceb4) |

The codebase had already reached the same conclusion for 0x5dc0d0:
`src/io/vfs_recon3_tempdir.h:36` reads `VIBE_Vfs_WriteBuffered @0x5dc0d0 = libc
fwrite (FILE buffer mutation)`. There is **no zlib wrapper** anywhere in this
cluster; the actual zlib port lives in `src/compress/` and is reached through the
gzip write layer which *calls* fwrite (0x5dc0d0), it does not live inside it.

## Reconstructed (this module): `VfsWriteBuffered` @0x5dc0d0

Files: `src/io/savestate_decompress.{h,cpp}`, `tests/unit/savestate_decompress_test.cpp`.

`0x5dc0d0` is the Microsoft C runtime `fwrite` implementation, recovered 1:1 from
the disassembly. It operates on the buffered FILE record already reconstructed in
`src/io/file_buffered.{h,cpp}` (the `BufferedFile` struct). Control flow mirrors
the original exactly:

1. **`off_64A910` lock + WRITE-flag gate** (0x5dc0e5..0x5dc10a): require the
   `+0x0C & 2` write flag; otherwise `VIBE_Runtime_SetErrnoEinval`, set the error
   bit `+0x0C |= 0x20`, return 0.
2. **Zero-length short-circuit** (0x5dc10f..0x5dc127): `total = size*count`; if 0,
   return 0 *before* the trailing `/size` divide (so `count==0`/`size==0` are
   distinct early-outs that never touch the buffer).
3. **Lazy buffer bind** (0x5dc12b..0x5dc133): `VIBE_File_AllocReadBuffer` if the
   stream has no buffer yet (default 4096 = `0x1000`).
4. **Binary path** (`+0x0C & 0x40`, 0x5dc15d..0x5dc22f): loop —
   - if the buffer already holds pending bytes OR `left < bufSize`: `qmemcpy`
     `min(room, left)` into the buffer, advance cursor `*a3`, bump `remaining`
     `+0x04`, mark dirty `+0x0D |= 0x10`; flush (`VIBE_File_FlushBuffer`) when the
     buffer fills;
   - else: direct write of `left & ~0x1FF` (512-aligned; if that rounds to 0,
     write `left`) straight to `VIBE_File_WriteHandle`, bypassing the buffer;
   - on a `-1`/`0` short write set the error bit (`0` => ENOSPC); stop on
     completion or sticky error.
5. **Text path** (no 0x40, 0x5dc23a..0x5dc2a5): emit every source byte through
   `VIBE_Crt_PutcBuffered`, which prepends `\r` (0x0D) before each `\n` (0x0A) in
   text mode; stop on error.
6. **Error => 0** (0x5dc2ae): a sticky error forces the reported byte count to 0.
7. **Return** `total / size` (whole-record count, 0x5dc121).

Callees decompiled to leaves and verified as CRT/Win32, NOT codec logic:
- `VIBE_File_WriteHandle @0x5fce50` → Win32 `WriteFile`/`SetFilePointer` (rule-4
  boundary: replaced by `shim::IFile::write`).
- `VIBE_File_FlushBuffer @0x5fb620`, `VIBE_Crt_PutcBuffered @0x5fcf40`,
  `VIBE_File_AllocReadBuffer @0x5fb820` → buffered-FILE internals, already modeled
  by `BufferedFile`/`FileFlush` in `file_buffered.cpp`.

### Real callers of 0x5dc0d0 (from `xrefs_to`)
`VIBE_Picture_SaveTga`, `SaveBmp24`, `SaveBmpPalette`, `SampleBank_SaveBinary`,
`Text_BuildTextArray`, `Text_SaveTextFile`, **`VIBE_Vfs_WriteStream @0x4517a8`**,
and the gzip write layer (`VIBE_Gzip_WriteBuffer/WriteStream/FlushWrite`). All are
asset/text/compressed-stream *writers* that funnel bytes through fwrite.

### Wiring (rule 13)
`VfsWriteBuffered(const void* src, size, count, BufferedFile*)` is exposed as the
canonical engine fwrite entry. It is golden-pinned and round-trips against
`FileRead`/`FileFlush` (both in `file_buffered.cpp`, reused via the shared lib).

**One-line handoff** (file not owned by this agent): in `src/io/vfs.cpp`'s
`VfsWriteStream @0x4517a8`, the loose-file (non-memory) branch currently does a
raw `wt->file->file->write(src, want)`. To match the original 1:1 it should route
through the buffered FILE: `guild::io::VfsWriteBuffered(src, size, count, wt->file)`
where `wt->file` is the `BufferedFile`. (Left as a documented handoff to the vfs.cpp
owner; observable byte output is identical for binary streams, differs only by
text-mode CR/LF expansion and buffering granularity.)

## Handoff: 0x41ceb4 + 0x40e50c (NOT decompression)

`VIBE_DecompressGameState @0x41ceb4` (callers: `VIBE_Form_RefreshIfVisible`,
`VIBE_Book_Open`, `VIBE_Book_RefreshVisiblePages`, `VIBE_Text_RenderRichString`)
is the per-frame scene-update pass:
- locks the DirectDraw back buffer via `VIBE_DecompressState_Blob @0x423500`
  (calls the DDraw surface vtable +100 = Lock + `VIBE_Render_ReportDDrawError`),
- walks the active entity table `dword_67EB80[238*idx]` dispatching
  `VIBE_EntityChild_Process @0x418f34`, `VIBE_Object_Reinitialize @0x40e818`,
  `VIBE_Animation_Apply @0x415b78`, `VIBE_Object_Update @0x40eea0`,
  `VIBE_Building_Update @0x40e2b4`, `VIBE_Entity_InteractionLogic @0x41078c`,
  `VIBE_Animation_Basic/Advanced`, `VIBE_Physics_Update`, `VIBE_Velocity_Apply`,
  `VIBE_Coord_Push @0x5d8ae8`, `VIBE_State_Finalize`, `VIBE_Result_Broadcast`, …
- unlocks via `VIBE_Decompression_Finalize @0x4235dc`.

`VIBE_Decompressor_Init @0x40e50c` (only caller: 0x41ceb4) scans a 10240-byte /
20-byte-stride result-handler table (`dword_62D2DC`) and dispatches
`VIBE_Result_Handler_Interaction @0x42395c` for entries bound to the object, then
`VIBE_Light_SetGrayColorThunk`. It is part of the same scene-update subtree.

**These two are render/game-logic, fanning into ~20 functions that are explicit
wave-20 targets owned by OTHER agents** (Object_Update, Animation_Apply,
Entity_InteractionLogic, EntityChild_Process, Coord_Push, Object_Reinitialize,
Building_Update — all on the coverage-audit-wave18 missing list). Reconstructing
them here would create ODR clashes and step on those agents' ownership. Deferred
to the entity/animation/scene wave-20 cluster; the DDraw surface-lock pair
(0x423500 / 0x4235dc) is a rule-3 Vulkan boundary, not codec logic.

## Tests

`tests/unit/savestate_decompress_test.cpp` — 10 tests / 26 checks, all passing
(memory-backed `IFile` captures committed bytes):
- binary small-write buffering + flush; binary record-count (size*count);
- binary large request → 512-aligned direct write path, byte-exact;
- text `\n` → CR,LF expansion; text plain passthrough;
- `size==0` and `count==0` no-op contracts;
- not-writable → error bit + 0; backend failure → count forced to 0;
- binary round-trip (write via fwrite, read back via `FileRead`).

No real `.sav` e2e: the format is **not reachable through this leaf** — fwrite is
generic byte plumbing under the asset/gzip writers, not a savegame codec, so a
`.sav` golden would test the save serializer (other modules), not 0x5dc0d0.

## Build status
`build/` green: `libguild.a` links with the new module; `savestate_decompress_test`,
`io_save_test` (153 checks), `io_vfs_test` (74 checks) all pass.
