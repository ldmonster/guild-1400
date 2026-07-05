# HARDEN mem_00 — allocator / heap subsystem (1:1 verification vs gilde.exe)

Chunk files: `src/mem/heap.cpp`, `src/mem/memory_debug.cpp`, `src/mem/mempool.cpp`,
`src/mem/small_heap.cpp`. Evidence: IDA MCP decompile/disasm/get_int for every
`// gilde.exe 0x…` function and every `@0x…` constant. All four files share a
documented, self-consistent **64-bit link widening** (pointers 4→8B, chunk
overhead / stride / header sizes grow accordingly); the widening is faithful and
NOT re-flagged below unless a constant transcribed wrong.

## heap.cpp — VIBE_Memory_* boundary-tag free-list heap
| fn | addr | verdict |
|----|------|---------|
| ComputeGrowSize | 0x5fcdd8 | VERIFIED-1:1 — `(v+11)&0xFFFFFFF8`, `+60`, `minGrow&~1`, `+4095`, page mask `0xFFFFF000`. Binary uses `LOBYTE&0xF8`/`&0xFE`/`LOWORD&0xF000` (low-byte/word masks) = reimpl's full-dword masks. kPageSize 4096 confirmed. |
| GrowHeap | 0x5fcd08 | VERIFIED-1:1 — VirtualAlloc→PageAlloc, `grow-4` region size, min-fit threshold (`0x38` orig → `RunOffset+kMinChunk` widened), set run in-use + allocCount=1 + ReturnToFreeList. `dword_64AE68`/`dword_64A960==-2` init/disable globals modeled as `m_cfg` gates (encapsulation). |
| RegisterHeapRegion | 0x5fcc90 | VERIFIED-1:1 — address-sorted insert; anchor node fields (dv/dvSize/maxFree/allocCount/freeCount, anchor.bk/fd=anchor); run size = regionSize−RunOffset. Orig stores full regionSize at +0x00 & runSize at run-start word; reimpl stores runSize at +0x00 — equivalent (reimpl never needs full size). Extra `&~7` on runSize is REQUIRED by the widened RunOffset(80) and correct. |
| HeapAllocBlock | 0x5fcab0 | VERIFIED-1:1 (constants) — `need=(size+11)&~7` → widened `+kChunkHdr`; floor 16→32; remainder<16→32 split threshold; in-use bit; user=chunk+hdr. **Documented deviation:** original dv-rover fast-path (search from `a2[3]`, update dv on success) and lazy maxFree hint are omitted; reimpl does pure first-fit-from-anchorFd + exact `RecomputeMaxFree`. Affects *which* equal-fit chunk is chosen internally; not a transcription bug. |
| HeapFreeBlock | 0x5fcb60 | VERIFIED-1:1 (coalesce arithmetic) — forward-coalesce sums sizes, sentinel(-1) stops at run end. **Documented deviation:** original maintains an address-ordered free list w/ a load-factor-directed predecessor search (`allocCount/(freeCount+1)`) + dv/dvSize/maxFree hints; reimpl uses head-insert + linear physical-predecessor scan + exact recompute. Equivalent free-list *membership*; internal order differs. |
| AllocFromFreeList | 0x5dbe70 | VERIFIED-1:1 — `size==0 || size>0xFFFFFFD4` reject; sweep regions, grow twice before fail. (Rover start = documented full-sweep equivalent.) |
| ReturnToFreeList | 0x5dbf60 | VERIFIED-1:1 — last-freed cache then region walk; HeapFreeBlock; update caches. |
| FreeUnusedRegions / UnlinkAndFreeRegion / VirtualFreeRegion | 0x606700 / 0x60679c / 0x60673c | VERIFIED-1:1 — release regions whose single chunk spans the run; VirtualFree→PageFree. |

## memory_debug.cpp — VIBE_Memory_* debug tracker
| fn | addr | verdict |
|----|------|---------|
| AllocDebug | 0x438f10 | **FIXED** — original guards the entire body with `if (result)` (result=userSize) @0x438f1c, so `userSize==0` returns null **without** interning a group / bumping callCount / allocating. Reimpl proceeded to allocate an 8-byte block and return block+4. Added early `if (userSize==0) return nullptr;`. Guard word `0xDEADBEEF` (-559038737), `total=userSize+8`, slot find (block==0), all accounting peaks, guards at block[0] & block[userSize+4], return block+4 — all VERIFIED. |
| FreeDebug | 0x43923c | VERIFIED-1:1 — block=userPtr-4, entry scan, both guard checks (start/end), free slot before ReturnToFreeList, group & global bytes/blocks decrements. Overwrite `ReportMessage` logging modeled as `m_corruptions` counter (documented). |
| IsValidPointer | 0x4391f0 | VERIFIED-1:1 — null→true; entry scan; `i != capacity`. |
| InitTracker | 0x43937c | VERIFIED-1:1 — capacity, zero counters, `16*cap`→`sizeof(entry)*cap` table (widened), memset, seed `_main_:` (string confirmed). Orig keeps `_main_` as static `unk_764840`; reimpl interns it on the heap — equivalent state. |
| ShutdownTracker | 0x439640 | VERIFIED-1:1 — reclaim live blocks, free group list, free table. Orig frees from `_main_->next` (static head not freed); reimpl frees all groups incl. heap `_main_` — each self-consistent, no leak/double-free. |

