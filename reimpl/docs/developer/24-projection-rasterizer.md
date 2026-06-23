# 24 — Projection & rasterizer

This chapter documents the **transform-and-lighting (T&L) / vertex-projection pipeline** and
the **software rasterizer** of `gilde.exe`. These are the lowest level of the render universe
chain ([23 — Render universe chain](23-render-universe-chain.md)): after the world has been
culled to a per-frame list of meshes, *this* code turns view-space float vertices into
screen-space fixed-point coordinates and walks triangles span-by-span, z-buffering and
texel-fetching into the back buffer.

The original ships **two** back ends that consume the *same* projected geometry:

* a **software rasterizer** (`VIBE_Raster_*`, `VIBE_Render_RasterizeMeshList @0x5aec88`), and
* a **Direct3D / DirectDraw fixed-function** path (`VIBE_Render_DrawTexturedTriangles @0x5ae434`),
  which packs the already-projected vertices into a D3D vertex buffer and submits triangle lists.

Per **rule 3**, the reimplementation keeps the projection and rasterizer **math 1:1** and runs
it either as a CPU reference (mirroring the software path) or as a Vulkan pipeline (replacing the
Direct3D path). The platform boundary is the GPU API only; every constant, fixed-point format and
rounding rule below must be reproduced exactly.

> Provenance note: addresses for the textured-triangle inner loop differ from earlier scoping
> notes. The authoritative software textured rasterizer is **`VIBE_Raster_RasterizeTexturedTriangle @0x5f7d58`**;
> `0x5f6c30` is the *mirror* variant `VIBE_Raster_RasterizeMirrorTriangle`.

---

## 1. The rounding primitive — `VIBE_Coord_ConvertX @0x5c6b08`

Everything that converts a `float`/`double` to an integer screen or fixed-point coordinate goes
through this one tiny helper (≈1101 xrefs). It performs an x87 **round-to-nearest-even** of the
value currently on top of the FPU stack and returns it on the stack (it is `__cdecl` with no
declared args — the operand is `st0` from the caller, the result is `st0`):

```asm
VIBE_Coord_ConvertX:           ; gilde.exe 0x5c6b08
    push    eax
    fstcw   word ptr [esp+4]   ; save current x87 control word
    push    [esp+4]
    mov     byte ptr [esp+1], 1Fh   ; set high byte of new CW = 0x1F..  -> RC=00 (round to nearest),
                                     ;   PC=11 (extended precision), all exceptions masked
    fldcw   word ptr [esp]     ; load the patched control word
    frndint                    ; round st0 to integer using RC=nearest
    fldcw   word ptr [esp+8]   ; restore the saved control word
    lea     esp, [esp+8]
    retn
```

Key facts to reproduce exactly:

* **Rounding mode is round-to-nearest, ties-to-even** (`frndint` with RC=00), *not* truncation.
  Whenever a caller does `v = ...; VIBE_Coord_ConvertX(); n = (int)v;`, the cast happens **after**
  `frndint` already rounded, so `(int)v` is an exact truncation of an already-integral value — i.e.
  the net effect is `round_half_to_even(v)`. Do not implement this as `(int)(x+0.5)`.
* The control word is saved/restored around the op, so it is locally scoped.

In the decompiler this appears as a bare `VIBE_Coord_ConvertX();` call sitting between the float
expression and its `(int)` truncation; treat that pair as a single `lround_even` operation.

---

## 2. Projection constants (exact bytes)

All projection/scale constants were recovered with `get_bytes`:

