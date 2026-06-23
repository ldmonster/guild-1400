# 25 — Lighting, Shadow, Weather & FX

How `gilde.exe` lights the world, casts blob shadows, drives the day/night colour
cycle, and renders particles, snow, rain and thunder. Everything here is reconstructed
from the IDA decompilation of the original 32-bit binary (imagebase `0x400000`); the
binary is the source of truth.

These subsystems are pumped once per frame from the per-frame loop
([14 — Per-frame loop](14-per-frame-loop.md)) in this order: `VIBE_Weather_UpdateSky`,
`VIBE_DayCycle_UpdateBrightness`, `VIBE_Weather_RenderAndThunder`, `VIBE_Rain_Render`.
Lighting writes per-vertex colour bytes that the rasterizer
([24 — Rasterizer](24-projection-rasterizer.md)) consumes; the day-cycle bands and sky
colours come from baked tables keyed by season and game time ([15 — Game time](15-game-time-tick.md)).

> **Platform boundary.** Lighting is pure CPU math — it only produces vertex colours
> (`vertex.color`/`vertex.specular` bytes) that feed the rasterizer. The FX renderers
> (`VIBE_Rain_Render`, `VIBE_Snow_Render`) submit triangle batches through the original
> Direct3D device vtable (`dword_64A320`, methods at +112 `DrawPrimitive`, +152
> `SetTexture`); in the reimplementation those calls route through the Vulkan
> `IGraphicsDevice` swap (rule 3). The math, vertex layout, batch caps and constants
> documented here are preserved 1:1; only the device underneath changes.

---

## 1. Vertex lighting model

The engine uses **per-vertex shading** with two sources: a cheap **flat directional/colour
shade** for most static geometry, and an **environment-reflection ("env-map") walk** used
for animated meshes whose env-map normal is rotated through the bone/world transform each
frame.

### 1.1 Flat vertex shade — `VIBE_Light_ApplyVertexShading @0x5c7f04`

Walks each sub-mesh of an object. For each vertex it computes a single grey luminance
from the vertex normal dotted against three baked light-direction weights plus the light's
high byte, scales and clamps to 255, then writes that grey into the vertex colour **and**
specular dwords:

```c
// per vertex (object node a1, mesh a2):
v17 = (normal.x*flt_628C64 + normal.y*flt_628C68 + normal.z*flt_628C6C + lightHi)
      * flt_628C70 * (float)(u8)lightLo;
v14 = (v17 <= 255.0) ? v17 : 255.0;              // clamp
grey = (int)v14;
*(u32*)(vert+64) = grey<<24 | grey<<16 | grey<<8 | grey;  // color
*(u32*)(vert+68) = same;                                   // specular
```

- `lightLo`/`lightHi` come from the per-vertex light id (`*(light+108)` when node flag
  `+529 & 0x80==0`, else `mesh[94]`). Bit `0x10000` of that id selects the dot-product
  path; otherwise the byte `(u8)id` is splatted directly as a flat colour (skipped when
  `id == 0xFF`, the "fully lit / no shade" sentinel).
- `flt_628C64..6C` are the global light direction weights, `flt_628C70` is the intensity
  scale, `flt_628C74 == 255.0` is the clamp ceiling.
