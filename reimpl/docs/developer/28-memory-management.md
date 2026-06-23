# 28 — Memory management

This chapter documents how `gilde.exe` allocates and frees heap memory. There are three
distinct layers, stacked:

1. **A custom freelist heap** (`VIBE_Memory_AllocFromFreeList @0x5dbe70` /
   `VIBE_Memory_ReturnToFreeList @0x5dbf60`) backed directly by `VirtualAlloc`. This is the
   real allocator — the engine does **not** route through CRT `malloc`/`free`. It manages
   memory in boundary-tagged blocks within page-sized regions, with a roving free-list
   pointer and best-fit-ish scanning.
2. **A tracked debug heap** layered on top (`VIBE_Memory_AllocDebug @0x438f10` /
   `VIBE_Memory_FreeDebug @0x43923c`). This is the `m_alloc`/`m_free` front the rest of the
   engine actually calls (`AllocDebug` alone has 381 xrefs). It wraps every allocation in an
   8-byte guard envelope, records the call-site string and a per-allocation tracking slot,
   accumulates per-group byte/block statistics, and at shutdown reports leaks and guard
   corruption (`VIBE_Memory_ShutdownTracker @0x439640`).
3. **A linear "stack" mempool** (`VIBE_MemPool_StartupStack @0x44e420` /
   `VIBE_MemPool_ShutdownStack @0x44e544`) — a single fixed-size array of 132-byte records
   carved from the debug heap once at startup.

Plus low-level fill/zero helpers (`VIBE_Memory_FillDword @0x5f8197`,
`VIBE_Memory_FillDwordAlignedThunk @0x5f8160`) that back the inlined `memset`-equivalents
seen all over the binary.

> Platform boundary (Rule 6): the only OS dependency in this subsystem is `VirtualAlloc`
> (`@0x60e738` import) used by `VIBE_Memory_GrowHeapWithVirtualAlloc @0x5fcd08` to grow the
> heap. Everything else — the freelist arithmetic, the boundary tags, the tracker — is pure
> reconstructed engine logic and is portable. The freelist heap is **not** the CRT heap; do
> not substitute `malloc` (Rule 8). See [02 — CRT startup](02-crt-runtime-startup.md) for the
> separate MSVC runtime heap used before application code.

Cross-references: [01 — Entry point](01-entrypoint-and-winmain.md),
[02 — CRT startup](02-crt-runtime-startup.md), [07 — Shutdown](07-shutdown-sequence.md),
[29 — VFS / file I/O](29-vfs-fileio-compression.md).

---

## Layer map

```
engine code (m_alloc("size","group:line"))
  └─ VIBE_Memory_AllocDebug @0x438f10        ← tracked debug heap (guard + tracker + stats)
       └─ VIBE_Memory_AllocFromFreeList @0x5dbe70   ← custom freelist heap
            ├─ VIBE_Memory_HeapAllocBlock @0x5fcab0      (split a free block, set in-use tag)
            ├─ VIBE_Memory_GrowHeapDefault @0x5fcdc4
            │    └─ VIBE_Memory_GrowHeapWithVirtualAlloc @0x5fcd08
            │         ├─ VIBE_Memory_ComputeGrowSize @0x5fcdd8   (round up to page)
            │         ├─ VirtualAlloc(MEM_COMMIT|RESERVE, PAGE_READWRITE)   ← only OS call
            │         └─ VIBE_Memory_RegisterHeapRegion @0x5fcc90  (link region, lay tags)
            └─ off_64A928() / off_64A930()  (heap lock enter/leave hooks)

engine code (m_free(p))
  └─ VIBE_Memory_FreeDebug @0x43923c         ← verify guards, untrack, sub stats
       └─ VIBE_Memory_ReturnToFreeList @0x5dbf60   ← custom freelist heap
            └─ VIBE_Memory_HeapFreeBlock @0x5fcb60      (coalesce with neighbours)
```

Convenience wrappers over the freelist heap:

