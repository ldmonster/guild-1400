# 26 — Mesh, asset, LOD & BGF

> Every visible 3D object in *Die Gilde* — a house, a market stall, a person, a
> cart, a tree — is a **stock object**: a heap-allocated mesh record loaded once
> from a `.bgf` file, registered in a global intrusive cache, and shared by every
> in-world instance that references it by name. This document covers that whole
> pipeline, from the high-level *load-or-find* orchestrator down to the byte
> layout of the `.bgf` on disk. The four moving parts are: (1) the **asset cache**
> — a singly-linked list of mesh records keyed by a case-insensitive name;
> (2) the **LOD-filename strategy** — how one logical object name expands into the
> base mesh plus a shadow mesh (`_s`) plus up to two detail-reduced meshes
> (`_1`, `_2`) according to a single global mode byte; (3) the **`.bgf`/`.txs`
> loader** — the reader that turns a file stream into vertices, polygons and
> materials; and (4) **texture attachment** — copying a stock object's geometry
> into a live object's per-LOD draw slots and binding its textures. The geometry
> produced here feeds the render-universe chain in
> [doc 23](23-render-universe-chain.md); the textures are uploaded to the GPU
> (DirectDraw/Direct3D in the original; **Vulkan** in this port — see the platform
> note in §8). Animation rigs that ride on top of these meshes are
> [doc 27](27-animation-skeleton.md).

All addresses are RVA-style absolute (imagebase `0x400000`). Every function is
named `VIBE_*` from the IDA database; the binary is the source of truth. Struct
offsets below are recovered from accessor instructions in the loaders, not from
symbols.

Cross-links: [16 — Characters](16-characters-persons.md) ·
[23 — Render universe chain](23-render-universe-chain.md) ·
[24 — Projection & rasterizer](24-projection-rasterizer.md) ·
[27 — Animation & skeleton](27-animation-skeleton.md) ·
[29 — VFS / file I/O / compression](29-vfs-fileio-compression.md).

---

## 1. The pipeline at a glance

```
VIBE_Object_AttachToUniverseNode  @0x5b3e30   (scene entry; doc 23)
        │
        ├── VIBE_Mesh_FindStockObject     @0x5d10d0   cache probe (by name)
        ├── VIBE_Mesh_LoadOrFindByName    @0x5d345c   load-or-find orchestrator
        │       ├── VIBE_Mesh_BuildLodFileName  @0x5d15fc   name → base/_s/_<lod>
        │       ├── VIBE_Mesh_FindStockObject   @0x5d10d0
        │       └── VIBE_Mesh_LoadAndRegister   @0x5d32d4   parse + link into cache
        │               ├── VIBE_Mesh_BuildTexturePath @0x5d1034  "*"+name+suffix
        │               ├── VIBE_Mesh_LoadTextureSet   @0x5d2240  .TXS sidecar
        │               └── VIBE_Mesh_LoadBgfFile      @0x5d2348  .bgf parser
        │                       └── VIBE_Model_LoadFastChunk @0x5f87b8  packed-archive fast path
        └── VIBE_Mesh_AttachStockObjectLods @0x5d1824  copy geometry into live object
                └── VIBE_Mesh_AttachStockTextures @0x5d1114  per-LOD draw slot + texture bind
```

The cache is loaded **lazily and exactly once per name**: the first object that
needs `"haus01"` triggers the file load and registration; every subsequent
object with the same name reuses the already-resident record.

---

## 2. The asset cache (stock-object list)

The cache is a single global **intrusive singly-linked list** of mesh records.
A mesh record is the `0x20C`-byte (524-byte) block allocated by the loader
(§5). Three globals govern it:

| Global | Addr | Meaning |
|---|---|---|
| `dword_13FCCFC` | `0x13FCCFC` | head pointer (first record, or the sentinel) |
| `dword_13FCAEC` | `0x13FCAEC` | "most-recently-loaded" pointer (prev-link helper) |
| `unk_13FC8EC`   | `0x13FC8EC` | the list **sentinel / terminator** node |

