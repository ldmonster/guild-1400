# Harden: io file_ops3 / file_ops4 (1:1 verification)

Scope: `src/io/file_ops3.cpp`, `src/io/file_ops4.cpp` (+ headers + unit tests).
Method: per function, `decompile` + `disasm` from IDA (module `gilde.exe`,
imagebase 0x400000), diffed line-for-line against the source; every constant
checked; signed/unsigned, fixed-point, float->int, struct offsets, side-effect
order, and `__usercall` register args confirmed against the disasm (disasm wins).

## Test status
All green: `file_ops3_test`, `file_ops4_test`, plus the `*_itest` and `*_e2e_test`
variants (6/6). Built only those targets; build/ untouched otherwise.

## file_ops3.cpp

| addr | name | status |
|------|------|--------|
| 0x5dc850 | VIBE_Bio_ReadByte | VERIFIED-1:1 (ReadStream(a2,1,a1,1)) |
| 0x5dc8b0 | VIBE_Bio_ReadDwordSwapArgs | VERIFIED-1:1 (ReadStream(a2,4,a1,1)) |
| 0x5e3bd0 | VIBE_Script_ReadToken (__usercall al, eax) | VERIFIED-1:1 (EOF->43, >0x3A->39) |
| 0x451698 | VIBE_Vfs_ReadByte (__usercall eax) | VERIFIED-1:1 |
| 0x4516cc | VIBE_Vfs_ReadLine (__usercall eax/edx/ebx) | VERIFIED-1:1 (trailing CR/LF over-read intact) |
| 0x5a75f4 | VIBE_Vfs_ReadStreamBool | VERIFIED-1:1 |
| 0x5f86fc | VIBE_Vfs_FindChunkStart (__usercall eax) | VERIFIED-1:1 |
| 0x5f8570 | VIBE_Vfs_ScanChunkLength (__usercall eax) | VERIFIED-1:1 |
| 0x5f8640 | VIBE_Vfs_CheckChunkFlag (__usercall al) | VERIFIED-1:1 |
| 0x5dc790 | VIBE_Vfs_ReadConfigLine (__usercall eax) | VERIFIED-1:1 (trim/skip/upper; BB1[i]=BB0[i+1]) |
| 0x5dc770 | VIBE_Vfs_FileExists (__usercall al) | VERIFIED-1:1 |
| 0x5fc7b0 | VIBE_Vfs_GetProcessId | VERIFIED-1:1 (OS boundary hook) |
| 0x5eeeb0 | VIBE_Vfs_GetWorkingDir (__usercall eax/edx) | FIXED (errno codes) |

Chunk-scan notes (FindChunkStart/ScanChunkLength): control flow in the binary is
`tok==45 break; tok!=43 -> ReadDword+Seek(len,CUR); tok==43 -> Close+ret`. The
source's reordering (EOF first) is behaviorally identical. Seek prototype recovered
from disasm: `(handle@eax, off@edx, whence@ebx)`. Magic compare is direct unsigned
`cmp …, 0FAB50005h`; sentinel-miss seek offset is `len-4`. The raw dword used as the
seek offset == `(i32)len` reinterpretation — `static_cast<long>(static_cast<i32>(len))`
is correct.

### FIX 1 — GetWorkingDir errno codes (0x5eeeb0)
`VIBE_Runtime_SetErrnoEinval` (0x5fb1c0) is **not** a fixed-EINVAL setter: it takes
the code in **eax** (`mov edx,eax; … mov [errnoblk+4],edx`). GetWorkingDir passes two
different codes:
- 0x5eef0d (dst!=null, len>size): `mov eax,0Eh` -> errno **14**.
- 0x5eeef2 (dst==null, alloc failed): `mov eax,5` -> errno **5**.

The source modelled `setErrnoEinval()` as a no-arg hook, losing the distinction.
Fixed: hook is now `void (*setErrnoEinval)(int code)`; the two sites pass 14 / 5.
Test `ProcessIdAndWorkingDir` extended to assert both codes (added an alloc-fail
leg). Both source and golden updated; cite addr+evidence above.

## file_ops4.cpp

| addr | name | status |
|------|------|--------|
| 0x6063d0 | VIBE_File_DetectDeviceType (__usercall eax) | FIXED (deviceTypeOverride branch) |
| 0x606350 | VIBE_File_LockAndCommit (__usercall eax) | VERIFIED-1:1 |
| 0x5fea04 | VIBE_File_ValidateHandleMode (__usercall eax/edx) | FIXED (errno 6) |
| 0x6063a8 | VIBE_File_SetDescriptorEntry (__usercall eax/edx) | VERIFIED-1:1 (+ boundary note) |
| 0x14278d8 | VIBE_File_Seek_278d8 (__cdecl) | VERIFIED-1:1 (osSeek 4-arg -> boundary) |
| 0x1422b96 | VIBE_File_Write | VERIFIED-1:1 (osWrite 5-arg -> boundary) |
| 0x1425d19 | VIBE_File_ReadTranslate | FIXED (tail-CR non-text v24==10) |
| 0x14296b3 | VIBE_File_ChangeSize | VERIFIED-1:1 |
| 0x1427cc8 | VIBE_File_OpenWithMode | VERIFIED-1:1 (umask/secAttr/osOpen 7-arg -> boundary) |
| 0x142431b | VIBE_File_ParseModeAndOpen | FIXED (signed char compare) |
| 0x5eb830 | VIBE_File_FindFirstEntry (__usercall eax/edx) | FIXED (errno 2) + boundary note |
| 0x5eb940 | VIBE_File_FindClose (__usercall eax) | VERIFIED-1:1 (FindClose-1) |
| 0x5d90c0 | VIBE_Vfs_CloseAndFreeEntry (__usercall eax) | VERIFIED-1:1 |

