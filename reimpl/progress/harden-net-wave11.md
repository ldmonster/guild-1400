# Wave-11 hardening — net cluster (W11-NET)

Cluster: `src/net/*.{h,cpp}` (transport, packet/message parse, session/lobby
protocol, savegame transfer, discovery, serialization) + their tests.
Method: ASAN+UBSAN build (`-fsanitize=address,undefined -fno-sanitize-recover=all`),
add malformed/truncated/oversized/empty-input tests, fix OOB/UB/leaks with faithful
guards only. MCP was DOWN — no new 1:1 reconstruction; behavioral items flagged.

Build dirs used (dedicated, to avoid colliding with other agents):
`build-w11net` (ASAN), `build-w11net-rel` (normal Release).

## Audit summary

Net is a thin, mostly-defensive layer. Most parsers already bounds-check before
the read (discovery accept predicate guards `buf.size() < 6` then reads `buf[0..5]`;
the blob copies clamp `n > buf.size()`; `DecodeLobbyAdvert` size-guards every field;
`HandleLoadBufAppend` already had a per-byte `cursor+i < buf.size()` bound;
`AccumulatePacketStats` guards `type < 96`; `CommandPacket` is a fixed 153-byte
array so the `+0x10` payload accesses are statically in bounds). Two genuine
memory-safety defects existed, both found/confirmed under ASAN, plus a test bug.

## Bugs found & fixed (all FAITHFUL — in-bounds/valid path byte-identical)

### 1. Transport `ReceivePacket` stage-2: OOB write on a corrupt on-wire length
`src/net/transport.cpp` (VIBE_Net_ReceivePacket @0x43b8d0).
Stage 2 did `recv(rx_buf_ + rx_cursor_, total - rx_cursor_)` where
`total = HdrLen(rx_buf_)` is a u16 read straight from the **untrusted peer**.
- If the peer declares `total < 3` (a frame smaller than its own 3-byte header),
  `total - rx_cursor_` (rx_cursor_ == 3 here) underflows to ~65533 → `recv` writes
  up to 64 KB **past** the reassembly buffer.
- If `total` exceeds the buffer capacity, the body read overruns the buffer.
The original reads into a fixed 153-byte global record (`kPacketStride`), so a
**valid** frame is always in `[3, cap]`; a frame outside that range is corrupt.
Fix: added `rx_cap_` (set via `SetRecvBuffer(buf, cap)`, default 0 == "unknown")
and a malformed-frame guard before the stage-2 recv:
`if (total < kHeaderBytes || (rx_cap_ && total > rx_cap_)) { Teardown(); return Closed; }`
plus a `rx_cap_ < kHeaderBytes` guard before stage 1. The `total < 3` check is
structural and never fires on a valid frame; `total > cap` matches the engine's own
fixed-buffer envelope. Files: `src/net/transport.h`, `src/net/transport.cpp`.
Pinned by tests `RecvDeclaredLengthBelowHeaderUnderflowGuard`,
`RecvZeroDeclaredLengthGuard`, `RecvDeclaredLengthExceedsBufferGuard`,
`RecvMaxValidFrameAtCapacityStillCompletes`, `RecvZeroLengthReadIsWouldBlock`,
`RecvTruncatedBodyNeverCompletes`, `RecvHeaderOnlyWaitsForBody` (net_test.cpp).

### 2. `LoadReceivedSaveStream`: defensive clamp of declared total vs buffer size
`src/net/net_savegame.cpp` (VIBE_Net_LoadReceivedSaveStream @0x5ac140).
`result.assign(buf.begin(), buf.begin() + r.total)` would read past `buf` if
`total` (declared in a remote opcode-8 packet) ever exceeded `buf.size()`. Normally
the alloc sizes `buf` to `total`, so `total == buf.size()`; the clamp makes a
desynced/hostile `total` fail safe (copies `min(total, buf.size())`). Valid path
(total == buf.size()) is unchanged. Pinned by `LoadClampsTotalToBufferSize`.