Record fields used by the cache (offsets in the 524-byte record, indices are the
decompiler's `_DWORD` indices ×4):

| Offset | Field |
|---|---|
| `+0x00` | name, NUL-padded, 63 chars + terminator (written by `VIBE_Util_StrNCopyPad(rec, name, 63)`) |
| `+508` (`[127]`) | **next** pointer in the cache list |
| `+512` (`[128]`) | **prev/back** pointer (set to `dword_13FCAEC` at register time) |

### 2.1 Find — `VIBE_Mesh_FindStockObject` @0x5d10d0

```c
int VIBE_Mesh_FindStockObject(u8 *name) {            // @<eax>
    rec = dword_13FCCFC;                              // head
    if (rec == &unk_13FC8EC) return 0;                // empty list
    while (VIBE_Util_StrCmpNoCaseN(name, rec, 63)) {  // case-insensitive, 63 chars
        rec = *(rec + 508);                           // follow next
        if (rec == &unk_13FC8EC) return 0;            // hit sentinel → miss
    }
    return rec;                                        // hit
}
```

The key is the **first 63 bytes of the record**, compared case-insensitively
(`VIBE_Util_StrCmpNoCaseN` @0x5e0db0). Because the name stored in the record *is*
the LOD-expanded filename (e.g. `HAUS01`, `HAUS01_S`, `HAUS01_1`), each LOD level
is a **separate cache entry** with its own record — they are *not* sub-slots of a
single record. The "slot" relationship between LODs is reconstructed later, at
*attach* time, into the live object's draw block (§6).

> ⚠️ Note the calling convention: `FindStockObject` reads `name` from `eax`. At
> several call sites (e.g. inside `AttachStockObjectLods`, `AttachStockTextures`)
> the decompiler shows `VIBE_Mesh_FindStockObject()` with no argument — the name
> is already in the register from the immediately-preceding `BuildLodFileName`
> call that wrote into the same buffer. This is a `__usercall` register artifact,
> not a missing argument.

### 2.2 Register — tail of `VIBE_Mesh_LoadAndRegister` @0x5d32d4

After a successful parse, the new record `rec` is linked at the **head**:

```c
v12 = dword_13FCAEC;                 // previous head-ish pointer
rec[128] = dword_13FCAEC;            // rec.prev  = old MRU
dword_13FCAEC = rec;                 // MRU = rec
rec[127] = &unk_13FC8EC;             // rec.next  = sentinel
*(v12 + 508) = rec;                  // old.next  = rec
```

So new records are appended after the previously-loaded record, with the
sentinel always terminating the chain. There is **no eviction** — stock objects
live for the process lifetime (freed only by `VIBE_Mesh_DeleteStockObject`
@0x5d1968, wired as the record's vtable-style destructor slot `[122]`).

---

## 3. Load-or-find orchestrator — `VIBE_Mesh_LoadOrFindByName` @0x5d345c

This is the function callers actually use. Given a base name `a1` (and an
optional *texture-name* override `a2`, usually equal to `a1`), it guarantees the
base mesh plus all its LOD/shadow companions are resident, returning the **base**
record:

```c
float *VIBE_Mesh_LoadOrFindByName(const char *name, const char *texName) {
    char base[256], lod0[256];
    VIBE_Mesh_BuildLodFileName(name, texName, lod0, 0, base);   // lod index 0
    rec = VIBE_Mesh_FindStockObject(base);
    if (!rec) rec = VIBE_Mesh_FindStockObject(lod0);
    if (!rec) {
        rec = VIBE_Mesh_LoadAndRegister(lod0, base);            // load LOD-0 (base mesh)
        if (VIBE_Mesh_BuildLodFileName(name, texName, _, -1, base)) // build "_s" shadow name
            VIBE_Mesh_LoadAndRegister(lod0, base);              // load shadow
        if ((byte_64A098 & 0x7F) == 1) {                        // LOD mode 1 → load detail meshes
            for (i = 1; i < 3; ++i) {
                while (!VIBE_Mesh_BuildLodFileName(name, texName, lod0, i, base)) {
                    if (++i >= 3) return rec;                   // no more LODs on disk
                }
                VIBE_Mesh_LoadAndRegister(lod0, base);          // load "_1", "_2"
            }
        }
    }
    return rec;                                                  // base record
}
```

Two buffers travel together: one holds the **mesh/geometry filename** (`lod0`
arg `a3`) and one holds the **texture-set filename** (`base` arg `a5`). Normally
both are the same name; the second exists so an object can borrow another
object's textures. The shadow mesh is loaded via `index = -1` (a special path in
`BuildLodFileName`, §4.2), and the two detail meshes (`_1`, `_2`) are loaded only
when the global LOD mode (§4.0) is **1**.

---

## 4. LOD filename strategy — `VIBE_Mesh_BuildLodFileName` @0x5d15fc

### 4.0 The LOD mode byte — `byte_64A098`

```
byte_64A098  @0x64A098   default value = 0x02 (read from the binary)
```

The function reads this byte two ways:

* **`byte_64A098 & 0x7F`** — the low 7 bits give the *LOD mode*: `0`, `1`, or `2`.
* **`byte_64A098 >= 0`** (sign test, i.e. the top bit `0x80`) — a separate flag
  that gates whether **`_s` shadow** names are produced at all.

| Mode (`&0x7F`) | Behaviour |
|---|---|
| `0` | plain — no detail-LOD probing; index 0 → bare `name`; explicit index *N* → `name_N` |
| `1` | classic LOD — index 0 → bare `name`; explicit index *N* → `name_N`; orchestrator loads `_1`,`_2` |
| `2` | **downward existence probe** — index 0 resolves to the *highest existing* `name_i`; explicit index *N* counts from the top |

`BuildLodFileName` takes: `a1` mesh base name, `a2` texture base name (may be
NULL), `a3` mesh-out buffer, `a4` **LOD index** (`0`, `1`, `2`, or the special
`-1`), `a5` texture-out buffer (may be NULL). It returns `1` if a name was
produced, `0` if not. The string copies in the decompilation are an inlined
word-at-a-time `strcpy`; treat them as plain copies.

### 4.1 Index 0 — the base (or top-LOD probe)

```c
if (a4 == 0) {
    if ((byte_64A098 & 0x7F) == 2) {                 // mode 2: probe downward
        v7 = 2;
      LABEL_5:
        sprintf(a3, "%s_%i", a1, v7 - 1);            // try name_1, name_0, …
        if (a2 && a5) sprintf(a5, "%s_%i", a2, ...);
        while (!VIBE_Mesh_BuildTexturePath(a3, ".bgf")) {  // does the .bgf exist?
            if (--v7 < 0) return 0;                  // none found
            if (v7 > 0)  goto LABEL_5;               // try next-lower index
            strcpy(a3, a1);                          // v7==0 → fall back to bare name
            if (a2 && a5) strcpy(a5, a2);
        }
        return 1;
    }
    strcpy(a3, a1);                                  // modes 0/1: bare base name
    if (a2 && a5) strcpy(a5, a2);
    return 1;
}
```

In **mode 2**, index 0 means *"give me the most detailed LOD that exists on
disk"*: it probes `name_1`, then `name_0`, and finally the bare `name`, using
`VIBE_Mesh_BuildTexturePath(..., ".bgf")` (§5.1) as an existence test (a NULL
return means the resolved path does not exist).

### 4.2 Index −1 — the `_s` shadow name

```c
if (a4 < 0) {
    if (byte_64A098 >= 0) return 0;                  // top bit clear → no shadow meshes
    strcpy(a3, a1);
    strcat(a3, aS_8);                                // append "_s"  (aS_8 @0x628F90 = "_s")
    if (a2 && a5) { strcpy(a5, a2); strcat(a5, "_s"); }
    return 1;
}
```

The shadow suffix is the literal **`"_s"`** (`aS_8` @0x628F90). Shadow names are
only generated when the sign bit of `byte_64A098` is set (`< 0`); otherwise the
function returns 0 and no shadow record is loaded.

### 4.3 Explicit index N (≥1) — the `_<lod>` detail name

```c
v6 = byte_64A098 & 0x7F;
if (v6 == 2) a4 = 2 - a4;                            // mode 2: flip the index (top-down)
v42 = a4 - 1;
sprintf(a3, "%s_%i", a1, v42);                       // "%s_%i" = aSI_1 @0x628F94
if (!a2 || !a5) return 1;
sprintf(a5, "%s_%i", a2, v42);
return 1;
```

The detail suffix is **`"_<n>"`** built by the format `"%s_%i"` (`aSI_1`
@0x628F94). The printed integer is `index - 1` in modes 0/1, and `(2 - index) -
1` in mode 2 (the **explicit-LOD index flip** — in mode 2 the on-disk numbering
runs top-down, so the engine inverts the requested level). Thus `AttachStockObjectLods`'s
loop `i = 1, 2` produces filenames `name_0`, `name_1` in modes 0/1.

---

## 5. The `.bgf` / `.txs` loader

### 5.1 Texture / asset path — `VIBE_Mesh_BuildTexturePath` @0x5d1034

Every asset filename is built and resolved through one helper:

```c
char **VIBE_Mesh_BuildTexturePath(char *name, char *suffix) {
    char buf[272];
    strcpy(buf, unk_628F14);     // unk_628F14 @0x628F14 = "*"   (VFS group/wildcard prefix)
    strcat(buf, name);           // logical object name
    strcat(buf, suffix);         // ".bgf", ".TXS", etc.
    return VIBE_Vfs_ResolveAndBuildPath(buf, dword_1406110, &unk_1406114, dword_62EB78);
}
```

The path format is **`"*" + name + suffix`**. The leading `"*"` is the VFS group
selector (see [doc 29](29-vfs-fileio-compression.md)): the resolver searches the
mounted archive groups for the named member. `VIBE_Vfs_ResolveAndBuildPath`
@0x4500a0 → `VIBE_Vfs_ResolvePath` + `VIBE_Vfs_BuildFullPath`; a NULL return
means the asset is not present, which is exactly how `BuildLodFileName` mode-2
uses it as an existence probe. Suffixes seen in this subsystem:

| Suffix | Const | Purpose |
|---|---|---|
| `.bgf` | `aBgf` @0x628F9C | geometry chunk file |
| `.TXS` | `aTxs` @0x628FCC | texture-set sidecar (per-instance texture name list) |

> **Packed archives.** Meshes and textures ship inside the game's packed
> archives — conventionally **`Objects.BIN`** (geometry) and **`Textures.BIN`**
> (texture pixels) — mounted as VFS groups. The `"*"` prefix routes the lookup
> through those groups; the *fast-chunk* path (§5.4) is the loader specialised
> for reading a `.bgf` body straight out of such a packed/“fast” chunk stream
> rather than a loose file. Group mounting and the BIN container format are
> documented in [doc 29](29-vfs-fileio-compression.md).

### 5.2 The `.TXS` texture-set sidecar — `VIBE_Mesh_LoadTextureSet` @0x5d2240

A `.TXS` file is an optional sidecar holding a table of texture *names* (a set of
texture-name rows, one column per material). Layout, all dwords **byte-swapped**
on read (`VIBE_Bio_ReadDwordSwapArgs` @0x5dc8b0 — the format is big-endian on
disk):

| Field | Size | Notes |
|---|---|---|
| magic | dword | must equal **`603064750`** (`0x23F3866E`) or the file is ignored |
| `nRows` | dword | number of texture-set rows (`i`) |
| `nCols` | dword | textures per row (`v11[0]`) |
| names | `nRows*nCols` × 64-byte strings | read row-major by `VIBE_Bio_ReadString`; each name slot is **64 bytes** (`<<6`) |

The whole table is allocated `(nRows*nCols) << 6` bytes and returned through the
`a2`/`a3` out-params; it is later matched against the materials' texture
references in the BGF.

### 5.3 The `.bgf` body — `VIBE_Mesh_LoadBgfFile` @0x5d2348

This is the heavyweight loader. It uppercases the filename
(`VIBE_Util_StrToUpper` @0x5e9f50), then tries the **fast-chunk** path first
(§5.4); on miss it falls back to a streaming script/chunk parse via
`VIBE_ModelIo_ReadChunkTag` @0x5e44b4 (which validates a 4-byte tag, reads a
`'.'` token = `46`, a swapped dword, then dispatches to the block parser
`VIBE_Script_ParseBlock` using the per-key handler table `byte_64A4F8`). Either
way it materialises the same 524-byte mesh record. Beyond raw parsing it also
performs three clean-up passes that are part of the format contract:

1. **De-duplicate vertices** (`RemoveDoubleMaterials`/point pass): pairs of
   points within tolerance `0.001` (`VIBE_Math_VectorWithinTolerance` @0x5caa4c)
   are merged and every polygon's vertex index is remapped (`-1` for indices
   above the removed one).
