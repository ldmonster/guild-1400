# Inventory, Storage Capacity & Wealth Accounting

Reverse-engineered model of how *The Guild / Europa 1400* (`gilde.exe`) represents
carried/stored goods, computes per-slot and per-container capacity, moves goods
between containers, and aggregates a person's liquid currency into a total net
worth. Every formula and constant below is grounded in the C++ reimplementation,
which is a 1:1 port of the binary; provenance addresses (`gilde.exe 0x…`) are
carried through from the original.

## Source files

| File | What it ports |
|------|---------------|
| `src/sim/types.h` | Record layouts: `ItemRec`, `InvGridSlot`, `Person`, `SceneNode`, item-type ids |
| `src/sim/inventory.h` / `.cpp` | Per-record rules: effective stock, slot capacity, UI-slot scan; carried-item add/remove/find model |
| `src/sim/inventory_capacity.h` / `.cpp` | Per-container capacity / free-space / carry-capacity engines; storage/workstation slot collection |
| `src/sim/inventory_wealth.h` / `.cpp` | Currency held + total-wealth aggregation |
| `src/sim/inventory2.h` / `.cpp` | Inventory/workstation GRID-UI cluster (slot windows, slot-vs-item reconciliation) |
| `src/sim/dragslot.h` / `.cpp` | The 6-entry carried-item drag-slot stacking table |
| `src/sim/building_storage.h` / `.cpp` | Per-building worth terms consumed by total-wealth (`ComputeRoomWorth`, `SumStorageItemWorth`) |
| `src/sim/person.h` / `.cpp` | The Person-record cash word (`GetCashAmount`) |

---

## 1. Inventory data structure

### 1.1 The item record

There is no dedicated "inventory array". In the original, every item / stock /
currency stack is a **scene-entity (GameObject) node** in the global scene tree
(base `*(0x13CE290)`, stride 67). A container's contents are obtained by walking
that tree with `VIBE_GameObject_QueryFind` / `IterNext`, filtered by item type.

The inventory accessors touch only two fields of each node, captured as `ItemRec`
(`src/sim/types.h:178-185`):

```c
struct ItemRec {
    i16 type;     // +0x00  item type id
    u8  pad2[12]; // +0x02..+0x0D  scene-node header (unused by inventory)
    i32 count;    // +0x0E  stock/count (dword, UNALIGNED)
};
```

- **`type` (+0x00)** — the good's prototype id (a `WORD`).
- **`count` (+0x0E)** — the quantity in this stack. It is an **unaligned dword**;
  the reimplementation reads/writes it via `memcpy` to preserve that
  (`inventory.cpp:14-22`, `ItemCount`/`SetItemCount`).

Special type ids with inventory semantics (`src/sim/types.h:189-198`,
enum `ItemType`):

| id | name | meaning |
|----|------|---------|
| 9 | `kItemCurrency` | money good (special storage handling) |
| 42 | `kItemReserveA` | effective stock = count − 1 |
| 278 | `kItemReserveB` | effective stock = count − 1 |
| 475 | `kItemReserveC` | effective stock = count − 1 |
| 476 | `kItemReserveD` | effective stock = count − 1 |
| 477 | `kItemHighCap` | slot capacity = 5·count + 10 |
| 377 | `kItemSpecialA` | carry-capacity special (capacity 0) |
| 378 | `kItemSpecialB` | carry-capacity special (capacity 0) |

### 1.2 Stacking

Goods of the same type are held as **one stack node**, with the quantity in
`count`. There is no fixed per-item slot array in the simulation core: a container
holds a variable list of stack nodes, one per distinct type present. The
reimplementation models a single container's item set as a flat array
(`Inventory`, `inventory.h:65-73`):

```c
struct Inventory {
    static constexpr int kMaxItems = 64;
    ItemRec items[kMaxItems] = {};
    int     itemCount = 0;
};
```

This is a modeling convenience for testability — `kMaxItems = 64` is **not** a
recovered game constant; the originals keep stacks as scene-tree children. The
true per-stack stacking limit is the **slot capacity** (section 2).

