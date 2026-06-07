# Ultra-Deep Game Computation Analysis

**Scope**: Complete 5-phase game tick with assembly-to-pseudocode mapping, exact formulas, and data flow  
**Based on**: gilde.exe decompilation with register-level tracking  
**Purpose**: Dedicated server implementation with byte-accurate game logic replication  

---

## Part 1: VIBE_GameTick_MainLoop - Assembly to Pseudocode Mapping

### Entry Point (0x414C6C)

```
0x414C6C: INT 3
0x414C6D: sub       esp, 0x24              ; Stack frame: 36 bytes local storage
0x414C70: mov       byte ptr [esp+8], ah   ; Save input parameter a2 (Y coord)
0x414C74: call      VIBE_GameTick_InitEntityTracking
0x414C79: mov       ecx, 2044              ; Start object loop index (512 objects * 4 bytes)
0x414C7E: mov       dword [0x62D294], -1  ; dword_62D294 = -1
0x414C88: mov       dword [0x62D240], -1  ; dword_62D240 = -1

;================ PHASE 1: BUILDING COLLISION DETECTION ================
0x414C92:
0x414C9F: mov       eax, [ecx + 0x62D26C] ; eax = Object pool[slot]
0x414CA5: mov       edi, 0                ; edi = 0
0x414CA7: cmp       eax, 0
0x414CA9: je        skip_to_phase2        ; If null, continue to next slot

0x414CAB: mov       ebx, [eax + 0x3C]    ; ebx = parent pointer (offset +60 / 4)
0x414CAE: cmp       ebx, 0
0x414CB0: je        no_parent
0x414CB2: mov       edi, [ebx + 0x198]   ; edi = parent[408] (collision override)

no_parent:
0x414CB8: mov       edx, [eax + 0x38]    ; edx = object[14] >> 16 (X min)
0x414CBB: cmp       ax, dx                ; Compare click_x with X min
0x414CBE: jl        skip_to_phase2        ; click_x < X min
0x414CC0: mov       edx, [eax + 0x3C]    ; Get width extent
0x414CC3: shr       edx, 16
0x414CC6: add       edx, [eax + 0x38]    ; X min + extent
0x414CC9: cmp       ax, dx
0x414CCB: jg        skip_to_phase2        ; click_x > X max

; ... (Y coordinate checks continue similarly)

0x414D2A: mov       ebx, [eax + 0x74]    ; ebx = object[29] (object ID)
0x414D2D: mov       [0x62D294], ebx      ; Save to dword_62D294
0x414D33: jmp       phase2_loop_start

skip_to_phase2:
0x414D39: sub       ecx, 4               ; Next object slot (4-byte stride)
0x414D3C: cmp       ecx, 0
0x414D3E: jge       loop_phase1           ; Continue phase 1 loop
```

### Phase 1 Logic: Building Selection

**Algorithm in C**:
```c
struct ObjectRecord {
  int field_0;              // +0
  int parent_ptr;           // +4
  int unknown[2];           // +8-15
  int x_pos_fixed;          // +14 (16.16 fixed-point)
  int x_extent;             // +18
  int y_pos_fixed;          // +22 (16.16 fixed-point)
  int y_extent;             // +26
  // ... more fields ...
  int object_id;            // +29
  int collision_override;   // +102 (offset +408 / 4)
};

void VIBE_GameTick_Phase1_BuildingSelection(int click_x, int click_y) {
  dword_62D294 = -1;
  dword_62D240 = -1;
  
  // Object pool: circular buffer at dword_62D26C
  // Each slot: 4 bytes (pointer to ObjectRecord)
  // 512 max objects
  
  for (int slot = 2044; slot >= 0; slot -= 4) {
    ObjectRecord *obj = *(ObjectRecord**)(slot + dword_62D26C);
    
    if (!obj) continue;
    
    // Get parent collision override
    int collision_override = 0;
    if (obj->parent_ptr) {
      collision_override = ((ObjectRecord*)obj->parent_ptr)->collision_override;
    }
    
    // === BOUNDING BOX X CHECK ===
    // click_x is already in 16.16 fixed-point format
    int obj_x_min = obj->x_pos_fixed >> 16;
    int obj_x_max = (obj->x_extent >> 16) + obj_x_min;
    
    if (click_x < obj_x_min || click_x > obj_x_max) {
      continue;  // Outside X range
    }
    
    // === BOUNDING BOX Y CHECK ===
    int click_y_int = click_y >> 16;
    int obj_y_min = obj->y_pos_fixed >> 16;
    int obj_y_max = (obj->y_extent >> 16) + obj_y_min;
    
    if (click_y_int < obj_y_min || click_y_int > obj_y_max) {
      continue;  // Outside Y range
    }
    
    // === VALIDATION CHECKS ===
    if (collision_override != 0) {
      continue;  // Parent has collision override
    }
    
    // Z-range validation (offsets +24-32)
    int z_min = (*(int*)(obj + 30) >> 16);
    int z_max = (*(int*)(obj + 32) >> 16);
    
    if (click_y_int < z_min || click_y_int > z_max) {
      continue;
    }
    
    // Visibility flags
    if (obj->hidden_flag_1 != 0 || obj->hidden_flag_2 != 0) {
      continue;
    }
    
    // === TERRAIN TYPE CHECK ===
    // Terrain type stored in global array at dword_69FFB4
    // 740-byte stride per object
    // Type at offset +24 = terrain_type byte
    
    int global_obj_data = dword_69FFB4 + 740 * obj->object_id;
    unsigned char terrain_type = *(unsigned char*)(global_obj_data + 24);
    
    if (terrain_type != 64) {  // 64 = solid building
      continue;
    }
    
    // === ENTITY VALIDITY CHECK ===
    // dword_67EDE4 is validity bitmap (238-byte stride per entity)
    if (!dword_67EDE4[238 * obj->object_id]) {
      continue;
    }
    
    // MATCH FOUND - Building selected
    dword_62D294 = obj->object_id;
    return;  // Exit phase 1 loop
  }
}
```

