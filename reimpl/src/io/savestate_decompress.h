#pragma once
// gilde.exe — guild::io  (CRT buffered-stdio `fwrite` core, recovered 1:1)
//
// WAVE-20 / W20-SAVELOAD provenance note (read this first):
// ---------------------------------------------------------------------------
// The wave-20 brief assigned three addresses under the assumption they form a
// "savegame compression + buffered VFS write" cluster wrapping the zlib port:
//
//   VIBE_DecompressGameState  @0x41ceb4   (1714 b)
//   VIBE_Decompressor_Init    @0x40e50c   (111 b)
//   VIBE_Vfs_WriteBuffered    @0x5dc0d0   (525 b)
//
// Decompiling all three (+ their callees down to leaves) shows the premise is a
// symbol-name artifact, NOT the actual semantics:
//
//   * 0x41ceb4 "VIBE_DecompressGameState" is NOT a decompressor. It is the
//     per-frame world/entity scene-update pass: it locks the DirectDraw back
//     buffer (via VIBE_DecompressState_Blob @0x423500, which calls the DDraw
//     surface vtable +100 = Lock and VIBE_Render_ReportDDrawError), then walks
//     the active entity table dword_67EB80[238*idx] calling
//     VIBE_EntityChild_Process / VIBE_Object_Reinitialize / VIBE_Animation_*
//     / VIBE_Object_Update / VIBE_Building_Update / VIBE_Entity_InteractionLogic
//     and finally unlocks (VIBE_Decompression_Finalize @0x4235dc). Its real
//     callers are GUI/text/book render refreshes (VIBE_Form_RefreshIfVisible,
//     VIBE_Book_Open, VIBE_Book_RefreshVisiblePages, VIBE_Text_RenderRichString).
//     => render/game-logic, fanning into ~20 functions OWNED BY OTHER wave-20
//     agents (Object_Update 0x40eea0, Animation_Apply 0x415b78,
//     Entity_InteractionLogic 0x41078c, EntityChild_Process 0x418f34, ...).
//     HANDOFF — do not reconstruct here (ODR/ownership). See progress doc.
//
//   * 0x40e50c "VIBE_Decompressor_Init" is NOT zlib init. It scans a 10240-byte
//     / 20-byte-stride result-handler table (dword_62D2DC) and dispatches
//     VIBE_Result_Handler_Interaction for entries bound to the given object.
//     Called only from 0x41ceb4 => part of the same scene-update subtree.
//     HANDOFF with 0x41ceb4.
//
//   * 0x5dc0d0 "VIBE_Vfs_WriteBuffered" IS the Microsoft C runtime `fwrite`
//     over the engine's buffered FILE clone (the same FILE record reconstructed
//     in io/file_buffered.{h,cpp}). The codebase already classified it as such
//     (io/vfs_recon3_tempdir.h:36 "= libc fwrite"). Its callers are the asset
//     writers (Picture_SaveTga/SaveBmp24/SaveBmpPalette, SampleBank_SaveBinary,
//     Text_SaveTextFile), the VFS write-stream front end (VIBE_Vfs_WriteStream
//     @0x4517a8) AND the gzip write layer (VIBE_Gzip_WriteBuffer/WriteStream/
//     FlushWrite). It is genuine, reconstructable leaf code — translated 1:1
//     below. (The simplified guild::io::FileWrite in file_buffered.cpp is the
//     VIBE_Vfs_WriteStream-path approximation; this is the exact CRT algorithm.)
// ---------------------------------------------------------------------------
//
// Recovered fwrite algorithm (0x5dc0d0), against the BufferedFile record:
//   1. Lock the per-stream mutex (off_64A910). Require the WRITE flag (+0x0C &2)
//      else set EINVAL + error bit and return 0.
//   2. count := size*count; if 0, unlock and return 0.
//   3. If no buffer is bound yet, allocate it (VIBE_File_AllocReadBuffer).
//   4. Snapshot then clear the low-mode bits: ungetc/lookahead bits (+0x0C &0x30)
//      are saved and stripped during the write, restored at the end.
//   5. BINARY path (+0x0C &0x40 set): loop over the request.
//        - If the buffer already holds pending bytes OR the remaining request is
//          smaller than the buffer capacity: copy min(room, left) bytes into the
//          buffer (qmemcpy), advance, mark dirty (+0x0D |0x10); if the buffer is
//          now full or the stream is line/unbuffered (+0x0D &4) flush it.
//        - Else: write directly, 512-byte-aligned (left & ~0x1FF; if that rounds
//          to 0, write `left`), bypassing the buffer, via VIBE_File_WriteHandle.
//        - On a -1 / 0 short write, set the error bit (and ENOSPC for 0).
//        - Advance src/total by the bytes consumed; stop at 0 or error.
//   6. TEXT path (no 0x40): force the dirty flush-marker, then emit every byte
//      through VIBE_Crt_PutcBuffered (which expands '\n' -> CR,LF in text mode);
//      stop on error (+0x0C &0x30). A final flush is issued if the stream had a
//      pending line-buffer commit pending.
//   7. If the error bit (+0x0C &0x20) got set, the returned byte count is forced
//      to 0. Restore the saved low-mode bits, unlock, return total/size records.

#include "guild/common/types.h"
#include "io/file_buffered.h"

#include <cstddef>

namespace guild::io {

// gilde.exe 0x5dc0d0 — VIBE_Vfs_WriteBuffered (the CRT fwrite core).
//
// Write `size * count` bytes from `src` through the buffered FILE `f`, returning
// the number of whole records (bytes_written / size) written — exactly like
// fwrite. `size == 0` returns 0 without touching the stream. Honors the
// binary/text split of the original: binary streams do block-aligned direct
// writes for large requests; text streams expand '\n' to CR,LF per byte.
//
// This is the exact engine entry point; the writers wired to 0x5dc0d0 in the
// original (Picture_Save*, SampleBank_SaveBinary, Text_SaveTextFile, the VFS
// write-stream front end and the gzip write layer) call through here.
std::size_t VfsWriteBuffered(const void* src, std::size_t size,
                             std::size_t count, BufferedFile* f);

} // namespace guild::io
