# Network Protocol Specification

**Protocol Name**: DieGilde TCP Game Protocol v1  
**Transport**: TCP/IP over WinSock  
**Default Port**: 7531  
**Architecture**: Client-Server, synchronous  
**Max Packet**: 153 bytes  
**Tick Rate**: 30Hz (30-50ms)  

---

## Packet Format

### General Structure

All packets follow a fixed 3-byte header format:

```
┌─────┬──────────┬──────────────────┐
│ Op  │  Length  │    Payload       │
│ (1) │   (2)    │   (0-150 bytes)  │
└─────┴──────────┴──────────────────┘
  0     1-2        3+
```

**Fields**:
- **Opcode** (Byte 0): Message type ID (0x00-0x5F)
- **Length** (Bytes 1-2): Total packet size including 3-byte header (little-endian u16)
- **Payload** (Bytes 3+): Variable data (max 150 bytes)

**Example**:
```
Handshake packet (opcode 0x03, 20-byte total):
  Byte 0:   0x03           (opcode)
  Byte 1-2: 0x14 0x00      (length = 20 in little-endian)
  Byte 3-19: [auth data]   (17 bytes of authentication payload)
```

---

## Opcode Classification

### Connection Management (0x03-0x06)

| Opcode | Name | Direction | Size | Purpose |
|--------|------|-----------|------|---------|
| 0x03 | Handshake | Bidirectional | 20 | Authentication / Echo |
| 0x04 | Keep-Alive | Bidirectional | 17 | Ping / Heartbeat |
| 0x05 | Disconnect | Client→Server | 20 | Request disconnection |
| 0x06 | End-of-Message | Server→Client | ? | Message boundary marker |

### Game State Transfer (0x08-0x09)

| Opcode | Name | Direction | Size | Purpose |
|--------|------|-----------|------|---------|
| 0x08 | LoadStart | Client→Server | 20 | Request game state |
| 0x09 | LoadChunk | Bidirectional | Variable | Game state chunk transfer |

**LoadStart (0x08)**:
- Client initiates game state transfer
- Payload contains total expected size (u32 LE)
- Server responds by queuing LoadChunk packets

**LoadChunk (0x09)**:
- Server → Client: Sends 128-byte chunks of compressed state
- Client → Server: Acknowledges receipt (optional)
- Transfer continues until all bytes received

### Game Commands (0x10-0x5D)

**Movement** (0x10-0x1F):
- 0x10-0x1F: Character movement, navigation, mount/dismount

**Character/Status** (0x20-0x2F):
- 0x20-0x2F: Skill changes, quest progress, character states

**Inventory/Economy** (0x30-0x3F):
- 0x30-0x3F: Buy, sell, trade, use item, loot

**Buildings/Structures** (0x40-0x4F):
- 0x40-0x4F: Construction, upgrade, hire NPCs, taxes

**NPC/Interaction** (0x50-0x5D):
- 0x50-0x5D: Dialogue, romance, cutscenes, events

---

## Detailed Opcode List

### 0x03 - Handshake (Echo Protocol)

**Flow**:
```
Client                              Server
  │                                   │
  ├─ send([0x03][20][auth_data]) ──→ │
  │                                   ├─ recv() & validate
  │                                   │
  │ ←── send([0x03][20][same_data]) ─┤
  │                                   │
  └─ recv() & verify match            │
```

**Packet Format**:
```
Offset  Size  Field
───────────────────────
0       1     0x03 (opcode)
1-2     2     0x14 (20 bytes)
3-4     2     Player ID or protocol version
5-6     2     Authentication type / flags
7-10    4     Checksum or timestamp
11-19   9     Reserved / authentication payload
```

### 0x04 - Keep-Alive / Ping

**Format** (17 bytes):
```
Offset  Size  Field
───────────────────────
0       1     0x04 (opcode)
1-2     2     0x11 (17 bytes)
3-6     4     Timestamp (GetTickCount())
7-10    4     Client sequence number
11-16   6     Reserved
```

### 0x08 - Request Game Load

**Triggered By**: Client after successful handshake  
**Server Response**: Stream of 0x09 packets

