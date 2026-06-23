# Wave-12 hardening — W12-OFFICE cluster (office / election / guild / council)

Scope (owned src + tests): `src/world/` office\*, election\*, guild\*, council\*,
`townhall_location_recon.{h,cpp}`. MCP was DOWN → hardening only (no new 1:1
reconstruction). Goal: ASAN+UBSAN over the cluster's deterministic rule cores,
adding boundary / malformed / oversized tests and fixing OOB/UB while keeping every
golden + valid-input path byte-identical.

## Build / method

ASAN+UBSAN build dir (unique, cleaned at end):

```
cmake -S . -B build-asan-office -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-office --target <cluster test targets> -j$(nproc)
```

All cluster test targets pass under ASAN+UBSAN. Normal `build/` stays green.

## Fixes (OOB/UB removed — faithful: the original did not corrupt memory)

### 1. office.cpp — office-definition table out-of-bounds reads  (FIXED)

`kOfficeDefTable` holds exactly the 446 bytes of `dword_62EC8E`. The accessors index
it by office *type* / *rank*:

* `OfficeGetCategoryByRank(rank)` keeps the original bound `rank <= 0x25` (37). For
  `rank == 37` it read byte `12*37+3 = 447` — **1 past the 446-byte array**.
* `OfficeDefBookCat/ReqCode/Flag/Id(type)` are fed the holder slot's `type` byte and
  a person's `office358/360/361` bytes. A **malformed save** (`LoadAemter`) can leave
  any value 0..0xFF there, driving these reads far out of bounds (the mutation rules
  `OfficeAssignToCandidate` / `OfficeTransferHoldership` reach them).

In the original binary `dword_62EC8E` is followed by other globals, so an
out-of-range index read adjacent data but never faulted. Our reconstruction holds
only the table, so the read is a genuine OOB.

Fix: routed every table-byte / table-dword read through bounds-checked helpers
(`TableByte` / `TableDword`) that return 0 past the table end. This keeps **every
in-range output byte-identical** (valid type/rank 0..36 unaffected) and reproduces
the original's zero-tail for the `rank == 37` edge (consistent with
`OfficeGetCategoryByRank`'s own `> 37 -> 0` path). Pinned by
`OfficeHarden.{GetCategoryByRankPastTableNoOob, DefAccessorsFullByteRangeNoOob,
GetDefinitionOutOfRange, GetRankRequirementsOutOfRange}` and the malformed-holder
mutation tests `{AssignToCandidateMalformedHolderTypes,
TransferHoldershipMalformedSlotType, ApplyForCandidacyBadOfficeType}`.

### 2. guild_assignment.cpp — `AssignGuildMembers` scratch overflow  (FIXED)

`AssignGuildMembers` hands the `collect` hook a 64-entry `buf[64]` and then loops
`for (i=0; i<n; ++i) buf[i]` over the returned count `n`. A malformed collector
returning `n > 64` (or negative) would index `buf[]` out of bounds. The original's
`CollectByCategory` never overran its fixed scratch. Fix: clamp `n` to `[0, 64]`
before the loops. Pinned by `OfficeHarden.AssignGuildMembersOverflowCollect`.

## BEHAVIORAL — needs MCP (NOT fixed; would change observable output / values unknown)

### A. office.cpp — `kPromotionCost` 7×7 matrix is undersized  (reqCode == 7)

`OfficeCanPromoteRank` computes `row = ReqCode(officeType)`, `col = ReqCode(targetRank)`
and reads `kPromotionCost[row][col]`. The reqCode byte in the def table legitimately
reaches **7** (office types 28..34 carry reqCode 7), but `kPromotionCost` is declared
`float[7][7]` (valid indices 0..6). UBSAN confirms a guard-passing valid pair hits it:

```
src/world/office.cpp:257 runtime error: index 7 out of bounds for type 'float [7]'
```

This is a real OOB on **valid** input, so a clamp would change the returned promotion
cost — a 1:1 question. The original's `dword_62EBCC` matrix must be larger than 7×7
(reqCode 1..7 is reachable), or the indexing is `reqCode-1`. The correct row-7 /
column-7 cost values (or the exact indexing) can only be recovered from the binary
via `get_bytes` / `decompile`, so this is left for MCP follow-up rather than guessed.
The hardening test `OfficeHarden.CanPromoteRankReqCode1to6NoMatrixOob` sweeps the
safe reqCode-1..6 region (office types 0..27) and documents the excluded edge.

## Other cluster files reviewed — no OOB/UB found

`office_forms.cpp` (bad crime case / null shuffle / over-cap successor candidates —
all guarded), `election.cpp` & `election_candidacy.cpp` (16-slot candidate cap,
quorum gate, dedup — guarded), `council.cpp` (`counts[16]` clamp, ballot-index range
check, RNG tie-break bounded), `guild_rank.cpp` / `guild.cpp` (rank-range gates),
`guild_election.cpp` (`GuildSuccessorPick` random walk bounded; `LoadAemter` /
`SaveAemter` length + count guards), `guildstate_recon.cpp` (DFS stack 256 entries;
opaque-id null guards), `office_recon2_rules.cpp` / `office_recon_privilege.cpp`
(table scans capped at 76/27/16 slots), `townhall_location_recon.cpp`
(family-member scan capped at 768). Council winner index is used to subscript a
caller-supplied `candidateObjs[]`; the index is range-checked against
`candidateCount`, so the array sizing is a caller contract (not an internal OOB).

## Tests added

New file `tests/unit/office_cluster_harden_test.cpp` (suite `OfficeHarden`, 70 checks):
table-index boundaries, malformed holder/office types, candidacy for a bad office,
0/many election candidates, 0/many council votes + over-cap candidate counts,
out-of-range ballots, guild rank out of range, successor pick on 1/tie pools,
truncated / bad-count / too-small Aemter save+load blobs, torture form on a bad
crime case + null shuffle order, empty/oversized successor dialog, role-template bad
ids, and the collect-overflow guard. Existing cluster goldens (world_office_flow,
world_amt2, world_trial, world_amt_office, world_court_session, gui_office_forms,
guild_assignment, election_candidacy, office_law3, …) remain byte-identical under
ASAN+UBSAN.

## Not mine (documented for the owners)

`tests/unit/world_law_test.cpp` (`LawHarden.GesetzLoadAcceptsExactCapacityCounts`)
fails on the law cluster's `g_crimeTable` / `g_evidenceOwner` capacity asserts —
unrelated to this cluster (LAW cluster owns `gesetz_flow.cpp` / `world_law_test.cpp`,
both uncommitted sibling edits). `src/sim/charaction_misc.{h,cpp}` had a transient
compile break during the run (SIM cluster); it resolved on retry.
