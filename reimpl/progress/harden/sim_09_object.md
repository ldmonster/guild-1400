# Harden report — src/sim/object.cpp (GameObject record lifecycle)

1:1 line-for-line diff of every provenance-bearing function in `src/sim/object.cpp`
against the gilde.exe decompile **and** disasm (module `gilde.exe`, imagebase
0x400000). Where Hex-Rays collapsed `__usercall` register args or mislabeled
strides, the disasm was used as the reference of record.

Test target: `sim_object_test` — built green, all 13 suites pass.

## Per-function status

| Addr | Name | Status |
|------|------|--------|
| 0x585980 | FindFreeSlot | VERIFIED-1:1 |
| 0x585aa4 | FreeChildList | VERIFIED-1:1 (index-model sentinel) |
| 0x585af4 | AddObjekt (record core) | VERIFIED-1:1 core; FIXED special-prototype block |
| 0x5862a4 | AddObjektToParent | VERIFIED-1:1 |
| 0x5859b4 | RemoveByProt | VERIFIED-1:1 |
| 0x585a30 | RemoveById | VERIFIED-1:1 |
| 0x5863b4 | RemoveObjektAmount | VERIFIED-1:1 |
| 0x586458 | DecrementObjektStock | VERIFIED-1:1 |
| 0x5916d0 | ResolveOwnerOrParentA | VERIFIED-1:1 |
| 0x591730 | ResolveOwnerOrParentB | VERIFIED-1:1 |
| 0x591790 | ResolveTypeFieldA | VERIFIED-1:1 |
| 0x591818 | ResolveRootContainer | VERIFIED-1:1 |
| 0x58f1f4 | CountAtLocation | VERIFIED-1:1 core; BOUNDARY currency filter |
| 0x58f240 | SumValuesAtLocation | VERIFIED-1:1 core; BOUNDARY currency filter |
| 0x5914fc | SumChildMoney | VERIFIED-1:1 core; BOUNDARY currency filter |
| 0x591584 | SumMoneyObjects | VERIFIED-1:1 core; BOUNDARY currency filter |
| 0x590200 | CollectStorageBuildings | VERIFIED-1:1 core; BOUNDARY storage filter |

Counts: 17 functions — 11 fully VERIFIED-1:1, 1 FIXED (+ otherwise verified),
5 verified-core with a documented data BOUNDARY in the QueryFind category filter.

## FIXED

### 0x585af4 AddObjekt — fabricated special-prototype block removed
**Before** (src lines ~281-293):
```cpp
if (prototype == 308) {
    ObSetAmount(newNode, ObGetPrototype(newNode) - 307);   // FABRICATED
} else if (prototype >= 0x134 && prototype <= 0x136) {
    int capChild = GameObjectAddObjekt(ObGetId(newNode), 477, 1, newNode);
    if (capChild >= 0) ObSetAmount(capChild, ObGetPrototype(capChild) - 307);
    StubSpawnTransportModel(newNode, prototype);
    StubAttachAvatar(newNode);
}
```
**Evidence (disasm):**
- Prototype 308 does **not** set the parent's amount. The disasm dispatch
  (0x585c79 `cmp dx,135h` … 0x5860d4 `cmp dx,134h; jz loc_585D2E`) routes 308
  and 0x134-0x136 to **LABEL_35 / loc_585D2E** (0x585d2e), the cart/horse path.
- The `*a4 - 307` write targets the **embedded 477 child**, not the parent:
  0x585dcc `mov [esi+14h]` after `AddObjekt(*(a4+1),477,1,a4)` at 0x585db4; the
  `*a4 - 51` write is `[esi+1Ch]` at 0x585dda. Both live inside the deferred
  cart-construction block (Person_QueryBegin @0x585ddd, Character_* ,
  Universe_SwitchActiveSlot @0x585e22, Object_AttachToUniverseNode @0x585f1e,
  RandNext @0x585e83 model-name selection).
