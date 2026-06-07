# Network Layer — DieGilde server.dll (VERIFIED)

Game: **Europa 1400 – Die Gilde / The Guild** (Russobit-M v1.03). 32-bit DLL, Open Watcom
(`__watcall`: EAX/EDX/EBX). TCP authoritative game server + UDP LAN discovery. Max 16 clients.
Listen port from `server.ini` `[Config]Port` (default 7531).

All functions below were decompiled and verified this session, then renamed `VIBE_*`.

## Entry points
| Addr | Name | Role |
|------|------|------|
| 0x427678 | `Init_` (export ord.1) | Spawns server thread `CreateThread(VIBE_ServerThreadMain, ctx)`. ctx ptr arrives in EAX, forwarded as lpThreadParameter. Thread handle -> `dword_365B6D8`. |
| 0x4276b0 | `Exit_` (export ord.2) | Sets `VIBE_g_ShutdownRequest`(0x43d970)=1, waits for thread to clear it, `TerminateThread`+`CloseHandle`. |
| 0x42b3e4 | `DllEntryPoint` (ord.4371428) | Watcom CRT DLL init. |

## Main thread — `VIBE_ServerThreadMain` (0x427700)
1. `GetModuleFileNameA` -> derive `<dir>\server.ini`.
2. `GetPrivateProfileIntA([Config]Port,7531)` -> `VIBE_g_ServerPort`; `([Config]NumPlayers,1)` -> `VIBE_g_NumPlayers`; `([General]GamePath)` -> `ReturnedString`.
3. lpThreadParameter copied to local `v77[53]`; `v77[1]` selects mode (1 or 4) and pulls player count from `HIBYTE(v77[19])`.
4. `VIBE_ServerInit` (load game data + create socket).
5. **Lobby loop** (while not all players present): `VIBE_AcceptConnection`; count connected slots; when `count==NumPlayers` and all past handshake, advance. Every 3000ms (`0xBB8`) or when ready -> `VIBE_SendLanBroadcast`. Each iter: `VIBE_ServiceClientsRecv`/`VIBE_ServiceClientsSend`, `Sleep(30)`.
6. **Game loop** (`VIBE_g_ShutdownRequest` gating): `VIBE_GameTick(0x414C6C)` -> `VIBE_ServiceClientsRecv` -> `VIBE_ServiceClientsSend` -> `VIBE_DispatchGameCommands`, `Sleep(30)`. Periodic time-sync packet (cmd 0x1E, payload from `dword_C2B562`/`word_C2B566` tick counters) and keepalive (cmd 0x20).
7. Shutdown / socket error -> `WSAGetLastError`; `VIBE_NetErrorHandler`; `VIBE_ServerShutdown`.

## Socket setup — `VIBE_CreateListenSocket` (0x428130)
`WSAStartup(0x101)`; `socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)`; `setsockopt` SO_REUSEADDR,
SO_BROADCAST, SO_SNDBUF/SO_RCVBUF=0x40000 (256 KB); `ioctlsocket(FIONBIO,1)`; `bind(INADDR_ANY:port)`;
`listen(5)`. Returns socket -> `VIBE_g_ListenSocket` (0x365b838).

## Accept — `VIBE_AcceptConnection` (0x428384)
`accept()` into first free slot of `VIBE_g_ClientSlots`. Slot init: `flags=0x103`
(Connected|Handshake | byte1 SendReady), recv/send byte counters=0, proto version=25 @+0x14A,
socket @+0x10, timeout `GetTickCount()+300000` @+0x14C. Builds two 8192-entry packet pools
(153 B nodes) as doubly-linked free-lists at slot+0x150 (incoming) / +0x132150 (outgoing);
queue heads/free-list heads at +0x2641EC..+0x2641F8.

