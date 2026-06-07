#include "sim/entity.h"

#include <cstring>

#include "sim/building.h"   // g_buildingTypes (589-stride type table @0x13CE294)

// Faithful 1:1 port of the entity-record substrate from gilde.exe.
//
// The original arrays are raw heap blocks scanned by byte stride. We model them
// as real C arrays of the recovered record structs (see types.h). The three
// id->ptr lookups are verbatim linear scans. The iterators preserve the exact
// stateful-global behavior of the originals (the query state lives in the
// 0x6498A8..0x6498E4 global block); we reproduce that state as file-scope
// globals with the same semantics.
//
// The GameObject scene tree used raw 32-bit child/entity pointers in the live
// process. The reimpl stores nodes in a flat array and uses node indices in the
// link fields (childPtr / entityPtr; -1 == null). The DFS control flow is a
// 1:1 translation with the pointer arithmetic replaced by index arithmetic.

namespace guild::sim {

// Helper: read an i16 from a (possibly unaligned) byte offset of a record.
// The original does raw unaligned x86 word loads; memcpy keeps that faithful
// without invoking C++ undefined behavior.
static i16 ReadWordAt(const void* base, int byteOffset) {
    i16 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(base) + byteOffset, sizeof(v));
    return v;
}

// ===========================================================================
// Global record arrays.
// ===========================================================================
Person   g_persons[kPersonCapacity];          // word_12CE910 @0x12CE910
i32      g_personIds[kPersonCapacity];        // dword_12CE914 @0x12CE914
ObjectRec g_objects[kObjectCapacity];         // *(0x13CE298)
SceneNode g_sceneNodes[kSceneNodeCapacity];   // *(0x13CE290)
int      g_sceneNodeCount = 0;                // dword_6498C0 @0x6498C0
bool     g_personArrayLoaded = false;         // dword_13CE294/13CE298 != 0
bool     g_sceneArrayLoaded  = false;         // dword_13CE27C != 0

void ResetEntityArrays() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        g_persons[i].marker = -1; // free
        g_personIds[i] = 0;
    }
    for (int i = 0; i < kObjectCapacity; ++i)
        g_objects[i].alive = 0;
    for (int i = 0; i < kSceneNodeCapacity; ++i) {
        g_sceneNodes[i].type = 0;
        g_sceneNodes[i].childPtr = -1;
        g_sceneNodes[i].entityPtr = -1;
    }
    g_sceneNodeCount = 0;
    g_personArrayLoaded = false;
    g_sceneArrayLoaded = false;
}

// ===========================================================================
// id -> record lookups.
// ===========================================================================

// gilde.exe 0x58bc6c — VIBE_Person_FindRecordById.
// Original: v2 byte cursor over word_12CE910; compares the marker word and the
// id in the parallel column dword_12CE914. 411648 == 536 * 768.
Person* PersonFindRecordById(i32 id) {
    int i = 0;
    while (g_persons[i].marker == -1 || id != g_personIds[i]) {
        if (++i >= kPersonCapacity) // i.e. byte cursor >= 411648
            return nullptr;
    }
    return &g_persons[i];
}

// gilde.exe 0x587b20 — VIBE_Building_FindById.
// Original: byte cursor over the object array; alive byte @+0, id @+1.
// 43264 == 169 * 256.
ObjectRec* BuildingFindById(i32 id) {
    int i = 0;
    while (g_objects[i].alive == 0 || id != g_objects[i].id) {
        if (++i >= kObjectCapacity)
            return nullptr;
    }
    return &g_objects[i];
}

