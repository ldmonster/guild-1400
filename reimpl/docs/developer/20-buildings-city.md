# 20 — Buildings, plots & city

> Source of truth: `gilde.exe` (32-bit x86, imagebase `0x400000`). All offsets, strides,
> kind codes and table addresses below are read directly from the IDA Pro decompilation.
> Cross-links: [12 — Session init](12-session-init-worldload.md),
> [13 — Save/load](13-save-load.md), [21 — Economy & office](21-economy-office-stats.md),
> [22 — Script engine](22-script-engine.md), [26 — Mesh](26-mesh-asset-lod.md).

---

## 1. Summary

Die Gilde models the city as three distinct, layered data systems:

1. **Building *types* (`PROT`)** — a static, file-loaded prototype table (`A_Geb.dat`,
   stride **589 bytes**) plus an object/item prototype table (`A_Obj.dat`, stride
   **65 bytes**). These describe *what a tavern is*, its work-objects, its category,
   its variant models. There are exactly **72 building-type prototypes** and **731
   object/item prototypes**.
2. **Building *instances* (`INST`)** — a runtime array of **256 slots**, stride **169
   bytes** (`43264 = 256 * 169` total bytes), holding the per-placed-building state
   (id, owner, level, name, occupant objects, jobs, gold, fire/condition flags).
3. **Bauplätze (build plots)** — scene-graph anchor nodes named `bk_*` with a
   stride-**96** size record (`BauplatzSizeRec`) giving the world-space quad of the
   plot. The construction code rasterizes that quad into the terrain "supermap" and
   walks the scene graph to find free plots near the player.

The construction UI (`BAUEN\GEB_BAUEN`, `BAUEN\STADT_BAUEN`) lets the player pick a
type, drag a ghost mesh over valid plots, confirm a price, and enqueue a network
command (commands → [19](19-commands-netcode.md)) that actually creates the instance.

All five of these subsystems are bootstrapped by `VIBE_World_LoadBuildingAndObjectData`
during session init.

---

## 2. The global tables (set up at load)

`VIBE_World_LoadBuildingAndObjectData @0x5835f8` allocates and fills the four core
globals. It is called once during world load ([12](12-session-init-worldload.md)).

| Global | Addr | Size alloc'd | Meaning |
|---|---|---|---|
| `dword_13CE294` | `0x13CE294` | `0xA5A8` = 42408 | **Building-type PROT table** — 72 records × **589** bytes. Loaded from `f3_gm:` VFS file `A_Geb.dat` via `VIBE_File_Read(stream, …, 0x24D /*=589*/, 72)`. |
| `dword_13CE298` | `0x13CE298` | `0xA900` = 43264 | **Building INSTANCE table** — 256 slots × **169** bytes. Zero-cleared by `VIBE_Light_SetGrayColorThunk(0, 43264, …)` (a memset thunk). |
| `dword_13CE27C` | `0x13CE27C` | `0xB99B` = 47515 | **Object/item PROT table** — 731 records × **65** bytes. Loaded from `A_Obj.dat` via `VIBE_File_Read(…, 0x41 /*=65*/, 731)`. |
| `dword_13CE290` | `0x13CE290` | `0x86000` = 548864 | **Object INSTANCE table** — 548864 / 731 ≈ 750 bytes per object slot. Zero-cleared. |

The PROT record stride **589** and object PROT stride **65** are the two most important
constants in this whole subsystem; nearly every accessor in the building cluster
computes an address as `dword_13CE294 + 589 * typeId` or
`dword_13CE27C + 65 * objId`.

After loading, the function does an in-place fixup pass over each building PROT
(stepping `v19 += 589`, `do … while (v16 != 42408)`): it counts the building's
work-objects (object slots at PROT+35, a `__int16` per slot, top bit `0x80` stripped)
whose object-PROT *kind byte* (`*(objProt+0) == 2`) and whose slot+36 byte is negative,
and adds that count into the PROT's `+33` field. It then calls
`VIBE_World_InitBuildingTypeTable`, `VIBE_Building_ResetAllBuildings`, seeds the RNG
from `timeGetTime()`, and returns 0.

### 2.1 `VIBE_World_InitBuildingTypeTable @0x5833b4`

