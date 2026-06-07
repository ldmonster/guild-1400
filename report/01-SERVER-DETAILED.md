# Server.dll - Complete Reverse Engineering Analysis

**Binary**: server.dll (32-bit x86)  
**Architecture**: x86 32-bit  
**Base Address**: 0x400000  
**Total Functions**: 943 (118 named, 299 library, 526 unnamed)  
**Total Strings**: 592  
**Key Segments**: AUTO (rx), .idata, DGROUP, .bss  
**Compiler**: Open Watcom C/C++ 2.18 (Watcom CRT, __watcall calling convention)  

---

## Entry Points & Initialization

### DLL Entry Point (0x42b3e4 - DllEntryPoint)

Standard Windows DLL initialization:
```
DllEntryPoint()
  → _LibMain()
    → _DLLMain()
      → C runtime initialization
```

**Exports**: Only 2 functions
- `Init_` (ordinal 1): Create server thread with configuration
- `Exit_` (ordinal 2): Signal shutdown, terminate server thread

### Init_ (0x427678)

**Signature**: `int __watcall Init_(void *param_1, uint param_2)`

**Flow**:
```c
{
  LPVOID context = param_1;  // Passed in EAX
  HANDLE hThread = CreateThread(
    NULL,                    // Default security
    0,                       // Default stack size
    (LPTHREAD_START_ROUTINE)&StartAddress,  // Entry point at 0x427700
    context,                 // Thread parameter
    0,                       // Default creation flags
    &DStack_c                // Thread ID output
  );
  dword_365B6D8 = hThread;   // Store global handle
  return success;
}
```

**Purpose**: Spawn background server thread with game configuration context

### StartAddress / Main Server Loop (0x427700)

**Called By**: CreateThread() from Init_  
**Purpose**: Infinite server event loop

**Flow**:

```c
StartAddress() {
  // 1. Initialize logging
  GetModuleFileNameA(hModule, path_buffer);
  open_log_file("server.log");
  
  // 2. Load game configuration from server.ini
  GetPrivateProfileIntA("Network", "Port");       // Default: 7531
  GetPrivateProfileIntA("Server", "MaxPlayers");  // Default: 16
  GetPrivateProfileStringA("Paths", "GameData");  // Path to game files
  
  // 3. Network initialization (sub_428038)
  sub_428038();
    → sub_4094F8()  [open game files, validate paths]
    → sub_40D6A8()  [load resource metadata]
    → sub_4022B4()  [initialize game memory]
    → sub_428130()  [create listening TCP socket]
    → WSAStartup(0x101)
    → socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
    → setsockopt(SO_REUSEADDR, SO_EXCLUSIVEADDRUSE, SO_BROADCAST)
    → setsockopt(SO_RCVBUF, 0x40000)  [256KB recv buffer]
    → setsockopt(SO_SNDBUF, 0x40000)  [256KB send buffer]
    → ioctlsocket(FIONBIO, 1)          [non-blocking mode]
    → bind(0.0.0.0:port)
    → listen(backlog=5)
  
  // 4. Main event loop (approximately every 30-50ms)
  while (!shutdown_requested) {
    
    // Accept new TCP connections
    sub_428384();
      for (i = 0; i < 16; i++) {
        if (client_slot[i] is free) {
          client_socket = accept(listening_socket);
          if (client_socket != INVALID_SOCKET) {
            client_slot[i]->socket = client_socket;
            client_slot[i]->flags = 0x03;      // Connected + Handshake
            client_slot[i]->timeout = GetTickCount() + 300000;
            initialize_buffers(client_slot[i]);
          }
        }
      }
    
    // Call undefined game logic function
    sub_414C6C();  // Game tick - decompress, process logic, sync world
    
    // Receive and dispatch messages from all clients
    sub_428864();
      for (i = 0; i < 16; i++) {
        if (client_slot[i]->flags & 0x01) {  // Connected
          // Accumulate bytes from socket
          sub_42861C();
            while (recv_count < 3) {
              recv(socket, &buf[recv_count], 3 - recv_count);
            }
            opcode = buf[0];
            expected_size = sub_403F44(opcode);  // Get size from table
            while (recv_count < expected_size) {
              recv(socket, &buf[recv_count], expected_size - recv_count);
            }
            client_slot[i]->flags |= 0x80;  // Mark as data ready
          
          // Dispatch if ready
          if (client_slot[i]->flags & 0x80) {
            sub_428B6C();  // Move to dispatch queue
            client_slot[i]->flags &= ~0x80;
          }
        }
      }
    
    // Transmit all queued packets
    sub_428ACC();
      for (i = 0; i < 16; i++) {
        if (client_slot[i]->flags & 0x01) {
          while (bytes_remaining > 0) {
            bytes_sent = send(socket, &buf[offset], bytes_remaining);
            bytes_offset += bytes_sent;
          }
        }
      }
    
    Sleep(30);  // Approximate 30Hz tick
  }
  
  // 5. Shutdown
  if (DAT_0043d970 != 0) {
    DAT_0043d970 = 0;  // Acknowledge shutdown signal
  }
  ExitThread(0);
}
```

