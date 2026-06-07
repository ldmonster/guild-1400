# MSVC CRT runtime (renamed library functions)

These are standard statically-linked MSVC 6.x C runtime functions. They are not
game/installer logic; they were given canonical CRT names so the IDB is fully
named. **Excluded from further multiplayer analysis** — none touch send/recv
(there is no networking in this module).

## Previously-unnamed CRT internals that were renamed this session

| Addr | Name | What it is |
|---|---|---|
| 0x10001c89 | `free` | MSVC `free` (dispatches by active heap mode @dword_1000C664). |
| 0x10001e00 | `fread` | Locking `fread` wrapper (lock_file → `_fread_nolock`@0x10001e2f → unlock). |
| 0x10001f55 | `_heap_alloc` | Core allocator behind `malloc`/`__nh_malloc`. |
| 0x10002879 | `flushall` | `_flushall` → `flsall(1)`. |
| 0x10002926 | `__get_pe_subsystem_version` | Reads Major/MinorSubsystemVersion from own PE optional header (heap-select helper). |
| 0x10002953 | `__heap_select` | Picks heap impl (1/2/3) from OS version + `__MSVCRT_HEAP_SELECT` env. |
| 0x10002a9b | `_heap_init` | `HeapCreate` + `__heap_select` + init chosen sbh variant. |
| 0x10002af8 | `_heap_term` | Heap teardown (VirtualFree regions + HeapDestroy). |
| 0x10002c13 | `__sbh_free_block_v3` | Small-block-heap (mode 3) free. |
| 0x100033f1 | `__sbh_alloc_new_region_v2` | Mode-2 sbh: reserve+commit a new region. |
| 0x10003535 | `__sbh_free_region_v2` | Mode-2 sbh: release a region. |
| 0x1000358b | `__sbh_decommit_pages_v2` | Mode-2 sbh: decommit free pages (trim). |
| 0x1000364d | `__sbh_find_block_v2` | Mode-2 sbh: locate region/block for a pointer. |
| 0x100036a4 | `__sbh_free_block_v2` | Mode-2 sbh free. |
| 0x100036e9 | `__sbh_alloc_block_v2` | Mode-2 sbh allocate. |
| 0x100038f1 | `__sbh_alloc_from_page_v2` | Mode-2 sbh: carve allocation out of a page. |
| 0x10005be4 | `calloc` | MSVC `calloc` (alloc + zero, heap-mode aware). |

## Already-named CRT (FLIRT) — not re-listed
~140 functions such as `_strchr`, `_strncpy`, `_atoi`, `_malloc`,
`_memcpy`, `_sprintf`, `__output`, `__sbh_alloc_block`, `__CRT_INIT@12`,
`_DllMain@12`, `__except_handler3`, `_parse_cmdline`, `_setargv`, etc. Left
as-is.

## Status
All CRT functions named; nothing actionable for the multiplayer goal here.
