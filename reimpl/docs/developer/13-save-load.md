# 13 — Save / load & serialization

How `gilde.exe` writes and reads a `.SAV` savegame. This is the on-disk format and
the exact field/table order a 1:1 save-compatible reimplementation must reproduce.
Everything below is from the IDA decompilation of the original binary (imagebase
`0x400000`); the binary is the source of truth.

The load path is `VIBE_Save_LoadGameFile @0x5a7604`; the write path is
`VIBE_Save_WriteGameFile @0x5a348c`. They are mirror images: the writer emits the
sections in one order and the loader reads them back in the **same order**, gated by
the same per-record format-version checks. All I/O goes through the VFS stream layer
(`VIBE_Vfs_OpenFile @0x450bc8`, `VIBE_Vfs_ReadStream @0x4514ac`,
`VIBE_Vfs_WriteStream @0x4517a8`, `VIBE_Vfs_CloseStream @0x451354`) — see
[29 — VFS & compression](29-vfs-fileio-compression.md). The file is opened binary
(`"rb"` @0x627f04 for load, `"wb"` @0x627ef8 for save).

> **Endianness / packing.** This is a 32-bit x86 build; all multi-byte fields are
> little-endian and structs are written field-by-field (the writers stream individual
> members, not whole structs), so there is no implicit struct padding in the file —
> the byte layout is exactly the sum of the streamed field sizes in order.

---

## 1. The format version

A single 32-bit **format version** drives every conditional in both reader and writer.
The writer hard-codes the current version and the reader stores the file's version in
the global `dword_649D4C`:

- Current/maximum version written: **`0x10045`** (decimal 65605), set in
  `VIBE_Save_WriteScenarioBlock @0x5a372c` (`v27 = 65605; dword_649D4C = 65605`).
- Accepted range on load (`VIBE_Save_LoadGameFile @0x5a765e`): version must be
  `>= 0x10026` and `<= 0x10045`, else the load aborts.
- The version is read first, inside `VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0`
  (`dword_649D4C = *(_DWORD*)a2;` at +0x5a7b49), then used throughout to skip fields
  that did not exist in older saves.

Known version gates referenced in load (`dword_649D4C` compared against):

| Version | What it added (field read only at/above this version) |
|---|---|
| `0x10014` | extra building-record fields (offsets +84..+104) |
| `0x10016` | map-tile name tables (`VIBE_Save_LoadMapTiles`) |
| `0x10022` | extra global `byte_6477A8` (4 bytes) |
| `0x10025` | minimum for thumbnail tail block to be valid |
| `0x10028` | extended header block (offsets +50..+89) |
| `0x10029` | trailing hotkey load (`VIBE_Hotkey_LoadTable`) |
| `0x1002A` | avatar block (`VIBE_Avatar_Load`) |
| `0x1002B` | header fields +92/+96 |
| `0x1002E` | hotkey table (`VIBE_Save_LoadHotkeyTable`) |
| `0x10030` | global `dword_649894` (4 bytes) |
| `0x10033` | header byte +49 |
| `0x10034` | header 8×i32 array at +100 |
| `0x10038` | header i32 at +132 |
| `0x10039` | header 0x60-byte name block at +136 (else copied from `"Savegame"`) |
| `0x1003D` | global `dword_632240` (else defaulted to `1000000`) |
| `0x10045` | offices/Ämter ordering moves earlier (`VIBE_Amt_LoadAemter`) |

---

## 2. `.SAV` file layout (ordered sections)

Top level: a **header + thumbnail block**, then a **scenario block**, then a long run
of **world tables**, then trailing **character / mission / hotkey** blocks. The table
below is the canonical order as emitted by `VIBE_Save_WriteGameFile` and consumed by
`VIBE_Save_LoadGameFile`.