---

## Network Architecture

### Socket Initialization (0x428130 - VIBE_SocketCreate)

**Called By**: sub_428038() (network init)  
**Returns**: Listening socket handle or INVALID_SOCKET (-1)

**Complete Flow**:

```c
int VIBE_SocketCreate(int port) {
  WSA_DATA wsaData;
  
  // 1. Initialize Winsock 1.1
  if (WSAStartup(0x0101, &wsaData) != 0) {
    return -1;  // Failure
  }
  
  // 2. Create TCP socket
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return -1;
  }
  
  // 3. Configure socket options
  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, 4);
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&reuse, 4);
  
  int buf_size = 0x40000;  // 256KB
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&buf_size, 4);
  setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buf_size, 4);
  
  // 4. Set non-blocking mode
  u_long nonblock = 1;
  ioctlsocket(s, FIONBIO, &nonblock);
  
  // 5. Bind to INADDR_ANY:port
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  
  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    closesocket(s);
    return -1;
  }
  
  // 6. Listen for connections
  if (listen(s, 5) < 0) {  // Backlog of 5
    closesocket(s);
    return -1;
  }
  
  return s;  // Success
}
```

### Per-Connection State Structure

Each of 16 client slots occupies ~2.4 MB (0x264208 bytes):

```c
struct ClientConnection {
  // Offsets 0x00-0x0F: Header & flags
  u32 state_flags;              // +0x00: byte 0=state, byte 1=send status
  u8 msg_type;                  // +0x01: Message type from packet
  u8 connection_index;          // +0x02: Slot index (0-15)
  u8 reserved;                  // +0x03
  u32 payload_size;             // +0x04-0x07: Message size
  
  // Offset 0x10: Socket
  SOCKET socket_handle;         // +0x10 (4 bytes)
  
  // Offset 0x14-0xAD: Inbound packet buffer
  char recv_buffer[0x9A];       // +0x14 (154 bytes, max packet)
  u16 recv_offset;              // +0xAE: Bytes received counter
  u16 payload_length_field;     // +0xB0: Length field from packet
  
  // Offset 0x14C: Timeout
  DWORD timeout_deadline;       // +0x14C: GetTickCount() + 300000ms
  
  // Offset 0x150+: Packet queues (linked lists)
  // Each queue entry is 153 bytes (0x99)
  // Incoming pool: +0x150 to +0x132150 (8192 entries, 1.2 MB)
  // Outgoing pool: +0x132150 to +0x2641F0 (8192 entries, 1.2 MB)
  
  u32 incoming_queue_head;      // +0x2641EC: Head of input queue
  u32 outgoing_queue_head;      // +0x2641F0: Head of output queue
  u32 incoming_free_list;       // +0x2641F4: Free list for input pool
  u32 outgoing_free_list;       // +0x2641F8: Free list for output pool
};
```

### Per-Packet Queue Entry

```c
struct QueueEntry {
  u8 opcode;                    // +0x00: Message type
  u16 data_size;                // +0x01-0x02: Actual payload size
  u8 sender_slot;               // +0x03: Which client sent this (written by dispatcher)
  u32 target_client;            // +0x04-0x07: Target (0xFFFFFFFF = broadcast)
  u8 payload[0x8D];             // +0x08-0x94: Packet payload (141 bytes max)
  u32 prev_ptr;                 // +0x95: Doubly-linked list
  u32 next_ptr;                 // +0x99: (Total: 0x99 = 153 bytes)
};
```