// gilde.exe 0x583b44 — VIBE_GameObject_ResolveEntityById.
int GameObjectResolveEntityById(ObjectRec** outObject, SceneNode** outScene,
                                i32 id, Person** outPerson) {
    int sceneCount = g_sceneNodeCount; // v4 = dword_6498C0

    // if (!dword_13CE27C) goto LABEL_23 (return v7 == dword_13CE27C == 0).
    if (!g_sceneArrayLoaded)
        return 0;

    if (outObject) *outObject = nullptr;
    if (outScene)  *outScene  = nullptr;

    // ---- Person search (only if outPerson provided) ----
    if (outPerson) {
        *outPerson = nullptr;
        int i = 0;
        for (;;) {
            // *v8 == -1 || a3 != *((_DWORD*)v8 + 1)  ->  marker / id@+4
            if (g_persons[i].marker != -1 && id == g_persons[i].id) {
                *outPerson = &g_persons[i];
                g_sceneNodeCount = sceneCount;
                return 3;
            }
            if (++i >= kPersonCapacity)
                break; // LABEL_11
        }
    }

    // ---- Object/Building search (only if outObject provided) ----
    if (outObject) {
        int i = 0;
        for (;;) {
            // !*(_BYTE*)v10 || a3 != *(_DWORD*)(v10+1)
            if (g_objects[i].alive != 0 && id == g_objects[i].id) {
                *outObject = &g_objects[i];
                g_sceneNodeCount = sceneCount;
                return 1;
            }
            if (++i >= kObjectCapacity)
                break; // LABEL_16
        }
    }

    // ---- Scene-node search (only if outScene provided) ----
    if (!outScene || sceneCount <= 0) {
        g_sceneNodeCount = sceneCount;
        return 0;
    }
    int v13 = 0;
    int idx = 0;
    for (;;) {
        // while (!*(_WORD*)v12) v12 += 67 — skip empty slots
        if (g_sceneNodes[idx].type == 0) {
            ++idx;
            if (v13 >= sceneCount) { g_sceneNodeCount = sceneCount; return 0; }
            continue;
        }
        if (id == g_sceneNodes[idx].id) {
            *outScene = &g_sceneNodes[idx];
            g_sceneNodeCount = sceneCount;
            return 2;
        }
        ++v13;
        ++idx;
        if (v13 >= sceneCount) { g_sceneNodeCount = sceneCount; return 0; }
    }
}

// gilde.exe 0x5917d4 — VIBE_GameObject_ResolveTypeFieldB.
// Original: ResolveEntityById(v4 /*outObj*/, 0 /*outScene*/, id /*node+10*/,
//                             &v3 /*outPerson*/). Because outPerson is non-null,
// ResolveEntityById searches Person first (returns 3) and Object second. Then:
//   if v3 (person)  -> *v3  (person marker word, +0)
//   else if v4[0]   -> *(object + 39)
//   else            -> -1
i16 GameObjectResolveTypeFieldB(SceneNode* node, int a2) {
    ObjectRec* obj = nullptr;   // v4[0]
    Person*    person = nullptr; // v3
    (void)a2;                    // v4[2] = a2 scratch; not read by resolution
    GameObjectResolveEntityById(&obj, nullptr,
                                static_cast<i32>(node->ownerId), &person);
    if (person)
        return person->marker;                  // *v3 (person +0 word)
    if (obj)
        return ReadWordAt(obj, 39);             // *(_WORD*)(v4[0] + 39)
    return -1;
}

// ===========================================================================
// Person query / iterator (operates on g_objects).
// Query state — mirrors the global block at 0x6498A8.
// ===========================================================================
namespace {
i8  q_alive    = -1;        // byte_6498A8
i32 q_id       = -1;        // dword_6498AC
i16 q_faction  = 0x7FFF;    // word_6498B4
i16 q_owner    = 0x7FFF;    // word_6498B6
i8  q_aiByte   = -1;        // byte_6498B8
bool q_matchAny = false;    // byte_6498B9
bool q_first    = true;     // byte_6498BA  (first-call flag)
int q_count     = 0;        // dword_6498BC (records examined)
int q_cursor    = 0;        // dword_6498DC (current object index)
} // namespace