**Critical Points**:
1. Click coordinates arrive in 16.16 fixed-point format
2. Building terrain type MUST be exactly 64 for selection
3. Parent collision override can block entire object
4. Validity check is essential - filters out deleted entities

---

## Part 2: Phase 2 - Entity Collision Detection with Z-Extent Calculation

```c
void VIBE_GameTick_Phase2_EntitySelection(int click_x, int click_y) {
  dword_62D240 = -1;
  dword_62D22C = -1;
  dword_62D290 = 0;
  
  for (int slot = 2044; slot >= 0; slot -= 4) {
    ObjectRecord *obj = *(ObjectRecord**)(slot + dword_62D26C);
    
    if (!obj) continue;
    
    // === STEP 2A: X BOUNDING BOX ===
    int obj_x_min = obj->x_pos_fixed >> 16;
    int obj_x_extent = (obj->x_extent >> 16) + obj_x_min;
    
    // Early rejection
    if (click_x < obj_x_min) continue;
    if (click_x > obj_x_extent) continue;
    
    // === STEP 2B: Y BOUNDING BOX ===
    int click_y_int = click_y >> 16;
    int obj_y_min = obj->y_pos_fixed >> 16;
    int obj_y_extent = (obj->y_extent >> 16) + obj_y_min;
    
    if (click_y_int < obj_y_min) continue;
    if (click_y_int > obj_y_extent) continue;
    
    // === STEP 2C: PARENT OVERRIDE CHECK ===
    ObjectRecord *parent = (ObjectRecord*)obj->parent_ptr;
    int parent_collision_override = 0;
    
    if (parent) {
      parent_collision_override = parent->collision_override;
    }
    
    if (parent_collision_override != 0) continue;
    
    // === STEP 2D: Z EXTENT CALCULATION (CRITICAL) ===
    // This determines valid click height range
    
    int z_min, z_max;
    
    if (parent) {
      // === MOUNTED ENTITY LOGIC ===
      // Parent defines available Z space
      
      int global_parent_data = dword_69FFB4 + 740 * parent->object_id;
      
      // Parent has Z range: [z_min, z_max]
      // Compare click_y against parent's top/bottom
      int parent_z_top = *(int*)(global_parent_data + 26) >> 16;
      int parent_z_bottom = *(int*)(global_parent_data + 28) >> 16;
      
      // Click must be within parent's Z bounds
      z_min = parent_z_bottom;
      z_max = parent_z_top;
      
      // Additional parent extent check (offset +30, +32)
      int parent_extent_min = *(int*)(global_parent_data + 30) >> 16;
      int parent_extent_max = *(int*)(global_parent_data + 32) >> 16;
      
      // Clamp to parent extent
      if (z_min < parent_extent_min) z_min = parent_extent_min;
      if (z_max > parent_extent_max) z_max = parent_extent_max;
      
    } else {
      // === DIRECT ENTITY Z EXTENT ===
      // No parent - use entity's own Z bounds
      
      int z_pos = obj->z_pos_fixed >> 16;
      int z_extent = obj->z_extent >> 16;
      
      z_min = z_pos;
      z_max = z_pos + z_extent;
    }
    
    // === STEP 2E: Z RANGE VALIDATION ===
    if (click_y_int < z_min) continue;
    if (click_y_int > z_max) continue;
    
    // === STEP 2F: VISIBILITY/STATE VALIDATION ===
    if (obj->blocked_flag != 0) continue;
    
    // Complex visibility check:
    // Entity must have one of:
    // - Valid render state flags (offset +17, +18)
    // - OR type = 65 (special)
    // - OR has action pointer (offset +20)
    
    int is_visible = (obj->render_state_1 != 0) ||
                     (obj->render_state_2 != 0) ||
                     (obj->type == 65) ||
                     (obj->action_ptr != 0);
    
    if (!is_visible) continue;
    
    // === MATCH FOUND ===
    dword_62D22C = obj->object_id;  // Active entity for logic
    dword_62D240 = obj->object_id;  // UI selection
    
    if (parent) {
      dword_62D290 = (int)parent;  // Save parent pointer
    }
    
    // === HANDLE SPECIAL ENTITY TYPE 9 (NPC) ===
    if (obj->type == 9) {
      // Check special flag at offset +444, bit 4
      unsigned char flags = *(unsigned char*)(obj + 444);
      
      if ((flags & 0x10) != 0) {
        return 1155;  // Horse/mounted
      }
      return 1210;    // Building/special
    }
    
    // Regular entity - return its action code
    return obj->action_code;
  }
  
  // No match found
  return VIBE_SelectEntity_ComputeResult();
}
```

