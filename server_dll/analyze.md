## Comprehensive Analysis of `server.dll.c`

### What It Is

This is a **Ghidra decompilation** of `server.dll` from the game **Europa 1400 - Die Gilde** (The Guild), specifically the **Russobit-M Russian release v1.03**. The DLL was compiled with **Open Watcom C/C++ 2.18** using the `__watcall` register-based calling convention (first param in EAX, second in EDX, third in EBX). At 21,597 lines, it contains the **entire multiplayer server engine** — networking, game simulation, savegame I/O, and a full copy of the game world logic needed for authoritative server-side operation.

### File Structure

| Line Range | Content |
|---|---|
| 1–1117 | PE type definitions, global variables, ~300 string references |
| 1118–9000 | **Game logic**: offices, buildings, objects, players, cities, savegame versioning |
| 9000–12800 | **VFS + zlib 1.1.3**: compressed game file I/O, custom memory allocator with leak detection |
| 12850–13700 | **Core networking**: TCP server, accept loop, recv/send, packet queuing |
| 13700–15600 | Watcom CRT: memset, string ops, stdio |
| 15620–15700 | DLL entry point (`entry()` — Watcom CRT DLL init) |
| 15700–21597 | More CRT, math routines, winsock thunks |

### Exports

The DLL has only **two exports** — this is a very clean interface:

```12858:12876:Server\server.dll.c
longlong __fastcall Init_(undefined4 param_1,uint param_2)
{
  LPVOID in_EAX;
  // ...
  // 0x27678  1  Init_
  _DAT_0365b6d8 =
       CreateThread((LPSECURITY_ATTRIBUTES)0x0,0,(LPTHREAD_START_ROUTINE)&LAB_00427700,in_EAX,0,
                    &DStack_c);
  // ...
}
```

```12882:12895:Server\server.dll.c
void Exit_(void)
{
                    // 0x276b0  2  Exit_
  if (_DAT_0365b6d8 != (HANDLE)0x0) {
    DAT_0043d970 = 1;
    do {
      Sleep(10);
    } while (DAT_0043d970 != 0);
    TerminateThread(_DAT_0365b6d8,0);
    CloseHandle(_DAT_0365b6d8);
  }
  return;
}
```

- **`Init_` (ordinal 1)**: Takes a context pointer in EAX, spawns a server thread, returns success/failure
- **`Exit_` (ordinal 2)**: Signals the server thread to stop (via `DAT_0043d970 = 1`), waits for it to acknowledge, then force-terminates and cleans up

### How It Works — Architecture

#### Startup Sequence

When `gilde.exe` calls `Init_`:

