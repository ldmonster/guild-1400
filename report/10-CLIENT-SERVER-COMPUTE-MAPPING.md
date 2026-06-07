# Client ↔ Server Computing Functions Mapping

**Purpose**: Map all computation functions between gilde.exe (client) and server.dll  
**Analysis Type**: Function-to-function correlation and data flow  
**Scope**: Game logic, physics, animations, interactions across network boundary  

---

## Overview: Client vs Server Computation Split

### Client-Side Computing (gilde.exe)
- **UI Rendering**: Direct game state visualization
- **Input Processing**: Player commands → opcodes
- **Local Prediction**: Anticipatory client-side updates
- **Animation**: Frame interpolation, sprite blitting
- **Sound**: Audio playback for local events
- **Camera**: View frustum calculation, coordinate transforms

### Server-Side Computing (server.dll)
- **Authoritative State**: Game world master copy
- **Physics**: Collision detection, terrain, gravity
- **AI Logic**: NPC behavior, pathfinding
- **Combat**: Damage calculation, hit detection
- **Economies**: Trading, crafting, building
- **Persistence**: Save/load game state

---

## Critical Computing Functions

### Phase 1: Movement Computing

**Client-Side**: VIBE_GameLogic_Movement (0x412F80)
```
Purpose: Track local movement prediction
Input: Player position, heading, velocity
Output: Predicted next position
Sends to Server: Movement opcode (0x10-0x15) with direction/speed
```

**Server-Side**: Movement Handler (server.dll)
```
Purpose: Validate and process movement
Input: Opcode + parameters from client
Validate: Terrain collision, speed limits, obstacles
Output: Accept/reject movement, broadcast to other clients
```

**Data Exchange**:
```
Client → Server:
  0x10: Move direction (byte: 0-7 = 8 directions)
  0x11: Move speed (byte: 0-9 = speed table)
  0x12: Run flag (boolean)
  0x13: Mount check (boolean)
  
Server → Client:
  0x09: State update with final position
  0x0A: Collision rejection (revert position)
```

---

### Phase 2: Object Creation

**Client-Side**: VIBE_GameLogic_Objects (0x4130BC)
```
Purpose: Allocate local object representation
Input: Opcode + object parameters
Output: Object slot in local pool (512 max)
Calls: sub_40EAF0() for 3D model loading
```

**Server-Side**: Object Creation Handler
```
Purpose: Validate and create authoritative object
Input: Create Object opcode (0x20-0x25)
Validate: Owner permissions, resource costs, space
Output: Assign object ID, broadcast creation
```

**Data Exchange**:
```
Client → Server:
  0x20: Create Building (requires position, type, owner)
  0x21: Create Item (requires position, type, quantity)
  0x22: Create NPC (requires position, type, name)
  0x23: Destroy Object (object ID)
  
Server → Client:
  0x09: Object creation state packet
       ├─ Object ID (4B)
       ├─ Position (3×4B)
       ├─ Type (1B)
       └─ Owner ID (4B)
```

---

### Phase 3: Entity State Updates

**Client-Side**: VIBE_GameLogic_Entities (0x4135EC)
```
Purpose: Decompress and apply server state
Input: 0x09 packet (zlib-compressed entity data)
Processing:
  1. VIBE_Decompressor_Init()
  2. VIBE_DecompressState_Blob() ← zlib inflate
  3. Entity type branch (17+ handlers)
  4. Call type-specific handlers
  5. VIBE_Decompression_Finalize()
Output: Updated local entity array
```

**Server-Side**: State Compression
```
Purpose: Compress entity state for transmission
Input: Current game state (all entities)
Processing:
  1. Filter visible entities (frustum culling)
  2. Delta-compress (only changes since last packet)
  3. zlib compress at level 6
  4. Split into 128-byte chunks
Output: 0x09 packets queued for each client
```

**Compression Format** (per entity):
```
Entity Record (684 bytes total):
  [Offset  0-407]: Entity array (102 * 4B)
  [Offset 408]:    Allocation flag (must be 1)
  [Offset 412]:    Entity pointer
  [Offset 416]:    Decompression context
  [Offset 420]:    Child count
  [Offset 424-839]: Child pointers (4B each, 105 max)
  
Type-Specific Data (within entity record):
  [Offset 24]: Entity type (0x01-0x69)
  [Offset 40]: Action flag (bool)
  [Offset 60]: Movement state (int)
  [Offset 64]: Movement counter (int)
  [Offset 88]: Combat flag (bool)
  [Offset 92]: Special action (bool)
  [Offset 116]: Animation ID (4B)
  [Offset 120]: Action byte (char)
```

