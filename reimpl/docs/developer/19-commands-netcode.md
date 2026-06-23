# 19 — Command dispatcher & netcode

> Provenance: reconstructed from the IDA Pro decompilation of `gilde.exe`
> (32-bit x86, imagebase `0x400000`). All addresses below are file/virtual
> addresses in that module. Function names use the project `VIBE_*` convention;
> the original symbols are the lower-case `cm_*` / `nt_*` names that survive in
> the binary's format strings (e.g. `cm_ExecCommand`, `cm_RequestSellObjekt`,
> `nt_Recv`).

## Summary

Die Gilde is a lockstep, deterministic-command-queue multiplayer engine. The
world is **never mutated directly** by local input. Instead every state change is
serialized into a fixed-format **command packet**, fed through a global ordered
queue (the `cm_*` system), shipped over a single TCP stream to the server
(`nt_*` / wsock32), and only applied to the simulation when it comes back through
the receive queue in **packet-count order**. Single-player is the same path with
the socket disabled (`cm_*` enqueue executes locally without a network hop).

This is the backbone of multiplayer sync, so the two invariants that the whole
file exists to preserve are:

1. **Deterministic ordering** — commands carry a monotonically increasing
   `Count` (sequence number). The receiver detects gaps ("Lost a Command") and
   the executor (`cm_ExecCommand`) replays commands strictly in queue order via a
   handler dispatch table. Every peer applies the same commands in the same order
   → identical world state.
2. **Exact wire layout** — packet field offsets and per-opcode sizes are
   hard-coded (`VIBE_Command_ComputePacketSize`). Any deviation desynchronizes
   the simulation. They are reproduced 1:1 below.

The command pump is driven from the per-frame loop
([14 — Per-frame loop](14-per-frame-loop.md)) — `VIBE_GameLogic_RunFrameLoop`
@`0x4c09a0` and the lighter `VIBE_Amt_RefreshGuildState` @`0x4becdc` both call the
trio `FlushSendQueue → ReceiveAndQueue → ExecCommands` once per iteration.

> **Platform boundary (Rule 6).** Networking is **kept**, not swapped: the
> original uses `wsock32` (`socket`/`connect`/`send`/`recv`/`select`/…). It is
> reconstructed 1:1 and routed through the `INetSocket` shim in the reimpl — the
> wire bytes, framing, blocking/`WSAEWOULDBLOCK` retry behaviour and teardown
> sequence are all preserved; only the concrete socket calls go through the
> interface. See the import table at the end of this doc.

---

## 1. Data structures & globals

### 1.1 The command record (153 bytes)

Both the send ring and the receive pool store fixed **153-byte** (`0x99`) records.
The first 18 bytes are a common header; the remainder is the opcode-specific
payload. `qmemcpy(..., 0x99)` is used throughout to copy a whole slot.

| Off  | Size | Field            | Meaning |
|------|------|------------------|---------|
| 0x00 | u8   | `Type`           | opcode (0–95); also reused as a per-slot **exec state** in the queue (1=done, 2=free/skip, 5=group-begin, 6=group-end, 7=reassembly-fragment) |
| 0x01 | u16  | `Length`         | total packet length in bytes (= `ComputePacketSize`) |
| 0x03 | u8   | `Channel`/flag   | fragment channel id, matched during reassembly |
| 0x04 | u32  | `OwnerId`/`CmdId`| originating entity / command id; `0xFFFFFFFF` = none/local |
| 0x08 | u32  | `Count`          | **sequence number** — the determinism key |
| 0x0C | u32  | `Group`/`FragId` | command-group / fragment-group link (0 = standalone) |
| 0x10 | …    | payload          | opcode-specific (see §4) |

The ring storage and link fields:

* **Send ring** — `unk_BAFB60` (`0xBAFB60`), `153 * slot` indexed, `slot =
  (cm_SendSeq+1) & 0x7FFF` → 32768 slots. Outbound list head `dword_11AA46C`
  (`cm_SendListHead`). Sequence counter `dword_11AA494` (`cm_SendSeq`).
* **Receive pool** — `byte_1078360` (`0x1078360`), also 153-byte stride, 0x2000
  (8192) slots, built as a free-list in `QueueInitAndSync`. Free head
  `dword_11AA49C` (`cm_RecvFreeHead`); received-and-ordered list head
  `dword_11AA498` (`cm_RecvListHead`).
* **Linked-list pointers inside each slot:** `+0x91` (145) = `prev`, `+0x95`
  (149) = `next`. Lists are intrusive doubly-linked.
