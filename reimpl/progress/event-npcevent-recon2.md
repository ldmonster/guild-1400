# Event / NpcEvent state machines (recon batch 2)

Files:
- `src/sim/event_recon2.h` — pure trigger/step/outcome cores (header-inline, `guild::sim`)
- `src/sim/event_recon2.cpp` — anchor TU + deferred-leaf & wiring documentation
- `tests/unit/event_recon2_test.cpp` — golden vectors (16 tests, 100 checks, all pass)

## Reconstructed (logic cores, 1:1 from Hex-Rays)
| addr | function | core reconstructed |
|------|----------|--------------------|
| 0x4d766c | VIBE_NpcEvent_RunSimDiseases | severity roll, workstation penalty, empty threshold, outbreak gate, ring-walk, run seed, sweep-complete |
| 0x4d66b4 | VIBE_NpcEvent_BardCreateScriptStep | case-0 day schedule (now+3+rand4), flag gates (bit2/bit4), script-finish gate (>=20), demolish gate |
| 0x4d9a60 | VIBE_NpcEvent_BroadcastWinnerPointsStep | advance gate (minute>=0x15), per-official rank kind (6/7 -> 1 else 2) |
| 0x4da978 | VIBE_NpcEvent_OfficeMatchmakingStep | phase classify (12/>5/step), result text id, promotion gate, vote-penalty magnitude, notify text id |
| 0x4ef900 | VIBE_Event_InitBetriebRun | state classify (-2/-1/0/positive + packet-ready), founding fee constant 3000 |
| 0x4f2a80 | VIBE_Event_AllocProduktion | favorability multiplier (favor-0.5)*0.25 |
| 0x4f2530 | VIBE_Event_AllocWorkActorAction | favorability multiplier (favor-0.5)*0.25 |
| 0x4f1d48 | VIBE_Event_OpenBuildingDialogRun | hour gate (<22), take-object rand(4) gate, minute bump |
| 0x4f1a00 | VIBE_Event_OpenHelpEventsForKind | group-code -> tutorial INI name mapping |
| 0x4f41bc | VIBE_Event_AllocSlotResetAction | duplicate-handler dedup gate |
| 0x4f5110 | VIBE_Event_AllocGebaeudeBauen | foreign-build dedup gate (state<5) |

Recovered constants: `kDiseaseStrideTable[16]` (dword_4C9768), float pool 0x61ef3c
(100/20/0.5/0.02/0.0125/25), favorability pools 0x620138/0x6200d8 (-0.5, 0.25).

## Deferred coupled leaves (rule 8 — documented, not faked)
- 0x4f1850 VIBE_Event_OpenHelpEventsFromIni — INI file parser + MessageBoxA (I/O+UI). Selection logic of its caller IS reconstructed.
- 0x4f58d4 VIBE_Event_SpawnBuildEffectByName — scene object-group spawn (scene/Vulkan).
- 0x58c92c VIBE_Event_BroadcastFamilyNews — text-message builder + command/delta packets (UI/world leaf; no numeric core).
- 0x4f1ed0 VIBE_Event_RegisterHandlerTable — handler-pointer registrar (dispatch wiring; belongs to He module).
- World/UI emitters: HandleAccidentRandom 0x4c9ac4, He_SendEntityMessage 0x4c5c54, Command_QueueRequest*, Building_PickRandomDiseaseEvent 0x58aaf4, StatChart_BuildOfficialComparison 0x58beb8, Office_CollectByCategory 0x47f03c.

## Wiring (xrefs_to)
- NpcEvent steps installed by VIBE_CharAction_RegisterHandlerTable @0x4db940 (data refs at 0x4dbe56/0x4dbeaa/0x4dbf36/0x4dbf8a).
- Event handlers installed by VIBE_Event_RegisterHandlerTable @0x4f1ed0 (types 0x6E InitBetrieb, 0x83 OpenHelp, 0x85 OpenBuildingDialog), invoked from VIBE_He_InitHandlerTable @0x4c5248.
- BroadcastFamilyNews called from VIBE_Event_ConversationSinkRun @0x4efd88.

## ODR
All 11 target symbols grep-clean before adding; no existing event/npcevent recon files in `src/sim` or `src/world`. No redefinitions.
