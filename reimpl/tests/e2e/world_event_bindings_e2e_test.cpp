// End-to-end test for the guild::world event-binding table.
//
// Flow exercised (mirrors the d3e engine's save/load of an object's event table):
//   1. Build a binding table by registering several named handlers
//      (VIBE_Event_RegisterEvent over the 7-slot owner table).
//   2. Serialize it to a byte stream (VIBE_Event_WriteEventNames).
//   3. Deserialize into a fresh table (VIBE_Event_LoadEventBindings), resolving
//      each event name back to its id and re-registering with a handler factory.
//   4. Verify the rebuilt table matches the original slot-for-slot, that the
//      populated count survives, and that remove-to-empty frees the table.
#include "test.h"

#include "world/event_bindings.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {
int g_factoryCalls = 0;
void* FakeHandlerFactory() {
    ++g_factoryCalls;
    return reinterpret_cast<void*>(0xABCD0000u + g_factoryCalls);
}
}

TEST(WorldEventBindingsE2E, SaveLoadRoundTrip) {
    g_factoryCalls = 0;

    // --- 1. build the source table -----------------------------------------
    EventBindingTable src;
    void* h0 = reinterpret_cast<void*>(0x1000);
    void* h1 = reinterpret_cast<void*>(0x2000);
    void* h2 = reinterpret_cast<void*>(0x3000);
    CHECK_EQ(RegisterEvent(&src, LookupNameToId("ZOOM_IN_OBJECT"), "zoomIn",  h0), 1);
    CHECK_EQ(RegisterEvent(&src, LookupNameToId("SCENE_ENTER"),    "onEnter", h1), 1);
    CHECK_EQ(RegisterEvent(&src, LookupNameToId("SCENE_SUPERVISOR"),"superv", h2), 1);
    CHECK_EQ(src.CountPopulated(), 3);

    // --- 2. serialize -------------------------------------------------------
    ByteWriter w;
    WriteEventNames(w, &src);
    CHECK(w.bytes.size() > 4);   // count + three name/handler pairs

    // The leading dword is the populated count (3).
    ByteReader peek{w.bytes.data(), w.bytes.size(), 0};
    CHECK_EQ(peek.ReadDword(), (u32)3);

    // --- 3. deserialize into a fresh table ---------------------------------
    EventBindingTable dst;
    ByteReader r{w.bytes.data(), w.bytes.size(), 0};
    int pairs = LoadEventBindings(r, &dst, &FakeHandlerFactory);
    CHECK_EQ(pairs, 3);
    CHECK_EQ(g_factoryCalls, 3);          // a handler synthesized per pair

    // --- 4. verify the rebuilt table ---------------------------------------
    CHECK_EQ(dst.CountPopulated(), 3);
    CHECK(dst.allocated);
    // names round-trip into the correct slots (slot index == event id)
    CHECK(dst.slots[LookupNameToId("ZOOM_IN_OBJECT")].handler != nullptr);
    CHECK(std::strcmp(dst.slots[LookupNameToId("ZOOM_IN_OBJECT")].name, "zoomIn") == 0);
    CHECK(std::strcmp(dst.slots[LookupNameToId("SCENE_ENTER")].name, "onEnter") == 0);
    CHECK(std::strcmp(dst.slots[LookupNameToId("SCENE_SUPERVISOR")].name, "superv") == 0);

    // re-serializing the rebuilt table yields byte-identical output (the format
    // is stable: count + (event-name, handler-name) in slot order).
    ByteWriter w2;
    WriteEventNames(w2, &dst);
    CHECK(w2.bytes == w.bytes);
}

TEST(WorldEventBindingsE2E, LoadWithoutTableJustCountsPairs) {
    // LoadEventBindings with a null table still consumes the stream and returns
    // the pair count (mirrors the original's `if (a2)` guard around register).
    ByteWriter w;
    w.WriteDword(2);
    w.WriteString("SCENE_EXIT");
    w.WriteString("h1");
    w.WriteString("TEST");
    w.WriteString("h2");
    ByteReader r{w.bytes.data(), w.bytes.size(), 0};
    int n = LoadEventBindings(r, nullptr, &FakeHandlerFactory);
    CHECK_EQ(n, 2);
}

TEST(WorldEventBindingsE2E, BuildThenTearDown) {
    // Register all reachable ids, then remove them one by one; the table must
    // stay allocated until the final removal empties it.
    EventBindingTable t;
    void* h = reinterpret_cast<void*>(0x55);
    const u8 ids[] = {2, 3, 4, 5, 6};
    for (u8 id : ids)
        CHECK_EQ(RegisterEvent(&t, id, "h", h), 1);
    CHECK_EQ(t.CountPopulated(), 5);

    for (int i = 0; i < 4; ++i) {
        RegisterEvent(&t, ids[i], nullptr, nullptr);
        CHECK(t.allocated);     // still has at least one populated slot
    }
    RegisterEvent(&t, ids[4], nullptr, nullptr);
    CHECK(t.Empty());
    CHECK(!t.allocated);        // last removal frees the table
}