* **Per-slot status byte array** — `byte_B5FB60` (`0xB5FB60`), 10-byte stride,
  0x8000 entries; `[0]` = status (0=pending, 1=acked-done, 2=free), `[+2..]` =
  back-reference seq. Queried by `VIBE_Command_GetPacketStatusById` @`0x4939d4`
  (returns `byte_B5FB60[10*id]`, or `-1` if `id >= 0x8000`).

### 1.2 Sequence / sync counters

| Global       | Addr        | Role |
|--------------|-------------|------|
| `cm_SendSeq` | `0x11AA494` | next outbound `Count` |
| `cm_RecvFreeHead` | `0x11AA49C` | free-list head of recv pool |
| `cm_RecvListHead` | `0x11AA498` | ordered received list |
| `cm_SendListHead` | `0x11AA46C` | pending outbound list |
| `cm_LastRequCount`| `0x11AA468` | last executed `Count` (exec-side gap detect) |
| `cm_LastSyncCount`| `0x11AA470` | `Count` of the last Sync packet (type 0x20,[16]=0x0E) |
| `nt_LastRequCount`| `0x7652F8`  | last received `Count` (recv-side gap detect) |
| `cm_PlayerId`     | `0x62EB38`  | local player id, returned by `QueueInitAndSync` |
| `nt_Socket`       | `0x764CE0`  | TCP socket fd; `-1` = single-player/disconnected |
| `nt_RxBuf`        | `0x764CE4`  | current receive scratch buffer ptr |
| `nt_TxBuf`        | `0x764CE8`  | current transmit slot ptr |
| `nt_RxCursor`     | `0x764CEC` (u16) | bytes received into current packet |
| `nt_TxCursor`     | `0x764CEE` (u16) | bytes sent of current packet |
| `nt_Disconnected` | `0x764CF0`  | set to 1 on socket teardown |

`nt_Socket == -1` is the **single-player switch**: `FlushSendQueue` and
`StoreReceivedPacket` short-circuit the wire and the queue runs purely in-memory.

---

## 2. The command lifecycle (per frame)

The frame loop calls these three, in this order:

```
VIBE_Command_FlushSendQueue   @0x4934cc   // drain outbound → socket (or loopback)
VIBE_Command_ReceiveAndQueue  @0x493ebc   // pull socket → ordered recv list
VIBE_Command_ExecCommands     @0x494088   // apply recv list in Count order
```

### 2.1 Local input → command (enqueue)

Gameplay code never touches the world; it calls a `cm_Request*` builder, which
fills a 153-byte stack record and hands it to:

**`VIBE_Command_EnqueuePacket` @`0x49388c`** (`cm_AddCommand`)
* Returns `-1` immediately if `nt_Disconnected` (`0x764CF0`) is set.
* Computes the next ring slot `v2 = (cm_SendSeq+1) & 0x7FFF`; back-pressure
  check: if multiplayer and that slot is the one currently being transmitted
  (`== nt_TxBuf`), returns `-1` (ring full — caller retries next frame; see the
  `while (… == -1) PumpMessages()` patterns in `VIBE_Net_StartNetworkGame`).
* `qmemcpy(slot, src, 0x99)`, bumps `cm_SendSeq`, computes `Length` via
  `ComputePacketSize`, writes header fields (`Channel=0`, `Group=0`,
  `Length`, slot index into `+4`, `Count=cm_SendSeq` into `+8`).
* If `Type==0x20 && payload[0]==0x0E` (a **Sync** packet) records
  `cm_LastSyncCount = Count`.
* Appends the slot to the tail of `cm_SendListHead` (intrusive list).
* If a pending **staged block** exists (`cm_PendingLen`, `0x11AA4A0`), calls
  `VIBE_Command_GeneratePendingPackets` to spill it into type-7 fragments.
* Returns the slot index (used later as the packet id for status polling).

Helper builders all funnel into `EnqueuePacket`:

| Builder | Addr | Emits opcode | Notes |
|---------|------|--------------|-------|
| `VIBE_Command_QueueInitAndSync` | `0x4931e0` | 3 (handshake) | resets the whole queue (see §2.4) |
| `VIBE_Command_QueueRequestPerm30` | `0x494a50` | 30 | guarded by `VIBE_GameTime_Compare` vs `qword_13CE852` |
| `VIBE_Command_QueueRequestState22` | `0x494750` | 22 | flushes the current delta block (`dword_11AA3E0` + 0x7C bytes) |
| `VIBE_Command_QueueRequestSlotReset28` | `0x4948c8` | 28 | zero-inits 8 dwords, stages a 0xF8-byte block, then enqueues |
| `VIBE_Command_QueueRequestArgs25` | `0x494810` | 25 | 5 dword args |
| `VIBE_Command_EnqueueCmd15` | `0x494604` | 15 | (id, ?, money, flag) |
| `VIBE_Command_QueueRequestFlagBlob32` | `0x494ab4` | 32 | byte sub-op at `+16` + optional 124-byte blob; sub-op `0x0E`=Sync, `6`/`9`/`16`/`18` used by net flow |
| `VIBE_Command_SetGameSpeed` | `0x493dec` | 32 sub-op 18 | clamps speed to [0,4] |

