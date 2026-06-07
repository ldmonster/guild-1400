// Unit tests for guild::world event-binding table (event_bindings.{h,cpp}).
// Covers the name<->id table, Register/remove semantics, and the Bio-stream
// primitives, with golden byte vectors for the serialized format.
#include "test.h"

#include "world/event_bindings.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {
void* kFn1 = reinterpret_cast<void*>(0x1111);
void* kFn2 = reinterpret_cast<void*>(0x2222);
}

// --- name <-> id table ------------------------------------------------------
TEST(WorldEventBindings, IdToName) {
    CHECK(std::strcmp(LookupIdToName(0), "NONE") == 0);
    CHECK(std::strcmp(LookupIdToName(1), "TEST") == 0);
    CHECK(std::strcmp(LookupIdToName(2), "ZOOM_IN_OBJECT") == 0);
    CHECK(std::strcmp(LookupIdToName(3), "ZOOM_OUT_OBJECT") == 0);
    CHECK(std::strcmp(LookupIdToName(4), "SCENE_ENTER") == 0);
    CHECK(std::strcmp(LookupIdToName(5), "SCENE_EXIT") == 0);
    CHECK(std::strcmp(LookupIdToName(6), "SCENE_SUPERVISOR") == 0);
    // unknown id falls back to the aNone sentinel
    CHECK(std::strcmp(LookupIdToName(99), "NONE") == 0);
}

TEST(WorldEventBindings, NameToId) {
    CHECK_EQ(LookupNameToId("NONE"), (u8)0);
    CHECK_EQ(LookupNameToId("SCENE_ENTER"), (u8)4);
    CHECK_EQ(LookupNameToId("SCENE_SUPERVISOR"), (u8)6);
    // case-insensitive (VIBE_Util_StrCmpNoCase lowercases A-Z)
    CHECK_EQ(LookupNameToId("scene_exit"), (u8)5);
    CHECK_EQ(LookupNameToId("Zoom_In_Object"), (u8)2);
    // duplicate "TEST" row resolves to the first match (id 1)
    CHECK_EQ(LookupNameToId("TEST"), (u8)1);
    // unknown -> 0
    CHECK_EQ(LookupNameToId("DOES_NOT_EXIST"), (u8)0);
}

// --- RegisterEvent / remove semantics ---------------------------------------
TEST(WorldEventBindings, RegisterNullTableReturnsZero) {
    CHECK_EQ(RegisterEvent(nullptr, 4, "h", kFn1), 0);
}

TEST(WorldEventBindings, RegisterIdZeroNoOp) {
    EventBindingTable t;
    CHECK_EQ(RegisterEvent(&t, 0, "h", kFn1), 1);
    CHECK(t.Empty());
    CHECK(!t.allocated);
}

TEST(WorldEventBindings, RegisterInstallsHandlerAndName) {
    EventBindingTable t;
    CHECK_EQ(RegisterEvent(&t, 4, "OnSceneEnter", kFn1), 1);
    CHECK(t.allocated);
    CHECK_EQ(t.slots[4].handler, kFn1);
    CHECK(std::strcmp(t.slots[4].name, "OnSceneEnter") == 0);
    CHECK_EQ(t.slots[4].flag, (u8)0);
    CHECK_EQ(t.CountPopulated(), 1);
}

TEST(WorldEventBindings, RegisterNamePaddedAndTruncated) {
    EventBindingTable t;
    // a name exactly at the 127 cap is fully copied; the field stays NUL after.
    std::string longName(kEventNameMax, 'x');     // 127 'x'
    RegisterEvent(&t, 5, longName.c_str(), kFn1);
    CHECK_EQ((int)std::strlen(t.slots[5].name), kEventNameMax);
    CHECK_EQ(t.slots[5].name[kEventNameMax], '\0');   // trailing NUL preserved
    // an over-length name is truncated to 127 chars (StrNCopyPad cap).
    std::string tooLong(200, 'y');
    RegisterEvent(&t, 6, tooLong.c_str(), kFn2);
    CHECK_EQ((int)std::strlen(t.slots[6].name), kEventNameMax);
}

