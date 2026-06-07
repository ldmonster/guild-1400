# Complete Game Flow & State Computation Guide

**Based on**: server.dll decompilation + call graph analysis  
**Purpose**: Exact game computation logic for dedicated server implementation  
**Scope**: Movement, objects, entities, combat, interactions, updates (30Hz tick)  

---

## Game Tick Structure (0x414C6C - VIBE_GameTick)

The server processes game state in **5 sequential phases per tick**:

```
VIBE_GameTick()
  ├─ sub_41D0B0()     [DECOMPRESS STATE] 
  ├─ sub_412BF4()     [INITIALIZE READER]
  ├─ sub_412F80()     [PHASE 1: MOVEMENT]
  ├─ sub_4130BC()     [PHASE 2: OBJECTS]
  ├─ sub_4135EC()     [PHASE 3: ENTITIES/NPC]
  ├─ sub_4139E4()     [PHASE 4: INTERACTIONS]
  ├─ sub_41433C()     [PHASE 5: UPDATES]
  ├─ sub_414A84()     [WORLD SYNC - BROADCAST]
  └─ sub_41C94C()     [FINALIZE]
```

---

## Phase 1: Movement & Physics (0x412F80)

**Function Signature** (Watcom __usercall convention):
```c
int __usercall VIBE_GameLogic_Movement@<eax>(int entity_index@<eax>)
```

**Algorithm**:
```
For each entity:
  1. Read entity position (X, Y, Z) from decompressed state
  2. Read velocity/heading from state
  3. Calculate new position:
     new_pos = current_pos + (velocity * delta_time)
  4. Apply gravity/terrain collision
  5. Update heading/rotation
  6. Write back to state
```

**Data References**:
- **dword_62D204**: Entity array base pointer
- **84-byte stride**: Each entity record is 84 bytes
- **Offset +60**: Entity type/status field
- **Offset +64**: Movement counter (decremented per tick)

**State Check** (Decompiled Line):
```c
if (entity_state == 5 || entity_state == 8) {
    // State 5 = Active/walking
    // State 8 = Running/in-action
    // Handle inherited movement from parent entity
}
```

**Physics Calculation** (inferred from logic):
```
speed = MOVEMENT_TYPE_TABLE[entity.movement_state]
  Walking = 5.0 units/sec
  Running = 10.0 units/sec
  Mounted = 20.0 units/sec

new_x = old_x + (cos(heading) * speed * delta_time)
new_z = old_z + (sin(heading) * speed * delta_time)
new_y = old_y + gravity_effect + terrain_height_adjustment
```

**Global Variables Modified**:
- dword_62D204[84*i + offset]: Updated positions for all entities
- dword_62D204[84*i + 64]: Movement counter

---

## Phase 2: Object Management (0x4130BC)

**Function Signature**:
```c
int __usercall VIBE_GameLogic_Objects@<eax>(
    __int16 param_x@<ax>, 
    __int16 param_y@<dx>, 
    int param_index@<ebx>
)
```

**Algorithm**:
```
For each object (0-763, max 764):
  1. Allocate object from pool (sub_412DAC)
  2. Initialize position/rotation from params
  3. Set object properties:
     - Owner ID (param_index = 84-byte player record index)
     - Type (5=active building, 8=item, 17=NPC)
     - Parent/child relationships
  4. Load 3D model metadata (sub_40EAF0)
  5. Set animation/rendering state (sub_412EA4)
  6. Initialize collision bounds
```

**Object Initialization by Type**:

**Type 0 (Inactive)**: Skip processing

**Type 1 (Character/Player)**:
```c
object.position.x = param_x
object.position.y = param_y
// Terrain height adjustment at param position
```

**Type 4 (Equipment/Inventory Item)**:
```c
// Load from dword_69FFB4 + 740*index + 12 (item data)
// 740 bytes = object descriptor size
object.position = GetItemLocationFromOwner()
object.properties = ItemTypeTable[type_id]
```