### 2.2 The delta-field encoder (how an entity action becomes a command)

Higher-level "do action X to entity Y" senders build a **delta packet** that
encodes only the *changed* fields of an entity record as signed deltas against
the baseline pointed to by `dword_11AA474` (`cm_BaselinePtr`):

* **`VIBE_Command_BeginDeltaPacket` @`0x493a94`** — resets the delta scratch
  (`dword_11AA3E0` header, `byte_11AA3E4` field-count=0, buffer `unk_11AA3E5`,
  `dword_11AA460` write-cursor=0), resolves the target entity via
  `VIBE_GameObject_ResolveEntityById`, and stores the baseline id into
  `cm_BaselinePtr`.
* **`VIBE_Command_AppendDeltaField` @`0x493aec`** — appends one field. Args
  `(width@al, count@dl, src@ecx, fieldOffset@bx)`. **Width must be 1, 2 or 4
  bytes** (else rejected). Layout per field in the scratch buffer:
  `[u8 width][u8 count][u16 fieldOffset][count × width bytes of delta]`. Each
  delta = `src[i] - baseline[fieldOffset + i]` at the matching width. Refuses to
  write past byte `0x77` (119) of the scratch (`width*count + cursor + 4 >=
  0x77`). Increments `byte_11AA3E4` (field count).
* **`VIBE_Command_QueueRequestState22` @`0x494750`** — copies the 124-byte
  (`0x7C`) delta scratch (`dword_11AA3E0` …) into a **type-22** packet and
  enqueues it. This is the packet that actually carries the entity mutation.

The matching **receiver** for a type-22 packet is
`VIBE_Command_ExPatchObjectFieldsAdd` @`0x497c18` (dispatch-table entry 22),
which re-resolves the entity (`-2/-3/-4` are indirections into
`dword_631288/63128C/631290`) and **adds** each stored delta back onto the live
field at the recorded offset and width — the exact inverse of `AppendDeltaField`.
This add-delta-onto-baseline scheme is what keeps every peer's entity records
bit-identical.

The `cm_RequestSellObjekt`-style entity-action senders (each handles a
"person panel" path and a "map entity" path):

| Sender | Addr | Builds | Calls |
|--------|------|--------|-------|
| `VIBE_Command_SendEntityActionA` | `0x5675ac` | state-118 + delta + type-22 | `SlotReset28`, `BeginDeltaPacket`, `AppendDeltaField(4,1,…)`, `QueueRequestState22` |
| `VIBE_Command_SendEntityActionB` | `0x567944` | two SlotReset28 (118/119) + delta + 22 + Args25(…,90,32,2,0) | + `QueueRequestArgs25` |
| `VIBE_Command_SendEntityActionC` | `0x567e24` | SlotReset28 (sub-op `-126`) | office-overview path |
| `VIBE_Command_SendEntityActionD` | `0x56801c` | SlotReset28 (sub-op `-126`) | office-overview path |
| `VIBE_Command_SendMapEntityAction` | `0x568278` | delta(1,1) + type-22 per matching map object | iterates the 411648-byte (×536) map-object table |

> The string at **`0x619998` = `"cm_RequestSellObjekt(%i, %i, %i, %i, %i, %i)"`**
> is the trace format of one such sender; the family of `cm_Request*`/
> `cm_RequestSell*` builders all reduce to `Begin/Append/QueueRequestState22`.

### 2.3 Flush outbound (`VIBE_Command_FlushSendQueue` @`0x4934cc`)

Walks `cm_SendListHead`. Two modes:

* **Single-player (`nt_Socket == -1`):** each pending slot is fed straight into
  `VIBE_Command_StoreReceivedPacket` — i.e. the command **loops back** into the
  receive list without any wire I/O. The list is consumed front-to-back.
* **Multiplayer:** transmits via `VIBE_Net_SendPacket` (see §3). It tracks the
  slot currently mid-transmission with `nt_TxBuf` (`0x764CE8`); if a partial send
  is in progress it returns `0` and resumes next frame, preserving order. On full
  send it advances `cm_SendListHead` to `+0x95` (next) and continues.