- The universal LABEL_22 tail (0x585c95 `mov dl,[edi]; cmp dl,17h`) reads the
  prototype-definition table byte `*(dword_13CE27C + 65*prototype)` and only then
  conditionally zeroes +0x1C — not an amount touch.

The reconstruction was making the parent stack's amount = prototype-307 for 308
(never happens in the binary) and partially spawning the 477 child outside its
real (render-gated) context. **After:** the fabricated logic is removed and the
entire prototype dispatch is documented as DEFERRED/BOUNDARY (see below); the
deterministic record core (prototype/id/location/owner/amount/fill/childHead/
sibling + dword_6498C0 bump) is unchanged and remains 1:1. No test exercises
308/0x134-0x136/310, so the golden is unaffected (still green).

## VERIFIED-1:1 — evidence notes

- **FindFreeSlot 0x585980.** `do{ if(*(WORD)(base+v1)) v1+=67; else v0=base+v1; }
  while(!v0 && v1<548864)` → returns the FIRST free slot (548864 == 67*8192).
  Reimpl returns first index with prototype==0, else -1. Equivalent.
- **FreeChildList 0x585aa4.** `*v1=*(v2+63)` splice; recurse on `*(v2+20)`;
  `*(WORD)v2=0`; `dword_6498C0--`; `while(v2)`; empty-head returns -1, null head
  field returns the (null) arg. Reimpl mirrors with the index-model null
  sentinel -1 (binary uses 0; index 0 is a valid node, so -1 is the faithful
  index translation, documented in object.h). Recursion passes the child-head
  cell address. ✔
- **AddObjekt core.** Field init verified against disasm 0x585c27:
  `[+12h]=64h (100)`, `[+13h]=0`, `[+14h]=0 (childHead, reimpl -1)`, `[+6]=loc`,
  `[+0Eh]=amount`, `[+3Fh]=0 (sibling, reimpl -1)`, id `[+2]=dword_649890++`,
  owner `[+0Ah]` resolved (obj: `*(obj+1)`; scene: `*(scene+0Ah)`; person:
  `*(person+4)`), count `dword_6498C0++`. ResolveEntityById out-param order is
  (obj=v50, scene=v49, person=v51); the head/owner double-resolve in the reimpl
  is safe because ResolveEntityById is pure (saves/restores dword_6498C0).
  SetGrayColorThunk(0,31,a4+1Ch) writes the +28 color block — render leaf,
  DEFERRED.
- **AddObjektToParent 0x5862a4.** `if(!prot) return 0`; resolve; QueryFind(*head,
  1,0,prot>>16); on hit `*(node+14)+=amount` and return node; else AddObjekt.
  Prototype read as `*(int)&v11[2] >> 16` (sign-extended i16). ✔
- **RemoveByProt 0x5859b4 / RemoveById 0x585a30.** Walk sibling chain (+63),
  count predecessors; on match free child list at +20 if set, splice (prev+63 or
  *head), zero +0 word, `dword_6498C0--`; empty/null -> -1, no-match -> -2. Both
  free `*(v5+20)` (RemoveByProt: `(_DWORD)v5+5 == +20`; `v5+10` int16 == byte
  +20). ✔
- **RemoveObjektAmount 0x5863b4.** Signed `if((int)amount > v6) return 0`; else
  `*(node+14)-=amount`; `if(*(node+14) <= 0) RemoveByProt; return 1`; no-match
  returns the ResolveEntityById code. ✔
- **DecrementObjektStock 0x586458.** Disasm-confirmed: AddObjekt 4th arg (ebp)
  carries the original `amount`, but AddObjekt overwrites a4 with FindFreeSlot's
  result before any use, so the ownerHint value is dead — reimpl `-1` is
  equivalent. `sub [eax+0Eh], ebp` (amount -= amount); removal at exactly 0
  (`cmp …,0; jnz`). Prototype via `sar edx,10h` (signed). ✔