**Format** (20 bytes):
```
Offset  Size  Field
───────────────────────
0       1     0x08 (opcode)
1-2     2     0x14 (20 bytes)
3-6     4     Expected total size (u32 LE)
7-19    13    Reserved / metadata
```

### 0x09 - Game State Chunk

**Format** (variable, typically 128 bytes + header):
```
Offset  Size  Field
───────────────────────
0       1     0x09 (opcode)
1-2     2     0xXX (total length)
3-6     4     Chunk offset (u32 LE)
7-10    4     Chunk sequence number
11-138  128   Compressed game state data
139+    ?     Padding / alignment
```

### Sample Command Opcodes

**0x10 - Move Character**:
- Destination coordinates (X, Y, Z)
- Movement flags (walk/run, mount, etc.)

**0x30 - Sell Object**:
- Object prototype ID (u32)
- Quantity to sell (u32)
- Buyer NPC ID (u32)
- Expected price (u32)

**0x40 - Build Structure**:
- Building blueprint ID (u32)
- Map coordinates (X, Y, Z)
- Orientation (u8)
- Building ID assignment (u32)

---

## Complete Opcode Size Table

From `sub_403F44()` (VIBE_GetPacketSize):

```c
int GetPacketSize(u8 opcode) {
  switch (opcode) {
    case 0x03: case 0x08: case 0x1C: case 0x28: case 0x31:
    case 0x3B: case 0x42: case 0x43: case 0x4A: case 0x4D:
    case 0x57: case 0x58:
      return 20;
    
    case 0x04:                      return 17;
    case 0x0A:                      return 80;
    case 0x0B:                      return 42;
    case 0x0C:                      return 73;
    case 0x0D: case 0x21: case 0x23: case 0x24: case 0x26:
    case 0x27: case 0x29: case 0x2A: case 0x33: case 0x39:
    case 0x3A: case 0x5A:
      return 24;
    
    case 0x0F: case 0x2C:           return 33;
    case 0x11: case 0x3D:           return 39;
    case 0x12: case 0x1E:           return 30;
    case 0x13: case 0x1A: case 0x25: case 0x2B: case 0x34:
    case 0x36: case 0x38: case 0x47: case 0x52: case 0x54:
    case 0x5B: case 0x5D:
      return 28;
    
    case 0x14:                      return 22;
    case 0x15:                      return 57;
    case 0x16: case 0x17:           return VARIABLE;  // Dynamic
    case 0x18:                      return VARIABLE;  // count * 5 + 21
    case 0x19: case 0x53:           return 36;
    case 0x1B:                      return 40;
    case 0x1D:                      return 95;
    case 0x1F: case 0x2D: case 0x45: return 31;
    case 0x20:                      return VARIABLE;  // Conditional
    case 0x22:                      return 61;
    case 0x2E: case 0x3C:           return 32;
    case 0x2F: case 0x30:           return 60;
    case 0x32: case 0x3E: case 0x4F: case 0x5E:
      return 52;
    
    case 0x35:                      return 93;
    case 0x37:                      return 69;
    case 0x40:                      return 47;
    case 0x41:                      return 74;
    case 0x44:                      return 23;
    case 0x46:                      return 25;
    case 0x48:                      return 21;
    case 0x49:                      return 55;
    case 0x4B:                      return 144;
    case 0x4C:                      return 53;
    case 0x4E:                      return 64;
    case 0x50: case 0x51:           return 68;
    case 0x55:                      return 97;
    case 0x56:                      return 56;
    case 0x5C:                      return 26;
    
    default:                        return 145;  // Unknown
  }
}
```

---

## Dynamic Packet Formats

### Opcode 0x16 / 0x17 (Variable Array)

```
Offset  Size  Field
───────────────────────
0       1     0x16 or 0x17
1-2     2     Total length (includes header)
3-20    18    Fixed header data
21      1     Element count (u8)
22+     var   Elements (4+ bytes each)
```

**Element Format** (example):
```
[width:1][height:1][bitmap:width*height]
```

### Opcode 0x18 (Counted Array)

**Size Formula**: `count × 5 + 21`

```
Offset  Size  Field
───────────────────────
0       1     0x18
1-2     2     Total length
3-6     4     Count (u32 LE)
7+      count*5 Array elements (5 bytes each)
```

