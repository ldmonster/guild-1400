# Setup flow & entry points

All addresses are imagebase 0x10000000.

## Entry points

| Addr | Name | Kind | Purpose |
|---|---|---|---|
| 0x100023fc | `DllEntryPoint` | DLL entry | Standard MSVC `_DllMainCRTStartup`: runs `__CRT_INIT` then user `DllMain` (`_DllMain@12` @0x10005909 is empty). |
| 0x10001000 | `AskGildeStart` | export #1 | Post-install "launch now?" prompt. |
| 0x10001030 | `CreateShellLink` | export #2 | Creates a `.lnk` desktop shortcut via COM. |
| 0x10001940 | `ViseEntry` | export #3 | VISE installer custom-action dispatcher. |

## Export 1 — AskGildeStart (0x10001000)
```
MessageBoxA(0, "M..."(Text @0x10009030), "Installation abgeschlossen", MB_YESNO)
if result == IDYES(6): return WinExec(lpCmdLine, SW_SHOW)
```
Shown by the installer at the end of setup; `lpCmdLine` is the game executable
to start. Pure launcher, no other logic.

## Export 2 — CreateShellLink (0x10001030)
Parses `cmdLineArgs` for four fields delimited by double-quotes / CSV:
1. `MultiByteStr` — shortcut **description** (later widened, passed to
   `IPersistFile::SetDescription`-style slot via vtbl+24).
2. `Str` — shortcut **target path** (`SetPath` vtbl+80; arg dir trimmed and set
   as **working directory** vtbl+36; also used for `SetIconLocation` vtbl+68 with
   the parsed icon index).
3. `Destination` — **arguments** string (`SetArguments` vtbl+28).
4. `atoi(...)` — **icon index** (`v10`).

COM sequence: `CoInitialize` → `CoCreateInstance(CLSID_ShellLink @0x10008118,
IID_IShellLinkA @0x10008128)` → set path/icon/args/workdir → `QueryInterface`
for `IID_IPersistFile` (@0x10008138) → `IPersistFile::Save(WideCharStr, TRUE)`
(vtbl+24) → `Release` → `CoUninitialize`. Writes "Die Gilde.lnk".

## Export 3 — ViseEntry (0x10001940) — dispatcher
```
switch(action):
  0 -> VIBE_Vise_InitSingleInstanceGuard()              (0x100011f0)
  1 -> VIBE_Vise_CheckAbortedThenUninstall(installDir)  (0x10001230)
  2 -> VIBE_UninstallGameFiles(installDir, force=0)      (0x10001310)
  default -> -1
```

### action 0 — VIBE_Vise_InitSingleInstanceGuard (0x100011f0)
`CreateMutexA(NULL, TRUE, "Die Gilde")`. If `GetLastError()==ERROR_ALREADY_EXISTS
(183)` → MessageBox "Es läuft bereits …" ("Die Gilde Setup") and return -1.
Else return 1. Prevents two setup instances.

### action 1 — VIBE_Vise_CheckAbortedThenUninstall (0x10001230)
- Touches the "Die Gilde" mutex (CreateMutexA then CloseHandle) — keep-alive/
  ensure-exists.
- Reads `<installDir>\setuplog.txt` fully into a heap buffer (fopen "rt",
  fseek/ftell sizing, malloc, `fread`, `free`).
- If the log contains the substring **"Installation Aborted!"** →
  `VIBE_UninstallGameFiles(installDir, force=1)` to roll back the partial
  install. Always returns 1.

### action 2 — VIBE_UninstallGameFiles (0x10001310, force=0)
Full uninstaller — see [uninstaller.md](uninstaller.md).

## Call graph (custom functions only)
```
DllEntryPoint -> __CRT_INIT -> _DllMain (empty)

AskGildeStart -> MessageBoxA, WinExec

CreateShellLink -> CoInitialize/CoCreateInstance/COM vtbl/MultiByteToWideChar/
                   CoUninitialize, strchr/strncpy/atoi/strrchr

ViseEntry
  ├─0─ VIBE_Vise_InitSingleInstanceGuard -> CreateMutexA, GetLastError, MessageBoxA
  ├─1─ VIBE_Vise_CheckAbortedThenUninstall
  │       -> CreateMutexA/CloseHandle, sprintf, fopen, fseek, ftell, malloc,
  │          fread, strstr, free, fclose
  │       -> VIBE_UninstallGameFiles(installDir, 1)
  └─2─ VIBE_UninstallGameFiles
          -> sprintf, FindFirstFileA, MessageBoxA, SHFileOperationA,
             SHGetSpecialFolderPathA
```

## Status
All entry/setup functions resolved. No open items here.