2. **De-duplicate materials**: 224-byte material records compared with `memcmp`;
   duplicates collapse and polygon `matIndex` fields are remapped/compacted.
3. **Per-vertex transform**: each point is rotated by the constants
   `dbl_6290CC`, `dbl_6290D4`, scaled by `dbl_6290DC` (a coordinate-system /
   handedness fix-up baked into load), and each polygon's per-corner UVs are
   rotated/translated by the material's UV-offset/scale/rotation fields.

After loading it binds the texture references (matching material texture names
against the `.TXS` set if one was supplied, else loading them by name through
`VIBE_Texture_LoadByName` @0x5da714), then calls
`VIBE_Mesh_ComputeBoundingExtents` @0x5d1b54 and
`VIBE_Mesh_ComputeVertexNormals` @0x5d1a6c.

### 5.4 The fast path — `VIBE_Model_LoadFastChunk` @0x5f87b8

`LoadFastChunk` reads the `.bgf` body straight from a located chunk
(`VIBE_Vfs_FindChunkStart` @0x5f86fc). It is the cleanest witness to the on-disk
field order because it does flat sequential `VIBE_Bio_*` reads. Its read order
(all multi-byte values **byte-swapped**):

```
rec = alloc(0x20C); StrNCopyPad(rec, name, 63);
ReadDwordSwap → rec[120] (+480)   // nMaterials (texset count)
ReadDwordSwap → rec[17]  (+68)    // nVertices
ReadDwordSwap → rec[19]  (+76)    // nPolygons
points = alloc(24*(nVertices+8));  rec[16] (+64) = points
for v in 0..nVertices+8:           // +8 sentinel/padding vertices
    ReadVec3 → point.pos   (+0,  3×float)
    ReadVec3 → point.normal(+12, 3×float)   // 24 bytes/vertex
ReadDword  → rec[117] (+468)       // flags
polys = alloc(56*nPolygons);       rec[18] (+72) = polys
for p in 0..nPolygons:             // 56 bytes/polygon
    ReadDwordSwap → poly[+24] vertex index 0
    ReadDwordSwap → poly[+28] vertex index 1
    ReadDwordSwap → poly[+32] vertex index 2
    ReadVec3      → poly[+0]  (UV.u for the 3 corners)   // interleaved u/v below
    ReadVec3      → poly[+?]  (UV.v for the 3 corners)
    ReadVec3      → poly[+44] (per-poly normal)
    poly[+36] = -1                 // resolved texture handle, filled at bind
    if (nMaterials <= 254) { ReadByte → poly[+40] matIndex (0xFF→ -1) }
    else                   { ReadDwordSwap → poly[+40] matIndex }
... read material/texture-name table (64-byte name slots) ...
ReadDwordSwap → rec[130] (+520)    // nAttachPoints
for a in 0..nAttachPoints:         // 88 bytes each
    ReadString → name (+116…)
    ReadVec3   → pos
    ReadVec3   → orientation
CloseStream
```