TEST(WorldEventBindings, RemoveClearsSlotKeepsTable) {
    EventBindingTable t;
    RegisterEvent(&t, 2, "a", kFn1);
    RegisterEvent(&t, 4, "b", kFn2);
    // removing one of two leaves the table allocated
    CHECK_EQ(RegisterEvent(&t, 2, nullptr, nullptr), 1);
    CHECK_EQ(t.slots[2].handler, (void*)nullptr);
    CHECK(t.allocated);
    CHECK_EQ(t.CountPopulated(), 1);
}

TEST(WorldEventBindings, RemoveLastFreesTable) {
    EventBindingTable t;
    RegisterEvent(&t, 4, "only", kFn1);
    CHECK(t.allocated);
    CHECK_EQ(RegisterEvent(&t, 4, nullptr, nullptr), 1);
    CHECK(t.Empty());
    CHECK(!t.allocated);          // all 7 slots empty -> table freed
}

TEST(WorldEventBindings, RemoveEmptyIsNoOp) {
    EventBindingTable t;
    // removing from a never-allocated table just returns 1
    CHECK_EQ(RegisterEvent(&t, 3, nullptr, nullptr), 1);
    // removing an empty slot of an allocated table also returns 1, no free since
    // the populated slot survives
    RegisterEvent(&t, 1, "p", kFn1);
    CHECK_EQ(RegisterEvent(&t, 3, nullptr, nullptr), 1);
    CHECK(t.allocated);
}

// --- Bio stream primitives (golden bytes) -----------------------------------
TEST(WorldEventBindings, ByteWriterDwordLE) {
    ByteWriter w;
    w.WriteDword(0x04030201u);
    CHECK_EQ(w.bytes.size(), (size_t)4);
    CHECK_EQ(w.bytes[0], (u8)0x01);
    CHECK_EQ(w.bytes[1], (u8)0x02);
    CHECK_EQ(w.bytes[2], (u8)0x03);
    CHECK_EQ(w.bytes[3], (u8)0x04);
}

TEST(WorldEventBindings, ByteWriterStringNulTerminated) {
    ByteWriter w;
    w.WriteString("hi");
    CHECK_EQ(w.bytes.size(), (size_t)3);
    CHECK_EQ(w.bytes[0], (u8)'h');
    CHECK_EQ(w.bytes[1], (u8)'i');
    CHECK_EQ(w.bytes[2], (u8)0);
}

TEST(WorldEventBindings, ByteReaderRoundTrip) {
    ByteWriter w;
    w.WriteDword(2);
    w.WriteString("SCENE_ENTER");
    w.WriteString("handlerA");
    ByteReader r{w.bytes.data(), w.bytes.size(), 0};
    CHECK_EQ(r.ReadDword(), (u32)2);
    CHECK(r.ReadString() == "SCENE_ENTER");
    CHECK(r.ReadString() == "handlerA");
}

// --- WriteEventNames golden -------------------------------------------------
TEST(WorldEventBindings, WriteEventNamesNullTable) {
    ByteWriter w;
    WriteEventNames(w, nullptr);
    // single zero dword
    CHECK_EQ(w.bytes.size(), (size_t)4);
    CHECK_EQ(w.bytes[0], (u8)0);
    CHECK_EQ(w.bytes[1], (u8)0);
    CHECK_EQ(w.bytes[2], (u8)0);
    CHECK_EQ(w.bytes[3], (u8)0);
}

TEST(WorldEventBindings, WriteEventNamesContent) {
    EventBindingTable t;
    RegisterEvent(&t, 4, "onEnter", kFn1);   // SCENE_ENTER
    RegisterEvent(&t, 5, "onExit", kFn2);    // SCENE_EXIT
    ByteWriter w;
    WriteEventNames(w, &t);
    // count(2) + "SCENE_ENTER\0" + "onEnter\0" + "SCENE_EXIT\0" + "onExit\0"
    ByteReader r{w.bytes.data(), w.bytes.size(), 0};
    CHECK_EQ(r.ReadDword(), (u32)2);
    CHECK(r.ReadString() == "SCENE_ENTER");   // slot index 4 -> name
    CHECK(r.ReadString() == "onEnter");
    CHECK(r.ReadString() == "SCENE_EXIT");    // slot index 5 -> name
    CHECK(r.ReadString() == "onExit");
}