| Function | Addr | Behaviour |
|---|---|---|
| `VIBE_Memory_AllocZeroed` | `0x608a40` | `calloc`-style: `AllocFromFreeList(n*size)` then zero-fill via `VIBE_Light_SetGrayColorThunk(0, …)` → `FillDwordAlignedThunk` |
| `VIBE_Memory_AllocArray` | `0x5fea9c` | `AllocFromFreeList(count*size)`, no zeroing |
| `VIBE_Memory_AllocFromFreeList_Thunk` | `0x608f00` | bare thunk to `AllocFromFreeList` |
| `VIBE_Memory_FreeWrapper` | `0x5feab4` | `__fastcall` thunk to `ReturnToFreeList` |
| `VIBE_Memory_CopyBlock` | `0x609060` | `qmemcpy(dst,src,n)` (a `memcpy`) |
| `VIBE_Memory_ResizeBlockInPlace` | `0x603b3c` | grow/shrink a freelist block in place (the realloc core) |
| `VIBE_Memory_IsValidPointer` | `0x4391f0` | scan tracker table for a pointer; returns true if tracked (or NULL) |

---

## The custom freelist heap

### Region layout and boundary tags

Memory is committed one region at a time by `VIBE_Memory_GrowHeapWithVirtualAlloc @0x5fcd08`:

- `VIBE_Memory_ComputeGrowSize @0x5fcdd8` rounds the request up: align the payload to 8
  (`(n+11)&~7`), add 60 bytes of region overhead, clamp up to the configured minimum
  (`dword_64AE6C`), then round the whole region up to a `0x1000` (4 KiB) page boundary.
- `VirtualAlloc(NULL, size, MEM_COMMIT|MEM_RESERVE=0x1000, PAGE_READWRITE=0x40)` commits it
  (the **only** OS memory call in the engine).
- `VIBE_Memory_RegisterHeapRegion @0x5fcc90` links the region into a sorted doubly-linked
  list rooted at `dword_64A30C`, initialises its control block, lays the first free chunk
  spanning the region, and writes a sentinel boundary tag of `0xFFFFFFFF` (`-1`) at the end
  so scans terminate.

Within a region, blocks carry **inline boundary tags**. Each chunk begins with a DWORD
size word whose low bit is the **in-use flag**:

```
chunk:  +0x00  size | inuse-bit       (size is 8-aligned, so bit0 is free for the flag)
        +0x04  payload ...            (returned pointer points here)
        ...
        +size  next chunk's size word
```

- Free chunks additionally store two DWORD link pointers at `+0x04`/`+0x08` (prev/next on a
  free list) plus the trailing size used for coalescing.
- `VIBE_Memory_HeapAllocBlock @0x5fcab0` walks free chunks of the region; the requested size
  is `(n+11)&0xF8` clamped to a 16-byte minimum. On a fit, if the remainder is `>= 0x10` it
  **splits** the chunk (carving a new free chunk from the tail); otherwise it consumes the
  whole chunk and unlinks it. It then sets the in-use bit (`*v6 |= 1`) and returns
  `chunk+4`.
- `VIBE_Memory_HeapFreeBlock @0x5fcb60` clears the in-use bit and **coalesces** with the
  adjacent next chunk (and, via the free-list links, the previous one) when they are free,
  updating the region's largest-free-block hints.

The heap keeps roving cursors and a largest-known-free-size cache in globals:
`dword_64A30C` (region list head), `dword_64A310` (current region/roving pointer),
`dword_64A314` (largest free size seen), `dword_1407BA0` (last-freed region hint, used by
`ReturnToFreeList` to locate the owning region quickly). `off_64A928` / `off_64A930` are
indirect lock enter/leave hooks called around every alloc/free for thread safety.
`byte_14090E0` / `byte_14090E1` are "heap dirtied" flags.

### `VIBE_Memory_AllocFromFreeList @0x5dbe70`

```c
if (!n || n > 0xFFFFFFD4) return 0;          // reject 0 and near-overflow
size = (n + 11) & 0xFFFFFFF8;                // 8-align + room for tag
if (size < 0x10) size = 16;                  // 16-byte minimum block
lock();
for each region (best-fit-ish across the roving cursor):
    if region.largest_free >= n && HeapAllocBlock(n, region) succeeds: done;
    else update largest_free cache;
if nothing fit: GrowHeapDefault(n) (VirtualAlloc) and retry, twice;
unlock();
return payload pointer (or 0 on OOM);
```