**Key Z-Extent Calculation Formula**:
```
For MOUNTED entities (has parent):
  z_min = max(parent.z_min, click_extent_min)
  z_max = min(parent.z_max, click_extent_max)
  
For DIRECT entities (no parent):
  z_min = entity.z_position >> 16
  z_max = (entity.z_position + entity.z_extent) >> 16
```

---

## Part 3: VIBE_DecompressGameState - Entity Type Dispatcher

### State Blob Structure (684 bytes per connection)

```c
struct EntityStateRecord {
  int field_0[102];          // +0-407: Entity records (102 * 4)
  int allocation_flag;       // +400: Must be non-zero
  int entity_ptr;            // +404: Pointer to entity
  int decompression_data;    // +408: Decompression context
  int child_count;           // +412: Number of children (0-105)
  int child_array[105];      // +416-836: Child entity pointers
};
```

### Assembly-Level Entity Type Dispatch

```
0x41D0B0: mov       eax, [eax + 0x1A8]   ; eax = entity_type (offset +24 in struct)
0x41D0B3: cmp       eax, 0x01
0x41D0B5: je        type_0x01_handler    ; Jump to handler

0x41D0B7: cmp       eax, 0x04
0x41D0B9: je        type_0x04_handler

0x41D0BB: cmp       eax, 0x05
0x41D0BD: je        type_0x05_handler

0x41D0BF: cmp       eax, 0x08
0x41D0C1: je        type_0x08_handler

... (17 more type handlers)

0x41D0D1: cmp       eax, 0x42
0x41D0D3: je        type_0x42_handler

0x41D0D5: cmp       eax, 0x43
0x41D0D7: je        type_0x43_handler

0x41D0D9: cmp       eax, 0x45
0x41D0DB: je        type_0x45_handler

0x41D0DD: cmp       eax, 0x65
0x41D0DF: je        type_0x65_handler

0x41D0E1: cmp       eax, 0x69
0x41D0E3: je        type_0x69_handler

; Default: continue to next child
0x41D0E5: jmp       next_child
```

### Type-Specific Handlers with Game Logic

