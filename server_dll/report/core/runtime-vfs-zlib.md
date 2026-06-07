# Core Runtime: VFS, zlib/minizip, Memory, Text/Config, CRT (VERIFIED)

These subsystems are not multiplayer-specific but are on the load path (world state is
gz/zlib-compressed and read through the VFS) and were fully named for completeness.

## zlib 1.1.3 (0x41d660–0x426450)
Stock zlib, confirmed by version/error strings (" inflate/deflate 1.1.3 … Mark Adler/Jean-loup Gailly").
All named `VIBE_zlib_*`.
- **Inflate path (decompresses world state each tick):** `VIBE_zlib_inflate`(0x422504) →
  `VIBE_zlib_inflate_blocks`(0x42073c) → `inflate_trees_fixed`/`huft_build`(0x422c78)/
  `inflate_trees_bits`(0x4232f0)/`inflate_trees_dynamic`(0x42338c) → `inflate_codes`(0x42166c)/
  `inflate_fast`(0x421e80) → `inflate_flush`(0x423550). Init `inflateInit2_`(0x422384).
- **Deflate path:** `VIBE_zlib_deflate`(0x41e310) FSM + strategies (stored/fast/slow),
  `longest_match`, `fill_window`; full `trees.c` (`build_tree`, `gen_bitlen/gen_codes`,
  `scan_tree/send_tree`, `_tr_flush_block`, `compress_block`, bit-buffer prims).
- **gzio:** `VIBE_zlib_gz*` (gzopen/gzread/gzwrite/gzprintf/gzseek/gzclose...).
- Checksums: `VIBE_zlib_adler32`(0x41d660), `VIBE_zlib_crc32`(0x41dba8).

## minizip unzip.c 0.15a (0x42646c–0x42762a)
ZIP reader (Gilles Vollant), named `VIBE_unz*`: `unzOpen`, `SearchCentralDir` (PK\05\06),
`GoToFirst/NextFile`, `LocateFile`, `unzOpenCurrentFile`→`unzReadCurrentFile` (raw inflate + crc32),
`GetCurrentFileInfo`, plus little-endian `unz_getByte/Short/Long`. Consumed by the VFS `.BIN5` mount.

## VFS (0x41becc–0x41d594, 0x419a3c–0x41be58)
Virtual filesystem layering loose files + `.BIN5` ZIP archives into a sorted directory-node tree.
`VIBE_vfs_Init/Shutdown/ScanDirectory/MountZip/AddFile/FinalizeDir(sort)/FindFileInTree(bsearch)`,
stream ops `VIBE_vfs_Open/Close/fseek/ftell/fgetc/fgets/fwrite`, mem stream `VIBE_vfs_OpenMemory`,
cursor `VIBE_ReadStreamBytes`(0x41caa4 — the byte source under `VIBE_ReadStateField`).

## Memory (0x4168f0–0x416ff4)
Allocation tracker with leak detection (`VIBE_MemTrackInit/Shutdown/DumpStats`, tagged blocks) and a
separate fixed-size pool allocator family "tp_m_pool" (`VIBE_Pool*`). zlib uses thin
`VIBE_zlib_zcalloc/zcfree` over CRT `nmalloc/nfree`.

## Text/config compiler (0x4171d4–0x41a040)
`VIBE_CompileTextFiles`(0x417cf4, txt_OpenText, ~7.5 KB): tokenizer for the game's text-definition
files (`//`/`/* */` comments, `"..."` strings with `|` variant separators, `{rN}` random tokens,
`(...)` gender/variant entries, `_label:` defs) → in-memory string table → emits compiled `.res`
resources + a `#define` index header (f3_textindex.h). Runtime label lookup `VIBE_FindTextLabel*`,
load/save `VIBE_ReloadTextFile`(txt_ReloadTextFile)/`VIBE_SaveTextFile`.

## Logging / error (0x415010, 0x4161xx, 0x415ffc)
Sink-bitmask logger (`byte_43C690` selects console/file/MessageBox/timestamp). `VIBE_LogInit`(0x415ffc),
`VIBE_LogError`(0x4163a4), `VIBE_LogMessage`(0x416310), `VIBE_LogWriteSinks`(0x415010),
`VIBE_LogShutdown`. Crash handler: `TopLevelExceptionFilter` + stack-walker `VIBE_StackFrameEmitLine`,
`VIBE_IsCodeAddressExecutable`, `VIBE_DecodeCallTarget`. Fallback box: "ERROR-Handler has not been started".

## Watcom CRT / DLL init (0x42b000–0x434000)
`DllEntryPoint`(0x42b3e4) → CRT `VIBE_cstart`(__cstart_, 0x42b908) → `VIBE_crt_init_cmdline_env`(0x42b6d8,
thread data + env + GetVersion + argv/wargv) → static-init tables → `_LibMain`/`_DLLMain` user hook.
Plus: per-thread data, x87 control (`VIBE_fnclex`), time lib (`VIBE_gmtime`/`secs_to_tm`/`days_since_1900`),
wide environ (`VIBE_wputenv`...), DBCS classification, 64-bit math helper, and Win32 libc wrappers
(`VIBE_mkdir/remove/rename/access/getpid/fprintf`). The remaining ~406 non-`VIBE_` functions are
Watcom CRT routines IDA had already named (sprintf_, strrchr_, memcpy_, srand_, etc.) — left as-is.
