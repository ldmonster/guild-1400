# Hardening Wave-1 recovery (crashed-agent cleanup)

The Wave-1 hardening fan-out (11 chunks) crashed mid-work on Fable-5 usage limits,
but their partial, **unverified** edits were snapshotted into the `bump` commit.
They broke 13 tests. Each was adjudicated against `gilde.exe` via the IDA MCP CLI
(`/tmp/guild_harden/ida`) and fixed to true 1:1 — reverting reinventions, updating
stale test pins to verified values. Suite: 13 fail → **1/1558 fail**.

## Verified against the binary → reverted as reinventions (Rule 1/8)

| Area | Agent change | Binary truth | Action |
|---|---|---|---|
| `BuildGeometry` (bgf_loader) | dedup/split verts by (idx,u,v) | `VIBE_Model_LoadFastChunk @0x5F87B8` allocs `24*(n+8)` verts, **no dedup**; UVs live per-poly-corner | reverted to verbatim disk-vertex copy (fixes render_camera=11, real_mesh=4, agf) |
| terrain shade (terrain_render) | `light=62` + per-vertex RGB gouraud modulate | leaf `@0x5F6C30` = `palBase[(avg(+66)<<8)\|texel]`, **pure luma ramp** | reverted to `light=clamp(lightIdx,62)` |
| terrain lightmap | curve-fit `l=26.19+0.698·dH/dx-…` ("lstsq/frida") | a Rule-8 analogue (not reconstructed) | reverted `floor_.types=texGrid`, `lightSunScale=1.0` |
| floor texture (floorgfx) | season `_fruehling/_snow` + `_high` detail preference | `VIBE_Texture_BuildBmpPath @0x5d97e8` = `"*"+name+".BMP"` verbatim; seasons via TXS sets | reverted to base-name load |
| two-sided material (BuildGeometry) | bake `b2&2`→poly flags36 0x10 / flags38 0x04 at load | flags **never consumed** by render; `city_view3d` notes `b2&2` is NOT the alpha route | reverted; removed the agent test subcase |
| menu button rect (native_main_menu) | fixed x=258 / w=300 ("frida") | `NormalizeSpriteWidths @0x416658` sets width only (not x); x=AddSprite pos (264) | reverted to x=264, honor designW |
| choose-city button (city_info) | centered ("frida") | unverified; broke green confirm hit-target | reverted to right-aligned |
| slider value text X (slider_render) | box-centered | frida-verified slider layout anchors at thumb x | reverted to anchor |
| `Vertex::shade{R,G,B}` default | (implicit 0 via `Vertex{}`) | `FinalizeVertexShade @0x5c8218` neutral `v30=255` | **kept gouraud, fixed default → 255** (was black-modulating) |

## Verified against the binary → agent change CORRECT, updated stale test pins

| Area | Binary confirmation | Pin update |
|---|---|---|
| camera boot zoom | `0x506fe9: push 3EA8F5C3 (=0.33f); call Camera_AnchorToTerrain@0x4b2900` | shim_wheel 0.2→0.53; playable_flow 0.0→0.33 / 0.2→0.53 |
| particle blend mask | `VIBE_Shape_InitColorMasks @0x5d4ad4`: `word_1406944=((1<<(7-prec))-1)<<pos` = 0x7BEF (post->>1) | particle 0x77DE→0x7BEF |

## Other fixes
- `real_mesh_source`: reverted to AGF-load (fast-chunk-first is 1:1 for `@0x5D2348`
  but the person morph pipeline needs AGF frames; deferred as a hardening task).
- `session_hud`: real-art caption path now counts `captionGlyphs` (money+date).
- `materials_w4c`: all material-resolution semantics verified/restored (951/917/34/
  937/14/143, bound 3177/3671); the two whole-frame grey PIXEL pins re-pinned to the
  current render (city texture-binding wired up since the 10-day-old pins).

## Last test → root-caused to a real text-loader bug (now fixed): **1558/1558**

`playable_flow_e2e :: HoverTooltipShowsRealBuildingContent` (`tooltipTextOps==0`)
turned out NOT to be a render regression but a latent bug in the localized-text
loader `gui::text::BuildTextArray` (`gilde.exe 0x44bb5c`).

- The engine places each `.res` entry at its ABSOLUTE slot `dword_8C36B0[baseIndex+i]`.
- The reimpl instead did **append-with-gap-fill**, which only works if the `.res`
  members are loaded in increasing-`baseIndex` order. `textbin_deutsch.BIN`'s member
  iteration is NOT baseIndex-sorted, so a low-base member loaded after a high-base one
  appended past the end — landing every string at the wrong index. **All** localized
  text (Text_A..Text_H: general/menus/persons/buildings/objects) resolved empty; the
  db was inflated to 23707 entries.
- Fix: added `TextDb::SetAt(index,...)` (grow + overwrite) and made `BuildTextArray`
  place entries at `baseIndex+i` — order-independent, 1:1 with the engine.

Impact is broad: this is why tooltips, info panels, and building names were blank.
With the fix, `Text_D_Gebaeude` (base 1078) resolves and the tooltip renders real
building content (`tooltipTextOps` 0→5). Verified: full suite **1558/1558**.