| # | Section | Writer | Loader | Notes |
|---|---|---|---|---|
| 0 | **Header + thumbnail** | (part of scenario block, see below) | `VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0` | version, slot meta, 320×240 thumbnail |
| 1 | **Scenario block** (magic `"SCENARIO"`, version, player snapshot, palette, 320×240 thumbnail) | `VIBE_Save_WriteScenarioBlock @0x5a372c` | (header part read by #0; rest read inline by loader) | written first by the writer |
| 2 | **Global scalars** (money/time/seed/etc.) | inline in `WriteGameFile` | inline in `LoadGameFile` | 9–11 small globals + 24-byte blob |
| 3 | **Map-tile table** | `VIBE_Save_WriteMapTileTable @0x5a3fa4` | `VIBE_Save_LoadMapTiles`-family (person/city loaders) | count-prefixed, 67-byte stride |
| 4 | **Person table** | `VIBE_Save_WritePersonTable @0x5a4134` | `VIBE_Save_LoadPersonIndexTable @0x5a7ffc` + `VIBE_Save_LoadPersonTable @0x5a8190` | 169-byte records + inventory |
| 5 | **Building table** | `VIBE_Save_WriteBuildingTable @0x5a45bc` | `VIBE_Save_LoadCityRecords @0x5a8d3c` | fixed 16 buildings, 164-byte stride |
| 6 | **Game-state / character header** | `VIBE_Save_WriteGameStateHeader @0x5a4938` | `VIBE_Save_LoadGlobalCounters @0x5a86d0` | active char roster, 268-word stride |
| 7 | **Building-slot tables** | `VIBE_Save_WriteBuildingSlotTables @0x5a5c1c` | `VIBE_Save_LoadBuildingSlotTables @0x5aa058` | 5×62 slots + 4×756-byte workshop recs |
| 8 | **Offices / Ämter** | `VIBE_Amt_SaveAemter @0x483198` | `VIBE_Amt_LoadAemter @0x4832d0` | order depends on version `0x10045` |
| 9 | **Laws** (skipped for the lightweight branch) | `VIBE_Gesetz_SaveState @0x4c25e0` | `VIBE_Gesetz_LoadState @0x4c28d8` | guild/city laws |
| 10 | **Map tiles (name strings)** | `VIBE_Save_WriteMapTiles @0x5a6258` | `VIBE_Save_LoadMapTiles @0x5aa7a8` | two 768×768 byte planes; `>=0x10016` |
| 11 | **Game globals** | `VIBE_Save_WriteGameGlobals @0x5a6324` | `VIBE_Save_LoadGameGlobals @0x5aa8dc` | economy/weather scalars + arrays |
| 12 | **Avatar** | `VIBE_Avatar_Save @0x484658` | `VIBE_Avatar_Load @0x4846ec` | `>=0x1002A` |
| 13 | **Object table** | `VIBE_Save_WriteObjectTable @0x5a6854` | `VIBE_Save_LoadObjectTable @0x5aae40` | 1024 objects + 256 + 256 sub-tables |
| 14 | **Amt table** | `VIBE_Save_WriteAmtTable @0x5a6e1c` | `VIBE_Save_LoadAmtTable @0x5ab3ac` | 96 records, 69-byte stride |
| 15 | **History & carts** | `VIBE_Save_WriteHistoryAndCarts @0x5a6ff4` | `VIBE_Save_LoadHistoryAndCarts @0x5ab59c` | chronicle counters + 4 cart slots |
| 16 | **Action queues** | `VIBE_Save_WriteActionQueues @0x5a714c` | `VIBE_Save_LoadActionQueues @0x5ab704` | two linked-node pools + event log |
| 17 | **Hotkey table** | `VIBE_Save_WriteHotkeyTable @0x5a7520` | `VIBE_Save_LoadHotkeyTable @0x5aba98` | 24 hotkeys; `>=0x1002E` |
| 18 | **City & person object tables** | `VIBE_Save_WriteCityAndPersonTables @0x5a57f4` | `VIBE_Save_LoadCharacters @0x5a986c` | per-object records, pointer-relocated |
| 19 | **Missions** | `VIBE_Mission_SaveSlotTable @0x53b148` | `VIBE_Mission_LoadSlotTable @0x53b25c` | quest/mission slots |
| 20 | **Hotkeys (trailing)** | `VIBE_Hotkey_SaveTable @0x4ff590` | `VIBE_Hotkey_LoadTable @0x4ff634` | `>=0x10029` |

> The writer's `(a4 & 2)` flag (passed to `WriteGameFile`) selects a **lightweight /
> scenario-only** save: when set, it stops after section 8 and the loader, seeing
> `byte_13CEC94 & 2`, likewise closes the file after building slots, relinks pointers,
> and returns. This is used for the `.SRV` network scenario snapshot, not full saves.

After all sections, the loader performs a **post-load fix-up pass** (section 21
below), which is where pointer relocation and scene re-init happen.

---

## 3. Header + thumbnail — `VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0`

This populates the in-memory header buffer at `&dword_13CEC90` (the loader passes
`a2 = &dword_13CEC90`). Read order (offsets are into that header buffer; sizes in
bytes):

| Off | Size | Field | Gate |
|---|---|---|---|
| +0 | 4 | **format version** → also stored in `dword_649D4C` | always |
| +4 | 1 | flags byte (`byte_13CEC94`; bit 1 = lightweight/scenario save) | always |
| +5 | 32 | savegame display name | always |
| +40 | 8 | timestamp / id | always |
| +48 | 1 | byte | always |
| +49 | 1 | byte | `>=0x10033` |
| +50 | 14 | game-time blob (`qword`/struct) | `>=0x10028` |
| +64 | 16 | blob | `>=0x10028` |
| +80,+81,+82 | 1 each | bytes | `>=0x10028` |
| +84 | 4 | i32 | `>=0x10028` |
| +88,+89 | 1 each | bytes | `>=0x10028` |
| +92 | 4 | i32 (`*((i32*)a2+23)`; else set to `-1`) | `>=0x1002B` |
| +96 | 4 | i32 (`*((i32*)a2+24)`; else set to `-1`) | `>=0x1002B` |
| +100 | 4×8 | i32 array (loop, 8 entries) | `>=0x10034` |
| — | 0xE100 | **thumbnail** (57600 bytes RGB, 160×120 of 3-byte texels) into temp buffer `ls:tmp`, unpacked via `VIBE_Result_Handler_Final @0x434f30` into `word_13CED76` | `>=0x10028` |
| +132 | 4 | i32 (else defaulted to `2`) | `>=0x10038` |
| +136 | 0x60 | save name block (else byte-copied from constant `"Savegame"` @0x627f08) | `>=0x10039` |

The thumbnail is a 160-pixel × 120-row image of packed 3-byte texels (`0xE100 =
57600 = 160*120*3`). On **save** the matching code in
`VIBE_Save_WriteScenarioBlock` builds the 57600-byte buffer by calling
`VIBE_Render_UnpackColor @0x434f7c` per pixel over `word_13CED78` and writes it with
`VIBE_Vfs_WriteStream(buf, 57600, ...)`.

If `dword_649D4C < 0x10025` the header is considered too old and the load fails.

---

## 4. Scenario block — `VIBE_Save_WriteScenarioBlock @0x5a372c`

This is the **first thing written** to a full save (it owns the version word and the
thumbnail). Write order:

1. `i32` version = `0x10045` (also stored to `dword_649D4C`).
2. `1` byte — the `a2` save-mode flag.
3. `32` bytes — active player's name (string, copied from
   `&byte_13CD6A0[756 * byte_6477A1]`, i.e. the player's workshop/dynasty record).
4. `8` bytes — two i32 from that record (+0x40, +0x44).
5. `1` byte — record +0x48.
6. `1` byte — `byte_63CC1D` (network/player slot id).
7. `14` bytes — **game time** `qword_13CE852` (the packed clock; see
   [15 — Game time](15-game-time-tick.md)).
8. Magic string `"SCENARIO"` + 8 trailing bytes (`v25`, 16 bytes total) — but only
   when there is **no active character** (`word_63CC5C == -1`); otherwise the 16 bytes
   are the active character's snapshot from `word_12CE910[268*idx + 24]`.
9. When a character is active: more per-character fields (bytes at +178, +179, +6.5,
   total wealth via `VIBE_Person_ComputeTotalWealth @0x591f7c` →
   `VIBE_Money_ConvertToDisplayCoord @0x58f14c` as i32, byte +4.5). When inactive:
   the placeholder fields `v39/v40/v37/v28(i32)/v36`.
10. `1` byte — `byte_649D50` (quicksave flag).
11. `4` bytes — selected/primary character id (`*(dword_631744+1)` else
    `*(dword_631748+1)` else `-1`).
12. `4` bytes — secondary selection id (`*(dword_63174C+2)` else `-1`).
13. A run of `4`-byte words copied from `dword_13CEC54` up to `&byte_13CEC94`
    (the header scalar block, including the format-flags byte region).
14. The **thumbnail**: 57600 bytes built per-pixel (see §3) then freed.
15. `4` bytes — `dword_63C744`.
16. `96` bytes — `byte_122F198` (current save path / level name string buffer).

`algn_5A3429` (the 7 bytes appended after `"SCENARIO"`) are all zero in the binary
(verified with `get_bytes` — `00 00 00 00 00 00 00`).

---

## 5. Global scalars (inline in `WriteGameFile`/`LoadGameFile`)

Immediately after the scenario block, both functions stream these globals in this
exact order:

| Size | Symbol | Gate | Meaning |
|---|---|---|---|
| 4 | `dword_649890` | always | — |
| 4 | `dword_632244` | always | — |
| 14 | `qword_13CE852` | always | game time (again; mirrored to `qword_122F840`) |
| 1 | `byte_6477A1` | always | active player slot |
| 4 | `dword_6498E4` (writer uses `dword_6498E4+4`) | always | player record id |
| 4 | `dword_64771C` | always | — |
| 4 | `dword_647720` | always | — |
| 4 | `dword_647724` | always | — |
| 4 | `byte_6477A8` | `>=0x10022` | — |
| 24 | `dword_B56450` | always | 6-i32 blob |
| 4 | `dword_649894` | `>=0x10030` | — |
| 4 | `dword_632240` | `>=0x1003D` (else `1000000`) | starting/seed money |

The loader copies the just-read `qword_13CE852` into the live clock mirror
(`qword_122F840`, `unk_122F848`, `unk_122F84C`), and copies `dword_13CED14` into
`dword_63C744` unless the lightweight flag (`byte_13CEC94 & 2`) is set.

---

## 6. Map-tile table — `VIBE_Save_WriteMapTileTable @0x5a3fa4`

Count-prefixed. The base array is at `dword_13CE290`, stride **67 bytes**, total span
`548864` bytes (= 8192 tiles × 67).

1. `i32` count = number of tiles whose first `WORD` is non-zero.
2. For each non-empty tile, in array order, write: `2` (id) · `4` (+2) · `4` (+6) ·
   `4` (+10) · `4` (+14) · `1` (+18) · `1` (+19) · `31` (+28).

(The decompiled load counterpart for this lives in the city/person loader family;
the record field order is identical.)

---

## 7. Person table — `VIBE_Save_WritePersonTable @0x5a4134`

Base `dword_13CE298`, stride **169 bytes**, span `43264` (= 256 persons × 169).

1. `i32` count = persons with non-zero +0 (alive flag).
2. Per live person, write the fields in this order (offset · size):
   +0·1 (alive), +1·4, +5·32 (name), +37·2, +39·2, +41·2, +43·4, +47·1, +52·1,
   +53·4, +57·4, +61·4, +65·4, +69·4, +73·4, +90·2, +92·1.
3. If `person[0] == 30` (a "trader/cart" person type): write a 64-entry inventory at
   `*(person+113)`, stride **24 bytes** (1536 total): per entry +0·4, +4·4, +8·1,
   +9·1, +10·2, +16·4, +12·1, +13·1. Otherwise write `48` bytes at +101.
4. Always: `16` bytes at +153.
5. After the loop: `i32` `dword_1234604` (a secondary record count), then
   `dword_1234604` records of **96 bytes** each from `dword_1234600` (cloned/compacted
   via `VIBE_Object_CloneOrFreeData @0x5f1d00`): each split into `64`+`16`+`16`.

Load side: `VIBE_Save_LoadPersonIndexTable @0x5a7ffc` then
`VIBE_Save_LoadPersonTable @0x5a8190` (index table first, then the records).

---

## 8. Building table — `VIBE_Save_WriteBuildingTable @0x5a45bc`

Fixed **16 buildings** (no count prefix). Two parallel arrays: `word_13C3110`
(stride 164, the `82*v2` word index) and `unk_13C3180` (stride 164, the 14-byte
block `i`). Per building, in order:

- +0·2, +2·16, +20·4, +40·4, +28·4, +32·4, +44·4, +48·4, +52·4, +56·4, +64·4, +68·4,
  +72·4, +76·4, +80·4
- **If `version >= 0x10014`:** +84·4, +88·4, +92·4, +60·4, +96·4, +100·4, +104·4
- 14 bytes from `unk_13C3180[i]`
- +128·4, +132·16, +148·12, +160·4, +108·4

Load: `VIBE_Save_LoadCityRecords @0x5a8d3c`.

---

## 9. Game-state / character header — `VIBE_Save_WriteGameStateHeader @0x5a4938`

The active-character roster. Base `word_12CE910`, stride **268 words** (536 bytes),
slot span `205824` bytes = 384 slots. Order:

1. `2` bytes — `word_63CC5C` (active character index, `-1` if none).
2. `i32` count = slots whose first word ≠ `-1`.
3. `i32` — primary player id (`*(dword_6498E8+4)`, else `-1`).
4. `i32` — secondary id (`*(dword_6498EC+4)`, else `-1`).
5. 8× `i32` — extra player ids from `dword_6498F0[0..7]` (each `*(p+4)` or `-1`).
6. For each active slot (`word_12CE910[v5] != -1`), a large per-character record. Notable:
   id `2`, +520·4, +2·1, +4·4, +8·1, +9·1, +10·2, +12·1, +13·1, +16·4, +20·4, +24·4,
   +28·4, +32·4, +36·4, +40·2, +44·4, +48·16, +64·16, +80·2, then a **rebased money
   field** (`*(p+21) - 1342`), +88·1, +92·32, +124·4, +128·5, +136·168, +356..+361·1,
   two **pointer→id** fields (`*(p+91)` and `*(p+92)`, writing `*(ptr+1)` or `-1`),
   +372·4, another pointer→id (`*(p+95)` → `*(ptr+4)` or `-1`), +384·1, a second
   rebased field (`*(p+99) - 1468`), +400..+428·4, +432·1, +433·1, +436·16, +456·4,
   +460·4, +464·16, +480..+492·4, +453·1, a final pointer→id (`*(p+97)` → `**ptr` or
   `-1`), +496·24, +524·4, +528..+532·1.

The `-1342` and `-1468` subtractions and the pointer→index conversions are the
**relocation** applied at save time; the loader (`VIBE_Save_LoadGlobalCounters
@0x5a86d0`) and the post-load relink (§21) reverse them.

Load: `VIBE_Save_LoadGlobalCounters @0x5a86d0` (despite the name, this is the
roster/state-header reader) and `VIBE_Save_LoadCharacters @0x5a986c` /
`VIBE_Save_LoadCharacterSlot @0x5a96c0`.

---

## 10. Building-slot / workshop tables — `VIBE_Save_WriteBuildingSlotTables @0x5a5c1c`

Two nested table groups (no per-group counts; fixed dimensions):

**Group A — `dword_13C3B50`, 5 blocks × 7952 bytes.** Per block: header `4·4·4·4`,
then **62 slots** of stride 128: per slot +0·2, +4·4, +8·4, +12·4, +32·4, +36·4,
+48·4, +52·4, +56·4, +60·2, +44·4.

**Group B — `byte_13CD6A0`, 4 records × 756 bytes** (the workshop/dynasty records,
also referenced by the scenario block). Per record: +0·32 (name), +64·8, +72·1,
+76·4, +80·4, +84·2, +88·8, +96·1, +97·1, then **10× 8-byte** entries at +100,
**10× 8-byte** at +180, +260·1, +261·1, **4× 4-byte** at +264, **4× 4-byte** at +280,
+296·4, **11× 1-byte** at +300, +311·1, **7× 1-byte** at +312, +319..+322·1,
**8× 18-byte** entries at +324, +468·4, +472·4, +476·208, +748·8.

Load: `VIBE_Save_LoadBuildingSlotTables @0x5aa058`.

---

## 11. Map tiles (name planes) — `VIBE_Save_WriteMapTiles @0x5a6258`

Only at `version >= 0x10016`. Two **768×768 byte planes** written row-by-row:

- Plane 1: base `(char*)dword_123D6CD + 3`, 768 rows of 768 bytes.
- Plane 2: base `byte_1333110`, 768 rows of 768 bytes.

Load: `VIBE_Save_LoadMapTiles @0x5aa7a8`.

---

## 12. Game globals — `VIBE_Save_WriteGameGlobals @0x5a6324`

A flat list of economy/weather scalars, then arrays. In order:

`dword_1234910`·4, `dword_1234914`·4, `dword_1234918`·4, `dword_123491C`·4,
`dword_1234920`·4, `dword_1234924`·4, `flt_1234928`·4, `dword_123492C`·4,
`dword_1234930`·4, `dword_1234934`·4, `flt_1235234`·4, `qword_1235262`·4 +2 +4
(split write), `byte_123526C`·4, `flt_641DA8`·4, `flt_641DAC`·4, `dword_1235238`·4,
`byte_123523C`·4, `byte_1235240`·4, `byte_1235244`·4, `byte_1235248`·4,
`byte_123524C`·4, `byte_1235250`·4, `dword_1235254`·4, `byte_1235258`·4.

Then **16 records of 20 bytes** from `flt_1234FA0` (per record: 4·4·4·4·4); then five
**68-byte** blocks (`dword_12350E0`, `byte_1235124`, `byte_1235168`, `byte_12351AC`,
`byte_12351F0`); then an **8×8 grid** at `word_12349A0` (outer step 96, inner step 12):
per cell +0·2, +2·2, +4·1, +8·4.

Load: `VIBE_Save_LoadGameGlobals @0x5aa8dc`.

---

## 13. Object table — `VIBE_Save_WriteObjectTable @0x5a6854`

Three count-prefixed sub-tables:

**13a — Objects.** Base `byte_11D6040`, stride 332, 1024 entries.
1. `i32` count (non-empty), 2. `i32` constant `160` (column/width marker). Per live
object: +0·1, +4·4, +8·2, +12·4, +16·4, +20·48, +68·14, +82·14, +96·14, +112·4,
+120·1, then **8× 4-byte** at +140; then either (+128·4 + a variable-length blob of
`*(p+32)` bytes at `*(p+31)`) or a `0`-i32 placeholder; then +172·160.

**13b — 256 records**, base `dword_11CB620`, stride 41 (in words). `i32` count, then
per live entry: +4·4, +1·1, +2·4, +3·4, +4·4, +5·14, +(8 words+2)·128.

**13c — 256 records**, base `byte_11C6560`/`dword_11C6568`, stride 80. `i32` count,
then per live entry: +0·1, +4·4, +8·4, +12·4, +16·64.

Load: `VIBE_Save_LoadObjectTable @0x5aae40`.

---

## 14. Amt (offices) table — `VIBE_Save_WriteAmtTable @0x5a6e1c`

Base `dword_11AE6B0`, stride **69 bytes**, span `6624` (= 96 records). `i32` count
(entries with +0 ≠ `-1`), then per live record: +0·4, +8·1, +20·4, +38·1, +48·1,
**+52·64 written as 16 elements** (`VIBE_Vfs_WriteStream(v5+52, 64, a1, 16)` — note
the element-count `16`, i.e. 16×4), +116·1, +124·152, +24·14, +12·4.

Load: `VIBE_Save_LoadAmtTable @0x5ab3ac`.

(Distinct from the guild-office state in section 8, which goes through
`VIBE_Amt_SaveAemter @0x483198` / `VIBE_Amt_LoadAemter @0x4832d0`.)

---

## 15. History & carts — `VIBE_Save_WriteHistoryAndCarts @0x5a6ff4`

1. `1` byte — `byte_633924` (history head flag).
2. `i32` — `dword_633928 - dword_63391C` (history ring write-index, **rebased** to a
   relative offset).
3. `i32` — `dword_633934 - dword_63392C` (second ring index, rebased).
4. **16** bytes from `byte_122DBF0` (one byte at a time).
5. **4 cart slots** (`dword_122DAE0`, stride 68): per slot a leading `i32`, then
   **8 sub-entries** of stride 8 (each +0·4, +4·1).

The two rebased indices are stored relative so the chronicle ring survives
reallocation; the loader re-adds the live base.

Load: `VIBE_Save_LoadHistoryAndCarts @0x5ab59c`.

---

## 16. Action queues — `VIBE_Save_WriteActionQueues @0x5a714c`

Two **linked-node pools** plus a flat event log, with all `next`/`prev` pointers
converted to **node indices** at write time:

**Pool 1 — `byte_1078360`, stride `0x99` (153), 8192 nodes** (span `1253376`).
For each node: copy the 153-byte record, then overwrite its two link fields (+145,
+149) with `(ptr - base)/0x99` (or `-1` if null). After the loop, write the pool's
head/tail pointers as indices: `dword_11AA498`, `dword_11AA49C` (each as relative
index or `-1`).

**Pool 2 — `unk_BAFB60`, stride `0x99`** (same scheme, count derived from
`&loc_4C7FFE + 2`). Then head index `dword_11AA46C` (relative or `-1`) and raw
`dword_11AA494`.

**Event log — `byte_B5FB60`, 0x8000 (32768) records of 10 bytes each.**

Tail scalars: `dword_11AA484`·4, `dword_11AA47C`·4, `dword_631288`·4,
`dword_63128C`·4, `dword_631290`·4.

Load: `VIBE_Save_LoadActionQueues @0x5ab704`. If it fails, the loader closes the
stream, relinks pointers, and returns 0.

---

## 17. Hotkey table — `VIBE_Save_WriteHotkeyTable @0x5a7520`

`version >= 0x1002E` on load. `i32` `dword_11BC1C0` (count/flags), then **24 hotkeys**:
per hotkey 4 parallel i32 from `dword_11BC038`, `dword_11BC098`, `dword_11BC100`,
`dword_11BC160`.

Load: `VIBE_Save_LoadHotkeyTable @0x5aba98`. (A second, trailing hotkey block —
section 20, `VIBE_Hotkey_SaveTable @0x4ff590` / `VIBE_Hotkey_LoadTable @0x4ff634` —
is gated on `>=0x10029`.)

---

## 18. City & person object tables — `VIBE_Save_WriteCityAndPersonTables @0x5a57f4`

This writes the heavy game **object records** (the things characters carry/own) with
explicit pointer relocation. Three passes:

1. **City objects** — `dword_12CEA94`, stride 134, 768 slots. `i32` count, then per
   live slot `VIBE_Save_WriteObjectRecord @0x5a55b0`.
2. **Map "type 29" objects** — walk the map-tile array `dword_13CE290` (stride 67),
   count those whose tile-type (`dword_13CE27C + 65*tileWord`) `== 29`, write the
   count, then a `WriteObjectRecord` per match.
3. **Object pool `dword_62CEFC`, stride 404, 1280 records.** Before writing, each
   record's pointer fields are converted to indices: `v[5]` (a back-pointer is
   dereferenced), and `v[9]`,`v[10]` become `(ptr-base)/404` or `-1`. Each record
   then writes: flag·1 (with an optional inline copy when the linked sprite has data),
   +12·4, +20·4, +48·32, +80·32, +240·160, +400·1, +36·4, +40·4. **After** the write
   loop the same fields are **restored** in place (indices → live pointers via
   `base + 404*idx`), so the in-memory state is unchanged by saving.