When a stack is emptied, the record is **kept with count 0** rather than removed —
`InventoryRemove` (`inventory.cpp:176-190`) writes `have - rem` even when that is
0, matching the binary which "keeps the record and zeroes its count"
(`inventory.h:99-102`).

### 1.3 Effective stock (the "reserve-1" rule)

`VIBE_Inventory_GetEffectiveStock` (`gilde.exe 0x5923fc`,
`inventory.cpp:36-43`) returns the *usable* stock, which is the raw count minus
one reserved unit for the four reserve goods:

```c
int InventoryGetEffectiveStock(const ItemRec* typeRec, const ItemRec* rec) {
    int count = ItemCount(rec);            // *(rec + 0x0E)
    return IsReserveGood(typeRec->type) ? count - 1 : count;
}
// IsReserveGood: type ∈ {42, 278, 475, 476}
```

Note the original reads the **type** from one pointer and the **count** from
another (`__usercall eax=typePtr, edx=record`); the common one-arg overload passes
the same record for both.

### 1.4 The UI grid-slot table

Distinct from the simulation item nodes is the on-screen slot table
`word_63D1D8` (24-byte stride), modeled as `InvGridSlot` (`src/sim/types.h:277-283`):

```c
struct InvGridSlot {
    i16 type;     // +0x00  item type id (0 marker terminates the scan)
    u8  pad2[22]; // +0x02..+0x17  UI widget / state payload
};
```

`VIBE_Inventory_FindSlotByItemId` (`gilde.exe 0x54f04c`) /
`…FindSlotIndexByItemId` (`0x54f090`) are pure linear scans of this table
(`inventory.cpp:69-102`). The scan terminates at the first slot whose *successor*
marker word is zero (`word_63D1F0[i]`), so an empty (`type == 0`) first slot
returns immediately.

---

## 2. Capacity limits

### 2.1 Per-slot capacity — the exact formula

The single capacity table is `VIBE_Inventory_GetSlotCapacity`
(`gilde.exe 0x592474`). It is **inlined into every capacity engine** in the
binary. Recovered byte-for-byte (`inventory.cpp:51-58`,
`inventory_capacity.cpp:20-26`):

```c
int InventorySlotCapacity(i16 type, i32 level) {  // level == count dword at +0x0E
    if (type == 477)  return 5 * level + 10;   // kItemHighCap
    if (level == 3)   return 80;
    return 20 * level;
}
```

So, in words:

```
capacity(type, level) =
    5*level + 10        if type == 477
    80                  else if level == 3
    20*level            otherwise
```

The second argument is the **level / grade dword** (`+0x0E`, the same field the
binary calls "Menge"/level). For most goods, capacity scales as `20·level`, with
`level == 3` overriding to a flat 80 (i.e. one grade above the linear 60), and the
high-capacity good 477 using a separate `5·level + 10` curve. (Documented at
`inventory.h:44`, `inventory_capacity.h:74-77`.)

### 2.2 Free space for one item type in a container

`VIBE_Inventory_ComputeFreeSpaceForItem` (`gilde.exe 0x59266c`,
`inventory_capacity.cpp:77-93`). Computes how many more units of `type` fit into a
container `c`, clamped to a caller `ceiling`:

```c
int InventoryComputeFreeSpaceForItem(const ContainerView& c, i16 type, int ceiling) {
    int cap = InventorySlotCapacity(c.selfType, c.selfLevel);
    const StockChild* ch = FindChildByType(c, type);
    if (ch) {                                  // a stack of `type` already exists
        int result = cap - ch->level;          // remaining room in that stack
        if (result <= ceiling) return result;
    } else {                                   // no such stack yet
        int count = (int)c.children.size();    // number of occupied slots
        if (count >= c.fill28) return 0;       // container slot count exhausted
        if (cap < ceiling)    return cap;
    }
    return ceiling;
}
```

Key points:

- Capacity is read from the **container's own** type/level header
  (`c.selfType`/`c.selfLevel`, the `a1+0` / `a1+14` capacity record), not the
  item's.
