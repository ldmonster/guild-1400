# Harden — sim/object_lifecycle2.cpp (VIBE_Object_* lifecycle leaves)

Line-for-line decompile + disasm diff of every provenance'd function. DISASM is
authoritative. All 14 functions verified; one source fix applied. Build +
`object_lifecycle2_test` green (1/1, all CHECKs pass).

## Per-function verdicts

| addr | name | verdict |
|------|------|---------|
| 0x583a2c | IsBuildingType | VERIFIED-1:1 (safe-guard noted) |
| 0x586508 | CollectMatchingProts | FIXED -> VERIFIED-1:1 |
| 0x4ffee8 | ResetSpawnTables | VERIFIED-1:1 (layout note) |
| 0x5a8140 | RebuildModelByOwner | VERIFIED-1:1 |
| 0x5a8184 | ClearVisualFlag | VERIFIED-1:1 |
| 0x43f434 | CmdSetActiveHandle | VERIFIED-1:1 |
| 0x43fc8c | SetBlocked | VERIFIED-1:1 (redundant-store note) |
| 0x43fcd8 | SetTransient | VERIFIED-1:1 (redundant-store note) |
| 0x43e79c | SetAngle | VERIFIED-1:1 (edx=x ecx=z ebx=y, arg order) |
| 0x500134 | UnlinkFromChain | VERIFIED-1:1 |
| 0x500174 | InitParticleEmitters | VERIFIED-1:1 |
| 0x506388 | HideFoliageDecor | VERIFIED-1:1 (hook boundary) |
| 0x506430 | HideFoliageByState | VERIFIED-1:1 (hook boundary) |
| 0x4b0e64 | IsNearDoor | VERIFIED-1:1 (pure-math core; chain leaves hooked) |

## Constants verified by get_bytes

- flt_617380 = `db 0f 49 40` = 3.14159274  -> kPi OK
- flt_617384 = `61 0b b6 3b` = 0.0055555556 (1/180) -> kDegPerRadInv OK
- flt_620BF4 = `cd cc 4c 3d` = 0.05 -> kEmitF1 OK
- flt_620BF8 = `cd cc 4c 3f` = 0.8  -> kEmitF2 OK
- flt_620BFC = `00 00 00 3f` = 0.5  -> kEmitF3 OK
- byte_634484 = 0 (HideFoliageDecor textureSet; passed as arg in reimpl)