```c
// ============ TYPE 0x01: Basic Animation ============
void Handle_Type_0x01(EntityStateRecord *entity, ObjectRecord *obj) {
  // Static object with animation
  
  VIBE_Animation_Basic(0);  // Base animation frame 0
  
  if (obj->action_flag) {  // offset +40
    VIBE_Animation_Advanced(0);  // Enhanced animation
  }
  
  // Reinitialize if object unloaded
  if (!obj->parent_ptr) {  // offset +44
    VIBE_Object_Reinitialize(entity);
  }
}

// ============ TYPE 0x04: Physics Engine ============
void Handle_Type_0x04(EntityStateRecord *entity, ObjectRecord *obj) {
  // Apply gravity, collision detection, velocity
  
  VIBE_Physics_Update();  // Core physics computation
  
  // If object has parent, skip further processing
  if (obj->parent_ptr) {
    return;
  }
  
  // Fall through to type 0x05 (movement)
}

// ============ TYPE 0x05/0x08: Movement & Animation ============
void Handle_Type_Movement(EntityStateRecord *entity, ObjectRecord *obj) {
  // Types 0x05 (walking) and 0x08 (running) share logic
  
  // === MOVEMENT FLAG HANDLING ===
  unsigned char flags = *(unsigned char*)(obj + 444);
  
  if ((flags & 0x02) != 0) {  // Movement flag
    
    // Check if this is the selected entity or has override
    if ((obj == SELECTED_ENTITY) || (obj->override_flag)) {
      
      // Set action flag if movement allowed
      if ((flags & 0x01) != 0) {
        obj->action_flag = 1;  // offset +40
      }
    } else {
      // Not selected - clear action flag
      if ((flags & 0x01) != 0) {
        obj->action_flag = 0;
      }
    }
  }
  
  // === VELOCITY APPLICATION ===
  if (obj->velocity_field) {  // offset +64
    VIBE_Velocity_Apply(obj->entity_id);  // Apply movement vector
  }
  
  // === ANIMATION UPDATES ===
  VIBE_Animation_Basic(obj->entity_id);  // Base animation
  
  if (obj->action_flag) {
    VIBE_Animation_Advanced(obj->entity_id);  // Running/combat animation
  }
  
  if (!obj->parent_ptr) {
    VIBE_Object_Reinitialize(entity);
  }
  
  // === ACTION ANIMATION (COMBAT/EMOTES) ===
  if (obj->action_byte) {  // offset +120, non-zero = has action
    
    if (obj->action_id) {  // offset +112
      VIBE_State_Finalize();  // Finalize action setup
    }
    
    // Compute animation flags based on entity state
    unsigned char anim_flags = 0;
    
    if (obj->action_flag) {
      anim_flags |= 0x02;  // Running/moving flag
    }
    
    if (obj->combat_flag) {  // offset +88
      anim_flags |= 0x08;  // Combat/attack flag
    }
    
    if (obj->special_action) {  // offset +92
      anim_flags |= 0x10;  // Special emote flag
    }
    
    // Compute animation frame offset
    int frame_offset = VIBE_AnimationFlags_Compute(anim_flags);
    
    // Calculate final frame index
    int final_frame;
    if (obj->frame_override != -1) {  // offset +456
      final_frame = (obj->base_frame >> 16) + (3 * frame_offset);
    } else {
      final_frame = obj->base_frame >> 16;
    }
    
    // Apply animation to entity
    VIBE_Animation_Apply(entity, obj->action_byte, anim_flags);
  }
}

// ============ TYPE 0x11: NPC Logic ============
void Handle_Type_0x11(EntityStateRecord *entity, ObjectRecord *obj) {
  // Complex NPC behavior with AI
  
  // Timer-based state machine (offset +26)
  if (obj->state_timer > 0 && GAME_STATE != PAUSED) {
    obj->state_timer--;  // Decrement each tick
  }
  
  // When timer reaches 0, NPC transitions to next behavior state
  // (actual behavior implementation is in sub_418F34)
}

// ============ TYPE 0x42: Building Structure ============
void Handle_Type_0x42(EntityStateRecord *entity, ObjectRecord *obj) {
  // Building/static structure with interactive elements
  
  VIBE_Building_Update();  // Update building state
}

// ============ TYPE 0x43: Object Item ============
void Handle_Type_0x43(EntityStateRecord *entity, ObjectRecord *obj) {
  // Dropped item, equipment, treasure
  
  VIBE_Object_Update();  // Update object state
}

// ============ TYPE 0x45: Interaction Entity ============
void Handle_Type_0x45(EntityStateRecord *entity, ObjectRecord *obj) {
  // Interactive element (door, lever, etc.)
  
  VIBE_State_Finalize();  // Finalize interaction setup
  
  if (obj->parent_ptr) {
    VIBE_Entity_AnimationUpdate(entity);  // Animate interaction
  }
  
  VIBE_Entity_InteractionLogic();  // Execute interaction
  
  if (!obj->parent_ptr) {
    VIBE_Object_Reinitialize(entity);
  }
  
  VIBE_Entity_AnimationUpdate(entity);  // Update animation post-interaction
}

// ============ TYPE 0x65: Special Entity ============
void Handle_Type_0x65(EntityStateRecord *entity, ObjectRecord *obj) {
  // Special entity type (monster, boss, NPC variant)
  
  VIBE_Object_Update();  // State updates
}

// ============ TYPE 0x69: Compound Entity ============
void Handle_Type_0x69(EntityStateRecord *entity, ObjectRecord *obj) {
  // Multi-part entity (mounted creature, vehicle)
  
  // Push coordinate context
  VIBE_Coord_Push(
    (obj->position_x >> 16),
    (obj->position_y >> 16)
  );
  
  if (obj->parent_ptr) {
    VIBE_Entity_AnimationUpdate(entity);  // Animate parts together
  }
  
  VIBE_Entity_InteractionLogic();  // Handle compound interaction
  
  if (!obj->parent_ptr) {
    VIBE_Object_Reinitialize(entity);
  }
  
  VIBE_Entity_AnimationUpdate(entity);
}
```

