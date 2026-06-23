# Wave-20 — He entity-handler messaging + icon cluster

**Agent:** W20-HE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Module: `src/sim/he_messaging.{h,cpp}` (new) + `tests/unit/he_messaging_test.cpp` (new).
Reconstructs the five entry-reachable (frame loop `0x4c09a0`) "He" functions that
build the event-message command packets, lay out floating event-icons in a circle,
dispatch a handler's icon GFX, and toggle the event-panel active slot.

---

## Reconstructed (1:1 from the decompile + disasm)

| addr | name | bytes | notes |
|------|------|------:|-------|
| 0x4c5c54 | VIBE_He_SendEntityMessage | 323 | type-17 message packet to a kind-6/7 person |
| 0x4c5d98 | VIBE_He_SendQuickjumpMessage | 422 | richer "quickjump" variant (contact name + a5/a6/a7 + 0x02 flag) |
| 0x4c6964 | VIBE_He_AssignIconForHandler | 748 | record-kind → icon-GFX dispatcher |
| 0x4c64bc | VIBE_He_ArrangeIconsInCircle | 365 | even circle layout (trig); radius 100 / 0 |
| 0x4c5b40 | VIBE_EventPanel_HandleSlotClick | 274 | active-slot toggle (hide old / show+raise new) |

### Constants golden-pinned (get_bytes)
- `flt_61E73C` = `0x40C90FDB` = **6.2831855f** (2·π) — angle scale.
- `dbl_61E744` = `0x3FE0000000000000` = **0.5** — height-midpoint factor.
- circle radius immediate `42C80000` = **100.0f** (radius 0 when a single icon).

### ConvertX / float→int audit (per brief)
**No float→int conversions exist in this cluster.** The four messaging/panel functions
are pure integer/byte work. `ArrangeIconsInCircle` does all its float math with x87
then stores results via `fstp` straight into a `float[3]` (no `fistp`, no ConvertX) and
hands that to `VIBE_Object_SetPosition`. The angle/radius/height are floats end-to-end;
the only integer→float is `fild` of the loop index/count (exact). Verified against the
`0x4c64bc` disasm (`fild;fmul flt_61E73C;fld1;fdivrp;fmulp;fsin/fcos;...;fstp`). So the
"ConvertX truncates" caveat does not apply to any site here.

### Message-packet header layout (the genuine boundary detail)
The two builders zero-fill a 248-byte (`0xF8`) header via
`VIBE_Light_SetGrayColorThunk(0, 0xF8, buf)` (a memset-dword thunk), then store:
- `+0x04` byte `0x11` (type 17)         · `+0x36` byte `9` (sub-kind)
- `+0x08` dword sender/recipient (a1)   · `+0x0C` dword a2
- `+0x98` dword payload-len/a4 (entity) or a5 (quickjump)
- `+0x9C`,`+0xA0` dwords a6,a7 (quickjump only)
- `+0x68` quickjump contact-name (wide-copy, cap `0x30`; over → ErrorLog)
- `+0xD4` flags byte (`|0x02` quickjump always; `|0x10` when a 2nd string follows)

The text is wide-copied (2-byte step, terminate on a NUL low byte) and the optional
second string `strcpy`'d after it; the assembled buffer + length go to
`VIBE_Command_QueueRequestBuffer28 @0x494910` (already reconstructed elsewhere). These
offsets are pinned in `he_messaging.h` (`kHeMsg*Off`).

### AssignIconForHandler kind → icon map (record[0] dispatch)
| kind | resolves | icon |
|------|----------|------|
| 2 | building @+16 (+97 gfx) | he_plus_hammer |
| 4 | local-city + (+200), building @+188 | he_ausrufezeichen |
| 0x1A | building @+208 | he_schlaege |
| 0x1B | building @+172 (passes +97 unconditionally) | he_muenze |
| 0x1C | building @+172 | he_hammer_gold |
| 0x1E | building @+172 | he_saege_hammer_gold |
| 0x1F | building @+172 | he_plus_hammer |
| 0x35 | person @+188, local city, !suppress, ob_SCHWARZES_BRETT | he_ausrufezeichen |
| 0x40 | building @+172, local city only | he_fernglas |
| 0x6B | phase==2, !suppress, !office-bit, ob_TRIBUENE | he_ausrufezeichen |

Gated by `byte_123356B` (icons enabled) and `record+136` (skip if gfx ptr already set).
All other kind bytes fall through unchanged (verified against the nested compare tree).

---

## Leaves — routed (genuine cross-module boundaries) vs reconstructed inline

Reconstructed inline: the full control flow, the kind-dispatch tree, the packet byte
layout, the wide-copy, the trig circle layout, the slot-toggle state machine.

