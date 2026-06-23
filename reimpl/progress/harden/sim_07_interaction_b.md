# Hardening pass — sim interaction (cluster B)

Scope: `src/sim/interaction4.cpp` + `src/sim/interaction_handlers.cpp`.
Method: for every `gilde.exe 0xADDR`-provenance function, decompile + disasm via IDA MCP
and diff line-for-line against the C++ port. Float constants recovered byte-exact with
`get_bytes`. Tests: `interaction4_test`, `sim_interaction_handlers_test` — both green.

Result: **no divergences in any assigned function.** Every assigned function was already
faithful; all verified -> VERIFIED-1:1. Wrong header *comments* noted but not load-bearing.

## Recovered float/double constants (byte-exact, confirmed)
| symbol | addr | bits | value |
|---|---|---|---|
| flt_61A438 | 0x61A438 | 0x3F733333 | 0.94999999 |
| flt_61A448 | 0x61A448 | 0x3F28F5C3 | 0.66000003 |
| flt_61A44C | 0x61A44C | 0xBF000000 | -0.5 |
| flt_61A450 | 0x61A450 | 0x3E800000 | 0.25 |
| flt_61A458 | 0x61A458 | 0x3E800000 | 0.25 |
| flt_61A47C | 0x61A47C | 0x3EA8F5C3 | 0.33000001 |
| flt_61A4C0 | 0x61A4C0 | 0x3EE147AE | 0.44 |
| flt_61A4C4 | 0x61A4C4 | 0x467A0000 | 16000.0 |
| dbl_6253C8 | 0x6253C8 | 0xC034000000000000 | -20.0 |

Note: `interaction4.h`/`interaction4.cpp` comment claiming flt_61A438 == "0.0085" is a
stale *comment*; the actual cpp constant `kFindNearestCashFactor` = 0.94999999f is correct.

## interaction4.cpp — per-function verdicts (14 functions)
| addr | function | verdict |
|---|---|---|
| 0x46B8C8 | EvalGiveGift | VERIFIED-1:1 (armed==1, mode==2, dragKind{1,4,7}, cash scale `0<f<1.0` via `<1065353216`, cash<price -> 0, else 11) |
| 0x46C0C0 | FindNearestRevalidate (kind==4) | VERIFIED-1:1 (`(bits&0x7FFFFFFF)!=0 && budget>=score` -> 15) |
| 0x46C0C0 | FindNearestPick (kind==0 tail) | VERIFIED-1:1 (bestDistShifted>=50 -> actionType 30 else 50; budget=cash*0.95; score!=0 && score<=budget) |
| 0x46C12C | FindNearestThresholdSeed | VERIFIED-1:1 (`mov bx,ax; add ebx,23h` => RandomModulo(0x24)+35, single draw) |
| 0x46BCC0 | RequestSellObject | VERIFIED-1:1 (slot==-1 or mode!=2 -> 0; +16==57 currency path needs entityResolved, emit 57; else emit 11) |
| 0x46E91C | PerformBroadcastSummon | VERIFIED-1:1 (hall+market gate -> 25; kind5 counts types 6/7, cap-check before type check, capped at byte_63CC1D) |
| 0x46C3D0 | EnterBuildingWorthReject | VERIFIED-1:1 (`(worth-room)/room*(-0.5)+0.25 <= (worth-cost)/worth` -> reject) |
| 0x46C9D4 | LeaveBuildingSaleReject | VERIFIED-1:1 (`(sale-cost)/sale >= 0.25` -> reject; flt_61A458) |
| 0x46D1A8 | BuildingActionLeavePreGate | VERIFIED-1:1 (`delta/totalSlots < 0.33` -> fires; flt_61A47C) |
| 0x46D1A8 | BuildingActionRankGate | VERIFIED-1:1 (`level1>cap` -> reject; `gameTimeLo%8 != entityId&7` -> reject; cap = 2+(kind==5)) |
| 0x513168 | Dialog_AttackCommand | VERIFIED-1:1 (CheckActiveCharFlag gate; bar; ShowMessageBox -> emit 108 + voice + clearSel; bar destroy) |
| 0x5180E8 | Dialog_TavernStammtischDispatch | VERIFIED-1:1 (node 301 + He(42,3) gates; activePlayer owns seat -> Leave else Join) |
| 0x518BF0 | Dialog_TavernDarkCornerDispatch | VERIFIED-1:1 (node 300 gate; ownerId==word_63CC5C -> Browse else Buy) |
| 0x5129EC | Dialog_RobberCampCheckAndShow | VERIFIED-1:1 (busy gate -> 0; hasCamp gate -> 0; bar+Panel(1)+bar+clearSel -> 1) |
| 0x512668 | Dialog_RobberCampShowBar | VERIFIED-1:1 (unconditional bar+Panel(1)+bar+clearSel -> 1) |
| 0x515E30 | Dialog_SpionageConfirm | VERIFIED-1:1 (RenderFormattedMessage(4876) inert + RunOfficeOverviewWindow + HUD) |

## interaction_handlers.cpp — per-function verdicts (35 functions)
Eval/stub family:
| addr | function | verdict |
|---|---|---|
| 0x469DA8 | AiMethodStubReturnZero | VERIFIED-1:1 |
| 0x469DCC/E0/F4/0x469E08 | EvalReturnEight/BoolNotArmed/Two/Three | VERIFIED-1:1 |
| 0x46B8B0 | EvalRejectStub | VERIFIED-1:1 (0) |
| 0x46EBFC | EvalRestStub | VERIFIED-1:1 (26) |
| 0x470A18 | EvalActionCode35 | VERIFIED-1:1 (mode==1 && armed==3 -> 35) |
| 0x470BE8 | EvalActionCode36 | VERIFIED-1:1 (mode==1 && armed==4 -> 36) |
| 0x46E8A0 | EvalUseDoor | VERIFIED-1:1 (armed>1 ->0; armed==1 needs mode13/target2; rank>=2 ->0; !(457&0x40) ->0; cash<0 or cash*0.44<16000 ->0; else set ev + 25) |