#### `.bgf` on-disk record layout (recovered offsets)

**Mesh record** (524 = `0x20C` bytes), key fields:

| Offset | Idx | Field |
|---|---|---|
| `+0x00` | `[0]` | name (≤63 + NUL) |
| `+64`  | `[16]`  | `points`  → vertex array |
| `+68`  | `[17]`  | `nVertices` |
| `+72`  | `[18]`  | `polys`   → polygon array |
| `+76`  | `[19]`  | `nPolygons` |
| `+468` | `[117]` | flags |
| `+480` | `[120]` | `nMaterials` |
| `+484` | `[121]` | material-set row count |
| `+516` | `[129]` | `materials/texnames` → 64-byte-per-material name block |
| `+520` | `[130]` | `nAttachPoints` |
| `+472..+500` | `[118]..[125]` | function-pointer slots (vtable-ish): `[122]` destructor `DeleteStockObject`, `[123]` `TransformVertexNormals`, `[124]` `InterpolateMorphVertices`, `[125]` `ComputeBoundingBox`, `[126]` `GetBoundingRadius` (set by `LoadAndRegister`) |
| `+508` | `[127]` | cache **next** |
| `+512` | `[128]` | cache **prev** |

**BgfVertex** — 24 bytes: `pos` `float[3]` `@+0`, `normal` `float[3]` `@+12`.