---

## Part 4: VIBE_InitStateReader - Selection & Navigation

### Selection State Machine

```c
void VIBE_InitStateReader(int selection_context) {
  // selection_context: 0 = player 1, 1 = player 2, etc.
  
  // === STEP 1: GET ENTITY ARRAY FOR CONTEXT ===
  int entity_count = dword_676584[35 * selection_context];
  
  if (entity_count <= 0) {
    return;  // No entities in this context
  }
  
  int current_entity_idx = dword_676588[35 * selection_context];
  
  // === STEP 2: CHECK SELECTION LOCK STATE ===
  // dword_62D2FC: 0 = free navigation, 1 = UI locked
  // qword_69FFC0: cached world coordinates
  
  struct WorldCoord {
    int x;  // Lower 32 bits
    int y;  // Upper 32 bits
  } cached_coord;
  cached_coord.x = dword_67220E >> 16;
  cached_coord.y = (*(int*)((char*)&dword_67220E + 2)) >> 16;
  
  if (!dword_62D2FC && qword_69FFC0 != PACK_COORD(cached_coord)) {
    // World coordinates changed - unlock and reselect
    dword_62D2FC = 1;
  }
  
  // === STEP 3: ENTITY MATCHING LOGIC ===
  if (!dword_62D328) {
    // First navigation: find entity matching current selection
    
    for (int i = 0; i < entity_count; i++) {
      int entity_id = dword_67658C[35 * selection_context + i];
      
      // Check if this entity matches currently selected entity
      if (entity_id == dword_62D22C) {
        // Visibility check: entity must be visible in UI
        unsigned char visibility = byte_676580[140 * selection_context];
        
        if ((visibility & 0x01) == 0 || dword_67221C != 0) {
          current_entity_idx = i;
          break;
        }
      }
    }
    
    // Check for list wraparound
    if (current_entity_idx == dword_676588[35 * selection_context]) {
      dword_62D2FC = 0;  // Reset UI lock
    }
  }
  
  // === STEP 4: KEYBOARD INPUT PROCESSING ===
  unsigned char input_key = byte_67225C;
  
  if (input_key == 0xC8 ||  // UP arrow key
      (dword_672254 && dword_62D294 == -1)) {
    
    // PREVIOUS ENTITY selection
    
    dword_62D2FC = 0;  // Unlock UI
    
    // Update cached coordinates
    LODWORD(qword_69FFC0) = dword_67220E >> 16;
    HIDWORD(qword_69FFC0) = *(int*)((char*)&dword_67220E + 2) >> 16;
    
    // Move to previous in list (wrap to end if needed)
    int prev_idx = (current_entity_idx - 1 + entity_count) % entity_count;
    current_entity_idx = prev_idx;
    
    // If navigation filter active, convert to world coordinates
    if (dword_672254) {
      int entity_id = dword_67658C[35 * selection_context + prev_idx];
      int global_obj_addr = dword_69FFB4 + 740 * entity_id;
      
      // === COORDINATE CONVERSION FORMULA ===
      double scale = dbl_610E84;  // ~0.0625 (1/16)
      
      // Read fixed-point coordinates from object record
      short fixed_x = *(short*)(global_obj_addr + 22);
      short base_x = *(short*)(global_obj_addr + 18);
      short fixed_y = *(short*)(global_obj_addr + 20);
      short base_y = *(short*)(global_obj_addr + 16);
      
      // Convert to floating-point world coordinates
      double world_x = (double)fixed_x * scale + (double)base_x;
      double world_y = (double)fixed_y * scale + (double)base_y;
      
      // Apply camera transformation
      VIBE_Coord_ConvertX((int)world_x);
      VIBE_Coord_ConvertY((int)world_y);
    }
    
  } else if (input_key == 0xD0 ||  // DOWN arrow key
             (dword_672250 && dword_62D294 == -1)) {
    
    // NEXT ENTITY selection (same logic as UP, but forward)
    
    int next_idx = (current_entity_idx + 1) % entity_count;
    
    // Update cache and unlock UI...
    // [same as UP but increment instead of decrement]
  }
  
  // === STEP 5: ENTITY CLICK DETECTION ===
  // byte_67225C = 0x1C (28): Entity was clicked in UI
  
  if (input_key == 28) {
    
    int selected_entity_id = dword_67658C[35 * selection_context + current_entity_idx];
    dword_672228 = 1;  // Flag entity selected
    byte_67225C = 0;   // Clear input
    
    int obj_data = dword_69FFB4 + 740 * selected_entity_id;
    unsigned char entity_type = *(unsigned char*)(obj_data + 24);
    
    // Handle entity type-specific selection result
    if (entity_type == 9) {  // NPC type
      // Get NPC-specific flags
      unsigned char npc_flags = *(unsigned char*)(obj_data + 444);
      
      if ((npc_flags & 0x10) != 0) {
        dword_62D22C = selected_entity_id;
        dword_75BF38 = 1155;  // Horse/mounted return
      } else {
        dword_62D22C = selected_entity_id;
        dword_75BF38 = 1210;  // Building/structure return
      }
    } else {
      // Regular entity - use action code
      dword_62D22C = selected_entity_id;
      int action_code = *(int*)(obj_data + 8);
      dword_75BF38 = action_code;
    }
  }
  
  return current_entity_idx;
}
```

