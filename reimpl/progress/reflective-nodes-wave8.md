# Reflective-node SETUP — wave-8 (W8-REFLECT)

Status: **reconstructed & wired (1:1)**. Closes the data-plumbing gap left by
wave-6 (`mirror-render-wave6.md`) and wave-7 (`mirror-scenegraph-wave7.md`): the
mirror reflection pass only runs once a scene node is flagged reflective
(`child[104] & 0x20`), but nothing set that bit, so the pass never fired. This
wave reconstructs **where the engine sets a surface reflective** and the
**reflection-plane derivation**, so the chain
`ShouldRenderMirrorPass → PrepareReflectionNode → AppendMirroredPolys` can fire.

Owned files (new):
- `src/render/reflective_nodes.{h,cpp}`
- `tests/unit/reflective_nodes_test.cpp` (11 tests / 40 checks, pass; clean under
  `-fsanitize=address,undefined`)

Read-only references (NOT edited): `mirror*.*`, `scene_node.*`, `scene_load.*`,
`texture.*`, `mesh_load.cpp`, `mesh_attach_textures.cpp`.

---

## The discovery (rule 2/7): WHERE the reflective bit is set

The reflective marker read by `VIBE_Mirror_PrepareReflectionNode` @0x5F676C is
`(child[104] & 0x20)` at **0x5f67c4** — where `child` is a **texture record** drawn
from the mesh's texture-record array (mesh+460 block → owner `[4]` whose +480 is the
texture count, `[5]` the pointer array). The matched surface poly's `+20` field
equals that texture-record pointer (the `surfaceKey` wave-7 collects on), so the
"reflective child" IS the surface's texture record.

Xrefs of the bit (`f6 40 68 20` = `test byte ptr [eax+68h], 20h`) and a full-binary
`find_bytes` for an immediate-`0x20` OR into `[reg+68h]` found **no immediate
writer** — the bit is set wholesale by the texture loader.

### The writer: VIBE_Texture_LoadByName @0x5DA714

bit5 (0x20) of a texture record's +104 byte is the loader local **`v85`**:

```
; 0x5da74f..0x5da76a  (v85 predicate)
mov al, bl          ; bl = a4 = flag2
sar eax, 6
test al, 1          ; flag2 bit6 ?
jz   -> v85 = 0
cmp byte ptr [flag0_lo], 0FFh
jnb  -> v85 = 0     ; (unsigned) flag0 low byte >= 0xFF
mov al, 1           ; v85 = 1

; written into +104 bit5:
;   0x5dac7b: and bl, 0DFh                  (clear bit5)
;   0x5dac92: v55 = (32 * (v85 & 1)) | v54  (set bit5 := v85)   [BMP fresh load]
;   0x5da9ad: same on the clone/alias path
```

So **`v85 = ((flag2 >> 6) & 1) && ((u8)flag0 < 0xFF)`**. The Hex-Rays
`((u8)v80 != 0xFF || (v80 & 0x10000) != 0)` is the decompiler's rendering of the
single *byte* `cmp ...,0FFh / jnb`; only flag0's low byte feeds it.

### What flag0/flag2 are (mesh_load.cpp step 6, @0x5d… material assembly)

`VIBE_Texture_LoadByName(name@eax, flag0@edx, flag1@cx, flag2@bl)`. Per material:
- `flag2 bit6 = (mat.shiftHi << 6)`  — the material "high shift" bit.
- `flag0 low byte = mat.presentFlag ? mat.paletteByte : 0xFF` (the 0xFF default).

**Net rule (1:1):** a surface is reflective iff its material has the **high-shift
bit set AND a real palette index (paletteByte < 0xFF, i.e. present)**. That texture
record then carries +104 bit5, and `PrepareReflectionNode` picks it up.

> NB: this is the *same* +104 bit5 the wave-3/4/5 texture work documented as the
> `v85` "8-bit-indexed/special-source" selector (`texture.h kTexFlagIndexed8`).
> The engine has **one** `v85` bit; the mirror path reuses it as the reflective
> marker. There is no separate reflective flag bit. `kReflectiveTexFlag` (0x20) in
> `reflective_nodes.h` is an intent-named alias of the same bit, not a new bit.

## The reflection plane (PrepareReflectionNode @0x5f689c..0x5f68cb)

Once the reflective texture is matched to its back-facing surface poly, the mirror
plane is built from that poly:

```
; n = camera-frame-rotated surface normal (the +24/28/32 fill)
VIBE_Transform_RotateVectorWithFrame(node, dword_13FCD1C, a2+24, vert0_frame@+44)
; 0x5f68c8 — plane distance:
*(a2+36) = n.x*p.x + n.y*p.y + n.z*p.z          ; d = n . p
```

`p` = the poly's first-vertex position (`*(*(a2+16))` → pos at +0/+4/+8).
`DeriveReflectionPlane(n, p)` reproduces this verbatim: `m = {n, d = n·p}` — exactly
the `MirrorPlane` wave-6/7 consume. (Convention note: here `d = +n·p`;
wave-6 `ReflectPointAcrossPlane` reflects with `t = -(P·n − d)*2`, consistent.)