### FIX 2 — ValidateHandleMode errno (0x5fea04)
At 0x5fea42 `SetErrnoEinval` is entered with **eax==6** on both reaching paths (the
writable-fail `jmp` keeps eax=6; the cmp path only falls through when eax==6). Since
SetErrnoEinval writes errno=eax, errno is set to **6**, not 22. Source set
`Errno()=kEINVAL` (22); fixed to `Errno()=6`. Golden `ValidateHandleModeWriteMismatch`
updated (was expecting 22). The byte0/byte1 access-bit logic and the `0xC0/0x01/0x02`
masks were already correct.

### FIX 3 — DetectDeviceType missing override branch (0x6063d0)
The binary first consults `dword_64AA50` (a function-pointer global, null in a clean
runtime): `if (dword_64AA50 && dword_64AA50(fd)) return 1;` BEFORE GetFileType
(0x6063db..0x6063ee). Source omitted it. Added an optional `deviceTypeOverride(fd)`
hook (defaults to null, matching dword_64AA50==0, so behavior is unchanged in the
default runtime but the branch is now faithful). `off_64A910/914` are null stubs
(0x5f8224); kept as lockEnter/lockLeave.

### FIX 4 — ReadTranslate tail-CR, non-text, v24==10 (0x1425d19)
In the lone-CR-at-buffer-tail handling, non-text branch
(`(flags & 0x48)==0`): after un-reading the look-ahead byte with `Seek(-1,1)`, the
binary does `cmp v24,0Ah; jz LABEL_39` (0x1425ec3) — when v24==10 it writes **nothing**
and does not advance the output cursor (the LF was pushed back by the seek). The
source wrongly wrote an LF (`*v7=10; ++v7`). Fixed to write nothing in that sub-case.
The rest of the (intricate) CRLF/peekChar/^Z translation matches the disasm. osRead
is a 5-arg call in the binary (trailing lpOverlapped=0) -> collapsed at the hook
boundary; the translate-buffer pointer arithmetic itself is 1:1.

### FIX 5 — ParseModeAndOpen signed-char dispatch (0x142431b)
The mode-char classifier uses `movsx eax,al; cmp eax,54h; jg` (0x1424373) — a
**signed** comparison. A mode byte with the high bit set (>=0x80, negative) takes the
`<=84` path, not the `>84` path. Source used `static_cast<u8>(v9) > 84` (unsigned),
which mis-routes such bytes. Fixed to `v9 > 84` (signed char). All other arms
(`v6&0xC000` b/t exclusivity, v18 c/n, v19 R/S, the '+' `v6&~3|2` / `v7&~0x83|0x80`
masks, pmode 0xA4, `dword_145A3B8`=0 base, `dword_145A25C` bump) verified equal.

### Boundary notes (OS/Win32, left as hooks; arithmetic kept 1:1)
- `osSeek` is `SetFilePointer(handle, lo, hi=0, whence)` (4 args) — high dword
  always 0; 3-arg hook is equivalent.
- `osWrite`/`osRead` carry a trailing lpOverlapped=0 (5th arg).
- `osOpen` is CreateFile-style with a `{12,0}` security-attr struct and trailing 0
  (7 args); collapsed to `(path, access, share, disp, attrs)`.
- OpenWithMode readonly-pmode test is `(pmode & ~dword_145A1E4 & 0x80)`; the umask
  `dword_145A1E4`==0 in the runtime, so it reduces to `(pmode & 0x80)`.
- `dword_145A4B0` (_fmode) is 0 in the static image; the source default 0x4000 is
  behaviorally identical for the only test (`!= 0x8000`).
- SetDescriptorEntry (0x6063a8) has NO bounds check and returns eax=4*fd (ignored by
  callers); our fixed-256 LowioTable keeps a defensive guard (never triggers for the
  valid fds the original passes) and returns void. Documented in source.
- FindFirstEntry not-found leg calls MapLastError (errno = GetLastError(), OS value);
  the findFirst hook does not surface the OS error, so EBADF is used as a documented
  deterministic stand-in. The fail-MATCH leg is the recoverable one and was fixed to
  errno 2 (FIX above).
