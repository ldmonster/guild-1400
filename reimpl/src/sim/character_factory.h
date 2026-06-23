#pragma once
// character_factory — the character-from-model FACTORY trio of gilde.exe
// (32-bit x86, imagebase 0x400000). This is the single path through which EVERY
// character record is created: the dynasty/menu actors (menu_actor.cpp's
// CreateMenuDummyActor), the live in-game NPCs (personnel/recruit spawns), the
// scripted actors (script_import3.cpp's HostCreateFromModel) and the animals
// (animal.cpp) all bottom out in VIBE_Character_CreateFromModel.
//
// Translated functions (absolute addresses):
//   0x402254 VIBE_Character_AllocSlot       (REUSED from character_path.cpp —
//                                            NOT redefined here; see note below)
//   0x4029c4 VIBE_Character_CreateMesh      (the factory body)
//   0x402d10 VIBE_Character_CreateFromModel (the trivial wrapper)
//
// AllocSlot @0x402254 is ALREADY reconstructed 1:1 in sim/character_path.cpp as
// guild::sim::AllocSlot() (returning LiveActor*, filling the real 512-entry
// g_live table from character_query.cpp). We REUSE it verbatim — there is exactly
// one definition in the unified build. CreateFromModel calls it, then hands the
// returned 516-byte record to CreateMesh, which writes the model-load result and
// the many initial fields/flags by raw byte offset, exactly as the binary does.
//
// CreateMesh's genuine scene-graph / model-load / anim leaves
// (Object_AttachToUniverseNode @0x5b3e30, IndexFromPointer @0x426724,
// QueryTerrainType @0x404650, BuildObjectCache @0x5c8218,
// PropagateDirtyFlag @0x5af2c0, the loc_5CB930 strstr creature probe,
// PreloadAniSet @0x403c34 / PreloadLowPolyAniSet @0x403da0 / UpdateLowPolyMesh
// @0x40244c) live in OTHER modules; they are routed through an installable
// CharacterFactoryHooks dispatch table whose default implementation is inert, so
// the pure control flow — the name decomposition, the raw-offset field/flag
// writes, the creature-type branch and the preload calls — is golden-testable.
// (PreloadAniSet / PreloadLowPolyAniSet are full reconstructions in
// sim/character_render3.cpp; the factory reaches them through hooks so the test
// can record the exact clip names without dragging the anim base in.)
#include "guild/common/types.h"

