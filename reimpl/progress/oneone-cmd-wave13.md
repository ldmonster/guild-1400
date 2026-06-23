# Wave-13 1:1 audit — command apply/queue pipeline (W13-CMD)

MCP was DOWN: this is the MCP-free half of the 1:1 audit — cross-check each
reconstructed function against in-tree evidence (provenance headers + progress
docs), pin its recovered 1:1 values with golden tests, and produce a confidence
map. The Hex-Rays decompile remains the reference of record; no constant/table
was invented (rule 8). All edits were TEST additions only — no source change was
needed (no drift found).

## Segment

`command.{h,cpp}`, `command_codec.{h,cpp}`, `command_pending.{h,cpp}`,
`command_receive.{h,cpp}`, `command_recon2_sync.{h,cpp}`,
`command_recon_syncrange.{h,cpp}`, the `CommandQueue`, and the two apply handlers
called out in the brief (`ExComputeObjectCoords` @0x49818C opcode 0x1B,
`ExSellObjekt` @0x496B90 opcode 0x11, both in `command_apply6.{h,cpp}`), plus the
`CheckSyncRangeAcked` scan @0x493a34 (`command_apply10.cpp`).

## Inventory + provenance (every function carries its gilde.exe address)

| function | addr | file |
|---|---|---|
| CommandQueue::Init (QueueInitAndSync init half) | 0x4931e0 | command.cpp |
| ComputePacketSize | 0x493034 | command.cpp |
| EnqueuePacket | 0x49388c | command.cpp |
| FlushSendQueue | 0x4934cc | command.cpp |
| StoreReceivedPacket | 0x493f80 | command.cpp |
| UnlinkReceivedPacket | 0x494028 | command.cpp |
| ExecCommands | 0x494088 | command.cpp |
| GetPacketStatusById / GetPacketSeqById | 0x4939d4 / 0x4939fc | command.cpp |
| BeginDeltaPacket | 0x493a94 | command_codec.cpp |
| AppendDeltaField / AppendRawField / AppendCopiedField | 0x493aec / 0x493c14 / 0x493c90 | command_codec.cpp |
| BeginAiMethodPacket / AppendAiMethodEntry | 0x493d64 / 0x493d9c | command_codec.cpp |
| QueueRequest16/17/Args25/Coord27/State22, EnqueueObjectInteraction | 0x494630/0x49465c/0x494810/0x494878/0x494750/0x4944f0 | command_codec.cpp |
| StagePendingBlock | 0x49436c | command_pending.cpp |
| GeneratePendingPackets | 0x493584 | command_pending.cpp |
| ReassembleReceived | 0x49377c | command_pending.cpp |
| CheckReassemblyComplete | 0x4936e4 | command_pending.cpp |
| EmitWithPendingBlock (EnqueuePacket tail @0x4939a1) | 0x49388c | command_pending.cpp |
| ReceiveAndQueue | 0x493ebc | command_receive.cpp |
| ExecCommandGroup | 0x4942c0 | command_receive.cpp |
| ExecReceivedCommands (networked ExecCommands) | 0x494088 | command_receive.cpp |
| SyncSceneObjectStates / SyncSceneEntryExit / SyncCharSlotAssignments | 0x500c38 / 0x500f0c / 0x501064 | command_recon2_sync.cpp |
| MarkSyncRangeStart / MarkSyncRangeEnd | 0x493a1c / 0x493a28 | command_recon_syncrange.cpp |
| CheckSyncRangeAcked | 0x493a34 | command_apply10.cpp |
| ExSellObjekt / ExComputeSellableAmount | 0x496B90 / 0x497538 | command_apply6.cpp |
| ExComputeObjectCoords (relation matrix, opcode 0x1B) | 0x49818C | command_apply6.cpp |

**RED FLAGS (missing provenance): NONE.** Every reconstructed function in the
segment has a `// gilde.exe 0xXXXX` header.

## PINs verified / added

The brief's pin list, all now backed by golden assertions:

- **Packet stride 153.** Already static-asserted; NEW runtime golden
  `SimCommand.PacketGeometryConstantsGolden` pins kPacketStride (0x99/153),
  sizeof(CommandPacket)==153, kSendRingSlots (0x8000), kSeqMask (0x7FFF),
  kAckEntryBytes (10)/kAckTableBytes (327680), kMaxPayload (0x77/119),
  kNumOpcodes (96), the header field offsets (+0/+1/+3/+4/+8/+0xC/+0x10), the
  intrusive links (+0x91=145 / +0x95=149), and the sync discriminator
  (0x20 / 14). NEW `AckEntryFieldLayoutGolden` pins the 10-byte AckEntry layout
  (+0 status / +1 slot / +2 ring i32 / +6 seq i32) and the 0/1/2 status codes.
- **Opcode jump-table slots / wire sizes.** `OpcodeSizeTableGolden` validates
  all 96 entries of ComputePacketSize against the recovered `kGoldenFixed[96]`
  table; the variable opcodes (0x16/0x17/0x18) and short/long sync (0x20→17/141)
  are pinned separately. The opcode→handler registration (RegisterApplyHandlers6)
  is pinned by `sim_command_apply6_test`.
