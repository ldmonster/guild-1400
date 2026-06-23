# 29 — VFS, file I/O & compression

> Reference of record: `gilde.exe` (32-bit x86, imagebase `0x400000`). Everything below is
> reconstructed from the IDA Pro decompilation of the original binary; the binary is the
> source of truth. Addresses are given as `VIBE_Foo @0xADDR`.

This chapter documents the game's **virtual file system** (VFS) — the layer every other
subsystem uses to read assets. It covers three things:

1. The **VFS model**: a single in-memory directory tree built by scanning the game root, and
   the `vfs_*` stream API (open / read / write / seek / close) that resolves a logical path
   to either a *loose file on disk* or *an entry inside a packed archive*.
2. The **archive format**: the packed `.BIN` / `.BIN0`…`.BIN5` / `.sbf` archives are ordinary
   **PKZIP** archives. They are opened through a *minizip*-style reader (`VIBE_Zip_*`) keyed on
   the End-Of-Central-Directory record and the central directory table-of-contents.
3. The **compression codec**: **zlib 1.1.3 DEFLATE** (the binary embeds the literal version
   string `"1.1.3"` and the canonical zlib error strings). ZIP entries use *store* (method 0)
   or *deflate* (method 8); the engine also reads/writes raw **gzip** streams. The codec is
   reconstructed 1:1 per project rule 6 — it is not swapped for a host zlib.

Cross-links: [04 — App init](04-app-init-subsystems.md) (when the VFS is mounted),
[13 — Save/load](13-save-load.md) (writes through the VFS, often gzip), [26 — Mesh & assets](26-mesh-asset-lod.md)
(reads `Objects.BIN` / `Textures.BIN` through it), [30 — Audio](30-audio.md) (streams `.sbf`).

---

## Platform boundary

All low-level byte movement bottoms out in a thin **CRT-style buffered FILE layer**
(`VIBE_File_*`) that calls **kernel32** directly:

| CRT-layer fn | kernel32 import | IAT |
| --- | --- | --- |
| `VIBE_File_OpenWithFlags` `@0x5d435c` | `CreateFileA` | `0x60e60c` |
| `VIBE_File_ReadHandle` `@0x5fb8b0` | `ReadFile` | `0x60e6f4` |
| (write path) | `WriteFile` | `0x60e750` |
| `VIBE_File_FlushAndSeek` `@0x5fb740` | `SetFilePointer` | `0x60e710` |
| `VIBE_Vfs_ScanDirectory` `@0x450234` | `FindFirstFileA` / `FindNextFileA` / `FindClose` | `0x60e640` / `0x60e644` / `0x60e63c` |

In the reimplementation every one of these is reconstructed **1:1** but routed through the
`IFileSystem` shim (rule 4: Win32 → SDL/portable backend). The buffered FILE semantics
(internal read buffer, text-mode CR/LF and Ctrl-Z handling in `VIBE_File_Read`) are part of
the observable behaviour and are reproduced, not approximated. The compression codec is
**not** a platform technology; it is the game's own embedded zlib and is reconstructed in C++.

---

## The VFS model

### Init / shutdown

```
VIBE_Vfs_Init     @0x451f98   build the directory tree from the game root
VIBE_Vfs_Shutdown @0x452004   free the tree, close any leaked streams
```

`VIBE_Vfs_Init(root, caseSensitiveFlag)`:
- stores the case-sensitivity flag in `byte_62EB84` (when **0**, all names are upper-cased on
  scan and on lookup — DOS/Windows-style case-insensitive matching);
- calls `VIBE_Vfs_ScanDirectory(root, 0)` `@0x450234`, storing the resulting root directory
  node in `dword_62EB78`;
- logs `vfs_init: root -> "%s" relative to "%s"` and returns `dword_62EB78 != 0`.

Global state:

| Global | Meaning |
| --- | --- |
| `dword_62EB78` | root directory node of the in-memory VFS tree |
| `dword_62EB80` | memory pool for open *stream* records (`VIBE_MemPool_*`, 320-byte slots) |
| `dword_62EB88` | count of currently-open vfs streams (leak detector at shutdown) |
| `byte_62EB84`  | 0 ⇒ case-insensitive (upper-case everything); non-0 ⇒ case-sensitive |

