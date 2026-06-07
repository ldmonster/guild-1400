# Complete Five-Phase Game Tick Deep Analysis

**Based on**: Direct decompilation from gilde.exe  
**Source Functions**:
- Phase 1: VIBE_GameLogic_Movement (0x412F80)
- Phase 2: VIBE_GameLogic_Objects (0x4130BC)
- Phase 3: VIBE_GameLogic_Entities (0x4135EC)
- Phase 4: VIBE_GameLogic_Interactions (0x4139E4)
- Phase 5: VIBE_GameLogic_Updates (0x41433C)

**Purpose**: Byte-level implementation details for each phase of the 30Hz game tick

---

## Phase 1: VIBE_GameLogic_Movement (0x412F80)

**Purpose**: Process entity movement and decrement movement counters

**Execution Time**: ~0.5ms per tick  
**Calls Per Tick**: Once per entity  

### High-Level Algorithm

```
For each entity:
  1. Check if entity state is 5 (walking) or 8 (running)
  2. If true: Get parent entity (if mounted)
  3. Get movement counter from entity record (offset +64)
  4. Decrement counter
  5. If counter <= 0: Call sub_438DA8 for movement completion
```

### Assembly-Level Pseudocode

```c
int VIBE_GameLogic_Movement(int entity_index)
{
  // dword_62D204 = global entity array (84-byte stride)
  // Each entity has movement state at offset +60
  
  int entity_offset = 84 * entity_index;
  int entity_data = dword_62D204 + entity_offset;
  
  // Get movement state (offset +60, 4-byte int)
  int movement_state = *(int*)(entity_data + 60);
  
  // Check if entity is walking (5) or running (8)
  if (movement_state == 5 || movement_state == 8) {
    
    // If mounted: get parent entity index
    if (!*(int*)(entity_data + 48)) {  // No direct parent
      entity_index = *(int*)(entity_data + 76);  // Get parent index
    }
    
    // Recalculate offset with parent index
    entity_offset = 84 * entity_index;
  }
  
  // Get movement counter (offset +64)
  int movement_counter = *(int*)(dword_62D204 + entity_offset + 64);
  
  // Decrement counter
  if (movement_counter <= 0) {
    // Movement completed
    return sub_438DA8(movement_state);
  }
  
  // Decrement counter for next tick
  *(int*)(dword_62D204 + entity_offset + 64) = movement_counter - 1;
  
  return dword_62D204 + entity_offset;
}
```

### Critical Variables

| Variable | Offset | Size | Purpose |
|----------|--------|------|---------|
| movement_state | +60 | 4B | 5=walk, 8=run, other values mean stationary |
| parent_ptr | +48 | 4B | Parent entity pointer (mounted) |
| parent_index | +76 | 4B | Parent entity index (if mounted) |
| movement_counter | +64 | 4B | Ticks remaining for current movement |

### Return Value

- Returns entity data base address (dword_62D204 + entity_offset)
- Used by subsequent phases for entity state updates

---

## Phase 2: VIBE_GameLogic_Objects (0x4130BC)

**Purpose**: Allocate objects from pool and initialize 3D model data

**Execution Time**: ~1ms per object created  
**Input Parameters**:
- a1 (@ax): X coordinate (16.16 fixed-point)
- a2 (@dx): Y coordinate (16.16 fixed-point)
- a3 (@ebx): Entity index

### High-Level Algorithm

```
1. Validate entity index (must be < dword_62D208)
2. Allocate object slot from pool (sub_412DAC)
3. Load 3D model metadata (sub_40EAF0)
4. Initialize object:
   - Position (X/Y from parameters)
   - Type (from entity)
   - Rotation fields
5. Type-specific initialization:
   - Type 1: Item pickup
   - Type 4: Coordinate system setup
   - Type 5/8: Movement with coordinate transform
   - Type 17: NPC with AI data
```

### Assembly-Level Pseudocode