`dword_13FCD1C` (the active camera node) is the rotation source; verified 0 at
static analysis (runtime-populated) — when null the engine takes the poly's stored
normal directly, which is what the bind site passes through.

## Reconstructed entries (addresses → reimpl)

| addr | engine | reimpl (guild::render) | status |
|------|--------|------------------------|--------|
| 0x5da76a / 0x5da75c | `v85` predicate | `ComputeReflectiveBit(flag0,flag2)` | **1:1** |
| mesh_load step 6 | flag0/flag2 assembly | `IsMaterialReflective(present,pal,shiftHi)` | **1:1** |
| 0x5dac7b / 0x5dac92 | bit5 write `(32*v85)\|(f&0xDF)` | `ApplyReflectiveBit` | **1:1** |
| 0x5f67c4 | `child[104] & 0x20` read | `TextureFlagIsReflective` | **1:1** |
| 0x5f67ad..0x5f67c4 | reflective-child scan loop | `FindReflectiveTexture` | **1:1** |
| 0x5f68c8 | plane `d = n·p` | `DeriveReflectionPlane` | **1:1** |

No new globals/symbols introduced (grepped `src/**`); reuses `MirrorPlane` and
`ReflectPointAcrossPlane` from `mirror.{h,cpp}`. One library, ODR-clean.

## EXACT handoff (rule 13)

Scene-load → mirror pass, two ends, both already owned by other files:

1. **Scene load marks reflective nodes.** When `mesh_load.cpp` resolves materials
   (step 6) and calls `TextureLoadByName`, a material with high-shift + palette
   sets +104 bit5 on its texture record — the engine's own behavior; this module's
   `ComputeReflectiveBit` / `IsMaterialReflective` is the exact predicate, so a
   loader (or a scene-load probe) can answer "is this surface reflective?" and
   `FindReflectiveTexture` answers "does this mesh carry one?" cheaply.

2. **CityView3D mirror pass then runs** (orchestrator-owned `city_view3d.cpp`, not
   edited here): during the scene walk, call wave-7
   `render::PrepareReflectionNode(node, &ctx, &reflectionPreparedGlobal, frame,
   basis)`. It uses `child[104] & 0x20` (== `TextureFlagIsReflective`) to find the
   marked texture, derives the plane (`DeriveReflectionPlane`, matching 0x5f68c8),
   and publishes `dword_649D6C`. Then set the wave-6 gate:
   `gate.reflectionPrepared = (reflectionPreparedGlobal != nullptr)` so
   `ShouldRenderMirrorPass` flips true and `AppendMirroredPolys` emits the
   reflection into the same draw list. No bind-site change beyond what wave-7
   already documented; this wave supplies the *flag origin* + *plane formula*.

The 5-term gate (wave-6) stays: `(byte_14080EC[0]&0x40)` mirror feature (a debug-key
toggle in `VIBE_Scene_HandleDebugKeyToggle`/`ApplyRenderStates`), `dword_649D6C`
prepared node, `dword_1408A74 && dword_1408A78` (the per-object MirroredPoints/Polys
buffers at object+492+2304/+2308, allocated by `BuildMirroredGeometry` @0x5f646b/
0x5f6498), and `byte_1408A98 & 1` runtime-active. `dword_1408A70` is the prepared
reflective OBJECT (set when that object is the bound mirror; freed in
`VIBE_Object_Dispose` @0x5b081b, `DisposeAllObjects`, `SwitchActiveSlot`).

## City scene reality (rule 8 — documented, not faked)

In the city scene the large flat reflector is the **water**, rendered by the
separate floor/terrain path (`VIBE_Floor_RenderTerrain` @0x5bf22c, wave floor-water
module) — NOT the texture-record mirror path above. The texture-record reflective
bit (+104 bit5) fires only for **mesh surfaces whose material has high-shift +
palette index** (e.g. an interior mirror/glass prop). If a given city map ships no
such material, the mirror pass is correctly idle and the frame is byte-identical to
the non-mirror path (gate false) — this is the engine's real behavior, not a stub.

How water *could* be reflective via this path (for completeness, NOT implemented
because it would diverge from the original): a water surface authored as a mesh
prop with a material carrying the high-shift+palette flags would receive +104 bit5
at load and flow through `PrepareReflectionNode`. The shipped game instead uses the
dedicated floor-water animation path for the lake/moat, so the two are independent
subsystems.

## Build / test

```
g++ -std=c++17 -Isrc -Iinclude -I. -Itests -o reflnodes_test \
  tests/unit/reflective_nodes_test.cpp tests/framework/test_main.cpp \
  src/render/reflective_nodes.cpp src/render/mirror.cpp \
  src/util/math.cpp src/util/coord.cpp src/util/math_trig.cpp
# => 40 checks, 0 failures  (also clean under -fsanitize=address,undefined)
```

CMake globs `src/**` and `tests/unit/*.cpp`, so the new files are picked up
automatically.