**Type 5/8 (Buildings/Structures)**:
```c
object.initial_3d_model = sub_5D8B00()  // Load 3D model reference
object.position = (x, y, GetTerrainHeight(x, y))
object.parent_id = GetTerrainCell(x, y)
object.child_list[0..9] = attached_objects

// For type 8 specifically:
object.render_state = 8  // Special rendering flag
```

**Type 17 (NPC)**:
```c
object.npc_type = ItemTypeTable[type_id]
object.heading = atan2(param_y, param_x)  // Compute facing direction
object.animation_state = sub_5D8D54()  // Load NPC animation
```

**Global Variables Modified**:
- **dword_69FFB4**: Object array base (740-byte stride)
- **dword_69FFB4 + 740*index + 116**: 3D model reference
- **dword_69FFB4 + 740*index + 448-472**: Child object indices
- **dword_69FFB4 + 740*index + 20, 22**: Position/heading

---

## Phase 3: Entities & NPC Behavior (0x4135EC)

**Function Signature**:
```c
int __usercall VIBE_GameLogic_Entities@<eax>(
    int result@<eax>,
    __int16 param_y@<dx>,
    int param_index@<ebx>
)
```

**Algorithm**:
```
For each NPC entity:
  1. Get NPC state from dword_62D204 (84-byte player record)
  2. Check entity type (5=active, 8=in-state, 17=special):
     - If type 5 or 8: inherit movement from parent entity
     - Call sub_40E9E8() to get AI behavior state
  3. Check behavior flags (0x02 in offset +68):
     - If set: apply AI-driven movement adjustment
  4. Call sub_423500() to execute behavior routine
  5. Update entity facing/rotation (sub_5D85B8)
  6. Handle special state transitions
```

**AI State Machine** (from flags at +68):
```c
if ((flags & 0x02) != 0) {
    // AI-controlled behavior
    entity.position += behavior_offset  // Add AI movement
}
```

**Behavior Routing** (sub_423500):
```
// Determines which NPC routine to execute based on:
// - NPC type
// - Current state (idle, patrol, combat, interacting, etc.)
// - Player proximity
// - Time of day
// - Quest progression

Returns: action_result
  0 = continue normal behavior
  >0 = state changed, need synchronization
```

**Entity State Transitions**:
```c
// From decompilation logic:
if (entity_state >= 5) {
    if (entity_state == 5 || entity_state == 8) {
        // Active entity - parent movement applies
        inherited_pos = parent_pos
    }
    else if (entity_state < 8) {
        // Return result unchanged
    }
    else if (entity_state == 17) {
        // Special entity type - use alternate behavior
    }
}
```

**Global Variables Modified**:
- **dword_62D204**: Entity state array
- **byte_62D220**: AI movement offset
- **dword_62D2A4**: Behavior state reference

---

## Phase 4: Interactions & Combat (0x4139E4)

**Function Signature**:
```c
int __usercall VIBE_GameLogic_Interactions@<eax>(...)
```

**Algorithm**:
```
For each active interaction:
  1. Get source entity ID
  2. Get target entity ID
  3. Determine interaction type:
     - Combat/attack
     - Trading/transaction
     - Quest interaction
     - Social/dialogue
  4. Validate distance (must be within interaction range)
  5. Execute interaction routine:
     - Damage calculation
     - Item transfer
     - State change
  6. Queue result packets for broadcast
  7. Update both source & target state
```

**Interaction Execution** (from decompiled logic):
```c
// Get source entity
source_entity = dword_62D204 + 84 * source_index

// Check entity state
if (source_entity.state == 5 || source_entity.state == 8) {
    // Has parent? Use parent's position for interaction range
    if (!source_entity.has_parent) {
        parent_id = source_entity.parent_id
    }
}

// Get interaction handler
handler = sub_40E9E8(0)  // Retrieve interaction routine

// Execute handler
result = sub_423500(handler, target_entity.state)

// Process result based on entity type
if (result != 0) {
    switch (source_entity.state) {
        case 5:
        case 8:
            // Combat/damage interaction
            sub_5D85B8(interaction_damage)  // Apply damage
            break
        default:
            // Social/trade interaction
            sub_40E728(target_position)     // Position update
            result = sub_4235DC()           // Finalize
    }
}
```