```c
int VIBE_GameLogic_Objects(int click_x, int click_y, int entity_index)
{
  // Entity pool management
  int max_objects = dword_62D208;  // Maximum objects
  
  if (entity_index > max_objects) {
    return -1;  // Invalid entity
  }
  
  // Allocate new object from pool
  int object_slot = sub_412DAC();  // Returns object pool index
  if (object_slot >= 512) {
    return -1;  // Pool full
  }
  
  // Load 3D model metadata
  int global_object_addr = dword_69FFB4 + 740 * object_slot;
  sub_40EAF0(global_object_addr, &object_data);  // Load model
  
  // Initialize position
  *(int*)(global_object_addr + 0x0C) = object_data;  // Position X
  *(int*)(global_object_addr + 0x08) = entity_index;  // Owner entity
  
  // Get entity type from entity array
  unsigned char entity_type = *(unsigned char*)(
    dword_62D204 + 84 * entity_index + 60
  );
  
  // Initialize rotation fields
  *(short*)(global_object_addr + 0x1A) = 2;  // Initial rotation
  *(unsigned char*)(global_object_addr + 0x18) = entity_type;
  *(short*)(global_object_addr + 0x10) = click_x;
  *(short*)(global_object_addr + 0x12) = click_y;
  
  // Load default rotation values from globals
  *(short*)(global_object_addr + 0x1C) = dword_64A1B4;
  *(short*)(global_object_addr + 0x1E) = dword_64A1BC;
  *(short*)(global_object_addr + 0x20) = dword_64A1B8;
  *(short*)(global_object_addr + 0x22) = dword_64A1C0;
  
  sub_412EA4();  // Finalize allocation
  
  // === TYPE-SPECIFIC INITIALIZATION ===
  
  if (entity_type == 8) {
    // Type 8: Item/loot
    *(int*)(global_object_addr + 72) = 1;  // Flag item
  }
  
  // Reset animation/state fields
  *(int*)(global_object_addr + 464) = -1;  // Animation frame
  *(int*)(global_object_addr + 468) = 0;   // Animation state
  *(int*)(global_object_addr + 472) = 0;   // Animation counter
  
  // === ENTITY TYPE DISPATCHER ===
  
  switch(entity_type) {
    
    case 0:
      // Type 0: Basic object
      return object_slot;
    
    case 1:
      // Type 1: Item
      {
        int model_data = *(int*)(global_object_addr + 12);
        *(short*)(global_object_addr + 20) = *(short*)(model_data + 44);
        *(short*)(global_object_addr + 22) = *(short*)(model_data + 46);
        return object_slot;
      }
    
    case 4:
      // Type 4: Coordinate transform
      {
        int transform_result = sub_5D8D54(0, click_y >> 16);
        *(int*)(global_object_addr + 116) = transform_result;
        
        int model_data = *(int*)(global_object_addr + 12);
        *(short*)(global_object_addr + 20) = *(short*)(model_data + 44);
        *(short*)(global_object_addr + 22) = *(short*)(model_data + 46);
        return object_slot;
      }
    
    case 5:
    case 8:
      // Type 5/8: Movement entity (walking/running)
      {
        // Complex coordinate transformation
        *(int*)(global_object_addr + 116) = dword_62D2A4;
        
        int model_data = *(int*)(global_object_addr + 12);
        int transform_result = VIBE_Coord_Transform(
          model_data,
          *(short*)(global_object_addr + 116)
        );
        
        // Store transformed coordinates
        *(short*)(global_object_addr + 20) = *(short*)(transform_result + 6);
        *(short*)(global_object_addr + 22) = *(short*)(transform_result + 10);
        
        // Animation frame setup
        *(int*)(global_object_addr + 456) = -1;  // Frame A
        *(int*)(global_object_addr + 460) = -1;  // Frame B
        
        // Store model dimensions
        *(int*)(global_object_addr + 448) = *(unsigned short*)(model_data + 44);
        *(int*)(global_object_addr + 452) = *(unsigned short*)(model_data + 46);
        
        // Set animation bytes
        *(unsigned char*)(global_object_addr + 104) = 8;
        *(unsigned char*)(global_object_addr + 105) = 8;
        
        return object_slot;
      }
    
    case 17:
      // Type 17: NPC with AI
      {
        int model_data = *(int*)(global_object_addr + 12);
        *(short*)(global_object_addr + 20) = *(short*)(model_data + 12);
        *(short*)(global_object_addr + 22) = *(short*)(model_data + 14);
        
        // NPC-specific: Set AI animation to 2
        *(int*)(global_object_addr + 116) = 2;
        
        return object_slot;
      }
    
    default:
      return object_slot;
  }
}
```