---

### Phase 4: Interaction Computing

**Client-Side**: VIBE_GameLogic_Interactions (0x4139E4)
```
Purpose: Detect and prepare user interactions
Input: Mouse click at (X, Y)
Processing:
  1. Test collision vs interactive entities
  2. Determine interaction type:
     - Talk to NPC
     - Open door
     - Use lever
     - Attack enemy
     - Trade with vendor
  3. Queue interaction command
Output: Send interaction opcode to server
```

**Server-Side**: Interaction Handler
```
Purpose: Validate and execute interaction
Input: Interaction opcode (0x30-0x3F)
Validate: Distance, permissions, preconditions
Processing:
  1. Check interaction type (talk/combat/trade/build)
  2. Call type-specific handler
  3. Modify game state if valid
  4. Generate response packet
Output: 0x09 state update or error response
```

**Interaction Opcodes**:
```
Client → Server:
  0x30: Talk to NPC (NPC_ID)
  0x31: Attack entity (TARGET_ID, weapon)
  0x32: Cast spell (SPELL_ID, target)
  0x33: Use item (ITEM_ID)
  0x34: Trade offer (ITEMS[], prices[])
  0x35: Build structure (BLUEPRINT_ID, position)
  0x36: Gather resource (RESOURCE_TYPE, quantity)
  0x37: Open container (CONTAINER_ID)
  
Server → Client:
  0x09: Result state packet
  0x0B: Interaction result (success/failure)
  0x0C: Dialogue text (for NPC)
  0x0D: Trade offer (vendor response)
```

---

### Phase 5: Update Broadcasting

**Client-Side**: VIBE_GameLogic_Updates (0x41433C) - Receive
```
Purpose: Apply all received state updates
Input: Incoming 0x09 packets
Processing:
  1. VIBE_Decompressor_Init()
  2. VIBE_DecompressState_Blob()
  3. For each entity in update:
     a. Call VIBE_Decompression_Finalize()
     b. Branch on entity type (17+ handlers)
     c. Update local copy
     d. Queue animation/sound effects
  4. VIBE_Result_Broadcast() to UI
Output: Updated game state, rendered next frame
```

**Server-Side**: State Broadcasting
```
Purpose: Send state updates to all clients
Processing per client per tick:
  1. Build entity update list (only visible entities)
  2. Compress with zlib (level 6)
  3. Split into 128-byte chunks
  4. Queue as 0x09 packets
  5. Track acknowledgement (ACK via 0x04)
Output: Network packets sent to all connected clients
```

**Broadcast Optimization**:
```
- Only send CHANGED entities (delta compression)
- Only send VISIBLE entities (frustum culling)
- Use zlib compression (ratio: 3-5:1)
- 128-byte chunks fit in single TCP packet
- Max 16 clients * 30 updates/sec = 480 packets/sec
- Actual bandwidth: ~8-50 KB/s per client
```

---

## Function Call Chain: Client → Server → Client

```
┌─────────────────────────────────┐
│  User Input (Mouse/Keyboard)    │
│ at 0x412BF4 (VIBE_InitStateReader)
└──────────────┬──────────────────┘
               │
               ▼
         ┌─────────────┐
         │ Interaction │
         │  Opcode      │
         │ (0x30-0x3F)  │
         └──────┬──────┘
                │
                ▼
         ┌──────────────────┐
         │ TCP Socket.send()│
         │ (wsock32.send)   │
         └──────┬───────────┘
                │
          Network Packet
                │
                ▼ [SERVER-SIDE]
          ┌──────────────────┐
          │  Receive Packet  │
          │  (sub_428864)    │
          └──────┬───────────┘
                 │
                 ▼
         ┌──────────────────┐
         │  Dispatch Opcode │
         │ (sub_428B6C)     │
         └──────┬───────────┘
                 │
                 ▼
         ┌─────────────────────┐
         │  Type-Specific      │
         │  Command Handler    │
         │ (40+ handlers)      │
         └──────┬──────────────┘
                 │
                 ▼
         ┌─────────────────────┐
         │  Update Game State  │
         │  Validate & Compute │
         │  (5-phase tick)     │
         └──────┬──────────────┘
                 │
                 ▼
         ┌─────────────────────┐
         │  Compress State     │
         │  (zlib level 6)     │
         └──────┬──────────────┘
                 │
                 ▼
         ┌──────────────────┐
         │  Queue 0x09      │
         │  Packets (128B)  │
         └──────┬───────────┘
                │
          Network Packet
                │
                ▼ [CLIENT-SIDE]
         ┌──────────────────┐
         │  Receive Packet  │
         │  (wsock32.recv)  │
         └──────┬───────────┘
                │
                ▼
         ┌──────────────────────┐
         │ VIBE_Decompressor_   │
         │ Init()               │
         └──────┬───────────────┘
                │
                ▼
         ┌──────────────────────┐
         │ VIBE_DecompressState_│
         │ Blob() [zlib]        │
         └──────┬───────────────┘
                │
                ▼
         ┌──────────────────────┐
         │ Entity Type Branching│
         │ (17+ handlers)       │
         └──────┬───────────────┘
                │
                ▼
         ┌──────────────────────┐
         │ Update Local Copy    │
         │ VIBE_Animation_*()   │
         │ VIBE_Physics_*()     │
         └──────┬───────────────┘
                │
                ▼
         ┌──────────────────────┐
         │ Render Next Frame    │
         │ (DirectDraw)         │
         └──────────────────────┘
```