Struct offsets confirmed against headers: SceneTypeDef.kind @+0; BuildingTypeDef.kind
@+0, .security @+583 (static_assert'd); ObjectRec.alive @+0, .id @+1 (i32), stride 169,
256 slots -> bound 43264 / step 169.

## Fix applied (CollectMatchingProts, 0x586508)

Disasm 0x586531 is `movsx edx, byte ptr [edi]` followed by signed `cmp edx, ecx` /
`jl`: the building type byte (`*a1`) is read SIGN-EXTENDED for both the `>=` compare
and the `589 * v5` kind index. The source compared as unsigned
(`static_cast<int>(buildingTypeByte) >= v4`), which diverges from the binary for type
bytes >= 128 (unsigned 128..255 vs signed -128..-1). Fixed to sign-extend:
`v5signed = (int)(i8)buildingTypeByte`, used for both the compare and the kind lookup.
Matches the binary exactly now. (Test data uses type byte 10, so the existing golden
was already passing; the fix closes the >=128 gap with no golden change needed.)

Also confirmed: the original reads `*a1` and the kind byte INSIDE the loop; the reimpl
hoists the (loop-invariant) `*a1` read and self-kind before the loop — behavior-identical.
Loop count 731 (`cmp eax, 2DBh; jl`), remap sentinel skip `cl == 0x48` (72), spawn-list
write `word_13CE29C[count++] = i`, final `word_13CE860 = count`, returns 0 (or 1 when
`dword_13CE27C == 0`). All matched.

## Notes (verified 1:1, documented nuances — no code change)

- **ResetSpawnTables (0x4ffee8)**: disasm fills via `(dword_122DDAC+3)[eax]` with
  `eax` pre-incremented (1..731) and `cmp eax,2DBh; jl` — i.e. 731 contiguous bytes at
  base+4..+734 set to 0xFF, then 72 bytes at base+5..+76 (`(dword_122DD5D+2)[eax]`,
  eax 1..72). It is a flat byte fill of stride 1, NOT a strided owner-byte write. The
  reimpl models the two regions as dedicated flat arrays `g_objSpawnOwner[731]` /
  `g_objSlotOwner[72]` filled with -1 (0xFF). Observable effect identical: 731 + 72
  bytes set to -1, returns 0. The base+offset shift is internal to the original's
  memory layout and not observable in the reimpl's dedicated arrays.

- **SetBlocked / SetTransient (0x43fc8c / 0x43fcd8)**: the original stores flags530
  TWICE (`*(v2+530)=v4;` then `*(v2+530)=(N*v3)|v4;`). The first store is dead (no
  intervening reader). The reimpl does the single final store; final state identical.
  Mask/shift constants verified: blocked = clear 0xEF, set `16*(flag&1)`; transient =
  clear 0xF3, set `4*(flag&3)`. Null path calls reportError and returns 0.

- **SetAngle (0x43e79c)**: register args edx=x(a2), ecx=z(a3), ebx=y(a4). Writes
  `+132 = *a2*F1*F2` (x->[0]), `+136 = *a4*v4*v5` (y->[1]), `+140 = v4 * *a3 * v5`
  (z->[2], note the `F1 * z * F2` factor order vs `x * F1 * F2`). The y/z source-arg
  swap into angle[1]/angle[2] is faithful. Sets `+528 |= 4`. Returns 0. Float math
  uses x87 doubles in the original; reimpl uses float — products of these scale
  constants are exact at the test goldens (1e-6 tolerance, passing).

- **IsBuildingType (0x583a2c)**: kind set {1,26,11,28,27,3,4} matched verbatim. The
  original has NO null guard on `dword_13CE27C` (`65*a1 + base`); on an unloaded table
  it would deref a null base (UB/crash). The reimpl's `SceneTypeDefAt` null-guard
  (returns kind 0 -> not-a-building) is a safe boundary over original UB, not a
  behavioral change for loaded tables. Same safe-guard pattern in CollectMatchingProts
  / HideFoliageByState type-table reads.

- **InitParticleEmitters (0x500174)**: gated on nodeType (`+533`) == 6. 7 emitters,
  stride 56 (`i != 392; i += 56`). |life| test is the exact sign-bit mask
  `(dword & 0x7FFFFFFF) == 0` -> reimpl `floatBitsZero`. Seeds: `e[8]=(e[6]+e[7])*0.05`,
  `e[7]*=0.8` (+28), `e[9]*=0.5` (+36). Node-own emitter (+92/+96/+100/+148) same
  pattern. Returns node. Matched.

- **UnlinkFromChain (0x500134)**: `v2 = node+512`, `result = node+504`; if v2==0 walk
  `result` via +504 copying `result+512 -> node+512` until `node+512 != 0` or chain
  ends. do/while with leading null-break. Returns last `result`. Matched.

- **HideFoliageDecor / HideFoliageByState (0x506388 / 0x506430)**: prefix tests
  "pfl_"(4) / "vg_"(3) / "!vg_"(4) via StrncmpN (0 == match). ByState: `v5 = security-1`
  (int; security=0 -> v5=-1 -> loop `v5>=i` never runs), `v5 = security` when kind==2;
  early return 1 when visualState in {2,4,3}; loop `for(i=0; v5>=i; ++i)` calls
  selectTextureSet. The unreconstructed `VIBE_Object_SelectTextureSet` 5-arg call and
  StrncmpN are routed through the `selectTextureSet`/strncmp boundary — genuine leaf
  boundary, faithfully hooked (rule 8: not faked, the (node,set) loop bound is exact).

- **IsNearDoor (0x4b0e64)**: exposed as a pure-math core (`ObjectIsNearDoorCore`) over
  pre-resolved inputs because the real impl chains FindByHandle / PointThroughBoneChain
  / VectorWithinTolerance (transform leaves owned elsewhere). Control flow faithful:
  if (target+97 && v3): tolerance test (750.0) of door anchor vs bone-chain point, and
  if door-open flag (`v3[74]`) is 0 -> return true; else fall through to
  `v3 && v3[11] == *(target+1)` owner-id compare. Tolerance 750.0 and the open-flag
  gating matched.

## Build / test

    cmake --build build --target object_lifecycle2_test -j      # OK
    cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original \
      ctest -R '^object_lifecycle2_test$' --output-on-failure   # 1/1 Passed