### Critical Constants

| Address | Value | Purpose |
|---------|-------|---------|
| dword_64A1B4 | varies | Default rotation X |
| dword_64A1BC | varies | Default rotation Y |
| dword_64A1B8 | varies | Default rotation Z |
| dword_64A1C0 | varies | Default rotation W |

### Global Pool Management

- **dword_62D208**: Maximum objects (typically 764)
- **sub_412DAC()**: Allocates next free slot
- **Returns**: -1 if pool full (>= 512 slots)

---

## Phase 3: VIBE_GameLogic_Entities (0x4135EC)

**Purpose**: Process entity state updates and animation

**Execution Time**: ~1-2ms per entity  

### High-Level Algorithm

```
1. Validate entity index
2. Check if entity is walking/running (state 5 or 8)
3. Call VIBE_State_Update for parent entity
4. Call VIBE_DecompressState_Blob for state blob processing
5. Update animations via VIBE_Animation_Basic
6. Finalize via VIBE_Decompression_Finalize
```

### Assembly-Level Pseudocode

```c
int VIBE_GameLogic_Entities(
  int param1,
  int click_y,
  int entity_index,
  int state_data
)
{
  // Validate entity index
  if (entity_index > dword_62D208) {
    return 0;
  }
  
  // === ENTITY STATE DETECTION ===
  int entity_addr = dword_62D204 + 84 * entity_index;
  int entity_state = *(int*)(entity_addr + 60);
  
  int parent_index = -1;
  
  // Check if walking or running
  if ((entity_state == 5 || entity_state == 8) && 
      !*(int*)(entity_addr + 48)) {
    
    // Has inherited movement from parent
    if (!*(int*)(dword_62D204 + 84 * *(int*)(entity_addr + 76) + 52)) {
      
      // Call state update for parent
      int state_update = VIBE_State_Update(entity_index);
    }
    
    parent_index = entity_index;
    entity_index = *(int*)(entity_addr + 76);  // Use parent index
  }
  else if (!*(int*)(dword_62D204 + 84 * entity_index + 52)) {
    // Regular entity without parent
    int state_update = VIBE_State_Update(entity_index);
  }
  
  // === MOVEMENT OFFSET ===
  unsigned char move_flag = *(unsigned char*)(
    dword_62D204 + 84 * entity_index + 68
  );
  
  if ((move_flag & 0x02) != 0) {  // Movement active flag
    entity_index += (unsigned char)byte_62D220;  // Add movement offset
  }
  
  // === STATE BLOB DECOMPRESSION ===
  int velocity = 0;
  if (!state_update) {
    velocity = *(int*)(dword_62D204 + 84 * entity_index + 52);
  }
  
  // Calculate array index (21-byte stride)
  int array_offset = 21 * (parent_index == -1 ? entity_index : parent_index);
  
  // Decompress state blob
  int decomp_result = VIBE_DecompressState_Blob(
    state_data,
    *(int*)(dword_62D204 + 4 * array_offset + 60),
    velocity
  );
  
  if (decomp_result) {
    unsigned int entity_type = (decomp_result >> 24);
    
    // === ANIMATION UPDATE ===
    if (entity_type >= 5) {
      if (entity_type <= 5 || entity_type == 8) {
        // Movement entity animation
        if (parent_index != -1) {
          VIBE_Animation_Basic(
            entity_type,
            click_y >> 16,
            entity_index - entity_index  // Relative offset
          );
        }
      }
    } else if (!entity_type || (entity_type > 1 && entity_type != 4)) {
      // Other types (skip complex logic)
    } else {
      // Basic animation
      VIBE_Animation_Basic(entity_type, click_y >> 16, 0);
    }
    
    // === STATE FINALIZATION ===
    VIBE_State_GetCurrent(
      param1,
      click_y >> 16,
      *(int*)(dword_62D204 + 84 * entity_index + 80) >> 16,
      *(int*)(dword_62D204 + 84 * entity_index + 78) >> 16
    );
    
    return VIBE_Decompression_Finalize(state_data);
  }
  
  return decomp_result;
}
```

