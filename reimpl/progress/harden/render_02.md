# Wave-H1 Hardening — chunk render_02 (orchestrator summary)

Chunk render_02 = 22 .cpp files in `src/render/`. Hardened by 5 parallel sub-agents
plus an orchestrator re-verification pass. MCP (gilde.exe @ 0x400000) live throughout.
Per-cluster detail lives in the sibling reports listed below.

## Cluster reports
| Cluster | Report | Files |
|---|---|---|
| frame/light/heightmap/hicoltab | `render_frame_light.md` | frame, light, light_atmos, hicoltab, heightmap, heightmap_create |
| fx/particle/shadow/gfx | `render_fx_particle.md` | fx_recon3_particle_render, fxrecon_particle_mirror_shadow, gfx_archive |
| mesh geometry/projection | `render_mesh_geometry.md` | mesh, meshlist, mesh_normals, mesh_postprocess, mesh_recon3_geometry, math_bigint96 |
| mesh load/asset | `render_mesh_load.md` | mesh_asset, mesh_load, mesh_lod_name, mesh_stock_object |
| mesh scene/textures | `render_mesh_scene.md` | mesh_scene, mesh_attach_textures, menu_widgets |

## Aggregate counts (per sub-reports)
- VERIFIED-1:1: 64 functions
- FIXED: 12 source fixes (+ several wrong header comments corrected)
- BOUNDARY (rule 3-5 GPU/SDL/Vulkan swap or out-of-tree data): ~13, math verified 1:1 at each
- Golden tests corrected to the binary: 4

## Orchestrator re-verification note (IMPORTANT)
The mesh-geometry sub-agent reported three FIXES (ProjectVerticesToScreen 8-bit cull,
ComputeBoundingExtents radius/radius2 field assignment, and the two golden tests) but a
format/lint hook REVERTED its edits before they landed — the working tree showed the files
clean/unchanged with the bug still present (and mesh_postprocess.cpp left internally
contradictory). The orchestrator re-decompiled both functions and re-applied the fixes:

1. **ProjectVerticesToScreen @0x5c5120 — 8-bit back-cull selector (RE-APPLIED FIX)**
   `src/render/mesh.cpp:31`. Disasm 0x5c5143..0x5c5159: `mov al,[eax+212h]; shl al,4;
   shr al,6; mov cl,al; test ecx,edx`. The `(u8)` truncation is on `16*objFlags530`
   BEFORE the `>>6` (an 8-bit unsigned shr). Recon had `(u8)((16*objFlags530)>>6)`
   (cast after) → diverges once objFlags530 >= 0x10. Fixed to `((u8)(16*objFlags530))>>6`.
   The flag-byte path (0x5c523c) recomputes the identical selector, so backCull is reused.

2. **ComputeBoundingExtents @0x5D1B54 — diagonal target offset (RE-APPLIED FIX)**
   `src/render/mesh_postprocess.cpp:78`. Disasm: 0x5d1bbe `*(a1+472)=i` (max-vertex),
   0x5d1bcd `*(a1+468)=*(a1+472)` (mirror), then 0x5d1ff5 `*(a1+472)=sqrt(diagonal)`
   OVERWRITES +472 only. Final: +472(m.radius)=diagonal, +468(m.radius2)=max-vertex.
   Recon overwrote +468 (`m.radius2`) instead → both fields wrong. Fixed the store to
   `m.radius` (+472). mesh_recon3_geometry.cpp's copy (obj.radius/obj.radius_alias) was
   already binary-correct; the two now agree.

3. **Header + golden corrections to the binary**
   - `src/render/mesh_load.h:204-205` comments described +468/+472 semantics backwards;
     corrected (+468 = max-|vertex|, +472 = AABB diagonal).
   - `tests/unit/render_mesh_load_test.cpp:62-63` and
     `tests/e2e/render_mesh_load_e2e_test.cpp:120-121` asserted swapped radius/radius2;
     for the ±1 cube the binary yields m.radius=2√3 (diagonal), m.radius2=√3 (max-vertex).
     Both fixed (rule 25: golden must equal the binary).

## Highest-value fixes from the sub-agents (see cluster reports for full evidence)
- `AverageAreaHeight @0x427468` — float (not double) running accumulator (frame/light).
- `project_slot @0x5e1278` — eye-space X must be f32 like Y/Z (fx/particle).
- `Shadow_CastFromLight @0x5f3f98` — LABEL_45 render+ground-emit block is inside `if(v19)`;
  recon always called BuildGroundShadow (fx/particle).
- `Shadow_InitBuffers @0x5f1fc0` — restored a3@ebx AllocCache arg (fx/particle).
- mesh_load texture flag2 selector @0x5d2db2 (tests +128 name2[0], not presentFlag);
  StrNCopyPad @0x5D9360 zero-fills the window; material disk-slot priority remap;
  stock-object record key = a2; fast-chunk writer vertex block gated on vertexCount>0.

## Compile status
All modified .cpp in chunk render_02 + the two edited test files pass
`g++ -std=c++17 -I. -Iinclude -Isrc -Ishim -fsyntax-only`. Full-library link is blocked by
PRE-EXISTING breaks OUTSIDE this chunk (`src/gui/widget_layout.cpp` `Widget::ld` missing;
an ar/ranlib archive issue in combat_slots4.cpp) — documented as handoffs, not touched.

## Handoffs (out-of-chunk, NOT edited)
- `src/render/bgf_loader.{h,cpp}` — positional material-name disk slots; bgf_loader.h:31
  has a stale magic comment (constant 0xFAB50005 is correct). Compensated in BuildParsedFromBgf.
- A pre-existing `git stash@{0}` (WIP) holds larger versions of some FX/scene files plus
  raster clip fields (`RgbzRasterState.clipX0/clipX1`); the fx sub-agent did NOT pop it.
  `render_system_to_surface` in fx_recon3_particle_render.cpp stays as-is pending that decision.
- `src/gui/widget_layout.cpp` pre-existing compile error (another agent's in-progress file).

NEVER committed. progress/INDEX.md untouched.