`VIBE_Vfs_Shutdown(verbose)`:
- frees the node tree via `VIBE_Vfs_FreeNodeTree` `@0x450868`;
- if `dword_62EB88` streams are still open and `verbose`, logs `"%i vfs_file(s) were left open!"`;
- walks the open-stream pool and force-closes each via `VIBE_Vfs_CloseStream`.

### Directory scan — `VIBE_Vfs_ScanDirectory @0x450234`

This is the mount step. It enumerates the on-disk tree with `FindFirstFileA`/`FindNextFileA`
under the root and, for every directory, recurses (building child VFS nodes via
`VIBE_Vfs_GetOrCreateSubDir` `@0x44ebcc`). For every *file* it does two things:

1. **Adds the loose file** to the tree with its DOS date/time, packed into the standard MS-DOS
   timestamp word:
   `((year-1980)<<25) | (month<<21) | (day<<16) | (hour<<11) | (minute<<5) | (second>>1)`
   built from `FileTimeToLocalFileTime` → `FileTimeToSystemTime`, via `VIBE_Vfs_AddFileSorted` `@0x44ed58`.
2. **If the file's extension matches a packed-archive extension**, it descends *into* the
   archive and mounts its entries as virtual files under the same directory node (recursive
   call with the archive flag set). The extension table is at `0x44e8f0`, a 6-byte-stride list:

   ```
   ".BIN5" ".BIN4" ".BIN3" ".BIN2" ".BIN1" ".BIN0" ".BIN"     ; 7 entries, asciiz, stride 6
   ```

   These are the packed archives the game ships (`Objects.BIN`, `Textures.BIN`, the `.BINn`
   split volumes, plus `.sbf` audio banks handled the same way). They are PKZIP containers
   (see below). The index of the matched extension is stored on the VFS node (`node[5]`) so the
   opener can rebuild the archive's on-disk name later.