### Critical State Values

| State | Value | Meaning | Handler |
|-------|-------|---------|---------|
| 0 | 0x00 | Idle/stationary | Skip animation |
| Walking | 0x05 | Normal movement | VIBE_Animation_Basic |
| Running | 0x08 | Fast movement | Complex coordinate transform |
| Other | varies | Special states | Type-specific |

---

## Phase 4: VIBE_GameLogic_Interactions (0x4139E4)

**Purpose**: Handle entity interactions (doors, levers, NPCs)

**Execution Time**: ~2ms per interaction  

### High-Level Algorithm

```
1. Detect clickable entities in view
2. Get interaction type and target
3. Execute interaction handler
4. Update UI/state based on interaction result
5. Trigger animations if applicable
```

### Types of Interactions

- **NPC Interaction**: Talk to NPC, initiate quest
- **Door**: Open/close door, change visibility
- **Lever**: Toggle lever state, trigger effects
- **Treasure**: Open chest, distribute loot
- **Building**: Enter building, change zone

### Pseudocode

```c
void VIBE_GameLogic_Interactions(int entity_id)
{
  // Get entity data
  int entity_addr = dword_69FFB4 + 740 * entity_id;
  unsigned char entity_type = *(unsigned char*)(entity_addr + 24);
  
  // Check if entity is interactive (type 0x45)
  if (entity_type != 0x45) {
    return;  // Not interactive
  }
  
  // Get interaction handler
  int handler_ptr = *(int*)(entity_addr + 116);  // Interaction function
  
  if (!handler_ptr) {
    return;  // No handler
  }
  
  // === EXECUTE INTERACTION ===
  VIBE_Interaction_Handler(handler_ptr);
  
  // Get interaction target
  int target_id = *(int*)(entity_addr + 112);
  
  if (target_id != -1) {
    // Update target entity state
    int target_addr = dword_69FFB4 + 740 * target_id;
    
    // Trigger animation on target
    VIBE_Animation_Apply(
      *(int*)(target_addr + 14) >> 16,
      *(int*)(target_addr + 16) >> 16,
      *(int*)(target_addr + 20) >> 16,
      *(int*)(target_addr + 18) >> 16,
      0,
      NULL,
      0
    );
  }
  
  // === BROADCAST TO CLIENTS ===
  VIBE_Result_Broadcast(entity_id, 0x01, 0);  // Interaction complete
}
```

---

## Phase 5: VIBE_GameLogic_Updates (0x41433C) ⭐ MOST COMPLEX

**Purpose**: Update all entities, apply physics, animations, and broadcast state changes

**Execution Time**: ~10-15ms per tick (largest phase)  
**Size**: 2100+ lines of assembly (1100+ line pseudocode equivalent)

### High-Level Algorithm

```
1. Initialize coordinate tracking (VIBE_Coord_Push)
2. Iterate through all 512 object slots
3. For each object:
   a. Validate existence and visibility
   b. Calculate Z-extent (with parent hierarchy)
   c. Branch on entity type (17+ handlers)
   d. Execute type-specific logic
   e. Update animations and coordinates
   f. Broadcast changes to clients
4. Finalize frame state
```

### Assembly-Level Pseudocode (Partial - 1100+ lines)