Load: `VIBE_Save_LoadCharacters @0x5a986c` (+ per-slot `VIBE_Save_LoadCharacterSlot
@0x5a96c0`). After loading, the master loader re-resolves the two stored player ids
through `VIBE_Person_FindRecordById @0x58bc6c`.

---

## 19–20. Missions and trailing hotkeys

- **Missions:** `VIBE_Mission_SaveSlotTable @0x53b148` / `VIBE_Mission_LoadSlotTable
  @0x53b25c` — the quest/mission slot table.
- **Trailing hotkeys:** `VIBE_Hotkey_SaveTable @0x4ff590` /
  `VIBE_Hotkey_LoadTable @0x4ff634`, only when `version >= 0x10029`.

---

## 21. Post-load fix-up (loader only)

After all sections read successfully, `VIBE_Save_LoadGameFile` runs the relink/scene
re-init tail (this has **no writer counterpart** — it reverses the save-time
relocations and rebuilds derived state):

1. `VIBE_Save_RelinkLoadedPointers(1, &word_13CE860) @0x5abb84` — converts all the
   saved indices back into live pointers (persons, objects, action-queue nodes,
   character record pointer fields, etc.). Called for both the lightweight and full
   branches.
2. `VIBE_Universe_SwitchActiveSlot(0, 1, …) @0x5b4a24` — activates the loaded world.
3. `VIBE_Scene_RefreshBuildingEffects @0x504910`, `VIBE_Light_EnableDaylight
   @0x504a00` — rebuild scene/lighting (see [12 — Session init](12-session-init-worldload.md)).