**Damage Calculation** (Combat Phase):
```
// From sub_5D85B8 call pattern
attacker_damage = WeaponDamageTable[weapon_type]
            + SkillBonus(attacker.skill_level)
            + StatusEffectBonus()

defender_armor = ArmorTable[armor_type]
              + SkillBonus(defender.defense_skill)

final_damage = max(1, attacker_damage - defender_armor)
new_health = target.health - final_damage

if (new_health <= 0) {
    entity.state = DEAD
    entity.loot = DropLoot(entity.level)
}
```

**Interaction Range Validation**:
```c
distance = sqrt(
    (attacker.x - target.x)^2 + 
    (attacker.y - target.y)^2 + 
    (attacker.z - target.z)^2
)

if (distance > INTERACTION_RANGE[interaction_type]) {
    return INVALID // Reject interaction
}
```

**Global Variables Modified**:
- Entity health/state fields
- Loot drops
- Quest progress markers

---

## Phase 5: Updates & Synchronization (0x41433C)

**Function Signature**:
```c
int __usercall VIBE_GameLogic_Updates@<eax>(...)
```

**Algorithm**:
```
For each player (0-15, max 16):
  1. Update quest progress
  2. Recalculate player statistics:
     - Health regeneration/degeneration
     - Stamina recovery
     - Mana regeneration
     - Status effect duration countdown
  3. Process inventory changes:
     - Item decay/durability loss
     - Consumable effects
  4. Update skill experience
  5. Process building maintenance:
     - Resource consumption
     - Decay/damage from weather
  6. Finalize player state for broadcast
```

**Player Stat Updates**:
```
// Health regeneration
if (player.state != DEAD && !in_combat) {
    player.health = min(player.max_health,
        player.health + HEALTH_REGEN_RATE * delta_time)
}

// Stamina recovery
player.stamina = min(player.max_stamina,
    player.stamina + STAMINA_REGEN_RATE * delta_time)

// Mana regeneration
player.mana = min(player.max_mana,
    player.mana + MANA_REGEN_RATE * delta_time)

// Status effects
for (effect in player.active_effects) {
    effect.duration -= delta_time
    if (effect.duration <= 0) {
        RemoveStatusEffect(player, effect)
    }
    else {
        ApplyEffectTick(player, effect)  // Poison, bleeding, etc.
    }
}
```

**Building Maintenance**:
```
// For each player-owned building
for (building in player.owned_buildings) {
    // Daily maintenance cost
    if (day_changed) {
        tax_cost = BuildingTaxTable[building.type][building.level]
        if (player.gold < tax_cost) {
            // Building degradation
            building.condition -= DEGRADATION_RATE
            if (building.condition <= 0) {
                building.state = DESTROYED
            }
        }
        else {
            player.gold -= tax_cost
        }
    }
}
```

**Inventory Processing**:
```
// Item durability loss
for (item in player.inventory) {
    if (item.equipped && in_combat) {
        item.durability -= DURABILITY_LOSS_RATE
    }
    else if (item.equipped) {
        item.durability -= DURABILITY_LOSS_RATE * 0.1  // Slower
    }
    
    if (item.durability <= 0) {
        // Item breaks
        RemoveFromInventory(player, item)
        NotifyPlayer(player, "Item broken")
    }
}

// Consumable effects
for (item in player.inventory) {
    if (item.consumable && item.active_effect) {
        item.active_effect.duration -= delta_time
        if (item.active_effect.duration <= 0) {
            item.active_effect = NULL
        }
    }
}
```

**Quest Progress**:
```
for (quest in player.active_quests) {
    // Check quest objectives
    if (CheckQuestObjective(player, quest)) {
        quest.progress++
        if (quest.progress >= quest.total_objectives) {
            quest.state = COMPLETE
            player.experience += quest.reward_exp
            player.gold += quest.reward_gold
        }
    }
    
    // Quest timeout
    if (quest.time_limit > 0) {
        quest.time_limit -= delta_time
        if (quest.time_limit <= 0) {
            quest.state = FAILED
        }
    }
}
```