```c
int VIBE_GameLogic_Updates(int *object_pool)
{
  // === INITIALIZATION ===
  dword_62D2F4 = 0;  // Parent entity tracker
  dword_62D2F8 = 0;  // Selection tracker
  sub_5D8B10();      // Initialize coordinate system
  
  // === MAIN LOOP: ITERATE ALL OBJECTS ===
  for (int i = 0; i < 2048; i += 4) {
    
    int *object_ptr = *(int**)(i + dword_62D26C);
    
    if (!object_ptr || i >= 2040) {
      break;  // End of pool
    }
    
    // === VALIDATION CHECKS ===
    int object_addr = (int)object_ptr;
    int obj_x = (*(int*)(object_addr + 14)) >> 16;
    int obj_x_extent = ((*(int*)(object_addr + 18)) >> 16) + obj_x;
    int obj_z = (*(int*)(object_addr + 26)) >> 16;
    int obj_z_extent = (*(int*)(object_addr + 32)) >> 16;
    
    // Check parent collision override
    int *parent = (int*)object_ptr[15];
    if (parent && *(int*)(*(int*)(*(_DWORD*)(object_addr + 60) + 408))) {
      continue;  // Parent blocks
    }
    
    // Check blocked/hidden
    if (!object_ptr[1] || *(int*)(object_addr + 52)) {
      continue;
    }
    
    // === Z-EXTENT CALCULATION ===
    int z_min = (*(int*)(object_addr + 28)) >> 16;
    int z_max = (*(int*)(object_addr + 32)) >> 16;
    
    // For parent hierarchy
    if (object_ptr[15]) {
      int *child_object = (int*)(740 * object_ptr[15] + dword_69FFB4);
      z_min = (*(int*)(child_object + 26)) >> 16;
      z_max = (*(int*)(child_object + 30)) >> 16;
    }
    
    // === COORDINATE TRACKING ===
    VIBE_Coord_Push(
      (*(int*)(object_addr + 26)) >> 16,
      (*(int*)(object_addr + 30)) >> 16,
      (*(int*)(object_addr + 32)) >> 16,
      (*(int*)(object_addr + 28)) >> 16
    );
    
    // === ENTITY TYPE DISPATCHER ===
    unsigned char entity_type = *(unsigned char*)(object_addr + 24);
    
    switch(entity_type) {
      
      // === TYPE 0x01-0x04: BASIC TYPES ===
      case 0x01:
        // Basic animation
        VIBE_Animation_Basic(
          *(int*)(object_addr + 12),
          obj_x_extent,
          0
        );
        break;
      
      case 0x04:
        // Physics update
        VIBE_Physics_Update(*(int*)(object_addr + 12));
        
        if (!object_ptr[11]) {
          // Has parent - continue to walking
          // Fall through to 0x05
        } else {
          break;  // Has parent - stop here
        }
      
      // === TYPE 0x05-0x08: MOVEMENT ENTITIES ===
      case 0x05:
      case 0x08:
        {
          unsigned char flags = *(unsigned char*)(object_addr + 444);
          
          // Movement flag check
          if ((flags & 0x02) != 0) {
            
            // Check if selected
            if (dword_69FFB4 + 740 * dword_62D22C == object_addr ||
                *(int*)(object_addr + 36)) {
              
              // Allow movement
              if ((flags & 0x01) != 0) {
                *(int*)(object_addr + 40) = 1;
              }
            } else {
              // Not selected - block movement
              if ((flags & 0x01) != 0) {
                *(int*)(object_addr + 40) = 0;
              }
            }
          }
          
          // === ANIMATION STATE ===
          if (*(short*)(object_addr + 26) == 2) {
            *(short*)(object_addr + 26) = 1;
          }
          
          // Apply velocity
          VIBE_State_Finalize(*(int*)(object_addr + 110) >> 16);
          
          if (*(int*)(object_addr + 40)) {
            // Action flag set
            unsigned char anim_flags = 0x02;  // Moving
          } else {
            unsigned char anim_flags = *(int*)(object_addr + 76) != 0;
          }
          
          if (*(int*)(object_addr + 88)) {
            anim_flags |= 0x08;  // Combat
          }
          
          if (*(int*)(object_addr + 92)) {
            anim_flags |= 0x10;  // Special action
          }
          
          if (*(int*)(object_addr + 64)) {
            anim_flags |= 0x04;  // Custom animation
          }
          
          // === APPLY ANIMATION ===
          VIBE_Animation_Apply(
            *(int*)(object_addr + 14) >> 16,
            *(int*)(object_addr + 16) >> 16,
            *(int*)(object_addr + 20) >> 16,
            *(int*)(object_addr + 18) >> 16,
            dword_62D210,
            (const char**)(object_addr + 116),
            anim_flags
          );
          
          // === ACTION ANIMATION ===
          if (*(char*)(object_addr + 120)) {
            
            if (*(short*)(object_addr + 112)) {
              VIBE_State_Finalize(*(int*)(object_addr + 110) >> 16);
            }
            
            // Compute frame offset
            int frame_offset = VIBE_AnimationFlags_Compute(
              *(int*)object_addr,
              &v48,
              &v49
            );
            
            // Calculate final frame
            int final_frame;
            if (*(int*)(object_addr + 456) == -1) {
              final_frame = (*(int*)(object_addr + 18) >> 16) + 
                           (3 * frame_offset);
            } else {
              final_frame = *(int*)(object_addr + 456) +
                           (*(int*)(object_addr + 16) >> 16);
            }
            
            // Apply action animation
            VIBE_Animation_Apply(
              final_frame,
              *(int*)(object_addr + 16) >> 16,
              *(int*)(object_addr + 20) >> 16,
              *(int*)(object_addr + 18) >> 16,
              dword_62D210,
              (const char*)(object_addr + 120),
              anim_flags
            );
          }
          
          break;
        }
      
      // === TYPE 0x40-0x45: COMPLEX TYPES ===
      case 0x40:
        // Building
        VIBE_Building_Update(*(int*)(object_addr + 116));
        break;
      
      case 0x41:
        // Object
        VIBE_Object_Update(*(int*)(object_addr + 116), dword_62D210);
        break;
      
      case 0x42:
        // NPC/mob (type 0x43)
        if (*(short*)(object_addr + 26) > 0) {
          VIBE_EntityChild_Process(*(int*)(object_addr + 116));
        }
        break;
      
      case 0x43:
        // Interactive
        {
          VIBE_State_Finalize(*(int*)(object_addr + 110) >> 16);
          VIBE_Entity_InteractionLogic(*(int*)object_addr, dword_62D210);
          VIBE_Entity_AnimationUpdate(
            *(int*)(object_addr + 14) >> 16,
            *(int*)(object_addr + 16) >> 16,
            *(int*)((char*)&dword_69FFB8 + 2) >> 16,
            dword_69FFBC >> 16,
            (_DWORD*)dword_62D210
          );
          break;
        }
      
      case 0x45:
        // Special
        {
          *(_WORD *)(object_addr + 32) = obj_z_extent;
          *(_WORD *)(object_addr + 34) = obj_z;
          sub_412668();
          break;
        }
      
      case 0x11:
        // NPC timer (state machine)
        if (*(short*)(object_addr + 26) > 0 && byte_62D25C != 1) {
          --*(short*)(object_addr + 26);
        }
        break;
      
      default:
        break;
    }
    
    // === RESET COORDINATES ===
    VIBE_Coord_Push(
      0, 0,
      *(int*)((char*)&dword_69FFB8 + 2) >> 16,
      dword_69FFBC >> 16
    );
  }
  
  // === FINALIZATION ===
  if (dword_62D2F8) {
    VIBE_Coord_Push(
      0, 0,
      *(int*)((char*)&dword_69FFB8 + 2) >> 16,
      dword_69FFBC >> 16
    );
    sub_4137BC();
    dword_62D2F4 = 0;
    dword_62D2F8 = 0;
  }
  
  // Validate property
  int validated = VIBE_Property_Validate();
  
  // Final state update
  int result = VIBE_State_Update(validated);
  
  // Store result
  LODWORD(qword_62D244) = result;
  dword_69FFB0 = 24;
  ++dword_62D238;
  
  return result;
}
```