Routed through `HeMessagingHooks` (inert defaults in the .cpp; tests install mocks),
because each is an out-of-module engine record/leaf already reconstructed elsewhere:
`VIBE_Person_FindRecordById` (+2 kind byte), `VIBE_Command_QueueRequestBuffer28`,
`VIBE_ErrorLog_ReportMessage`, `VIBE_Building_FindById`, `VIBE_Object_FindByHandle`,
`VIBE_He_CreateGfxInfo` (already in `world/world_history2.cpp`), the He-record field
reads, `byte_123356B`/`word_63CC5C`/`dword_649D60`/`dword_12CEAD8` globals,
`VIBE_Transform_PointThroughBoneChain`, `VIBE_Mesh_ComputeHeightRange`,
`VIBE_Object_SetPosition`, `VIBE_Object_SetValueOrText`, `VIBE_Form_SetObjectsVisible`,
`VIBE_Form_RaiseWindows`, `word_63C740 & 4`. This mirrors the established
WorldHistory2Hooks / CourtCouncilHooks pattern — nothing in `src/` is left dangling.

`He_ArrangeIconsInCircle` reuses the **shared 64-slot icon pool** reconstructed in
`world/world_history2.cpp` (`world::HeIconPool()`, `HeIconSlot`) — no duplicate pool,
no ODR clash. It matches on the slot `+8` field (the owning-parent handle, per the
W16-confirmed misnomer note in world_history2.cpp).

The event-panel globals (`dword_63226C` enabled gate, `dword_632270` active slot,
`dword_62EB4C` input-suppress) are this module's state, exposed via
`EventPanel_SetEnabled/ActiveSlot/SetActiveSlot/InputSuppressed/Reset` for tests/wiring.

---

## Wiring / handoff (rule 13)

The five functions are reached only through indirection / not-yet-reconstructed
callers, so they are exposed and the one-line handoffs are documented here:

1. **Messaging** — ~150 NpcAction/CharAction/Amt/Office step functions call these via
   the `sendEntityMessage` / `sendQuickjumpMessage` / `sendMessage` function pointers
   in `sim/charaction_steps5.h` and `sim/npcaction10.h` (currently `Inert*` stubs in
   `charaction_steps4.cpp` / `charaction_steps6.cpp`). **Handoff:** those bind-site
   files (not owned here) should point those hook members at
   `guild::sim::He_SendEntityMessage` / `He_SendQuickjumpMessage`.
2. **ArrangeIconsInCircle** — called by `VIBE_He_DestroyIconGfx @0x4c68d8` (already in
   `world/world_history2.cpp`) through `WorldHistory2Hooks::arrangeIconsInCircle`
   (inert default). **Handoff:** a binding site sets that hook to a thunk calling
   `guild::sim::He_ArrangeIconsInCircle(reinterpret_cast<const float*>(parentMesh))`.
3. **AssignIconForHandler** — sole caller is `VIBE_He_RunAllHandlers @0x4c6e38` (not yet
   reconstructed). **Handoff:** when `He_RunAllHandlers` is reconstructed it calls
   `guild::sim::He_AssignIconForHandler(record, a2)` per live handler.
4. **EventPanel_HandleSlotClick** — callers `VIBE_Hud_HandleMouseClick @0x4bc280`,
   `VIBE_Hud_ProcessDragDropClick @0x59542c`, `VIBE_CharAction_RunEventMessagebox
   @0x4e0294`. **Handoff:** those HUD/charaction sites call
   `guild::sim::EventPanel_HandleSlotClick(slot, a2, a3, a4)` with the panel enabled.

The previous deferral note for `0x4c6964` in `src/sim/he_recon.cpp` (lines 94-107) is
now superseded by this reconstruction; that comment can be trimmed by the he_recon
owner (not edited here — out of ownership).

---

## Tests (`tests/unit/he_messaging_test.cpp`, suite `HeMsg`, 18 tests / 61 checks)

- SendEntityMessage: rejects missing recipient / wrong kind; builds kind-6 header
  (type/sub-kind/sender/a2/payload-len/flags golden-pinned); kind-7 accepted; second
  string sets `0x10` + correct lengths.
- Quickjump: `0x02` flag always set, a1/a2/a5/a6/a7 dwords pinned, contact-too-long
  (`>0x30`) reports the exact error string.
- ArrangeIconsInCircle: 2-icon circle (x=sin·R+ax, y=(hi-lo)·0.5+lo, z=cos·R+az with
  R=100, anchor=(10,0,20)) golden-checked at θ=0 and θ=π; single-icon radius-0 case.
- AssignIconForHandler: disabled / already-has-gfx no-ops; kinds 0x1C/0x1B/2/0x6B
  dispatch to the right icon string; 0x6B phase gate.
- EventPanel: disabled returns input; activates a new slot (show+raise+set value);
  toggles the old slot hidden + value reset.

**Result:** 18/18 pass, ASAN+UBSAN clean (`-fsanitize=address,undefined`), 61 checks /
0 failures. Builds clean in the full-tree CMake (`he_messaging.cpp.o`) and the
`he_messaging_test` ctest target passes.