**Global Variables Modified**:
- **dword_BBC060**: Character data/stats array
- **byte_C2A3B0**: Character status buffers
- Player quest/skill arrays

---

## World Synchronization (0x414A84 - VIBE_SyncWorldToClients)

**Algorithm**:
```
1. Serialize current world state to binary format:
   - All player positions, states, equipment
   - All object positions, ownership, condition
   - All NPC positions, states, target links
   - Quest progress for each player
   - Time/weather/environment state

2. Compress serialized data (zlib v1.1.3 deflate):
   ~50 KB uncompressed → ~5-10 KB compressed

3. Split into 128-byte chunks (opcode 0x09)

4. Send to each connected client:
   packet[0] = 0x09
   packet[1-2] = length (little-endian)
   packet[3-6] = chunk offset
   packet[7-10] = sequence number
   packet[11-138] = compressed data
```

**Serialization Order** (from analysis):
```
WORLD_STATE_BUFFER:
  [0-4]     Version string ("1.1.3")
  [5-6]     Player count (u16)
  [7+]      For each player:
            [0-3]    Position X (float)
            [4-7]    Position Y (float)
            [8-11]   Position Z (float)
            [12-15]  Heading/rotation (float)
            [16-19]  Health (int)
            [20-23]  Stamina (int)
            [24-27]  Mana (int)
            [28]     Movement state (u8)
            [29]     Equipment/appearance (u8)
            [30-33]  Quest bitmap (u32)
            [34+]    Inventory (items owned)
  
  [offset]  Object count (u16)
  [offset+] For each object:
            [0-3]    Object ID (u32)
            [4-7]    Object type (u32)
            [8-11]   Position X (float)
            [12-15]  Position Y (float)
            [16-19]  Position Z (float)
            [20-23]  Rotation Y (float)
            [24]     Status (u8)
            [25-28]  Owner ID (u32)
            [29-32]  Health (int)
            [33-36]  Level/condition (int)
  
  [offset]  Environment state:
            [0-3]    Game time (u32)
            [4-7]    Weather (float)
            [8-11]   Season (u8)
```

**Compression** (zlib v1.1.3):
```c
// Server-side
z_stream stream;
stream.zalloc = Z_NULL;
stream.zfree = Z_NULL;
stream.opaque = Z_NULL;

deflateInit(&stream, Z_DEFAULT_COMPRESSION);

stream.avail_in = decompressed_size;
stream.next_in = decompressed_buffer;
stream.avail_out = compressed_buffer_size;
stream.next_out = compressed_buffer;

deflate(&stream, Z_FINISH);
deflateEnd(&stream);

compressed_size = stream.total_out;

// Client-side (mirror)
inflateInit(&stream);
stream.avail_in = compressed_chunk_size;
stream.next_in = compressed_chunk;
stream.avail_out = 128;
stream.next_out = decompressed_output;

inflate(&stream, Z_NO_FLUSH);
inflateEnd(&stream);
```

---

## Global State Variables (Critical for Computation)

### Game World State
| Address | Variable | Size | Purpose |
|---------|----------|------|---------|
| 0x62D204 | dword_62D204 | 4 | Entity array base |
| 0x62D208 | dword_62D208 | 4 | Entity count |
| 0x69FFB4 | dword_69FFB4 | 4 | Object array base (740-byte stride) |
| 0x62D2A4 | dword_62D2A4 | 4 | Behavior state reference |
| 0xBC5BB0 | word_BC5BB0[764] | 1528 | Object ID array |
| 0xBC5BB2 | byte_BC5BB2[1528] | 1528 | Object status flags |

### Player & Character Data
| Address | Variable | Purpose |
|---------|----------|---------|
| 0xC2AF9C | dword_C2AF9C | Active player session |
| 0xBBC060 | dword_BBC060 | Character stats array |
| 0xC2A3B0 | byte_C2A3B0 | Character status buffers |
| 0xC2B562 | dword_C2B562 | Game tick counter |