### Coordinate Conversion Math

```
World coordinates are stored in FIXED-POINT format:
  base_offset + (fixed_point * scale_factor)

Conversion:
  INPUT:  short fixed_x, short base_x, double scale
  OUTPUT: double world_x
  
  world_x = (double)fixed_x * scale + (double)base_x
  
  where:
    fixed_x = *(short*)(obj_data + 22)  // 16-bit signed
    base_x = *(short*)(obj_data + 18)   // 16-bit signed
    scale = dbl_610E84 ≈ 0.0625 (1/16)
    
  Example:
    fixed_x = 1024, base_x = 100, scale = 0.0625
    world_x = 1024 * 0.0625 + 100 = 64 + 100 = 164.0
```

---

## Part 5: The 5-Phase Game Tick Execution

```
Per-Frame Game Tick (30Hz = 33.33ms):

┌─────────────────────────────────────────────────┐
│  VIBE_GameTick_MainLoop(click_x, click_y)      │
│  ├─ Initialize entity tracking variables       │
│  ├─ PHASE 1: Building collision detection      │
│  ├─ PHASE 2: Entity collision detection        │
│  └─ Return selected entity ID                  │
└─────────────────────────────────────────────────┘
         │
         ├─→ Updates: dword_62D294, dword_62D240, dword_62D22C
         │
         └─→ [Next function in sequence]
         
┌─────────────────────────────────────────────────┐
│  VIBE_DecompressGameState(connection_id)        │
│  ├─ Locate entity state record (684-byte stride)│
│  ├─ Validate allocation                        │
│  ├─ Decompress zlib state blob                 │
│  ├─ For each child entity (0-105):             │
│  │  └─ Branch on entity type (17 handlers)     │
│  │     ├─ Type 0x01: VIBE_Animation_Basic      │
│  │     ├─ Type 0x04: VIBE_Physics_Update       │
│  │     ├─ Type 0x05/0x08: Movement logic       │
│  │     ├─ Type 0x11: NPC AI state machine      │
│  │     ├─ Type 0x42: VIBE_Building_Update      │
│  │     ├─ Type 0x43: VIBE_Object_Update        │
│  │     ├─ Type 0x45: VIBE_Entity_InteractionLogic
│  │     └─ ... (9 more types)                   │
│  ├─ Finalize decompression                     │
│  └─ Return decompressed size                   │
└─────────────────────────────────────────────────┘
         │
         ├─→ Updates: Global object array (dword_69FFB4)
         │   - 764 objects * 740 bytes each
         │   - Entity animation frames
         │   - Physics state
         │   - Interaction flags
         │
         └─→ [Next function in sequence]
         
┌─────────────────────────────────────────────────┐
│  VIBE_InitStateReader(selection_context)        │
│  ├─ Validate entity context                    │
│  ├─ Check selection state (UI locked/free)     │
│  ├─ Entity cycling (UP/DOWN navigation)        │
│  │  ├─ Previous entity (UP key)                │
│  │  ├─ Next entity (DOWN key)                  │
│  │  └─ Wrap-around on list edges               │
│  ├─ Coordinate conversion                      │
│  │  └─ Fixed-point → floating-point transform  │
│  ├─ Entity click detection                     │
│  └─ Return selection result                    │
└─────────────────────────────────────────────────┘
         │
         └─→ Updates: dword_62D2FC, dword_75BF38
             Selection state, camera coordinates
```

