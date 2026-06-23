// Golden unit tests for the character-from-model factory trio (gilde.exe):
//   0x402254 VIBE_Character_AllocSlot       (reused from character_path.cpp)
//   0x4029c4 VIBE_Character_CreateMesh
//   0x402d10 VIBE_Character_CreateFromModel
//
// No assets. The genuine scene-graph / model-load / anim leaves are routed through
// CharacterFactoryHooks; a recording mock observes the exact field/flag writes, the
// name decomposition, the creature probe and the preload clip names.
#include "tests/framework/test.h"

#include "sim/character_factory.h"
#include "sim/character_path.h"     // AllocSlot
#include "sim/character_query.h"    // LiveActor, g_live, kLiveCapacity, ResetCharacterQuery

#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild::sim;

namespace {

// Raw byte view of a record/node (the factory writes by raw byte offset).
inline unsigned char* B(void* p) { return static_cast<unsigned char*>(p); }
inline int   RdI32(void* p, int off) { int v; std::memcpy(&v, B(p) + off, 4); return v; }
inline float RdF32(void* p, int off) { float v; std::memcpy(&v, B(p) + off, 4); return v; }
inline const char* RdStr(void* p, int off) {
    return reinterpret_cast<const char*>(B(p) + off);
}

// A recording mock for CharacterFactoryHooks. A single static instance backs the
// installed table (the hook fn-pointers can't capture).
struct Recorder {
    // attach
    void*       returnNode = nullptr;     // what attachToUniverseNode hands back
    int         attachCalls = 0;
    std::string attachModel;
    // index-from-pointer truthiness control
    unsigned    indexValue = 0;
    int         indexCalls = 0;
    // substring (creature) — the real strstr is fine for the golden split
    int         substrCalls = 0;
    // leaf call observations
    int         queryTerrainCalls = 0;
    int         buildCacheCalls = 0;
    int         propagateCalls = 0;
    int         updateLowCalls = 0;
    // preload recordings
    std::vector<std::string> preloadNames;   // flattened, in call order
    std::vector<std::string> preloadLowNames;
    // error / destroy
    std::string lastError;
    int         errorCalls = 0;
    int         destroyCalls = 0;
    LiveActor*  destroyedRec = nullptr;
};
Recorder g_rec;

void* H_Attach(void*, const char* model) {
    g_rec.attachCalls++;
    g_rec.attachModel = model ? model : "";
    return g_rec.returnNode;
}
unsigned H_Index(void*) { g_rec.indexCalls++; return g_rec.indexValue; }
void H_QueryTerrain(LiveActor*) { g_rec.queryTerrainCalls++; }
void H_BuildCache(void*) { g_rec.buildCacheCalls++; }
void H_Propagate(void*) { g_rec.propagateCalls++; }
int  H_Substr(const char* h, const char* n) {
    g_rec.substrCalls++;
    if (!h || !n) return 0;
    return std::strstr(h, n) != nullptr;
}
void H_PreloadAni(LiveActor*, const char* const* names, int count) {
    for (int i = 0; i < count; ++i) g_rec.preloadNames.emplace_back(names[i]);
}
void H_PreloadLow(LiveActor*, const char* const* names, int count) {
    for (int i = 0; i < count; ++i) g_rec.preloadLowNames.emplace_back(names[i]);
}
void H_UpdateLow(LiveActor*) { g_rec.updateLowCalls++; }
void H_Report(const char* m) { g_rec.errorCalls++; g_rec.lastError = m ? m : ""; }
void H_Destroy(LiveActor* rec) {
    g_rec.destroyCalls++;
    g_rec.destroyedRec = rec;
    // Mirror the inert default: drop from g_live + free, so the slot frees up.
    if (rec) {
        for (int i = 0; i < kLiveCapacity; ++i)
            if (g_live[i] == rec) { g_live[i] = nullptr; break; }
        CharacterPathGetHooks().freeDebug(rec);
    }
}

CharacterFactoryHooks MakeMock() {
    CharacterFactoryHooks h{};
    h.attachToUniverseNode = H_Attach;
    h.indexFromPointer = H_Index;
    h.queryTerrainType = H_QueryTerrain;
    h.buildObjectCache = H_BuildCache;
    h.propagateDirtyFlag = H_Propagate;
    h.findSubstring = H_Substr;
    h.preloadAniSet = H_PreloadAni;
    h.preloadLowPolyAniSet = H_PreloadLow;
    h.updateLowPolyMesh = H_UpdateLow;
    h.lowPolyEnabled = 0;
    h.networkBit = 0;
    h.reportError = H_Report;
    h.destroy = H_Destroy;
    return h;
}

// A 516-byte node buffer (the scene node the factory writes flags into).
struct NodeBuf {
    unsigned char raw[600];   // > the highest touched offset (node+536) + slack
    void* subObj = nullptr;   // *(node+492) -> sub-object (anim-rate float at +2296)
    NodeBuf() { std::memset(raw, 0, sizeof(raw)); }
};

// Free every live record (AllocSlot mallocs a 516-byte block; the inert default
// teardown only fires on the destroy path). Tests own the slots they allocate, so
// release them here before ResetCharacterQuery() clears the g_live pointers — keeps
// the ASAN heap clean (wave-11 hardening).
void FreeLiveSlots() {
    for (int i = 0; i < kLiveCapacity; ++i) {
        if (g_live[i] != nullptr &&
            g_live[i] != reinterpret_cast<LiveActor*>(0x1)) {  // skip alloc sentinels
            CharacterPathGetHooks().freeDebug(g_live[i]);
            g_live[i] = nullptr;
        }
    }
}

void ResetAll() {
    FreeLiveSlots();                 // free any leftover records from a prior test
    ResetCharacterQuery();           // clears g_live (and the universe table)
    g_rec = Recorder{};
    SetCharacterFactoryHooks(nullptr);
}

} // namespace