- **ResolveOwnerOrParentA/B 0x5916d0/0x591730.** ResolveEntityById(&obj,0,
  *(node+10),&person); person -> person; else obj && `*(obj+37 | +39)!=0xFFFF`
  -> `&word_12CE910[268 * faction]`; the 268-word stride == 536 bytes == Person
  record (reimpl `g_persons[faction]`); kPersonCapacity bound added defensively
  (binary indexes unconditionally). ✔
- **ResolveTypeFieldA 0x591790.** person -> `*person` (marker +0); obj ->
  `*(obj+37)`; else -1. ✔
- **ResolveRootContainer 0x591818.** v2=*(node+6); -1 -> 0; loop resolve(v2):
  obj -> obj; person -> 0; scene -> v2=*(scene+6); -1 -> 0; unresolved -> 0. ✔

## BOUNDARY (data not in static call tree — rule 8)

### Currency aggregation filter — 0x58f1f4 / 0x58f240 / 0x5914fc / 0x591584
The binary builds the iterator with `QueryFind(head, 1, 4, 9)` (0x5857fc):
arg `4` is selector code 4 -> `byte_6498CE = 9`. In IterNext (0x58529c, 0x5853d6)
this filters on **`*(char*)(dword_13CE27C + 65 * prototype) == 9`** — the
*prototype-definition table* category byte (base dword_13CE27C, stride 65), NOT
`prototype == 9`. No DFS/flat selectors are set (codes 6/7 absent), so traversal
is a flat sibling walk from `head` summing the amount field (+0Eh):
`SumMoneyObjects` reads `*(i+7)` (= +14). dword_13CE27C is the runtime-loaded
prototype table and is 0 in the static IDB — **not in the tree**. The
reconstruction filters `ObGetPrototype(n) == kObjProtCurrency(9)` as the closest
faithful proxy (the currency good's id is 9; its typedef category is also 9 in
shipped data, but that mapping lives in unloaded data). Left as a documented
BOUNDARY; the golden `CurrencyAggregation` encodes the proxy and stays green.

### CollectStorageBuildings — 0x590200
`QueryFind(0, 2, 7, 4, 29)` -> two selectors: code 7 (`byte_6498D4=1`, flat
array scan) and code 4 (`byte_6498CE=29`, typedef-category filter via the same
`dword_13CE27C + 65*prototype` table). Match test is `*(v4+7)==*(building+1)`
(node +0Eh == building id +1), collecting up to 3 (`if(v5>=3) return v5`) into
`a2`. The reimpl does the flat scan with `count<3` cap and the +0Eh==building.id
match faithfully, but substitutes `ObGetPrototype(i)==29` for the typedef
category-byte filter (same unloaded-table BOUNDARY as above). Flat-scan control
flow, cap, and field offsets are 1:1.

### AddObjekt prototype dispatch (0x585c79..0x586297)
Render/Character/Universe leaves (rules 3-5) plus the prototype-definition table
(LABEL_22 tail). DEFERRED in full; see the FIXED note above. The deterministic
record core is faithful and tested.

## Notes on the index-model substitution (documented in object.h, unchanged)
- Link fields hold node **indices** (-1 == null) instead of raw 32-bit pointers;
  the binary uses 0 as null. Index 0 is a valid node, so -1 is the only correct
  index sentinel — every walk/compare uses `>= 0`.
- Container heads: the binary stores child-list heads inside the container
  record at kind-specific offsets that even vary per function (object +93/+5Dh,
  scene +20, person +188 in AddObjekt vs +376 in Decrement/RemoveObjektAmount).
  The reimpl resolves scene containers to their real +20 field and routes
  person/object containers through a single id-keyed side table — consistent
  within the index model and behavior-equivalent for the scene-container case the
  tests exercise.

## Test result
`cmake --build build --target sim_object_test -j` — built green.
`ctest -R '^sim_object_test$'` — **1/1 Passed** (13 TEST blocks).
