# Game Command Handlers — opcode dispatch (VERIFIED)

Dispatch: `VIBE_DispatchGameCommands` (0x428cfc) drains each client's incoming game queue and,
for command byte `cmd < 0x60`, calls `VIBE_g_CommandHandlerTable[cmd]()` (table @ **0x439b60**,
96 entries). Each handler is `__usercall` with the packet ptr in EAX and a reply-record buffer in
EDX. **Return 0 ⇒ broadcast result to all clients** (target 0xFFFFFFFF); **return ≠0 ⇒ reply
error (cmd=2) to the originator only**. Special: cmd 0x20 with `payload[0x10]==0x0E` is copied to
the per-client state buffer `unk_127D7A0[slot]` instead of broadcasting.

Reply-record layout written by handlers: `[0]=result/ack code (1=ok, 2=neutral/error)`,
`[1]=record-type tag (1=building, 2=object-instance, 3=container/object, 5=map-marker, 8=errcode)`,
`[6]=ptr to affected record`. Result ids land in per-client arrays `dword_127D84C/850/854`
(keyed by `626818*slot`).

## Opcode → handler map (all renamed `VIBE_Cmd_*`)
| op | handler | meaning |
|----|---------|---------|
| 00/03/05/06/07 | VIBE_Cmd_ValidateCommandType (0x4049B4) | validate cmd-type byte; "Invalid Commandtype" on bad |
| 01 | VIBE_Cmd_Ack_01 (0x4049EC) | trivial ack |
| 02 | VIBE_Cmd_CheckGameStarted_02 (0x4046B4) | ok iff election-ready guard passes |
| 04/08/09 | VIBE_Cmd_Ack_04/08/09 | trivial acks |
| 0A | VIBE_Cmd_CreateObjectInstance_0A (0x404A2C) | resolve prototype, alloc instance, copy 0x30 payload |
| 0B | VIBE_Cmd_CreateBuilding_0B (0x404B04) | alloc building via VIBE_AllocBuildingInstance |
| 0C | VIBE_Cmd_CreatePlayer_0C (0x404BD8) | create player/character building + name |
| 0D | VIBE_Cmd_LookupObjectOwner_0D (0x404D3C) | lookup owner, reply ptr |
| 0E | VIBE_Cmd_RemoveBuildingOrObject_0E (0x404D88) | remove building/object by id |
| 0F | VIBE_Cmd_TransferObjectOwnership_0F (0x404E10) | transfer ownership between entities |
| 10 | VIBE_Cmd_SetObjectOwnership_10 (0x404EC8) | set ownership (no-error variant) |
| **11** | **VIBE_Cmd_ExSellObjekt_11 (0x404F70)** | `cm_ExSellObjekt`: move/sell qty between containers |
| 12 | VIBE_Cmd_BuyObject_12 (0x405824) | buy: consume ingredients, credit money/stats |
| 13 | VIBE_Cmd_MoveObjectToContainer_13 (0x405BC0) | relink object between container lists |
| 14 | VIBE_Cmd_DeleteSingleObject_14 (0x405D08) | delete a qty==1 object |
| 15 | VIBE_Cmd_CreateObjectWithData_15 (0x405D98) | alloc object, copy 0x1C payload |
| 16 | VIBE_Cmd_PatchFieldsRelative_16 (0x405E08) | additive 1/2/4-byte field patches |
| 17 | VIBE_Cmd_WriteFieldsAbsolute_17 (0x405F50) | absolute 1/2/4-byte field writes |
| 18 | VIBE_Cmd_AdjustFloatStats_18 (0x406058) | adjust float stat array, clamp [0,1000] |
| 19 | VIBE_Cmd_BitmaskField_19 (0x406168) | clear-mask + OR-value bitfield op |
| 1A | VIBE_Cmd_AddFloatField_1A (0x406228) | add float delta to one field |
| 1B | VIBE_Cmd_AdjustTerrainGrid_1B (0x406290) | adjust terrain/relation grid values |
| 1C | VIBE_Cmd_AllocSeqId_1C (0x406718) | alloc seq id (dword_365B7E8) |
| 1D/1E/1F | VIBE_Cmd_Nop_1D1E1F (0x404A20) | no-op (note: 1E is the time-sync packet built by server thread, not handled here) |
| 20 | VIBE_Cmd_SetClientFlagOrDifficulty_20 (0x406730) | set client slot flag / game difficulty (keepalive path) |
| 21 | VIBE_Cmd_RecalcBuilding_21 (0x4067A0) | recalc building |
| 22 | VIBE_Cmd_AllocSeqId439E60_22 (0x4067C8) | alloc seq id (dword_439E60) |
| 23–26,28–37,3C,3E–43 | VIBE_Cmd_Nop (0x406770) | shared no-op/default stub |
| 27 | VIBE_Cmd_TimestampPing_27 (0x4067E0) | stamp seq + timeGetTime (ping/latency) |
| 2B | VIBE_Cmd_ConsumeBuildingObjects_2B (0x406834) | consume/remove objects from building |
| 2C | VIBE_Cmd_PlaceMapMarker_2C (0x406914) | place player/clan guild-map marker (reply type 5) |
| 2D | VIBE_Cmd_CreateObjectWithPos_2D (0x406A94) | alloc object with position |
| 38 | VIBE_Cmd_DemolishBuilding_38 (0x406B08) | demolish building |
| 39 | VIBE_Cmd_AssignBuildingWorker_39 (0x406BA8) | assign worker, update occupancy |
| **3A** | **VIBE_Cmd_ExSetGebUpgrade_3A (0x406C98)** | `cm_ExSetGebUpgrade`: upgrade building level |
| 3B | VIBE_Cmd_RemoveBuilding_3B (0x406D5C) | remove building + clear grid refs |
| 3D | VIBE_Cmd_SpawnCharacter_3D (0x406DF8) | spawn NPC/character at building |
| 44 | VIBE_Cmd_CheckCond40271C_44 (0x406FE4) | guard office-election-assign else error |
| 45 | VIBE_Cmd_CheckCond402998_45 (0x406FF8) | guard office-holder-assign else error |
| 46/47/48 | VIBE_Cmd_Nop_464748 (0x407008) | no-op |
| 49 | VIBE_Cmd_CreateBuildingVariant_49 (0x40700C) | create building variant + optional object |
| 4A | VIBE_Cmd_RemoveBuildingByProt_4A (0x407140) | remove building by prototype |
| 4B/4D/4E/4F/56 | VIBE_Cmd_Ack_* | trivial acks |
| 4C | VIBE_Cmd_CreateObjectFromBuilding_4C (0x407184) | create object from resolved building |
| 50/51/52/55 | VIBE_Cmd_Reject_* | ack with errorcode=8 |
| 53 | VIBE_Cmd_BuildingSlotOp_53 (0x407274) | FourCC-keyed building actor-slot clear/edit |
| 54 | VIBE_Cmd_BuildingSlotOp2_54 (0x4073C8) | FourCC-keyed slot add/remove |
| 57 | VIBE_Cmd_CheckActorThenAck_57 (0x40760C) | require actor else error, then ack |
| 58/5F | VIBE_Cmd_Nop_58 / _5F | no-op |
| **59** | **VIBE_Cmd_ExCutsceneReady_59 (0x407634)** | `cm_ExCutsceneReady`: mark actors ready for cutscene |
| 5A | VIBE_Cmd_AdjustActorStat404_5A (0x4076C8) | adjust actor stat @+404, clamp [0,50] |
| 5B | VIBE_Cmd_AdjustActorStat433_5B (0x407714) | adjust actor byte @+433, clamp [0,255] |
| 5C | VIBE_Cmd_CheckCond402CBC_5C (0x407784) | guard office-swap else error |
| 5D | VIBE_Cmd_AdjustActorGridStat_5D (0x407798) | adjust actor grid stat byte, clamp [0,252] |
| 5E | VIBE_Cmd_UpsertActorRelation_5E (0x40786C) | upsert actor relation, clamp pct 100 |