Note the request `n` (not the rounded `size`) is what `HeapAllocBlock` compares against the
chunk payload, while the rounding governs the chunk's total footprint.

### `VIBE_Memory_ReturnToFreeList @0x5dbf60`

Given a payload pointer, it locates the owning region by checking, in order, the last-freed
hint `dword_1407BA0`, the roving region `dword_64A310`, and finally a linear walk of the
region list `dword_64A30C` (matching `region <= p < region+region.size`). It then calls
`VIBE_Memory_HeapFreeBlock` to clear the tag and coalesce, refreshes the largest-free cache,
and updates the last-freed hint. A NULL pointer is a no-op.

---

## The tracked debug heap

This is the layer the engine calls everywhere (the `m_alloc` / `m_free` macro pair). It adds
a guard envelope, leak tracking, and per-group statistics on top of the freelist heap.

### Per-allocation guard envelope

`VIBE_Memory_AllocDebug @0x438f10` requests `userSize + 8` from the freelist heap, zeroes
the whole block, then stamps a 4-byte **guard magic `0xDEADBEEF`** (`-559038737`) at both
ends and returns `block + 4`:

```
freelist block:
  +0x00   guard_head  = 0xDEADBEEF          (*(_DWORD*)v36          = -559038737)
  +0x04   user payload[userSize]            ← pointer returned to caller (block+4)
  +0x04+userSize  guard_tail = 0xDEADBEEF   (*(_DWORD*)&block[user+4] = -559038737)
```

So the returned pointer is always 4 bytes past the freelist payload; the freed pointer is
`p - 4`. Guard offsets: head at `[p-4]`, tail at `[p + userSize]`.

`VIBE_Memory_FreeDebug @0x43923c` reverses this:

1. `block = p - 4`. Linear-scan the tracker table (`dword_62D9F4`, stride 16 = 4 DWORDs)
   for the slot whose `+0x08` pointer equals `block`. If not found, it does nothing (untracked).
2. Verify `*block == 0xDEADBEEF`; if not, format `"Memory overwritten (start):\n"`
   (`@0x615b20`) appended with the slot's call-site string and report it.
3. Verify `*(block + slotSize + 4) == 0xDEADBEEF`; if not, report
   `"Memory overwritten (end):\n"` (`@0x615b40`) likewise. (`slotSize` is the tracked user
   size stored in the slot at `+0x0C`.)
4. Clear the slot's pointer (`slot[2] = 0`), `ReturnToFreeList(block)`, then decrement the
   live counters: `dword_62D9E0 -= userSize+8` (live bytes), `--dword_62D9E4` (live blocks),
   and the owning group's live-bytes/live-blocks.

### The tracker table and tracking slot

`VIBE_Memory_InitTracker @0x43937c` allocates the table: `16 * capacity` bytes from the
freelist heap (4 DWORDs per slot), zeroes it, and stores the base in `dword_62D9F4` and the
capacity in `dword_62D9DC`. The capacity is the `a1` argument passed by
`VIBE_App_InitSubsystemsAndMovieDll @0x527de0` (the sole caller).

Each tracker slot is **16 bytes / 4 DWORDs**:

```
slot:  +0x00   group*     pointer to the group-stats record for this allocation
       +0x04   file_line* pointer to the call-site string (e.g. "person.cpp:412")
       +0x08   block*     the freelist block (p-4); 0 == slot free / freed
       +0x0C   userSize   user-requested size (without the 8 guard bytes)
```

On alloc, `AllocDebug` finds the first slot with `block==0`, fills the four fields, writes
the guards, and bumps statistics. The table is fixed-capacity: if it is full
(`dword_62D9E4 >= dword_62D9DC`) the allocation still succeeds and is guarded, but is left
**untracked** (no slot), so it won't be leak-reported.

### Global tracker counters (`@0x62D9DC`…)