// gilde.exe 0x586a6c — VIBE_Person_IterNext.
ObjectRec* PersonIterNext() {
    int i = q_cursor;
    int examined = q_count;

    // if (!loaded || q_count > 256) -> null
    if (!g_personArrayLoaded || q_count > kObjectCapacity) {
        q_cursor = i;       // dword_6498DC keeps last value in orig (v0 unchanged)
        return nullptr;
    }

    // Advance to the next alive slot. First call does not pre-advance.
    if (q_first) {
        q_first = false;
        while (i < kObjectCapacity && g_objects[i].alive == 0)
            ++i;
    } else {
        do { ++i; } while (i < kObjectCapacity && g_objects[i].alive == 0);
    }

    if (i >= kObjectCapacity) {
        q_count = examined;
        q_cursor = i;
        return nullptr;
    }

    bool match = false;
    do {
        bool m = false;
        if (q_matchAny) m = true;
        bool fail = false;

        if (q_alive != -1) {
            if (g_objects[i].alive != static_cast<u8>(q_alive)) fail = true;
            else m = true;
        }
        if (!fail && q_id != -1) {
            if (q_id != g_objects[i].id) fail = true;
            else m = true;
        }
        if (!fail && q_faction != 0x7FFF) {
            if (ReadWordAt(&g_objects[i], 37) != q_faction) fail = true;
            else m = true;
        }
        if (!fail && q_owner != 0x7FFF) {
            if (ReadWordAt(&g_objects[i], 39) != q_owner) fail = true;
            else m = true;
        }
        if (!fail && q_aiByte != -1) {
            // *(_BYTE*)(589 * *v0 + dword_13CE294) == byte_6498B8 — the
            // AiPlayer/building-type descriptor byte (+0 of the 589-stride type
            // record) indexed by the OBJECT record's +0 type byte. We consult the
            // canonical type table (sim::g_buildingTypes) when loaded; when the
            // table is absent (matching the orig's dword_13CE294-null bail) the
            // filter cannot match by table, so only match-any / prior filters
            // decide. Faithful "m = 1 on equality".
            if (g_buildingTypesLoaded) {
                u8 typeByte = g_objects[i].alive;  // object +0 type byte
                if (g_buildingTypes[typeByte].kind == static_cast<u8>(q_aiByte))
                    m = true;
            }
        }

        match = fail ? false : m;

        ++examined;
        if (!match) {
            do { ++i; } while (i < kObjectCapacity && g_objects[i].alive == 0);
        }
    } while (i < kObjectCapacity && !match);

    if (!match)
        i = -1; // v0 = 0 in orig; sentinel "null"
    q_count = examined;
    q_cursor = (i < 0) ? kObjectCapacity : i;
    return (i < 0) ? nullptr : &g_objects[i];
}

// gilde.exe 0x586c20 — VIBE_Person_QueryBegin.
ObjectRec* PersonQueryBegin(const PersonFilter* filters, int count) {
    if (!g_personArrayLoaded)
        return nullptr;

    q_alive    = -1;
    q_id       = -1;
    q_faction  = 0x7FFF;
    q_owner    = 0x7FFF;
    q_aiByte   = -1;
    q_count    = 0;
    q_first    = true;
    q_matchAny = false;

    for (int i = 0; i < count; ++i) {
        switch (filters[i].op) {
            case 0: q_alive   = static_cast<i8>(filters[i].value); break;
            case 1: q_id      = filters[i].value;                  break;
            // case 2: name/string match — needs the string column; ignored.
            case 3: q_faction = static_cast<i16>(filters[i].value); break;
            case 4: q_owner   = static_cast<i16>(filters[i].value); break;
            case 5: q_aiByte  = static_cast<i8>(filters[i].value);  break;
            case 6: q_matchAny = true;                              break;
            default: break;
        }
    }
    q_cursor = 0; // dword_6498DC = dword_13CE298 (array base) -> index 0
    return PersonIterNext();
}

// ===========================================================================
// GameObject scene-tree query / iterator (DFS).
// State — mirrors the global block at 0x6498C4..0x6498E0 + the DFS stack.
// ===========================================================================
namespace {
i16  s_type    = 0x7FFF;   // word_6498C4
i32  s_id      = -1;       // dword_6498C8
i16  s_fieldB  = -1;       // word_6498CC
i8   s_typedef = -1;       // byte_6498CE
bool s_first   = false;    // byte_6498CF
bool s_matchAny = false;   // byte_6498D6
bool s_flat    = false;    // byte_6498D4
bool s_useStack = false;   // byte_6498D5
int  s_flatIdx = 0;        // dword_6498D0
int  s_current = -1;       // dword_6498E0 (current node index, -1 == null)
int  s_depth   = 0;        // dword_13CE274 (DFS stack depth)
int  s_stack[kSceneNodeCapacity + 8]; // dword_12356CC (DFS stack of node idx)
} // namespace

