# Harden sweep — render cluster 10 — src/render/terrain.cpp

Date: 2026-06-16
MCP: gilde.exe LIVE. Test target: `render_terrain_test` (suite `RenderTerrainGrid`).

## Scope
Only provenanced function in the file:
- `0x5bbbf4 VIBE_Floor_TileIsUniform` -> `guild::render::TileIsUniform`

The inline helpers `TileTypeIndex` / `TileType` in terrain.h carry no own address
provenance; they mirror the address math recovered from 0x5bbbf4 and are correct
(`idx = (mask & y)*size + (mask & x)`) — left as-is.

## VIBE_Floor_TileIsUniform @0x5bbbf4 — FIXED -> VERIFIED-1:1

### Decompile / disasm diff
Args (recovered __usercall): grid@eax(esi), x0@edx(var_10/edi base), span@ecx, y0@ebx(var_1C).
Derived: `v7 = span + x0 = xEnd` (edi); `v12 = y0 + span = yEnd` (var_14); `v11 = y0` (mutated).

Control flow verified line-for-line, all matched the reconstruction:
- 0x5bbc12 `cmp ebx,eax; jg` -> `if (y0 > yEnd) return 1`. OK.
- inner guard 0x5bbc1a `cmp edi,eax; jl` -> `for x while x <= xEnd`. OK.
- inner inc 0x5bbc43 `inc eax; cmp eax,edi; jle`. OK.
- outer inc 0x5bbc4b `inc ebx; cmp ebx,ebp(yEnd); jle`. OK.
- index math 0x5bbc1e..0x5bbc36: `(mask & y)*size + (x & mask)`,
  struct offsets `[esi+0]`=size, `[esi+0xC]`=mask, `[esi+0x14]`=types base. The
  reimpl `TileGrid` packs these to +0/+4/+8 (a reimpl-local descriptor we populate),
  so only the *logical* indexing matters and it matches. No source change needed there.

### Divergence found (FIXED)
`v6` (the reference) lives in `dl` — a **single signed byte (char)**:
- 0x5bbc04 `mov dl,0FFh`  (sentinel = -1)
- 0x5bbc38 `cmp dl,0FFh`  (seed branch test)
- 0x5bbc40 `mov dl,[ecx+ebx]` (byte load of type)
- 0x5bbc63 `cmp dl,[ecx+ebx]` (mismatch test)

The reconstruction modeled `ref` as `int` and seeded `ref = (u8)t` (0..255).
That breaks the **sentinel collision** the binary exhibits when a terrain-type
byte equals 0xFF: in the binary 0xFF == -1 (as char) re-takes the seed branch, so a
0xFF cell encountered while no real reference exists never becomes the reference.
With `int ref`, seeding 255 != -1, so the C++ would (wrongly) lock the reference to
255 and then mismatch every following real cell.

Evidence the values can be 0xFF: type byte is read as a raw byte (`mov dl,[..]`),
full 0..0xFF range; nothing masks it. The sentinel and a real 0xFF type are
indistinguishable in the original — that is the observable behavior to clone.

Behavior to reproduce precisely:
- leading 0xFF cells (before any real ref) re-seed the sentinel -> never become ref;
- once a real ref is set, a later 0xFF cell takes the compare branch and mismatches
  (ref != 0xFF) -> returns 0. (0xFF is "invisible" only while still seeding.)

Fix: `int ref` -> `signed char ref`, and seed/compare with a `signed char` type byte
so 0xFF aliases the -1 sentinel exactly as `dl` does. Branch order/structure unchanged.

```
- int ref = -1;
- u8 t = grid->types[idx];
- if (ref == -1) { ref = t; } else if ((u8)ref != t) return false;
+ signed char ref = -1;
+ signed char t = (signed char)grid->types[idx];
+ if (ref == -1) { ref = t; } else if (ref != t) return false;
```

### Golden
The existing golden (`RenderTerrainGrid.TileTypeIndexAndUniform`) used only types
7/8 — it never exercised the 0xFF collision, so it neither encoded wrong behavior
nor caught the bug. Added a new golden `RenderTerrainGrid.TileUniform0xFFSentinelCollision`
that locks the corrected behavior:
- all-0xFF region -> uniform/true (sentinel never escapes);
- leading 0xFF then reals -> the 0xFF re-seeds, first real cell seeds ref, uniform/true;
- real ref then a 0xFF cell -> mismatch -> false.

## Result
- 1 function diffed; 1 FIXED -> VERIFIED-1:1; 0 BOUNDARY; 0 remaining divergence.
- Build: `cmake --build build --target render_terrain_test -j` OK.
- Test: `ctest -R render_terrain_test` -> 1/1 Passed.