The scanner normalises path separators (`/` ⇄ `\`) via `VIBE_Path_ConvertBackslashToSlash`
`@0x44eb54` / `VIBE_Path_ConvertSlashToBackslash` `@0x44eb90`, and upper-cases names when
`byte_62EB84 == 0`.

### Path resolution & open — `VIBE_Vfs_OpenFile @0x450bc8`

`VIBE_Vfs_OpenFile(path, mode, outRoot)` is the central `vfs_Tfopen`. It parses the mode
string char-by-char (upper-cased through `VIBE_Util_CharToUpper`) into three booleans:

| mode char | effect | flag var |
| --- | --- | --- |
| `R` | read (default) | `v5` (read-vs-write select) |
| `W` | write | `v5 = 0` |
| `B` | binary | `v76` (binary, **not** text) |
| `T` | text | `v76 = 0` |
| `N` | "no gzip" | `v77 = 0` |
| `Z` | gzip | `v77 = 1` |

Then it resolves the logical path against the VFS tree with `VIBE_Vfs_ResolvePath` `@0x44fa5c`,
rebuilds the full physical path with `VIBE_Vfs_BuildFullPath` `@0x44fbcc`, and branches:

- **Loose file** (node has no archive association, `node[8]==0`): opens the real path.
  - If the `Z` flag is set ⇒ `VIBE_Gzip_OpenPath` `@0x5ebc78` (gzip stream).
  - Else ⇒ `VIBE_File_OpenStream` `@0x5d4488` (plain buffered FILE).
- **Archived file** (node has an archive association): rebuilds the archive's on-disk name by
  appending the matched `.BINn` extension from the table, then:
  - `VIBE_Zip_OpenArchive` `@0x5ea908` opens the ZIP container;
  - if the entry's central-dir offset is known (`node[4]`), seeks straight to it via
    `VIBE_Zip_SetCurrentFilePosition` `@0x5eb7e8`; otherwise it iterates entries
    (`VIBE_Zip_GoToNextFile` `@0x5eaf3c`) comparing the upper-cased name;
  - `VIBE_Zip_OpenCurrentFile` `@0x5eb2d0` opens the matched entry for decompression.
  - Note: opening a **text** file *inside a zip* is explicitly unsupported —
    it logs `"Unsupported Read-Mode in vfs:Open (textfile inside zip)"`.
  - Opening a **gzip** file inside a zip is detected and warned at read time:
    `"vfs: gz-compressed file (\"%s\") in zip found!"`.

A failed open logs `vfs_Tfopen: Could not find "%s"!!` / `vfs_Tfopen: Could write to "%s"!!`.

On success it allocates a **320-byte stream record** from the pool, copies the path into it,
stores the backend handle at `+0x100`, and sets the type/flags byte at **`+0x13C` (offset 316)**.

### The stream record (320 bytes)

The stream record returned by `VIBE_Vfs_OpenFile` and consumed by the read/write/seek/close
API. Offsets below are the ones the API actually touches:

```c
// gilde.exe — vfs stream record, 320 bytes, MemPool slot (dword_62EB80)
struct VfsStream {
    char     path[256];     // +0x000  logical path (copied in on open)
    void    *backend;       // +0x100  FILE* / gzip state / zip archive, by type
    // The next fields are an embedded codec scratch area, reused as a
    // memory-stream cursor OR a deflate/inflate "z_stream"-style state:
    u8      *mem_next;      // +0x100  (alias) memory-stream read/write cursor
    u32      mem_avail;     // +0x104  bytes remaining in memory stream
    u32      mem_total;     // +0x108  total bytes consumed/produced
    u8      *out_next;      // +0x10C  deflate output cursor (write path)
    u32      out_avail;     // +0x110  deflate output space
    u32      out_total;     // +0x114  deflate output total
    // ...
    u32      crc;           // +0x138  running CRC-32 (deflate write / gzip)
    u8       flags;         // +0x13C  TYPE + MODE bits (see table)
};
```

The **`flags` byte at +0x13C** is the discriminator. Bits observed across
`VIBE_Vfs_ReadStream` / `WriteStream` / `Seek` / `CloseStream`:

| bit | mask | meaning |
| --- | --- | --- |
| 0 | `0x01` | **read-only** (set ⇒ opened for reading) |
| 2 | `0x04` | "first read" / gzip-in-zip probe pending |
| 3 | `0x08` | backend is **gzip** stream |
| 4 | `0x10` | backend is a **zip** archive entry |
| 5 | `0x20` | backend is an **in-memory** stream (`VIBE_Vfs_OpenMemoryStream` `@0x451b30`); the embedded codec scratch is used directly |

### Read / write / seek / close

```
VIBE_Vfs_ReadStream  @0x4514ac   (443 xrefs)  size = fread(ptr, elemSize, nElems, stream)
VIBE_Vfs_WriteStream @0x4517a8   (443 xrefs)  fwrite(ptr, elemSize, nElems, stream)
VIBE_Vfs_Seek        @0x4518f0                fseek
VIBE_Vfs_Tell        @0x451aa4                ftell
VIBE_Vfs_CloseStream @0x451354                fclose
```

`VIBE_Vfs_ReadStream(ptr, elemSize, stream, nElems)` (register `__usercall`: `eax=ptr`,
`edx=elemSize`, `ecx=stream`, `ebx=nElems`) computes `want = nElems * elemSize`, validates the
record, and dispatches on the flags byte:

- **`0x20` memory + `0x08` gzip-deflate**: pushes `want` bytes through the embedded inflate
  state (`VIBE_Inflate_Process(record+0x100, 2)`), returns produced byte count.
- **`0x20` memory (plain)**: a hand-unrolled `qmemcpy` copy out of the memory buffer
  (4-byte aligned head/tail), advancing `mem_next` / decrementing `mem_avail`.
- **`0x10` zip**: `VIBE_Zip_ReadCurrentFile` `@0x5eb418` (transparent store/inflate).
  On the *first* read of a zip entry (flag `0x04`) it sniffs the first two bytes against
  `dword_62EB70` / `dword_62EB74` to detect a gzip member packed inside the zip and warns.
- **`0x08` gzip**: `VIBE_Gzip_ReadStream` `@0x5ebfa4`.
- **else**: plain buffered `VIBE_File_Read` `@0x5d4770`.

`VIBE_Vfs_WriteStream` mirrors this (memory/deflate, gzip, plain), and on the **deflate** path
also updates the running CRC at `+0x138` via `VIBE_Crc_Compute` `@0x5eed98`.

`VIBE_Vfs_Seek(stream, offset, whence)`:
- only supported on **read-only** streams (`"Seeking only supported in read-only files"`);
- plain file ⇒ `VIBE_File_Seek` `@0x5d45f8`; gzip ⇒ `VIBE_Gzip_Seek` `@0x5ec488`;
- **zip entries cannot truly seek**: forward seek is emulated by *reading and discarding* the
  skipped bytes into a scratch buffer; a backward seek re-opens the entry from the start and
  re-reads. `SEEK_CUR`/`SEEK_END` inside a zip are rejected
  (`"Unsupported vfs_fseek(inzip,SEEK_CUR/END)"`).

`VIBE_Vfs_CloseStream` flushes the codec (for write-mode deflate it appends the 8-byte gzip
trailer: little-endian CRC-32 then ISIZE), ends the inflate/deflate stream, closes the backend
(`VIBE_Zip_CloseCurrentFile`/`CloseArchive`, `VIBE_Gzip_CloseStream`, or the FILE), decrements
`dword_62EB88`, and frees the 320-byte record back to the pool.

---

## The archive format — PKZIP

The packed archives are **standard PKZIP files** read by a *minizip 1.1*-style API
(`VIBE_Zip_*`). All multi-byte fields are **little-endian**, read one byte at a time by:

```
VIBE_Zip_ReadByte  @0x5ea68c
VIBE_Zip_ReadShort @0x5ea6d0   v = b0 | (b1 << 8)
VIBE_Zip_ReadLong  @0x5ea718   v = b0 | (b1<<8) | (b2<<16) | (b3<<24)
```

### Open & locate the End-Of-Central-Directory

`VIBE_Zip_OpenArchive @0x5ea908`:
1. opens the container as a buffered FILE (`"rb"`);
2. finds the **EOCD** record with `VIBE_Zip_FindEndOfCentralDir` `@0x5ea7ec`: it scans backward
   from EOF in 1 KB windows (up to 64 KB, the max ZIP comment length) looking for the 4-byte
   signature **`50 4B 05 06`** (`"PK\x05\x06"`), i.e. `0x06054b50`;
3. from the EOCD it reads, in order: signature, disk-number, disk-with-CD, entries-on-this-disk,
   **total entries**, **central-directory size**, **central-directory offset**, comment-length.
   It rejects multi-disk archives (entries-on-disk must equal total entries, disk numbers must
   be 0) with error `-103`;
4. stores the central-dir base in a 0x80-byte archive handle (allocated by
   `VIBE_Memory_AllocFromFreeList`) and positions on the first entry via
   `VIBE_Zip_GoToFirstFile` `@0x5eaef0`.

### Central directory entry (the TOC) — `VIBE_Zip_ReadCentralDirEntry @0x5eab48`

Each table-of-contents record begins with the **central-directory signature
`0x02014b50`** (`33639248`, `"PK\x01\x02"`; explicitly checked, else `-103`). The fields read,
in PKZIP order, are:

| offset in record | field |
| --- | --- |
| 0x00 | signature `0x02014b50` |
| 0x04 | version made by |
| 0x06 | version needed |
| 0x08 | general purpose bit flag |
| 0x0A | **compression method** (0 = store, 8 = deflate) |
| 0x0C | DOS mod time+date (decoded by `VIBE_Zip_DecodeDosDateTime` `@0x5eaaf0`) |
| 0x10 | CRC-32 |
| 0x14 | **compressed size** |
| 0x18 | **uncompressed size** |
| 0x1C | **file-name length** |
| 0x1E | extra-field length |
| 0x20 | comment length |
| 0x22 | disk number start |
| 0x24 | internal attributes |
| 0x26 | external attributes |
| 0x2A | **relative offset of local header** |
| 0x2E… | file name, extra field, comment |

`VIBE_Zip_GetCurrentFileInfo` `@0x5eaec8` is a thin wrapper that fills caller buffers with the
50-byte parsed info block plus the name string; `VIBE_Vfs_OpenFile` upper-cases the name and
normalises slashes to match the logical lookup. Iteration is `VIBE_Zip_GoToNextFile @0x5eaf3c`.

### Local file header — `VIBE_Zip_ReadLocalFileHeader @0x5eb090`

Before decompressing, the reader seeks to the entry's local-header offset and parses the
**local file header** (signature `0x04034b50`, `"PK\x03\x04"`): version, flag, method, time,
date, crc, compressed-size, uncompressed-size, name-length, extra-length. The data stream
starts at `local_header_offset + 30 + name_len + extra_len`. The *relative offset within the
file* (`+30+name_len`) is recorded so reads can address the compressed payload.

### Reading an entry — `VIBE_Zip_ReadCurrentFile @0x5eb418`

Holds a 0x4000-byte read buffer. If **method == 0 (store)** it copies bytes straight out (still
updating CRC). If **method == 8 (deflate)** it refills the input buffer from the file and runs
`VIBE_Inflate_Process(state, 2)` until `want` bytes are produced or `Z_STREAM_END`. It verifies
the running CRC with `VIBE_Crc_Compute` against the header CRC. The deflate state is initialised
in `VIBE_Zip_OpenCurrentFile` via `VIBE_Inflate_Init2(state, -15, …)` — **`windowBits = -15`**,
i.e. a *raw* DEFLATE stream with no zlib wrapper (exactly minizip's behaviour for ZIP members).

---

## The compression codec — zlib 1.1.3 DEFLATE

The codec is a faithful, statically-linked **zlib 1.1.3**. Evidence in the binary:
- the embedded version string `"1.1.3"` at `0x62bf44`, passed as the compatibility check into
  `VIBE_Inflate_Init2`;
- the canonical zlib error strings used verbatim by the inflate state machine:
  `"incorrect header check"`, `"unknown compression method"`, `"invalid window size"`,
  `"need dictionary"`, `"incorrect data check"`;
- the exact zlib `inflate` / `inflate_blocks` / `inftrees` structure (state enum, Huffman tree
  build, the LZ77 copy loop).

Per **rule 6**, this is reconstructed 1:1 in C++ — *not* delegated to a host zlib.

### Inflate front-end — `VIBE_Inflate_Process @0x5eca44`

This is zlib's `inflate()`. It is a state machine over the stream record (treated as a
`z_stream`-like struct: `next_in`/`avail_in`/`next_out`, plus an internal-state pointer at
index 7). States (the `switch (*state)`):

| state | name | action |
| --- | --- | --- |
| 0 | METHOD | read CMF byte; method nibble must be `8` (deflate), else `"unknown compression method"`; window size `(CMF>>4)+8` must fit, else `"invalid window size"` |
| 1 | FLAG | read FLG byte; `(CMF*256+FLG) % 31` must be 0, else `"incorrect header check"`; if preset-dict bit set ⇒ DICT states, else ⇒ BLOCKS |
| 2–5 | DICT0..3 | read the 4-byte Adler-32 dictionary id (big-endian) |
| 6 | DICT_WAIT | `"need dictionary"` (returns `Z_NEED_DICT = -2`) |
| 7 | BLOCKS | drive `VIBE_Inflate_BlocksProcess @0x5fec3c` (the actual LZ77/Huffman block decoder) |
| 8–11 | CHECK4..1 | read the trailing 4-byte Adler-32 (big-endian) |
| — | compare | computed vs stored Adler ⇒ `"incorrect data check"` on mismatch |
| 12 | DONE | `Z_STREAM_END = 1` |
| 13 | BAD | `Z_DATA_ERROR = -3` |

`VIBE_Inflate_Init2(state, windowBits, sizeofState, "1.1.3")` `@0x5ec8c4`:
- a **negative `windowBits`** means **raw deflate** (no 2-byte zlib header, no Adler trailer) —
  this is exactly how ZIP members are opened (`-15`). It sets the `nowrap` flag and uses
  `|windowBits|` (must be 8…15);
- a **positive `windowBits`** ⇒ full zlib stream with header + Adler-32 (`VIBE_Zlib_Adler32 @0x5ffaf0`);
- allocates the sliding-window block state via `VIBE_Inflate_BlocksNew @0x5feb7c` with a window
  of `1 << windowBits` bytes (32 KB at `windowBits = 15`).

### The LZ77 / Huffman engine

The real decompression happens under `VIBE_Inflate_BlocksProcess @0x5fec3c`
(`inflate_blocks`) and its codes helper (`inflate_codes` / `inftrees`). This is **standard
DEFLATE** (RFC 1951):

- **Block types** from the 3-bit block header: `00` stored (copy `LEN` bytes, `LEN`/`~LEN`
  checked), `01` fixed Huffman, `10` dynamic Huffman (HLIT/HDIST/HCLEN code-length tree built
  by `inftrees`).
- **Control / literal-length / distance codes**: literals `0–255` are emitted directly;
  length code `256` ends the block; codes `257–285` decode a (length, extra-bits) pair; the
  paired distance code decodes a (distance, extra-bits) pair.
- **LZ window**: a circular **32 KB** sliding window (`1 << windowBits`). A match `(len, dist)`
  copies `len` bytes from `dist` bytes back in the already-produced output, wrapping at the
  window boundary. This is the codec's "LZ window + control bits" detail: distances up to
  32768, lengths 3–258, exactly per RFC 1951.
- Bit input is LSB-first; Huffman trees are walked via the `inftrees`-generated lookup tables.

Inflate is reset between members by `VIBE_Inflate_Reset @0x5ec824` and torn down by
`VIBE_Inflate_End @0x5ec87c`; the write side uses the symmetric `VIBE_Deflate_Process @0x5ed5f0`
/ `VIBE_Deflate_End @0x5ed8b4` (zlib `deflate`).

### gzip wrapper — `VIBE_Gzip_ReadStream @0x5ebfa4`

Loose `.gz` (and `Z`-mode) streams use the gzip member format on top of the same inflate
engine: `VIBE_Gzip_CheckHeader @0x5ebdac` validates the 2-byte magic and skips the optional
fields; data is inflated; the **8-byte gzip trailer** (CRC-32 then ISIZE, both little-endian via
`VIBE_Gzip_GetLong @0x5ec668`) is verified against the running `VIBE_Crc_Compute` CRC. A CRC
mismatch yields `Z_DATA_ERROR (-3)`. The matching write path (`VIBE_Vfs_CloseStream` on a
deflate stream) emits the same 8-byte trailer.

### CRC-32

`VIBE_Crc_Compute @0x5eed98` is the standard zlib `crc32()` (table-driven, polynomial
`0xEDB88320`). It guards every stored/deflated read, every deflated write, and the gzip trailer.

---

## Reimplementation notes

- The whole `VIBE_File_*` kernel32 layer is reconstructed 1:1 behind `IFileSystem`
  (`CreateFileA`/`ReadFile`/`WriteFile`/`SetFilePointer`/`FindFirstFileA`); the buffered-FILE
  text/binary semantics (CR/LF translation, Ctrl-Z EOF in text mode) are observable and kept.
- The PKZIP reader (EOCD scan, central-dir TOC, local header, store/deflate) and the zlib 1.1.3
  inflate/deflate/gzip/crc codec are pure-C++ ports — no host zlib, no third-party dependency
  (rules 6 & 8).
- Golden vectors: a known `.BIN`/`.BIN0` archive's central directory must enumerate identically
  (names, methods, sizes, CRCs, local offsets), and each entry must inflate to a CRC that
  matches its header CRC, byte-for-byte against the original.