4. `VIBE_Save_LoadCharacters @0x5a986c`, then re-resolve `dword_6498E8` /
   `dword_6498EC` ids via `VIBE_Person_FindRecordById @0x58bc6c`.
5. `VIBE_Mission_LoadSlotTable`, optional `VIBE_Hotkey_LoadTable`.
6. `VIBE_Character_UpdateAllFlags @0x4b63c0`, another
   `VIBE_Scene_RefreshBuildingEffects`, `VIBE_Scene_ActivateAndRefreshCharacters
   @0x506df4`, `VIBE_Object_UpdateBuildingVisualState @0x506b68`, and a final copy of
   the save name (`unk_13CED18` → `byte_122F198`).

On **any** sub-loader failure the loader jumps to `LABEL_51`: `VIBE_Vfs_CloseStream`
+ `return 0`. The on-disk file is never partially mutated by a failed load.

The pre-load teardown (run before reading anything) is:
`VIBE_Building_ResetAllBuildings @0x5896fc`, `VIBE_CharAction_QueueFreeAll @0x40c120`,
clear `dword_6498E4/E8/EC`, and `VIBE_Object_DestroySpawnedEntities @0x4fff10`.

---

## 22. Save file paths & triggers

### Path templates

| Trigger | Path / template | Where |
|---|---|---|
| Quicksave (single-player) | `Gamedata\Saves\Quicksave.SAV` (@0x6252b8) | `VIBE_Save_DoQuickSave @0x56d984` |
| Numbered manual save | `GILDE_SAVEGAME_%i` (@0x61e55c) formatted via `VIBE_Crt_Sprintf_0 @0x5cba00` | frame loop @0x4c17f7 |
| Network saved game (load) | `gamedata/network/%s.SAV` (@0x620eac) | `VIBE_Net_LoadSavedNetworkGame @0x50442c` |
| Network scenario snapshot | `gamedata/network/%s.SRV` (@0x620ec4) | same |

