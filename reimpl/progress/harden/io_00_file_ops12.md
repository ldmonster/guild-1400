# Hardening: io/file_ops.cpp + io/file_ops2.cpp (1:1 verification)

Date: 2026-06-22. Method: per-function `decompile` + `disasm` line-for-line diff
against gilde.exe (imagebase 0x400000), `get_bytes` on every table/constant.

## file_ops.cpp

| Addr | Function | Verdict |
|------|----------|---------|
| 0x6062c0 | MapCreateDisposition | VERIFIED-1:1 (disp 2→0xC0000000/128, 1→0x40000000/128, else 0x80000000/1; returns disp) |
| 0x6062f4 | MapAccessFlags | VERIFIED-1:1 (group=a1&0x70, sub=a1&7; full jb/jbe/jz arm structure matches: 0x00→1(+2 if sub==0), 0x10→0, 0x20→1, 0x30→2, 0x40→3; returns group) |
| 0x5d45a0 | AdjustBufferOffset | VERIFIED-1:1 (bounds `[start-ptr, cnt]`, jg/jl signed; clears 0x10; ptr+=delta, cnt-=delta; returns 0/1) |
| 0x5d45e0 | ResetBuffer | VERIFIED-1:1 (clear 0x10, cnt=0, ptr=base->start) |
| 0x5fe760 | ConvertFileTimeToDos | BOUNDARY — original is a thin wrapper over Win32 `FileTimeToLocalFileTime`+`FileTimeToDosDateTime`. Source reimplements the documented FAT bit-packing math directly: `date=((y-1980)<<9)|(m<<5)|d`, `time=(h<<11)|(min<<5)|(s>>1)`, year range [1980,2107]. Math is correct per the documented Win32 semantics; the OS time APIs are the boundary. |
| 0x5fe788 | ConvertDosToFileTime | BOUNDARY — same: original wraps `DosDateTimeToFileTime`+`LocalFileTimeToFileTime`; source unpacks the FAT bits 1:1 (y=((d>>9)&0x7F)+1980, mo=(d>>5)&0xF, day=d&0x1F, h=(t>>11)&0x1F, min=(t>>5)&0x3F, s=(t&0x1F)*2). Correct documented math. |
| 0x5fe7f4 | FindEntryMatches | BOUNDARY (partial) — original is a `do{ if(attrs==0) attrs=128; if(mask&attrs) return 1; }while(FindNextFileA(...))` loop. Source implements the single-entry test (attrs==0→128, `(mask&attrs)!=0`) faithfully; the FindNextFileA enumeration loop is the OS dir-iteration boundary (handled by the host VFS layer). |
| 0x5eb894 | CopyFindDataAttributes | DIVERGENCE (data-shape boundary) — original masks individual Win32 attr bits (0x20/0x10/0x02/0x01/0x04) from a source dword, converts THREE FILETIMEs via VIBE_File_FileTimeToUnix (0x5fe830) into out+4/+8/+12, copies size from `[src+0x20]`→out+0x10, then a byte-pair name copy. Source maps only `isDir→0x10 else 0x20`, size=0, and stuffs one DOS mtime into all three time slots. This is a simplification driven by the host `shim::DirEntry` (which only carries isDir + one packed time + name); the faithful bit/size/3×FILETIME logic cannot be reproduced without changing DirEntry (out of this file's edit scope). FLAGGED for follow-up. |
| 0x5eb920 | CreateDirectory | N/A here — translated as FileCreateDirectory in file_buffered.cpp (correctly not redefined). |
| 0x606520 | CheckAccess | BOUNDARY — original: GetFileAttributesA; on -1 returns VIBE_File_MapLastError(); on (mode&2)&&(attr&1) returns VIBE_Runtime_SetErrnoReturnError() with EACCES(5); else 0. Source returns -1 (not MapLastError) on missing and skips the read-only check because the host FS doesn't surface the read-only bit. Documented OS boundary. |
| 0x5d911c | NibbleToHexChar | VERIFIED-1:1 (n+0x30; if >0x39 (jle skips) +0x27). Return is int→char; only AL used by callers. |
| 0x5d9128 | BuildTempFileName | VERIFIED-1:1 — pid fold `pid|(pid>>16)` (shr logical), byte-pair tempdir copy, p[0]='t', nibble loop fills p[4..1] (low nibble at p[4]) matching pre-decremented `[edx+1]` write order, idx>>4 is arithmetic (sar) → C++ `int>>4`, p[5]='_', p[6/7]=hex(idx hi/lo nibble), p[8..12]=".tmp\0". GetProcessId/GetTempDir passed as args (boundary). |

## file_ops2.cpp (CRT fd-table, 0x142xxxx region)

| Addr | Function | Verdict |
|------|----------|---------|
| 0x14266b1 | InitIoTable | VERIFIED-1:1 for the modeled path — alloc 256-byte block, init each 8-byte entry (osHandle=-1, flags=0, peekChar=10), bind fds 0..2: flags=0x81, openStd(-10/-11/-12), type==0/2 or h==-1 → flags|=0x40, type==3 → flags|=0x08, occupied → flags|=0x80. The STARTUPINFO inherited-handle probe (dword_14677E0 / loop at 0x1426728) is a startup/OS boundary with no inherited handles in this reconstruction (correctly documented as empty). |
| 0x14276ae | AllocHandle | VERIFIED-1:1 behaviorally — block scan, free slot (flags&1==0)→osHandle=-1, fd = base + ((entryAddr-blockBase)>>3) == base+i (8-byte stride); grow by new 256-byte block on empty pointer (count+=32, return 32*blockIdx); -1 on OOM. The `fd!=-1` guard is always true (fd≥0), so source's missing inner `break` is equivalent. |
| 0x1427743 | SetOsHandle | VERIFIED-1:1 — fd<count & osHandle==-1 → (gated notify) set, return 0; else doserrno=0, errno=9, -1. **FIXED notify gate** (see below). |
| 0x1427834 | GetOsHandle | VERIFIED-1:1 — fd<count (unsigned) & flags&1 → osHandle; else doserrno=0, errno=9, -1. |
| 0x14277ba | FreeHandle | VERIFIED-1:1 — fd<count & flags&1 & osHandle!=-1 → (gated notify) osHandle=-1 (flags intact), 0; else doserrno=0, errno=9, -1. **FIXED notify gate**. |
| 0x1429b12 | LockHandle | VERIFIED-1:1 — fd<count & flags&1; op 0x8000 clears 0x80, 0x4000 sets it, else errno=22/-1; return packing `v6=-(prevSet!=0); LOWORD&=0xC000; +0x8000` (0x4000 if prev set, 0x8000 if not); bad fd → errno=9/-1. |
| 0x1422b3f | Commit | VERIFIED-1:1 — bad fd → errno=9/-1 (doserrno untouched); commit hook ok → 0; else result=doserrno; on result→doserrno=result, errno=9, -1. |
| 0x1424618 | CloseDescriptor | VERIFIED-1:1 — close path short-circuit OR: GetOsHandle==-1 ‖ ((fd==1‖fd==2) & GetOsHandle(2)==GetOsHandle(1)) ‖ closeHandle(h) → osErr=0 else doserrno; FreeHandle; flags=0; osErr==0→0 else MapOsErrorToErrno+(-1). Bad fd → doserrno=0, errno=9, -1. |
| 0x14246f6 | InvokeLockHook | VERIFIED-1:1 — `lockHook && lockHook(fd)`. |
| 0x1427f81 | AllocStreamBuffer | VERIFIED-1:1 — ++count; malloc 0x1000; base=mem; ok→flags|=8,size=4096; OOM→flags|=4,base=&smallBuf(a1+5/+0x14),size=2; ptr=base, cnt=0; return base. |
| 0x14246cb | FreeStreamBuffer | VERIFIED-1:1 — flags&0x83 & flags&8 → free base, `(WORD@+0x0C)&=0xFBF7` (clears 0x408), zero ptr/base/cnt, 0; else return flags. (Source's `base!=smallBuf` free-guard is always true under flags&8, so equivalent; allocator is the std::malloc/Mem_Alloc boundary.) |
| 0x1427871 | MapOsErrorToErrno | VERIFIED-1:1 — doserrno=osError; linear scan of 45-pair table until &table+i*8 >= 0x14552D8; hit→errno=mapped; else [0x13,0x24]→13, [0xBC,0xCA]→8, else 22. |
| 0x1455170 (..0x14552D8) | errno_table | VERIFIED-1:1 via get_bytes — all 45 (os,errno) dword pairs match kErrnoTable exactly (1,22 / 2,2 / … / 0x718(1816),12). Table length 360 bytes = 45*8; end == 0x1455170+0x168 == 0x14552D8. |

## Fixes applied

1. **file_ops2.cpp — notify-gate (`dword_1452BE4`)**: `get_bytes 0x1452BE4` → `02 00 00 00`
   (value 2). The original SetOsHandle (0x1427743) and FreeHandle (0x14277ba) only
   invoke the std-rebind notify hook (dword_146777C) `if (dword_1452BE4 == 1)`. With
   the global == 2 the notify path is **dead** in the shipped binary. The previous
   source fired `hookNotify(...)` unconditionally for fd 0/1/2. Added
   `static const int kStdNotifyMode = 2;` and wrapped both notify blocks in
   `if (kStdNotifyMode == 1)`, exactly mirroring the binary (path never taken).
   Observable behavior was already identical with default (null) hooks; this makes
   the source strictly faithful regardless of installed hooks. Evidence: disasm
   0x1427779 `cmp ds:dword_1452BE4, 1` / 0x14277f2 same; get_bytes 0x1452BE4.

## Boundaries (legit hooks, not 1:1 violations)

- DOS↔FILETIME (0x5fe760/0x5fe788): math reconstructed 1:1; Win32 time APIs swapped
  for the documented bit-packing (FAT date/time math is the verifiable part).
- Dir enumeration (0x5fe7f4 FindNextFileA loop) and read-only attr (0x606520) ride
  on the host VFS, which doesn't surface every Win32 attribute/the read-only bit.
- InitIoTable inherited-handle/STARTUPINFO probe, openStd/getFileType/closeHandle/
  commit/notify, and Mem_Alloc/Mem_Free are installable hooks / allocator boundary.

## Flagged for follow-up (not editable from this file's scope)

- **CopyFindDataAttributes (0x5eb894)** is a simplification, not 1:1: the host
  `shim::DirEntry` lacks the per-bit Win32 attributes, the 3 FILETIMEs, and the file
  size that the original copies. Faithful reconstruction needs DirEntry extended to
  carry those fields (raw attr dword, 3 FILETIMEs, size). Out of scope for
  file_ops.cpp/file_ops2.cpp only.

## Test status

- `file_ops_test` (suite #207): **built + PASSED** (1/1).
- `file_ops2_test` / `*_e2e` targets: **could not link** — pre-existing UNRELATED
  compile error in `src/ai/meister_workstation.cpp:215` (`AffordableSupply` arg
  mismatch) breaks the shared `guild` library. Not in scope (edit restricted to
  io/file_ops*.cpp) and not introduced by this work.
- `src/io/file_ops2.cpp` verified to compile standalone:
  `g++ -std=c++17 -fsyntax-only -Isrc -Iinclude -I. src/io/file_ops2.cpp` → clean.