// ===========================================================================
// (a) AllocSlot — first free slot fill + record[0] = index; full-table -> null.
// ===========================================================================
TEST(CharacterFactory, AllocSlotFillsFirstFreeSlot) {
    ResetAll();
    LiveActor* a = AllocSlot();
    CHECK(a != nullptr);
    CHECK(g_live[0] == a);
    CHECK_EQ(a->slotIndex, 0);          // record[0] = 0

    LiveActor* b = AllocSlot();
    CHECK(b != nullptr);
    CHECK(g_live[1] == b);
    CHECK_EQ(b->slotIndex, 1);

    // Free slot 0, the next alloc should re-take it (first free index).
    CharacterPathGetHooks().freeDebug(a);  // release the orphaned record (no leak)
    g_live[0] = nullptr;
    LiveActor* c = AllocSlot();
    CHECK(g_live[0] == c);
    CHECK_EQ(c->slotIndex, 0);
}

TEST(CharacterFactory, AllocSlotFullTableReturnsNull) {
    ResetAll();
    // Fill all 512 slots with a sentinel (non-null) so the scan overflows.
    for (int i = 0; i < kLiveCapacity; ++i)
        g_live[i] = reinterpret_cast<LiveActor*>(0x1);
    LiveActor* a = AllocSlot();
    CHECK(a == nullptr);                // overflow -> record freed, null
    for (int i = 0; i < kLiveCapacity; ++i) g_live[i] = nullptr;  // cleanup sentinels
}

// ===========================================================================
// (b) Name decomposition — base -> +304, prefix -> +368.
// ===========================================================================
TEST(CharacterFactory, NameDecompositionOneUnderscore) {
    char base[256], prefix[256];
    bool wrote = false;
    DecomposeModelName("dieb_MANN2", base, prefix, &wrote);
    CHECK(wrote);
    CHECK_EQ(std::string(base), std::string("MANN2"));
    CHECK_EQ(std::string(prefix), std::string("dieb"));
}

TEST(CharacterFactory, NameDecompositionTwoUnderscores) {
    char base[256], prefix[256];
    bool wrote = false;
    // The tail after the SECOND '_' is dropped; base is the middle token.
    DecomposeModelName("dieb_MANN2_alt", base, prefix, &wrote);
    CHECK(wrote);
    CHECK_EQ(std::string(base), std::string("MANN2"));
    CHECK_EQ(std::string(prefix), std::string("dieb"));
}

TEST(CharacterFactory, NameDecompositionNoUnderscore) {
    char base[256], prefix[256];
    std::strcpy(prefix, "UNTOUCHED");
    bool wrote = true;
    DecomposeModelName("MANN2", base, prefix, &wrote);
    CHECK(!wrote);                                   // prefix field not written
    CHECK_EQ(std::string(base), std::string(""));    // byte_610134 default ("")
    CHECK_EQ(std::string(prefix), std::string("UNTOUCHED"));
}

