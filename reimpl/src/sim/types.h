#pragma once
// Entity-record substrate for the Guild simulation (gilde.exe).
//
// This header recovers the layouts of the three master record arrays that the
// id->pointer lookups and iterators address, plus the packed game-time record.
// The originals are flat arrays of fixed-stride POD records, scanned linearly.
// Field offsets below come from the lookup/iterator/resolver accessors; fields
// the accessors never touch are left as raw byte padding with a TODO.
//
// Original global bases (all heap pointers / arrays in the live process):
//   Person / NPC array     word_12CE910  0x12CE910  stride 536, 768 slots
//   Person id column       dword_12CE914 0x12CE914  stride 134 dwords (== 536 B)
//   Object & Building base  *(0x13CE298)             stride 169, 256 slots
//   Scene-node base         *(0x13CE290)             stride 67, tree (count 0x6498C0)
//   AiPlayer/type base      *(0x13CE294)             stride 589
//   Scene type-def base     *(0x13CE27C)             stride 65
//   Game time record        qword_13CE852 0x13CE852
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Person / NPC record  (gilde.exe word_12CE910 @0x12CE910)
//   stride 536 / 0x218, 768 slots. Also aliased as 134 dwords / 268 words.
//   marker word @+0: -1 == free slot.  id dword @+4.
// The lookup compares the marker word (word_12CE910[i]) and the id stored in a
// PARALLEL column dword_12CE914[134*i]. In the live game +4 of the record and
// the parallel id column hold the same id; we keep both so the two distinct
// lookups (FindRecordById vs ResolveEntityById) stay byte-faithful.
// ---------------------------------------------------------------------------
constexpr int kPersonStride   = 536;   // 0x218
constexpr int kPersonCapacity = 768;