**Noted (unreachable) deviation:** InternGroup prepends; original appends, keeping
`_main_` as the permanent list head = the default group for a colon-less `info`.
Reimpl's colon-less fallback resolves to the newest group instead. Only observable
for an AllocDebug `info` string with no `:` — no such caller exists in the live tree
(all use `"…:…"`), so unreachable; left unchanged to avoid churn.

## mempool.cpp — VIBE_MemPool_* fixed-block pool
| fn | addr | verdict |
|----|------|---------|
| MemPoolAlloc | 0x439778 | VERIFIED-1:1 — `stride=round_up(bs,4)+4`→widened `round_up(bs,8)+8`; header `stride*count+16`→+24; find non-full matching-stride chunk; strings `"m:pool_alloc(tp_m_pool)"` / `"m:pool_get_free(tp_m_pool)"` confirmed exact per branch; owner-word scan; user=slot+owner; zero payload. |
| MemPoolFree | 0x439880 | VERIFIED-1:1 — owner back-ptr, slot-range bounds `[chunk+hdr, chunk+count*stride+hdr)`, usedCount-- , unlink+FreeDebug when emptied. |
| MemPoolFreeAll | 0x4399b4 | VERIFIED-1:1. |
| MemPoolResetBlocks | 0x4399dc | VERIFIED-1:1 — sum usedCount, clear owner words, keep chunks. |
| MemPoolGetNextUsed | 0x4398f8 | VERIFIED-1:1 — seed remaining=usedCount, skip free slots, advance chunk; user=slot+owner. (Reimpl adds a `idx<count` safety bound = documented.) |
| MemPoolGetFirstUsed | 0x43998c | VERIFIED-1:1. |

## small_heap.cpp — CRT __sbh 3-mode small-block heap (mode 3)
| fn | addr | verdict |
|----|------|---------|
| SmallChunkSize / SmallSizeClass | 0x1424ca5 | VERIFIED-1:1 — binary `(n+23)&0xFFFFFFF0` → widened `(n+31)&~15`; class `(chunk>>4)-1` clamp 63; `kSmallBlockMax=1016` confirmed = `InitSmallBlock(1016)` arg @0x1424886, gate `a1<=dword_146503C`. |
| CreateRegion | 0x1424f88 | VERIFIED-1:1 (geometry) — reserve `0x100000`, header 16836B, 20B region descriptor, uncommitted=-1, bitmaps 0. Table-growth (`+16` cap) modeled as linked list (substitute). |
| CommitGroup | 0x1425039 | VERIFIED-1:1 (constants) — bsr pick highest uncommitted bit; group commit `0x8000`, group stride `516*g+324`, index `<<15`; usable run `28672`=`0x7000`=kGroupRunBytes. **Documented deviation:** original lays 7×4080B per-page chunks threaded into bucket 63; reimpl lays ONE 28672B chunk into its class — self-consistent boundary-tag substitute, verified by Validate(). |
| AllocSmallBlock | 0x1424c7f | VERIFIED-1:1 (arithmetic) — class-mask `0xFFFFFFFF>>cls` over gbm0/gbm1, split into remainder class, in-use bit + footer. Two-level region bitmap scan + header curGroup cache replaced by direct group sweep + `m_cursor` (documented structural). |
| FreeSmallBlock | 0x1424956 | VERIFIED-1:1 (arithmetic) — clear in-use, fwd+bwd coalesce via footer `*(chunk-foot)`, relink by class, bitmap/count maintenance, deferred-decommit cache (`dword_1465030/28`, MEM_DECOMMIT `0x4000`). |
| FindRegion | 0x142492b | VERIFIED-1:1 — `(p-reserve)<0x100000` ownership → committed-run test (per-group reserve substitute). |
| ResizeSmallBlock | 0x1425134 | VERIFIED-1:1 — `(n+23)&~15` request; `v4<=v6` shrink / `v4>v6` grow; grow needs next free & big enough; remainder relinked. Reimpl `rem<kMinChunk(32)` keep-slack is the forced widening consequence (orig floor 16). |

## Fix applied
1. `src/mem/memory_debug.cpp` AllocDebug — added `if (userSize==0) return nullptr;`
   (binary evidence: `if (result)` body guard @0x438f1c). No golden pin changed
   (no test allocates 0 bytes).

## Tests (GUILD_GAME_DIR set; only these targets built/run)
| target | checks | failures |
|--------|--------|----------|
| mem_heap_test | 8599 | 0 |
| mem_small_heap_test | 4512 | 0 |
| mem_e2e_test | 2769546 | 0 |
| mem_small_heap_e2e_test | 66668 | 0 |