### 2.4 Receive (`VIBE_Command_ReceiveAndQueue` @`0x493ebc`)

* No-op if `nt_Socket == -1`.
* Drives `VIBE_Net_ReceivePacket` (§3) into the scratch buffer `byte_11AA4A4`
  (`nt_RxBuf` points at it), then for each fully-assembled packet calls
  `VIBE_Command_StoreReceivedPacket` to **insert it, in arrival order, at the
  tail of `cm_RecvListHead`**. Increments a received-counter `dword_631278`.

**`VIBE_Command_StoreReceivedPacket` @`0x493f80`** pulls a slot off the recv
free-list (`cm_RecvFreeHead`), `qmemcpy(0x99)`, recomputes `Length`, and links it
at the tail of `cm_RecvListHead`. If the free-list is exhausted it triggers
`VIBE_Net_Disconnect` (multiplayer) and returns 1 (overflow).

### 2.5 Fragment reassembly

Large commands are split into **type-7 fragments** (128-byte chunks) on send
(`GeneratePendingPackets`) and rebuilt on the receive side:

* **`VIBE_Command_CheckReassemblyComplete` @`0x4936e4`** — returns true only when
  all fragments of a group (`Channel` `+3` and `Group` `+0x0C`/`+8` matching, up
  to total size `u16 @+16`, 128 bytes per fragment, base 126) are present in the
  recv list.
* **`VIBE_Command_ReassembleReceived` @`0x49377c`** — concatenates the fragments
  into the linear staging buffers `unk_1077B60` (first 0x7C) + `unk_1077BDE…`
  (subsequent 0x80 chunks), clearing each fragment's `Type` to 0 as consumed, and
  records total length in `dword_11AA478`.

### 2.6 Execute (`VIBE_Command_ExecCommands` @`0x494088` — `cm_ExecCommand`)

This is the **deterministic apply** step and the heart of sync. Walking
`cm_RecvListHead` in order:

1. **Group framing.** A slot with `Type==5` opens a command group; `Type==6`
   closes it. `VIBE_Command_ExecCommandGroup` @`0x4942c0` runs the group's member
   handlers via the table `funcs_4942DD` (`0x631418`); if **any** member handler
   returns non-zero (failure) the whole group is rolled back by marking every
   member `Type=2` (skip), otherwise marked `Type=1` (done) — groups are atomic.
2. **Reassembly gate.** Skips fragments (`Type==7`); only dispatches a slot once
   `CheckReassemblyComplete` says its group is whole, then `ReassembleReceived`
   linearizes it.
3. **Sequence / gap detection.** Compares the slot's `Count (+8)` against
   `cm_LastRequCount` (`0x11AA468`):
   * `last+1 == Count` → in order, advance `cm_LastRequCount`.
   * `Count == cm_LastSyncCount` → expected Sync, logged
     `"cm_ExecCommand(): Received a Command with Count == cm_LastSyncCount …"`
     (`0x61bbfc`).
   * `Type==0x20 && payload[0]==0x0E` → Sync packet, logged
     `"cm_ExecCommand(): Received Sync …"` (`0x61bc68`).
   * otherwise → **gap**: logs
     `"cm_ExecCommand(): Lost a Command, cmd->Count is %li, cm_LastRequCount is %li"`
     (`0x61bcb0`) and resyncs `cm_LastRequCount = Count`. This is the desync
     alarm.
4. **Dispatch.** For `Type < 96` calls the handler:
   `funcs_4941F4[Type](slot@eax, scratch@edx)` — the table at **`0x631298`**
   (`funcs_4941F4`), 96 `__usercall` handler pointers indexed by opcode. The slot
   is then unlinked back onto the free-list (`UnlinkReceivedPacket` @`0x494028`).

The dispatch table (first entries, little-endian, from `get_bytes 0x631298`):

| Opcode | Handler addr | Opcode | Handler addr |
|-------:|--------------|-------:|--------------|
| 0  | `0x49644C` | 8  | `0x4964A4` |
| 1  | `0x496474` | 9  | `0x4964D8` |
| 2  | `0x496484` | 10 | `0x496520` |
| 3  | `0x49644C` | 11 | `0x496614` |
| 4  | `0x49648C` | 12 | `0x496714` |
| 5  | `0x49644C` | …  | … |
| 6  | `0x49644C` | 22 | `0x497C18` (`ExPatchObjectFieldsAdd`) |
| 7  | `0x49644C` | …  | … |

(Opcodes 0/3/5/6/7 share the no-op/framing stub `0x49644C`; the full 96-entry
table is the array at `0x631298`.)