- If the type is already present, free room is `cap − currentStackLevel`.
- If absent, a new slot is needed: the container's **slot count gate** is
  `fill28` (container byte `+28`); when `childCount ≥ fill28` there is no room for
  a new distinct stack and the result is 0.

The carried-item helper `InventoryFreeSpace` (`inventory.cpp:130-144`) reproduces
the same `cap − count` / `min(free, ceiling)` shape over the flat `Inventory`
model, seeding a fresh stack's level to 0 for type 477 and 1 otherwise.

### 2.3 Free capacity through the root container

`VIBE_Inventory_ComputeFreeCapacity` (`gilde.exe 0x5924a8`,
`inventory_capacity.cpp:122-144`). Resolves the root container, takes the slot
capacity from the **stock record's** own type/level (`a1+0`/`a1+14`), then:

- If a stack of `proto` exists: `free = cap − effectiveStock(child)`, clamped to
  `min(free, ceiling)`.
- Else, for the reserve good **type 42**: count children; only if **both** fill
  gates pass (`count < fill28 && count < fill29`) return `min(cap, ceiling)`,
  else 0.
- Else (non-42): if `count < fill28` return `min(cap, ceiling)`, else 0.

The dual-gate (`fill28` *and* `fill29`, container bytes `+28`/`+29`) is specific to
the type-42 reserve branch; all other goods use only `fill28`.

### 2.4 Carry capacity on a person

`VIBE_Inventory_ComputeCarryCapacity` (`gilde.exe 0x592710`,
`inventory_capacity.cpp:166-192`). Room for a carried good on a **person** (the
hand/carry slot, distinct from a storage room):

```c
if (typeCategory == 9) return ceiling;     // category 9 (money): UNLIMITED carry
if (((personKind==6||personKind==7) && hasAvatar)
    || proto==377 || proto==378 || !carriable) return 0;

const StockChild* ch = FindChildByType(carried, proto);
if (ch) {                                   // already carrying some
    int v4 = (ceiling + ch->level > 3) ? (3 - ch->level) : ceiling;
    return v4 >= 0 ? v4 : 0;
}
int count = carried.children.size();
if (count >= 6)       return 0;             // 6 carry slots max
if (ceiling <= 3)     return ceiling;
return 3;                                    // a fresh carried stack caps at 3
```

So a person can carry at most **6 distinct goods**, each carried stack capped at
**3 units**, *except* money (type-def category 9), which is uncapped. `personKind`
is the person record's kind byte (`+0x02`); kinds 6/7 with an avatar cannot carry
(they delegate to an avatar/animal). `typeCategory` comes from the goods type-def
table `dword_13CE27C[65*proto + 0]`. The two special protos 377/378 always return 0.

### 2.5 Collecting storage / workstation slots

`VIBE_Inventory_CollectStorageSlots` (`gilde.exe 0x590fc0`) and
`…CollectWorkstationSlots` (`0x590df8`) walk a resolved room's children and emit a
parallel slot table (`CollectedSlots`, `inventory_capacity.h:141-146`): per kept
child they record **effective stock** (`+104`), **slot capacity** (`+8`), and the
**item type** (`+72`), bumping the count (`+4`). The keep predicate filters by the
goods' type-def category against a mode flag (`inventory_capacity.cpp:212-250`):

```
storage:     keep = (cat==23 && mode) || (cat!=23 && !mode)
workstation: keep = ((cat==23||cat==37) && mode)
                  || (cat!=23 && cat!=37 && !mode)
                  || (child.type == 278)
```

The per-mode capacity hint is derived from the room's fill bytes (`+28`/`+29`),
with the type-278 room handled specially (mode!=0 over a 278 room returns 0).

---

## 3. How money is held

Money is held in **two distinct places**, and they are not the same quantity:

### 3.1 As inventory currency stacks (the "wealth" path)

Currency is an ordinary scene-tree stack node whose **type is a per-player
currency prototype**. The prototype is resolved from the goods table:

```
currencyProto(player) = dword_13CD6F2[189 * player] >> 16
```

