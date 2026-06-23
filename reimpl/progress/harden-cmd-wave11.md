# Wave-11 hardening — command/packet pipeline (W11-CMD)

Cluster: `src/sim/command_apply*.{h,cpp}`, `command_codec.{h,cpp}`,
`command_pending.{h,cpp}`, `command_receive.{h,cpp}`, `command.{h,cpp}` (size/
dispatch), the request builders + their tests (`sim_command*`, `command_apply*`,
`command_recon*`, apply/receive/builders tests). The packet parse + apply-handler
side of the deterministic lockstep command system — it ingests fixed-size 153-byte
wire packets and applies them to entity/world state, so it is OOB-prone on
malformed/truncated input.

Method: ASAN+UBSAN build (`-fsanitize=address,undefined -fno-sanitize-recover=all`),
added malformed/truncated/oversized/empty-input tests, fixed OOB with faithful
guards only. MCP was DOWN — no new 1:1 reconstruction; behavioral items flagged.

Build dir (dedicated, to avoid colliding with other agents): `build-asan-w11cmd`
(ASAN). Normal `build/` re-verified green for the changed targets.

Ownership note: `command_recon4_senders.{cpp,h}` was concurrently modified in the
working tree by another wave — NOT edited here (audited read-only; no OOB found in
the parse it owns; its tests `command_recon4_senders_test` / `_senders2_test` pass
clean under ASAN). The net cluster's in-flight `src/net/transport.h` edit (adding
`rx_cap_`) caused one transient compile race during my lib build; it resolved on
its own and is owned by W11-NET (not edited here).

## Audit summary