---

## Packet Reception Pipeline

### Receive Handler (0x42861C - VIBE_RecvPacketData)

**Called By**: sub_428864() for each active connection  
**Purpose**: Accumulate bytes from socket into per-connection buffer

**Algorithm**:

```c
int VIBE_RecvPacketData(ClientConnection *conn) {
  SOCKET sock = conn->socket_handle;
  char *buf = conn->recv_buffer;
  int *recv_count = &conn->recv_offset;
  
  // Step 1: Ensure we have at least 3 bytes (header)
  while (*recv_count < 3) {
    int n = recv(sock, &buf[*recv_count], 3 - *recv_count, 0);
    
    if (n > 0) {
      *recv_count += n;
    } else {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        return 0;  // Non-blocking, try again next tick
      } else {
        return -1;  // Error, cleanup connection
      }
    }
    
    if (GetTickCount() > conn->timeout_deadline) {
      return -1;  // Timeout, cleanup
    }
  }
  
  // Step 2: Parse opcode and get expected size
  u8 opcode = buf[0];
  u16 length = buf[1] | (buf[2] << 8);  // Little-endian u16
  int expected_size = VIBE_GetPacketSize(opcode);  // sub_403F44
  
  // Step 3: Accumulate remaining bytes
  while (*recv_count < expected_size) {
    int n = recv(sock, &buf[*recv_count], expected_size - *recv_count, 0);
    
    if (n > 0) {
      *recv_count += n;
    } else {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        return 0;  // Partial packet, wait for more
      } else {
        return -1;  // Error
      }
    }
    
    if (GetTickCount() > conn->timeout_deadline) {
      return -1;  // Timeout
    }
  }
  
  // Step 4: Packet complete
  conn->state_flags |= 0x80;  // Set data_ready flag
  *recv_count = 0;            // Reset for next packet
  return 0;                    // Success
}
```

**Error Handling**:
- `WSAEWOULDBLOCK (10035)`: Non-fatal, return 0 (non-blocking I/O)
- Other errors: Call sub_428EF0() (error logging), sub_4285EC() (cleanup), return -1
- Timeout (5 min): Cleanup connection, return -1

---

## Message Transmission Pipeline

### Send Accumulator (0x428C04 - VIBE_QueueOutboundMsg)

**Purpose**: Queue outbound packet in connection's send buffer  
**Called By**: sub_428864() (game responses), various game handlers

**Algorithm**:

```c
int VIBE_QueueOutboundMsg(ClientConnection *conn, char *packet) {
  // 1. Get next free buffer from output pool
  QueueEntry *entry = allocate_from_pool(conn->outgoing_free_list);
  if (!entry) {
    return -1;  // Pool exhausted
  }
  
  // 2. Copy packet data
  memcpy(entry->payload, packet, 153);
  
  // 3. Set size from lookup table
  u8 opcode = entry->payload[0];
  entry->data_size = VIBE_GetPacketSize(opcode);
  
  // 4. Link to tail of output queue
  if (!conn->outgoing_queue_head) {
    conn->outgoing_queue_head = (u32)entry;
  } else {
    QueueEntry *tail = find_tail(conn->outgoing_queue_head);
    tail->next_ptr = (u32)entry;
  }
  entry->prev_ptr = (u32)tail;
  entry->next_ptr = 0;
  
  return 0;  // Success
}
```

### Send Transmitter (0x42878C - VIBE_SendPacketBytes)

**Called By**: sub_428ACC() (transmit loop)  
**Purpose**: Flush buffered packets via socket.send()

**Algorithm**:

```c
int VIBE_SendPacketBytes(ClientConnection *conn) {
  // 1. Check if buffer already fully sent
  QueueEntry *entry = (QueueEntry*)conn->outgoing_queue_head;
  if (!entry) {
    return 0;  // No data to send
  }
  
  if (entry->flags & 0x01) {
    return 0;  // Already sent
  }
  
  // 2. Send remaining bytes
  char *data = &entry->payload[0];
  int total_len = entry->data_size;
  int sent_count = entry->sent_offset;  // Partial send tracking
  
  int n = send(conn->socket_handle, &data[sent_count], total_len - sent_count, 0);
  
  if (n > 0) {
    entry->sent_offset += n;
    conn->timeout_deadline = GetTickCount() + 300000;  // Reset timeout
    
    // 3. If fully sent, mark and move to next
    if (entry->sent_offset == total_len) {
      entry->flags |= 0x01;  // Mark sent
      entry->sent_offset = 0;
      conn->outgoing_queue_head = entry->next_ptr;  // Dequeue
      
      // Return to free pool
      return_to_pool(conn->outgoing_free_list, entry);
    }
  } else {
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
      return 0;  // Would block, try again next tick
    } else {
      return -1;  // Error
    }
  }
  
  return 0;  // Success
}
```

---

## Game Logic Integration

### Game Tick Processor (0x414C6C - VIBE_GameTick)

**Called By**: StartAddress main loop (~30Hz)  
**Purpose**: Read queued commands, process game logic, synchronize world state

**Flow**:

```c
int VIBE_GameTick() {
  // 1. Decompress game state from global buffer
  void *decompressed = VIBE_DecompressGameState();
    → Validates version (1.1.3)
    → Calls sub_422384() (zlib decompress)
    → Returns decompressed data pointer
  
  // 2. Initialize state reader for this tick
  VIBE_InitStateReader();
    → Resets buffer pointer
    → Prepares for structured reads
  
  // 3. Run game logic subsystems
  // Each reads typed data via VIBE_ReadStateField() ~300+ times
  
  VIBE_GameLogic_Movement();      // 0x412F80: Movement & physics
  VIBE_GameLogic_Objects();       // 0x4130BC: Object management
  VIBE_GameLogic_Entities();      // 0x4135EC: NPC/entity AI
  VIBE_GameLogic_Interactions();  // 0x4139E4: Combat, trades, quests
  VIBE_GameLogic_Updates();       // 0x41433C: Player status, inventory
  
  // 4. Synchronize world to all clients
  VIBE_SyncWorldToClients();      // 0x414A84
    → For each connected client:
    →   Extract delta from game state
    →   Compress using deflate
    →   Queue outbound message (opcode 0x09 or broadcast)
    →   Apply version gates (client version check)
  
  // 5. Finalize state
  VIBE_FinalizeState();           // 0x41C94C
  
  return 0;  // Success
}
```

### Game State Decompression (0x41D0B0 - VIBE_DecompressGameState)

**Flow**:

```c
void* VIBE_DecompressGameState() {
  // Read 10-byte header from global state buffer
  if (header != "1.1.3") {
    return NULL;  // Version mismatch
  }
  
  // Decompress using sub_422384() (zlib)
  void *decompressed = decompress_zlib(compressed_state);
  
  return decompressed;
}
```

### Game State Reader (0x412BE0 - VIBE_ReadStateField)

**Purpose**: Extract typed data from decompressed buffer  
**Called**: ~300+ times per tick

**Signature**:

```c
int VIBE_ReadStateField(void *var_addr, int size_bytes) {
  // Read 'size_bytes' from buffer
  // Update buffer pointer
  // Store at var_addr
  return success;
}
```

---

## Object Management

### Object State Tracking (0x414A84 - VIBE_SyncWorldToClients)

**Maximum**: 764 objects (word_BC5BB0), each 268 bytes

```c
struct GameObject {
  u16 object_id;                // +0x00 (word_BC5BB0[i])
  u8 status_flags;              // +0x02 (byte_BC5BB2[i*2])
                                //   0x06 = loading/init
                                //   0x05 = active/visible
  u8 object_type;               // +0x03
  
  // ... ~256 bytes of game data ...
  
  u32 parent_id;                // +0x41
  u32 first_child;              // +0x5B
  u32 next_sibling;             // +0x5C
  u32 flags;                    // +0x65 (value > 4 resets to 2)
  
  // ... remaining bytes for position, velocity, properties ...
};
```

---

## Global State Variables (Critical)