### Entity Type Handlers Breakdown

| Type | Hex | Handler | Logic |
|------|-----|---------|-------|
| **Basic** | 0x01 | VIBE_Animation_Basic | Static animation frame |
| **Physics** | 0x04 | VIBE_Physics_Update | Gravity + collision |
| **Walking** | 0x05 | Movement | Walking animation + state |
| **Running** | 0x08 | Movement | Running animation + state |
| **NPC** | 0x11 | Timer check | Decrement state timer |
| **Building** | 0x40 | VIBE_Building_Update | Structure updates |
| **Object** | 0x41 | VIBE_Object_Update | Item updates |
| **Mob** | 0x42 | VIBE_EntityChild_Process | Child entity updates |
| **Interactive** | 0x43 | VIBE_Entity_InteractionLogic | Door/lever/treasure |
| **Special** | 0x45 | Custom handler | Special entity logic |

### Critical Constants (Phase 5)

| Constant | Value | Purpose |
|----------|-------|---------|
| 2048 | Size | Max iterations (512 * 4) |
| 2040 | Boundary | Loop termination check |
| 0x02 | Bit | Movement flag (offset +444) |
| 0x01 | Bit | Allow movement flag |
| 0x08 | Bit | Combat flag |
| 0x10 | Bit | Special action flag |
| 0x04 | Bit | Custom animation flag |

