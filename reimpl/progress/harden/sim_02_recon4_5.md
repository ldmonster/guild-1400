# Harden sweep — sim chunk 02: character_recon4_avatar / character_recon4_flags / character_recon5_spawn

MCP-verified 1:1 diff of every provenanced function in these three .cpp files against
gilde.exe. Decompile + targeted disasm used for every branch, constant, float→int site,
and signed/unsigned compare.

Result: **10 functions verified** — 8 VERIFIED-1:1, 2 FIXED, plus several documented
boundaries (indeterminate scratch regs / unmodeled subsystem data). All three test
binaries build and pass (avatar 44 checks, flags 30 checks, spawn 29 checks; +1 new
flags test, +1 new spawn test).

---

## character_recon4_avatar.cpp

### EnsureObjectAvatar 0x505074 — VERIFIED-1:1
- Slot read `(dword_122DDAC + type + 1) >> 24` (sar, signed top byte) == byte at
  base+type+4; write `byte_122DDB0[type]` == base+type+4. hiByte=4 both. Confirmed via
  disasm (`sar ecx,18h`, `mov byte_122DDB0[ecx],bl`). Matches `kObjectSlotHiByte=4`.
- 984 stride built by the shift chain at 0x5050c8..0x5050de (×984). Name src
  `65*type + dword_13CE27C + 1` (shl 6 + add). Copy loop (0x505103) advances **dst by 2**
  — matches `CopyAvatarName`.
- BOUNDARY (0x50508c): special-type test is `cmp edx, ds:dword_63174C` — a POINTER
  identity compare of the arg pointer against dword_63174C, not a value compare. The
  reimpl models it as `type == H.specialObjectType` (value), the only representable form
  without engine pointer identity; documented in header. Same for building (0x505156).

### EnsureBuildingAvatar 0x505134 — VERIFIED-1:1
- Read `(dword_122DD5D + type) >> 24` == byte base+type+3; write base+type+3. hiByte=3
  both. Special test `a1==dword_631748 || *a1==30` (pointer-or-value). Name stride 589,
  +1 header. Copy loop dst+=2. All match.

### EnsureGateAvatars 0x505870 — VERIFIED-1:1
- `QueryBegin(a2,1,5,11)`; on null returns the (null) result; else tail-calls
  EnsureBuildingAvatar(rec, a1, v3) with v3 = uninitialised `cx`. Reimpl passes 0 for the
  indeterminate a3 (documented). 1:1.

### DestroyThunk 0x43cb20 — VERIFIED-1:1  (return Character_Destroy(*p))
### CmdPreloadSitMeshStub 0x43d9f0 — VERIFIED-1:1  (return 0)

---

## character_recon4_flags.cpp

### RefreshAllFlags 0x4b5fa4 — VERIFIED-1:1
- Save dword_649D60; SwitchActiveSlot(0,1,a2,a1); person loop (filter 1/4) →
  WalkAndInvoke/ShowFlag when person+97 != 0; gameobject loop (3/7/3..4/29) →
  IndexFromPointer(char+136) → SwitchActiveSlot → texture predicate.
- Texture predicate confirmed: `*((_DWORD*)v3+21)` = +84 dword buildingId (skip 0,
  skip ==1341); `*((_BYTE*)v3+2)` type in {5,6,7}; `*((_BYTE*)v3+84)-61` = texIndex.
  All offsets/constants match the view fields.
- BOUNDARY: the 3rd SwitchActiveSlot arg is the caller's `ecx` (a2 / v8), which the sole
  caller (ExSysMessage case 13, 0x498d84) does NOT initialise before the call — an
  indeterminate scratch value. Reimpl passes 0 (consistent with the gate-avatar v3
  treatment). Not observable through modelled hooks.

### SyncTurnState 0x531e60 — **FIXED** (2 sub-fixes)
- **FIX 1 (0x531e96): signed >=10 compare.** Disasm: `cmp dl,0Ah` / `jge` (SIGNED), not
  `jae`. State bytes 0x80..0xFF are negative → they take the ELSE branch, not the HIBYTE
  branch.
  - before: `if (state == 6 || state == 7 || state >= 10)` with `u8 state` (unsigned →
    0xFF wrongly entered HIBYTE branch).
  - after: `... || static_cast<i8>(state) >= 10`.
  - evidence: 0x531e93 `cmp dl, 0Ah`, 0x531e96 `jge loc_532069`.
- **FIX 2 (0x532078): HIBYTE prev re-reads the SAME cell.** The HIBYTE branch re-loads
  `byte_12CE912[536*a1]` — identical to `state` — so prev≡state and the inner
  `prev∈{6,7}` is just `state∈{6,7}` (always false on the state≥10 sub-path).
  - before: used a separate `v.prevStateByte` (could diverge from state).
  - after: `const u8 prev = state;` (prevStateByte retained only as a doc alias).
  - evidence: v3 = byte_12CE912[v2*2] with v2=268*a1 (→536*a1); v13 = byte_12CE912[536*a1]
    — same address.
- VERIFIED parts: else-emit set {1,2,4,5}∪(data372≠0) all equality compares; building
  branch — rank≠0 (`test;jz`), rank<6 (`cmp 6;jge`), gauge≥1.0 (`fld;fld1;fcompp;ja`
  skip → keep when !(1>gauge)), ledger≥0 (`jl` skip). data372 @ +536*a1, ledger
  dword_12CEAA4 @ +ecx, rankInput = HIBYTE(unk_12CEA71+536*a1) (sar 18h), textByte =
  (v5+353)>>24 (sar 18h). The +93 (174h) refcount bump/restore around DrawProductionGauge
  is a net no-op (faithfully modelled). No float→int truncation: gauge stays float (v27).
