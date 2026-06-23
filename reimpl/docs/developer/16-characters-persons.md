# 16 — Characters & persons

> The game has **two parallel actor models** that meet at a single point. The
> *person record* is the persistent, save-loaded social entity (a citizen, a
> dynasty member, a guild official, a vagrant) — it lives in a flat fixed-size
> table and is found by id. The *character object* is the live, in-world 3D
> incarnation of an actor: a heap-allocated control block that owns a mesh, an
> animation rig, an action queue and a position on the terrain. The same factory
> path, [`VIBE_Character_CreateFromModel`](#3-the-factory), builds **both** the
> dynasty player actors and every live NPC; the creature variants (rat / dog /
> cat / horse) are produced by the very same call, distinguished only by a
> substring probe on the model name. This document covers the record layout, the
> factory walkthrough, the id→record lookup core, and the counting / collection
> helpers. The per-actor action state machine and AI that drive these objects
> once created are [doc 17](17-character-actions-ai.md).

All addresses are RVA-style absolute (imagebase `0x400000`). Every function is
named `VIBE_*` from the IDA database; the binary is the source of truth. Struct
offsets below are recovered from accessor instructions, not from symbols.

Cross-links: [09 — New-game flow](09-newgame-flow.md) ·
[13 — Save/load](13-save-load.md) ·
[17 — Character actions & AI](17-character-actions-ai.md) ·
[26 — Mesh](26-mesh-asset-lod.md) · [27 — Animation](27-animation-skeleton.md).

---

## 1. The two actor models at a glance

| | **Person record** | **Character object** |
|---|---|---|
| What it is | Persistent social entity (citizen / dynast / official) | Live 3D in-world actor control block |
| Storage | Flat table `word_12CE910`, **768 slots × 536 bytes** | Heap blocks, pointer array `dword_66F0D0`, **512 slots** |
| Identity | `id` at record `+4`, type byte at `+2`, alive byte `+8` | back-pointer to record at object `+0x88`/`+0x90`; slot index at `+0` |
| Found by | id → `VIBE_Person_FindRecordById @0x58bc6c` | iterate `dword_66F0D0[0..511]` |
| Lifetime | Created/destroyed by world + game logic; saved | `VIBE_Character_CreateFromModel` / `VIBE_Character_Destroy` |
| Size | `0x218` (536) | `0x204` (516) |

A person record does **not** always have a live character object; an off-screen
citizen is just a table row. When an actor needs to appear in the world, the
factory allocates a `0x204`-byte character object, loads its model, and links the
two via the record's object reference.

---

## 2. The person / entity record table

The world owns one contiguous table of person records. The base pointer is read
through several aliased symbols that all point at the same array:

```
word_12CE910   base, as __int16[]          (alive/type half-word)
byte_12CE912   base+2, type byte            (record +2)
dword_12CE914  base+4, id dword             (record +4)
byte_12CE918   base+8, alive/active byte     (record +8)
```

### 2.1 Stride and capacity — 768 slots of 536 bytes

`VIBE_Person_FindRecordById @0x58bc6c` is the canonical linear scan and pins down
both the stride and the capacity:

```c
// gilde.exe 0x58bc6c — VIBE_Person_FindRecordById (eax = id)
__int16 *VIBE_Person_FindRecordById(int id) {
    unsigned v2 = 0;
    while (word_12CE910[v2/2] == -1 || id != dword_12CE914[v2/4]) {
        v2 += 536;                 // record stride = 0x218
        if ((int)v2 >= 411648)     // 411648 / 536 = 768 slots
            return 0;
    }
    return &word_12CE910[v2/2];     // pointer to the record
}
```

- **Record stride: 536 bytes (`0x218`).**
- **Capacity: 768 slots** (`411648 / 536 = 768`). The 768 figure recurs across the
  collection helpers (`< 768` loop bounds) and the per-slot helpers index the
  table with `268 * slot` half-words (`268 * 2 = 536`).
- An **empty** slot has its leading half-word (`+0`) set to `-1` (`0xFFFF`); the
  scan skips those.
- A record is matched by comparing the dword `id` at **`+4`** (`dword_12CE914`).

`VIBE_Person_ComputeTotalWealth @0x591f7c` independently confirms the geometry:
it rejects `a1 >= 0x300` (768) and indexes `&word_12CE910[268 * a1]` — i.e. slot
`a1` begins at byte `536 * a1`.

### 2.2 Validity / type predicates → recovered field offsets

Two predicates expose the early record header:

```c
// 0x4f8e60 — VIBE_Person_IsValidActiveRecord (ax = slot)
BOOL valid(u16 slot) {
    unsigned o = 536 * slot;
    return word_12CE910[o/2] != -1      // +0  != 0xFFFF (slot occupied)
        && byte_12CE918[o]               // +8  alive/active != 0
        && byte_12CE912[o] < 10;         // +2  type byte in [0..9]
}

// 0x4f8ee4 — VIBE_Entity_IsPersonType (ax = slot)
//   occupied && alive && type<10 && type!=6 && type!=7  → true
```

So the **type byte at `+2`** discriminates entity sub-kinds (`< 10` = a real
entity; values `6` and `7` are *not* "person" — likely building/office kinds that
share the table), and the **alive byte at `+8`** marks the slot active.

### 2.3 Recovered record layout

Offsets confirmed from the accessors in this document (`VIBE_Person_*`,
`VIBE_InfoPanel_BuildPerson`, `VIBE_Character_CollectByOwner`):

| Offset | Type | Field (inferred) | Evidence |
|---|---|---|---|
| `+0x00` | `i16` | occupied flag (`-1` = empty) | `FindRecordById`, `IsValidActiveRecord` |
| `+0x02` | `u8`  | **entity type** byte | `IsValidActiveRecord`, `Entity_IsPersonType` |
| `+0x04` | `i32` | **person id** (lookup key) | `FindRecordById` (`dword_12CE914`) |
| `+0x08` | `u8`  | **alive / active** flag | `IsValidActiveRecord` (`byte_12CE918`) |
| `+0x5D` | `i32` | owning-dynasty / faction back-ref | `CollectByOwner` reads object `+44` from here |
| `+0x65` | `i32` | live **object reference** | `VIBE_Person_FindByObjectRef` (`result+101`) |
| `+0xC2` | `i32` | linked game-object handle | `InfoPanel` `*(v2+99 dwords)` |
| `+0x82` | `u8`  | sub-type 5/6/7 selector | `InfoPanel_BuildPerson` (`v2+2 byte`) |
| `+0x130` | `i32` | inventory/storage object | `InfoPanel` `*((_DWORD*)v2 + 21)` |
| `+0x182`,`+0x183` | `u8` | two tiled-row counters (HUD) | `InfoPanel` `+130/+131` |
| `+0x165` | `u8`  | building-group selector flag | `InfoPanel` `+357` |

The wealth / stat accessors (`VIBE_Person_SumCurrencyHeld @0x59152c`,
`VIBE_BuildingValue_ComputeRoomWorth @0x59116c`,
`VIBE_BuildingValue_SumStorageItemWorth @0x591658`, driven from
`VIBE_Person_ComputeTotalWealth @0x591f7c`) treat the record as the root of a
*property graph*: a person's "money / wealth" is not a single scalar but the sum
of currency held plus the worth of every owned building room and stored item,
gathered by re-querying the person table (`QueryBegin(record, 1, 3, ...)`). The
HUD/Tooltip builders (`VIBE_Tooltip_BuildPersonDetailed @0x4f882c`,
`VIBE_Hud_BuildPersonCard @0x553f30`, `VIBE_Person_ResolveStatusFlags @0x553ce8`)
read the per-person skill / faith / health fields off this same record; their
exact byte offsets are deferred to the HUD doc, but they all index
`word_12CE910 + 536*slot`.

### 2.4 Table reset

`VIBE_World_ResetPersonTable @0x58389c` clears the *companion* world-object table
(`dword_13CE298`, stride 169, 43264 bytes = 256 entries) — the building/room side
of the entity model — and is paired with the person table proper. The person
table base/limit live in `dword_13CE294` (record-1 base used by name lookups) and
the world-object base in `dword_13CE298`.

---

## 3. The factory

`VIBE_Character_CreateFromModel @0x402d10` is the **single creation path** for
every live actor — dynasty members, NPCs, and the four creature types. It is a
thin three-call orchestrator:

```c
// gilde.exe 0x402d10 — VIBE_Character_CreateFromModel
//   eax = model-name string, edi = universe/parent node
int CreateFromModel(const char *name, int parentNode) {
    obj = VIBE_Character_AllocSlot(parentNode);       // 0x402254
    if (VIBE_Character_CreateMesh(obj, name))          // 0x4029c4
        return obj;
    VIBE_Character_Destroy(obj);                       // 0x402120  (rollback)
    return 0;
}
```

If mesh creation fails, the just-allocated slot is destroyed and `0` is returned —
the factory never leaks a half-built object.

### 3.1 `VIBE_Character_AllocSlot @0x402254` — the 512-slot registry

```c
char *AllocSlot(int parentNode) {
    obj = VIBE_Memory_AllocDebug(0x204, "ch:Create:Character");  // 516-byte block
    memset(obj+0, 0, 516 - 4)            // VIBE_Light_SetGrayColorThunk(0,516,obj) zero-fills the tail
    // find first free slot in the 512-entry pointer table:
    for (i = 0; i < 512 && dword_66F0D4[i]; ) i++;
    if (i >= 512) {                       // overflow
        VIBE_Memory_FreeDebug(obj);
        VIBE_ErrorLog_ReportMessage("ch_Create:Too many characters!");
        return 0;
    }
    dword_66F0D0[i] = obj;                // register
    *(i32*)obj = i;                       // object +0 = its own slot index
    return obj;
}
```

- The live-character registry is **`dword_66F0D0`, 512 slots** of object pointers.
  (`dword_66F0D4` is `+4` of the same array, used by the free-slot scan.)
- Every character object is **`0x204` = 516 bytes**, zero-initialised, and stores
  its **own slot index at `+0`**.

### 3.2 `VIBE_Character_CreateMesh @0x4029c4` — model load, name split, creature probe

This is the heart of actor creation. Walking it in order:

**1. Attach to the 3D universe.** A 4-dword identity transform is copied from
`dword_401010` and passed to `VIBE_Object_AttachToUniverseNode @0x5b3e30`, which
loads the model (`name`) and returns the **scene-object node**, stored at
**object `+52` (`+0x34`)**. `object+40` is set to `-1` (no script bound yet). If
the attach fails, it logs `"ch_CreateMesh(): Could not load 3D-Character: %s"` and
returns `0` (→ factory rolls back). *This is the platform boundary: the model /
LOD load is [doc 26](26-mesh-asset-lod.md); the rig is [doc 27]
(27-animation-skeleton.md). The DirectDraw/D3D path is reimplemented over
Vulkan, but the engine-side node math here is reconstructed 1:1.*

**2. Name decomposition.** The model name (e.g. `"<base>_<prefix>_<suffix>"`) is
copied to a 256-byte scratch buffer and split on `'_'` using
`VIBE_Util_StrChr @0x5d3ef0`:

```
name = "BASE_PREFIX_REST"
        │     │
   first '_'  └── second '_' → truncated to 0 here
        │
   everything before first '_' = BASE
   between first and second '_' = PREFIX
```

- If a first `'_'` is found: the **prefix** substring (after the first `_`, up to
  the second `_`) is copied to **object `+304` (`+0x130`)**, and the **base** (the
  head, with the byte before the first `_` nulled) is copied to **object `+368`
  (`+0x170`)**.

  > Note the cross-over: the **base name → `+304`**, the **prefix → `+368`** when a
  > split occurs. (The decompiler labels the destinations the other way around
  > because of the goto-merge; the *base* buffer `v28` is what reaches `+368` via
  > the shared copy loop at `loc_402A9D`.) When there is **no** `'_'`, the fallback
  > default string `byte_610134` is copied to `+304` and the whole name stays as the
  > base.

- The raw model name is **also** copied verbatim to **object `+5`** (a third name
  field), and `object+44`, `+48` are set to `-1` (tile coords, unset).

**3. Node flag block** (all writes go through the node pointer at object `+52`):
`node+530 |= 0x0C`, `node+529 &= ~2`, `node+535 = 2`, `node+536 = 1`,
`node+531 &= ~4`, a float `1072064102` written into the sub-node at
`*(node+492)+2296`, and later `node+72 = 0x03000000`. Object `+136` is set to the
vtable/handler `off_649D64`, `+416 = 1.0f` (`0x3F800000`). These configure
visibility / culling / render flags on the scene node.

**4. The creature-type probe** (`loc_5CB930` = the case-sensitive `strstr`
thunk). The **base name** is searched for the German creature tokens. From the
disassembly at `0x402bb0`:

```
strstr(base, "RATTE")  ──true──┐         (rat)
strstr(base, "HUND")   ──true──┤  → animal branch:
strstr(base, "KATZE")  ──true──┘    re-probe "RATTE": node+529 &= ~8, ~4
strstr(base, "PFERD")  ──true────→  horse branch: object+4 |= 8, node+529 &= ~8
```

The literals live contiguously: `aRatte "RATTE" @0x610138`, `aHund "HUND"
@0x610140`, `aKatze "KATZE" @0x610148`, `aPferd "PFERD" @0x610150`. A match sets a
class byte at **object `+4`** (`1` = human default, `2` = small animal /
rat·dog·cat, `|8` adds the horse/mount bit) and clears render/selection bits
(`node+529` bits `8`/`4`) so creatures are not pickable or shadow-casting like
people. The whole probe is *the* mechanism that lets one factory build pets,
mounts and humans from one code path.

**5. Terrain / lighting / dirty.** `object+4 = 1` (default human class),
`VIBE_Character_QueryTerrainType(obj, 0) @0x404650` resolves the standing tile,
`VIBE_Light_BuildObjectCache @0x5c8218` builds the per-object light cache, and
`VIBE_Object_PropagateDirtyFlag @0x5af2c0` marks the node dirty for re-render.

**6. Gait + idle preload.** `VIBE_Character_PreloadAniSet @0x403c34` is called with
the two default clips `"bewegung/gehen"` (walk gait) and `"stehen/stehen_newnoise"`
(idle), formatting paths `character/<base>/<clip>_<base>.baf` and streaming each
into the animation stock (`VIBE_Anim_LoadStreamToStock @0x5d3858`, attached via
`VIBE_Anim_AttachToBone @0x5d0b64`). If the low-poly system is enabled
(`dword_62D088` set and object `+492` present) it additionally preloads the
low-poly `"gehen"` set and rebuilds the LOD mesh
(`VIBE_Character_PreloadLowPolyAniSet @0x403da0`,
`VIBE_Character_UpdateLowPolyMesh @0x40244c`). *Both are the animation subsystem —
[doc 27](27-animation-skeleton.md).*

**7. Done.** `object+141 |= 0x10` (fully-initialised flag) and the function
returns `1`.

### 3.3 `VIBE_Character_Destroy @0x402120` — teardown

The inverse of allocation. It (a) finishes any bound script
(`object+40` → `VIBE_Script_FindByHandle @0x442174` / `VIBE_Script_Finish`), (b)
removes the object pointer from `dword_66F0D0[]` (linear find, `< 512`) and writes
`-1` into the slot index at `+0`, (c) clears the action queue
(`VIBE_ActionQueue_ClearAll @0x4043dc`), (d) if a mesh is attached (`obj[13]` =
`+52` non-zero) releases the morph-ani, optionally switches the active universe
slot around the detach (`VIBE_Universe_SwitchActiveSlot @0x5b4a24`), detaches and
releases the scene node, the attached sub-object (`obj[73]`), and a secondary
object (`obj[123]`), via `VIBE_Object_DetachAndRelease @0x5b4258`, then (e) frees
the auxiliary buffer (`obj[73]`) and the object block itself
(`VIBE_Memory_FreeDebug @0x43923c`). Returns `1` on success.

---

## 4. The lookup / query core

There are **two query families**, mirroring the two actor models.

### 4.1 Person table queries

`VIBE_Person_FindRecordById @0x58bc6c` (678 xrefs — the single most-called lookup
in this subsystem) is the id→record map of §2.1. The **filtered iterator** is a
stateful pair:

- `VIBE_Person_QueryBegin @0x586c20` — a varargs filter setup. It takes
  `(count, key0, val0, key1, val1, …)` pairs and writes the active filter into a
  block of globals (`byte_6498A8`…`dword_6498DC`):

  | key | filter on | global |
  |---|---|---|
  | `0` | type byte (record `+0`) | `byte_6498A8` |
  | `1` | id (record `+1` dword) | `dword_6498AC` |
  | `2` | name substring (copied) | `dword_6498B0` |
  | `3` | field at record `+37` (`u16`) | `word_6498B4` |
  | `4` | field at record `+39` (`u16`) | `word_6498B6` |
  | `5` | indirect type via `byte[589*type + base]` | `byte_6498B8` |
  | `6` | "any-match" (OR) mode | `byte_6498B9` |

  It seeds the cursor `dword_6498DC = dword_13CE298` and tail-calls the iterator.

- `VIBE_Person_IterNext @0x586a6c` — advances the cursor in **169-byte steps**
  over the *world-object* table (`dword_13CE298`, limit `+43264`), skipping empty
  rows (`!*v0`), and applies the active filters. Note the iterator walks the
  **169-byte building/room table**, not the 536-byte person table — the person
  query is expressed against the world-object companion table that each person
  links into (this is the "property graph" again). `dword_6498BC` caps the scan at
  256 entries.

- `VIBE_Person_FindByObjectRef @0x586a40` is a thin wrapper: `QueryBegin(obj, 1,
  0, 71)` then iterate until record `+101` equals the object pointer — i.e. find
  the person that owns a given scene object.

### 4.2 Game-object queries

`VIBE_GameObject_QueryFind @0x5857fc` (583 xrefs) is the parallel varargs finder
over the *game-object* table (`dword_13CE27C` guard). Its key set:
`0`=type (`word_6498C4`), `1`=owner id (`dword_6498C8`), `3`=sub-id
(`word_6498CC`), `4`=byte filter (`byte_6498CE`), `5/6/7`=mode flags
(self-include / "must" / "exclusive"). It seeds `dword_6498E0` and tail-calls the
iterator `VIBE_GameObject_IterNext @0x58529c`.

`VIBE_GameObject_SpawnWindowObject @0x40e1c8` creates a *UI* game-object (a HUD
window-anchored object): it round-robins a slot counter `dword_62D2EC` in `1..8`,
allocates two layout objects via `VIBE_GameLogic_Objects @0x412fa0`, allocates a
**740-byte widget** (`VIBE_Widget_AllocSlot @0x412dac`, into `dword_69FFB4`),
stamps kind `66`, and copies the x/y/w/h rectangle into both the widget and its
backing object. This is the bridge that lets a person/object be shown in a HUD
panel (see `VIBE_InfoPanel_BuildPerson`).

---

## 5. Counting & collection helpers

These are the call-graph roots that fan out over the two registries; they show
exactly which array is "the live characters" and which is "the persons".

| Function | Addr | Scans | Purpose / loop bound |
|---|---|---|---|
| `VIBE_Character_CountActiveUniverse` | `0x401a9c` | `dword_62CEFC`, stride 404, 517120 bytes (**1280 entries**) | count universe slots with a non-null first dword |
| `VIBE_Character_CountByOwner` | `0x401ad4` | `dword_66F0D0[0..511]` | count live characters whose node (`obj+136`) == `&byte_13ECEC8[984*owner]`; `a2=0` excludes paused (`node+533==1`) |
| `VIBE_Character_CountByOwnerInRange` | `0x401b40` | `dword_66F0D0` | owner-count gated by tile range |
| `VIBE_Character_CountWithTransport` | `0x401bd8` | `dword_66F0D0[0..511]` | count actors with a transport (`obj+292`), optionally of the active dynasty (`dword_649D60`) |
| `VIBE_Character_CountByType` | `0x4b092c` | `dword_66F0D0` | count by class byte |
| `VIBE_Character_CountActiveByTurn` | `0x4525fc` | — | per-turn active count |
| `VIBE_Character_CollectNearbyAtTile` | `0x401c3c` | `dword_66F0D0[0..511]` | gather up to 15 live actors on the same tile (`obj+44/+48`) within radius (50/100), then apply a separation/steering nudge (`VIBE_Math_VectorWithinTolerance`, `VIBE_Math_VectorNormalize`) and re-validate the destination tile |
| `VIBE_Character_CollectByOwner` | `0x4b99ac` | **person table** `word_12CE910`, stride 268 (`×2`=536), **768 slots** | collect persons whose live object (`record obj-ref +97 dwords`) has owner field `+44` == `owner+1`; caps the visible set at 8 (extra → `SetVisible(0)`), array cap 31 |
| `VIBE_Person_CountActiveSlots` | `0x587b60` | name-table (`dword_13CE294`, stride 589, 42408 = **72 names**) | find a name's slot index by case-insensitive compare |
| `VIBE_Person_CollectByType` | `0x587b9c` | person table | collect by type byte |

Two distinct capacities to keep straight: **512** for the live-character pointer
registry (`dword_66F0D0`), and **768** for the persistent person table
(`word_12CE910`, the `< 768` / `<0x300` bound).

---

## 6. Handoff to actions & AI (doc 17)

Once `VIBE_Character_CreateFromModel` returns a live object, it is inert until an
**action queue** is attached. `VIBE_Character_Destroy` calls
`VIBE_ActionQueue_ClearAll @0x4043dc`; the queue itself, the per-tick action
dispatcher, the pathfinding/steering (the `CollectNearbyAtTile` separation logic
above is the *steering* hook), and the NPC decision AI
(`VIBE_AiMethod_ScanCandidatePersons @0x4680e0`,
`VIBE_AiTarget_FindNearestPerson @0x479dd8`, etc., all of which use the person
queries from §4) are documented in [17 — Character actions & AI]
(17-character-actions-ai.md). Save/load of the person table and the rebuild of
live objects on load is [13 — Save/load](13-save-load.md); the wizard that creates
the very first dynasty actors is [09 — New-game flow](09-newgame-flow.md).

---

## 7. Provenance summary

| Symbol | Addr | Role |
|---|---|---|
| `VIBE_Character_CreateFromModel` | `0x402d10` | factory entry (alloc → mesh → rollback) |
| `VIBE_Character_AllocSlot` | `0x402254` | 512-slot registry alloc, 516-byte block |
| `VIBE_Character_CreateMesh` | `0x4029c4` | model load, name split, creature probe, gait preload |
| `VIBE_Character_Destroy` | `0x402120` | full teardown |
| `VIBE_Person_FindRecordById` | `0x58bc6c` | id→record (768×536), 678 xrefs |
| `VIBE_Person_QueryBegin` / `IterNext` | `0x586c20` / `0x586a6c` | filtered person iterator |
| `VIBE_Person_FindByObjectRef` | `0x586a40` | object→person |
| `VIBE_Person_IsValidActiveRecord` | `0x4f8e60` | occupied & alive & type<10 |
| `VIBE_Entity_IsPersonType` | `0x4f8ee4` | person-kind predicate (type<10, ≠6,7) |
| `VIBE_GameObject_QueryFind` | `0x5857fc` | game-object iterator, 583 xrefs |
| `VIBE_GameObject_SpawnWindowObject` | `0x40e1c8` | HUD-anchored object spawn |
| `VIBE_Person_ComputeTotalWealth` | `0x591f7c` | wealth = currency + room + storage worth |
| `VIBE_Character_CollectByOwner` | `0x4b99ac` | person collect by owner (768-loop) |
| `VIBE_Character_CountWithTransport` | `0x401bd8` | live-char count (512-loop) |
| `VIBE_World_ResetPersonTable` | `0x58389c` | clear companion world-object table |

Tables: `word_12CE910` (person table, base) · `byte_12CE912`/`dword_12CE914`/`byte_12CE918`
(aliases at +2/+4/+8) · `dword_66F0D0` (live-character registry, 512) ·
`dword_13CE294` (name table base) · `dword_13CE298` (world-object table base) ·
`dword_649D60` (active dynasty index) · creature literals `0x610138`–`0x610150`.