## Per-client slot layout (stride 0x264208 = 2,507,272 B; ×16)
| Off | Size | Field |
|-----|------|-------|
| +0x00 | 4 | flags: byte0 state machine, byte1 send status (bit0 send-done) |
| +0x10 | 4 | SOCKET |
| +0x14 | 153 | recv buffer `[cmd:1][total_len:2 LE][payload:150]` |
| +0xAD | 153 | send buffer (same format) |
| +0x146 | 2 | recv byte count (partial recv) |
| +0x148 | 2 | send offset (partial send) |
| +0x14A | 2 | protocol version (init 25 = 0x19) |
| +0x14C | 4 | timeout deadline (GetTickCount) |
| +0x150 | ~1.2M | incoming packet pool (8192×153) |
| +0x132150 | ~1.2M | outgoing packet pool (8192×153) |
| +0x2641EC | 4 | incoming game-queue head |
| +0x2641F0 | 4 | outgoing queue head |
| +0x2641F4 | 4 | incoming pool free-list head |
| +0x2641F8 | 4 | outgoing pool free-list head |

## Packet queue node (153 B / 0x99)
| Off | Size | Field |
|-----|------|-------|
| +0x00 | 1 | command byte |
| +0x01 | 2 | data size (set by `VIBE_GetCommandDataSize`, NOT wire total_len) |
| +0x03 | 1 | sender slot index (set before enqueue) |
| +0x04 | 4 | target slot (0xFFFFFFFF = broadcast) |
| +0x08..+0x90 | 137 | payload |
| +0x91 | 4 | prev ptr |
| +0x95 | 4 | next ptr (offset 149) |

## Wire format
`[cmd:1][total_len:2 LE incl. 3-byte header][payload:total_len-3]`. Max 153 B, min 3 B.
No magic, no encryption, no checksum. Plaintext.

## RX/TX pipeline
- `VIBE_RecvClientData` (0x42861c): accumulate header then payload; complete -> flags|=0x80 (PacketReady). WSAEWOULDBLOCK non-fatal.
- `VIBE_ServiceClientsRecv` (0x428864): per-slot state machine —
  - flags&2 (Handshake) + cmd3 -> echo via `VIBE_EnqueueOutgoingPacket`; flags=(flags&0x79)|4 (PostAuth).
  - flags&8 (Init): cmd8 LoadStart -> size=`VIBE_g_LoadBufferSize`, alloc `VIBE_AllocLoadBuffer`, 16 progress slots=-1; cmd9 LoadChunk -> memcpy 128 B into `VIBE_g_LoadBuffer`, progress+=128; done -> clear flag8.
  - flags&4 (PostAuth, unexpected) -> "Unexpected Request from connection nr %i", return -1.
  - else -> set sender index, `VIBE_EnqueueIncomingPacket`, clear PacketReady.
- `VIBE_EnqueueIncomingPacket`/`VIBE_EnqueueOutgoingPacket` (0x428b6c/0x428c04): alloc node from free-list, copy 0x99, set size, append to tail.
- `VIBE_ServiceClientsSend` (0x428acc): copy outgoing-queue head into send buffer, `VIBE_DequeueOutgoingPacket`, `VIBE_SendClientData`.
- `VIBE_DequeueOutgoingPacket` (0x428c9c): unlink node, return to free-list.

## Command dispatch — `VIBE_DispatchGameCommands` (0x428cfc)
Drains each slot's incoming game queue (+0x2641EC). Orders cmd5 (disconnect) before cmd6
(end-marker). For cmd<0x60: `VIBE_g_CommandHandlerTable[cmd]()` (table @ **0x439b60**, 96 entries).
Handler ret 0 -> **broadcast** result to all connected clients (`VIBE_EnqueueOutgoingPacket` per slot,
target=0xFFFFFFFF); ret!=0 -> set cmd=2 (error), send only to originator. Special: cmd0x20 with
`payload[0x10]==0x0E` -> copy node to per-client state buffer `unk_127D7A0[slot]` instead of broadcasting.