### 2.7 Queue reset / handshake (`VIBE_Command_QueueInitAndSync` @`0x4931e0`)

Run at session entry. It (a) rebuilds the 8192-slot recv free-list
(`byte_1078360`), (b) clears the send/recv list heads and counters, (c)
re-initializes the 0x8000-entry status array `byte_B5FB60` (status=1, seq=-1),
(d) enqueues a **type-3 handshake** packet with a magic constant
(`v9 = -1288263715` = `0xB3402EDD`) and blocks in
`VIBE_Command_WaitForPacketType(3, …)` @`0x493f34` until the server echoes it
back with the matching magic at payload `+4`; mismatch → `VIBE_Net_Disconnect`.
Finally sets `cm_PlayerId` (`0x62EB38`) and returns it.

`VIBE_Command_WaitForPacketType` is the synchronous wait primitive — it spins
`FlushSendQueue → ReceiveAndQueue`, scanning `cm_RecvListHead` for a matching
`Type`, with a deadline derived from `cm_PlayerId`-relative time.

---

## 3. Networking layer (wsock32, 1:1)

The wire is a **single blocking-but-non-blocking TCP stream** (the socket is put
in non-blocking mode, code retries on `WSAEWOULDBLOCK` = 10035). Framing is the
153-byte command header's own `Length` field — the receiver reads a 3-byte prefix
(`Type` + `u16 Length`), then the remaining `Length-3` bytes.

### 3.1 Connect (`VIBE_Net_ConnectToServer` @`0x43b51c`)

`(host@eax, port@edx)`:
1. `WSAStartup(2.0)`, `socket(AF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6)`.
2. `setsockopt` sequence — `SO_REUSEADDR(4)=1`, `SO_DONTLINGER(-129)=1`,
   `SO_RCVBUF(4098)=0x40000`, `SO_SNDBUF(4097)=0x20000`; then
   `ioctlsocket(FIONBIO=0x8004667E)=1` → **non-blocking**.
3. Address: `inet_addr(host)`, falling back to `gethostbyname`; `htons(port)`.
4. `connect`, then `select(writefds, timeout=500ms)` to confirm the (non-blocking)
   connect completed. On any failure → `shutdown/closesocket/WSACleanup` and
   return `-1`.
5. On success stores the fd in `nt_Socket` (`0x764CE0`) and returns it.

### 3.2 UDP broadcast discovery (`VIBE_Net_OpenBroadcastSocket` @`0x43aac0`)

Lazily creates a `socket(AF_INET, SOCK_DGRAM=2, 0)` (global `s` @`0x62E5C0`),
`setsockopt(SO_BROADCAST=32)=1`, `bind` to `INADDR_ANY:0`, and pre-fills a
`sockaddr` `to` (`0x764CD0`) = `255.255.255.255:port` for LAN game-discovery
broadcasts (`sendto`/`recvfrom`).

### 3.3 Wire send (`VIBE_Net_SendPacket` @`0x43bc54` — internal `nt_Send`)

* No-op if `nt_Socket==-1` or `nt_TxBuf==0`.
* `send(socket, nt_TxBuf + nt_TxCursor, Length - nt_TxCursor, 0)` where
  `Length = *(u16*)(nt_TxBuf+1)`.
* **Partial-send aware:** advances `nt_TxCursor` (`0x764CEE`); returns `0`
  (not done) until `cursor == Length`, then accounts stats
  (`VIBE_Net_AccumulatePacketStats(...,0)`), clears `nt_TxBuf`/`nt_TxCursor`.
* `WSAEWOULDBLOCK` → return 0 (retry next frame). Other error →
  `shutdown/closesocket/WSACleanup`, `nt_Disconnected=1`, `nt_Socket=-1`.

### 3.4 Wire receive (`VIBE_Net_ReceivePacket` @`0x43b8d0` — internal `nt_Recv`)

Two-stage read into `nt_RxBuf` (`0x764CE4`), cursor `nt_RxCursor` (`0x764CEC`):

1. **Header:** while `nt_RxCursor < 3`, `recv` up to `3 - cursor` bytes (the
   `Type` + `u16 Length`). Bail to caller until the 3-byte header is complete.
2. **Body:** `recv(buf + cursor, Length - cursor)` where
   `Length = *(u16*)(buf+1)`. Returns `0` until `cursor == Length`.