### 3. TEST BUG (own cluster): use-after-return in net_session_test
`tests/unit/net_session_test.cpp`. ASAN flagged a **stack-use-after-return** on the
clean baseline: the capturing error-log sink stashed the raw `message` pointer,
which (for the unknown-winsock-code path) points into `ReportWinsockError`'s local
`char[256]`. That buffer dies when the function returns; the test then read it.
The production code is faithful — the original consumes the diagnostic string
synchronously inside the call (the sink runs before return). Fix: the test sink now
**copies** the message contents into a stable buffer (`char msg[256]`). No
production change. This was a real latent UAR the wave's ASAN pass exposed.

## Malformed-input tests added

- `tests/unit/net_test.cpp` (+7 transport tests, listed under bug #1): header-only,
  truncated body, 0-length read, declared len < header (underflow), len == 0,
  len > buffer, max-valid frame at capacity.
- `tests/unit/net_savegame_test.cpp` (NEW, 10 tests): opcode-9 before opcode-8
  (no-op), 0-length declared total, over-append beyond buffer, non-aligned tail
  clamp, incomplete stream -> empty, the load clamp, pad-length goldens, empty-save
  send, happy-path reassembly.

Existing malformed coverage kept and confirmed green: `net_discovery_test`
(RejectsBadHeaderBytes, MaxServersZeroReturnsMinusTwo, too-short advert),
`net_session_test` (UnknownBoundaryCodes, ClassifyToleratesNullBuffer),
`net_lobby_test` (DecodeRejectsBadHeader, NameClampedIntoRecord).

## Test results (ASAN+UBSAN, build-w11net)

| target | checks | result |
|---|---|---|
| net_test | 494 | pass |
| net_savegame_test | 523 | pass |
| net_discovery_test | 90 | pass |
| net_lobby_test | 38 | pass |
| net_session_test | 80 | pass (was: ABORT on baseline UAR) |
| net_recon3_loadsync_test | 61 | pass |
| net_lobby_itest (integration) | 88 | pass |
| net_lobby_e2e_test (e2e) | 2 | pass |
| gui_netfile_run_test | 28 | pass |

Normal Release build (build-w11net-rel): all six net unit targets build + pass.

## BEHAVIORAL — needs MCP (not changed)

- `SaveStreamReassembler::HandleLoadBufAlloc` does `buf.assign(total, 0)` with
  `total` read from a remote opcode-8 packet (VIBE_Command_HandleLoadBufAlloc
  @0x4964a4). A hostile `total` near `0xFFFFFFFF` triggers a multi-GB allocation
  (std::bad_alloc -> terminate). The original `alloc(total)` would likewise fail on
  a huge length, so this is the engine's own envelope on degenerate input, NOT an
  OOB. Whether the original clamps/validates `total` (e.g. against a max savegame
  size) before allocating is a 1:1 question — needs the IDB to confirm. Documented,
  not changed.

## For OTHER cluster owners (do not edit from net)

- **sim/ owner — `src/sim/command_receive.cpp:82-103` (`ReceiveDriver::ReceiveAndQueue`):**
  it receives the recv-buffer capacity as `rxBufLen` but `(void)`s it and arms the
  transport with `net.SetRecvBuffer(rxBuf)` (cap defaults to 0). The transport's new
  buffer-capacity guard (#1) only fully engages when the capacity is passed. Please
  change both `SetRecvBuffer(rxBuf)` calls (lines ~89 and ~100) to
  `SetRecvBuffer(rxBuf, static_cast<u16>(rxBufLen))` so a frame declaring a length in
  (153, 65535] is rejected instead of overrunning the 153-byte command record. The
  structural `total < 3` underflow guard already protects the live path regardless.

- **render/ owner — `src/render/shape_convert16.cpp:140` (`ShapeConvertRgbTo16`):**
  ASAN reports a **stack-buffer-overflow** (READ size 1) reached from
  `tests/unit/wire_worldnet_test.cpp:91` (`InstalledLeaves9ConvertersAreTheRealOnes`),
  which feeds a 64-byte stack `shape` buffer (line 80) to `HookConvertRgbTo16`. The
  converter reads past the buffer. This is wholly in the render cluster (no net code
  involved); not touched here. The net assertions in wire_worldnet_test are
  unaffected by net changes.