- New golden test `SyncTurn_state255_signed_falls_to_else_branch` pins FIX 1.

---

## character_recon5_spawn.cpp

### SpawnOfficeStaffActor 0x57c744 — VERIFIED-1:1
- Defaults v20=dword_577A68 (4), v22=dword_577A78 (3); v7=a4?a4:v20 (pos),
  v8=a3?a3:v22 (rot). a2<0 → return 0. SwitchActiveSlot(a2,1,0,v8). a6 → FindByHandle.
  `if(v9||v7||v8)` then bone-chain (`v9+19` = +76 = obj.pos) else copy v7[0..2].
- Head-variant copy loop (0x57c866): reads src 2 bytes, advances **dst by 1**
  (`*((_BYTE*)v15++ +1)=v17`) — the deliberate distinction vs CopyAvatarName's dst+=2.
  Confirmed; the "Wirt"→"Wr" golden pins it.
- SetWorldTranslation gated on a3 (rotPtr), passed rotPtr. CreateFromModel universe = v8
  (rot). Final SwitchActiveSlot(v23,1,v9,v8). Pointer-as-int arg passing is a faithful
  bit-copy of the binary. 1:1.

### SpawnAtBuildingEntrance 0x57c8f0 — **FIXED**
- **FIX (0x57c9a9): entrance→staff pos/rot args were swapped on the queue branch.**
  Binary: `SpawnOfficeStaffActor(v8, v13, a5, a2, 0, v20)` → staff a3(rotPtr)=a5(entrance
  rotPtr), staff a4(posPtr)=a2(entrance posPtr).
  - before: `SpawnOfficeStaffActor(person, slot, posPtr, (f32*)rotPtr, ...)` — posPtr
    landed in the staff rotPtr slot and rotPtr in the posPtr slot.
  - after: `SpawnOfficeStaffActor(person, slot, (f32*)rotPtr, posPtr, ...)`.
  - evidence: decompile arg order (v8,v13,a5,a2,...) + entrance prototype a2@ecx=posPtr,
    a5(stack)=rotPtr; reimpl header maps posPtr=a2, rotPtr=a5.
- VERIFIED: arg map personId=a4, building=a1, objHandle=a3, posPtr=a2, rotPtr=a5;
  FindRecordById; person+97 already-spawned → 0; QueryBegin(1,1,building); objHandle==-1
  skips GameObjectQueryFind (v9=building) else requires non-null objRec; production →
  EnsureBuildingAvatar(rec,v9) else (objRec==0 → slot 0) / EnsureObjectAvatar; slot<0 → 0;
  SwitchActiveSlot(slot,1,v11,slot); queue(objRec) → wait-anim/dummy_EINGANG, else
  dummy_TUER; PreloadAniSet(handle,1,"bewegung/gehen") when handle; restore slot; +44/+48
  stores; visibility kept only when (special1==0||rec==special1)&&(special2==0||rec==
  special2). All match.
- BOUNDARY (0x57ca54 dummy-TUER else-branch): binary passes findCtx=*(rec+97) and a6 =
  the "dummy_TUER" string pointer (used as a FindByHandle handle). The reimpl passes
  findCtx=rec, findHandle=0 — the tag-string-as-object-handle behaviour drives an
  unmodelled object subsystem (rules 6/8); findByHandle never fires with findHandle=0 so
  findCtx is inert. Documented.
- New golden test `EntrancePosRotForwardedInOrder` pins the FIX via SetWorldTranslation
  firing with the rotVec pointer (would NOT fire under the old swap).

### PreloadSceneAnimations 0x50650c — VERIFIED-1:1
- Anim-set row located by walking byte_6344A4 (stride 1729) until keyed byte (dword_6344A0
  +1+v2 >>24) == *v3 or terminator zero. CollectByOwner(a1) single arg. PreloadAniSet
  called with count **16** and exactly 16 slice ptrs (v6+1,+49,...,+721; stride 48 →
  16) — confirmed by counting the call args at 0x50666c; pinned by
  PreloadAniSetCountIsSixteen. Match condition `setKey==key || matchVal==setMatch`,
  objPtr(+97) nonzero gate. Loop over word_12CE910 (stride 536) to byte_1333110 modelled
  via `count`. The actual slice string bytes are engine table data (boundary; modelled as
  empty slices). 1:1 on the orchestration.

---

## Counts
- Functions: 10 total — 8 VERIFIED-1:1, 2 FIXED (SyncTurnState ×2 sub-fixes;
  SpawnAtBuildingEntrance pos/rot swap).
- Boundaries documented: 4 (special pointer-identity ×2 collapsed to value hooks;
  RefreshAllFlags indeterminate ecx; dummy-TUER tag-string-as-handle).
- Tests: +1 flags (`SyncTurn_state255_signed_falls_to_else_branch`),
  +1 spawn (`EntrancePosRotForwardedInOrder`). All pass:
  avatar 44, flags 30, spawn 29 checks, 0 failures.

## Handoffs
- None outside the chunk. No shared symbols changed. No files outside the three .cpp +
  their two unit tests touched. Not committed (per memory rule).