3. On a complete packet, if `OwnerId(+4) != -1`, runs the **same gap detector**
   as the executor but against `nt_LastRequCount` (`0x7652F8`):
   `last+1==Count` advance; `Count==cm_LastSyncCount` → `"nt_Recv(): Received a
   Command with Count == cm_LastSyncCount …"` (`0x616714`);
   `Type==0x20 && [16]==0x0E` → `"nt_Recv(): Received Sync …"` (`0x616778`);
   else `"nt_Recv(): Lost a Command …"` (`0x6167b8`) + resync. Then stats-accounts
   the packet and clears the cursor for the next frame.
4. `recv()==0` → peer closed → `shutdown/closesocket/WSACleanup`,
   `nt_Disconnected=1`. `WSAEWOULDBLOCK` → return 0.

So the **wire protocol = a contiguous stream of 153-byte-max command records**,
self-delimited by the `u16 Length` at offset 1; ordering is guaranteed by TCP and
re-checked by the `Count` field on both ends.

### 3.5 Disconnect (`VIBE_Net_Disconnect` @`0x43b868`)

`shutdown(SD_BOTH=2)`, `closesocket`, `nt_Socket=-1`, `nt_Disconnected=1`,
`WSACleanup`, clears the started flag `dword_62E5C4`. Called on protocol error,
recv-pool overflow, or handshake mismatch.

### 3.6 Network game bring-up & sync barriers

* **`VIBE_Net_StartNetworkGame` @`0x503f78`** — the host/client session bootstrap:
  shows the lobby progress form, (host) ships the savegame to clients
  (`VIBE_Net_SendSaveGameToClients`) / (client) loads the received stream
  (`VIBE_Net_LoadReceivedSaveStream`), reads player name/family from `game.INI`
  (`[Network] Name`/`Familienname`), loads `scenes/*network.ed3`, then drives a
  series of **enqueue-then-poll** barriers using `GetPacketStatusById` and the
  flag-blob commands (sub-ops 6, 9, 16, 18) interleaved with
  `RunSyncWaitLoop`. Game speed is finally set via `EnqueueCmd15` + command 32/18.
* **`VIBE_Net_RunSyncWaitLoop` @`0x4beac8`** — enqueues a `cm_Request*` **type-32
  sub-op 14** ("`sv_NetworkSync`") barrier packet and busy-runs the frame loop
  (`VIBE_GameLogic_RunFrameLoop`) until `GetPacketStatusById` reports the server
  acknowledged it — a global lockstep rendezvous all peers wait on.
* **`VIBE_Net_RunWaitLoop` @`0x4bec44`** — same barrier pattern (type-32 sub-op
  14) with a status string, used during mid-session syncs.
* **`VIBE_Net_LoadAndSyncSession` @`0x56da74`** — late-join / scenario reload:
  counts guild masters, CRC32s the loaded header (`VIBE_Util_Crc32`), sends a
  **type-32 sub-op 16** packet carrying `{playerId, crc}` so the server can verify
  every client loaded an identical world (CRC mismatch = hard desync), writes
  `Gamedata\network\%s.SAV/.SRV`, and waits via `RunWaitLoopWithStatus`.

---

## 4. Packet sizes by opcode (`VIBE_Command_ComputePacketSize` @`0x493034`)

`Length` is **derived from the opcode**, not stored by the sender — the table
below is authoritative for the wire size of each command. Two opcodes are
variable-length. Default (unknown opcode) = **145**.

| Opcode(s) (hex)                                                                 | Length |
|---------------------------------------------------------------------------------|-------:|
| 03, 08, 1C, 28, 31, 3B, 42, 43, 4A, 4D, 57, 58                                  | 20 |
| 04                                                                              | 17 |
| 0A                                                                              | 80 |
| 0B                                                                              | 42 |
| 0C                                                                              | 73 |
| 0D, 21, 23, 24, 26, 27, 29, 2A, 33, 39, 3A, 5A                                  | 24 |
| 0F, 2C                                                                          | 33 |
| 11, 3D                                                                          | 39 |
| 12, 1E                                                                          | 30 |
| 13, 1A, 25, 2B, 34, 36, 38, 47, 52, 54, 5B, 5D                                  | 28 |
| 14                                                                              | 22 |
| 15                                                                              | 57 |
| 16, 17                                                                          | **variable**: `21 + Σ_i (payload[+20+…].count * .width + 4)` over `payload[+20]` groups |
| 18                                                                              | `21 + 5 * payload[+20]` |
| 19, 53                                                                          | 36 |
| 1B                                                                              | 40 |
| 1D                                                                              | 95 |
| 20                                                                              | `payload[+16]==0x0E ? 17 : 141` (Sync=17, else flag-blob=141) |
| 22                                                                              | 61 |
| 2D, 45                                                                          | 31 |
| 2E, 3C                                                                          | 32 |
| 2F, 30                                                                          | 60 |
| 32                                                                              | 48 |
| 35                                                                              | 93 |
| 37                                                                              | 69 |
| 3E, 4F, 5E                                                                      | 52 |
| 40                                                                              | 47 |
| 41                                                                              | 74 |
| 44                                                                              | 23 |
| 46                                                                              | 25 |
| 48                                                                              | 21 |
| 49                                                                              | 55 |
| 4B                                                                              | 144 |
| 4C                                                                              | 53 |
| 4E                                                                              | 64 |
| 50, 51                                                                          | 68 |
| 55                                                                              | 97 |
| 56                                                                              | 56 |
| 5C                                                                              | 26 |
| default                                                                         | 145 |