| Address | Name | Size | Purpose |
|---------|------|------|---------|
| 0x365B7EC | dword_365B7EC | 4 | Expected game data size (opcode 0x08) |
| 0x365B834 | dword_365B834 | 4 | Game data buffer pointer |
| 0x365B7F0 | dword_365B7F0[16] | 64 | Per-client received byte counts |
| 0x1019650 | dword_1019650[16] | 64 | Connection state flags |
| 0x1019664 | byte_1019664[16] | 16 | Message type per connection |
| 0x1019667 | byte_1019667[16] | 16 | Connection index / sender ID |
| 0x1019674 | dword_1019674[16] | 64 | Payload size per connection |
| 0x127D840 | dword_127D840[16] | 64 | Receive buffer pointers |
| 0xBC5BB0 | word_BC5BB0[764] | 1528 | Object ID array |
| 0xBC5BB2 | byte_BC5BB2[1528] | 1528 | Object state flags |
| 0xC2AF9C | dword_C2AF9C | 4 | Active player session ID |
| 0xC2B8F4 | byte_C2B8F4 | 1 | Game state flags |
| 0xC2B562 | dword_C2B562 | 4 | Game tick counter |

---

## Shutdown Flow

### Exit_ (0x427000B0)

**Called By**: Host process (gilde.exe) at shutdown

**Flow**:

```c
void Exit_() {
  if (dword_365B6D8 != NULL) {
    dword_0043D970 = 1;         // Signal shutdown to server thread
    
    while (dword_0043D970 != 0) {
      Sleep(10);                // Wait for acknowledgment
    }
    
    TerminateThread(dword_365B6D8, 0);
    CloseHandle(dword_365B6D8);
  }
  
  // Cleanup
  WSACleanup();
  Close all sockets
  Free all buffers
}
```

---

## Critical Function Reference

| Address | Name | Renamed | Size | Purpose |
|---------|------|---------|------|---------|
| 0x427700 | sub_427700 | VIBE_ServerMainLoop | 4KB+ | Main event loop entry |
| 0x428038 | sub_428038 | VIBE_InitNetworking | 2KB | Network init |
| 0x428130 | sub_428130 | VIBE_SocketCreate | 1KB | Create TCP socket |
| 0x428384 | sub_428384 | VIBE_AcceptConnections | 2KB | Accept pending connections |
| 0x42861C | sub_42861C | VIBE_RecvPacketData | 1.5KB | Receive packet bytes |
| 0x428864 | sub_428864 | VIBE_ProcessConnections | 3KB | Main RX/TX state machine |
| 0x428B6C | sub_428B6C | VIBE_DispatchMessage | 1KB | Queue for dispatch |
| 0x428C04 | sub_428C04 | VIBE_QueueOutboundMsg | 1KB | Queue send buffer |
| 0x428ACC | sub_428ACC | VIBE_TransmitLoop | 1.5KB | Transmit all queued |
| 0x42878C | sub_42878C | VIBE_SendPacketBytes | 1.5KB | Send via socket |
| 0x403F44 | sub_403F44 | VIBE_GetPacketSize | 2KB | Size lookup by opcode |
| 0x417070 | sub_417070 | VIBE_SendUDPBroadcast | 1KB | UDP discovery |
| 0x414C6C | sub_414C6C | VIBE_GameTick | 3KB | Game logic processor |
| 0x41D0B0 | sub_41D0B0 | VIBE_DecompressGameState | 2KB | State decompression |
| 0x412BE0 | sub_412BE0 | VIBE_ReadStateField | 1KB | Typed field reader |
| 0x414A84 | sub_414A84 | VIBE_SyncWorldToClients | 2KB | World sync broadcast |
| 0x416480 | sub_416480 | VIBE_AllocGameDataBuffer | 1KB | Allocate game buffer |
| 0x4285EC | sub_4285EC | VIBE_CloseConnection | 1.5KB | Cleanup & close |
| 0x428EF0 | sub_428EF0 | VIBE_ErrorHandler | 1KB | WSA error logging |

---

## Known Limitations

1. **Message Handler Dispatch**: Exact implementation of handlers for opcodes 0x10-0x5D not fully documented
2. **Game State Format**: Complete field-by-field layout of decompressed state unknown (300+ reads per tick)
3. **Object Structures**: Full 268-byte object descriptor layout inferred but not 100% verified
4. **Encryption**: No crypto imports found; packets appear plaintext
5. **Performance Optimization**: Memory allocation patterns and buffer reuse not fully analyzed

---

END OF SECTION