// Decomposition is exercised through CreateMesh too: the fields land at the raw
// record offsets +304 / +368.
TEST(CharacterFactory, CreateMeshWritesNameFields) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = AllocSlot();
    int r = CreateMesh(rec, "dieb_MANN2");
    CHECK_EQ(r, 1);
    CHECK_EQ(std::string(RdStr(rec, kRecBaseName)), std::string("MANN2"));   // +304
    CHECK_EQ(std::string(RdStr(rec, kRecPrefix)),   std::string("dieb"));    // +368
    CHECK_EQ(std::string(RdStr(rec, kRecModelName)),std::string("dieb_MANN2")); // +5

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (c) CreateMesh success path: scale, flags, preloads, returns 1.
// ===========================================================================
TEST(CharacterFactory, CreateMeshSuccessFieldsAndFlags) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    g_rec.indexValue = 1;                 // a real local universe slot
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = AllocSlot();
    int r = CreateMesh(rec, "buerger_MANN1");
    CHECK_EQ(r, 1);

    // record fields
    CHECK_EQ(RdF32(rec, kRecScale), kInitialScale);       // +416 = 1.0f
    CHECK_EQ(RdI32(rec, kRecScriptH), -1);                // +40  = -1
    CHECK_EQ(RdI32(rec, kRecUniverseId), -1);             // +44  = -1
    CHECK_EQ(RdI32(rec, kRecGroupId), -1);                // +48  = -1
    CHECK_EQ(*(B(rec) + kRecTypeByte), (unsigned char)1); // +4   = 1 (human)
    CHECK((*(B(rec) + kRecFlagsB) & 0x10) != 0);          // +141 |= 0x10

    // object-node flag bytes
    CHECK_EQ(*(B(node.raw) + kNodeFlag530) & 0x0C, 0x0C); // +530 |= 0x0C
    CHECK_EQ(*(B(node.raw) + kNodeFlag529) & 0x02, 0x00); // +529 &= ~0x02
    CHECK_EQ(*(B(node.raw) + kNodeByte535), (unsigned char)2); // +535 = 2
    CHECK_EQ(RdI32(node.raw, kNodeDw536), 1);             // +536 = 1
    CHECK_EQ(*(B(node.raw) + kNodeFlag531) & 0x04, 0x00); // +531 &= ~0x04
    CHECK((*(B(node.raw) + kNodeFlag529) & 0x04) != 0);   // +529 |= 0x04
    CHECK_EQ(*(B(node.raw) + kNodeFlag529) & 0x08, 0x00); // local: +529 &= ~0x08
    CHECK_EQ(RdI32(node.raw, kNodeWord72Off), kNodeWord72); // +72 = 0x3000000
    CHECK_EQ(RdF32(node.subObj, 2296), kNodeAnimRate);     // (node+492)+2296 = 1.8f

    // leaf calls
    CHECK_EQ(g_rec.queryTerrainCalls, 1);
    CHECK_EQ(g_rec.buildCacheCalls, 1);
    CHECK_EQ(g_rec.propagateCalls, 1);

    // preload: exactly {"bewegung/gehen","stehen/stehen_newnoise"}; no low-poly.
    CHECK_EQ((int)g_rec.preloadNames.size(), 2);
    CHECK_EQ(g_rec.preloadNames[0], std::string("bewegung/gehen"));
    CHECK_EQ(g_rec.preloadNames[1], std::string("stehen/stehen_newnoise"));
    CHECK_EQ((int)g_rec.preloadLowNames.size(), 0);

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

// Creature-type probe: a horse model -> type 2 | 8; a rat model -> type 2 + node
// flag tweaks. Low-poly preload fires when enabled and rec+492 is set.
TEST(CharacterFactory, CreateMeshCreatureProbeAndLowPoly) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    g_rec.indexValue = 1;
    mock.lowPolyEnabled = 1;
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = AllocSlot();
    // Give rec a non-null low-poly object so the low-poly preload branch fires.
    void* lowObj = reinterpret_cast<void*>(0xBEEF);
    std::memcpy(B(rec) + kRecLowPoly, &lowObj, sizeof(void*));

    int r = CreateMesh(rec, "PFERD_braun");
    CHECK_EQ(r, 1);
    CHECK((*(B(rec) + kRecTypeByte) & 0x02) != 0);   // animal
    CHECK((*(B(rec) + kRecTypeByte) & 0x08) != 0);   // horse adds bit 8

    // low-poly preloaded ("gehen") + UpdateLowPolyMesh called.
    CHECK_EQ((int)g_rec.preloadLowNames.size(), 1);
    CHECK_EQ(g_rec.preloadLowNames[0], std::string("gehen"));
    CHECK_EQ(g_rec.updateLowCalls, 1);

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (d) Model-load failure: attach returns null -> returns 0 + error reported.
// ===========================================================================
TEST(CharacterFactory, CreateMeshModelLoadFails) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    g_rec.returnNode = nullptr;          // attach fails
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = AllocSlot();
    int r = CreateMesh(rec, "missing_MODEL");
    CHECK_EQ(r, 0);
    CHECK_EQ(RdI32(rec, kRecScriptH), -1);          // +40 = -1 set before the bail
    CHECK_EQ(g_rec.errorCalls, 1);
    CHECK(g_rec.lastError.find("Could not load 3D-Character") != std::string::npos);
    CHECK(g_rec.lastError.find("missing_MODEL") != std::string::npos);

    SetCharacterFactoryHooks(nullptr);
}