### Selected payload layouts

**Type 22 — entity-state delta** (`QueueRequestState22`, len 61). Header (18) +
124-byte delta scratch copied from `dword_11AA3E0`:
```
+0x00 Type=22
+0x0E header field cm_BaselineId  (dword_11AA3E0)
+0x10 (payload) byte fieldCount (byte_11AA3E4)
+0x11 fields[]: each = [u8 width][u8 count][u16 fieldOffset][count*width signed delta]
```
(Note `ComputePacketSize` reports 61, but the staged scratch copied is 124 bytes;
the on-wire `Length` is what the field encoder actually filled.)

**Type 32 — flag/blob command** (`QueueRequestFlagBlob32`, len 17 or 141):
```
+0x00 Type=32
+0x10 u8  subOp        (0x0E=Sync→len 17; 6,9,16,18 used by net flow→len 141)
+0x11 124-byte blob    (present only when subOp != 0x0E)
```
Sub-op `18` = set game speed (clamped 0–4); `16` = CRC handshake `{playerId,crc}`;
`6`/`9` = lobby progress flags.

**Type 25 — args** (`QueueRequestArgs25`, len 36): `+0x10` five dwords
`(a1,a2,a3,a4,a5)`.

**Type 28 — slot reset** (`QueueRequestSlotReset28`, len variable per table for
0x1C → 20… but it also stages a 0xF8 block via `StagePendingBlock`): zero-inits 8
dwords in a local struct, sets any zero entry `[+14]` to `-1`, stages the block,
enqueues.

**Type 15** (`EnqueueCmd15`, len 57): `+0x10 dword a1`, `+0x14 dword a2`,
`+0x1C dword a3`, `+0x18 u8 a4`.

**Type 30** (`QueueRequestPerm30`, len 60): packs a `GameTime` quad
(`+0x10 dword, +0x14 dword, +0x18 dword, +0x1C u16`) — gated on
`VIBE_GameTime_Compare` vs `qword_13CE852` ([15 — Game time](15-game-time-tick.md)).

---

## 5. Determinism checklist (why this is 1:1-critical)

* **Every** world mutation goes through `EnqueuePacket` → wire → `ExecCommands`.
  Do not let reimplemented gameplay touch entity records directly.
* `Count` is assigned at enqueue (`cm_SendSeq`) and is the sole ordering key.
  Executor advances `cm_LastRequCount` by exactly 1 per command; a skip raises
  "Lost a Command".
* `ComputePacketSize` is the canonical length oracle — reproduce the table byte-
  for-byte; a wrong size shifts every subsequent packet in the TCP stream.
* Delta fields are *signed deltas vs baseline* applied with `+=` on receive
  (`ExPatchObjectFieldsAdd`); width must be ∈{1,2,4}; truncation/overflow wraps
  per width exactly as the original `char/__int16/int` arithmetic.
* Command groups (`Type 5..6`) are atomic — all-or-nothing apply.
* Sync barriers (type-32 sub-op 14) and CRC checks (sub-op 16) are the explicit
  desync tripwires; keep them.

---

## Reimplementation notes (shim boundary)

* All wsock32 calls (`socket/connect/bind/send/recv/sendto/recvfrom/select/
  setsockopt/ioctlsocket/shutdown/closesocket/WSAStartup/WSACleanup/
  WSAGetLastError/htons/htonl/inet_addr/gethostbyname`) route through
  **`INetSocket`** in `src/shim_impl/` — the only Rule-6-approved network keep.
  Preserve non-blocking semantics, `WSAEWOULDBLOCK` retry, partial send/recv, and
  the teardown order (`shutdown(2)`→`closesocket`→`WSACleanup`).
* The command queue itself (`cm_*`) is **pure logic** — no platform calls — so it
  is reconstructed verbatim in portable C++ and is fully unit-testable with golden
  vectors (enqueue → expected 153-byte bytes → exec → expected entity deltas).