| Symbol | Address | Bytes (LE) | Value | Role |
|---|---|---|---|---|
| `dbl_610784` | 0x610784 | `3FE0000000000000` | **0.5** | round bias added to x/y in `VIBE_Coord_ProjectPoint` |
| `flt_628B94` | 0x628B94 | `3F600000` | **0.875** | screen x/y offset added in mesh projection |
| `flt_628B98` | 0x628B98 | `437E0000` | **254.0** | LOD/level clamp ceiling |
| `dbl_628B9C` | 0x628B9C | `3FA999999999999A` | **0.05** | backface threshold on `fabs(normal.y)` |
| `flt_62C3D4` | 0x62C3D4 | `47800000` | **65536.0** | 16.16 fixed-point scale (mirror raster) |
| `flt_62C3D8` | 0x62C3D8 | `47800000` | **65536.0** | 16.16 fixed-point scale (textured raster) |
| `flt_6280DC` | 0x6280DC | `3F000000` | **0.5** | half-pixel / center bias in view setup |
| `flt_6280E0` | 0x6280E0 | `47800000` | **65536.0** | 16.16 scale in view setup |
| `flt_6280E4` | 0x6280E4 | `BF800000` | **-1.0** | y-axis flip for vertical screen mapping |
| `flt_6280D0` | 0x6280D0 | `42480000` | **50.0** | depth/fog range constant |
| `flt_6280D4` | 0x6280D4 | `40800000` | **4.0** | particle/cost scale |
| `flt_6280D8` | 0x6280D8 | `3D2AAAAB` | **0.041666… (1/24)** | particle/cost scale |
| `flt_62A6F4` | 0x62A6F4 | `40000000` | **2.0** | aspect term in D3D projection matrix |

`65536.0 == 1<<16` is the conversion to **16.16 fixed-point**: a float screen coordinate `x` is
stored as `round_even(x * 65536.0)` and thereafter manipulated as an `int` in 16.16 format.

---

## 3. Camera / view transform

### 3.1 `VIBE_Render_SetupViewTransform @0x5af5f8`

`__stdcall (float a1..a7, int a8, int a9)`. Recomputes the view+projection state only when any
input differs from the cached globals (a dirty check against `flt_13FC77C…`, etc.). Its work:

* If `byte_649D7C` (Direct3D active) → calls `VIBE_Render_SetProjectionTransform(a1,a2,a3,a4,a5,a6)`
  to push the D3D projection/viewport matrices (§3.3).