The 153-byte `CommandPacket` is a fixed `u8 bytes[0x99]` struct, so the fixed-offset
header/payload accessors (`get32(0x10)`, the constant-offset `memcpy`s in apply2–5,
the bitfield/float handlers) are statically in-bounds. The dispatch jump-table is
already guarded (`opcode < kNumOpcodes==96`, null-handler check, `ack_[cmdId &
0x7FFF]` masked, sync-range ack indexed `10*(seq & 0x7FFF)`). The relation grid
handler (`command_apply6` 0x1B) indexes a 768×768 grid only via `FindPersonIndex`
results (bounded `[0,768)` or `-1`→reject) and clamps `0x5D` fill index `<= 4`.
`StagePendingBlock` already rejects `len > 0x3FE` (the original's own cap).

The genuine OOB lived in the **variable-length, cursor-walking** parse loops and one
copy-from-undersized-source caller. All were confirmed by ASAN by neutralizing the
guard and re-running the new test (stack-buffer-overflow each time), then fixed.

## Bugs found & fixed (all FAITHFUL — in-bounds/valid path byte-identical)

### 1. Field-patch parse cursor runs off the 153-byte packet
`src/sim/command_apply.cpp` — `ExPatchObjectFieldsAdd` (0x16 @0x497c18),
`ExWriteObjectFields` (0x17 @0x497da4), `ExApplyNeedDeltas` (0x18 @0x497ed0).
Each walks per-field descriptors `[width:1][count:1][off:2][values:width*count]`
starting at packet `+0x15`, advancing the cursor `p` by `width*count+4` per record
for `bytes[0x14]` records. A malformed received packet with a bogus `+0x14` count
or an oversized `width*count` drives `p`/`vals` past `bytes[153]` (OOB read of
adjacent memory). The need-delta variant reads 5-byte entries the same way.
Fix: bound the 4-byte header read and the `width*count` value read against
`pkt.bytes + kPacketStride` before each record; `break` once a record would read
off the end. A packet built by the codec's `AppendDeltaField`/`AppendRawField`/
`AppendAiMethodEntry` never exceeds the 119-byte payload cursor guard
(`kMaxPayload`), so this is byte-identical on every valid packet.
Pinned by: `SimCmdApply.Malformed_PatchFieldsAdd_OverlargeCount`,
`Malformed_WriteFields_OverlargeCount`, `Malformed_NeedDeltas_OverlargeCount`,
`Malformed_PatchFields_TruncatedRecord` (sim_command_apply_test.cpp).
ASAN-confirmed: removing the guard → stack-buffer-overflow at
`command_apply.cpp` in `ExPatchObjectFieldsAdd`.

### 2. Fragment reassembly buffer overflow on a huge declared length
`src/sim/command_pending.cpp` — `ReassembleReceived` (@0x49377c).
`state.reasm_len = frag->get16(16)` is a u16 (up to 65535) taken straight off the
wire from the first fragment. The chunk loop copies 128 bytes per fragment into the
fixed 1536-byte `state.reasm` buffer (`kReassembleBufBytes`) until `total >=
reasm_len`, so a malformed fragment chain with a huge `reasm_len` runs the `dst`
pointer (and its `memcpy`) far past the buffer — a stack/heap buffer overflow.
A valid block is produced by `GeneratePendingPackets` from a block capped at
`kPendingMaxBlock` (1022), whose reassembly never reaches the buffer end, so the
end-of-buffer bound is byte-identical on every valid fragment chain.
Fix: `if (dst + kFragChunkBytes > state.reasm + kReassembleBufBytes) break;` before
each chunk copy (placed before the un-consumed fragment is advanced/zeroed, so the
consumed count stays exact).
Pinned by: `SimCmdRecv.Malformed_Reassemble_HugeReasmLen` (+
`Malformed_StagePendingBlock_OverCapLength` for the producer cap) in
sim_command_receive_test.cpp. ASAN-confirmed: removing the guard → memcpy
stack-buffer-overflow (`reasm` offset 3128 vs 1536-byte buffer).

### 3. `QueueRequestFlagBlob32` copies 124 bytes from undersized callers
`src/sim/command_apply7.cpp` — `QueueRequestFlagBlob32` (@0x494ab4) does
`qmemcpy(&p.bytes[0x11], blob, 124)`. Two callers — `SetGameSpeed` (@0x493dec),
`IncreaseGameSpeed` (@0x493e2c), `DecreaseGameSpeed` (@0x493e58) — pass `&i32`
(a 4-byte local), so the copy reads 120 bytes off the end of the source local.
In the original this copies indeterminate adjacent stack bytes into the unused
payload tail (no path reads them: only the first dword — the game-speed level — is
observed; receivers/tests assert only `get32(0x11)`).
Fix: each caller now stages a 124-byte zero buffer with the level in dword 0 and
passes that. The observable packet (first dword @+0x11, wire length) is unchanged;
the OOB source read is gone. Also fixed a **test bug**:
`CmdApply7.FlagBlob32SyncMarkerShortLen` passed a 4-byte `i32` as the blob — now a
124-byte buffer (the function's documented contract).
Pinned by the existing `command_apply7_test` (now ASAN-clean: 523 checks).
ASAN-confirmed: the unfixed test aborted with stack-buffer-overflow in
`QueueRequestFlagBlob32` (and again from `SetGameSpeed`) before the fix.

### 4. `CheckParamRefsValid` field-walk runs off the 153-byte command record
`src/sim/command_apply10.cpp` — `CheckParamRefsValid` (@0x495ef8) walks the same
`[width:1][count:1][off:2]+values` descriptors out of the 153-byte command record,
advancing `cur` by header (4) + `width*count` per `cmd[20]` records, and (in the
owner-link branch) reads `rdI32(cmd, cur)`. A malformed `+20` count or oversized
fields runs `cur` past the record (OOB read). `command_apply10.h` does not see
`sim::kPacketStride`, so the bound uses a local `kCmdStride = 0x99` (153) with a
comment. Guarded the header read (`cur + 4 > kCmdStride → break`) at loop top and
the owner-value read (`cur + 4 > kCmdStride → break`) after the header advance.
Byte-identical on valid input (valid deltas stay within the 119-byte payload).
Pinned by: `CmdApply10_Malformed.ParamRefsOverlargeFieldCount` and
`ParamRefsCursorWalksOffEnd` (command_apply10_test.cpp). ASAN-confirmed: removing
both guards → stack-buffer-overflow in `rdU8` (cmd offset past 153).

### 5. `DeltaWriter::ApplyDelta` cursor bound (codec round-trip helper)
`src/sim/command_codec.cpp` — `ApplyDelta(payload, fieldCount, entity)` is the
shared decode helper used by the e2e/itest round-trips. It took a raw `payload`
pointer with no length and walked descriptors by `width*count+4`; a malformed
`fieldCount`/record could read past the encoder's 119-byte payload window. Bounded
the header + value reads against `payload + kMaxPayload` (119). Every caller passes
either `DeltaWriter::payload()` (119 bytes) or the packet payload window
(`a1 + 0x11`, also 119 bytes to the record end), so valid payloads are unchanged.
Covered by the existing `sim_command_test` / `*_e2e` / `*_itest` round-trips (all
ASAN-clean).

### 6. `ComputePacketSize` size-walk bound (0x16/0x17)
`src/sim/command.cpp` — `ComputePacketSize` (@0x493034). The variable-size branch
for 0x16/0x17 walks the same field headers from the packet to sum the wire length;
a malformed `+20` count walked the reader past the 153-byte record. Added
`if (v4 + 4 > kPacketStride) break;` in the field loop — the returned size for a
malformed packet is don't-care, and a valid packet (≤119-byte payload) is
byte-identical. Pinned by `SimCmdApply.Malformed_ComputePacketSize_BadFieldCount`.

## Malformed-input tests added (all ASAN+UBSAN green)

`tests/unit/sim_command_apply_test.cpp` (51 → 64 checks): overlarge field count
(0x16/0x17/0x18), truncated mid-record, opcode out of jump-table range (via
dispatch + direct apply), `ComputePacketSize` bad field count, fill-level index
out of range, relation unknown-person reject, empty/zero packet.
`tests/unit/sim_command_receive_test.cpp` (53 → 59 checks): huge `reasm_len`
fragment chain, `StagePendingBlock` over-cap length + boundary.
`tests/unit/command_apply10_test.cpp` (38 → 40 checks): two `CheckParamRefsValid`
cursor-walk-off-end cases.
`tests/unit/command_apply7_test.cpp`: fixed the undersized-blob test bug.

## BEHAVIORAL — needs MCP (NOT changed; valid-input control flow would differ)

- **`ExApplyNeedDeltas` statId destination bound** (`command_apply.cpp` 0x18
  @0x497ed0): writes to `person + 144 + 12*statId` where `statId` is a wire byte
  (0..255). `144 + 12*255 + 4 = 3208` overruns the 536-byte `Person` record into
  the adjacent `g_persons` slots. The packet-read cursor is now bounded (fix #1),
  but I did NOT add a destination `statId` bound: whether the original clamps
  `statId` to the need-stat count is unknown without MCP, and adding a bound could
  change observable output on the (valid) in-range path. The original arrays are
  contiguous so a moderate overrun stays in-array (the engine's envelope). FLAG:
  decompile @0x497ed0 to recover the statId range/bound.
- **Field-patch destination offset bound** (`command_apply.cpp` 0x16/0x17/0x19/0x1A,
  `command_apply10` 0x16-family): `dst = base + off` with a wire-controlled `off`
  (u16, or u32 in the bitfield/float handlers) writes into the resolved entity
  record (Person 536 / Object 169 / Scene 67 bytes). A valid command writes within
  the record, but the original's destination-side bound (if any, and which record
  size it uses) is unknown without MCP. The packet *source* read is bounded (fixes
  #1/#4); the destination bound is a 1:1 question. FLAG: decompile @0x497c18 /
  @0x497da4 / @0x498024 / @0x498104 to recover any dest bound.

## Status

- Cluster test targets under ASAN+UBSAN: ALL GREEN (22 targets, incl.
  apply/apply2–12, codec, receive, pending, inherit, builders3, unit_orders,
  recon2_sync, recon4_resolve/senders/senders2, syncrange; spot-checked wire
  cmdops/apply_input/recon45).
- Normal `build/`: re-verified green for all changed targets.
- Goldens: byte-identical (e2e/itest wire round-trips unchanged).
- Each of the 4 source fixes was ASAN-confirmed load-bearing (guard removed →
  stack-buffer-overflow on the new test; restored → clean).