(`inventory_wealth.h:39-47`; modeled via the injected `WealthSetCurrencyProtoTable`
because the table is runtime-seeded and zero in the static image.) Each
player/faction has its own currency good id (189-byte goods records). The currency
stack lives under the person's container at **person + 0x178** (`+376`), and its
**amount is the `+14` count dword** — i.e. money is stored exactly like any other
good's count, just with the currency type id. The type-def **category 9** marks the
money class (uncapped carry, section 2.4).

### 3.2 As a Person-record cash word (the "cash" path)

Separately, the Person record carries a plain **cash-on-hand word** at offset
`+0x0A` (`src/sim/types.h:70`, `PersonField::kPfCash`). `VIBE_Person_GetCashAmount`
(`gilde.exe 0x58bc9c`, `person.cpp:57-60`) returns it as a double:

```c
double PersonGetCashAmount(u16 idx) {
    return (double)(u16)PersonGetWord(&g_persons[idx], kPfCash);  // *(u16*)(rec+0x0A)
}
```

This is a 16-bit unsigned field on the record, used by NPC-facing systems (recruit
fees `recruit_cost.cpp:115`, combat cash tiers `combat_slots2.cpp`, gifts
`interaction4.h`). It is a *separate scalar* from the currency-stack amount; the
total-wealth computation (section 4) uses the **currency-stack** sum, not this word.

> Unknown / not fully reconstructed: the precise rule for when the `+0x0A` cash
> word and the currency-stack amount are kept in sync (or whether they intentionally
> diverge) is not established by the files in scope. They are read by different
> subsystems; treat them as two independent representations of "money" pending
> further reversing.

---

## 4. Wealth accounting (net worth)

### 4.1 Liquid currency held

`VIBE_Person_SumCurrencyHeld` (`gilde.exe 0x59152c`,
`inventory_wealth.cpp:54-61`) sums the `+14` amount of **every** child stack under
the person's container whose type equals the **active player's** currency proto
(`byte_6477A1` selects the player row):

```c
int PersonSumCurrencyHeld(const ContainerView& container) {
    i16 proto = WealthCurrencyProto(g_activePlayer);
    int sum = 0;
    for (const StockChild& ch : container.children)
        if (ch.type == proto) sum += ch.level;   // ch.level == +14 amount
    return sum;
}
```

`VIBE_Person_GetCurrencyAmount` (`gilde.exe 0x5915b8`,
`inventory_wealth.cpp:40-44`) is the single-stack variant for a *specified*
player: it returns the `+14` amount of the one currency stack of that player's
proto, or 0 if none.

### 4.2 Total wealth — the exact formula

`VIBE_Person_ComputeTotalWealth` (`gilde.exe 0x591f7c`,
`inventory_wealth.cpp:71-82`):

```c
int PersonComputeTotalWealth(int personIndex, bool freeSlot,
                             const ContainerView& currency,
                             const std::vector<OwnedBuildingWorth>& ownedBuildings) {
    if ((unsigned)personIndex >= 0x300u) return -1;   // 768 person cap
    if (freeSlot)                        return -1;    // marker == -1 (free slot)
    int v4 = PersonSumCurrencyHeld(currency);         // liquid currency
    for (const OwnedBuildingWorth& b : ownedBuildings)
        v4 += b.roomWorth + b.storageWorth;           // per owned building
    return v4;
}
```

In words:

```
totalWealth(person) =
    SumCurrencyHeld(person)
  + Σ over owned buildings b of:
        ComputeRoomWorth(b, b.kind>>24)
      + SumStorageItemWorth(b)
```

Guards: returns **−1** if `personIndex ≥ 0x300` (768, the person-array cap) or if
the slot is free (record marker word `+0` == −1).

### 4.3 The per-building worth terms

Each owned building contributes two terms (`OwnedBuildingWorth`,
`inventory_wealth.h:74-77`), both ported in `building_storage.cpp`:

- **`SumStorageItemWorth`** (`gilde.exe 0x591658`, `building_storage.cpp:39-47`):
  the running, integer-truncated sum of `Building_ComputeMarketPrice(prot, qtyByte)`
  over every item in the building's main storage room.

  ```c
  int v5 = 0;
  for each item: v5 = truncToZero( ComputeMarketPrice(it.prot, it.qtyByte) + v5 );
  return v5;
  ```

- **`ComputeRoomWorth`** (`gilde.exe 0x59116c`, `building_storage.cpp:89-…`):
  starts from a base `v19 = 3840 * typeDef.roomWorthMul`, walks the type's room
  list adding item market price for storage rooms (kind 2/6), and finally scales by
  `mul · 0.01` (`flt_626A10 = 0.009999999776482582`). The `mul` passed by
  total-wealth is `building.kind >> 24` (`inventory_wealth.h:75`).

> Note: a related but *separate* aggregator, `ComputeStockValue`
> (`gilde.exe 0x590360`, `building_storage.cpp:52-81`), produces an owner-share +
> stock-worth pair. Its owner-share term itself calls `ComputeTotalWealth`
> (`ownerShare = clamp(ownerWealth, 2560000) * (kind==2 ? 0.09 : 0.04)`), so there is
> a value cycle between building appraisal and owner wealth. `ComputeStockValue` is
> **not** part of `ComputeTotalWealth`'s sum — total wealth uses `ComputeRoomWorth +
> SumStorageItemWorth`.

---

## 5. Item transfer mechanics

### 5.1 Simulation-side add / remove

The reimplementation's carried-item model routes every mutation through a command
hook (`InventoryCommandFn`, `inventory.h:79-80`) so a real backend can serialize /
veto, mirroring the binary's lockstep command system
(`ExSetObjectField`-style stock changes).

`InventoryAdd` (`inventory.cpp:160-174`):

1. `room = InventoryFreeSpace(inv, type, INT_MAX)` — clamp to slot capacity.
2. `add = min(qty, room)`; bail if ≤ 0.
3. Call `cmdHook(inv, type, +add)` **before** the local apply.
4. Get-or-create the stack, `count += add`. Returns the amount actually added.

`InventoryRemove` (`inventory.cpp:176-190`):

1. Find the stack; `rem = min(qty, have)`; bail if ≤ 0.
2. Call `cmdHook(inv, type, −rem)`.
3. `count = have − rem` (record kept even at 0). Returns the amount removed.

A transfer between two containers is therefore `min(srcRemovable,
dstFreeSpace)` units, with `dstFreeSpace` computed by the capacity engines of
section 2 (`ComputeFreeSpaceForItem` / `ComputeFreeCapacity` / `ComputeCarryCapacity`
depending on the destination kind).

### 5.2 The drag-slot stacking table (UI carry bar)

When the player drags goods between an inventory/stall grid and the carry bar, the
binary accumulates `(prototype, quantity)` pairs into a fixed **6-entry** table
based at `dword_75B9F0`. Each entry is a 12-byte (3-dword) record
(`dragslot.h:49-61`):

```
slot[i] @ dword_75B9F0 + 12*i :
  +0  key   (dword)  item prototype id   (-1 == free slot)
  +4  accum (dword)  accumulated quantity
  +8  gfx   (dword)  icon/widget rider   (NOT touched by the stacking mutators)
