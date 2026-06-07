# Game Tick & Data Model (VERIFIED)

## Per-tick simulation — `VIBE_GameTick` (0x414c6c)
Called every ~30 ms by `VIBE_ServerThreadMain`. It deserializes the authoritative world state
(pushed from the host gilde.exe via the LoadStart/LoadChunk transfer into `VIBE_g_LoadBuffer`),
runs the simulation read-back, and relinks the world graph. Pipeline:

1. **`VIBE_vfs_OpenMemory`** (0x41d0b0) wraps the load buffer as a raw-inflate stream (gz magic →
   `inflateInit2(-15)`); `VIBE_ReadStreamBytes` (0x41caa4) is the byte cursor;
   **`VIBE_ReadStateField`** (0x412be0, ~223 callers) is the typed-field primitive `ReadField(dst,size)`.
2. **`VIBE_ReadHeaderState`** (0x412bf4) — alloc `ls:tmp`, read save-format version → gate `dword_43C684`.
3. **`VIBE_ReadWorldObjects`** (0x412f80) — count → `dword_43C380`, placed-object records stride 67.
4. **`VIBE_ReadBuildings`** (0x4130bc) — buildings stride 169 @`dword_C2AF94`; farm type-30 alloc a
   64-plot `f3g:PlantMap`; second stride-96 table.
5. **`VIBE_ReadCityState`** (0x4135ec) — 16 records stride 164 @`unk_BBAB20`.
6. **`VIBE_ReadCharacters`** (0x4139e4) — NPC/character array `word_BC5BB0` stride 268 (≤768).
7. **`VIBE_ReadPlayerState`** (0x41433c) — 5×62 inventory (stride 32) + 4 player records (stride 756,
   nested skill/goods) + STADTAUSWAHL city selection.
8. **`VIBE_SyncWorldObjects`** (0x414a84) — relink entity array; thread every world object into its
   owner/building/global render linked-lists (next-ptr @+63); calls `VIBE_RelinkBuildingTenants` (0x412af8).
   GameTick frees the load buffer and returns.

**Version gates** (compare `dword_43C684`): 0x10013–0x10045 (per-field: 0x10014/15/17/18/20/21/24/28/
2A/2B/2C/31/32/33/36/37/38/39/3B/3D/3E/43). This is the savegame backward-compat ladder.

### Tick query/util helpers (0x4120xx–0x412Bxx)
Object capacity/weight/value and spatial/relation queries used by handlers & tick:
`VIBE_GetObjectStackCapacity/FreeCapacity/ContainerRemainingSpace`, `VIBE_SumEntityObjectValue`,
`VIBE_IsEntityOverloaded`, `VIBE_CountEntitiesInRadius`, `VIBE_FindNearestEntityByMetric`,
`VIBE_GetEntityRelationMatrixEntry` (relation matrix `dword_A9A3DD`), `VIBE_MapBuildingTypeToCategory`,
`VIBE_GetBuildingListByCategory`, etc.

## Data model (record layouts, confirmed by `VIBE_LoadDataModel` 0x4094f8)
| Table | Base global | Stride | Count | Notes |
|-------|-------------|--------|-------|-------|
| Building prototypes (GebaeudePROT) | dword_C2AF90 | 589 | 72 | byte0=type→category 1..8; +35w=obj-slot list head; +583=max-level; +585=value scale |
| Building/actor instances | word_BC5BB0 | 536 (268w) | 768 | word0==0xFFFF=free; +4=id; +37=head city; +39=linked city; +88=family lvl; +92=family[8]; +93=owner; +63=next-link |
| Building instances (alt view) | dword_C2AF94 | 169 | 256 | byte0=valid; +1=owner key |
| Object prototypes (ObjektePROT) | dword_C2AF9C | 65 | — | name@+1 |
| Object instances (ObjekteINST) | dword_C2AFA8 | 67 | 8192 | type@+0; id@+2; qty(Menge)@+14; child-list@+20; next@+63 |
| Offices (Aemter) | byte_4426D8 / dword_C2AF94 | 24 / 169 | 37 / 256 | election/rank/holder |
| Cities (Stadt) | dword_BC5D20 / word_BBAAA0 | 134dw / 164 | — / 16 | building head list / plot records |
| Market goods/prices | dword_BBC060 | 1988 (62×32) | — | supply/demand pricing |

### Object/building lifecycle
- Lookup engine: `VIBE_QueryObjectField`(0x40A928, ~110 callers) / `VIBE_QueryBuildingField`(0x40B8D4)
  consume typed tag/value criteria into a global block, then iterate via
  `VIBE_ObjectQueryFindFirst/Next`, `VIBE_BuildingQueryFindNext`. `VIBE_ResolveEntityId`(0x409A44)
  maps a packet entity-id → (building|object|container, ptr) before any mutation.
- Allocators do linear first-free scans over fixed arrays (no free-list): `VIBE_AllocObjectSlot`,
  `VIBE_AllocBuildingSlot`, `VIBE_AllocBuildingInstance`(0x40ee0c, 4.6 KB), `VIBE_AllocObjectInstance`,
  `VIBE_CreateObjectRecord`, `VIBE_CreateBuildingRecord`. Monotonic id from `dword_43C350`; live count
  `dword_43C380`/`dword_43A1E4`. Destruction recurses the +20/+63 child links and zeroes type bytes.
- Quantity ops: `VIBE_AddObjectQuantity`(gm_AddObjekt), `VIBE_RemoveObjectQuantity`, `VIBE_AdjustObjectQuantity`.

### Office / city-government subsystem (Aemter, 0x402428–0x403a8c)
888-byte office table (37×24). Lookup, election eligibility (`VIBE_CheckOfficeElectionEligible`,
cmd 02 guard), assignment (`VIBE_TryAssignOfficeElection` cmd 44, `VIBE_AssignOfficeHolder` cmd 45),
swap (`VIBE_SwapOfficeHolders` cmd 5C), release, ranking/promotion/succession, and save-load
(`VIBE_LoadOfficeTableFromState` = amt_fio_LoadAemter, called by GameTick). Init: `VIBE_InitOfficeTable`(0x4022b4).

### Economy / production / time
- Building production: `VIBE_UpdateBuildingProductionFlows`, `VIBE_RecalcBuildingProductionCost`,
  `VIBE_RunWarehouseRestockTick`, `VIBE_DistributeBuildingResourceDemand`, `VIBE_CalcBuildingOutput/Rate/Efficiency`.
- Market: `VIBE_InitMarketGoodsPrices`, `VIBE_RebuildMarketGoodsAvailability`,
  `VIBE_CalcMarketBaseDemand/Supply/Price`, `VIBE_GetMarketGoodsPriceByProto`.
- Game time: `VIBE_AddGameTime/SetGameTime/CompareGameTime/GameTimeDiffMinutes/ConvertGameDateToSystemTime`.
- Map/parcel spawning: `VIBE_FindBuildableParcel`, `VIBE_SpawnRandomBuildingOnParcel`, `VIBE_AssignClanColorToBuilding`.
- RNG: Park–Miller + Fisher–Yates cluster `VIBE_RandUnitInterval/RandMod/ShuffleByteArray/...` (0x40e1c8+).

## Client/server & client/client model
Strict **authoritative client-server**. Clients send game commands (opcodes); the server is the only
authority, mutates state, and **broadcasts** results to every client. There is **no peer-to-peer**
traffic — all "client-client" effects are mediated by server broadcast. The host process (gilde.exe)
shares memory with the DLL and drives PostAuth→Init and the world-state push (LoadStart/LoadChunk).