// gilde.exe 0x58529c — VIBE_GameObject_IterNext.
SceneNode* GameObjectIterNext() {
    int v0 = s_current;     // edx — current node index
    int v1 = s_depth;       // ecx — stack depth
    int result = -1;        // esi — matched node index
    bool match = false;     // bl

    if (v0 < 0) {           // !dword_6498E0
        s_current = -1;
        return nullptr;
    }

    if (s_first) {          // byte_6498CF (first iteration)
        s_first = false;
        if (v0 >= 0 && s_useStack) {
            int sub = g_sceneNodes[v0].entityPtr; // *(+20)
            if (sub >= 0) {
                v1 = s_depth + 1;
                s_stack[v1] = sub;
            }
        }
    }

    while (s_flatIdx < g_sceneNodeCount && v0 >= 0 && !match) {
        // Skip empty slots (flat mode walks the array; tree mode the node is
        // already non-empty but the orig still guards). The original scan is
        // unbounded; we cap at capacity so the model never reads OOB.
        while (v0 < kSceneNodeCapacity && g_sceneNodes[v0].type == 0)
            ++v0;
        if (v0 >= kSceneNodeCapacity)
            break;

        if (s_matchAny)
            match = true;
        if (s_type != 0x7FFF) {
            if (g_sceneNodes[v0].type != s_type) { match = false; goto cont; }
            match = true;
        }
        if (s_id != -1) {
            if (s_id != g_sceneNodes[v0].id) { match = false; goto cont; }
            match = true;
        }
        s_depth = v1;
        if (s_fieldB != -1) {
            s_current = v0;
            i16 fb = GameObjectResolveTypeFieldB(&g_sceneNodes[v0], v1);
            v0 = s_current;
            if (fb != s_fieldB) { v1 = s_depth; match = false; goto cont; }
            match = true;
        }
        v1 = s_depth;
        if (s_typedef != -1) {
            // *(char*)(dword_13CE27C + 65 * type) — type-def table byte.
            // We have no loaded type-def table; treat the node type's low byte
            // as the descriptor (TODO: wire to the real 65-byte type-def array).
            i8 td = static_cast<i8>(g_sceneNodes[v0].type & 0xFF);
            if (td != s_typedef) { match = false; goto cont; }
            match = true;
        }

    cont:
        if (s_flat) {
            if (match) result = v0;
            ++v0;
            ++s_flatIdx;
        } else {
            if (match) result = v0;
            v0 = g_sceneNodes[v0].childPtr; // *(+63)
            if (v0 >= 0) {
                int sub = g_sceneNodes[v0].entityPtr; // *((_DWORD*)v0+5)
                if (sub >= 0 && s_useStack)
                    s_stack[++v1] = sub;
            }
            if (v0 < 0) {
                if (s_useStack && v1) {
                    v0 = s_stack[v1--];
                    int sub = g_sceneNodes[v0].entityPtr;
                    if (sub >= 0)
                        s_stack[++v1] = sub;
                }
            }
        }
    }

    s_depth = v1;
    s_current = v0;
    return (result < 0) ? nullptr : &g_sceneNodes[result];
}

// gilde.exe 0x5857fc — VIBE_GameObject_QueryFind.
SceneNode* GameObjectQueryFind(int startIndex, const SceneFilter* filters,
                               int count) {
    if (!g_sceneArrayLoaded)
        return nullptr;

    int filterId   = -1;    // v3
    i16 filterFB   = -1;    // v4
    s_type    = 0x7FFF;
    s_typedef = -1;
    s_first   = true;
    s_flatIdx = 0;
    s_depth   = 0;
    s_matchAny = false;
    s_flat     = false;
    s_useStack = false;

    for (int i = 0; i < count; ++i) {
        switch (filters[i].op) {
            case 0: s_type    = static_cast<i16>(filters[i].value); break;
            case 1: filterId  = filters[i].value;                   break;
            case 3: filterFB  = static_cast<i16>(filters[i].value); break;
            case 4: s_typedef = static_cast<i8>(filters[i].value);  break;
            case 5: s_matchAny = true;                              break;
            case 6: s_useStack = true;                              break;
            case 7: s_flat     = true;                              break;
            default: break;
        }
    }

    if (s_flat) {
        if (!s_useStack) {
            s_id = filterId;
            s_fieldB = filterFB;
            s_current = 0;          // dword_13CE290 array base -> index 0
            return GameObjectIterNext();
        }
        s_id = filterId;
        s_fieldB = filterFB;
        return nullptr;
    }
    s_id = filterId;
    s_fieldB = filterFB;
    if (startIndex >= 0) {
        s_current = startIndex;
        return GameObjectIterNext();
    }
    return nullptr;
}

} // namespace guild::sim