---

## Cross-Phase Communication

### State Flow Between Phases

```
Phase 1: Movement
  │
  ├─ Updates: movement_counter (offset +64)
  ├─ Updates: movement_state (offset +60)
  └─ Return: entity_offset
       │
       ↓
Phase 2: Objects
  │
  ├─ Input: entity_offset
  ├─ Allocates: new object slot
  ├─ Updates: object properties (position, type, rotation)
  └─ Return: object_slot
       │
       ↓
Phase 3: Entities
  │
  ├─ Input: entity_state, object_slot
  ├─ Decompresses: zlib state blob
  ├─ Updates: entity arrays
  └─ Return: animation_state
       │
       ↓
Phase 4: Interactions
  │
  ├─ Input: selected_entity
  ├─ Processes: user interactions
  ├─ Updates: interaction targets
  └─ Return: interaction_result
       │
       ↓
Phase 5: Updates
  │
  ├─ Input: All previous results
  ├─ Updates: All object states
  ├─ Broadcasts: Changes to clients
  └─ Return: final_state
```

### Global State Variables Modified

**Phase 1**: dword_62D204 (movement counter)  
**Phase 2**: dword_69FFB4 (object array)  
**Phase 3**: dword_67EB80 (entity info)  
**Phase 4**: dword_62D210 (interaction flags)  
**Phase 5**: All globals + dword_69FFB0, dword_62D238  

---

## Performance Analysis

| Phase | Time (ms) | Operations | Complexity |
|-------|-----------|-----------|------------|
| 1: Movement | 0.5 | 16 per entity | O(n) |
| 2: Objects | 1.0 | 30 per object | O(n) |
| 3: Entities | 1.5 | 40 per entity | O(n) |
| 4: Interactions | 2.0 | 50 per interaction | O(n) |
| 5: Updates | 10.0 | 200+ per object | O(n * m) |
| **Total** | **~15ms** | **2000+ ops** | **~90% accuracy** |

---

## Implementation Checklist

**Phase 1 - Movement**:
- [ ] Implement movement state checking (5, 8)
- [ ] Handle parent entity inheritance
- [ ] Decrement movement counter
- [ ] Call completion handler when counter = 0

**Phase 2 - Objects**:
- [ ] Allocate objects from pool
- [ ] Load 3D model metadata
- [ ] Initialize position/rotation
- [ ] Type-specific setup (1, 4, 5, 8, 17)

**Phase 3 - Entities**:
- [ ] Validate entity state
- [ ] Call state updaters
- [ ] Decompress state blob
- [ ] Execute animations

**Phase 4 - Interactions**:
- [ ] Detect interactive entities
- [ ] Execute interaction handlers
- [ ] Update targets
- [ ] Trigger animations

**Phase 5 - Updates**:
- [ ] Iterate all objects
- [ ] Execute 17+ type handlers
- [ ] Apply physics/animations
- [ ] Broadcast to clients