- **Relation grid addressing (768 bytes/row, saturate-127, new-game packets).**
  Pinned by `sim_command_apply6_test` per `command-apply-relation.md`: row stride
  768 (kRelPersons), ClampRel [-127,127], modes 0/1/2/3/4 + unknown-mode no-op,
  the band-decay table, and the six mode-0/127 new-game packets through batch 6.
  Geometry matched to `world::g_relationMatrix` by static_assert in apply6.cpp.
- **Sync-range barrier (MarkSyncRangeStart/End + CheckSyncRangeAcked).**
  `command_recon_syncrange_test` (start/end = sendCount+1, u32 wraparound, range
  bracket, and round-trips through CheckSyncRangeAcked: empty/pending/acked/NAK).
- **Field-patch descriptor walk.** `SimCommand.Delta*` pins the
  [width][count][offset:2][values] triplet layout, new-minus-old deltas (widths
  1/2/4), the ApplyDelta decode round-trip, bad-width/overflow rejection, and the
  AI-method 5-byte-entry encoding.
- **Apply handler record field writes.** ExSellObjekt decode offsets
  (+20 src / +16 dst / HIWORD(+22) proto / +31 qty / +35 raw / +30 good /
  +26 curSellCmd) and the ack stamp (status 1, slot 3, seq=moved) pinned by
  `sim_command_apply6_test`.
- **Lockstep flush+exec.** `SendRingSequenceWraparound` runs the full
  enqueue→flush(standalone apply)→exec→recycle cycle over >32768 packets,
  pinning the (count & 0x7FFF) ring wrap while Count keeps rising. NEW
  `EnqueueRejectsWhenDisconnected` pins the dword_764CF0 early-out, and NEW
  `ExecLostCommandResyncsLastRequested` pins the ExecCommands sequence
  classification (the "Lost a Command" resync branch snapping last_req_count to
  the received Count + the status-2 ACK stamp). The networked dispatch order is
  pinned by `sim_command_e2e_test`; the fragment/reassembly pipeline
  (StagePendingBlock cap 0x3FE, 128-byte chunks, +12 Count chain, reassembly
  round-trip + buffer-overrun bound) by `sim_command_receive_test`.

## Confidence map

GOLDEN-PINNED (constants + control flow tested; high 1:1 confidence):
- ComputePacketSize (full 96-entry table + variable + sync).
- EnqueuePacket (header stamp, ring wrap, disconnected latch, sync-count latch).
- FlushSendQueue / StoreReceivedPacket / ExecCommands lockstep cycle, Exec
  sequence classification (in-order advance + lost-command resync).
- DeltaWriter / AiMethodWriter encode + ApplyDelta decode (field-patch walk).
- StagePendingBlock / GeneratePendingPackets / ReassembleReceived /
  CheckReassemblyComplete (fragment pipeline).
- MarkSyncRangeStart/End + CheckSyncRangeAcked (sync barrier).
- ExComputeObjectCoords relation matrix (all modes + band table + new-game
  packets), ExSellObjekt / ExComputeSellableAmount decode + ack.
- Packet geometry + AckEntry layout constants (NEW this wave).

UNDER-VERIFIED (reached on the flow; only indirectly pinned):
- ExecCommandGroup (0x4942c0) accept/reject gate stamping — exercised in
  `sim_command_receive_test` group cases; no standalone golden of the gate
  pre-pass accept-vs-reject opcode-stamp values. (Low risk: pure control flow.)
- ReceiveAndQueue (0x493ebc) — the transport pump contract is modeled through
  the net-transport hook; pinned by `sim_command_receive_e2e_test` but the
  standalone early-out (dword_764CE0==-1) leans on the hook's connected() shape.

NEEDS-LIVE-MCP (1:1 fidelity not confirmable from in-tree evidence; targets for
the binary-diff when MCP returns):
- 0x493034 ComputePacketSize — re-confirm the full switch byte-for-byte against
  the live decompile (the golden table is our recovered record, but a live diff
  is the only way to catch a missed case label).
- 0x494088 ExecCommands — the duplicate-sync and received-sync diagnostic
  branches are modeled as no-ops; confirm the original has no observable side
  effect there beyond the log string.
- 0x49818C ExComputeObjectCoords mode-3 trunc-toward-zero (Coord_ConvertX
  @0x5c6b08 frndint RC=chop) — the cast models it; a live diff of the x87 control
  word path would fully confirm the rounding mode on edge magnitudes.

## Build / test

Build green. Segment tests after the additions:
- `sim_command_test`: 310 checks (was 274; +36 from the geometry/ack/disconnect/
  resync pins), 0 failures.
- `command_recon_syncrange_test`: 21, `command_recon2_sync_test`: 42,
  `sim_command_apply6_test`: 113, `sim_command_e2e_test`: 50,
  `sim_command_receive_test`: 59 — all 0 failures.

No source edits (no drift found); all prior goldens left byte-identical.