// Recovered Person field map (extends the original four fields additively).
// Offsets below are byte offsets into the 536-byte record, recovered from the
// person/personnel/recruit/inventory accessors. The originals address these via
// per-field global symbols (e.g. byte_12CE912 == record+2, dword_12CEABC ==
// record+428); the symbol minus the array base 0x12CE910 gives the offset.
//   +0x02 (byte_12CE912)  kind/class byte (4..30; <10 == "real" person)
//   +0x08 (byte_12CE918)  is-player-controlled / "is a live actor" byte
//   +0x09 (dword_12CE919) gender/married low byte (1 == one sex/married)
//   +0x0A                 cash-on-hand (word; GetCashAmount reads (12CE919)+1)
//   +0x26 (a1+38)         birth/creation timestamp (dword; >>16 used)
//   +0x30 (a1+48)         per-person random seed (dword; birthdate derivation)
//   +0x50 (word_12CE960)  family/household slot word (& 0xF indexes word_13C3110)
//   +0x51 (a1+81)         family-valid sign byte (<0 == has family record)
//   +0x58 (a1+88)         age/level byte (family eligibility delta)
//   +0x5C (dword_12CE96C) father/employer person id (-1 == none); also relation[0]
//   +0x5C..+0x78          8-entry relation/family person-id array (dword each)
//   +0x60 (dword_12CE970) office-superior person id (ComputeOfficeRank recurse)
//   +0x80 (a1+128)        reputation/heat scalar byte (price & recruit cost)
//   +0x165 (byte_12CEA75) profession/role byte
//   +0x166 (byte_12CEA76) office id byte (Office_GetDefinition key)
//   +0x169 (byte_12CEA79) secondary office byte
//   +0x170 (dword_12CEA80) status flag dword
//   +0x178 (a1+376)       associated container/scene-entity id (dword)
//   +0x184 (dword_12CEA94) jail/incapacitated status dword
//   +0x1AC (dword_12CEABC) wealth score (dword; ComputeWealthRank key)
//   +0x1B1 (byte_12CEAC1) "already recruited / unavailable" flag byte
//   +0x1C8 (dword_12CEAD8) per-turn bitfield (0x800000 / 0x40000000 gates)
//   +0x1EC (dword_12CEAFC) misc eligibility flag dword
GUILD_PACKED_BEGIN
struct Person {
    i16 marker;          // +0x00  record-kind / alive marker (-1 == free slot)
    u8  kind;            // +0x02  (byte_12CE912) kind/class byte (<10 == person)
    u8  pad3;            // +0x03  packing
    i32 id;              // +0x04  (dword_12CE914) person / entity id
    u8  isPlayer;        // +0x08  (byte_12CE918) is-player-controlled / live actor
    u8  gender;          // +0x09  (dword_12CE919 low byte) gender/married flag
    i16 cash;            // +0x0A  cash-on-hand word (GetCashAmount)
    u8  pad12[25];       // +0x0C..+0x24  TODO: AI/turn fields
    i16 factionA;        // +0x25  (+37) faction/owner-A (Person_QueryBegin slot 3)
    i16 ownerPlayer;     // +0x27  (+39) owner/player id (QueryBegin slot 4)
    u8  pad41[495];      // +0x29..+0x217  remaining record (see field map above)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(Person) == kPersonStride, "Person stride must be 536");

// Byte offsets of Person fields the accessors reach by raw offset (the originals
// fold these into per-field global symbols). Used by person.cpp/recruit.cpp to
// read/write the record exactly as the binary does (unaligned, by byte offset).
enum PersonField : int {
    kPfKind          = 0x02,  // byte_12CE912
    kPfId            = 0x04,  // dword_12CE914
    kPfIsPlayer      = 0x08,  // byte_12CE918
    kPfGender        = 0x09,  // dword_12CE919 low byte
    kPfCash          = 0x0A,  // word
    kPfBirthTs       = 0x26,  // dword (a1+38)
    kPfSeed          = 0x30,  // dword (a1+48)
    kPfFamilyWord    = 0x50,  // word_12CE960
    kPfFamilySign    = 0x51,  // byte (a1+81)
    kPfAge           = 0x58,  // byte (a1+88)
    kPfRelationBase  = 0x5C,  // dword_12CE96C: relation[0..7] person ids
    kPfSuperiorId    = 0x60,  // dword_12CE970 (office-superior id)
    kPfReputation    = 0x80,  // byte (a1+128)
    kPfProfession    = 0x165, // byte_12CEA75
    kPfOffice        = 0x166, // byte_12CEA76
    kPfOffice2       = 0x169, // byte_12CEA79
    kPfStatusFlag    = 0x170, // dword_12CEA80
    kPfContainerId   = 0x178, // dword (a1+376)
    kPfJailStatus    = 0x184, // dword_12CEA94
    kPfWealthScore   = 0x1AC, // dword_12CEABC
    kPfRecruited     = 0x1B1, // byte_12CEAC1
    kPfTurnBits      = 0x1C8, // dword_12CEAD8
    kPfMiscFlag      = 0x1EC, // dword_12CEAFC
};

// ---------------------------------------------------------------------------
// Object & Building record  (gilde.exe *(0x13CE298))
//   stride 169 / 0xA9, 256 slots. Objects and Buildings share this array.
//   alive byte @+0 (!=0 == alive).  id dword @+1.
// ---------------------------------------------------------------------------
constexpr int kObjectStride   = 169;   // 0xA9
constexpr int kObjectCapacity = 256;

GUILD_PACKED_BEGIN
struct ObjectRec {
    u8  alive;           // +0x00  alive/type byte (0 == free slot)
    i32 id;              // +0x01  object/building id (unaligned dword)
    u8  pad5[164];       // +0x05..+0xA8  TODO: transform/fill/parent/occupant data
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(ObjectRec) == kObjectStride, "ObjectRec stride must be 169");

// ---------------------------------------------------------------------------
// Scene-entity / GameObject node  (gilde.exe *(0x13CE290))
//   stride 67 / 0x43. Forms a tree: child ptr @+63 for descent, linked-entity
//   ptr @+20 used as a DFS subtree root. type word @+0 (0 == empty slot),
//   id dword @+2, owner/parent id word @+10.
// Pointers in the original were 32-bit; for the reconstruction we use a node
// index instead of a raw pointer in the link fields so the model is portable
// (see entity.cpp for the index-based tree walk).
// ---------------------------------------------------------------------------
constexpr int kSceneNodeStride = 67;   // 0x43

GUILD_PACKED_BEGIN
struct SceneNode {
    i16 type;            // +0x00  node type (0 == empty slot)
    i32 id;              // +0x02  scene-entity id (unaligned dword)
    u8  pad6[4];         // +0x06  TODO
    i16 ownerId;         // +0x0A  (+10) owner/parent id
    u8  pad12[8];        // +0x0C  TODO
    i32 entityPtr;       // +0x14  (+20) linked-entity ptr (DFS subtree root in orig)
    u8  pad24[39];       // +0x18..+0x3E  TODO
    i32 childPtr;        // +0x3F  (+63) first-child ptr (orig); index in reimpl
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(SceneNode) == kSceneNodeStride, "SceneNode stride must be 67");

// ---------------------------------------------------------------------------
// Packed game-time record  (gilde.exe qword_13CE852 @0x13CE852)
//   GameTime_Advance addresses fields at byte offsets +0/+4/+6/+10 of a base.
//     +0  dayCounter (dword)
//     +4  hour       (word, wraps 0..23)
//     +6  minute     (dword, 0..59)
//     +10 second     (dword, 0..59)
// Carry chain: seconds -> minutes (/60) -> hours (/60) -> days (wrap 24).
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct GameTime {
    i32 day;             // +0x00  day counter
    u16 hour;            // +0x04  hour of day (0..23)
    i32 minute;          // +0x06  minute (unaligned dword)
    i32 second;          // +0x0A  second/tick (unaligned dword)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(GameTime) == 14, "GameTime record must be 14 bytes");

// ---------------------------------------------------------------------------
// Item / carried-good record  (the scene-entity payload returned as __int16*
// by VIBE_GameObject_QueryFind for an item/stock node). The inventory accessors
// only touch two fields:
//   +0x00  item type id (word; e.g. 42/278/475/476 are "reserve-1" goods,
//          477 is the high-capacity good, 9 is currency, 377/378 special)
//   +0x0E  stock/count (dword) — quantity held in this slot/stack
// (The full scene node is larger; we model just the inventory-relevant header.)
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct ItemRec {
    i16 type;            // +0x00  item type id
    u8  pad2[12];        // +0x02..+0x0D  scene-node header (unused by inventory)
    i32 count;           // +0x0E  stock/count (dword, unaligned)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(ItemRec) == 18, "ItemRec inventory header must be 18 bytes");

// Item type ids with special inventory semantics (recovered from the constants
// in GetEffectiveStock / GetSlotCapacity / FindItemStock).
enum ItemType : i16 {
    kItemCurrency   = 9,    // money good (special storage handling)
    kItemReserveA   = 42,   // effective stock = count - 1
    kItemReserveB   = 278,  // effective stock = count - 1
    kItemReserveC   = 475,  // effective stock = count - 1
    kItemReserveD   = 476,  // effective stock = count - 1
    kItemHighCap    = 477,  // slot capacity = 5*count + 10
    kItemSpecialA   = 377,  // carry-capacity special (capacity 0)
    kItemSpecialB   = 378,  // carry-capacity special (capacity 0)
};

// ---------------------------------------------------------------------------
// Live scene-actor record  (the "ch_t"; pointers held in dword_66F0D0[0..511]
// @0x66F0D0, 512 slots; slot != 0 == live). Allocated 0x204 == 516 bytes by
// VIBE_Character_AllocSlot (0x402254). NB: this is the RAW-OFFSET view of the
// same actor the action system models in character.h::Character — the query /
// state / mesh data-and-rules functions (character_query/_state/_mesh) read it by
// byte offset exactly as the binary does, so we keep a faithful byte-offset POD
// here (separate from the named-field Character used by the coroutine handlers).
//
// Field offsets recovered from VIBE_Character_AllocSlot (+0 slot index), Update
// (0x405148), CollectNearbyAtTile (0x401c3c), SetVisible (0x401894),
// ProcessFlaggedLocal (0x40204c), CountWithTransport (0x401bd8), ResolveMesh
// (0x4013fc), SetAllFreezeState (0x40238c) and the count/collect helpers:
//   +0x00  slot index (dword; AllocSlot writes the array index back)
//   +0x2C  universe id    (dword [11]; CollectNearbyAtTile self+44 == cand[11])
//   +0x30  sub-universe / group id (dword [12]; self+48 == cand[12])
//   +0x34  mesh handle    (dword [13]; mesh+76 world XYZ, mesh+533 cull gate,
//                          mesh+460 material, mesh+100 attached sub-object)
//   +0x54  target world X (float; redraw target, set by CollectNearbyAtTile)
//   +0x58  target world Y/Z packed start (dword cleared to 0 on retarget)
//   +0x5C  target world Z (float)
//   +0x88  universe ptr   (into g_universes / byte_13ECEC8, stride 984; compared
//                          to off_649D64 active scene & dword_649D60 active id)
//   +0x8C  flag byte A    (0x01 redraw-pending, 0x08 dirty-mesh, 0x10 sitting,
//                          0x20 hidden, 0x40 fade-registered)
//   +0x124 transport / secondary-mesh ptr (dword; +292)
//   +0x128 action-queue head (dword; +296; node+9 == action type byte)
//   +0x1EC low-poly mesh ptr (dword; +492; +533 cull gate)
// ---------------------------------------------------------------------------
constexpr int kLiveActorSize     = 516;   // 0x204 (AllocSlot alloc size)
constexpr int kLiveActorCapacity = 512;   // dword_66F0D0 slot count
constexpr int kLiveActorUniverseOff  = 136;  // +0x88
constexpr int kLiveActorFlagsOff     = 140;  // +0x8C
constexpr int kLiveActorMeshOff      = 52;   // +0x34
constexpr int kLiveActorActionOff    = 296;  // +0x128
constexpr int kLiveActorTransportOff = 292;  // +0x124
constexpr int kLiveActorLowPolyOff   = 492;  // +0x1EC

// Flag-byte-A bits (live actor +140).
enum LiveActorFlag : u8 {
    kLaRedraw      = 0x01,  // redraw/refresh pending (ProcessFlaggedLocal acts on it)
    kLaDirtyMesh   = 0x08,  // dirty-mesh (skip social/collect)
    kLaSitting     = 0x10,  // sit/idle animation active
    kLaHidden      = 0x20,  // hidden (SetVisible(0) sets, SetVisible(1) clears)
    kLaFadeSlot    = 0x40,  // registered in a fade-out slot
};

// ---------------------------------------------------------------------------
// Universe / scene record  (gilde.exe byte_13ECEC8 @0x13ECEC8, stride 984/0x3D8,
// 64 slots). VIBE_Character_IndexFromPointer (0x426724) maps a universe pointer
// back to its slot index: (ptr - base) / 984, capped at 64. Live actors hold a
// universe pointer at +136. FindFreeSlot (0x4266f4) scans byte_13ECEC8[i*984] and
// the byte at +1 for the first slot with both clear.
// Fields the data/rule functions read:
//   +0x00  occupied/type byte 0 (FindFreeSlot probes; 0 == part of free slot)
//   +0x01  occupied/type byte 1 (FindFreeSlot probes the second byte too)
//   +0xB0  heightmap/collision-grid handle (dword; ResolveMesh +176)
//   +0xB4  source asset handle for Heightmap_Create (dword; +180)
//   +0x3D5 "no-reload" guard byte (+981; ResolveMesh skips reload when set)
//   +0x3D6 inflate/log flags byte (+982; ResolveMesh logging gate)
// ---------------------------------------------------------------------------
constexpr int kUniverseStride   = 984;   // 0x3D8
constexpr int kUniverseCapacity = 64;
constexpr int kUniverseMeshOff      = 176;  // +0xB0  collision-grid / mesh handle
constexpr int kUniverseAssetOff     = 180;  // +0xB4  source asset for mesh build
constexpr int kUniverseNoReloadOff  = 981;  // +0x3D5
constexpr int kUniverseFlagsOff     = 982;  // +0x3D6

// ---------------------------------------------------------------------------
// Inventory UI grid slot  (gilde.exe word_63D1D8, 24-byte stride). Runtime
// state populated by the inventory window; FindSlotByItemId/FindSlotIndexByItemId
// scan it. Each entry: item type word at +0, then 22 bytes of UI/state. The
// scan terminates when the next entry's +0x18 (== next slot's marker word, read
// as word_63D1F0[i]) is zero.
// ---------------------------------------------------------------------------
constexpr int kInvSlotStride = 24;

GUILD_PACKED_BEGIN
struct InvGridSlot {
    i16 type;            // +0x00  item type id (0 marker terminates the scan)
    u8  pad2[22];        // +0x02..+0x17  UI widget / state payload
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(InvGridSlot) == kInvSlotStride, "InvGridSlot stride must be 24");

} // namespace guild::sim