1. **Thread creation**: A background thread starts at `LAB_00427700` → `FUN_00428038`
2. **Thread init** (line 12901): Sets thread priority above normal, initializes logging to `server.log`
3. **Game data loading** (line 12922-12962): Constructs path from `\project\DieGilde\game\` + suffix, loads game files through VFS
4. **Game state init**: Initializes buildings (`GebaeudePROT/INST`), objects (`ObjektePROT/INST`), offices (`LoadAemter` — 37 slots), NPCs, and the city map
5. **TCP server creation** via `FUN_00428130`

#### Main Server Loop

The server thread (at `LAB_00427700`) runs a tight loop:

1. **Accept** new connections (`FUN_00428384`)
2. **Receive** packets from all clients (`FUN_00428864`)
3. **Dispatch** game commands through the command queue (`FUN_00428cfc`)
4. **Send** outgoing packets to all clients (`FUN_00428acc`)

Shutdown is signaled by `Exit_` setting `DAT_0043d970 = 1`. The thread acknowledges by clearing it before termination.

#### TCP Server Setup (`FUN_00428130`, line 12975)

```12993:13021:Server\server.dll.c
  WSAStartup(0x101,&WStack_1bc);
  s = socket(2,1,6);
  // ... socket options:
  // SO_REUSEADDR, SO_BROADCAST, 256KB send/recv buffers, non-blocking mode
  // ...
  iVar2 = bind(s,&local_2c,0x10);
  if (iVar2 != -1) {
    iVar2 = listen(s,5);
```

- Creates an `AF_INET / SOCK_STREAM / IPPROTO_TCP` socket
- Sets `SO_REUSEADDR`, `SO_BROADCAST`, 256KB (`0x40000`) send and receive buffers
- Puts socket in non-blocking mode (`ioctlsocket` with `FIONBIO`)
- Binds to `INADDR_ANY` on the game port and listens with backlog 5

#### Connection Management (line 13075)

The server supports up to **16 concurrent clients** (client slots at stride `0x264208` bytes — about 2.4 MB per client). When a client connects:

- `accept()` is called on the listening socket
- A free client slot is found in the connection table (`DAT_01019650`)
- State flags initialized to `0x0103` (byte 0 = Connected + Handshake, byte 1 = SendReady)
- Protocol version field set to `0x19` (25) at slot offset `0x14A`
- 64KB send/recv buffers assigned per client
- 300-second timeout via `GetTickCount() + 300000`
- Two packet queue pools initialized: 8192 entries × 153 bytes each (incoming + outgoing)

#### Client Slot Structure

Each client occupies a 0x264208-byte (2,433,544 bytes) slot:

| Offset | Size | Field |
|--------|------|-------|
| `+0x00` | 4 | State flags: byte 0 = state machine, byte 1 = send status |
| `+0x10` | 4 | Socket handle |
| `+0x14` | 153 | Recv buffer: `[cmd:1][total_len:2][payload:150]` |
| `+0xAD` | 153 | Send buffer: same format |
| `+0x146` | 2 | Recv byte count (partial receive tracking) |
| `+0x148` | 2 | Send byte count (partial send tracking) |
| `+0x14A` | 2 | Protocol version (init 0x19 = 25) |
| `+0x14C` | 4 | Timeout deadline (GetTickCount value) |
| `+0x150` | ~1.2M | Incoming packet queue pool (8192 × 153 bytes) |
| `+0x132150` | ~1.2M | Outgoing packet queue pool (8192 × 153 bytes) |
| `+0x2641EC` | 4 | Incoming game queue head pointer |
| `+0x2641F0` | 4 | Outgoing queue head pointer |
| `+0x2641F4` | 4 | Incoming pool free-list head |
| `+0x2641F8` | 4 | Outgoing pool free-list head |

#### Packet Queue Entry Structure

Each queue entry is 153 bytes (0x99):

| Offset | Size | Field |
|--------|------|-------|
| `+0x00` | 1 | Command byte |
| `+0x01` | 2 | Data size (set by `FUN_00403f44`, NOT the wire total_len) |
| `+0x03` | 1 | Sender slot index (written by dispatch before enqueue) |
| `+0x04` | 4 | Target slot (0xFFFFFFFF = broadcast to all) |
| `+0x08`–`+0x90` | 137 | Packet payload data |
| `+0x91` | 4 | Prev pointer (doubly-linked list) |
| `+0x95` | 4 | Next pointer (doubly-linked list) |

### Packet Protocol

#### Wire Format (lines 13194-13318)

```
┌──────────┬───────────────┬────────────────────┐
│ Command  │ Total Length  │ Payload            │
│ (1 byte) │ (2 bytes LE) │ (Length - 3 bytes) │
└──────────┴───────────────┴────────────────────┘
```

- **Total Length** includes the 3-byte header itself
- Maximum packet: 153 bytes (0x99)
- Minimum packet: 3 bytes (header only)

#### Recv (`FUN_0042861c`, line 13194)

1. Read 3-byte header into recv buffer (slot + 0x14)
2. Extract total_len from bytes 1-2 (u16 LE)
3. Read remaining (total_len - 3) payload bytes
4. On completion: set PacketReady flag (byte 0 |= 0x80)
5. Error 0x2733 (WSAEWOULDBLOCK): non-fatal, return and try next tick

#### Send (`FUN_0042878c`, line 13277)

1. Send from send buffer (slot + 0xAD), starting at send_offset
2. Send remaining = total_len - send_offset
3. On completion: set byte 1 flag bit 0 (send complete)
4. Partial sends tracked via send_offset at slot + 0x148

### State Machine

```
Connect → Handshake(0x02) → PostAuth(0x04) → Init(0x08) → Playing(0x10)
```

#### State Flag Transitions (byte 0 of slot)

| Transition | Trigger | Flag Operation |
|---|---|---|
| **Accept** | `accept()` | flags = `0x03` (Connected + Handshake) |
| **Handshake → PostAuth** | Recv cmd 0x03 | `flags = (flags & 0x79) \| 0x04` |
| **PostAuth → Init** | Host triggers externally | Host sets `0x08` in shared memory |
| **Init → Playing** | LoadChunk completes | `flags &= 0xF7` (clear Init) |
| **Playing → dispatch** | Recv any cmd | `flags &= 0x6F` (clear Playing + PacketReady) |
| **PacketReady clear** | After dispatch | `flags &= 0x7F` |

#### Handshake (Command 0x03)

The handshake is an **echo protocol**:

1. Client sends `[0x03][len][auth_payload]`
2. Server copies the recv buffer (153 bytes) to the outgoing queue via `FUN_00428c04`
3. Send loop (`FUN_00428acc`) copies from outgoing queue to send buffer
4. `FUN_0042878c` sends it on the wire
5. **Client receives its own packet back** as confirmation
6. State transitions to PostAuth: `byte0 = 0x05` (Connected + PostAuth)

PostAuth is a **blocking wait state** — the server rejects all further packets with "Unexpected Request from connection" until the host game process externally sets FlagInit.

#### Game State Transfer (Commands 0x08/0x09)

**LoadStart (0x08)**:
- Total game state size at recv_buffer[0x10] = payload[13:17] (u32 LE)
- Allocates global load buffer via `FUN_00416480("srv_LoadBuf")`
- Initializes 16 progress slots to 0xFFFFFFFF
- Resets byte counter to 0

**LoadChunk (0x09)**:
- Copies 128 bytes (0x80) from recv_buffer[0x10] to load buffer
- Source: payload[13:141]
- Increments received counter by 0x80
- When `received >= total_size`: clears FlagInit → transition complete

The load buffer is **global** (`_DAT_0365b834`), not per-client. Only one transfer at a time.

#### Game Dispatch (Commands 0x00-0x5F)

Processed by `FUN_00428cfc` from the incoming game queue:

1. Dequeue packet from slot's incoming queue (offset `+0x2641EC`)
2. Prioritize: cmd 0x05 (Disconnect) processed before cmd 0x06 (EndMarker)
3. Command byte < 0x60 → call jump table handler: `PTR_LAB_00439b60[cmd]()`
4. Return 0 (success): **broadcast** result to all connected clients
5. Return != 0 (error): set cmd to 0x02, **send error to originator only**

Special case: cmd 0x20 with payload[0x10] == 0x0E → copy to per-client state buffer instead of broadcasting.

### Command Size Table (`FUN_00403f44`)

This function returns the meaningful data size for each command within the 153-byte queue entry:

| Size | Commands |
|------|----------|
| 0x14 (20) | 0x03, 0x08, 0x1C, 0x28, 0x31, 0x3B, 0x42, 0x43, 0x4A, 0x4D, 0x57, 0x58 |
| 0x11 (17) | 0x04 |
| 0x50 (80) | 0x0A |
| 0x2A (42) | 0x0B |
| 0x49 (73) | 0x0C |
| 0x18 (24) | 0x0D, 0x21, 0x23, 0x24, 0x26, 0x27, 0x29, 0x2A, 0x33, 0x39, 0x3A, 0x5A |
| 0x21 (33) | 0x0F, 0x2C |
| 0x27 (39) | 0x11, 0x3D |
| 0x1E (30) | 0x12, 0x1E |
| 0x1C (28) | 0x13, 0x1A, 0x25, 0x2B, 0x34, 0x36, 0x38, 0x47, 0x52, 0x54, 0x5B, 0x5D |
| 0x16 (22) | 0x14 |
| 0x39 (57) | 0x15 |
| Variable | 0x16, 0x17 (sub-array), 0x18 (count×5+21), 0x20 (conditional) |
| 0x24 (36) | 0x19, 0x53 |
| 0x28 (40) | 0x1B |
| 0x5F (95) | 0x1D |
| 0x3D (61) | 0x22 |
| 0x1F (31) | 0x2D, 0x45 |
| 0x20 (32) | 0x2E, 0x3C |
| 0x3C (60) | 0x2F, 0x30 |
| 0x34 (52) | 0x32, 0x3E, 0x4F, 0x5E |
| 0x5D (93) | 0x35 |
| 0x45 (69) | 0x37 |
| 0x2F (47) | 0x40 |
| 0x4A (74) | 0x41 |
| 0x17 (23) | 0x44 |
| 0x19 (25) | 0x46 |
| 0x15 (21) | 0x48 |
| 0x37 (55) | 0x49 |
| 0x90 (144) | 0x4B |
| 0x35 (53) | 0x4C |
| 0x40 (64) | 0x4E |
| 0x44 (68) | 0x50, 0x51 |
| 0x61 (97) | 0x55 |
| 0x38 (56) | 0x56 |
| 0x1A (26) | 0x5C |
| 0x91 (145) | Default (all other commands) |

### UDP LAN Discovery (line 9075)

```9088:9098:Server\server.dll.c
  s = socket(2,2,0);
  setsockopt(s,0xffff,0x20,local_14,4);
  // ...
  local_34.sa_data._2_4_ = htonl(0xffffffff);  // 255.255.255.255
  local_34.sa_data._0_2_ = htons(in_AX);
  sendto(s,param_2,0x6a,0,&local_34,0x10);
  closesocket(s);
```

A separate UDP datagram socket broadcasts 0x6A (106) bytes to `255.255.255.255` for LAN game discovery.

### Game Logic Subsystems

The DLL contains a **full authoritative copy of the game simulation**:

| Subsystem | Evidence | Details |
|---|---|---|
| **Offices** | `LoadAemter` | 37 (0x25) office slots, election/appointment logic |
| **Buildings** | `GebaeudePROT/INST` | Prototypes + instances, map placement (`PlantMap`) |
| **Objects** | `ObjektePROT/INST` | Object instances with parent-child hierarchy, type IDs |
| **Players** | `SpID`, `PlayerID`, `PlayerName` | Up to 768 (0x300) player slots of 0x218 bytes each |
| **Cities** | `STADTAUSWAHL` | City selection and map management |
| **Savegames** | `Savegame`, version checks | Version codes 0x10024–0x10039+, backward-compatible loading |
| **Memory** | Custom allocator | Pool allocator, debug tracking, buffer overwrite detection, leak reporting |
| **Logging** | `server.log`, console | Categorized logging with module/line info, error handler with MessageBox fallback |
| **Compression** | zlib 1.1.3 | Gz-compressed game data files through custom VFS |

### Can You Make a Dedicated Server From This?

**Short answer: Yes conceptually, but the decompiled code is not the path — loading the original binary is.**

#### Why the decompiled C code is NOT directly usable:

1. **Not compilable**: Ghidra artifacts everywhere — `CONCAT44`, `undefined4`, `extraout_ECX`, `ulonglong` casts, phantom register variables. This is pseudocode, not C.
2. **Lost type information**: All game structures are flattened to raw pointer offsets (`in_EAX + 0x178`, `iVar2 + 0xbc5d90`). The original structs had meaningful fields.
3. **Watcom `__watcall`**: The calling convention passes params in EAX/EDX/EBX — Ghidra partially recovers this but introduces `in_EAX` ghost variables throughout.
4. **Hardcoded addresses**: Hundreds of absolute addresses (`DAT_01019650`, `0xbc5bb0`, `0xc2af9c`) are resolved at the original base address.

#### What IS feasible — a dedicated server launcher:

The DLL has a beautifully clean 2-function API. A dedicated server would be a small host program:

```c
// Conceptual dedicated server launcher
typedef int (__watcall *PFN_Init)(void *ctx);
typedef void (__watcall *PFN_Exit)(void);

int main() {
    HMODULE hServer = LoadLibraryA("Server\\server.dll");
    PFN_Init pfnInit = (PFN_Init)GetProcAddress(hServer, "Init_");
    PFN_Exit pfnExit = (PFN_Exit)GetProcAddress(hServer, "Exit_");

    // Init_ passes its EAX parameter to CreateThread as lpParameter
    // Need to determine what context gilde.exe passes here
    pfnInit(game_context);

    // Keep process alive — server runs on its own thread
    WaitForSingleObject(GetCurrentThread(), INFINITE);

    pfnExit();
    FreeLibrary(hServer);
}
```

#### Challenges for a real dedicated server:

1. **Unknown context parameter**: `Init_` receives a pointer in EAX that it passes to `CreateThread`. You'd need to reverse-engineer what `gilde.exe` passes here — likely a configuration struct with port number, game settings, and file paths.

2. **Game file dependency**: The server thread loads data from `\project\DieGilde\game\` path. The full game installation must be present.

3. **Window handle dependency**: `FUN_00415ffc` calls `GetWindowTextA(unaff_EBX, ...)` — it expects a window handle (for the error handler). A headless server would need to provide a dummy HWND or patch this path.

4. **Port number**: The port is passed via `in_AX` to `htons()` in `FUN_00428130`. It comes from the context/configuration — you'd need to trace how `gilde.exe` sets it.

5. **Missing game loop driver**: The host game process (gilde.exe) shares memory with server.dll and directly manipulates client slot flags (e.g., PostAuth → Init transitions). Without a host process, a pure protocol reimplementation must handle these transitions itself.

#### Most practical approach:

Rather than decompiling and rewriting, the most practical dedicated server would:

1. **Run `gilde.exe` headless** with command-line or config-file options to start hosting immediately
2. Use your existing **networkfix** proxy DLL infrastructure to inject a "dedicated server mode" that auto-hosts on startup
3. Intercept the `Init_` call to capture and log the context parameter, then replicate it in a standalone loader

The decompiled code's value is as a **reference for understanding the protocol and game state** — not as source code to compile. It tells you exactly what packets the server expects, how client slots work, what the timeout values are, and how the game state is structured. That knowledge is invaluable for writing a proper dedicated server or protocol-compatible reimplementation.