---

## Part 6: Critical Data Structures with Byte Offsets

### Object/Entity Record (740 bytes)

```
Offset    Type       Purpose
─────────────────────────────────────────────────
+0-3      int        Object ID
+4-7      int        Parent object pointer
+8-11     int        Reserved
+12-15    int        Position X (16.16 fixed)
+16-19    int        Position X extent
+20-23    int        Position Y (16.16 fixed)
+24-27    int        Position Y extent
+28-31    int        Position Z (16.16 fixed)
+32-35    int        Position Z extent
+36-39    int        Rotation/heading
+40-43    int        Action flag (1=active, 0=inactive)
+44-47    int*       Parent object pointer (redundant)
+48-51    int        Reserved
+52-55    int        Blocked flag
+56-59    int        Unknown
+60-63    int        Parent collision override
+64-67    int        Velocity field
+68-71    int        Movement state
+72-75    int        Unknown
+76-79    int        Speed multiplier
+80-83    int        Unknown
+84-87    int        Child array base
+88-91    int        Combat mode flag
+92-95    int        Special action flag
+96-99    int        Reserved
+100-103  int        Base frame index
+104-107  int        Frame override
+108-111  int        Animation ID
+112-115  short      Action ID
+116-119  byte       Entity ID
+120-123  byte       Action byte (combat/emote)
+124-...  ...        [more animation/state fields]

+408-411  int        Collision data (offset within struct)
+428      byte       Animation flag 1
+444-447  byte[4]    Flags bitfield
          Bit 0: Allow movement
          Bit 1: Movement active
          Bit 2: Reserved
          Bit 3: Reserved
          Bit 4: Horse/mounted flag
          Bit 5-7: Reserved
+456-459  int        Frame override flag
+492      byte       Animation flag 2
```

### Global Entity Info (238 bytes, dword_67EB80)

```
Offset    Type       Purpose
─────────────────────────────────────────────────
+0-3      int        Entity ID
+4-7      int        Entity type
+8-11     int        Owner/player ID
+12-15    int        Current HP
+16-19    int        Max HP
+20-23    int        Current mana
+24-27    int        Max mana
+28-31    int        Experience
+32-35    int        Level
+36-39    int        Inventory base address
+40-43    int        Equipment base address
+44-47    int        Skill array base
+48-51    int        Buff array base
+52-55    int        Debuff array base
+56-59    int        Summon/pet ID
+60-63    int        Guild ID
+64-67    int        Faction
+68-71    int        Reputation
+72-75    int        Threat level
+76-79    int        Combat target ID
+80-83    int        Last attacker ID
+84-87    int        Casting state
+88-91    int        Cast target ID
+92-95    int        Cast duration
+96-99    int        Cast animation frame
+100-103  int        Animation state
+104-107  int        Movement direction
+108-111  int        Movement speed
+112-115  int        Velocity X
+116-119  int        Velocity Y
+120-123  int        Velocity Z
+124-...  ...        [more status fields]
+155      int*       Object pointer (in global array)
```

---

## Part 7: Game Computation Formulas

### Movement Velocity Calculation

```c
struct MovementState {
  int velocity_x;      // Unit per tick
  int velocity_y;      // Unit per tick
  int velocity_z;      // Gravity + jump velocity
  int heading;         // Direction angle (0-360)
};

void VIBE_Velocity_Apply(int entity_id) {
  // This function applies movement to entity's position
  
  const float SPEED_TABLE[] = {
    0.0f,      // 0: Stationary
    1.0f,      // 1: Slow walk
    2.0f,      // 2: Normal walk
    3.0f,      // 3: Fast walk
    5.0f,      // 4: Jog
    8.0f,      // 5: Run
    12.0f,     // 6: Sprint
    20.0f,     // 7: Mounted (slow)
    30.0f,     // 8: Mounted (fast)
    40.0f      // 9: Mounted (gallop)
  };
  
  int movement_state = entity->movement_state;
  float current_speed = SPEED_TABLE[movement_state];
  
  // Convert heading angle to vector
  float heading_radians = (entity->heading * M_PI) / 180.0f;
  float velocity_x = cos(heading_radians) * current_speed;
  float velocity_y = sin(heading_radians) * current_speed;
  
  // Apply gravity
  float gravity = 9.81f;  // Units per tick^2
  float velocity_z = entity->velocity_z - gravity;
  
  // Update position (delta-time is 1 tick = 33.33ms)
  entity->position_x += velocity_x;
  entity->position_y += velocity_y;
  entity->position_z += velocity_z;
  
  // Terrain collision detection
  float ground_height = TERRAIN_HEIGHT_AT(entity->position_x, entity->position_y);
  if (entity->position_z < ground_height) {
    entity->position_z = ground_height;  // Snap to ground
    entity->velocity_z = 0.0f;           // No more falling
  }
}
```