`VIBE_Menu_RunSaveGame @0x56a804` is the manual save-game menu that calls
`VIBE_Save_WriteGameFile`; `VIBE_Map_LoadCityFile @0x528bd0` and
`VIBE_GameLogic_InitOrLoadSession @0x533a54` also invoke the writer (initial world
snapshot / session bootstrap).

### Autosave / Quicksave in the frame loop

`VIBE_GameLogic_RunFrameLoop @0x4c09a0` checks a deferred save request near the end of
each frame (block at +0x4c13fc, gated on `byte_63CC74` and not in a blocking mode):

- `dword_63CC70 == 1` → name = `"QUICKSAVE"` (`aQuicksave` @0x61e544).
- `dword_63CC70 == 0` → name = `"AUTOSAVE"` (`aAutosave` @0x61e550).
- `dword_63CC70` else → `sprintf("GILDE_SAVEGAME_%i", dword_63CC70)` (numbered slot).

The chosen name is copied and the request is dispatched via
`VIBE_Net_LoadAndSyncSession @0x56da74` (which in multiplayer routes through the
command system so all peers save/load in lockstep; `VIBE_Net_LoadAndSyncSession`
itself calls `VIBE_Save_WriteGameFile` twice — once per `.SAV`/`.SRV`). The flag
`byte_63CC74` is then cleared so each request fires once.

