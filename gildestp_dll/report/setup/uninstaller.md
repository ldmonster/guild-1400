# VIBE_UninstallGameFiles (0x10001310)

`int __cdecl VIBE_UninstallGameFiles(const char *installDir, int forceFullDelete)`

Invoked via `ViseEntry` action 2 (`force=0`, interactive uninstall) and from
the abort-rollback path action 1 (`force=1`, silent full delete).

All deletions use `SHFileOperationA` with `wFunc = FO_DELETE(3)` and
`fFlags = 0x414` (`FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI` =
1044 decimal).

## Decision
```
sprintf(FileName, "%s\\resources\\gamedata\\saves\\*.*", installDir)
savesExist = (FindFirstFileA(FileName) != INVALID_HANDLE_VALUE)

if (!savesExist || forceFullDelete ||
    MessageBoxA(0, "M…", "Deinstallation von Die Gilde", MB_YESNO|MB_ICONQUESTION) != IDNO(7))
    -> FULL DELETE   (delete entire installDir + shortcut)
else
    -> PARTIAL DELETE (delete sub-dirs, KEEP saves + shortcut removed)
```
The Yes/No prompt is "also delete your saved games?" — **Yes(6)** ⇒ full wipe;
**No(7)** ⇒ keep the `saves` folder, remove everything else.

## FULL DELETE branch (`if`)
- `SHFileOperationA` deletes the whole `installDir` (pFrom = "%s").
- `SHGetSpecialFolderPathA` (desktop) → delete `<desktop>\Die Gilde.lnk`.

## PARTIAL DELETE branch (`else`) — preserves `saves`
Deletes, each via its own `SHFileOperationA`, under `installDir`:

```
Server, msx, movie, data, gfx, text, sfx, forms,
Resources\animations.zip, Resources\objects.zip, Resources\scenes.zip,
Resources\groups.zip, Resources\textures.zip, Resources\scripts.zip,
Resources\gamedata\cities, Resources\gamedata\network
```
Then deletes `<desktop>\Die Gilde.lnk`. Returns 1.

> Note: `Resources\gamedata\network` here is **installed game data on disk**
> (multiplayer content of the game), being removed by the uninstaller. It is the
> only "network" reference in the binary and is not networking *code*.

## Strings used (.rdata/.data)
`%s\resources\gamedata\saves\*.*`, `Deinstallation von Die Gilde`, `%s`,
`%s\Die Gilde.lnk`, `%s\Server`, `%s\msx`, `%s\movie`, `%s\data`, `%s\gfx`,
`%s\text`, `%s\sfx`, `%s\forms`, `%s\Resources\animations.zip`,
`…\objects.zip`, `…\scenes.zip`, `…\groups.zip`, `…\textures.zip`,
`…\scripts.zip`, `%s\Resources\gamedata\cities`,
`%s\Resources\gamedata\network`.

## Status
Fully resolved.