Builds the per-object **kind→default-prototype** lookup `byte_13CE862[]` (731 entries).
It seeds a small fixed table at `byte_13CEB3C..13CEB4D` (e.g. `[0x09]=47`, `[0x08]=50`,
`[0x0B]=33`, `[0x10]=31`, `[0x07]=41`, `[0x0F]=30`, `[0x11]=32`, `[0x0C]=23`,
`[0x0D]=53`, `[0x0A]=20`), fills `byte_13CE862` defaults with **72** (the "no type"
sentinel = number of building types), then walks all 72 building PROTs, and for each
work-object distributes the object across building types of the same kind. Any object
still `==72` falls back to `byte_13CEB3D[objProt+33]`. This is what maps a raw item
(e.g. "bread") back to the building type that makes it.

---

## 3. The building-type (PROT) record

Stride **589 bytes** (`dword_13CE294 + 589 * typeId`). Fields recovered from accessors:

| Offset | Type | Meaning | Recovered from |
|---|---|---|---|
| `+0x00` | `u8` | **Building kind code** (1..0x1A; see §3.1) | `VIBE_Building_MapTypeToCategory`, `VIBE_Building_IsStorageType`, `VIBE_Building_IsProductionType` all read `*(base+589*id)` |
| `+0x01` | char[] | building name string (game name) | `LoadAndAlignGebaeudeModel` copies from `589*id + base + 1` |
| `+0x21` (`+33`) | `u8` | **work-object count** (incremented at load) | `World_Load` fixup `*(v12+33)`; `InitBuildingTypeTable` reads `obj+33` |
| `+0x22` (`+34`) | `u8`/`u16` | number of work-object slots | `CheckBuildRequirements` loops `*(base+34)` |
| `+0x23` (`+35`) | `u16[64]` | **work-object slot array** — each entry is an object-PROT id, high bit `0x80` = "auto-spawn"; `0xFFFF` = empty terminator | `AllocStorageRoom`, `CreateGebaeude`, `InitBuildingTypeTable` walk `*(WORD*)(base+35+2*i)` |
| `+0x223` (`+547`) | `u8[6]` | **required profession/category codes** (0-terminated list of ≤6) | `CheckBuildRequirements` scans `v4[547]…v4[547+5]` |
| `+0x239` (`+569+…)` | … | model/variant fields | (see model loading) |
| `+0x23D` (`+573`) | `u8` | min floor clamp A | `AllocStorageRoom` `v28[573]` |
| `+0x23E` (`+574`) | `u8` | min group clamp B | `AllocStorageRoom` `v28[574]` |
| `+0x23F` (`+575`) | `u8` | default floor | `AllocStorageRoom` `v28[575]` |
| `+0x243` (`+579`) | `u32` | string-id / price-text id | `LoadAndAlignGebaeudeModel` reads `*(v23+579)` for sale text |

### 3.1 Building **kind codes** (`PROT+0`)

`VIBE_Building_MapTypeToCategory @0x5878b0` switches on the kind byte and yields the UI
**category** (the tab in the build window):

| Kind byte | Category result | Notes |
|---|---|---|
| 1, 3, 6, 0xF | **3** | residential/guild houses (`MapTypeToCategory` → 3); kind 3 = "Stadthaus" tier |
| 2 | **6** | market / Marktstand (kind **2** is the storage/office split — see below) |
| 4, 5, 9 | **8** | |
| 7 | **4** | (e.g. taverns) |
| 8, 0xE, 0x12, 0x14, 0x15, 0x16 | **1** | civic |
| 0xB, 0xC, 0xD | **2** | production workshops |
| 0x13 | **7** | |
| 0x17..0x1A | **5** | |
| default | **0** | |

Two predicates derive directly from the kind byte:

- `VIBE_Building_IsStorageType @0x587f50`: `kind == 10` (0x0A) → it is a warehouse/Lager.
- `VIBE_Building_IsProductionType @0x587f80`: `kind ∈ {11, 12, 13, 16, 28}`
  (0x0B,0x0C,0x0D,0x10,0x1C) → it produces goods.

`VIBE_Building_ClassifyTypeFlag @0x589818` classifies an **object-PROT kind** byte into
a coarse flag (returns 2 for "real" objects, 0 for structural markers); the ranges it
treats as 0 are exactly `{0,1==no? , 0x2A, 0x2E..0x34 except 0x34, 0x35..0x39}` — i.e.
the **storage-room object kind 42 (0x2A)** and the **floor markers** are excluded.

### 3.2 Type lookup helpers

- `VIBE_Building_LookupTypeRecordA @0x589778` — reads a small fixed **6-byte stride**
  table at `byte_649910` (`v4 = &byte_649910[6 * a1]`, valid for `a1 < 76`) and copies
  `{dword, word}` (6 bytes) out into the caller's buffer. This is the **type→variant/cost
  descriptor** table; the raw bytes at `0x649910` begin:
  `00 00 00 00 00 00 | 69 69 BD 93 69 04 | 3F 3F 93 69 3F 03 | …`
  (record 0 is all-zero; record 1 = `69 69 BD 93 / 69 04`, etc.). `…RecordB @0x5897c8`
  is the identical accessor for a sibling table.
- `VIBE_Building_LookupTypeName @0x50c738` — given a kind byte, scans a **stride-44**
  string table starting at `aBkBrunnen` (`bk_BRUNNEN`, the first plot/type name) with a
  parallel id table at `dword_63C515`; 12 ids per 44-byte row, 528 bytes total (12 rows).
  Copies the matched `bk_*` plot base name into the caller buffer (used to build the
  plot-search key and the `.ogr` model path).
- `VIBE_Building_RegisterNames @0x504a54` — for every owned building of an iterated
  person, picks a random localized building name. It seeds from `byte_620EFC`, then for
  each of up to **12** name candidates (`dword_8C4790[14*typeId + k]`) checks the name is
  not already used by scanning the **169-stride** instance table
  (`k < 43264; k += 169`), rejects names ≥ 32 chars ("Building name too long: %s"), and
  finally copies `&v25[64 * RandomModulo(count)]` into the instance's name field at
  `inst+5`. So instance names live at **INST+5** (64-byte cap).

---

## 4. The building **instance** (INST) record

Stride **169 bytes** (`dword_13CE298 + 169 * slot`, 256 slots). Fields recovered from
`VIBE_Building_CreateGebaeude @0x586fb8`, `FindById`, `RegisterNames`, and the dialogs:

| Offset | Type | Meaning | Source |
|---|---|---|---|
| `+0x00` | `u8` | **alive flag / type id** — non-zero = slot occupied; also holds the building's kind/type | `FindById` tests `*(base+v2)`; `Create` writes `*v3 = typeId` |
| `+0x01` (`+1`) | `u32` | **building id** (`GebID`, monotonically increasing from `dword_649890`) | `FindById` compares `*(base+v2+1)`; `Create` writes `*(v3+1)=GebID++` |
| `+0x05` (`+5`) | char[64] | **building name** (chosen by `RegisterNames`) | `Create`/`RegisterNames` copy into `inst+5` |
| `+0x25` (`+37`) | `u16` | **city / region id** (`word_63CC5C` context) | `Create` `*(v3+37)=v57`; dialogs `*(a1+37)` index `dword_12CE914[134*…]` |
| `+0x27` (`+39`) | `u16` | city id (mirror) | `Create` `*(v3+39)` |
| `+0x29` (`+41`) | `u16` | primary occupant object id | `Create` `*(v4+41)` set from storable object |
| `+0x2B` (`+43`) | `u32` | scene/parent node ptr | `Create` `*(v3+43)` |
| `+0x30` (`+48`) | `u32` | gold / cash (initialized 0) | `*((DWORD*)v3+12)` = `+48`; `Create` sets 5000 for production, 80000 for kind 71, 100 for several |
| `+0x39` (`+57`) | `u32` | = 32000 (max stock?) | `Create` |
| `+0x3D` (`+61`) | `u32` | = 100 (condition %) | `Create` |
| `+0x41` (`+65`) | `u32` | = 2 (level?) | `Create` |
| `+0x45` (`+69`) | `u32` | = 100 | `Create` |
| `+0x49` (`+73`) | `f32` | = 1.0 (`1065353216`) production scalar; some types use 0.8 (`1061997773`) | `Create` |
| `+0x5C` (`+92`) | `u8` | = 100 | `Create` |
| `+0x5D` (`+93`) | `u32` | **owner person ptr** | dialogs `*(a1+93)`; `Create` `*(v3+93)` |
| `+0x61` (`+97`) | `u32` | **plot/anchor scene node ptr** | `GetGebaeudeBauplatzPos` tests `*(a2+97)`; `Create` `*(v3+97)` |
| `+0x65` (`+101)…` | varies | type-specific payload (PlantMap ptr for kind 30, recipe slots, etc.) | `Create` per-kind init |
| `+0x71` (`+113`) | `u32` | PlantMap alloc (kind 30 only, `f3_gm:PlantMap`, 0x600 bytes, 24-byte cells) | `Create` |
| `+0x95` (`+149`) | `u32` | = -1 | `Create` |
| `+0xA5` (`+165`) | `u32` | = -1 | `Create` |

`VIBE_Building_FindById @0x587b20` is the canonical instance lookup: linear scan
`v2 += 169` over the 256 slots (`v2 >= 43264 → not found`), matching on `+0` alive and
`+1` id.

`VIBE_Building_CreateGebaeude @0x586fb8` is the per-instance constructor: grabs a free
slot (`FindFreeSlot`), stamps the defaults above, assigns a fresh `GebID`, copies the
type's work-object slot array (`PROT+35`) into spawned objects, allocates storage rooms
via `AllocStorageRoom` for object kinds **2** and **6**, picks a random name, applies
per-kind special cases, then calls `InitWorkerCapacities`, `MapTypeToCategory`, and
`EnsureDefaultObjects`. `VIBE_Building_ResetAllBuildings @0x5896fc` tears down all 256
(`i<768`?, actually `i<768` loop calling `RemoveAndCleanup(i,2)` over the slot index
space) and clears the 16 city-summary rows at `word_13C3110` (82-word stride).

---

## 5. Storage rooms & the special kind codes (42 / 278)

`VIBE_Building_AllocStorageRoom @0x588988` is where the magic numbers from the prompt
live. After spawning the room object (`VIBE_GameObject_AddObjekt`), it:

1. Walks the building-type's work-object slots (`PROT+35`, up to 64), and for each
   object whose object-PROT kind (`*(dword_13CE27C + 65*objId)`) is **2** or **6**,
   stops adding (those are office/market storage themselves); otherwise auto-spawns the
   object into the new room.
2. Finds the **general storage object, kind 42** (`0x2A`):
   `VIBE_GameObject_QueryFind(room, 1, 0, 42)`. If present it sets the object's owner and
   tag = 2 and clamps its **floor/group to ≥ 2** (`*(obj+28) = max(PROT+573, 2)`,
   `*(obj+29) = max(PROT+574, 2)`), or uses `PROT+575` if both clamps are zero.
3. Finds the **market stall storage, kind 278** (`0x116`), **group 6**:
   `VIBE_GameObject_QueryFind(room, 2, 0, 278, 6)`. Sets tag = 2 and clamps its
   **floor/group to ≥ 6** (`max(PROT+573, 6)`, `max(PROT+574, 6)`, else `max(PROT+575,6)`).
4. Special case for building types **146..151**: finds object kind 278 (group 1), sets
   floor 12 / group 0 / tag 3; if missing logs
   `"gm_AllocRaum(): ob_LAGERFLAECHE_ALLGEMEIN is missing in ob_MARKTSTAND!"`. It then
   adds object id 255 (the generic container) and up to 16 of the world's display
   objects (`dword_6477AA`, stride 32 dwords/128 bytes) whose region matches.

So **kind 42 = "ob_LAGERFLAECHE_ALLGEMEIN" (general storage area), floor ≥ 2** and
**kind 278 = "ob_MARKTSTAND" (market stall), group 6, floor ≥ 6** are the two clamp
rules. The same two QueryFind calls reappear in `CreateGebaeude`. Storage rooms are
removed by `VIBE_Building_RemoveStorageRoom @0x588ce4`.

---

## 6. Bauplätze (build plots) → terrain mapping

### 6.1 `BauplatzSizeRec`, stride 96 — `VIBE_Bauplatz_GetSize @0x577628`

The plot-size table is two globals: a base pointer `dword_1234600` and a count
`dword_1234604`. `GetSize` takes a plot **name** and does a **case-insensitive linear
scan** (`VIBE_Util_StrCmpNoCase`) over the table, advancing by **96 bytes per record**
(`v4 = v5 + 96`). On miss it formats `"rd_GetBKSize(): Unbekannter Bauplatz '%s'"` and
returns 0; on hit it returns `record_base = v5 + dword_1234600`.

Inferred `BauplatzSizeRec` layout (96 bytes; field indices from `MapOneToSupermap`,
which reads it as a `_DWORD*`):

| Word index | Byte off | Meaning |
|---|---|---|
| `[0]` | +0 | plot **name** string (case-insensitively matched) — occupies the leading bytes |
| `[16]` | +64 | corner X (world) — used as quad corner 1.x and corner 4.x |
| `[17]` | +68 | corner Y/anchor (shared across all 4 corners' middle component) |
| `[18]` | +72 | corner Z (world) — corners 1 & 2 |
| `[20]` | +80 | second X (corners 2 & 3) |
| `[22]` | +88 | second Z (corners 3 & 4) |

I.e. the record stores an **axis-aligned rectangle** as (x0,z0)…(x1,z1) plus a shared
mid component; the four corners are assembled as the four (x∈{16,20}, z∈{18,22})
combinations.

### 6.2 `VIBE_Bauplatz_MapOneToSupermap @0x5774b8` — quad → tiles → raster fill 255

This is the plot→terrain projection. Steps:

1. `Size = VIBE_Bauplatz_GetSize(name)`; on miss log
   `"rd_AllBauplatzToSupermap(): Unbekannter Bauplatz '%s'"`.
2. Build the **4-corner quad** by reading the rectangle fields above into a scratch
   `v17/v18/v19` triple in four combinations:
   - corner 1 = `(Size[16], Size[17], Size[18])`
   - corner 2 = `(Size[20], Size[17], Size[18])`
   - corner 3 = `(Size[20], Size[17], Size[22])`
   - corner 4 = `(Size[16], Size[17], Size[22])`
3. For each corner call `VIBE_Coord_WorldToTile @0x577690`, which transforms the world
   point through the terrain's bone-chain pivot (`VIBE_Transform_PointThroughBoneChainPivot`)
   and then maps to tile space:
   `tileX = (p.x − map[+144]) / map[+160]`, `tileZ = (p.z − map[+152]) / map[+184]`.
   The eight output floats (`v9..v16`) are the four corners in tile coordinates.
4. Call `VIBE_Map_RasterizeBauplatzEdge(map, quad, 255) @0x5776d8` which **scan-fills**
   the tile-space quad: it computes the longer of the two edge lengths, divides into
   `2*ceil(len)` steps (min 1), and bilinearly interpolates across the quad. For each
   raster cell, if the fill byte (255) is valid it writes the **plot-id byte 255** into
   the supermap pixel buffer (`*(map->pixels + tileZ*width + tileX) = 255`) and
   optionally a second per-cell byte. So a placed/known plot stamps **255** into the
   terrain mask, which is later how the placement code knows a tile belongs to a plot.

### 6.3 Finding free plots — scene-graph walk

The construction flow does **not** scan the size table to find candidate plots; it walks
the live scene graph for `bk_*`-named anchor nodes:

- `VIBE_Building_FilterBlockedBauplatze @0x50c8ec` sets `dword_122EE48 = outArray`,
  resets the count `dword_63C708`, then `VIBE_SceneGraph_WalkAndInvoke(off_649D64, …,
  VIBE_Building_CollectFreeBauplatzCandidate, 384, …)` to collect candidates. It then
  rejects any plot already physically occupied by a nearby person/building: for each
  candidate it transforms the anchor to world space and checks every person within a
  **100.0** tolerance (`VIBE_Math_VectorWithinTolerance(…, 100.0)`); a hit zeroes that
  slot. It also walks the candidate's child `bk_`-prefixed nodes (`StrncmpN(…, "bk_", 3)`,
  child link at `+496`) and applies the same proximity test. Returns the count.
- `VIBE_Building_FindNearestPlotByDistance @0x50cf24` picks the closest non-blocked plot
  to a target world point.
- `VIBE_Building_GetGebaeudeBauplatzPos @0x50cd94` resolves a single building's own plot
  position by switching the active universe slot, transforming the building's plot node
  (`inst+97`), and walking the scene graph (logs
  `"lc_GetGebaeudeBauplatz(): FindPos->Pos:%f %f %f"`).

---

## 7. The construction UI flow

### 7.1 Type selection — `VIBE_Building_OpenGebaeudeBauenWindow @0x50de7c`

Opens the form `BAUEN\GEB_BAUEN`. It:
1. Tests interaction flag `64` (gate), dispatches panel event `0xE`.
2. Builds a **category tab strip** of 9 entries (`HIDWORD(v84)` 1..8): each tab is an
   object `1607 + tab`. Tabs are greyed unless the player's guild rank
   (`byte_12CEA76[536*city]` ∈ {15,10} for some, `byte_12CEA74` in 52..57 for others)
   permits them.
3. On tab click, clears the child window and re-fills it: walks all **72** building
   types (`589`-stride, `j<72`), and for each whose `MapTypeToCategory` equals the
   selected category and which passes `VIBE_Building_CheckBuildRequirements`, adds a
   clickable thumbnail `1010 + typeId` with its name (`dword_8C4788[14*typeId]`) and its
   price label (`VIBE_Building_ComputeSalePrice → VIBE_Money_FormatWithSeparators`).
   Requirement-failed types show a generic "locked" tile (object 1190) instead.
   If `byte_63C70C == 3` (office category) it pulls the office storage via
   `VIBE_Building_FindOfficeStorage`.
4. On a building tile click (id 1010..1082, so `typeId = id − 1010`, model id `+14`):
   it checks affordability (`VIBE_Dialog_CheckResourceByItem` for office-funded, else
   `VIBE_Dialog_CheckResourceAmount`), hides the form, and calls the placement loop
   `VIBE_Building_LoadAndAlignGebaeudeModel`.
5. On success, enqueues a build command (`VIBE_Command_QueueRequestSlotReset28`).

`VIBE_Building_CheckBuildRequirements @0x587bfc` is the gate: it maps the type to a
profession category, reads the player's guild/profession state
(`word_12CE910[268*personId]`), and depending on the category code (1/3/4/6/7/8) checks
the type's required-profession list (`PROT+547`, ≤6 entries) against the player's owned
buildings (`VIBE_Person_CollectByType`). Returns **0** = forbidden, **1** = allowed
now, **2** = allowed-but-prerequisite-missing (shows the locked tile).

### 7.2 City buildings — `VIBE_Building_OpenStadtBauenWindow @0x50e784`

Same shape for civic ("Stadt") buildings via `BAUEN\STADT_BAUEN`. It enumerates a fixed
list (`dword_507F48[16/17]` give the count and id list), queries the city office's funds
object (`QueryFind(office, 2, 6, 0, 277)` — object kind **277** = the city treasury),
lays out tiles `1010 + typeId` in a scrollable grid (scroll buttons 1211/1212 →
`VIBE_Window_Scroll`), and on click runs the same `LoadAndAlignGebaeudeModel` placement
loop. Funding is always checked against the **office** (`CheckResourceByItem`), since
city buildings are paid from the treasury.

### 7.3 Placement loop — `VIBE_Building_LoadAndAlignGebaeudeModel @0x50d01c`

The ghost-placement interactive loop (the biggest function in the cluster, 836 insns):

1. **Resolve the model set.** Builds the type's `gb_` + name prefix (`unk_621458` =
   `"gb_"`), globs `"%sgebaeude/*%s.ogr"` and `"%sgebaeude/*%s_%c.ogr"` variants via
   `VIBE_Vfs_ResolveAndBuildPath`, collecting matching `.ogr` filenames into a
   **96-byte-stride** scratch array (so each candidate model name ≤ 96 bytes). Capped at
   ~50 variants (`v116 < 1124073472f`).
2. **Collect plots.** `VIBE_Building_LookupTypeName` → `VIBE_Building_FilterBlockedBauplatze`
   gives the list of free `bk_*` plots; for each it spawns a "build pennant" marker
   (`%s*sp_BAU_WIMPEL.ogr`, animation `sonstiges\sp_WIMPEL.baf`), tinted, and tracks the
   nearest one to the camera.
3. **Ghost mesh + frame loop.** Loads a random variant `.ogr`
   (`VIBE_Scene_LoadObjectGroup`), drops it on the terrain
   (`VIBE_Collision_ResolveMeshAgainstTerrain`), snaps it to the nearest plot
   (`FindNearestPlotByDistance` + `ComputePlacementHeight` + `SetWorldTranslation`), and
   tints it **green (64,64,192)** when on a valid plot or **red (192,64,64)** when not
   (`VIBE_Mesh_SetVertexColors`, with status banner text). The loop runs under
   `VIBE_GameLogic_RunFrameLoop(415687, …)`; mouse raycast
   (`VIBE_Heightmap_RaycastFromCursor`/`TileToWorld`) moves the ghost, key `50` ('2'?)
   and the rotate handler spin it (`VIBE_Math_MatrixFromEuler`), and a confirm shows the
   price dialog (`VIBE_Building_ComputeSalePrice` + `VIBE_Text_RenderFormattedMessage`
   id 5122 + `VIBE_Dialog_ShowMessageBox`).
4. On confirm it returns the chosen plot name (copied into the caller's `a2` buffer);
   on cancel it releases the ghost and returns 0. Pennant markers are always cleaned up.

### 7.4 Upgrade flow — `VIBE_Building_OpenUpgradeWindow @0x50f7c0` / `RunUpgradeLoop @0x50febc`

`RunUpgradeLoop @0x50febc` is the outer driver: it pumps
`VIBE_GameLogic_RunFrameLoop(425983, …)` and, when the global UI command
`dword_631720` equals `"UPGRADE"` (case-insensitive), calls
`VIBE_Building_OpenUpgradeWindow(_, 44, …)`. It tail-jumps back to `0x50DEB8` (shared
epilogue with the bauen window).

`VIBE_Building_OpenUpgradeWindow @0x50f7c0`:
1. Gates on interaction flag `512`, dispatches panel event `0x11`.
2. Finds the building's office object, opens the **upgrade-tree** form
   (`VIBE_Building_OpenUpgradeTreeWindow`), renders the tech tree
   (`VIBE_Building_BuildUpgradeTree`), and lights the current state
   (`VIBE_Groundplan_GetBuildingState`).
3. On a tech-node click (id 206..937): it looks the node up in the upgrade table
   (`dword_12CDDAC`, **11-dword stride**, count `dword_13CE28C`), tests the prerequisite
   handler (`VIBE_Interaction_InvokeHandlerSlot60(43, …)`). If the upgrade is an *object*
   purchase it spawns it (or, for object-PROT kind 6, builds the place-a-room flow with a
   13-byte spawn descriptor); otherwise it prices the upgrade via
   `VIBE_Building_ComputeMarketPrice(node, 0x64)` scaled by
   `dword_63C744 * dbl_621620 + dbl_621628`, confirms, and enqueues the build command
   (`VIBE_Command_EnqueueBuildingActionStart("upgrade")` … `…End()`), debiting either the
   office storage (`FindUpgradeStorage` → `QueueRequest16`) or the family directly
   (`EnqueueCmd15`, plus `Person_GetFamilyRecord`+16 spend tracking).
4. Closes via `VIBE_Building_CloseUpgradeWindow`, re-dispatches panel event `0x11`.

---

## 8. Market price — `VIBE_Building_ComputeMarketPrice @0x58f3d0`

Computes the credit price of an object/recipe (`a1` = object-PROT id, `a2` = quality 0..100).
`v3 = 65*a1 + dword_13CE27C` (object PROT base, stride **65**). Two paths:

- **Cached** (`*(v3+56) != 0`): returns `cache * 32 * quality * flt_626948`, ×`flt_62694C`
  if the object's profession byte (`v3+33`) == 3.
- **Computed** (cache == 0): walks the producing building's work-objects (via
  `byte_13CE862[a1]` → building PROT at `589 * that`), summing object values
  (`flt_626950 * flt_626954 * dbl_62695C * dbl_626964` weighting), recurses into
  sub-ingredient objects (`v3+46` slots, up to 4, each `ComputeMarketPrice(subId, q)`),
  applies a base markup (`v24 = 2.2`, `flt_626978`), caches the result into `*(v3+56)`,
  and returns. Object kind 23 (`*v3==23`) short-circuits with `×flt_626974`.

`VIBE_Building_ComputeSalePrice @0x591480` (used everywhere in the UI) wraps this with
the building-level cost; `VIBE_Money_FormatWithSeparators @0x58f798` renders the digits.

---

## 9. Per-building state flags & the utility cluster

The building flag/util cluster (`func_profile` filter `VIBE_Building*`) — the
flag-related entries:

| Function | Addr | Role |
|---|---|---|
| `VIBE_Building_ComputeSelectionFlags` | `0x588dec` | Big (477-insn) routine: derives the per-building **selection/HUD flag bitset** from kind + occupant objects + owner — called 14× across the UI. |
| `VIBE_Building_BuildFlagNodeList` | `0x5880b4` | Builds the list of flag nodes via `VIBE_Building_CollectFlagNodeCallback @0x588044` (scene-graph callback). |
| `VIBE_Building_GateStateMachine` | `0x4f6d28` | City-gate open/close state machine (`ResetGateState @0x4f704c`, `RequestGateFlagSync @0x4f70a0`, `RegisterGateHandlers @0x4f72a0`). |
| `VIBE_Building_CheckEntryAllowed` | `0x51dcd4` | Per-building entry gate — combines `CheckTimeWindowOpen @0x51dc04` (opening hours) with owner/occupant flags. |
| `VIBE_Building_MapTypeToCategory` | `0x5878b0` | kind → UI category (§3.1). |
| `VIBE_BuildingType_*` (`0x589818`–`0x589cb0`) | — | a family of tiny classifiers: `ClassifyTypeFlag`, `IsTypeInGroup`, `MapToActionCode`, `MapToCategoryCode`, `GetGuildRankPair`, `MapToProfessionCode`, `ClassifyByRange`, `ComputeVariantIndex`, plus the constant-returning stubs `ReturnCode15/4/7/23/24/25/26` (each returns one literal building-kind code, used as function-pointer table entries). |

The **flag bytes** observed on the instance: `INST+153` (`v3[153] |= 1`) is a status
bitfield set at creation; `INST+92`, `+61`, `+65` are condition/level scalars (§4);
`byte_6317B5` is the global "build mode active" flag (set while either bauen window is
open). The fire/condition and production flags are detailed in
[21 — Economy & office](21-economy-office-stats.md) (the `VIBE_Building_*Production*`,
`*Stock*`, `*Output*` family at `0x57c…`–`0x585…`).

---

## 10. Provenance index (quick reference)

| Symbol | Addr | What |
|---|---|---|
| `VIBE_World_LoadBuildingAndObjectData` | `0x5835f8` | loads PROT(589)/INST(169)/objPROT(65) tables |
| `VIBE_World_InitBuildingTypeTable` | `0x5833b4` | builds `byte_13CE862` kind→type map |
| `VIBE_Building_LookupTypeRecordA` / `B` | `0x589778` / `0x5897c8` | 6-byte-stride variant table @`0x649910` |
| `VIBE_Building_FindById` | `0x587b20` | INST scan, stride 169 |
| `VIBE_Building_CreateGebaeude` | `0x586fb8` | per-instance constructor |
| `VIBE_Building_ResetAllBuildings` | `0x5896fc` | wipe 256 instances |
| `VIBE_Building_RegisterNames` | `0x504a54` | random localized names |
| `VIBE_Building_AllocStorageRoom` | `0x588988` | kind-42 floor≥2 / kind-278 group-6 floor≥6 |
| `VIBE_Building_MapTypeToCategory` | `0x5878b0` | kind→category |
| `VIBE_Building_IsStorageType` / `IsProductionType` | `0x587f50` / `0x587f80` | kind 10 / kind {11,12,13,16,28} |
| `VIBE_Building_CheckBuildRequirements` | `0x587bfc` | profession/prereq gate (PROT+547) |
| `VIBE_Building_ComputeMarketPrice` | `0x58f3d0` | recursive object price (objPROT stride 65) |
| `VIBE_Bauplatz_GetSize` | `0x577628` | plot table scan, stride-96 `BauplatzSizeRec` |
| `VIBE_Bauplatz_MapOneToSupermap` | `0x5774b8` | quad → WorldToTile → raster fill 255 |
| `VIBE_Coord_WorldToTile` | `0x577690` | world→tile via terrain pivot |
| `VIBE_Map_RasterizeBauplatzEdge` | `0x5776d8` | scan-fill plot id 255 into supermap |
| `VIBE_Building_FilterBlockedBauplatze` | `0x50c8ec` | scene-graph `bk_*` collect + 100.0-proximity reject |
| `VIBE_Building_LoadAndAlignGebaeudeModel` | `0x50d01c` | ghost-mesh placement loop |
| `VIBE_Building_OpenGebaeudeBauenWindow` | `0x50de7c` | `BAUEN\GEB_BAUEN` type picker |
| `VIBE_Building_OpenStadtBauenWindow` | `0x50e784` | `BAUEN\STADT_BAUEN` civic picker |
| `VIBE_Building_OpenUpgradeWindow` | `0x50f7c0` | upgrade-tree window |
| `VIBE_Building_RunUpgradeLoop` | `0x50febc` | `"UPGRADE"` command driver |

### Key constants

- PROT record stride **589** (`A_Geb.dat`, 72 records); object-PROT stride **65**
  (`A_Obj.dat`, 731 records); INST stride **169** (256 slots, 43264 bytes).
- `BauplatzSizeRec` stride **96**.
- Storage object **kind 42 (0x2A)** → floor/group clamp ≥ **2**;
  market stall **kind 278 (0x116)** → **group 6**, floor/group clamp ≥ **6**;
  city treasury object **kind 277 (0x115)**.
- Raster fill plot-id byte **255**; proximity reject radius **100.0**.
- Build-window tab objects `1607+tab`; building tiles `1010 + typeId` (model id `+14`);
  upgrade nodes `206..937`; tables `0x13CE294` (PROT), `0x13CE298` (INST),
  `0x13CE27C` (obj PROT), `0x649910` (variant), `0x12CDDAC` (upgrade tree, stride 11).