---

## Computation Functions by Category

### Movement Computation

| Function | Location | Purpose | Bidirectional |
|----------|----------|---------|---------------|
| VIBE_GameLogic_Movement | Client 0x412F80 | Predict movement | Sends 0x10-0x13 |
| VIBE_Velocity_Apply | Client 0x5D883C | Apply velocity vector | Server validates |
| VIBE_Coord_Transform | Client 0x5D8B00 | Transform coordinates | Coordinate sync |
| Movement Handler | Server | Process move command | Broadcasts 0x09 |
| Collision Checker | Server | Validate terrain | Rejects via 0x0A |

### Animation Computation

| Function | Location | Purpose | Bidirectional |
|----------|----------|---------|---------------|
| VIBE_Animation_Basic | Client 0x5D85B8 | Play base animation | Server sends frame |
| VIBE_Animation_Advanced | Client 0x5D89BC | Combat/special anim | Server sets flag |
| VIBE_AnimationFlags_Compute | Client 0x41DC74 | Compute frame offset | Server provides flags |
| VIBE_FrameData_Process | Client 0x5D781C | Interpolate frames | 30Hz timing |
| Animation Handler | Server | Sync animation state | Sends via 0x09 |

### Physics Computation

| Function | Location | Purpose | Bidirectional |
|----------|----------|---------|---------------|
| VIBE_Physics_Update | Client 0x5D8E80 | Apply gravity | Server authoritative |
| Collision Detection | Server | Terrain + object | Rejects movement |
| Damage Calculation | Server | Combat math | 0x09 health update |
| Status Effects | Server | Buff/debuff logic | 0x09 state change |

### Interaction Computation

| Function | Location | Purpose | Bidirectional |
|----------|----------|---------|---------------|
| VIBE_Interaction_Handler | Client 0x411F8C | Prepare interaction | Sends 0x30-0x37 |
| VIBE_Entity_InteractionLogic | Client 0x41078C | Process target | Server validates |
| NPC Talk Handler | Server | Dialogue logic | 0x0C response |
| Combat Handler | Server | Damage/hit logic | 0x09 state change |
| Trade Handler | Server | Economy logic | 0x0D offer |

### State Synchronization

| Function | Location | Purpose | Bidirectional |
|----------|----------|---------|---------------|
| VIBE_DecompressGameState | Client 0x41D0B0 | Decompress state | Receives 0x09 |
| VIBE_Decompressor_Init | Client 0x40E50C | Initialize zlib | Per-packet |
| VIBE_DecompressState_Blob | Client 0x423500 | Inflate compressed | Decompress routine |
| State Compression | Server | Compress state | Sends 0x09 |
| Delta Compressor | Server | Only changes | ~60% reduction |

---

## Network Packet Flow with Computation

### Movement Command Example

