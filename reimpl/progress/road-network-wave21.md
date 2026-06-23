# Wave-21 — Road-network / building-upgrade-tree layout (W21-MAP)

**Agent:** W21-MAP · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructed the city map-generation geometry the audit flagged at
`0x592d98 VIBE_Map_ComputeRoadNetworkLayout` (2180 bytes) and its leaves, 1:1 from
the Hex-Rays decompile + paged disassembly, with every float→int site verified.

## What this function actually is

Despite the "road network" name, the single live caller is
`VIBE_Building_OpenUpgradeTreeWindow @0x594100`. The function lays out a **layered
DAG (Sugiyama-style) of building-upgrade nodes** reachable from a starting building
type, producing per-node screen X/Y so the caller can draw the upgrade tree with
`VIBE_Paintbox_DrawLine`. It is the geometry pass; the sibling
`VIBE_Building_BuildUpgradeTree @0x59361c` then fills the Y/`coordY` field used by
the renderer.

## Reconstructed (with addresses)

| addr | name | where | status |
|------|------|-------|--------|
| 0x592d98 | VIBE_Map_ComputeRoadNetworkLayout | `src/world/road_network.cpp` `RoadComputeNetworkLayout` | full 1:1 body |
| 0x592c7c | VIBE_Map_ComputeBuildingChainDepth | `src/world/road_network.cpp` `ChainDepth` / `RoadComputeChainDepth` | full 1:1 body |
| 0x5c6b08 | VIBE_Coord_ConvertX (truncate) | reused `guild::util::ConvertX` (`src/util/coord.cpp`) | leaf — already reconstructed, reused |

### Callees → leaves

- `VIBE_Map_ComputeBuildingChainDepth (0x592c7c)` — recursive longest-chain depth;
  reconstructed here (the only non-trivial callee).
- `VIBE_Coord_ConvertX (0x5c6b08)` — x87 truncate-toward-zero (`fstcw`/RC=trunc/
  `frndint`/`fldcw`); a genuine leaf, already reconstructed as `util::ConvertX`
  (`std::trunc`). Both `(int)` sites (the `-v62*ratio` origin and the per-node
  `cost*ratio`) truncate toward zero — verified against the `fistp`-after-`frndint`
  sequence (st0 is already integral, so the store is exact).

No hooks were needed — the function is pure integer/fixed math over caller-supplied
tables. (Rule 8: nothing faked, nothing stubbed.)

## Recovered memory model (the tricky part)

The original folds the node array into many overlapping per-field BSS globals that
all alias **one 44-byte (0x2C) record array based at `0x12CDD68`**, indexed at
stride 44. The apparent base mismatch (`0x12CDD68` for the init fields vs
`dword_12CDD8E+2 = 0x12CDD90` for the swapped record) is the `add eax,0x2C` the
init loop executes **between** two groups of field stores (`0x592ebf`). Fields also
overlap **across** records — e.g. node N's `parentToId` (+0x2C) is the same physical
word as node N+1's `childType` (+0x00). To be provably byte-exact, the
reconstruction models the whole store as a **flat byte buffer** addressed by the
literal symbol offset + 44·node, exactly as the disassembly does — so the aliasing
and the mid-loop `+44` bias reproduce automatically.

Verified field offsets within the 44-byte record (rel. `0x12CDD68`):

| off | symbol | meaning |
|----|--------|---------|
| +0x00 | word_12CDD68 | childType (`[link+0xA5]`); aliases next node's parentTo |
| +0x02 | word_12CDD6A | init 0xFFFF; aliases prev node's depth slot |
| +0x04 | dword_12CDD6C | laid-out X |
| +0x08 | dword_12CDD70 | laid-out Y |
| +0x10..0x20 | dword_12CDD78..88 | scratch link slots, init -1 |
| +0x24 | byte_12CDD8C | flag, init 0xFF |
| +0x28 | hi-word of dword_12CDD8E | **nodeId** (match key) |
| +0x2A | dword_12CDD92 lo | **parentFromId** (== `(dword_12CDD8E+2)>>16`) |
| +0x2C | dword_12CDD92 hi | **parentToId** (== childType of next slot) |
| +0x2E | word_12CDD96 | **depth** (0xFFFF == uncomputed; aliases next reserved02) |
| +0x30 | dword_12CDD98 | **cost** (sort key / scaled X) |