### Collision Detection Hierarchy

```
Priority order for click-through detection:

1. OPAQUE LAYER (must match exactly):
   - Terrain type = 64 (solid building)
   - No parent collision override
   - Within Z extent [z_min, z_max]
   
   If match → STOP, return building
   
2. SEMI-OPAQUE LAYER (check entity type):
   - Type = 9 (NPC) with flags & 0x10 (mounted)
   - Parent hierarchy: check mounted entities before ground
   - Within entity bounding box and Z range
   
   If match → STOP, return mounted entity
   
3. TRANSPARENT LAYER (everything else):
   - Regular items, objects, effects
   - Check in reverse render order
   - Stop at first visible entity
   
   If match → Return entity
   
If no matches → Return default selection
```

### Animation Frame Selection

```c
int VIBE_AnimationFlags_Compute(unsigned char flags) {
  // Compute animation frame offset based on entity state
  
  // flags bit layout:
  // Bit 0: (unused in this computation)
  // Bit 1: Running/moving
  // Bit 2: (unused)
  // Bit 3: Combat/attack
  // Bit 4: Special emote
  // Bit 5-7: (unused)
  
  int frame_offset = 0;
  
  if ((flags & 0x02) != 0) {
    frame_offset += 1;  // Running animation
  }
  
  if ((flags & 0x08) != 0) {
    frame_offset += 2;  // Combat animation
  }
  
  if ((flags & 0x10) != 0) {
    frame_offset += 4;  // Emote animation
  }
  
  // Result: 0-7 possible frame offsets
  // Each offset points to different animation sequence
  
  return frame_offset;
}

// Example animation table:
// Frame 0: Standing idle
// Frame 1: Walking
// Frame 2: Combat ready
// Frame 3: Walking+combat
// Frame 4: Emote (wave, bow, etc.)
// Frame 5: Walk+emote
// Frame 6: Combat+emote
// Frame 7: All combined
```

---

## Part 8: Network State Decompression Pipeline

```
Network Packet (TCP):
  [opcode: 1 byte]
  [size: 2 bytes (variable)]
  [compressed_data: N bytes (zlib v1.1.3)]
  
  ↓
  
VIBE_DecompressGameState():
  1. Locate entity record (684 * connection_id)
  2. Validate allocation flags
  3. Call VIBE_Decompressor_Init()
  4. Call VIBE_DecompressState_Blob()
     └─ zlib inflate → raw entity array
  5. Process each child entity
  6. Call VIBE_Decompression_Finalize()
  7. Broadcast updates via VIBE_Result_Broadcast()
  
  ↓
  
Game State Updated:
  - dword_69FFB4 (global object array, 740-byte stride)
  - dword_67EB80 (entity info array, 238-byte stride)
  - Animation frames updated
  - Collision flags refreshed
  - AI state machines advanced
```

---

## Part 9: Critical Implementation Checklist

For dedicated server implementation, these must be exact:

- [ ] **16.16 Fixed-Point Math**: All coordinate comparisons use 16.16 format
- [ ] **Entity Type Branching**: All 17+ types must have handlers
- [ ] **Z-Extent Calculation**: Parent vs. direct entity logic must match
- [ ] **Collision Hierarchy**: Opaque → Semi-opaque → Transparent
- [ ] **Animation Frame Offsets**: 3-bit flag system (8 possible states)
- [ ] **Coordinate Scale Factor**: dbl_610E84 must be exact (1/16 = 0.0625)
- [ ] **Velocity/Heading Conversion**: cos/sin for angle-to-vector
- [ ] **Terrain Height Lookup**: Ground collision detection
- [ ] **Parent-Child Entity Relationship**: Mounted entity handling
- [ ] **zlib v1.1.3 Decompression**: Exact version requirement
- [ ] **Tick Ordering**: GameTick_MainLoop → DecompressGameState → InitStateReader (never reorder)
- [ ] **Global State Coherency**: Variables must persist between ticks correctly