```
PLAYER CLICKS MOVE DIRECTION
    ↓
Client: VIBE_InitStateReader()
    │   ├─ Detect input (byte_67225C = direction)
    │   ├─ Validate terrain (no obstacles)
    │   └─ Queue movement opcode
    ↓
Client: Build opcode 0x10
    │   ├─ Direction (1B)
    │   ├─ Position (6B: X,Y as shorts)
    │   └─ Timestamp (4B)
    ↓
Client: wsock32.send() [11-byte packet]
    ↓
Server: sub_428864() [Receive]
    │   ├─ Parse header (opcode 0x10)
    │   └─ Queue for dispatcher
    ↓
Server: sub_428B6C() [Dispatch]
    │   ├─ Look up handler for 0x10
    │   └─ Call movement handler
    ↓
Server: Movement Handler
    │   ├─ Validate direction
    │   ├─ Check terrain collision
    │   ├─ Check entity collision
    │   ├─ Apply speed modifier
    │   ├─ Update dword_62D204[entity_offset + 60]
    │   └─ Set movement_counter = ticks_needed
    ↓
Server: Next tick (30ms later)
    │   ├─ VIBE_GameLogic_Movement() runs
    │   ├─ Decrements movement_counter
    │   ├─ If counter ≤ 0: movement complete
    │   └─ Prepares state for broadcast
    ↓
Server: State Compression (per client)
    │   ├─ Collect changed entities
    │   ├─ Include player's new position
    │   ├─ zlib compress (level 6)
    │   ├─ Split into 128B chunks
    │   └─ Queue as 0x09 packets
    ↓
Server: wsock32.send() [128+ byte packets]
    ↓
Client: wsock32.recv() [Get 0x09 packet]
    │   ├─ Extract packet header
    │   ├─ Buffer compressed data
    │   └─ When complete, decompress
    ↓
Client: VIBE_Decompressor_Init()
    │   └─ Prepare zlib stream
    ↓
Client: VIBE_DecompressState_Blob()
    │   ├─ zlib inflate
    │   ├─ Parse entity records
    │   ├─ For player entity:
    │   │   ├─ Get new position
    │   │   ├─ Call VIBE_Animation_Basic() for walk
    │   │   └─ Update dword_62D204
    │   └─ For other entities:
    │       ├─ Apply type-specific handlers
    │       └─ Update local copies
    ↓
Client: VIBE_GameLogic_Updates()
    │   ├─ Call VIBE_Result_Broadcast()
    │   └─ Mark frame dirty
    ↓
Client: Render Thread
    │   ├─ BlitObject(player_pos)
    │   ├─ Draw walk animation frame
    │   └─ Present to screen
```

---

## Computing Resource Distribution

### Per Tick (33.33ms budget)

**Client-Side** (~8-10ms):
- Input processing: 0.5ms
- Local collision: 1-2ms
- Animation frame: 1-2ms
- Decompression: 2-3ms
- Rendering: 3-4ms

**Server-Side** (~15-20ms for 16 players):
- Receive packets: 1-2ms
- Dispatch commands: 1-2ms
- Command handlers: 5-10ms
- 5-phase game tick: 3-5ms
- Compression: 1-2ms
- Send packets: 1-2ms

**Network** (~0-100ms latency):
- Average LAN: 5-20ms
- Modem: 50-150ms
- Internet: 10-100ms

---

## Critical Computing Bottlenecks

### Client-Side
1. **Decompression** (2-3ms)
   - zlib at level 6 is slow
   - Could use level 1-3 for speed
   
2. **Animation Interpolation** (1-2ms)
   - 17+ entity types × 512 objects
   - Could batch or LOD distant objects
   
3. **Collision Detection** (1-2ms)
   - Terrain heightmap lookup
   - Object bounding box checks

### Server-Side
1. **Compression** (1-2ms)
   - zlib compression per client
   - Could use threading or pre-compute
   
2. **Game Logic** (5-10ms)
   - 5-phase tick with complex branching
   - 40+ command handlers
   
3. **Network I/O** (1-2ms)
   - TCP send/recv for 16 clients
   - Could batch packets

---

## Optimization Opportunities

**Reduce Compression Overhead**:
- Use zlib level 1-3 instead of 6
- Pre-compute static state
- Delta-compress only changed bytes

**Optimize Entity Processing**:
- LOD system for distant entities
- Skip invisible entities
- Early culling before type dispatch

**Batch Network Operations**:
- Accumulate 0x09 packets
- Send multiple updates per packet
- Use UDP for latency-critical updates

---

## Implementation Checklist

For custom server that mirrors gilde.exe computing:

- [ ] Movement computation (phase 1)
- [ ] Object pool management (phase 2)
- [ ] Entity state updates (phase 3)
- [ ] Interaction processing (phase 4)
- [ ] State broadcasting (phase 5)

For optimization:

- [ ] Implement delta compression (only changes)
- [ ] Add frustum culling (visible entities only)
- [ ] Implement LOD system (distant objects)
- [ ] Optimize zlib level (1-3 vs 6)
- [ ] Thread compression for multi-core