- Flag `+530 & 1` clears bit `0x4` of the per-face flag (`+38`) and re-derives it from
  `8*(u8)*(light+104) >> 7` (the light's "enabled" bit). Flag `+530 & 2` skips the whole
  pass (already-baked / unlit object).

### 1.2 Env-map reflection walk — `VIBE_Mesh_ComputeVertexLighting @0x5c9054`

For animated meshes (`__usercall`, `eax=mesh-cache, edx=object`). Preconditions: object
has an active light in its 16-slot light array (`*(light+104) & 1`), and the mesh has
vertices (`*(meshDef+480) > 0`).

It first rebuilds the bone world matrix
(`VIBE_Transform_ComputeBoneWorldMatrix`, with `dword_13FCD1C` as the frame matrix when
node `+528 >= 0`), then finds the highest-priority animation layer
(`VIBE_Anim_FindHighestPriorityLayer`). If that layer supplies a packed env-normal table
(`v6 = 192*layer[0] + *(layer[26]+348)`), the **reflection path** runs; otherwise the
**static-normal path** runs.

**Reflection path** — for each vertex `v10` whose flag `+77` is set, unpack a 3-byte
signed env-normal and dequantize:

```c
n = ((double)(i16)byte + flt_628CC8) * flt_628CC4;   // flt_628CC8=-128.0, flt_628CC4=1/127.5
// rotate n by the cached frame basis rows (v20..v28):
r = n·basis;
d = (r·vertNormal) * flt_628CBC;                      // flt_628CBC = -2.0  (reflection)
refl = r + d*vertNormal;                              // reflect across normal
VIBE_Math_VectorNormalize(&refl);
vert[+32] = refl.x*0.5 + 0.5;                          // flt_628CC0 = 0.5  -> UV in [0,1]
vert[+36] = 0.5 + refl.y*0.5;                          // env-map texcoords
```

The static-normal path is identical except the un-rotated mesh normal
(`*(vert+72)+12/16/20`) is used in place of the unpacked env-normal. **Key fact:** this
function does *not* write vertex *colours* — it writes the **environment-map UV
coordinates** (`vert+32`, `vert+36`) used to sample a reflection texture; the colour
shade itself is the job of §1.1. `R = N - 2(N·V)V` with the `0.5*x+0.5` remap is the
classic sphere-env-map projection.

Dequant constants (`@0x628cbc`): `-2.0, 0.5, 0.0078431(=1/127.5), -128.0`.

### 1.3 Helpers

| Function | Addr | Role |
|---|---|---|
| `VIBE_Light_EnableSun` | `0x42dc5c` | Sets sun-ray request flag `dword_62D568=1`, stamps start time `dword_62D56C = now`. |
| `VIBE_Light_EnableDaylight` | `0x504a00` | Frees old octree (`dword_634488`), flags everything for redraw (`VIBE_Character_FlagRedrawByMode`), rebuilds the region octree (`VIBE_SceneGraph_BuildOctreeForRegion(0,64,7,8)`). |
| `VIBE_Light_SetGrayColorThunk` | `0x5c6af0` | Splats one grey byte `a1` across all 4 bytes of a colour and bulk-fills `a2` dwords at `a3` (`VIBE_Memory_FillDwordAlignedThunk`) — used to flood a vertex block with a uniform grey. |
| `VIBE_Light_RefreshAllObjects` | `0x5c886c` | Re-runs the lighting cache over every lit object after a band change. |

---

## 2. Day / night cycle

### 2.1 Time-table build — `VIBE_DayCycle_BuildTimeTable @0x4b2438`

Picks a season (`VIBE_GameTime_GetSeasonFromDay`), indexes a 6-entry-per-season table of
**sunrise/​sunset minute marks** at `dword_4AD160` (`6 * season`), and for each of the 6
marks splits minutes into `H = mins/60`, `M = mins - 60*H`, stores them in
`byte_631AB4[]`/`byte_631AB5[]`, and writes the absolute second-of-day
`3600*H + 60*M` into the band-boundary array `dword_631AAC[]` (entries at +12-dword
stride). These boundaries (`dword_631AB8`, `…AC4`, `…AD0`, `…ADC`, `…AE8`, `…AF4`) are the
edges of the brightness bands.

The raw season table (`@0x4ad160`, first season shown) is minute marks
`{ 0x186, 0x1FE, 0x276, 0x41A, 0x492, 0x50A }` = `{390, 510, 630, 1050, 1170, 1290}`
minutes — i.e. ~06:30, 08:30, 10:30, 17:30, 19:30, 21:30.

### 2.2 Brightness bands — `VIBE_DayCycle_UpdateBrightness @0x4b2504`

Converts the current clock (`3600*hour + 60*min` from the game-time struct at `a1+4/+6`)
into a single **0–600 brightness scalar** by piecewise-linear interpolation across the
**seven boundaries**, producing **six bands**:

| Time vs. boundary | `v10` (brightness) | Meaning |
|---|---|---|
| `t < B0` (`631AB8`) | `0` | full night |
| `B0 ≤ t < B1` (`…AC4`) | `100*(t−B0)/(B1−B0)` → 0..100 | pre-dawn ramp |
| `B1 ≤ t < B2` (`…AD0`) | `100 + 100*(t−B1)/(B2−B1)` → 100..200 | dawn |
| `B2 ≤ t < B3` (`…ADC`) | `200 + 200*(t−B2)/(B3−B2)` → 200..400 | morning → full day |
| `B3 ≤ t < B4` (`…AE8`) | `400 + 100*(t−B3)/(B4−B3)` → 400..500 | afternoon |
| `B4 ≤ t < B5` (`…AF4`) | `500 + 100*(t−B4)/(B5−B4)` → 500..600 | dusk |
| `t ≥ B5` | `600` | full night again |

That `0..600` scalar is mapped onto the **7 baked lighting bands** via
`v3 = brightness * dbl_61DD68`, `band = (int)v3 % 7`, `frac = v3 − (int)v3` and handed to
`VIBE_SkyColor_BlendBandLighting(band, frac, 1.0, changedFlag)`. So the menus and the
in-world view both use a **7-entry colour table interpolated by a fractional band index**.

Hysteresis: the function caches the last band/frac/region (`dword_11BC1E8`,
`flt_11BC1EC`, `dword_11BC1F0`) and skips the expensive re-blend when within tolerance
(`|frac−1| < dbl_61DD58`, `|band−last| < 10`, same region) — otherwise it forces a redraw
(`v9 = 1`). Interior/indoor regions (`dword_13ECF74[246*region]`) bypass world lighting
and just apply a flat ambient blend. Torch flicker brightness is folded in from
`*(dword_11BC1D8 + 38) * flt_61DD50` (`flt_61DD50 = 1/255`).

When the band crosses certain thresholds it also re-rolls the **sun height**:
`VIBE_Light_SetSunHeight @0x4b24b0` sets `*(light+420)` to a randomized elevation —
descending `-0.3 − rand*0.6` (`dbl_61DD40=−0.3`, `dbl_61DD38=0.6`) or ascending
`0.3 + rand*0.6` (`dbl_61DD48=0.3`) depending on the day-half flag `byte_631D9C`.

### 2.3 Band lighting blend — `VIBE_SkyColor_BlendBandLighting @0x5b85e4`

Takes `(band a1 in 0..6, frac a2 in 0..1, scale a3, changedFlag a4)`; rejects out-of-range
band/frac. It:

1. Pushes `band + frac` time-of-day to every scene node
   (`VIBE_SkyColor_SetTimeOfDay`, traversal type 30).
2. Lerps the **global ambient light colour** `flt_64A070..8C` between band `a1` and band
   `(a1+1)%7` of the baked colour tables `flt_13FD1B8/BC/C0/C4/C8/CC` (stride `24*band`),
   scaled by `a3`. The "sun" RGB (`64A074/78/7C`) is additionally collapsed to a
   **luma** for `flt_64A070` using weights `flt_628728=0.30`, `flt_62872C=0.59`,
   `flt_628730=0.11` (standard NTSC luma).
3. Loops `i = 0..5` building **6 interpolated RGB colours** in `dword_13FD170[3*i]`
   (the per-light-slot tints) by lerping the packed byte colours `dword_13FD1D0` /
   `flt_13FD1D4/D8` between the two bands with weights `1−frac` and `frac`.
4. Calls `VIBE_Light_RefreshAllObjects(a4)` to re-shade the world, records the active
   band in `dword_649F04`, and writes the interpolated **sky/fog colour** bytes
   (`+2724/2725/2726`) into the sky object `dword_64A7C8` from table `dword_1408770`.

This is the function that turns "what minute is it" into the exact RGB the rasterizer
sees, and it is what the **main-menu background** uses to render its time-tinted scene.

### 2.4 Sky update — `VIBE_Weather_UpdateSky @0x4c0040`

Per-frame sky driver. Reads the current hour `WORD2(qword_13CE852)` into `dword_11BC1C4`,
then:

- Grows the snow flake list / rain drop list to match the **hour's weather intensity**
  `dword_11BC038[hour]` (`VIBE_Snow_GrowFlakeList`, `VIBE_Rain_GrowDropList`). Rain only
  grows when `intensity & 1` and at `intensity/5`; snow at full intensity.
- Computes a wind/cloud scroll speed from per-hour wind vectors `dword_11BC100[hour]`
  (x) and `dword_11BC160[hour]` (y), taking the **max intensity over hour−1, hour,
  hour+1** so cloud cover transitions smoothly across the hour boundary.
- Swaps the two scrolling **sky-cloud layers** (`dword_11BC1D4`, `dword_11BC1D0[0]`) by
  weather class: picks a random texture from `Sky_Schoen_01..` (fair, `mod 4`),
  `Sky_Mittel_01..` (medium, `mod 3`, intensity ≥ 50) or `sky_schwer_01..` (heavy,
  `mod 2`, intensity ≥ 150), avoiding an immediate repeat, and cross-fades over
  1500 ms (`VIBE_Sky_SetLayerFade`). Scroll speeds come from
  `sqrt(wind.x²+wind.y²)*(intensity+150)*dbl_61E4C0` scaled per layer
  (`dbl_61E4C8`, `dbl_61E4D0`).

---

## 3. Shadows (blob/ground shadows)

Shadows are **projected ground blobs** cast from a capped list of up to **4 shadow
lights**.

### 3.1 The 4-light collector

- `VIBE_Shadow_ResetLightList @0x5f4428` — zeroes the count `dword_1408A5C` and walks the
  scene graph (`VIBE_SceneGraph_WalkAndInvoke`, type 16) calling the push predicate on
  each node.
- `VIBE_Render_PushToDrawList @0x5f43f4` — the collector. For a node with shadow-light
  flag `+529 & 4` set, appends the node pointer into `dword_1408A0C[++count]` and returns
  `count < 4`. **Hard cap: 4 shadow lights** (`return v2 < 4`). Nodes without the flag are
  ignored but still gate on `count < 4`. The list lives at `dword_1408A0C`/`dword_1408A10`.

### 3.2 Casting — `VIBE_Shadow_CastFromAllLights @0x5f444c` → `VIBE_Shadow_CastFromLight @0x5f3f98`

`CastFromAllLights` iterates the collected light list (`dword_1408A10[0..count)`) and
calls `CastFromLight(node, light, ground, node)` for each, so a caster receives at most
4 shadows.

`CastFromLight` is the heavy worker. Gating conditions (all must hold): shadows enabled
(`dword_1408A60`), caster shadow-count `<5` (`*(node+533)`), both caster and light carry
the shadow flag `+529 & 4`, the light has a mesh (`+492`), the camera-distance test
`(2*dword_64A7EC−2) > *(light.mesh+256)` passes, and the light's attenuation `light[37]`
is non-zero.

It maintains a **per-caster ring of 4 shadow slots** (`node[123] + 1780`, stride 128).
It searches for an existing slot bound to this light, or—if all 4 are used (`v7 == 4`)—
finds a free one and (re)initializes it: clears transform fields, sets sentinel
`+108 = -1`, copies the caster's flag byte, and decides the **shadow texture**:

- If `(16 * casterFlag) >> 7` ("force decal" bit) is set, it loads the static
  `"Schatten"` blob texture (`VIBE_Texture_LoadByName(aSchatten, 131199, …)`).
- Otherwise it picks a **LOD-scaled cache surface** via `VIBE_Shadow_AcquireCacheSlot`,
  doubling resolution (`v27 *= 2`) for each LOD band the caster's screen-size
  `*(mesh+472)` crosses in `flt_5F1D80 = {112.0, 200.0}` (decoded: `0x4270_0000=60.0`,
  `0x4348_0000=200.0`), clamped to `dword_1408A54`.

Then it projects the shadow: transforms the caster origin through the bone chain
(`VIBE_Transform_PointThroughBoneChain`), derives the **light direction** (either a fixed
near-vertical `flt_5CA2B0` vector with tolerance `0.01` when flag `+529 & 0x10`, or
caster→light delta with tolerance `1.0`), and—only if the direction changed beyond
tolerance or the silhouette differs (`VIBE_Math_VectorWithinTolerance`)—re-renders the
mesh silhouette (`VIBE_Shadow_RenderMeshShadow`) and rebuilds the ground projection
(`VIBE_Shadow_BuildGroundShadow(slot, ground)`), flagging the ground node dirty
(`*(dword_649D68+528) |= 4`). This **dirty check** is what keeps static shadows from being
re-rasterized every frame.

Supporting leaves: `VIBE_Shadow_ProjectGroundQuad @0x5f216c`,
`VIBE_Shadow_RasterizeHeightField @0x5f2a58`, `VIBE_Shadow_BuildGroundShadow @0x5f3048`,
`VIBE_Shadow_RasterizeTriangle @0x603ed4`, `VIBE_Shadow_ComputeCasterHeight @0x5f34c0`,
plus the cache layer `VIBE_Shadow_AllocCache @0x5f1d90` /
`VIBE_Shadow_AcquireCacheSlot @0x5f1e7c` and teardown
`VIBE_Shadow_ClearAllCasters @0x5f474c`, `VIBE_Shadow_RemoveCasterByLight @0x5f47dc`.

---

## 4. Particle systems

### 4.1 System object & memory — `VIBE_Particle_AllocSystem @0x5e1000`

Allocates a `0x310`-byte particle-system object plus three buffers:

- particle array: `84 * count` bytes (each particle is **84 bytes**), at `sys+40`
- point/poly vertex buffers: `80 * 4*count` (`sys+220`) and `40 * 2*count`.

System object fields used by the updaters (offsets from `sys`):

| Off | Field |
|---|---|
| `+0..+28` | physics scalars: gravity, drag, bounce, restitution, life-rate, etc. |
| `+32` | emitter flags: bit `0`=active/seed, bit `1`=exhausted, bit `2`=continuous |
| `+36` | spawn budget per frame |
| `+40` | particle array base |
| `+52`/`+208` | particle count (two mirrors) |
| `+200`/`+228` | base lifetime / spawn-time stamp |
| `+212` | colour-palette object (per-particle frame count at `+112`) |
| `+736`/`+752` | owner node / scene root |

Per-particle record (84 bytes, offsets from particle base):

| Off | Field |
|---|---|
| `+0..+8` | velocity (x,y,z, float) |
| `+16..+24` | seed/scratch (used as accel by gravity emitter) |
| `+48` | last-update time stamp |
| `+56..+64` | **world position** (x,y,z) |
| `+72` | base size / scale |
| `+76` | palette index |
| `+77/+78/+80` | colour bytes / sub-frame |
| `+79` | **alpha byte** (output, what the renderer reads) |
| `+81` | flags: bit `0` = alive |

It links the system into the global FX list (`dword_140874C`) and attaches it to the
scene root `off_649D64`. Spawned via `VIBE_Particle_SpawnEffect @0x42bd6c` and the typed
helpers (`SpawnSmoke 0x42cbc4`, `SpawnBlood 0x42c728`, `SpawnExplosion 0x42c8e0`,
`SpawnDebris 0x42d188`, `SpawnSparkleEffect 0x4d887c`).

### 4.2 Emitter behaviours (the update callbacks)

Each system carries one update function (`__usercall eax=sys, edx=now`) chosen at spawn.
Common loop: per alive particle, integrate `pos += vel * dt`, decrement size/life, write
alpha into `+79`, kill (`flags &= ~1`) when life ≤ 0; respawn dead particles when the
emitter is still active. Behaviours observed:

| Updater | Addr | Behaviour |
|---|---|---|
| `VIBE_Particle_UpdateEmitter` | `0x42b930` | General emitter: spawns up to budget `+36`/frame with randomized velocity (`flt_6119A8 * rand * dbl_6119AC + flt_6119C0`), integrates, and does **sphere collision/containment** — when a particle exits radius `+12` it reflects velocity off the surface normal (`VIBE_Math_VectorNormalize`) and applies restitution `+24`; alpha = `(vel.y/+4)` ramp × `flt_6119A4`. Sets "exhausted" bit `2` when zero spawned. |
| `VIBE_Particle_SeedParticles` | `0x42bec0` | Initial fill + bounce-on-floor: seeds dead slots with random velocity/spread (`flt_6119C8` family), then per particle integrates and, on hitting floor height `sys[2]`, clamps and damps velocity (`flt_6119F8/FC`, `flt_611A00`); decrements life by `dt*sys[1]`. Used as the seed callback for `SpawnRefLens`. |
| `VIBE_Particle_ResetEmitter` | `0x42c104` | Sets emitter origin `(a2,a3)` and clears the alive bit on all `+52` particles (recycle). |
| `VIBE_Particle_UpdateTrail` | `0x42c140` | Trail/ribbon: integrate + life-decay; respawn dead particles with small random offset around emitter (`dbl_611A1C/24/34`, `flt_611A2C`) and random colour bytes `+76/77/78` masked `0x3F`. |
| `VIBE_Particle_SpawnRefLens` | `0x42c3ac` | Lens/flare spawner: allocates a `"Ref-Map-Linse"` textured system (size `7.0`, flags `131199`), inits colours, seeds via `SeedParticles`. |
| `VIBE_Particle_UpdateGravity` | `0x42c42c` | Gravity fountain: `accel`→`vel`→`pos`, `vel.y -= dt`; kills below floor `dbl_611A84`; alpha from `vel.y*flt_611A70 + flt_611A8C`. Keeps the system half-full (`count/2`) by respawning with upward-biased random velocity. |
| `VIBE_Particle_UpdateCosineWave` | `0x42c7c8` | Pulsing/ease: progress `t = (now−spawn)/life ∈ [0,1]`; intensity `1−(cos((t+1)*flt_611AA8)+1)`, scales position by it (`flt_611AAC/B0`) and alpha by `(1−i)`. A cosine ease-in/out used for glints. |
| `VIBE_Particle_UpdateFadeOut` | `0x42cadc` | Linear fade: `t = (now−spawn)/duration`; ramps position by `t`, alpha = `sys[0]*t*p[0]` rising then `sys[0]*(1−(t−p+28)*p[4])` falling about pivot `+28`; kills when out of `[spawn, spawn+dur]`. |
| `VIBE_Particle_UpdateScatter` | `0x42cde8` | Burst + bounce: on emitter-active bit, scatters all particles into a disc/ring (radius from `+16`, ring test `dbl_611B0C`, `flt_611B14`) with random colour `+76/77/78 = base + (rand & mask)`; then integrates with floor bounce (`flt_611B1C/20/24`) and velocity damping, snapping to rest when `|vel.y| < dbl_611B2C`. |

Plus the GPU-facing renderers `VIBE_Particle_RenderSystem @0x5e1278`,
`UpdatePoints @0x5e1e0c`, `UpdatePolys @0x5e2814`, `UpdateLens @0x5e32c0`,
`UpdateBillboards @0x5ac970` (billboard the quads toward the camera), and
`VIBE_Particle_SpawnSystemByType @0x5e3ae0`.

> All `VIBE_Coord_ConvertX @0x5c6b08` calls in these updaters are the engine's
> float→int truncation thunk used to bake the computed alpha/size float into the
> `u8` at particle `+79`.

---

## 5. Weather & precipitation

### 5.1 Per-frame weather render — `VIBE_Weather_RenderAndThunder @0x4c05ac`

1. `VIBE_Rain_Render(dword_11BC1C8)` and `VIBE_Snow_Render(dword_11BC1CC)` if their
   systems exist.
2. **Thunder.** For weather classes in `dword_11BC098[hour]`: with probability
   `1/300` (`RandomModulo(0x12C)`), or for class 3 with extra `1/100`, it triggers a
   lightning flash — records flash time (`dword_11BC0F8 = now`, end at
   `dword_11BC0FC = +8`) and plays a thunder voice sample (`VIBE_Audio_StartVoiceSample`,
   `dword_63C750`) with random pan/volume seeded from `flt_634490 * flt_61E4E8`.
3. **Sun-rays / god-rays.** Outdoors (`dword_649D60 == 0`), during hours 11–17
   (`0xB..0x11`) when the hour's precip cleared, with `1/2` odds
   (`RandomModulo(0x80) > 0x60`) and no existing rays (`dword_62D564`), spawns
   `VIBE_Light_CreateSunRays`; later removes them via `VIBE_Light_SetSunDirection(…, 1)`
   when the hour advances past the recorded `dword_631E90`.

### 5.2 Rain — `VIBE_Rain_Render @0x429c38`

Owns a drop list (`result[4]`) of **10-float drops** `[ … vx,vy,vz, x0,y0, x1,y1 ]`.
Per frame:

- Interpolates spawn/fade counts when a transition is in flight
  (`result[12]≠result[13]`, ramped against `now` between `result[12]` and `result[15]`).
- Steps drop physics with `dt` scaled by `flt_611764 = 0.10`
  (`VIBE_Rain_UpdateDrop @0x4294d4`).
- Begins a scene (`VIBE_Render_BeginScene`), sets additive-ish blend
  (`VIBE_Render_SetBlendMode(1,0,0,…)`), computes a fade colour from drop count
  (`1 − count*flt_611768`, scaled by `flt_61176C=96.0` and `flt_611770=128.0` to alpha),
  then builds **line-list** vertices (`v34[]`, 8 floats/vertex) clipping each drop to the
  view AABB (`dword_13ECE58..64`). Vertex colour packs alpha into the high byte
  (`0x80000000 | alpha<<8 | alpha`), brightness from `(1−drop[2])*flt_611774`.
- Flushes in **batches of 128 vertices** through device vtable `+112` (`DrawPrimitive`,
  primitive type `2`=line list, FVF `452`, stride `28`); errors go to
  `VIBE_Render_ReportDDrawError`. Ends scene.

### 5.3 Snow — `VIBE_Snow_Render @0x42b5b0`

Mirrors rain but draws **textured quads** (two triangles per flake): same transition
interpolation (`result[24..27]`), physics step
(`VIBE_Snow_UpdateFlake`, dt × `flt_611990`), additive blend
`SetBlendMode(1,1,0,…)`, binds the flake texture via device `+152` (`SetTexture`).
For each flake inside the view AABB it emits **3 vertices** (`v29[]`, 8 floats each, with
fixed UVs `1348756580/1065353216`), centering on `(x0+x1)*flt_611998`. Flushes in
**batches of 192 vertices** (primitive type `4`=triangle list, FVF `452`, stride `28`).

### 5.4 Snow accumulation on geometry

Distinct from the falling flakes — these "paint" snow onto world surface textures over
time:

- `VIBE_Snow_AccumulateOnObjects @0x42b3fc` — integrates an accumulation amount
  `*(a1+84) += depth*dt`, derives an opacity `min(255, amount*flt_6118A4)`, then for
  every world object whose name starts `"ter"` (`byte_6117C8`, terrain) and whose accum
  byte `+88` is below the new opacity, calls `VIBE_Snow_AccumulateOnTexture` to blend
  white into that object's texture. It round-robins work across frames using cursors at
  `a1+24/28` so it never processes the whole object list (`dword_1406A80`) in one frame.
- `VIBE_Snow_ApplyToVisibleObjects @0x42b1fc` — same idea but targets meshes named
  `"*fltx"` (`aFltx`) only when the region is current (`dword_649D58−1 == obj+84`), with a
  budgeted per-frame slice (`a1+16` divisor, cursors `a1+48/52`); opacity clamps via
  `dbl_611894/9C`.
- `VIBE_Snow_AccumulateOnTexture @0x42ab78` does the actual per-texel blend.

---

## 6. Constant reference

| Symbol | Addr | Value | Use |
|---|---|---|---|
| `flt_628CBC` | `0x628cbc` | `-2.0` | env-map reflection `N−2(N·V)V` |
| `flt_628CC0` | `0x628cc0` | `0.5` | env-UV remap `0.5x+0.5` |
| `flt_628CC4` | `0x628cc4` | `1/127.5` | env-normal dequant scale |
| `flt_628CC8` | `0x628cc8` | `-128.0` | env-normal dequant bias |
| `flt_628728/2C/30` | — | `0.30 / 0.59 / 0.11` | sun-colour → luma weights |
| `flt_61DD50` | `0x61dd50` | `1/255` | torch flicker → brightness |
| `dbl_61DD38/40/48` | — | `0.6 / -0.3 / 0.3` | sun-height random range |
| `flt_5F1D80[2]` | `0x5f1d80` | `60.0, 200.0` | shadow LOD distance bands |
| `dword_1408A5C` cap | — | `4` | **max shadow lights** |
| particle record | — | `84` bytes | per-particle stride |
| `flt_611764` | `0x611764` | `0.10` | rain dt scale |
| `flt_61176C/70` | — | `96.0 / 128.0` | rain alpha scale |
| rain batch | — | `128` verts, prim `2` (lines) | `DrawPrimitive` flush |
| snow batch | — | `192` verts, prim `4` (tris) | `DrawPrimitive` flush |
| thunder odds | — | `1/300` (`+1/100` class 3) | `RandomModulo` |

---

## See also

- [14 — Per-frame loop](14-per-frame-loop.md) — where these systems are pumped each frame.
- [15 — Game time](15-game-time-tick.md) — the clock that feeds the day-cycle bands.
- [23 — Render universe chain](23-render-universe-chain.md) — scene traversal that draws lit geometry.
- [24 — Projection & rasterizer](24-projection-rasterizer.md) — consumes the vertex colours produced here.
- [27 — Animation & skeleton](27-animation-skeleton.md) — supplies the bone transform/env-normal layer used by `VIBE_Mesh_ComputeVertexLighting`.