**BgfPolygon** — 56 bytes:

| Offset | Field |
|---|---|
| `+0`  | UV `u` for the 3 corners (group A of texcoords) |
| `+12` | UV `v` for the 3 corners |
| `+24` | vertex index 0 |
| `+28` | vertex index 1 |
| `+32` | vertex index 2 |
| `+36` | resolved texture handle (filled at bind; `-1` until then) |
| `+40` | **`matIndex`** (byte when `nMaterials ≤ 254`, else dword; `0xFF` → `-1`) |
| `+44` | per-polygon normal `float[3]` |

**BgfMaterial** — 224 bytes (in the streaming loader's `v147` table). Read as a
flat block; the texture-name strings live at `+0` (diffuse/base), `+64`
(secondary), `+128` (extra) — each a 64-byte slot — followed by colour/flag
bytes (`+192` alpha-mode, `+193` alpha value, `+194..+201` blend/lighting flags),
and UV transform floats (`+204` u-scale, `+208` v-scale, `+212/+216` u/v offset,
`+220` rotation). The colour/flag bytes are packed into a 32-bit `color`/`mode`
word per the bit-twiddling in `VIBE_Texture_LoadByName`’s argument build.

---

## 6. Texture / geometry attach to a live object

A *stock object* is shared, read-only geometry. To draw it, the live object gets
its own **draw block** (allocated by `VIBE_Object_AllocDrawData` @0x5b107c at
offset `+492`) into which the stock geometry is copied per LOD.

### 6.1 Per-LOD wiring — `VIBE_Mesh_AttachStockObjectLods` @0x5d1824

This is where the **base@244 / `_s`@1396 / LOD-frame@244+384·lod** slot model is
established. `a1` is the live object; `obj->drawData = *(a1+492)`.

```c
void VIBE_Mesh_AttachStockObjectLods(obj, mode, const char *name, edi) {
    if (mode) { /* attach a single, already-resolved stock object */ ... }
    else {
        VIBE_Mesh_BuildLodFileName(name, 0, buf, 0, 0);   // base LOD-0 name
        if (VIBE_Mesh_FindStockObject(/*buf*/)) {
            if (!obj->drawData) VIBE_Object_AllocDrawData(obj);
            // BASE mesh → draw slot at +244
            VIBE_Mesh_AttachStockTextures(obj, obj->drawData + 244, edi);

            // SHADOW (_s) → draw slot at +1396
            if (VIBE_Mesh_BuildLodFileName(name, 0, _, -1, 0) && VIBE_Mesh_FindStockObject())
                VIBE_Mesh_AttachStockTextures(obj, obj->drawData + 1396, edi);

            // DETAIL LODs (mode 1 only) → slot at +244 + 384*lod
            if ((byte_64A098 & 0x7F) == 1) {
                for (v8 = 384, lod = 1; lod < 3 && v8 < 1152; ++lod, v8 += 384) {
                    if (VIBE_Mesh_BuildLodFileName(name, 0, buf, lod, 0)
                        && VIBE_Mesh_FindStockObject())
                        VIBE_Mesh_AttachStockTextures(obj, obj->drawData + v8 + 244, v8);
                }
            }
        }
    }
}
```

So inside the live object's draw block:

| Draw slot offset | Holds |
|---|---|
| `drawData + 244` | **base** LOD-0 frame |
| `drawData + 244 + 384·lod` (lod = 1,2) | **detail** LOD frames (mode 1) |
| `drawData + 1396` | **`_s` shadow** frame |

Each LOD frame is **384 bytes**; the loop is bounded by `v8 < 1152` (= 3·384), so
at most three forward frames (base + 2 detail) plus the dedicated shadow slot.

### 6.2 Copy & bind — `VIBE_Mesh_AttachStockTextures` @0x5d1114

For one draw slot (`a2`), this:

1. Finds the stock record (`a4`/name), stores `nVertices @slot[2] (+8)`,
   `nPolygons @slot[3] (+12)` and a back-pointer to the stock record `@slot[4]
   (+16)`.
2. `VIBE_Object_AllocPolysAndPoints` @0x5b0c10 allocates the slot's per-instance
   point and polygon working arrays (80 bytes/point, 40 bytes/polygon entry).
3. Copies each polygon: its 3 vertex pointers (into the slot's point array),
   default colour from the material (`(matIndex<<7) + dword_1406A84` indexes the
   global material/texture table at `0x1406A84`), per-corner colour `+64/+68`, and
   the alpha/blend flag bits `+38`.
4. Builds the slot's **CurTexSet** array (`slot[5] (+20)` = `alloc(4 *
   nMaterials)`), one texture handle per material, incrementing each texture's
   refcount (`VIBE_Texture_IncrementRefCount` @0x5da2e4). Missing textures log
   `"stock object %s, texture %s not found"` and
   `Error attaching Stock-Object "…": Not all Textures used/found!`.
5. Picks the object's active render frame (`+460`) via
   `VIBE_Mesh_SelectLodFrame` @0x5adb6c, scanning the 384-byte LOD frames for the
   first non-empty one.

The `dword_1406A84` global (`0x1406A84`) is the base of the engine's flat
**material/texture table** (128-byte entries — note the `<<7` and `>>7` index
math); a polygon's `matIndex` is converted to a table pointer and back to an
index throughout these loaders.

---

## 7. Scene entry — `VIBE_Object_AttachToUniverseNode` @0x5b3e30

This is the live call-tree root (from [doc 23](23-render-universe-chain.md)):

```c
obj = VIBE_Object_Spawn(4, name);
if (parent) VIBE_Object_SetParent(parent, obj);
if (!VIBE_Mesh_FindStockObject()) VIBE_Mesh_LoadOrFindByName(name, name); // ensure resident
VIBE_Mesh_AttachStockObjectLods(obj, 0, name, edi);                       // copy geometry
// for each LOD frame present (drawData[2316] = LOD count, step 384):
//   for each material's texture handle (drawData+264 array, count @stock+480):
//       VIBE_Texture_UploadToSurface(handle, …)   // upload pixels to GPU surface
VIBE_Object_SetPosition(obj, pos);
VIBE_Object_SetWorldTranslation(obj, worldXlate);
if (!parent) VIBE_Object_LinkIntoScene(obj);
```

The byte at `drawData + 2316` is the **count of attached LOD frames**
(incremented once per `AttachStockTextures` that targets a non-shadow slot — see
the `++*(drawData+2316)` in §6.2). The texture upload step
(`VIBE_Texture_UploadToSurface` @0x5db234) is the platform boundary (§8).

---

## 8. Platform boundary (rules 3–5)

* **Texture pixels → GPU.** The original uploads decoded texture pixels to
  DirectDraw/Direct3D surfaces (`VIBE_Texture_UploadToSurface` @0x5db234,
  reached from §7). In this port that surface upload is replaced by a **Vulkan**
  image/sampler upload through `IGraphicsDevice`; the *selection* of which
  texture handles to upload, the refcount bookkeeping, and the material/UV math
  are reconstructed 1:1 and stay platform-independent.
* **Meshes → render chain.** The geometry produced here (points, polygons,
  per-LOD draw frames) is *not* drawn by this subsystem; it feeds the
  render-universe traversal and the software/HW projection-raster path in
  [doc 23](23-render-universe-chain.md) and [doc 24](24-projection-rasterizer.md).
* **File bytes → VFS.** All `.bgf`/`.TXS`/BIN reads go through the VFS layer
  (`VIBE_Vfs_*`, `VIBE_Bio_*`); the byte-swapping read helpers mean the on-disk
  format is **big-endian**. File-system access is behind `IFileSystem`; see
  [doc 29](29-vfs-fileio-compression.md).

---

## 9. Constants & globals reference

| Symbol | Addr | Value / meaning |
|---|---|---|
| `byte_64A098` | `0x64A098` | LOD mode (`&0x7F`: 0/1/2) + shadow-enable sign bit; default `0x02` |
| `unk_628F14` | `0x628F14` | `"*"` VFS group prefix |
| `aS_8` | `0x628F90` | `"_s"` shadow suffix |
| `aSI_1` | `0x628F94` | `"%s_%i"` LOD-name format |
| `aBgf` | `0x628F9C` | `".bgf"` |
| `aTxs` | `0x628FCC` | `".TXS"` |
| `.TXS` magic | — | `603064750` (`0x23F3866E`) |
| `dword_13FCCFC` | `0x13FCCFC` | cache list head |
| `dword_13FCAEC` | `0x13FCAEC` | cache MRU pointer |
| `unk_13FC8EC` | `0x13FC8EC` | cache sentinel |
| `dword_1406A84` | `0x1406A84` | material/texture table base (128-byte entries) |

---

## 10. Gaps & deferrals

* `VIBE_Mesh_LoadBgfFile`'s **streaming** branch dispatches through
  `VIBE_Script_ParseBlock` (@0x5e3c44) using the per-key handler table
  `byte_64A4F8` (@0x64A4F8); the individual `.bgf` script-block key handlers are
  not enumerated here (they belong with the model-script parser). The
  *fast-chunk* path (§5.4) fully pins the binary field layout, which is what the
  format documentation needs.
* The exact interleave of the polygon's two UV `Vec3` reads vs. the corner→UV
  mapping (`poly+0` group A, `poly+12` group B) is given to the field level;
  the precise float-component-to-corner assignment inside the rotate/translate
  fix-up (`LoadBgfFile` material UV pass) is described functionally rather than
  byte-by-byte.
* `Objects.BIN` / `Textures.BIN` container framing is summarised here (mounted as
  `"*"` VFS groups) and documented in full in
  [doc 29](29-vfs-fileio-compression.md).
