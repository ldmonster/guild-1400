#pragma once
// Entity-record substrate: the global record arrays and id->pointer lookups for
// the Guild simulation. Faithful 1:1 port of the Person/Object/GameObject array
// machinery from gilde.exe. See types.h for the recovered record layouts and the
// original global base addresses.
//
// Translated functions:
//   VIBE_Person_FindRecordById        0x58bc6c
//   VIBE_Building_FindById            0x587b20
//   VIBE_GameObject_ResolveEntityById 0x583b44
//   VIBE_Person_QueryBegin            0x586c20
//   VIBE_Person_IterNext              0x586a6c
//   VIBE_GameObject_QueryFind         0x5857fc
//   VIBE_GameObject_IterNext          0x58529c
//   VIBE_GameObject_ResolveTypeFieldB 0x5917d4
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ===========================================================================
// Global record arrays (modeled as real arrays; original bases in comments).
// ===========================================================================

// gilde.exe word_12CE910 @0x12CE910 (stride 536, 768 slots).
extern Person g_persons[kPersonCapacity];
// gilde.exe dword_12CE914 @0x12CE914 — parallel id column (stride 134 dwords).
// Held in lockstep with g_persons[i].id; the two lookups read different columns.
extern i32 g_personIds[kPersonCapacity];

// gilde.exe *(0x13CE298) (stride 169, 256 slots).
extern ObjectRec g_objects[kObjectCapacity];

// gilde.exe scene-node tree. The original walks raw child/entity pointers; the
// reimpl models the nodes in a flat array and uses indices (-1 == null) in the
// SceneNode::childPtr / SceneNode::entityPtr link fields.
//   g_sceneNodes : node storage (capacity == g_sceneNodeCount used as the flat
//                  scan bound, mirroring dword_6498C0).
//   g_sceneNodeCount : dword_6498C0 @0x6498C0 — flat-scan count.
constexpr int kSceneNodeCapacity = 512;
extern SceneNode g_sceneNodes[kSceneNodeCapacity];
extern int g_sceneNodeCount; // dword_6498C0

// "Loaded" guards: the originals bail out if the array base global is 0 (game
// not loaded). We mirror that with explicit enable flags.
//   g_personArrayLoaded : guards dword_13CE294 / dword_13CE298 reads.
//   g_sceneArrayLoaded  : guards dword_13CE27C (type-def base) read.
extern bool g_personArrayLoaded;
extern bool g_sceneArrayLoaded;

// Resets all arrays/flags to the empty state (test/setup helper, not in orig).
void ResetEntityArrays();

// ===========================================================================
// id -> record lookups (linear scans, exactly as the originals).
// ===========================================================================

// gilde.exe 0x58bc6c — VIBE_Person_FindRecordById  (__usercall, eax=(id@eax)).
// Linear scan over g_persons: skip free slots (marker==-1) and id mismatches.
// Returns the matching Person* or nullptr.
Person* PersonFindRecordById(i32 id);

// gilde.exe 0x587b20 — VIBE_Building_FindById  (__usercall, eax=(id@eax)).
// Linear scan over g_objects: skip dead slots (alive==0) and id mismatches.
ObjectRec* BuildingFindById(i32 id);

// gilde.exe 0x583b44 — VIBE_GameObject_ResolveEntityById
//   (__usercall: eax=outObject@a1, edx=outScene@a2, ecx=id@a3, ebx=outPerson@a4)
// Unified id resolver across the three arrays. Each out-pointer may be null,
// which both suppresses that array's search and changes the search order:
//   - if outPerson != null: searches Person; on hit returns 3 (and clears
//     outObject/outScene which were probed first).
//   - else if outObject != null: searches Object/Building; on hit returns 1.
//   - else if outScene != null: searches the scene tree (flat scan); returns 2.
// Returns 0 on miss. The exact ordering/short-circuit matches the original.
int GameObjectResolveEntityById(ObjectRec** outObject, SceneNode** outScene,
                                i32 id, Person** outPerson);

// gilde.exe 0x5917d4 — VIBE_GameObject_ResolveTypeFieldB
//   (__usercall: ax=ret, eax=node@a1, ecx=a2). Resolves a node's "type field B":
// looks up the node's id (read at node+10) across the entity arrays; returns the
// resolved scene node's type word, else the object's +39 word, else -1.
i16 GameObjectResolveTypeFieldB(SceneNode* node, int a2);

// ===========================================================================
// Person query / iterator (varargs filter; stateful iterator over g_objects).
// ===========================================================================
//
// NOTE: despite the name, VIBE_Person_QueryBegin / VIBE_Person_IterNext iterate
// the OBJECT/BUILDING array (g_objects, stride 169) — the filter compares the
// alive byte, id (+1), faction (+37), owner (+39), and an AiPlayer-table byte.
//
// Filter opcodes (variadic int pairs, terminated by argc):
//   0: alive/type byte == (byte)value
//   1: id == value
//   2: name/string match (UNSUPPORTED here — needs string column; ignored)
//   3: faction word (+37) == value
//   4: owner word   (+39) == value
//   5: AiPlayer-table byte == value
//   6: "match-any" mode (a record matching ANY active filter is returned)

// Variadic-friendly filter spec for the C++ API (replaces the original va_list).
struct PersonFilter {
    int op;
    int value;
};

// gilde.exe 0x586c20 — VIBE_Person_QueryBegin. Initializes the iterator filter
// state from `filters`, then returns the first match via PersonIterNext().
ObjectRec* PersonQueryBegin(const PersonFilter* filters, int count);

// gilde.exe 0x586a6c — VIBE_Person_IterNext. Advances the iterator; returns the
// next matching ObjectRec* or nullptr when exhausted.
ObjectRec* PersonIterNext();

// ===========================================================================
// GameObject scene-tree query / iterator (DFS).
// ===========================================================================
//
// Filter opcodes:
//   0: type word == value
//   1: id == value
//   3: typeFieldB (ResolveTypeFieldB) == value
//   4: type-def byte (typedef table @+0) == value
//   5: "match-any" mode
//   6: use DFS stack (walk entityPtr subtrees)
//   7: flat-array scan mode (stride 67 over g_sceneNodeCount), no tree descent

struct SceneFilter {
    int op;
    int value;
};

// gilde.exe 0x5857fc — VIBE_GameObject_QueryFind. `startIndex` is the node index
// to begin a tree walk from (-1 for "use array base"); ignored in flat-scan mode.
SceneNode* GameObjectQueryFind(int startIndex, const SceneFilter* filters,
                               int count);

// gilde.exe 0x58529c — VIBE_GameObject_IterNext. Advances the DFS / flat scan.
SceneNode* GameObjectIterNext();

} // namespace guild::sim