`VIBE_Save_DoQuickSave @0x56d984` is the direct single-player quicksave (bound to a
hotkey via `VIBE_Input_HandleGameSpeedKeys @0x4ff800`): in a multiplayer game it
queues command 15 (`VIBE_Command_QueueRequestFlagBlob32 @0x494ab4`) and waits; in
single-player it renders a thumbnail (`VIBE_Render_CaptureScreenThumbnail @0x56d48c`),
sets `byte_649D50 = 1`, and calls `VIBE_Save_WriteGameFile("Gamedata\Saves\
Quicksave.SAV", …)`.

---

## 23. Network load — `VIBE_Net_LoadSavedNetworkGame @0x50442c`

For multiplayer, a saved game is distributed and loaded in lockstep:

1. Build `gamedata/network/<name>.SAV` and a status string; show the progress form
   (`VIBE_Window_ShowProgressForm @0x503f44`).
2. Spin pumping messages + `VIBE_Amt_RefreshGuildState @0x4becdc` until the peer-sync
   flag `byte_63CC28` is set.
3. If this peer is the host (`byte_63CC28 & 1`): build `gamedata/network/<name>.SRV`
   and `VIBE_Net_SendSaveGameToClients @0x5abfe8` (ships the scenario `.SRV` to
   clients); on failure return 1.