## State machine (byte0 of slot flags)
```
accept -> 0x03 Connected|Handshake
 recv cmd3 (echo)        -> 0x05 Connected|PostAuth   (flags&0x79 |0x04)
 host sets bit3          -> 0x08 Init (LoadStart/LoadChunk transfer; gilde.exe-driven)
 load complete           -> clear 0x08 -> Playing
 recv any cmd (Playing)  -> enqueue+dispatch, clear PacketReady(0x80)
 timeout(5min)/err/cmd5  -> VIBE_CloseClientConnection
```
PostAuth is a blocking wait: server rejects packets ("Unexpected Request...") until the host
process (gilde.exe, shared memory) sets the Init bit. Bits 0x10/0x20/0x40 are loop-phase markers
used by ServerThreadMain for lobby->game and time-sync sequencing.

## Command data-size table — `VIBE_GetCommandDataSize` (0x403f44)
| Size | Commands |
|------|----------|
| 20 | 03,08,1C,28,31,3B,42,43,4A,4D,57,58 |
| 17 | 04 |
| 80 | 0A |
| 42 | 0B |
| 73 | 0C |
| 24 | 0D,21,23,24,26,27,29,2A,33,39,3A,5A,19,53(36) |
| 33 | 0F,2C |
| 39 | 11,3D |
| 30 | 12,1E |
| 28 | 13,1A,25,2B,34,36,38,47,52,54,5B,5D,1B(40) |
| 22 | 14 |
| 57 | 15 |
| variable | 16,17 (sub-array), 18 (count×5+21), 20 (conditional) |
| 95 | 1D |
| 61 | 22 |
| 31 | 2D,45 |
| 32 | 2E,3C |
| 60 | 2F,30 |
| 52 | 32,3E,4F,5E |
| 93 | 35 |
| 69 | 37 |
| 47 | 40 |
| 74 | 41 |
| 23 | 44 |
| 25 | 46 |
| 21 | 48 |
| 55 | 49 |
| 144 | 4B |
| 53 | 4C |
| 64 | 4E |
| 68 | 50,51 |
| 97 | 55 |
| 56 | 56 |
| 26 | 5C |
| 145 | default |

## Command handler jump table @ 0x439b60 (`VIBE_g_CommandHandlerTable`, 96 entries)
65 distinct targets in range 0x404xxx–0x407xxx. Default/no-op stub = `sub_406770` (3 bytes).
Control-cmd handler (cmd 0,3,5,6,7) = `sub_4049B4`. Full opcode->handler map and per-handler
naming tracked in [report/network/command-handlers.md](command-handlers.md).

## UDP LAN discovery — `VIBE_SendLanBroadcast` (0x417070)
`socket(AF_INET,SOCK_DGRAM)`, SO_BROADCAST, `sendto(255.255.255.255:port, buf, 106, 0)`, close.
Announces server presence + current/expected player count to LAN.

## Key globals (renamed)
| Addr | Name | Meaning |
|------|------|---------|
| 0x365b7e0 | VIBE_g_ServerPort | listen port |
| 0x365b7e4 | VIBE_g_NumPlayers | expected players |
| 0x365b838 | VIBE_g_ListenSocket | listen SOCKET |
| 0x43d970 | VIBE_g_ShutdownRequest | Exit_ sets 1, thread clears |
| 0x365b834 | VIBE_g_LoadBuffer | global game-state load buffer |
| 0x365b7ec | VIBE_g_LoadBufferSize | expected total size |
| 0x365b7f0 | VIBE_g_LoadProgress | per-transfer received counters[16] |
| 0x1019650 | VIBE_g_ClientSlots | base of 16 client slots |
| 0x439b60 | VIBE_g_CommandHandlerTable | opcode dispatch table |
| 0x127d840 | VIBE_g_ClientOutQueueHead | outgoing queue head array |
| 0xc2b562 / 0xc2b566 | tick counters | server time-sync source (cmd 0x1E) |
