# server.dll — Reverse Engineering Report (single source of truth)

**Game:** Europa 1400 / Die Gilde (The Guild), Russobit-M v1.03. Dedicated multiplayer game server,
shipped as a 32-bit DLL loaded by `gilde.exe`. Open Watcom C/C++, `__watcall` (EAX/EDX/EBX).
Authoritative TCP server, max 16 clients, UDP LAN discovery. Listen port from `server.ini`
`[Config]Port` (default 7531).

> Status: **function naming COMPLETE.** All 947 functions named — 531 `VIBE_*` (renamed this
> session), 406 pre-existing Watcom/CRT library names, 5 `nullsub` stubs, 5 `j_` thunks.
> `sub_`=0, `unknown_`=0. IDB saved. Findings verified by decompilation + xref/call-graph + strings.

## Documents
- [network/network.md](network/network.md) — TCP/UDP stack, sockets, packet schema, queues, RX/TX, state machine, slot layout, command-size table.
- [gamelogic/command-handlers.md](gamelogic/command-handlers.md) — full opcode→handler map (jump table @0x439b60), reply-record format, broadcast/reply semantics.
- [gamelogic/tick-and-data-model.md](gamelogic/tick-and-data-model.md) — per-tick simulation pipeline, world-state deserialization, object/building/office/city/market data model, lifecycle, client/server model.
- [core/runtime-vfs-zlib.md](core/runtime-vfs-zlib.md) — zlib 1.1.3, minizip, VFS, memory, text/config compiler, logging, Watcom CRT/DLL init.

(Legacy flat notes `../report.md` and `../analyze.md` predate this folder; this `report/` tree is now
the canonical, corrected source. Key correction vs legacy: the true command dispatcher is
`VIBE_DispatchGameCommands` (0x428cfc) via jump table 0x439b60 — not 0x428b6c, which only enqueues.)

## End-to-end multiplayer architecture
```
gilde.exe --Init_(ctx)--> CreateThread(VIBE_ServerThreadMain)
 VIBE_ServerThreadMain (0x427700):
   read server.ini (Port/NumPlayers/GamePath)
   VIBE_ServerInit -> VIBE_LoadDataModel + VIBE_CreateListenSocket(TCP,nonblock,256KB bufs)
   LOBBY: loop VIBE_AcceptConnection + VIBE_SendLanBroadcast(UDP 255.255.255.255) until NumPlayers
   GAME LOOP @30ms:
     VIBE_GameTick           # inflate world state, run sim read-back, relink graph
     VIBE_ServiceClientsRecv # recv per client, run handshake/load state machine, enqueue cmds
     VIBE_ServiceClientsSend  # flush outgoing queues
     VIBE_DispatchGameCommands # run VIBE_g_CommandHandlerTable[cmd]; broadcast/reply
   shutdown: VIBE_NetErrorHandler / VIBE_ServerShutdown ; Exit_ sets VIBE_g_ShutdownRequest
```

## Command → network flow
client TCP packet `[cmd][len][payload]` → `VIBE_RecvClientData` accumulates →
`VIBE_ServiceClientsRecv` state machine → `VIBE_EnqueueIncomingPacket` (per-slot queue) →
`VIBE_DispatchGameCommands` → `VIBE_g_CommandHandlerTable[cmd]()` (mutates authoritative state via
`VIBE_ResolveEntityId` + `VIBE_QueryObjectField`/`VIBE_QueryBuildingField` + allocators) →
return 0 ⇒ `VIBE_EnqueueOutgoingPacket` to **all** clients; ≠0 ⇒ error reply to originator →
`VIBE_ServiceClientsSend` → `VIBE_SendClientData` → wire.

## Server call catalog (by purpose)
- **Lifecycle:** Init_(0x427678), Exit_(0x4276b0), VIBE_ServerThreadMain, VIBE_ServerInit, VIBE_ServerShutdown.
- **Sockets:** VIBE_CreateListenSocket, VIBE_AcceptConnection, VIBE_CloseClientConnection, VIBE_SendLanBroadcast.
- **RX/TX:** VIBE_RecvClientData, VIBE_SendClientData, VIBE_ServiceClientsRecv, VIBE_ServiceClientsSend,
  VIBE_EnqueueIncomingPacket, VIBE_EnqueueOutgoingPacket, VIBE_DequeueOutgoingPacket, VIBE_GetCommandDataSize, VIBE_NetErrorHandler.
- **Dispatch:** VIBE_DispatchGameCommands + 96-entry handler table (VIBE_Cmd_*).
- **Simulation:** VIBE_GameTick + VIBE_Read{Header,WorldObjects,Buildings,CityState,Characters,Player}State,
  VIBE_SyncWorldObjects, VIBE_ReadStateField.
- **Data model:** VIBE_ResolveEntityId, VIBE_Query{Object,Building}Field, alloc/create/destroy object/building/office/city.
- **Load/compress:** VIBE_AllocLoadBuffer, VIBE_vfs_OpenMemory, VIBE_zlib_inflate, VIBE_ReadStreamBytes.

## Protocol / packet summary
Wire: `[cmd:1][total_len:2 LE incl header][payload]`, ≤153 B, plaintext, no magic/checksum/encryption.
Queue node 153 B: `[cmd][size:2][sender:1][target:4][payload:137][prev:4][next:4]`.
State machine: accept(0x103) → handshake echo cmd3 (→PostAuth 0x05) → host-driven Init(0x08) →
LoadStart/LoadChunk world push → Playing → dispatch. 5-min idle timeout. Full tables in network.md.

## Unresolved gaps / next targets
The naming objective is complete; the following are *semantic* deep-dives, not blockers:
1. **Init_ context struct** — the EAX ctx pointer from gilde.exe (`v77[53]` in VIBE_ServerThreadMain;
   `v77[1]` mode 1/4, `HIBYTE(v77[19])` player count). Exact field layout of this host↔DLL config
   block is unconfirmed (needs the client/gilde.exe side or a runtime capture).
2. **Field-level payload schemas per opcode** — handler roles and sizes are known; the precise
   per-field meaning of each command's payload bytes is only partially mapped (M-confidence handlers
   in command-handlers.md, e.g. FourCC slot-ops 0x53/0x54, building variant 0x49).
3. **Savegame version ladder** — version gate constants (0x10013–0x10045) are catalogued but the exact
   field added at each version is not exhaustively enumerated.
4. **Per-client state buffer `unk_127D7A0`** (cmd 0x20 / payload[0x10]==0x0E path) — purpose of this
   per-slot copy vs broadcast is identified but its consumer is not fully traced.

No functions remain unidentified; revisit the above only with client-side artifacts or runtime packet captures.