| Global | Addr | Meaning |
|---|---|---|
| `dword_62D9DC` | `0x62D9DC` | table capacity (slot count) |
| `dword_62D9E0` | `0x62D9E0` | live bytes (sum of `userSize+8`) |
| `dword_62D9E4` | `0x62D9E4` | live block count |
| `dword_62D9E8` | `0x62D9E8` | peak live bytes (high-water) |
| `dword_62D9EC` | `0x62D9EC` | peak live block count (high-water) |
| `dword_62D9F0` | `0x62D9F0` | "verbose stats" flag (gates `LogGroupStats`) |
| `dword_62D9F4` | `0x62D9F4` | tracker table base pointer |

All are zeroed at init and again at shutdown.

### Group statistics records (`unk_764840` list)

Each allocation is attributed to a **group** keyed by the prefix of the call-site string up
to the first `':'`. `unk_764840 @0x764840` is the head of a singly-linked list of group
records; the first/default group is named `"_main_:"` (`aMain_0 @0x615b5c`), copied in by
`InitTracker`. A group record is **40 bytes / 10 DWORDs**:

```
group: +0x00 (16 bytes) name string (the "group:" prefix, NUL-padded)
       +0x10  live_bytes
       +0x14  live_blocks
       +0x18  peak_bytes      (max live_bytes)
       +0x1C  peak_blocks     (max live_blocks)
       +0x20  call_count      (total allocs ever attributed to this group)
       +0x24  next*           link to next group record (0 at tail)
```

(Indices as DWORDs: `[4]`=live_bytes, `[5]`=live_blocks, `[6]`=peak_bytes,
`[7]`=peak_blocks, `[8]`=call_count, `[9]`=next.) In `AllocDebug`, the call-site prefix
(checked `< 16` chars, else `"m_alloc_debug: GroupInformation too long"` is logged) is
matched against existing groups via `VIBE_Util_StrCmp`; on a miss a new 40-byte record is
allocated from the freelist heap, name-copied, zeroed, and linked. Then `call_count++`,
`live_bytes += userSize`, `live_blocks++`, with peaks updated.

`VIBE_Memory_LogGroupStats @0x43941c` walks this list to emit
`"[*] Memory-Subsystem-Information:"` plus per-group lines
(`"[%s] w/ %i (%ik) bytes in %i block(s) (called %i times)."`).

### Leak report at shutdown — `VIBE_Memory_ShutdownTracker @0x439640`

Called from the shutdown sequence (see [07 — Shutdown](07-shutdown-sequence.md)):

1. If anything is still live (`live_blocks || live_bytes || verbose`), format and log
   `"[*] Memory-Shutdown-Anomaly: [*]\nUsed %i blocks in %i bytes.\n\n%i block(s) left in %i bytes."`
   (`@0x615cc0`) via `VIBE_ErrorLog_ReportMessage`, using peak and current counters.
2. If verbose, dump per-group stats (`VIBE_Memory_LogGroupStats(0)`).
3. Walk every tracker slot; for each with a non-NULL `block*`, log
   `"-> Pointer not freed:%s"` (`@0x615d1c`) with the slot's call-site string, free the
   leaked block back to the freelist, and clear the slot.
4. Zero all counters, free the chain of group records (rooted via `dword_764864`), and free
   the tracker table itself (`dword_62D9F4`).

So leaks are detected purely from tracker slots that were never zeroed by a matching
`FreeDebug`, and each leak is named by the file:line string captured at allocation time.

---

## The stack mempool

`VIBE_MemPool_StartupStack @0x44e420` and `VIBE_MemPool_ShutdownStack @0x44e544` implement a
small fixed-size pool of uniform records, allocated once from the tracked debug heap under
the group `"tr_startup"` (`aTrStartup @0x618e70`).

### Block layout

Each record is **132 bytes (0x84)**. The pool is a flat array of `count` records:

```
pool base = AllocDebug(132 * count, "tr_startup")     → dword_62EB5C (= base = current top)
pool end  = base + 132 * count                        → dword_62EB60
handle    = AllocDebug return                         → dword_62EB64
```

| Global | Addr | Meaning |
|---|---|---|
| `dword_62EB64` | `0x62EB64` | pool handle / allocation base (also the "already-initialised" guard) |
| `dword_62EB5C` | `0x62EB5C` | pool base (start of record array; the linear cursor origin) |
| `dword_62EB60` | `0x62EB60` | pool end (base + 132*count) |
| `off_62EB68` / `off_62EB6C` | `0x62EB68` / `0x62EB6C` | two 13-char label strings copied into each record's header |