4. Host waits ~80 frames (`Sleep(0x1E)` each) then queues request 6
   (`VIBE_Command_QueueRequestFlagBlob32`).
5. All peers wait until `byte_63CC28 & 4`, then call **`VIBE_Save_LoadGameFile`** on
   the local `.SAV` — the *same* loader as single-player; the only difference is the
   path and the surrounding sync handshake.
6. `VIBE_Net_RunSyncWaitLoop @0x4beac8`, then `VIBE_Scene_SyncMeisterBuildings
   @0x504ce0` to reconcile building ownership across peers.

So there is exactly **one** save format: network play reuses the identical
`.SAV` produced by `VIBE_Save_WriteGameFile`, plus an auxiliary `.SRV` scenario
snapshot written with the lightweight (`a4 & 2`) flag.

---

## 24. Compression note

Save I/O is funneled through the VFS stream layer (`VIBE_Vfs_OpenFile` /
`VIBE_Vfs_ReadStream` / `VIBE_Vfs_WriteStream` / `VIBE_Vfs_CloseStream`). The save
functions themselves perform **no compression** — they stream raw little-endian
fields. Whether the underlying VFS stream applies any container compression (the
engine's packed archives do use a codec elsewhere) is a property of the VFS layer,
documented in [29 — VFS & compression](29-vfs-fileio-compression.md). The thumbnail
RGB block is stored uncompressed (57600 raw bytes) in both header and scenario block.

---

## Cross-references

- [12 — Session init & world load](12-session-init-worldload.md) — how a loaded world
  is activated (`VIBE_Universe_SwitchActiveSlot`, scene/light rebuild).
- [14 — Per-frame loop](14-per-frame-loop.md) — where autosave/quicksave requests fire.
- [15 — Game time](15-game-time-tick.md) — the 14-byte `qword_13CE852` clock blob.
- [16 — Characters / persons](16-characters-persons.md) — the 169-byte person record
  and 268-word character roster.
- [20 — Buildings / city](20-buildings-city.md) — the 16-building table and slot
  tables.
- [29 — VFS & file I/O / compression](29-vfs-fileio-compression.md) — the stream layer
  all saves go through.