## Handler helpers (validation / map-marker cluster, 0x4044D4–0x407FF0)
Pre-validators: `VIBE_ValidatePatchFields_Helper`(0x4044D4), `VIBE_ValidateFieldWrite_Helper`(0x404608),
`VIBE_CheckBuildingFlag2_Helper`(0x40468C), `VIBE_CheckCond402900_Helper`(0x4046CC),
`VIBE_ValidateBuildingSlotOp_Helper`(0x4046DC), `VIBE_ValidateBuildingSlotOp2_Helper`(0x4047A4),
`VIBE_CheckCutsceneBusy_Helper`(0x404954). Guild-map markers:
`VIBE_MapMarker_FindGroupByOwner/FindByCoord/FindInRadius/FindFreePlacement/Upsert/NormalizeLayers`
(0x407950–0x407C14), `VIBE_InitGuildMapDefaults`(0x407C88), `VIBE_GetClanColorPair`(0x407FF0).

## Notes
- The genuinely stateful gameplay handlers cluster in 0x0A–0x2D, 0x38–0x3D, 0x53/54/59/5A/5B/5D/5E
  (object economy, building lifecycle, actor stats, guild map). Many opcodes are pure ack/no-op stubs.
- All handlers go through the shared lookup engine `VIBE_QueryObjectField`/`VIBE_QueryBuildingField`
  + `VIBE_ResolveEntityId` (see [data-model.md](data-model.md)).
- This layer is **server-directed**: clients submit commands; the server validates, mutates
  authoritative state, and broadcasts the result. No client-to-client (peer) traffic exists.