ContextAction cluster (profession at +358, profession2 +361, rank +13, kind +2,
flags 456/457/458, score36 +36 int->double, statusBits44 +44, dragSource dword[135]==+540):
| addr | function | gate / accept set | leaf | tooltip | verdict |
|---|---|---|---|---|---|
| 0x56EFB0 | AssignTask | assoc rec !player or kind15 | Recruit | 0x18E1 | VERIFIED-1:1 |
| 0x56F0AC | PromoteRank | rank>=3 -> 4 | ShowDialog | 0x1933 | VERIFIED-1:1 |
| 0x56F15C | OpenInventory | kind in {6,5} | ChangeProf|2 | 0x1935 | VERIFIED-1:1 |
| 0x56F1C0 | ToggleFollow | (456&0x40) or (score36>=-20.0 && (statusBits44&~0xF)==0) | Medicus | 0x193A | VERIFIED-1:1 |
| 0x56F234 | AssignGuard | prof==13 reject; He(44) list empty | EvidenceReview | 0x1944 | VERIFIED-1:1 |
| 0x56F2C0 | ToggleFlag457 | (457&1) | Blackmail | 0x1954 | VERIFIED-1:1 |
| 0x56F314 | TrainRank3A | rank==3 (>3->4,<3->1) | ShowDialog | 0x195E | VERIFIED-1:1 |
| 0x56F468 | TrainRank4A | rank==4 (>4->4,<4->1) | ShowDialog | 0x1984 | VERIFIED-1:1 |
| 0x56F670 | TrainRank5A | rank>=5 | ShowDialog | 0x198A | VERIFIED-1:1 |
| 0x56F3C0 | DemoteIfRank3 | rank>=3 | SendBuildCmd | 0x1960 | VERIFIED-1:1 |
| 0x56F410 | EquipIfRank3 | rank>=3 && !(457&4) | SendSimpleCmd | 0x1965 | VERIFIED-1:1 |
| 0x56F7B0 | AssignToSlot | rank>=5; free-slot scan (stride 268 words, cap 768); assoc player | Divorce | 0x198E | VERIFIED-1:1 |
| 0x56F9A4 | ShowDualProfession | prof||prof2 | ShowDialog | 0x1996 | VERIFIED-1:1 |
| 0x56FAF0 | StartWorkTask | prof!=0; busy(457&2)->9; (458&0x20)->1; drag(457&1) | RemoveFromOffice | 0x1998 | VERIFIED-1:1 |
| 0x56FE98 | ProfessionMenu11 | prof==11 | ShowDialog | 0x199F | VERIFIED-1:1 |
| 0x56FF58 | ProfessionMenuGuard | prof in {27,15,21,13} | ShowDialog | 0x19A1 | VERIFIED-1:1 |
| 0x5703F8 | AttackOrSteal | prof {22,25,18} | CharmConfirm | 0x19AD | VERIFIED-1:1 |
| 0x5704D4 | TalkOrSocialize | prof {19,20,24,26} | CounterEspionage | 0x19B6 | VERIFIED-1:1 |
| 0x570A1C | CommandPatrol | prof {15,21,27} | SwapSeats | 0x19C8 | VERIFIED-1:1 |
| 0x570C78 | CommandMultiType | prof {23,19,20,13} | Interrogation | 0x19D6 | VERIFIED-1:1 |
| 0x570D60 | CommandType22Or25 | prof {22,25} | ExpelWorker | 0x19DB | VERIFIED-1:1 |
| 0x570EF8 | CommandType27 | prof {27} | MakePeace | 0x19E3 | VERIFIED-1:1 |
| 0x5707F0 | StartGuildTask | prof==14; busy(457&2)->9; (458&0x40)->1; leaf|2 / |0xA | Embezzlement | 0x19C1 | VERIFIED-1:1 |

Perform* mutators (command sequence abstracted to commandHook(tag,stagedAction)):
| addr | function | verdict |
|---|---|---|
| 0x46C30C | PerformRenovate | VERIFIED-1:1 ("renovieren", returns 15) |
| 0x46E1EC | PerformSabotage | VERIFIED-1:1 (mode==4 + building resolve; "sabotage" staged 25, returns 23) |
| 0x46E7B4 | PerformBeating | VERIFIED-1:1 (mode==7 + person resolve; "pruegel" staged 26, returns 24) |

## Flagged (NOT in assigned address list — left untouched)
- **0x595B98 DispatchPanelEvent** (`interaction4.cpp`): outside this agent's scope. Two
  struct-offset mismatches vs binary found while cross-reading; documented here for the
  owning agent, not fixed:
  1. Non-advanced callback path: binary `inc [edx+0xC]` increments panel dword[3]
     (eventCount); cpp increments `panel->lastTick`. Also the callback **gate** is
     step+0x38 (step dword[14]) while the cpp gates on `step->callback` (also +0x38) — the
     gate field equals the call target here, OK; the counter is the real bug.
  2. Non-advanced dwell branch: binary tests `step+0x40 (byte 64) == 3`; cpp tests
     `step->mode` (step+8). These are distinct step fields.
  Its unit tests pass against the current model. Belongs to whoever owns 0x595B98.

## Build / test
- `cmake --build build --target interaction4_test sim_interaction_handlers_test -j` — clean.
- `ctest -R '^(interaction4_test|sim_interaction_handlers_test)$'` — 2/2 PASS.