TEST(CharacterFactory, CreateMeshNullRecordReturnsZero) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    SetCharacterFactoryHooks(&mock);
    CHECK_EQ(CreateMesh(nullptr, "x"), 0);
    CHECK_EQ(g_rec.attachCalls, 0);                 // never reaches the attach
    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (e) CreateFromModel: success returns the slot; failure destroys + returns null.
// ===========================================================================
TEST(CharacterFactory, CreateFromModelSuccess) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    g_rec.indexValue = 1;
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = CreateFromModel("dieb_MANN2");
    CHECK(rec != nullptr);
    CHECK(g_live[0] == rec);              // AllocSlot filled slot 0
    CHECK_EQ(rec->slotIndex, 0);
    CHECK_EQ(g_rec.destroyCalls, 0);      // success -> no destroy

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

TEST(CharacterFactory, CreateFromModelDestroysOnFailure) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    g_rec.returnNode = nullptr;           // CreateMesh will fail (model load)
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = CreateFromModel("missing_MODEL");
    CHECK(rec == nullptr);                // factory returns null on failure
    CHECK_EQ(g_rec.destroyCalls, 1);      // the allocated slot was destroyed
    CHECK(g_rec.destroyedRec != nullptr);
    CHECK(g_live[0] == nullptr);          // destroy cleared the g_live slot

    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (f) WAVE-11 HARDENING — malformed name inputs must not overrun the 516-byte
//     record / the 256-byte scratch buffers. ASAN exercises the bounds.
// ===========================================================================

// Empty model name: DecomposeModelName -> base "" (no '_'), prefix untouched.
TEST(CharacterFactory, NameDecompositionEmpty) {
    char base[256], prefix[256];
    std::strcpy(prefix, "KEEP");
    bool wrote = true;
    DecomposeModelName("", base, prefix, &wrote);
    CHECK(!wrote);
    CHECK_EQ(std::string(base), std::string(""));
    CHECK_EQ(std::string(prefix), std::string("KEEP"));
}

// Null model pointer: must not crash; base = "" (model? : "").
TEST(CharacterFactory, NameDecompositionNullModel) {
    char base[256], prefix[256];
    bool wrote = true;
    DecomposeModelName(nullptr, base, prefix, &wrote);
    CHECK(!wrote);
    CHECK_EQ(std::string(base), std::string(""));
}

// Overlong tokens: a model name whose prefix/base tokens far exceed the record
// field spans. The decomposition's 256-byte buffers must stay in bounds, and
// CreateMesh must NOT overrun the 516-byte record when copying into +5/+304/+368.
TEST(CharacterFactory, NameDecompositionOverlongTokens) {
    // '_' at position 100 (within the 255-char scratch window) then a 700-char
    // base token — exercises the truncating copy into the 256-byte scratch and
    // the 255-cap on base/prefix.
    std::string model = std::string(100, 'A') + "_" + std::string(700, 'B');
    char base[256], prefix[256];
    bool wrote = false;
    DecomposeModelName(model.c_str(), base, prefix, &wrote);
    CHECK(wrote);
    // strncpy(...,255) truncates; buffers stay NUL-terminated and in bounds.
    CHECK(std::strlen(base) <= 255);
    CHECK(std::strlen(prefix) <= 255);
    CHECK_EQ(std::string(prefix), std::string(100, 'A'));   // prefix == first token
}