Side state: `word_12CE890[]` level-start indices (with `word_12CE892` being the
"+1" read of the same array for level-end), `dword_13CE28C` node count,
`dword_13CE284` level count — all moved into the explicit `RoadLayoutState`.

### Input record format (caller-supplied, read verbatim)

- type-record table base `dword_13CE294`, stride **589** (`0x24D`); record base =
  `589*typeByte + base`. `[+34]` = entry count (u8). `[+35]` = `u16[]` entry ids
  (bit15 a flag, masked with `&0x7FFF`). `[+163]` = parallel `dword[]`, lo16 =
  parentFrom id, hi16 = childType id.
- type-flag table base `dword_13CE27C`, stride **65** (`0x41`); `[0]` = category
  byte. The populate scan stops on category **2** or **6**.
- special target id **253** (`0xFD`) takes a drain-the-count branch (no node
  effect) — preserved as the original's bare advance loop.

## Algorithm passes (all reproduced 1:1)

1. Scan entries for the target id (masked); bail (`return 1`) if absent.
2. Populate nodes from entries **after** the target until a stop category; bail if
   none.
3. Chain-depth each node, track max → `levelCount = max+1`.
4. Bubble-sort nodes by depth (whole-44-byte-record swap).
5. Level-start banding (`levelStart[]`).
6. Spread level 0 across `height` (`v27 = height/(2·count0)`, step `2·v27`,
   `X = v27-24`).
7. Per level ≥1: (A) average cost from prior-level connected nodes; (B) bubble-sort
   the level by cost; (C) spread equal-cost runs across `(height/count0)>>levelIdx`
   bands; (D) enforce 80px minimum sibling spacing.
8. Cost min/max; if the span overflows the area, **float-rescale** X via
   `ratio = height/span` and `ConvertX` truncation.
9. Y placement: `v72 = min(96·levelCount, width-16)`, `Y = v72·depth/levelCount + 16`.

(Note the original's deliberate axis swap — X spacing derives from `height`/`a4`,
Y from `width`/`a3`; kept exact.)

## Tests + docs

- `tests/unit/world_road_network_test.cpp` — 7 cases, **33 checks**: chain-depth
  root/cache, target-not-present / empty / stop-category / 253 returns, and a
  **golden 4-node/3-level vector** pinning exact depths, level boundaries, ids,
  and X/Y (`X={266,22,510,22}`, `Y={16,112,112,208}` for 800×600).
- `tests/integration/world_road_network_itest.cpp` — 1 flow case, **34 checks**:
  a 7-node/4-level tree driving the relaxation + float-rescale path, asserting the
  layered-layout invariants (depth-monotone Y, banded level starts, root on top).
- Header `src/world/road_network.h` documents the model + every constant.

Build: `world_road_network_test` and `world_road_network_itest` both green; the
`guild` lib compiles clean; the untouched `pathfind_map_test` (72 checks) still
passes.

## Wiring / handoff (rule 13)

The real caller `VIBE_Building_OpenUpgradeTreeWindow @0x594100` is **not yet
reconstructed** (only a comment stub in `src/sim/building.cpp`), so there is no live
`src/` call site to bind into today. **Handoff:** when 0x594100 lands, it must call
`guild::world::RoadComputeNetworkLayout(state, dword_13CE294_base,
dword_13CE27C_base, a1, a2, rect.right>>16, rect.bottom>>16)`; on `return 0` it then
runs `VIBE_Building_BuildUpgradeTree @0x59361c` and draws each node pair with
`VIBE_Paintbox_DrawLine(coordX+24, coordY+48, …)` (see the decompile of 0x594100).

### Relationship to the existing `sim` sketch

`src/sim/pathfind_map.{h,cpp}` already carries a deliberately **simplified**
`MapComputeRoadNetworkLayout` (operates on a pre-populated graph; omits the
input-record scan, the within-level cost sort, the equal-cost gap spread, the 80px
push, and the float X-rescale). It has its own tests and **no external callers**.
This wave's `src/world/road_network.cpp` is the **faithful 1:1 body** and is what
the real caller path should use. The two coexist in different namespaces
(`guild::sim` vs `guild::world`) — no symbol is redefined (no ODR clash). The sim
sketch is left untouched (outside this agent's ownership); it can be retired in a
later pass once 0x594100 is wired to the faithful version.