Startup builds a 132-byte **template record** on the stack: it is zeroed, then
`VIBE_Util_StrNCopyPad` copies a 13-char label into the head and another 13-char label at
`+0x74`. The template is then `qmemcpy`'d into **every** slot of the array (the loop from
`base` to `end`, stride 132). So all records start identical. This is a classic linear /
"stack" allocator: records are handed out by advancing a cursor through the contiguous array
(grows toward `end`); the whole pool is reclaimed in one shot.

Return codes from startup: `-1` (count==0), `-2` (already initialised, `dword_62EB64` set),
`-3` (debug-heap alloc failed), `0` (success).

`VIBE_MemPool_ShutdownStack @0x44e544` frees the pool back to the debug heap with
`VIBE_Memory_FreeDebug(dword_62EB64)` and clears the three pointers. It guards with
`__debugbreak()` if the pool pointer is inconsistent (`handle < base`), i.e. the pool was
corrupted or double-shut-down.

---

## Low-level fill / zero helpers

These back the inlined "memset" sequences the Hex-Rays output shows throughout the binary
(the `(3 * (BYTE)ptr) & 3` head-alignment dance is the MSVC inlined `memset`). The
out-of-line versions:

### `VIBE_Memory_FillDword @0x5f8197`

A DWORD-granular fill: `FillDword(dst, value, dwordCount)`. It first advances byte-by-byte
until `dst` is 32-byte aligned (`((u8)dst & 0x1F)==0`), then fills 8 DWORDs per iteration
(32-byte unrolled stride), then a 4-DWORD tail block, then 0–3 trailing DWORDs. It writes
whole DWORDs (the same `value` in each), returning the final pointer.

### `VIBE_Memory_FillDwordAlignedThunk @0x5f8160`

A `memset`-with-DWORD-pattern entry that handles **byte** counts. Given `(dst, value, n)`:
it writes leading bytes until `dst` is 4-aligned (rotating `value` by 8 each byte via
`__ROR4__` so the byte pattern stays correct), calls `VIBE_Memory_FillDword` for the
`n>>2` whole DWORDs, then writes the 0–3 trailing bytes. This is the engine's true
`memset`/`memcpy`-pattern primitive.

### Zero-fill path for `AllocZeroed`

`VIBE_Memory_AllocZeroed @0x608a40` reaches zeroing via `VIBE_Light_SetGrayColorThunk
@0x5c6af0`, which broadcasts a single byte across all 4 bytes of a DWORD and calls
`FillDwordAlignedThunk(dst, pattern, byteCount)`. With byte `0` this is exactly
`memset(dst, 0, n)` — that is how `calloc`-style allocations are cleared. (The same helper,
called from CRT startup to clear the TLS block, is documented in
[02 — CRT startup](02-crt-runtime-startup.md).)

---

## Lifecycle summary

- **Init:** the freelist heap is lazy — its first region is committed by `VirtualAlloc` on
  the first allocation that can't be satisfied. The tracker is initialised explicitly by
  `VIBE_Memory_InitTracker @0x43937c` (from `VIBE_App_InitSubsystemsAndMovieDll @0x527de0`)
  with a fixed slot capacity. The stack mempool is brought up later by
  `VIBE_MemPool_StartupStack`.
- **Runtime:** all engine allocations go `m_alloc` → `AllocDebug` (guard + track + group
  stats) → `AllocFromFreeList` (split/grow). Frees go `m_free` → `FreeDebug` (guard check +
  untrack) → `ReturnToFreeList` (coalesce).
- **Shutdown:** `VIBE_MemPool_ShutdownStack` frees the pool, then
  `VIBE_Memory_ShutdownTracker @0x439640` logs the anomaly summary, names every un-freed
  pointer by call-site, frees leaked blocks + group records + the table, and zeroes the
  counters. Committed `VirtualAlloc` regions are released separately during teardown (see
  `VIBE_Heap_FreeUnusedRegions @0x606700`). See [07 — Shutdown](07-shutdown-sequence.md).
