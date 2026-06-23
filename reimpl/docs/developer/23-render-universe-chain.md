# 23 — Render engine: the universe chain

This is the **top of the 3D pipeline** — the chain that turns the active *universe* (the
engine's name for the 3D scene) into pixels each frame. The same source module produces
nearly every string in this subsystem: **`"d3_d3d.c"` @0x6298b0** is the most-referenced
string in the renderer, tagged into ~60 device/error sites across `VIBE_Render_*`
(`VIBE_Render_EnumDevices`, `VIBE_Render_CreateDeviceAndViewport`,
`VIBE_Render_ApplyRenderStates`, `VIBE_Render_DrawTriangleList`,
`VIBE_Render_SetProjectionTransform`, …). That file is the original's **DirectDraw /
Direct3D** back end. The math in it is reconstructed 1:1; the GPU API beneath it is the one
sanctioned tech swap.

> **Platform boundary (rule 3) — DDraw/Direct3D → Vulkan+SDL.** Every call through a COM
> vtable (`(*(...**)(*(_DWORD *)dev + N))(dev, …)`), every `IDirectDrawSurface` lock, every
> `GetDC`/`BitBlt` present path, and every `IDirect3DDevice` `SetTexture`/`DrawPrimitive` is a
> **Vulkan swap point**. They are flagged inline below as **[Vulkan swap]**. Everything else
> in this doc — the scene-graph walk, draw-list build, sort, and clip/project math — is pure
> CPU logic and is reconstructed verbatim.

Cross-links: [14 — Per-frame loop](14-per-frame-loop.md) ·
[24 — Projection & rasterizer](24-projection-rasterizer.md) ·
[25 — Lighting & FX](25-lighting-fx.md) · [26 — Mesh & LOD](26-mesh-asset-lod.md) ·
[27 — Animation](27-animation-skeleton.md).

---

## 1. The chain at a glance

```
VIBE_GameLogic_RunFrameLoop @0x4c09a0            (doc 14)
  └─ VIBE_Render_RenderMainViewFrame @0x5b6074    top render entry (one caller)
       └─ VIBE_Render_RenderUniverseFrame @0x5b3de8
            ├─ VIBE_Render_BeginUniverseFrame @0x5b3900   clear → project → walk → SORT → raster
            │    ├─ VIBE_Render_ClearViewport / ClearRect            [Vulkan swap]
            │    ├─ VIBE_Floor_RenderTerrain @0x5bf22c               (terrain, doc 26)
            │    ├─ VIBE_SceneGraph_WalkAndInvoke @0x5ac738  ── per node ──▶
            │    │      VIBE_Render_ProcessSceneNode @0x5add1c   node → draw-list records
            │    │        ├─ VIBE_Mesh_SelectLodFrame @0x5adb6c     pick LOD frame
            │    │        ├─ VIBE_Render_CullNodeAgainstFrustum @0x5ad588
            │    │        ├─ VIBE_Mesh_InterpolateMorphVertices / T&L
            │    │        └─ VIBE_Render_ComputeVertexClipFlags @0x5ad614  (doc 24)
            │    ├─ VIBE_Particle_RenderSystem @0x5e1278   (particles → draw-list)
            │    └─ VIBE_Render_ResetEngineState @0x5af200   ── SORT + RASTERIZE ──▶
            │         ├─ VIBE_Render_RadixSortDrawList @0x5aef34          LSD radix
            │         └─ byte_649D70 ? VIBE_Render_DrawTexturedTriangles @0x5ae434  (HW D3D)
            │                        : VIBE_Render_RasterizeMeshList     @0x5aec88  (SW raster)
            └─ VIBE_Render_DrawUniverseAndStats @0x5b3bbc   second pass: anim pose + FPS stats

VIBE_Render_PresentFrame @0x4349e4    flips back buffer → screen   [Vulkan swap]
```

Two device lifecycle bookends sit outside the per-frame chain:
`VIBE_Render_InitDisplayAndPaths @0x527fa4` (startup) and
`VIBE_Render_ShutdownEngine @0x5b0228` (teardown), §8.

Note on names: the prompt's *`VIBE_ProcessSceneNodeAppend`* is the IDB's
`VIBE_Render_ProcessSceneNode @0x5add1c`, and *`VIBE_Render_SelectLodFrame`* is
`VIBE_Mesh_SelectLodFrame @0x5adb6c`.

---

## 2. The universe singleton and its globals

The whole renderer hangs off one global universe object pointer, **`dword_649D68`**
("current camera/universe node"), plus a small bank of state DWORDs at `0x649D__`. The
scene root array is **`off_649D64`** (the engine keeps up to **64 universe slots**, indexed
by `dword_649D60` = active slot; see the `for i<64` loops in `DrawUniverseAndStats` and
`ShutdownEngine`). Key per-frame state:

| Global | Meaning |
|---|---|
| `dword_649D68` | active universe / camera node (`+492` → its render record) |
| `off_649D64` | scene-graph root container (children list at `+128`) |
| `dword_64A050` | re-entrancy guard (`++` on entry, `--` on exit; ≥1 ⇒ frame already in flight) |
| `byte_649D71` | renderer-live flag (set after init, cleared by `ShutdownEngine`) |
| `byte_649D70` | **hardware-raster flag**: 1 = D3D HW T&L path, 0 = software rasterizer |
| `byte_649D7C` | device-objects-present flag (release on shutdown) |
| `dword_13FCD1C` | active camera transform block (`+76` pos, `+396` orientation) |
| `dword_64A028` | terrain/floor object (`VIBE_Floor_RenderTerrain`) |

`RenderMainViewFrame @0x5b6074` only renders when `byte_649D71 && dword_64A050 <= 0` — i.e.
the renderer is live and no frame is already in flight. It clears (full
`VIBE_Render_ClearViewport` or a partial `VIBE_Render_ClearRect` of the dirty viewport rect
`dword_13ECE58`/`dword_649DD4`) then calls `RenderUniverseFrame(64, 1, 1)`.

`RenderUniverseFrame @0x5b3de8` is the smallest node — it just runs the two phases:

```c
VIBE_Render_BeginUniverseFrame(a1, a2);          // build + draw the 3D scene
return VIBE_Render_DrawUniverseAndStats(a1, a2, 1, a3);  // anim pose pass + FPS stats
```

---

## 3. Phase A — `BeginUniverseFrame @0x5b3900`: clear → project → walk → sort → raster

This single function is the spine. In order:

1. **Re-entrancy guard + clear.** `if (dword_64A050<0) dword_64A050=0;` then guard on
   `byte_649D71 && !dword_64A050 && dword_13FCD1C`, `++dword_64A050`, and clear the surface
   (`ClearViewport` HW or `ClearRect` SW). **[Vulkan swap: clear]**
2. **Reset draw-list + depth accumulators.** `dword_64A060=0`, `dword_64A058=0`,
   `flt_13FCF3C=0` (max scene Z), `flt_13FD168[0]=1e10` (min scene Z), and clears the
   per-camera counters at `*(camrec+492)+252/256`.
3. **Fog range.** If a terrain object exists, project the camera anchor with
   `VIBE_Heightmap_ProjectPointToView @0x5c4034` and set near/far fog via
   `VIBE_Render_SetFogRange @0x5ae2a0` (clamped against `flt_13FC5F8`/`flt_13FC5FC`).
4. **Terrain.** `VIBE_Floor_RenderTerrain(dword_64A028, …)` lays down the ground mesh
   (doc 26).
5. **Light list reset.** `VIBE_Shadow_ResetLightList @0x5f4428`; `dword_649D6C` (mirror flag)
   and `dword_649DC8/CC` (node counters) zeroed.
6. **Scene-graph walk → draw list.** `VIBE_SceneGraph_WalkAndInvoke(off_649D64, …)`
   recursively visits every node, invoking `VIBE_Render_ProcessSceneNode` per visible node
   (§4–§5). This is where the draw list is populated.
7. **Particles.** Walk the particle-system list `dword_1408438` (sentinel `unk_1408440`),
   `VIBE_Particle_RenderSystem @0x5e1278` each → more draw-list records.
8. **Sky flares / mirrors.** `VIBE_Render_UpdateSkyFlares @0x5ef7cc` if a sky object exists;
   a second scene walk runs `VIBE_Mirror_BuildMirroredGeometry @0x5f637c` when mirror flags
   (`byte_14080EC & 0x40`, `dword_649D6C`, planar-mirror state) are set.
9. **Light callbacks** (`VIBE_Light_RegisterUpdateCallbacks @0x5c7d70`) when `a2` is set.
10. **Sort + rasterize + present-of-3D.** Finally:
    ```c
    dword_649DA8 = dword_13FC54C;      // snapshot accumulated screen-space offsets
    dword_649DB8 = dword_13FC574;
    VIBE_Render_ResetEngineState();    // ← SORT then RASTERIZE (see §6)
    --dword_64A050;                    // release re-entrancy guard
    ```

So a frame is strictly **walk-then-sort-then-raster**: every node first *appends* triangles
to a flat draw list; nothing is rasterized until the whole scene is gathered and sorted.

---

## 4. The scene-graph model

### 4.1 The walk — `VIBE_SceneGraph_WalkAndInvoke @0x5ac738`

A recursive depth-first traversal of the node tree. It is a generic visitor: it takes a
callback `a3` and a flag mask `a4`, and is reused for the geometry pass
(`VIBE_Render_ProcessSceneNode`), the animation-pose pass
(`VIBE_Anim_UpdateSkeletonPose @0x5cd1d8`), and the mirror pass
(`VIBE_Mirror_BuildMirroredGeometry`).

Reconstructed node layout (offsets in bytes; the node is addressed both as a `_DWORD*` —
index `[n]` = `+4*n` — and by raw byte offset):

```c
// gilde.exe — scene node (visited by VIBE_SceneGraph_WalkAndInvoke @0x5ac738)
struct SceneNode {
    /* +0x00  */ _BYTE   header[0x200];        // transform / bounds / payload (see below)
    /* +0x1F0 */ void   *renderRec;            // [+492] LOD frames, vis flags, counters
    /* +0x1F4 */ SceneNode *child;             // [+124]  *** dword[124] = +0x1F0? *** see note
    /* +0x1FC */ SceneNode *next;              // [+127]  sibling chain
    /* +0x208 */ _BYTE    flags0;              // [+520] node-level flags
    /* +0x210 */ _BYTE    stopBit;             // [+528] &1 = stop sibling walk
    /* +0x211 */ _BYTE    visFlags;            // [+529] visibility / shadow caster bits
    /* +0x215 */ _BYTE    kind;                // [+533] node kind (4 = light-cached object)
};
```

Indices observed directly in the decompile: **child pointer = `node[127]` (+0x1FC)**, the
recursed-into subtree; **sibling pointer = `node[124]` (+0x1F0)**, the next node in the same
level; **`+528 & 1`** terminates a sibling scan; **`HIBYTE(node[132])`** (`*(_DWORD*)(node+530)
>> 24`) is the per-node flag byte tested against the walk mask.

Walk logic (per level):

```c
if (!a4) return 0;                                  // empty flag mask → nothing
while (1) {
    v11 = 1;
    if (VIBE_SceneGraph_TestNodeFlag(node_flag, a4)) // does this node match the mask?
        v11 = a3();                                  // ← invoke callback (ProcessSceneNode)
    if (!v11) break;                                 // callback returned 0 → abort subtree
    child = node[127];
    if (child && v11 >= 0)                            // descend, unless 0x200 "no-recurse" bit
        v11 = WalkAndInvoke(a1, child, a3, a4 & 0xFDFF, a5);
    node = (a4 & 0x200) ? 0 : node[124];             // advance to sibling
    if (!node) return 1;
    if (*(_BYTE*)(node+528) & 1) return 1;           // sibling-stop bit
}
```

When called with `a2 == 0` (root form), it first snapshots a few globals into the root
container (`off_649D64[32..42]` = scene/particle/list heads) and then iterates the root's
own child list at `*(a1+128)` (sentinel `unk_13FCF4C`).

`VIBE_SceneGraph_TestNodeFlag @0x5ac6d8` gates the callback so a node is only processed if
its flag byte intersects the active mask — that is how the same walk does
geometry-only, anim-only, or mirror-only passes.

### 4.2 Octree frustum cull — `VIBE_SceneGraph_CullOctreeAgainstFrustum @0x5f09f0`

For spatial culling the engine stores nodes in an **octree** (a node has 8 child cells at
`*(node+0..7)` and an object list at `*(node+15)`). The cull:

```c
corners = VIBE_SceneGraph_TransformNodeBoxCorners(node, cam+76, cam+396);
r = VIBE_Render_ClassifyBoundingBoxPlanes(corners, 0, 0);   // 6-plane test (doc 24)
if (r & 0x40)              // box fully INSIDE → tag every object visible this frame
    for (o = node[15]; o; o = o[1])  *(o->renderRec+176) = frameStamp;
else if (r & 0x3F)         // box STRADDLES a plane → recurse into the 8 octants
    for each non-null child cell: CullOctreeAgainstFrustum(cell, cam, frameStamp);
// (r == 0) → fully outside → prune (return, touch nothing)
```

`0x40` = "fully inside" bit, `0x3F` = "intersects plane N" bits. The `frameStamp`
(`dword_649D58`, bumped each frame by `DrawUniverseAndStats`) stamped into each object's
`renderRec+176` is the visibility marker that `ProcessSceneNode` later reads
(`dword_649D58 == *(v8+176)` ⇒ trivially visible, skip per-node frustum test).

---

## 5. Node → draw-list records: `VIBE_Render_ProcessSceneNode @0x5add1c`

This is the per-node callback — it converts one visible scene node into draw-list entries.
Steps:

1. **LOD select.** `frame = VIBE_Mesh_SelectLodFrame(node)` (§5.1). If null, skip.
2. **Accumulate screen-space origin.** `dword_13FC574 += frame[+8]`, `dword_13FC54C +=
   frame[+12]` (running screen offset for batched geometry).
3. **Frustum cull (per node).** If the octree stamp doesn't match this frame, run
   `VIBE_Render_CullNodeAgainstFrustum @0x5ad588` and store the 6-bit result into
   `*(renderRec+2317)`. Bit `0x40` set ⇒ culled; the function returns early for those.
4. **T&L / vertex transform.** For surviving nodes it transforms vertices — either through a
   custom node transform `(*(frame+16)+496)()` or the default
   `VIBE_Mesh_InterpolateMorphVertices @0x5c953c` (skeletal/morph blend, doc 27) — then
   `VIBE_Render_ComputeVertexClipFlags @0x5ad614` computes per-vertex outcodes (doc 24) and
   `VIBE_Particle_UpdateBillboards @0x5ac970` orients any billboards.
5. **Lighting caches.** Depending on `node[+533]==4` (light-cached kind) and per-frame light
   budget counters (`dword_64A05C/60`, `dword_64A054/58`, `dword_64A064`), it may call
   `VIBE_Light_BuildObjectCache @0x5c8218`; shadow casters trigger
   `VIBE_Shadow_CastFromAllLights @0x5f444c` / `VIBE_Shadow_UpdateNodeShadows @0x5f4494`.
6. **Emit draw-list records.** The triangle loop walks the LOD frame's face array
   (`frame[+4]`, stride **40 bytes/triangle**) for up to `frame[+12]` triangles and appends
   one 8-byte record per visible (back-face-accepted) triangle. Two emit variants — HW
   (`byte_649D70`) and SW — differ only in how the sort key is computed (§5.2).

### 5.1 LOD selection — `VIBE_Mesh_SelectLodFrame @0x5adb6c`

The mesh's render record (`node+492`) holds `[+2316]` = LOD frame count and an array of
frames at `[+244]`, **stride 384 bytes/frame**. Selection:

```c
if ((node[+531] & 0x30) || !camera) {                 // forced LOD or no camera
    lod = ((node[+531]<<2 >> 6) - 1);                  // explicit LOD index from node flags
    if (lodCount-1 < lod) lod = lodCount-1;            // clamp
    frame = renderRec + 244 + 384*lod;
} else {                                               // distance-based LOD
    d = |node.pos - camera.pos|;                       // Euclidean distance
    metric = sqrt(dx²+dy²+dz²) * lodCount * flt_13FC774; // *world→screen scale
    // compare metric vs. a screen-size threshold (VIBE_Coord_ConvertX), pick frame
    frame = renderRec + 244 + 384*(int)lodLevel;
}
if (!frame[+8] || !frame[+12]) return 0;               // empty frame → cull node
if (frame != node[+460] /*last frame*/ || byte_64A068) node[+528] |= 0x40; // LOD changed
```

`flt_13FC774 = 1/projScale` is set in `SetupViewTransform` (§7) — so LOD is a function of
**screen-projected size**, not raw distance. Detail in doc 26.

### 5.2 Draw-list record layout and sort key

The draw list lives in a flat array. Three globals govern it (all set up by
`InitDisplayAndPaths`/`ShutdownEngine`):

| Global | Role |
|---|---|
| `dword_13FC584` | **base** of the draw-list array (record #0) |
| `dword_13FC570` | **write cursor** (advances by 8 per record) |
| `dword_13FC51C` | scratch/ping-pong buffer (radix sort output) |
| `dword_13FC770` | **record count** for the frame |

Each record is **8 bytes**:

```c
// gilde.exe — draw-list record (built in VIBE_Render_ProcessSceneNode @0x5add1c)
struct DrawListEntry {
    u32 sortKey;   // +0x00  radix sort key (depth / texture key, see below)
    u32 triPtr;    // +0x04  pointer to the prepared triangle (vertex/uv/material refs)
};
```

The **sort key** is computed two ways:

- **Software path** (`byte_649D70 == 0`): a **depth key** — max screen Z of the triangle's 3
  vertices, scaled into an integer bucket:
  ```c
  z = max(v0.z, v1.z, v2.z);
  rec.sortKey = (int)(flt_13FC774 * flt_628080 * flt_628084 * z);   // depth bucket
  rec.triPtr  = triangle;
  ```
  This is a **back-to-front depth sort** (painter's order) for the CPU rasterizer.
- **Hardware path** (`byte_649D70 != 0`): a **material/texture key** —
  `rec.sortKey = v27 + ((material - dword_1406A84) >> 7)` where `v27` is a high-bit class
  selector (`0x4000` for opaque vs `1` for the camera/special node) and the material pointer
  is quantized to a 128-byte slot index. This **batches by texture/material** to minimize D3D
  state changes (`SetTexture` calls) in `DrawTexturedTriangles`.

In both cases the key is **24/32-bit** and consumed by the radix sort as a sequence of
8-bit digits.

---

## 6. Sort → rasterize: `VIBE_Render_ResetEngineState @0x5af200`

Called at the very end of `BeginUniverseFrame`. It picks the sort width and the rasterizer
by the hardware flag:

```c
dword_649DA0 = 0;
if (byte_649D70) {                                   // HARDWARE (Direct3D)
    VIBE_Render_RadixSortDrawList(dword_13FC770, 1); // 3-digit sort (key < 2^24)
    VIBE_Render_DrawTexturedTriangles();             // D3D vertex buffers + DrawPrimitive  [Vulkan swap]
} else {                                             // SOFTWARE
    VIBE_Render_RadixSortDrawList(dword_13FC770, 0); // 4-digit sort (full 32-bit key)
    VIBE_Render_RasterizeMeshList();                 // CPU span raster into locked surface  [Vulkan swap: lock]
}
// reset accumulators for next frame
dword_13FC54C = dword_13FC574 = dword_13FC4E0 = 0;
dword_13FC570 = dword_13FC584;                        // rewind draw-list cursor
dword_13FC770 = 0;                                    // empty the list
```

### 6.1 The sort — `VIBE_Render_RadixSortDrawList @0x5aef34`

A textbook **LSD (least-significant-digit-first) radix sort**, 8 bits per pass, ping-ponging
between `dword_13FC584` and the scratch buffer `dword_13FC51C` (pointers tracked in
`dword_13FC4CC`/`dword_13FC4C8`). It uses a 256-bucket histogram at **`dword_13D8380`**
(1024 bytes = 256 × u32), zeroed each pass, prefix-summed, then a stable scatter:

```c
for each pass over digit byte b (b = 0,1[,2,3]):
    memset(hist, 0, 1024);
    for (i = 0; i < n; i++) ++hist[ key_byte_b(entry[i]) ];   // count
    for (j = 0; j < 255; j++) hist[j+1] += hist[j];           // prefix sum
    for (i = n-1; i >= 0; i--)                                // stable scatter (reverse)
        dst[--hist[byte_b(src[i])]] = src[i];                 // copies 8 bytes/record
```

The `a2` argument selects the pass count: **`a2 != 0` ⇒ 2 passes (digits 0,1)** then stops;
**`a2 == 0` ⇒ all 4 passes (digits 0,1,2,3)**. (The HW caller passes `a2=1` because its
material key fits in the low bytes; the SW caller passes `a2=0` for the full depth key.)
The result is a draw list ordered by sort key — back-to-front for SW, by-material for HW.

### 6.2 Software rasterizer — `VIBE_Render_RasterizeMeshList @0x5aec88`

Walks the sorted list **back to front** (`v44 = count-1; v1 = 8*v44; … v1 -= 8`). For each
record it:
- Reads the 3 triangle vertices (`v2[0..2]` = the three vertex pointers, `v2[3]` = clip
  context, `v2[4]` = uv/color attrs, `v2[5]` = material).
- Picks a raster routine from the function table **`dword_13D8780[]`** indexed by
  `v45[1]>>24` — a material/blend class dispatch (flat / textured / alpha / etc.).
- If any vertex needs clipping (`& 0x3F` outcode bits) it calls
  `VIBE_Render_ClipPolygonToPlane @0x5ad7d8` (Sutherland–Hodgman against the view planes,
  producing up to `dword_649D74` output verts), perspective-divides the survivors
  (`1/z`, then `flt_13FCD0C*x/z + flt_13FCD18`, `flt_13FCAF8*y/z + flt_13FCD10` — the
  viewport map from §7), and rasterizes the resulting fan.
- Brackets the whole loop with **`VIBE_Render_AcquireBackBuffer @0x4345d4`** /
  **`VIBE_Render_UnlockBackBuffer @0x434680`**, which lock/unlock the DirectDraw back
  surface so the CPU can write pixels (`byte_762721` selects the present mode). **[Vulkan
  swap: surface lock + CPU pixel write → staging buffer / compute upload.]**

Deep triangle/span fill, the projection map, and clipping are **doc 24**.

### 6.3 Hardware path — `VIBE_Render_DrawTexturedTriangles @0x5ae434`

The Direct3D equivalent. It opens with `VIBE_Render_BeginScene @0x5e010c`, then for each
sorted record fills a **D3D vertex buffer** obtained from
`VIBE_Render_GetVertexBufferInfo @0x5dd9c8` (24 floats / triangle: xyz·rhw, diffuse/specular,
uv). On every material change it flushes the accumulated triangles with
`VIBE_Render_DrawTriangleList @0x5dd9dc` and rebinds the texture via the device vtbl:

```c
dword_7626C8 = (*(...**)(*(_DWORD*)dword_64A320 + 152))(dword_64A320, 0, texHandle); // SetTexture  [Vulkan swap]
if (dword_7626C8) VIBE_Render_ReportDDrawError();   // d3_d3d.c error path
```

The `>>7` texture-key quantization in the sort key (§5.2) is exactly what minimizes these
`SetTexture` calls. It closes with `VIBE_Render_EndScene @0x5e0324`. **[Vulkan swap: the
entire BeginScene/DrawPrimitive/SetTexture/EndScene sequence maps to a Vulkan command buffer
with vertex-buffer uploads and descriptor binds.]**

---

## 7. Camera / view transform — `VIBE_Render_SetupViewTransform @0x5af5f8`

Establishes the projection used by both rasterizers. It is change-gated (only recomputes if
any of FOV/near/far/center/aspect changed). It:

- Pushes the projection to the device via `VIBE_Render_SetProjectionTransform @0x5de3e4`
  when `byte_649D7C` (HW device present). **[Vulkan swap: this writes the D3D projection
  matrix; in the port it feeds the Vulkan push-constant / UBO matrix. The matrix *values*
  are reconstructed 1:1.]**
- Computes the **viewport mapping constants** consumed everywhere in the raster paths:
  `flt_13FCD0C`/`flt_13FCD18` (x scale/offset), `flt_13FCAF8`/`flt_13FCD10` (y scale/offset),
  and `flt_13FC774 = 1.0 / aspect` (the world→screen scale used by LOD and depth keys).
- Builds the view matrix via `VIBE_Render_BuildViewMatrix @0x5accd0` and invalidates the
  cached object transform (`VIBE_Object_InvalidateCurrent @0x5af2e4`).

The screen extents `dword_13ECE58/5C/60/64` (left/top/right/bottom) it derives here also gate
the clear rect in `RenderMainViewFrame`. Math detail: doc 24.

---

## 8. Device lifecycle (outside the per-frame chain)

### 8.1 Init — `VIBE_Render_InitDisplayAndPaths @0x527fa4`
Startup-time. Reads graphics/sound settings (`VIBE_Config_ReadGfxAndSoundSettings`),
enumerates display modes (`VIBE_Render_EnumDisplayModes @0x43371c`), selects a resolution
(`dword_63D728 × dword_63D72C`), sizes and centres the Win32 window
(`AdjustWindowRectEx`/`SetWindowPos` — **[Win32→SDL]**), fills a ~24-DWORD device-desc
(`v26[]`, e.g. `[2]=16` bpp, `[7]=4096`, `[10]=32`, near/far/aspect at `[21..23]`) and creates
the device with **`VIBE_Render_InitEngineDevice @0x5af984`** — the `d3_d3d.c`
DirectDraw/Direct3D device + viewport. **[Vulkan swap: instance/device/swapchain creation.]**
It then creates default cameras (`VIBE_Universe_CreateDefaultCameras @0x5b5f48`), inits
DirectInput (**[→SDL]**), and wires the asset path strings (`"textures/"`, `"objects/"`,
`"animations/"`, `"groups/"`, `"scripts/"`, `x:\engine\gfx\scripts\`) before loading
`gilde.gfx`.

### 8.2 Present — `VIBE_Render_PresentFrame @0x4349e4`
Flips the finished back buffer to the screen. `byte_762721` selects one of 5 present modes
(0 = GDI `GetDC`+`BitBlt`; 1/3 = DirectDraw `Blt`/`Flip` via `dword_62D584` vtbl+20 / +44;
2/4 = lock-and-copy via `VIBE_Render_LockSurface @0x4343e4` then blit). It is called from the
GUI/window layer and the frame loop (`VIBE_GameLogic_RunFrameLoop`,
`VIBE_Window_RenderUpdates`, movie playback, the debug overlay). **[Vulkan swap: this whole
function becomes `vkQueuePresentKHR` on the SDL-owned swapchain; all `GetDC`/`BitBlt`/`Blt`/
`Flip`/`Lock` variants collapse to a single present.]**

### 8.3 Shutdown — `VIBE_Render_ShutdownEngine @0x5b0228`
Teardown. Clears `byte_649D71`, then for all 64 universe slots disposes objects
(`VIBE_Object_Dispose`), frees terrain/sky/particle/mirror buffers, releases the mesh and
texture caches (`VIBE_TextureCache_Shutdown @0x5ba428`, `…_d9c98`), frees the draw-list
buffers (`dword_13FC584`, `dword_13FC51C`), and finally — if `byte_649D7C` — releases the D3D
device objects (`VIBE_Render_ReleaseDeviceObjects @0x5de764`) and DirectDraw surfaces
(`VIBE_Render_ReleaseSurfaces @0x431f44`). **[Vulkan swap: destroy pipeline/buffers/swapchain
and the device.]** Covered alongside the global teardown in [doc 7](07-shutdown-sequence.md).

---

## 9. Phase B — `DrawUniverseAndStats @0x5b3bbc`

The second half of `RenderUniverseFrame`. Same re-entrancy guard. When `a2` (anim flag) is
set it:
- scrolls animated UV coords (`VIBE_Texture_ScrollUvCoords @0x5db094`),
- runs a scene walk with `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8` over the active slot, then
  over **all 64 universe slots** except the active one (advancing every object's skeleton for
  next frame — doc 27),
- updates the **FPS counters**: it stamps `dword_649D58` (the frame counter), and over
  sliding windows (`>0x3C` and `>0x0A` ms gaps, scaled by `uDelay`) computes
  `dword_649DC0` and `dword_649DC4` (the two FPS readouts the debug overlay shows).

The frame counter bump here is what `CullOctreeAgainstFrustum` compares against next frame to
mark trivially-visible objects (§4.2).

---

## 10. Summary — the contract for the port

1. **Walk → append → sort → raster, never interleaved.** The scene graph is traversed in
   full (`WalkAndInvoke` → `ProcessSceneNode`), every visible triangle appended as an 8-byte
   `{sortKey, triPtr}` record, and *only then* the list is radix-sorted and handed to one
   rasterizer. Preserve this ordering exactly — it determines draw order and therefore pixels.
2. **Two keying regimes.** SW = back-to-front depth bucket (painter's algorithm, 4-pass
   sort); HW = material/texture batching key (2-pass sort). Both must be reproduced bit-for-
   bit so the sort is stable and the visible result identical.
3. **Vulkan swaps are confined** to: clear, surface lock / pixel store, the
   BeginScene/DrawPrimitive/SetTexture/EndScene HW path, projection upload, present, and
   device create/destroy. The scene-graph walk, LOD selection, cull, draw-list build, sort
   key math, clip and perspective-divide are CPU logic reconstructed 1:1.