```

Four mutators (`dragslot.cpp`), all preserving the original's raw x86 return
registers:

| Function | addr | semantics | returns |
|----------|------|-----------|---------|
| `DragSlotAddItem` | `0x41f880` | **accumulate** qty into matching-or-first-free slot | dword index `3*slot`, or 6 if full, or untouched `key` on qty==0 |
| `DragSlotStoreItem` | `0x41f95c` | **overwrite** matching-or-first-free slot; qty==0 frees it | slot index `0..6` |
| `DragSlotRemoveItem` | `0x41f900` | subtract qty; free slot (key=−1) when total hits exactly 0 | byte offset `12*slot` |
| `DragSlotResetTable` | `0x41f9dc` | clear all: key=−1, accum=0 | 72 |
| `DragSlotCountUsed` | `0x41fa00` | number of slots with key≠−1 | count |

`AddItem` accumulates, `StoreItem` overwrites — that distinction matters: the
in-progress drag amount is set via `StoreItem`, while incremental drags add up via
`AddItem`. The icon/widget side (the `gfx` rider) is set by the GUI and is never
read or written by these stacking mutators.

### 5.3 GRID-UI reconciliation (inventory2)

`src/sim/inventory2.cpp` ports the widget-coupled inventory/workstation grid:
`OpenSlotWindow` (`0x54ebd8`), `RefreshSlots` (`0x54ecb0`), `RenderItemGrid`
(`0x54f8dc`), the grid-surface create/destroy, and `HandleUseChoice` (`0x567538`).
These reconcile the **6 visible icon slots** (`kInvSlotCount = 6`) and the
per-workstation grids (`kGridStations = 32`, `kGridSlotsPerWs = 6`) against the
container's live item nodes, re-staging each populated slot into the drag table via
the `dragStore` / `dragAdd` hooks. The icon id for a slot is `type + 206`
(`kSlotIconBase`, `inventory2.h:49`). The production-fill bar for an in-progress
item is `1 − progress28 / slotCapacityDword` (`inventory2.cpp:198-201`), and
`HandleUseChoice` translates a "consume" choice into a command arg of
`item.magnitude28 * 0.1` (`kUseSpeedScale`, `inventory2.cpp:424`). This layer is
purely presentational/staging — it does not itself change stock counts; mutations
flow through the command system (section 5.1).

---

## 6. Constants & magic numbers (quick reference)

| Constant | Value | Where |
|----------|-------|-------|
| reserve goods (count−1) | 42, 278, 475, 476 | `types.h:190-194` |
| high-cap good | 477 → `5·level+10` | `inventory.cpp:53-54` |
| level-3 capacity override | 80 | `inventory.cpp:55-56` |
| default capacity | `20·level` | `inventory.cpp:57` |
| carry slots / per-stack carry cap | 6 slots / 3 units | `inventory_capacity.cpp:185-191` |
| money type-def category | 9 (uncapped carry) | `inventory_capacity.cpp:169` |
| carry-disallowed protos | 377, 378 | `inventory_capacity.cpp:171-172` |
| currency proto resolver | `dword_13CD6F2[189*player] >> 16` | `inventory_wealth.h:46` |
| currency-stack amount field | node `+0x0E`/`+14` | `inventory_wealth.cpp:43,59` |
| Person cash word | record `+0x0A` (u16) | `types.h:70`, `person.cpp:59` |
| person cap (wealth guard) | 0x300 = 768 | `inventory_wealth.cpp:74` |
| `ComputeRoomWorth` base | `3840 · roomWorthMul` | `building_storage.cpp:94` |
| room-worth scale | `mul · 0.01` (`flt_626A10`) | `building_storage.cpp:12,87` |
| owner-wealth cap (StockValue) | 2,560,000 (`flt_6269D4`) | `building_storage.cpp:8` |
| drag-slot table | 6 entries × 12 bytes @ `dword_75B9F0` | `dragslot.h:49-61` |
| slot-icon id bias | `type + 206` | `inventory2.h:49` |
| inventory icon slots | 6 | `inventory2.h:50` |

---

## 7. Open questions / flagged unknowns

- **Cash word vs. currency stack sync** — the relationship between the Person
  `+0x0A` cash word and the currency-stack `+14` amount is not established by the
  in-scope files (section 3.2).
- **`fill28` / `fill29` semantics** — these container bytes (`+28`/`+29`) act as
  slot-count / category gates, but their full meaning (and why type 42 uses both)
  is inferred from the branch structure, not from an independent definition.
- **`Inventory::kMaxItems = 64`** is a reimplementation modeling bound, not a
  recovered game limit; real stacking is bounded by slot capacity.
- **`ComputeStockValue` ↔ `ComputeTotalWealth` cycle** — appraisal of a building's
  stock value pulls in the owner's total wealth; the termination/ordering of this
  recursion in the live game is outside these files.
</content>
</invoke>