* Computes the screen-mapping scale/offset used by the software path:
  * `flt_13FC518 = a1 * 0.5` (half-width scale), `flt_13FC514 = a2 * 0.5` (half-height scale).
  * `flt_13FCD18 = flt_13FC518 + a3` → **screen-space X center** (added per projected vertex).
  * `flt_13FCD10 = flt_13FC514 + a4` → **screen-space Y center**.
  * `flt_13FCD0C = a7` (horizontal projection scale), `flt_13FCAF8 = a7 * -1.0`
    (**vertical scale, negated** to flip y so screen-y grows downward).
  * `flt_13FC774 = 1.0 / a6` (reciprocal far/scale), `flt_13FCAFC = a6`, `flt_13FC76C = a5`.
  * `dword_13FC5C0 = round_even((a2+a4) * 65536.0)` — the screen bottom edge in 16.16
    (the initial "lowest scanline" seed used by the mirror rasterizer's top-vertex search).
* Calls `VIBE_Render_BuildViewMatrix()` (§3.2) and `VIBE_Object_InvalidateCurrent(0)`.

These four globals — **`flt_13FCD0C` (x scale), `flt_13FCAF8` (-y scale), `flt_13FCD18` (x center),
`flt_13FCD10` (y center)** — are the entire screen-space mapping consumed by both back ends in §4.

### 3.2 `VIBE_Render_BuildViewMatrix @0x5accd0`

Builds the rotation/orientation tables from the camera's two angles, derived via
`VIBE_Math_Atan2 @0x5f5701`:

```
yaw   = atan2(flt_13FC518, flt_13FCD0C)        ; (half-width-scale, x-scale)
pitch = atan2(cos(yaw), sin(yaw))
```

It then fills a set of per-axis rotation rows (`flt_13DCDA0…`) using `sin`/`cos` of those angles,
composes them via `VIBE_Math_MatrixFromEuler` (§3.4), and bakes a **64-entry table**
(`dword_13DB398`, stride 26 dwords) indexed by a 6-bit mask: for each `v6` in `0..0x3F`, the bits
of `v6` select which axis-rows are appended, so the table is a precomputed set of partial rotation
matrices selected per object by an orientation bitmask. Two hard-coded 1.0/scale fixups
(`flt_13DCDEC = flt_13FC76C`, `flt_13DCDFC = -flt_13FCAFC`, `dword_13DCDF8 = -1082130432 = -1.0f`)
fold the projection scale and y-flip into the matrix.

### 3.3 `VIBE_Render_SetProjectionTransform @0x5de3e4` (Direct3D path → Vulkan)

`__stdcall(float a1..a6)`. Builds and uploads the **D3D fixed-function projection matrix** and
viewport via the device vtable (`IDirect3DDevice` at `dword_64A320`/`dword_64A324`):

* Projection matrix (44-byte D3DMATRIX-style record `v23`), built only when `(a1,a2,a3,a4)` change:
  * `aspect = a2 / a1`; `m[2][1]`-style term `= aspect * 2.0` (`flt_62A6F4`).
  * Several entries rounded through `VIBE_Coord_ConvertX`. Diagonal/`w` fixups:
    `v23[5] = -1.0f`, `v23[7] = 2.0f (0x40000000)`, `v23[10] = 1.0f`.
  * `a1,a2` = projection width/height, `a3,a4` = center offsets.
  * Submitted via vtable slot `*(+68)`.
* Viewport / near-far transform, rebuilt only when `(a5,a6)` (near, far) change:
  * Identity-ish 64-byte `v19`; then `v19.minz=1.0f`, `v19.maxz = a5`,
    `v19[?] = a5/(a6-a5) + 1.0` → the standard D3D z-range mapping.
  * Submitted three times via vtable slot `*(+100)` (RENDERSTATE/transform setters).

For the Vulkan reimplementation, reproduce this matrix math 1:1 and feed it as the projection
UBO; the vtable submission is the GPU-API boundary that gets swapped.

### 3.4 `VIBE_Math_MatrixFromEuler @0x5cb1bc`

Classic Euler→3×3 (stored as a 4×4 with translation column zeroed, `m[3][3]=1.0`). With
`a1 = {rx, ry, rz}` (radians) and `sX=sin, cX=cos`:

```
sy=sin(ry); cx=cos(rx); cy=cos(ry); cz=cos(rz); sz=sin(rz); sx=sin(rx)
m00 = cy*cz
m10 = cy*sz
m20 = -sy
m01 = sx*(sy*cz) - cx*sz
m11 = (sy*cz)*cx + sx*sz          ; note: composed as v5*v3 + v8*v12 in the decompile
m21 = sx*cy
m02 = (sy*sz)*cx - sx*cz
m12 = (sy*sz)*sx ... (see decompile for exact term ordering)
m22 = cx*cy
```

Reproduce the **exact term ordering** from the decompile (float associativity matters for 1:1
bit-equality): e.g. `m11 = v5*v3 + v8*v12` with `v5 = sy*cz`, `v3 = cos(rx)`, `v8 = sin(rx)`,
`v12 = sin(rz)`.

---

## 4. Vertex projection (T&L)

Two projection entry points share the same screen mapping but differ in how they fetch the source
point.

### 4.1 `VIBE_Coord_ProjectPoint @0x407428` (perspective divide + screen map)

`__usercall(float *a1@<eax> /*camera*/, float *a2@<edx> /*world point*/, _DWORD *a3@<ebx> /*out screen[2]*/)`

```c
dx = a2[0] - a1[0];                 // world - camera, x
dz = a2[2] - a1[2];                 // world - camera, z
inv = 1.0 / a1[4];                  // 1 / camera-w  (a1[4] is the projective denominator)
sx  = dx * inv;
sy  = inv * dz;
out[0] = round_even(sx + 0.5);      // 0.5 == dbl_610784 ; ConvertX then (int)
out[1] = round_even(sy + 0.5);
```

So the basic projection is an **orthographic-style divide by a fixed camera-w** (the engine is a
fixed-isometric camera; `a1[4]` is constant per frame), plus a `+0.5` round-to-pixel bias.

### 4.2 `VIBE_Coord_ProjectFramePoint @0x407488`

Thin wrapper: `VIBE_Heightmap_TileToWorld(x, y)` → world point, then `VIBE_Coord_ProjectPoint`.
Returns 0 if the tile is off-map. Used to project terrain/heightmap tile corners.

### 4.3 `VIBE_Mesh_ProjectVerticesToScreen @0x5c5120` (the per-mesh T&L + cull)

`__usercall(int a1@<eax> /*mesh*/, int a2@<edx> /*verts*/)`. For each of the 3 candidate vertices
it precomputes `1/w` (`v30[i+3] = 1.0 / vert[i].w`), then for every vertex of the mesh:

* `screen.x = (vert.x - cam.x) * (1/w) + 0.875`  (`flt_628B94`)
* `screen.y = cam_y_offset + (vert.z - cam.z) * scaleZ`
* A per-vertex **level/LOD value** `v39` is computed from `(vert.y - cam.y) * scaleY`, clamped:
  `< 1.0 → 1.0`; `> flt_628B98 (254.0) → 254.0`; the result is rounded via `ConvertX` and stored as
  a **byte** at `vert+66`. (This byte later picks a light/shade level palette — see §6 and
  [25 — Lighting & FX](25-lighting-fx.md).)

Then it walks the mesh's triangle list (`v17` = `min(visibleCount, tri count)`), and for each tri:

* `VIBE_Math_TriangleNormal(v0,v1,v2, &n)` (§4.4) → face normal `n`.
* **Backface / facing test:** keep the tri if `fabs(n.y) >= 0.05` (`dbl_628B9C`) OR the "force"
  flag bit `0x40` is set; otherwise mark it degenerate (`|= 0xC0`).
* A signed-area sign test in screen space sets the **winding/back-face bit** `0x80`:
  `area = x0*y1 - y0*x1 + ... (the 2D cross product of the three projected screen positions)`;
  `if (area < 0.0)` set bit 7 of the tri flags.
* If the tri is front-facing, fully on-screen (all six screen coords in `[0, vert.screenSize)`),
  and not already-clipped, it is **appended to the global draw list**: `dword_13FC570` (write
  cursor) receives `{768 * level, triPtr}` and `dword_13FC770` (the visible-tri counter) is
  incremented. `level = max(v0.byte66, v1.byte66, v2.byte66)` (the brightest of the 3 vertices),
  and `768 = 256*3` indexes a per-level RGB shade table.

`dword_13FC770` (visible triangle count) and `dword_13FC584/13FC570` (draw-list base/cursor) are
the hand-off to the rasterizers in §5.

### 4.4 `VIBE_Math_TriangleNormal @0x5cb824`

Standard cross product of two edges, then `VIBE_Math_VectorNormalize`:

```
e1 = v1 - v0 ;  e2 = v3 - v0   (v3 = a4, the third vertex)
n.x = e1.z*? ...  ->  n = (e2.y*e1.z - e1.y*e2.z, ... )   ; see exact ordering in decompile
n = normalize(n)
```

(Exact component ordering: `n[0]=v10*v6 - v9*v7`, `n[1]=v8*v7 - v10*v5`, `n[2]=v9*v5 - v8*v6`,
with `v5,v6,v7 = v1-v0` and `v8,v9,v10 = a4-v0`.)

---

## 5. Software rasterizer

### 5.1 Driver — `VIBE_Render_RasterizeMeshList @0x5aec88`

Iterates the visible-triangle list **back-to-front** (`v1 = 8*(count-1)`, stepping `-8`, i.e. the
list is already depth-sorted so it walks from far to near for painter-style overdraw). For each
entry `{level, triPtr}`:

* Acquires the back buffer (`VIBE_Render_AcquireBackBuffer`), processes, then
  `VIBE_Render_UnlockBackBuffer` at the end.
* Chooses a rasterizer **`v45[4]`** = 4 (textured) by default, 3 if the material is partially
  translucent (`*(v5+108) - (*(v5+110)&1) < 0xFF`).
* Sets up the three vertex pointers, clips against the active plane via
  `VIBE_Render_ClipPolygonToPlane @0x5ad7d8` (which may emit `dword_649D74 ≥ 3` clip vertices),
  re-projects the clipped vertices to screen (the `1/z` perspective divide again, using
  `flt_13FCD0C`, `flt_13FCAF8`, `flt_13FCD18`, `flt_13FCD10`),
* then **dispatches through a function-pointer table** `dword_13D8780[flags>>24]`, which is the
  per-material rasterizer selector. (The table is empty in the static IDB — it is populated at
  runtime by the render-init code; its entries are the `VIBE_Raster_*Triangle` functions below.)

The clip emits a triangle **fan** (`dword_649D74 - 2` triangles) over the clipped polygon, each fed
to the selected rasterizer.

### 5.2 Screen-space setup, top-vertex search, edge tables

Every `VIBE_Raster_*Triangle` begins identically:

1. **Signed-area / winding sign** decides vertex traversal order. For the textured rasterizer
   (`@0x5f7d58`) the leading test is
   `a1[4]*a1[3] - a1[5]*a1[2] + a1[2]*a1[1] - a1[3]*a1[0] + a1[0]*a1[5] - a1[1]*a1[4] <= 0.0`
   (the 2D cross product / signed area of the three screen positions). `<= 0` ⇒ walk vertices
   forward (`i=0,1,2`); else walk them **reversed** (the `else` branch iterates `v10 -= 2`,
   `--v8`). This makes the triangle's vertices consistently wound before edge walking.

2. For each of the 3 vertices: convert screen X and Y to **16.16 fixed-point**:
   `xfix = round_even(screenX * 65536.0)`, `yfix = round_even(screenY * 65536.0)` (constant
   `flt_62C3D8 = 65536.0`), stored to per-vertex parallel arrays `dword_13FC5B0[i]` (X 16.16) and
   `dword_13FC59C[i]` (Y 16.16). The **texture/light index** byte is widened to 16.16 too:
   `dword_13FC578[i] = texByte << 16`.

3. **Top-vertex search:** track the vertex with the **minimum Y** (`v16 >= y → v7 = i`). `v7` is the
   top vertex; if no valid top found (`v7 == -1`) the triangle is rejected.

4. **Edge-adjacency tables** (recovered bytes):
   * `dword_5AC540 @0x5ac540` (stride 2 dwords) = **next**: `{1, 2, 0}`
   * `dword_5AC544 @0x5ac544` = **prev**: `{2, 0, 1}`
   These give, for the top vertex, the two edges going down-left and down-right. The code compares
   `min Y` against the neighbours' Y to decide whether the top is a single apex or a flat top, and
   assigns `(v19,v20,v21)` = the long edge and the two short edges accordingly.

### 5.3 Edge interpolation (fixed-point, with the `+0xFFFF` ceil)

Two helpers compute per-scanline slopes in 16.16:

**`VIBE_Raster_InterpolateEdgeZ @0x5f6a8c`** — interpolates **X** down an edge:

```c
dy = Y[b] - Y[a];                       // 16.16
if (dy >= 0x10000)                      // edge spans >= 1 pixel
    slope = ((int64)(X[b]-X[a]) << 16) / dy;     // 16.16 dX/dY
else
    slope = ((0x40000000 / dy) * (int64)(X[b]-X[a])) >> 14;  // reciprocal trick for sub-pixel edges
// snap start X to the first integer scanline:
yTop  = Y[a];
startX = X[a] + ((int64)slope * (((yTop + 0xFFFF) >> 16 << 16) - yTop)) >> 16;
```

The expression `((y + 0xFFFF) >> 16) << 16` is **ceil-to-integer in 16.16** — it rounds the
fractional Y *up* to the next scanline (top-left fill rule). The pre-step `((ceil(y)) - y)` advances
the X accumulator from the true vertex Y to the first covered scanline.

**`VIBE_Raster_InterpolateEdgeZTex @0x5f7840`** — same, but interpolates **both** X
(`dword_13FC5E8` slope, `dword_13FC5D8` start) **and the texture/light coordinate**
(`dword_13FC5C4` slope, `dword_13FC5F4` start) down the edge, using the identical `dy`/`0x40000000`
reciprocal branch.

The `0x40000000 / dy` then `>> 14` path is the **exact** reciprocal-multiply used for edges shorter
than one pixel; it must be reproduced bit-for-bit (it differs from the `<<16 / dy` long-edge path in
rounding).

The per-triangle **U gradient across the span** (`dword_13FC590`) is computed once from the screen
positions and texcoords:

```c
e1y = a1[1]-a1[3];  e2y = a1[5]-a1[3];
det = (a1[0]-a1[2])*e2y - (a1[4]-a1[2])*e1y;             // v45, 2D edge determinant
dUdx = (det & 0x7FFFFFFF) ? (int)( ((u0-u1)*e2y - (u2-u1)*e1y) * (65536.0/det) ) : 0;
```

i.e. `du/dx = 65536 * (texArea / screenArea)` — an **affine** horizontal texture gradient (see §5.6).

### 5.4 Span filler — `VIBE_Raster_FillTexturedSpansShaded @0x5f7960`

Walks scanlines from the top vertex (`a5`) for `a1` lines. Per scanline:

* `xL = ceil16(dword_13FC5D8)`, `xR = ceil16(dword_13FC5BC)`; `width = xR - xL` (`dword_13FC588`).
* If width > 0 and the "draw texels" flag (`(a4>>1)&1`) is set, it runs the **inner texel loop**:
  starting texel value `dword_13FC5F4 + (dword_13FC590 * ((xL<<16) - dword_13FC5D8) >> 16)`,
  `ROR`'d by 16, and steps by `dword_13FC590` per pixel, writing one byte per pixel into
  `dword_13FC5DC + xL`. (The `__ROR4__(...,16)` packs the 16.16 fractional/integer halves so the
  high word — the integer texel index — lands in the low byte for the store.)
* It additionally writes **shadow/coverage** bytes (`v44`, `v47`, with 24-byte stride into
  `dword_1408AA4`) when `v43 = (a4 & 5)` is set — this is the per-pixel light/shadow accumulation
  buffer feeding [25 — Lighting & FX](25-lighting-fx.md). `dword_1408AA0` is a shadow-spread radius;
  the nested loops bleed the coverage value `±dword_1408AA0` pixels.
* Per-scanline stepping: `dword_13FC5D8 += dword_13FC5E8` (left X), `dword_13FC5BC += dword_13FC5C8`
  (right X), `dword_13FC5F4 += dword_13FC5C4` (texcoord), `dword_13FC5DC += a2` (dest row pitch),
  `dword_1408AA4 += 24*a2` (coverage row).

The triangle is filled as **two trapezoids** (top half then bottom half), each calling
`FillTexturedSpansShaded`; `VIBE_Raster_RasterizeTexturedTriangle` orchestrates the two halves by
re-interpolating the edge that changes slope at the middle vertex (`v41` selects which side is the
long edge).

### 5.5 The textured/z inner loop — `VIBE_Raster_FillSpanTextured @0x5f71ad` (self-modifying!)

The mirror/textured-with-z path (`VIBE_Raster_RasterizeMirrorTriangle @0x5f6c30` →
`VIBE_Raster_FillSpanLoop @0x5f6b34` → `VIBE_Raster_FillSpanTextured @0x5f71ad`) interpolates
**U, V and Z** per pixel using full RGBZ edge interpolation (`VIBE_Raster_InterpolateEdgeRgbz
@0x5f6930`, which carries X (`13FC5E8`), U (`13FC5CC`) and V (`13FC5D0`) slopes).

**Critical 1:1 detail:** the inner texel loop at `0x5f71ad` is **self-modifying code**. The
decompiler shows placeholder immediates `0x12345678`, `305419896` (= `0x12345678`) and
`word_1234567` / `dword_13DCE54` because the real values are **patched in at run time** by
`VIBE_Raster_PatchSpanConstantsTextured @0x5f7500`, which rewrites the instruction operands before
each triangle:

| Patch site | Source | Meaning |
|---|---|---|
| `loc_5F71E4 + 2` | `dword_1406A88` | **UV mask** AND-ed against the packed UV accumulator (`v7 & mask`) |
| `loc_5F71EF + 2` | `unk_1406A8C` | **texel base address** (`*(v12 + texbase)` fetches the palette index byte) |
| `loc_5F7202 + 1` | `dword_1406A78` | **palette base** (`word_PAL[index]` → the 16-bit pixel) |
| `loc_5F71C7 + 2` | `byte_1407A91` | **texture width shift** (log2 of texture width, for V<<shift) |
| `loc_5F71EA + 1` | `dword_13FC594` | per-triangle **V start** (16.16) |
| `loc_5F71FC + 2` | `dword_13FC5A8` | per-triangle **V step** packed value |

So the inner loop is effectively:

```c
// after patching, per pixel:
idx   = *(u8*)((uv & UV_MASK) + TEX_BASE);   // sample 8-bit texel
pixel = PAL[idx];                            // 16-bit 565 color from the active palette
*(u16*)dst = pixel;                          // write to back buffer
uv += UV_STEP;  dst += 2;                    // step one texel, one pixel (2 bytes)
```

* **Pixel format is 16-bit 565** (the palette `word_*` table emits `u16` values written 2 bytes at
  a time into the back buffer — note all dest stores are `*(_WORD *)`).
* **Texture addressing is affine** within a span (a single additive `UV_STEP` per pixel, *not*
  per-pixel perspective divide). Perspective correction is applied only **per-vertex** (the `1/w`
  divide in §4) — the engine is affine-textured across each triangle. This matters for 1:1 visual
  fidelity (affine texture "swim").
* **Z buffering / depth:** the level byte `dword_13FC5E0` and the row index machinery
  (`dword_13FC5D4 = 2*scanline*pitch + base`) gate the writes; the painter-order far-to-near walk in
  the mesh-list driver (§5.1) plus the per-triangle top-down fill provide the depth ordering.

### 5.6 Summary of the rasterizer math (for the CPU reference)

* **Fixed-point:** all screen X/Y and texcoords are **16.16**. Conversion in is
  `round_even(f * 65536.0)`; the fractional→integer scanline snap uses **ceil**:
  `ceil16(v) = (v + 0xFFFF) >> 16`.
* **Edge slope:** `(Δcoord << 16) / Δy` for `Δy ≥ 1px`; `((0x40000000 / Δy) * Δcoord) >> 14` for
  sub-pixel edges. Reproduce both branches exactly.
* **Top-left fill rule:** spans run `[ceil16(xL), ceil16(xR))`; scanlines run from `ceil16` of the
  top vertex Y. Pre-step the accumulators by `(ceil16(y0)<<16) - y0`.
* **Fan from clip:** a clipped polygon of `n` vertices rasterizes as `n-2` triangles.
* **Backface cull:** screen-space signed area sign (§4.3, §5.2); face-normal `|n.y| ≥ 0.05` keep
  test in projection.
* **Texture:** affine UV, 8-bit indexed → 565 palette lookup, self-modifying inner loop.

---

## 6. Direct3D path → Vulkan — `VIBE_Render_DrawTexturedTriangles @0x5ae434`

This is the alternative consumer of the same projected geometry, used when the hardware D3D device
is active. It does **not** call the software `VIBE_Raster_*` functions; instead it:

1. `VIBE_Render_BeginScene` / `VIBE_Render_GetVertexBufferInfo` to lock a D3D vertex buffer
   (`v74`, capacity `v89-128`).
2. For each visible triangle (`dword_13FC770` of them): when the material/texture changes
   (`v75 != mesh.material`), flush the current batch with `VIBE_Render_DrawTriangleList @0x5dd9dc`,
   re-lock, set the texture (vtable `+152`), and set the blend mode via `VIBE_Render_SetBlendMode`
   (`material+108 != 0xFF` ⇒ alpha-test; `material+110` bits ⇒ alpha-blend / src-over flags).
3. Writes each vertex into the D3D vertex (a 24-float / 96-byte stride `TLVERTEX`):
   * Screen X,Y come from the projected `flt_13FCD0C/AF8/D18/D10` mapping plus per-mesh UV/light
     offsets `flt_13FC4D0`/`flt_13FC4D4` (from material light-row tables
     `flt_1406950[material+105 & 0x1F]`, `flt_14069D0[material+106 & 0x1F]`).
   * Depth: `(viewZ + flt_13FC4DC) * flt_13FC4D8`, where `flt_13FC4D8 = 1.0 / (zNear + 50.0 - zMin)`
     (`flt_6280D0 = 50.0`) — the **w/z mapping into the D3D [0,1] depth range**, the analogue of the
     software path's level byte.
   * The same `1/w` perspective divide (`v36 = 1.0 / vert[2]`) and screen map
     (`flt_13FCD0C * x * v36 + flt_13FCD18`, `v36 * (flt_13FCAF8 * y) + flt_13FCD10`) is applied to
     the clipped fan vertices — **identical math to the software path** (§5.1).
4. A per-batch **back-face reject** in screen space:
   `(y0-y1)*(x0-x2) > (y0-y2)*(x0-x1)` rewinds the write cursor (drops the triangle) when the
   winding flag indicates a back face. Same sign convention as §4.3.
5. `VIBE_Render_DrawTriangleList` / `VIBE_Render_EndScene` submit and present.

For the Vulkan reimplementation, items (1)(2)(5) (BeginScene, vertex-buffer lock, texture/blend
state, DrawTriangleList, EndScene) are the **GPU-API boundary** replaced by a Vulkan pipeline +
descriptor sets + draw calls. Everything in (3)(4) — the projection, depth mapping, UV/light offsets
and back-face math — is reconstructed **1:1** and uploaded as vertex data / push constants. The CPU
reference (the software path, §5) and the Vulkan path must produce the same projected vertices and
the same cull decisions.

---

## 7. Cross-references

* [23 — Render universe chain](23-render-universe-chain.md) — what fills `dword_13FC770` /
  `dword_13FC584` (the visible-triangle list) before this stage, and `BeginUniverseFrame @0x5b3900`.
* [25 — Lighting & FX](25-lighting-fx.md) — the per-vertex **level byte** (`vert+66`, §4.3), the
  `768`-stride shade table, the shadow/coverage buffer written by `FillTexturedSpansShaded`, and the
  16-bit 565 palette.
* [26 — Mesh & LOD](26-mesh-asset-lod.md) — mesh/triangle struct layout, the LOD level clamp
  (`254.0`), and the material flags consumed by the rasterizer dispatch table `dword_13D8780`.
* [31 — Math & util](31-math-util-strings.md) — `VIBE_Math_Atan2 @0x5f5701`,
  `VIBE_Math_MatrixFromEuler @0x5cb1bc`, `VIBE_Math_TriangleNormal @0x5cb824`,
  `VIBE_Math_VectorNormalize`, and the `VIBE_Coord_ConvertX @0x5c6b08` rounding primitive.
