# History — ParseContext substitution resolver + funcs_4FD5D2 dispatch

Module: `src/sim/history_parse.{h,cpp}` (namespace `guild::sim`), tests in
`tests/unit/history_parse_test.cpp` — 20 tests, 88 checks, all passing.

Closes the "wire the resolvers" item from `progress/command-recon4-resolve-senders.md`:
the 10 reconstructed `VIBE_Command_ResolveTarget*` handlers are now genuinely
dispatched through the reconstructed `funcs_4FD5D2` table, exactly as the original.

## Reconstructed 1:1

- **0x4fd44c `VIBE_History_ParseContext`** (`__usercall`, eax=(token@eax, out@edx,
  params@ebx)) → `HistoryParseContext(token, out, params)`. Translated from the
  disassembly (the Hex-Rays `v17>>24` artifacts are just the byte locals var_14 /
  var_18 read back sign-extended):
  - prefix-mode scan: token's first dword vs `dword_6343B8` entries 0..2
    ("_NEW"→0 / "_USE"→1 / "_REL"→2, 5-byte stride; no match → mode 4 literal);
    the loop compares all 3 entries (a match does not break — faithful).
  - prefixed tokens: group gate (`params && *(u32*)params`, else "Wrong group
    reference"); slot digit `token[5]` through `VIBE_Util_ParseInt` 0x5dc070
    (reused: `guild::world::UtilParseInt`); signed gate `0 <= slot < 8` (else
    "Wrong slot reference %i"); tail = token+6.
  - mode 1 (`_USE`): replacement index read back from byte `params[8*slot+8]`
    (no name scan; resolver name arg stays token+6).
  - modes 0/2/4: role-name scan at tail+1 against the 15-entry / 64-byte-stride
    table @0x633ff8 (`memcmp` over `strlen(name)` — prefix match, table order
    wins); on match tail advances past the keyword; mode 0 (`_NEW`) back-writes
    the index byte to `params[8*slot+8]`.
  - `repl >= 15` (signed byte) → "Unknown Replacement" → 0.
  - dispatch `funcs_4FD5D2[repl](mode@al, params@edx, tail@ecx, slot@ebx, out)`;
    a zero result zeroes `*(u32*)params` (group invalidation), result returned.
  - the three failure messages are formatted into a dead 256-byte stack buffer
    in the original (VIBE_Crt_Sprintf_0, no observable effect); reproduced via
    snprintf into the same dead local, strings kept as named constants
    (`kHistErr*`, 0x620890 / 0x6208c4 / 0x6208f8).
  - **Documented divergence:** a `_USE` row byte ≥ 0x80 sign-extends negative,
    passes the original's `jge 15` gate and wild-calls memory *before* the table
    (UB — the engine only ever stores 0..14 via `_NEW`). The reimpl returns 0
    for negative indices instead of crashing.

## The dispatch table — gilde.exe 0x6343d8 (`funcs_4FD5D2`, 15 × 4 bytes)

Recovered with `get_bytes`. Slot stride 4; indexed by replacement-name index
0..14 (the data from 0x634414 onward — 0x4fb0f0, 0x4fb180, ... — is an adjacent,
separate table not consumed by ParseContext).

| slot | keyword @0x633ff8+64*i | target     | status |
|-----:|------------------------|------------|--------|
|  0 | BUERGERMEISTER      | 0x4f8fac `VIBE_Command_ResolveTargetGuard`             | HOOK (pending) |
|  1 | BISCHOF             | 0x4f90e0 `VIBE_Command_ResolveTargetOfficial`          | HOOK (pending) |
|  2 | RND_GILDENMEISTER   | 0x4f9238 `VIBE_Command_ResolveTargetClergy`            | RECON, wired |
|  3 | RND_AMTSTRAEGERIN   | 0x4f9518 `VIBE_Command_ResolveTargetPersonByName`      | RECON, wired |
|  4 | RND_AMTSTRAEGER     | 0x4f989c `VIBE_Command_ResolveTargetPersonAlt`         | RECON, wired |
|  5 | RND_AMTSPERSON      | 0x4f9c20 `VIBE_Command_ResolveTargetPersonScoped`      | RECON, wired |
|  6 | REICHSTER_EINWOHNER | 0x4f9f74 `VIBE_Command_ResolveTargetBestRated`         | RECON, wired |
|  7 | BESTES_WIRTSHAUS    | 0x4fa178 `VIBE_Command_ResolveTargetBestThief`         | HOOK (pending) |
|  8 | GELD                | 0x4fa290 `VIBE_Command_ResolveTargetSelectedStat`      | HOOK (pending) |
|  9 | STADTKASSE          | 0x4fa3bc `VIBE_Command_ResolveTargetBuildingStat`      | HOOK (pending) |
| 10 | RND_KIRCHENBERUF    | 0x4fa50c `VIBE_Command_ResolveTargetCraftWorker`       | RECON, wired |
| 11 | RND_REICH           | 0x4fa818 `VIBE_Command_ResolveTargetByStatGroup`       | RECON, wired |
| 12 | RND_NPC_EINWOHNER   | 0x4faab8 `VIBE_Command_ResolveTargetWoundedPerson`     | RECON, wired |
| 13 | RND_HANDELSHERR     | 0x4fac80 `VIBE_Command_ResolveTargetByProfessionRange` | RECON, wired |
| 14 | RND_SPIELER         | 0x4faf54 `VIBE_Command_ResolveTargetRandomCarried`     | RECON, wired |

The 5 HOOK slots route through `HistoryParseHooks` (inert default: return 0 →
token unresolved → group gate zeroed → the text pass fails the label, the safe
no-op). Group-tag / role-name tables are reused from `world/history_full.cpp`
(`kHistoryPrefixTable` / `kHistoryRoleNames`, byte-exact recoveries) — no
duplicate definitions.

## Wiring (rule 13)

- The table dispatches **directly** into the 10 real `guild::sim::ResolveTarget*`
  functions — verified by address-equality tests plus live end-to-end dispatch
  tests (`_NEW` → WoundedPerson scan + back-writes; `_USE` → confirm path via
  FindRecordById; `_REL` → RandomCarried kind-2 reject + group invalidation).
- ParseContext's only original callers are the two call sites **0x4fdf1f /
  0x4fe04e inside `VIBE_History_ParseTextSecondPass` 0x4fdcec**, already
  reconstructed as `guild::world::HistoryParseTextSecondPass` behind a
  `HistorySubstResolver` hook. `guild::sim::MakeHistorySubstResolver(params)`
  adapts the real ParseContext into that hook (1024-byte out scratch, matching
  the original's v48/v51 buffers); covered by two second-pass wiring tests.
  No production caller of the second pass exists in-tree yet (it is itself a
  hook-consumed leaf of the chronicle pipeline), so the chain currently ends
  there — callers of 0x4fdcec are pending in the chronicle driver.

## Token shape note

Chronicle tokens reach ParseContext from the second pass as
`_<head>--<digit-class run>` (e.g. `_RND_NPC_EINWOHNER--0`,
`_NEW%0%RND_REICH--2`); ParseContext is offset-faithful (`token[0..3]` tag,
`token[5]` slot digit, scan at +7 / +1) and never validates the separator chars
at `token[4]` / `token[6]`.

## Known sibling discrepancy (not fixed here)

`world/history_full.cpp HistoryTokenSlot` reads `token[4]` as the slot digit
while the binary (and `HistoryParseContext`) reads `token[5]`; that classifier
is a standalone helper not on the dispatch path, left to its owning module.