namespace guild::sim {

struct LiveActor;   // character_query.h — the dword_66F0D0[512] 516-byte record.

// ===========================================================================
// Recovered constants (imagebase 0x400000).
//   dword_401010[0..3] == {0,0,0,0}  : the 4-dword transform seed passed to
//                                       Object_AttachToUniverseNode (a zero offset).
//   1065353216 == 0x3F800000 == 1.0f : the initial actor scale (rec+416).
//   1072064102 == 0x3FE66666 == 1.8f : the node anim-rate float (*(node+492)+2296).
//   50331648   == 0x3000000          : the node bookkeeping word (node+72).
//   byte_610134 == ""                : the no-'_' default base-name (an empty string).
//   loc_5CB930 creature needles: "RATTE","HUND","KATZE","PFERD".
// ===========================================================================
constexpr float kInitialScale   = 1.0f;          // 0x3F800000  -> rec+416
constexpr float kNodeAnimRate   = 1.7999999523162842f;  // 0x3FE66666 -> (node+492)+2296
constexpr int   kNodeWord72     = 0x3000000;     // 50331648    -> node+72

// Raw field offsets in the 516-byte record (the originals all use *(rec+N)).
//   +0   slot index           (AllocSlot writes it back)
//   +4   creature-type byte   (1 human / 2 animal, |8 horse, |8 rat-clear)
//   +5   full model-name str  (256-byte char field; loc_5CB930 reads it)
//   +40  script handle        (set to -1)
//   +44  universe id          (set to -1)
//   +48  group id             (set to -1)
//   +52  mesh / object node   (Object_AttachToUniverseNode result)
//   +136 owning universe ptr  (off_649D64 active scene)
//   +141 flag byte B          (|= 0x10 idle-anim pending)
//   +304 base-name field      (the post-first-'_' .. pre-second-'_' substring)
//   +368 prefix field         (everything before the first '_')
//   +416 scale float          (1.0f)
//   +492 low-poly object ptr  (gates the low-poly preload)
constexpr int kRecSlotIndex   = 0;
constexpr int kRecTypeByte    = 4;
constexpr int kRecModelName   = 5;
constexpr int kRecScriptH     = 40;
constexpr int kRecUniverseId  = 44;
constexpr int kRecGroupId     = 48;
constexpr int kRecMesh        = 52;
constexpr int kRecUniverse    = 136;
constexpr int kRecFlagsB      = 141;
constexpr int kRecBaseName    = 304;
constexpr int kRecPrefix      = 368;
constexpr int kRecScale       = 416;
constexpr int kRecLowPoly     = 492;

// Object-node (rec+52 == the scene node) raw flag offsets touched by CreateMesh.
//   +529 flag byte (network/visibility bits 0x02/0x04/0x08)
//   +530 flag byte (|= 0x0C)
//   +531 flag byte (&= ~0x04)
//   +535 byte = 2
//   +536 dword = 1
//   +72  bookkeeping dword = 0x3000000
//   +492 -> sub-object; (*(node+492)+2296) = 1.8f anim-rate float
constexpr int kNodeFlag529 = 529;
constexpr int kNodeFlag530 = 530;
constexpr int kNodeFlag531 = 531;
constexpr int kNodeByte535 = 535;
constexpr int kNodeDw536   = 536;
constexpr int kNodeWord72Off = 72;
constexpr int kNodeSubObj  = 492;     // *(node+492) -> sub-object; +2296 anim rate

// ===========================================================================
// Cross-module dispatch hooks. Each genuine scene-graph / model-load / anim leaf
// the factory body calls is a slot here; the default table is inert so the
// control flow (name split, field/flag writes, creature branch, preloads) is
// testable in isolation. Tests install a recording mock. Handles are opaque
// pointers; the originals store 32-bit handles.
// ===========================================================================
struct CharacterFactoryHooks {
    // VIBE_Object_AttachToUniverseNode(0, parentMat, model, seed, &nameOut) @0x5b3e30 —
    // load the .bgf model and attach a scene node under `parentMat`. Returns the
    // node (rec+52), or null when the model cannot be loaded. `seed` is the
    // dword_401010 4-dword zero offset.
    void* (*attachToUniverseNode)(void* parentMat, const char* model);
    // VIBE_Character_IndexFromPointer(universe) @0x426724 — universe ptr -> slot
    // index 0..63, or 0xFFFFFFFF (-1) when out of range. The factory uses the raw
    // value's truthiness (slot 0 and "no universe yet" are both falsy).
    unsigned (*indexFromPointer)(void* universe);
    // VIBE_Character_QueryTerrainType(rec, 0) @0x404650.
    void (*queryTerrainType)(LiveActor* rec);
    // VIBE_Light_BuildObjectCache(node) @0x5c8218.
    void (*buildObjectCache)(void* node);
    // VIBE_Object_PropagateDirtyFlag(node, 1) @0x5af2c0.
    void (*propagateDirtyFlag)(void* node);
    // loc_5CB930 — substring search (strstr): returns nonzero when `needle` occurs
    // in `haystack` (rec+5, the model name). The four creature probes use it.
    int (*findSubstring)(const char* haystack, const char* needle);
    // VIBE_Character_PreloadAniSet(rec, count, names...) @0x403c34. The factory
    // always preloads {"bewegung/gehen","stehen/stehen_newnoise"}.
    void (*preloadAniSet)(LiveActor* rec, const char* const* names, int count);
    // VIBE_Character_PreloadLowPolyAniSet(rec, 1, "gehen") @0x403da0.
    void (*preloadLowPolyAniSet)(LiveActor* rec, const char* const* names, int count);
    // VIBE_Character_UpdateLowPolyMesh(rec) @0x40244c.
    void (*updateLowPolyMesh)(LiveActor* rec);
    // dword_62D088 — global "low-poly enabled" gate (nonzero => preload low-poly).
    int lowPolyEnabled;
    // dword_62D010 & 1 — the network-universe broadcast bit folded into node+529
    // when the universe is not yet a local slot.
    int networkBit;
    // VIBE_ErrorLog_ReportMessage(msg) @0x438da8 — the model-load failure report.
    void (*reportError)(const char* msg);
    // VIBE_Character_Destroy(rec) @0x402120 — teardown on the CreateFromModel
    // failure path (clears the g_live slot + frees the record). Modeled as a hook
    // because Destroy is a large reconstruction target of its own (see report).
    void (*destroy)(LiveActor* rec);
};

// Install hooks; nullptr restores the inert defaults. Returns the previous set.
CharacterFactoryHooks SetCharacterFactoryHooks(const CharacterFactoryHooks* hooks);
const CharacterFactoryHooks& GetCharacterFactoryHooks();

// ===========================================================================
// Name decomposition — the pure, testable core of CreateMesh (disasm @0x402a4c).
//
// The original copies the model name into a 256-byte scratch buffer, then:
//   p = strchr(buf, '_');                       // first '_'
//   if (p) {                                     // there is at least one '_'
//       q = strchr(p+1, '_'); if (q) *q = 0;     // cut at the SECOND '_'
//       strcpy(rec+304, p+1);                    // base = [after 1st '_' .. 2nd '_')
//       p[-1] = 0;                               // zero the FIRST '_' in buf
//       strcpy(rec+368, buf);                    // prefix = [.. first '_')
//   } else {
//       strcpy(rec+304, byte_610134 /* "" */);   // no '_' -> empty base
//       // rec+368 (prefix) is left untouched.
//   }
// So for "dieb_MANN2"      -> base="MANN2", prefix="dieb".
//        "dieb_MANN2_alt"  -> base="MANN2", prefix="dieb"  (tail after 2nd '_' dropped).
//        "MANN2"           -> base="",      prefix unchanged.
// `outBase`/`outPrefix` must each hold >= 256 bytes. `wrotePrefix` reports whether
// the prefix field was written (false on the no-'_' path).
// ===========================================================================
void DecomposeModelName(const char* model, char outBase[256], char outPrefix[256],
                        bool* wrotePrefix);

// ===========================================================================
// The factory trio.
// ===========================================================================

// gilde.exe 0x4029c4 — VIBE_Character_CreateMesh (__usercall, eax=rec, edx=model,
// ebx=parentMat). Loads the model, attaches its scene node, decomposes the name,
// writes the initial fields/flags, runs the creature-type probe, preloads the
// gait+idle anim set (and the low-poly set when enabled), and returns 1. On a
// model-load failure it reports the error and returns 0 (rec+40 already set to -1).
// Returns 0 immediately if `rec` is null.
int CreateMesh(LiveActor* rec, const char* model, void* parentMat = nullptr);

// gilde.exe 0x402d10 — VIBE_Character_CreateFromModel (__usercall, eax=model,
// edi=universe, edx=parentMat). AllocSlot a record, CreateMesh it from the model;
// on success return the record, else Destroy the record and return null.
LiveActor* CreateFromModel(const char* model, void* parentMat = nullptr);

} // namespace guild::sim