// CreateMesh with an overlong model name: the +5 / +304 / +368 string writes must
// stay inside the 516-byte record (ASAN catches any overflow). The full path runs.
TEST(CharacterFactory, CreateMeshOverlongNameStaysInRecord) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    g_rec.indexValue = 1;
    SetCharacterFactoryHooks(&mock);

    // prefix token 300 chars, base token 300 chars — each dwarfs its field span.
    std::string model = std::string(300, 'p') + "_" + std::string(300, 'b');
    LiveActor* rec = AllocSlot();
    int r = CreateMesh(rec, model.c_str());
    CHECK_EQ(r, 1);                                   // full success path ran
    // The model-name field [+5,+40) is bounded; the stored string fits the span
    // and is NUL-terminated (<= 34 chars).
    CHECK(std::strlen(RdStr(rec, kRecModelName)) <= 34);
    // base field [+304,+368) bounded to <= 63 chars.
    CHECK(std::strlen(RdStr(rec, kRecBaseName)) <= 63);
    // prefix field [+368,+416) bounded to <= 47 chars.
    CHECK(std::strlen(RdStr(rec, kRecPrefix)) <= 47);

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (g) WAVE-13 1:1 CONSTANT PINNING — the recovered offsets/constants the factory
//     writes (character-factory.md). Pinned directly here (the success-path test
//     exercises them but never asserts the raw values); every value traces to the
//     progress doc's disasm recovery — none invented.
// ===========================================================================

// The name-decomposition field offsets: base -> +304, prefix -> +368 (disasm
// @0x402a4c). And the prefix span ends where the scale field begins (+416).
TEST(CharacterFactory, ConstNameFieldOffsets) {
    CHECK_EQ(kRecBaseName, 304);
    CHECK_EQ(kRecPrefix, 368);
    CHECK_EQ(kRecScale, 416);
    CHECK_EQ(kRecModelName, 5);     // full model name -> rec+5
}

// The literal float/word constants: scale 1.0f (0x3F800000) -> rec+416, the node
// anim-rate 1.8f (0x3FE66666) -> *(node+492)+2296, and the node word 0x3000000.
TEST(CharacterFactory, ConstLiteralValues) {
    CHECK_EQ(kInitialScale, 1.0f);
    union { float f; unsigned u; } s; s.f = kInitialScale;
    CHECK_EQ(s.u, 0x3F800000u);
    union { float f; unsigned u; } r; r.f = kNodeAnimRate;
    CHECK_EQ(r.u, 0x3FE66666u);     // 1.8f node anim rate
    CHECK_EQ(kNodeWord72, 0x3000000);
}

// The node-flag offsets the factory touches (disasm @0x402ac5..0x402b8e).
TEST(CharacterFactory, ConstNodeFlagOffsets) {
    CHECK_EQ(kNodeFlag529, 529);
    CHECK_EQ(kNodeFlag530, 530);
    CHECK_EQ(kNodeFlag531, 531);
    CHECK_EQ(kNodeByte535, 535);
    CHECK_EQ(kNodeDw536, 536);
    CHECK_EQ(kNodeWord72Off, 72);
    CHECK_EQ(kNodeSubObj, 492);
}

// The creature probe sets type 2 for an animal needle, |8 for PFERD; a rat needle
// also clears node +529 bits 0x08 and 0x04 (disasm @0x402be4/0x402bee). Pins the
// exact node-flag side effects of the RATTE branch the success-path test omits.
TEST(CharacterFactory, ConstCreatureRatNodeFlagClears) {
    ResetAll();
    CharacterFactoryHooks mock = MakeMock();
    NodeBuf node;
    node.subObj = std::malloc(2400);
    std::memset(node.subObj, 0, 2400);
    std::memcpy(node.raw + kNodeSubObj, &node.subObj, sizeof(void*));
    g_rec.returnNode = node.raw;
    g_rec.indexValue = 0;            // network path so +529 carries bit 0x08 first
    SetCharacterFactoryHooks(&mock);

    LiveActor* rec = AllocSlot();
    int r = CreateMesh(rec, "RATTE_grau");
    CHECK_EQ(r, 1);
    CHECK_EQ(*(B(rec) + kRecTypeByte), (unsigned char)2);  // animal, NOT |8 (rat)
    CHECK_EQ(*(B(node.raw) + kNodeFlag529) & 0x08, 0x00);  // rat clears bit 0x08
    CHECK_EQ(*(B(node.raw) + kNodeFlag529) & 0x04, 0x00);  // rat clears bit 0x04

    std::free(node.subObj);
    SetCharacterFactoryHooks(nullptr);
}

// ===========================================================================
// (z) Final teardown — free any record still live so ASAN sees a clean heap.
//     Registration order == run order, so this runs last.
// ===========================================================================
TEST(CharacterFactory, ZZ_TeardownFreesLiveSlots) {
    FreeLiveSlots();
    ResetCharacterQuery();
    for (int i = 0; i < kLiveCapacity; ++i)
        CHECK(g_live[i] == nullptr);
}