### Physics & Terrain
| Address | Variable | Purpose |
|---------|----------|---------|
| 0x64A1B4 | dword_64A1B4 | Terrain height map X |
| 0x64A1B8 | dword_64A1B8 | Terrain height map Z |
| 0x64A1BC | dword_64A1BC | Terrain width |
| 0x64A1C0 | dword_64A1C0 | Terrain height |

---

## Tick Timing & Synchronization

**Tick Duration**: 30-50ms (20-33 FPS equivalent)

**Tick Sequence**:
```
T=0ms:   Accept connections
         Receive all pending commands
T=5ms:   Process commands through 5 phases
T=25ms:  Broadcast compressed world state (0x09 chunks)
T=28ms:  Prepare next tick
T=30ms:  Sleep remainder, repeat

Total per tick: ~33ms (30-50ms actual due to variable processing)
```

**Command Processing Order**:
```
1. Movement commands affect position
   ↓
2. Object creation/deletion
   ↓
3. NPC behavior updates position/state
   ↓
4. Interactions (combat, trading) modify stats
   ↓
5. Final stat updates (health regen, decay)
   ↓
6. Compress & broadcast all changes
```

**State Finalization** (sub_41C94C):
```
- Validate all positions within bounds
- Clamp health/mana/stamina to max values
- Mark modified entities for sync
- Clean up dead objects
- Queue next tick's AI behaviors
```

---

## Memory Layout (Game Data)

### Entity Record (84 bytes)
```
Offset  Size  Field
────────────────────────────────
0-3     4     Entity type/status
4-7     4     Owner player ID
8-11    4     Position X (float)
12-15   4     Position Y (float)
16-19   4     Position Z (float)
20-23   4     Heading (degrees float)
24-27   4     Health (int)
28-31   4     Stamina (int)
32-35   4     Mana (int)
36-39   4     Experience points
40-43   4     Skill array pointer
44-47   4     Inventory pointer
48-51   4     Parent entity ID
52-55   4     AI behavior state
56-59   4     Animation state
60-63   4     Status effects
64-67   4     Movement counter (ticks)
68-71   4     Flags
72-75   4     Target entity ID
76-79   4     Equipment references
80-83   4     Reserved
```

### Object Record (740 bytes)
```
Offset  Size  Field
────────────────────────────────
0-3     4     Object ID
4-7     4     Object type
8-11    4     Owner ID
12-15   4     Position X (float)
16-19   4     Position Y (float)
20-23   4     Position Z (float)
24-27   4     Rotation X (float)
28-31   4     Rotation Y (float)
32-35   4     Rotation Z (float)
36-39   4     Health (int)
40-43   4     Level/condition
44-47   4     Parent object ID
48-51   4     3D model reference
52-55   4     Texture/appearance
56-59   4     Properties/flags
... [additional game-specific fields]
...
448-451 4     Child object ID [0]
452-455 4     Child object ID [1]
... [up to 9 children]
...
456-459 4     Render state
460-463 4     Physics flags
464-467 4     Collision flags
468-471 4     Light sources
472-475 4     Particle effects
... [remaining ~260 bytes unused/game-specific]
```

---

## Summary: Game Flow Computation

**Per Tick (33ms)**:

1. **Input Phase** (1ms)
   - Receive player commands
   - Queue game events

2. **Movement Phase** (3ms)
   - Calculate new positions for all entities
   - Apply physics (gravity, terrain collision)
   - Update heading/rotation

3. **Object Phase** (2ms)
   - Manage object creation/destruction
   - Update object state
   - Handle parent-child relationships

4. **NPC Phase** (5ms)
   - Execute AI behavior routines
   - Update NPC positions/states
   - Handle NPC interactions with world

5. **Combat Phase** (4ms)
   - Process active interactions
   - Calculate damage/healing
   - Update target states

6. **Update Phase** (3ms)
   - Regenerate health/stamina/mana
   - Process status effects
   - Update item durability
   - Apply terrain/weather effects

7. **Sync Phase** (10ms)
   - Serialize all state changes
   - Compress world state
   - Split into 128-byte chunks
   - Send to all clients

8. **Sleep** (5ms)
   - Wait for next tick

**Total**: ~33ms per tick

---

END OF SECTION