### Opcode 0x20 (Conditional)

**Special Handling**:
```
if (payload[0x10] == 0x0E) {
  // Route to per-client state buffer instead of broadcast
  // Size varies based on condition
} else {
  // Standard broadcast (145 bytes)
}
```

---

## Connection State Transitions

### State Flags (per connection)

```c
// Byte 0: Connection state machine
#define FLAG_CONNECTED      0x01  // TCP connection established
#define FLAG_HANDSHAKE      0x02  // Waiting for/processing handshake
#define FLAG_POSTAUTH       0x04  // Auth complete, waiting for host
#define FLAG_INIT_LOAD      0x08  // Game data transfer in progress
#define FLAG_UNKNOWN_10     0x10  // Possibly: ready for commands?
#define FLAG_UNKNOWN_20     0x20  // (Unknown)
#define FLAG_UNKNOWN_40     0x40  // (Unknown)
#define FLAG_DATA_READY     0x80  // Packet complete, ready to dispatch

// Byte 1: Send/transmit status
#define SEND_FLAG_SENT      0x01  // Current packet fully transmitted
#define SEND_STATUS_2       0x02  // (Unknown)
#define SEND_STATUS_4       0x04  // (Unknown)
```

### Transition Diagram

```
TCP ACCEPTED
    │
    ├─ flags = 0x03 (Connected + Handshake)
    │
    ↓
Client sends opcode 0x03 (handshake)
    │
    ├─ server echoes response
    │ └─ flags = (flags & 0x79) | 0x04  → PostAuth state
    │
    ↓
Host externally sets flag 0x08 (Init Load)
    │ (This is done by gilde.exe main thread, not server)
    │
    ↓
Client sends opcode 0x08 (request game state)
    │
    ├─ Server queues 0x09 chunks
    │
    ↓
Client recv() all chunks, decompresses
    │
    ├─ flags &= 0xF7  → Clear init load flag
    │
    ↓
PLAYING STATE
    │
    ├─ Client sends game commands (0x10-0x5D)
    ├─ Server processes via game tick
    ├─ Server broadcasts state updates (0x09+)
    │
    ↓
Idle timeout (5 minutes) or client disconnect
    │
    ├─ Close socket
    ├─ Free buffers
    ├─ flags = 0x00
```

---

## Error Responses

### Error Packet (Opcode 0x02)

**Triggered**: When game command handler returns non-zero

```
Offset  Size  Field
───────────────────────
0       1     0x02 (error)
1-2     2     0x91 (145 bytes, default size)
3-6     4     Original opcode that failed
7-10    4     Error code
11-144  134   Error message / context
```

---

## Broadcast Packet (UDP Discovery)

**Purpose**: LAN server discovery  
**Transport**: UDP  
**Size**: 106 bytes (0x6A)  
**Destination**: 255.255.255.255:[game_port]

**Contents**:
```
Offset  Size  Field
───────────────────────
0-9     10    Server identification / magic
10-13   4     Game version
14-17   4     Player count
18-21   4     Max players (16)
22-105  84    Server name, game info, etc.
```

---

## Reliability & Ordering

**Reliability Model**: TCP-based (guaranteed delivery, in-order)

**Packet Ordering**:
1. Handshake (0x03) must complete before game state
2. Game state (0x08/0x09) must complete before commands
3. Commands (0x10+) processed in order received

**No explicit retransmission**: Handled by TCP layer

**No sequence numbers**: Except in game state chunks (optional tracking)

**Timeout**: 5 minutes of no activity = disconnect

---

## Bandwidth Analysis

### Per-Client Estimates

| Direction | Opcode | Size | Frequency | Bandwidth |
|-----------|--------|------|-----------|-----------|
| C→S | 0x10-0x5D | 17-145 | 1-5/tick | 0.5-7 KB/s |
| S→C | 0x09+ | 5-20KB | 1/tick | 5-20 KB/s |
| Total | - | - | - | 5-27 KB/s |

**Total Server Bandwidth** (16 clients):
- Inbound: 8-112 KB/s
- Outbound: 80-320 KB/s
- Total: **88-432 KB/s** (typical: ~200 KB/s)

---

## Implementation Checklist

For custom server implementation:

- [ ] Implement TCP socket server (AF_INET, SOCK_STREAM)
- [ ] Set SO_REUSEADDR, SO_EXCLUSIVEADDRUSE
- [ ] Set 256KB send/recv buffers
- [ ] Non-blocking mode (FIONBIO)
- [ ] Handle partial sends/receives
- [ ] Implement opcode size lookup table
- [ ] Queue-based packet handling
- [ ] Echo handshake protocol (0x03)
- [ ] Game state chunking (0x08/0x09)
- [ ] Command dispatch routing (0x10-0x5D)
- [ ] 5-minute timeout handling
- [ ] UDP broadcast for discovery
- [ ] Per-connection state machine
- [ ] Error response generation (0x02)

---

END OF SECTION

---

## ✅ VERIFIED LIVE (session 2026-06-04, gilde.exe.i64)

Confirmed by decompiling the actual WinSock wrappers and reading their embedded
`nt_Recv()` debug format strings. Function names below are the live IDB names.

### Transport wrappers
| Addr | Name | WinSock | Notes |
|------|------|---------|-------|
| 0x43bc54 | `VIBE_Net_SendPacket` | `send()` | Partial-send aware: advances `word_764CEE` cursor, returns 0 until full packet flushed. |
| 0x43b8d0 | `VIBE_Net_ReceivePacket` (`nt_Recv`) | `recv()` x2 | Stage 1 reads 3-byte header; stage 2 reads `len-3`. |
| 0x43b51c | `VIBE_Net_ConnectToServer` | `connect` | client socket. |
| 0x43b868 | `VIBE_Net_Disconnect` | shutdown/closesocket | |
| 0x43aac0 | `VIBE_Net_OpenBroadcastSocket` | UDP socket | LAN server discovery. |
| 0x43abcc | `VIBE_Net_DiscoverServers` | sendto broadcast | |

**Globals**: `dword_764CE0` = g_clientSocket (-1 = none); `dword_764CE8` = TX buffer,
`word_764CEE` = TX cursor; `dword_764CE4` = RX buffer, `word_764CEC` = RX cursor;
`dword_764CF0` = disconnect flag; `dword_62E5E0`/`dword_62E5D8` = total bytes tx/rx.

### Confirmed command-record layout (the "payload" after the 3-byte frame)
The 3-byte frame ([0]=type, [1..2]=u16 total length) is followed by a command
structure. recv() pulls the whole `length`, then the dispatcher reads:

| Offset | Type | Field | Evidence |
|--------|------|-------|----------|
| +0  | u8   | type / opcode        | `*(u8*)buf`; `==0x20` for Sync |
| +1  | u16  | total length (LE)     | `recv(... *(u16*)(buf+1) - cursor ...)` |
| +4  | u32  | cmdId / owner (-1=none) | `if (*(u32*)(buf+4) != -1)` gate |
| +8  | u32  | **Count (sequence #)** | `cmd->Count` in debug strings |
| +16 | u8   | sub-type              | `==0x0E` with type 0x20 => Sync |

**Sequence / loss detection** (in `VIBE_Net_ReceivePacket`):
- `dword_7652F8` = `nt_LastRequCount` (last received Count)
- `dword_11AA470` = `cm_LastSyncCount`
- `Count == LastReq+1` -> in order, advance. `Count == cm_LastSyncCount` -> dup sync.
  Else -> logs `"nt_Recv(): Lost a Command, cmd->Count is %li, nt_LastRequCount is %li"`
  and resyncs. This is the protocol's ack/sequence layer.

`WSAEWOULDBLOCK (10035)` = non-fatal retry. `recv()==0` = peer closed -> teardown
(shutdown + closesocket + WSACleanup, sets `dword_764CF0`).

### Correction
`0x5cba00` and `0x5f8554` were mislabeled `VIBE_AnimationFrame_Lookup` /
`VIBE_FrameData_Access` by the early animation pass. They are actually the CRT
string formatters `VIBE_Crt_Sprintf` / `VIBE_Crt_Vsprintf` (forward to
`VIBE_Crt_FormatStringCore` @0x6051f0). The "animation frame" narrative in docs
07/08 referencing these two addresses should be read as debug-log formatting.
