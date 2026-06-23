# Cluster: Cheat codes / Hotkeys / Debug keys

Files: `src/sim/cheat_recon.{h,cpp}`, `tests/unit/cheat_recon_test.cpp`
Namespace: `guild::sim`. Provenance: gilde.exe `VIBE_Cheat_*`, `VIBE_Hotkey_*`,
`VIBE_DebugKey_*`. Tests: 173 checks, 0 failures.

## Done (addr -> symbol -> file)

Cheat handlers (table indices 16..26; dispatched from `VIBE_History_Parse­Command­lineSecondPass`
0x4fd8ac via aFest @0x633938 + funcs_4FDAA5 @0x634414):

| addr | original | reimpl |
|------|----------|--------|
| 0x4fc260 | VIBE_Cheat_QueueWinGame (AUFRUHR) | `Cheat_QueueWinGame` |
| 0x4fc2d0 | VIBE_Cheat_QueueAcquireOffices (GILDENSITZE_BRACH) | `Cheat_QueueAcquireOffices` |
| 0x4fc464 | VIBE_Cheat_ParseSetWeapons (NACHFRAGE) | `Cheat_ParseSetWeapons` (full sign+cat+amount decode) |
| 0x4fc7f0 | VIBE_Cheat_QueueHealAllChars (SOELDNER_PLUENDERN) | `Cheat_QueueHealAllChars` |
| 0x4fc8b0 | VIBE_Cheat_QueueRestoreAllChars (SOELDNER_MARODIEREN) | `Cheat_QueueRestoreAllChars` |
| 0x4fc970 | VIBE_Cheat_ParseSpawnChar (FERNHANDEL_RAUBRITTER) | `Cheat_ParseSpawnChar` (N/S/O/W/A gate) |
| 0x4fca1c | VIBE_Cheat_QueueGiveBuildingsTeam (SPENDE_ANSEHEN) | `Cheat_QueueGiveBuildingsTeam` |
| 0x4fcae4 | VIBE_Cheat_QueueGiveBuildingsAlt (BETEILIGUNG) | `Cheat_QueueGiveBuildingsAlt` |
| 0x4fcbac | VIBE_Cheat_QueueTeleportCharByName (KOMMENTARE) | `Cheat_QueueTeleportCharByName` |
| 0x4fcd80 | VIBE_Cheat_ParseSetGuildLevel (PEST) | `Cheat_ParseSetGuildLevel` |
| 0x4fce2c | VIBE_Cheat_ParseRenameChar (INVENTAR_PLUS) | `Cheat_ParseRenameChar` |
| (loop) 0x4fd8ac | per-token memcmp scan | `Cheat_MatchToken` |

Hotkeys:

| addr | original | reimpl |
|------|----------|--------|
| 0x4fedb0 | VIBE_Hotkey_ClearEntry | `Hotkey_ClearEntry` |
| 0x4fedc0 | VIBE_Hotkey_InitTable | `Hotkey_InitTable` (keys 59..68, entry10=87) |
| 0x4ff590 | VIBE_Hotkey_SaveTable | `Hotkey_SaveTable` |
| 0x4ff634 | VIBE_Hotkey_LoadTable | `Hotkey_LoadTable` |
| 0x4ff7a8 | VIBE_Hotkey_HandleKeyPress | `Hotkey_HandleKeyPress` (full decode; effects via hooks) |

Debug keys:

| addr | original | reimpl |
|------|----------|--------|
| 0x4bfa54 | VIBE_DebugKey_Dispatch | `DebugKey_Dispatch` (3-mode classifier + dword_63C7C4) |
| 0x4bf054 | VIBE_DebugKey_ToggleUpdateFlags | `DebugKey_ToggleUpdateFlags` (10 keys, exact flags+banners) |
| 0x4bed44 | VIBE_DebugKey_HandleSelectionCmds | `DebugKey_ClassifySelection` (key->action id) |
| 0x4bf2a8 | VIBE_DebugKey_HandleActionCmds | `DebugKey_ClassifyAction` (key->action id) |

## Deferred / engine-side (reason)

The per-branch EFFECTS of the four large handlers and the windowed hotkey-assign
UI mutate engine state this cluster cannot reach (command queue
`VIBE_Command_QueueRequestSlotReset28` 0x4948c8 etc., entity arrays @0x12CE910/
0x13C3B50, building/office tables, the form/window system, the global clock).
Per rule 8 these are NOT faked: the cluster reconstructs the faithful
key/string DISPATCH (which input -> which action id / which branch / which flag
flip) exactly; the action ids are delivered through inert-default hooks /
enum results for the caller to route to the real subsystems.

- 0x4ff070 VIBE_Hotkey_OpenAssignWindow — pure UI/form-system flow; surfaced as
  the `openAssignWindow` hook (called on key 88 exactly as the original).
- 0x4feec4 VIBE_Hotkey_ActivateBuilding, 0x4fef54 VIBE_Hotkey_AssignFromSelection,
  0x4fee18 VIBE_Hotkey_ValidateAssignments, 0x4ff954 StoreDefaultEntry,
  0x4ff9ac AssignDefaults — building/object lookup + HUD banner; surfaced as
  hooks (`activateBuilding`, `assignFromSelection`). Decode/iteration order
  reproduced 1:1 in `Hotkey_HandleKeyPress`.
- ParseSetWeapons per-entity price sweep, SpawnChar/GuildLevel/Offices/Rename/
  Teleport command-queue packets — engine-side; decode is faithful, effect is
  the returned CheatAction.

## Cross-module deps / wiring (xrefs_to)

- Cheat handlers are reached only from `VIBE_History_ParseCommandlineSecondPass`
  (0x4fd8ac) through the `funcs_4FDAA5` table (data xref 0x634414). The match
  loop is reconstructed as `Cheat_MatchToken`; wire a future
  commandline/history parser to it.
- `VIBE_Hotkey_HandleKeyPress` (0x4ff7a8) is called from
  `VIBE_GameLogic_RunFrameLoop` (0x4c09a0, at 0x4c127f) — the per-frame input
  pump. Wire `Hotkey_HandleKeyPress` there once the frame loop is reconstructed.
- `VIBE_DebugKey_Dispatch` (0x4bfa54) has no live xref in the current IDB (dev
  hotkey path, reached indirectly); kept faithful, ready to wire to the input
  router.
- Shared globals modeled as `CheatReconState` (byte_67225C lastkey, byte_671D8A
  assign flag, dword_11BC27C frame mode, dword_63C7C4 debug mode) and
  `DebugUpdateFlags` (dword_631E7x / dword_63C8Ex toggle flags).

## ODR

Grepped src/include/tests for every symbol; none pre-existed. The referenced
engine leaves (VIBE_Command_*, VIBE_Light_SetGrayColorThunk, VIBE_Util_ParseInt)
appear only in other clusters' comments, not as definitions; this cluster does
not redefine them (effects routed through hooks; a local atoi-style parse-int
mirrors VIBE_Util_ParseInt for self-containment).