---

## Cross-references

* [12 — Session init / world load](12-session-init-worldload.md) — where
  `QueueInitAndSync` and the network bring-up are invoked.
* [14 — Per-frame loop](14-per-frame-loop.md) — the frame that pumps
  Flush/Receive/Exec.
* [16 — Characters / persons](16-characters-persons.md) — the entity records that
  type-22 deltas mutate.
* [20 — Buildings / city](20-buildings-city.md) — map-object table touched by
  `SendMapEntityAction`.
* [22 — Script engine](22-script-engine.md) — script-issued commands enter the
  same queue.

### Function index

| Function | Addr |
|----------|------|
| `VIBE_Command_ReceiveAndQueue` | `0x493ebc` |
| `VIBE_Command_ExecCommands` (`cm_ExecCommand`) | `0x494088` |
| `VIBE_Command_FlushSendQueue` | `0x4934cc` |
| `VIBE_Command_EnqueuePacket` (`cm_AddCommand`) | `0x49388c` |
| `VIBE_Command_StoreReceivedPacket` | `0x493f80` |
| `VIBE_Command_UnlinkReceivedPacket` | `0x494028` |
| `VIBE_Command_ExecCommandGroup` | `0x4942c0` |
| `VIBE_Command_CheckReassemblyComplete` | `0x4936e4` |
| `VIBE_Command_ReassembleReceived` | `0x49377c` |
| `VIBE_Command_GeneratePendingPackets` | `0x493584` |
| `VIBE_Command_StagePendingBlock` | `0x49436c` |
| `VIBE_Command_ComputePacketSize` | `0x493034` |
| `VIBE_Command_WaitForPacketType` | `0x493f34` |
| `VIBE_Command_QueueInitAndSync` | `0x4931e0` |
| `VIBE_Command_BeginDeltaPacket` | `0x493a94` |
| `VIBE_Command_AppendDeltaField` | `0x493aec` |
| `VIBE_Command_QueueRequestState22` | `0x494750` |
| `VIBE_Command_QueueRequestSlotReset28` | `0x4948c8` |
| `VIBE_Command_QueueRequestArgs25` | `0x494810` |
| `VIBE_Command_QueueRequestPerm30` | `0x494a50` |
| `VIBE_Command_QueueRequestFlagBlob32` | `0x494ab4` |
| `VIBE_Command_EnqueueCmd15` | `0x494604` |
| `VIBE_Command_SetGameSpeed` | `0x493dec` |
| `VIBE_Command_GetPacketStatusById` | `0x4939d4` |
| `VIBE_Command_ExPatchObjectFieldsAdd` (type-22 recv) | `0x497c18` |
| `VIBE_Command_SendEntityActionA…D` | `0x5675ac`/`0x567944`/`0x567e24`/`0x56801c` |
| `VIBE_Command_SendMapEntityAction` | `0x568278` |
| `VIBE_Net_ConnectToServer` | `0x43b51c` |
| `VIBE_Net_OpenBroadcastSocket` | `0x43aac0` |
| `VIBE_Net_SendPacket` (`nt_Send`) | `0x43bc54` |
| `VIBE_Net_ReceivePacket` (`nt_Recv`) | `0x43b8d0` |
| `VIBE_Net_Disconnect` | `0x43b868` |
| `VIBE_Net_StartNetworkGame` | `0x503f78` |
| `VIBE_Net_RunSyncWaitLoop` | `0x4beac8` |
| `VIBE_Net_RunWaitLoop` | `0x4bec44` |
| `VIBE_Net_LoadAndSyncSession` | `0x56da74` |
| Dispatch table `funcs_4941F4` (96 handlers) | `0x631298` |

### wsock32 imports (kept, routed via `INetSocket`)

`socket@0x60e4d8`, `connect@0x60e494`, `bind@0x60e4c8`, `send@0x60e484`,
`recv@0x60e488`, `sendto@0x60e4c0`, `recvfrom@0x60e4b4`, `select@0x60e490`,
`setsockopt@0x60e4d4`, `getsockopt@0x60e48c`, `ioctlsocket@0x60e4b8`,
`shutdown@0x60e49c`, `closesocket@0x60e4c4`, `WSAStartup@0x60e4bc`,
`WSACleanup@0x60e4a8`, `WSAGetLastError@0x60e4ac`, `htons@0x60e4cc`,
`htonl@0x60e4d0`, `inet_addr@0x60e498`, `inet_ntoa@0x60e4b0`,
`gethostbyname@0x60e4a0`, `gethostname@0x60e4a4`.
