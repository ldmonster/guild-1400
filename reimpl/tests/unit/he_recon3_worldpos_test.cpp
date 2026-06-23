// Golden-vector unit tests for VIBE_He_UpdateHandlerWorldPos @0x4c6cdc.
// Vectors derived from the gilde.exe Hex-Rays decompile + the cmp/jcc disasm of
// the kind-byte switch at 0x4c6d39..0x4c6e08 (reference of record).
#include "tests/framework/test.h"
#include "sim/he_recon3_worldpos.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A fresh, zeroed He handler record with a known sentinel state so we can detect
// whether the routine reset it.
struct Rec {
    HeRecord h;
    Rec() {
        std::memset(&h, 0, sizeof(h));
        He_State(&h) = 0x12345678;   // sentinel != -2
    }
};

GameTime MakeClock() {
    GameTime t{};
    t.day = 7; t.hour = 13; t.minute = 41; t.second = 22;
    return t;
}

// A table view that returns one fixed row for any index.
HeWorldPosRow g_row;
bool g_haveRow = true;
bool RowLookup(u16 /*idx*/, HeWorldPosRow* out) { *out = g_row; return g_haveRow; }

HeWorldPosTable MakeTable(const GameTime* clk) {
    HeWorldPosTable t;
    t.lookup = &RowLookup;
    t.clock = clk;
    return t;
}

bool WasReset(const Rec& r) { return He_State(const_cast<HeRecord*>(&r.h)) == -2; }
bool ClockMatches(const Rec& r, const GameTime& c) {
    return std::memcmp(&He_ApptTime(const_cast<HeRecord*>(&r.h)), &c, sizeof(GameTime)) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Guard: record+8 == 0xFFFF -> early return, record untouched.
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, NoOwningRowLeavesUntouched) {
    GameTime clk = MakeClock();
    Rec r;
    He_CityIndex(&r.h) = 0xFFFF;             // record+8
    HeWorldPosTable t = MakeTable(&clk);
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(!WasReset(r));
}

// ---------------------------------------------------------------------------
// Stale row: id mismatch (record+12 != row.id) -> RESET.
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, IdMismatchResets) {
    GameTime clk = MakeClock();
    Rec r;
    He_CityIndex(&r.h) = 3;
    He_CityId(&r.h) = 100;                   // record+12
    HeBytes(&r.h)[0] = 0x05;                     // kind that does NOT reset on its own
    g_haveRow = true; g_row = HeWorldPosRow{0x01, /*id*/ 999, /*suppress*/ 0};
    HeWorldPosTable t = MakeTable(&clk);
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(WasReset(r));
    CHECK(ClockMatches(r, clk));
}

// ---------------------------------------------------------------------------
// Dead marker (row.marker == 0x0F) -> RESET even when id matches.
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, DeadMarkerResets) {
    GameTime clk = MakeClock();
    Rec r;
    He_CityIndex(&r.h) = 3;
    He_CityId(&r.h) = 555;
    HeBytes(&r.h)[0] = 0x05;                     // non-reset kind
    g_haveRow = true; g_row = HeWorldPosRow{0x0F, /*id*/ 555, /*suppress*/ 0};
    HeWorldPosTable t = MakeTable(&clk);
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(WasReset(r));
}

// ---------------------------------------------------------------------------
// Valid row + suppress flag set -> early return, record untouched, regardless of kind.
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, SuppressFlagSkips) {
    GameTime clk = MakeClock();
    Rec r;
    He_CityIndex(&r.h) = 3;
    He_CityId(&r.h) = 555;
    HeBytes(&r.h)[0] = 0x16;                     // a kind that WOULD reset if reached
    g_haveRow = true; g_row = HeWorldPosRow{0x01, /*id*/ 555, /*suppress*/ 1};
    HeWorldPosTable t = MakeTable(&clk);
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(!WasReset(r));                      // suppress short-circuits before the switch
}

// ---------------------------------------------------------------------------
// Valid + not suppressed: switch on the kind byte. Reset-kinds reset; others not.
// ---------------------------------------------------------------------------
namespace {
void RunKind(u8 kind, const GameTime& clk, Rec& r) {
    He_CityIndex(&r.h) = 3;
    He_CityId(&r.h) = 555;
    HeBytes(&r.h)[0] = kind;
    g_haveRow = true; g_row = HeWorldPosRow{0x01, /*id*/ 555, /*suppress*/ 0};
    HeWorldPosTable t = MakeTable(&clk);
    He_UpdateHandlerWorldPos(&r.h, t);
}
} // namespace

TEST(HeRecon3WorldPos, ResetKindSetExact) {
    // The exhaustive reset set traced from 0x4c6d39..0x4c6e08.
    const u8 resets[] = {
        0x16, 0x18, 0x19, 0x1A, 0x2D, 0x2E, 0x2F, 0x35, 0x36, 0x37, 0x38,
        0x41, 0x45, 0x46, 0x47, 0x4A, 0x4B, 0x5E, 0x5F, 0x60, 0x69, 0x6A,
        0x6F, 0x7E,
    };
    GameTime clk = MakeClock();
    for (u8 k : resets) {
        Rec r;
        RunKind(k, clk, r);
        CHECK(WasReset(r));
        CHECK(ClockMatches(r, clk));
    }
    // Every byte not in the reset set must NOT reset (full 0..255 sweep, valid row).
    auto inReset = [&](u8 k) {
        for (u8 rk : resets) if (rk == k) return true;
        return false;
    };
    for (int kv = 0; kv < 256; ++kv) {
        u8 k = static_cast<u8>(kv);
        Rec r;
        RunKind(k, clk, r);
        CHECK_EQ(WasReset(r), inReset(k));
        // He_WorldPosKindResets must agree with the observed behaviour.
        CHECK_EQ(He_WorldPosKindResets(k), inReset(k));
    }
}

// ---------------------------------------------------------------------------
// Boundary kinds adjacent to reset values must NOT reset (off-by-one guard).
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, BoundaryKindsDoNotReset) {
    const u8 nonResets[] = {
        0x15, 0x17, 0x1B, 0x2C, 0x30, 0x34, 0x39, 0x40, 0x42, 0x43, 0x44,
        0x48, 0x49, 0x4C, 0x5D, 0x61, 0x68, 0x6B, 0x6E, 0x70, 0x7D, 0x7F, 0x00,
    };
    GameTime clk = MakeClock();
    for (u8 k : nonResets) {
        Rec r;
        RunKind(k, clk, r);
        CHECK(!WasReset(r));
        CHECK(!He_WorldPosKindResets(k));
    }
}

// ---------------------------------------------------------------------------
// Reset uses NpcClock() when no clock is injected into the table view.
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, FallsBackToNpcClock) {
    GameTime gc{}; gc.day = 99; gc.hour = 1; gc.minute = 2; gc.second = 3;
    SetNpcClock(gc);
    Rec r;
    He_CityIndex(&r.h) = 3;
    He_CityId(&r.h) = 1;
    HeBytes(&r.h)[0] = 0x41;                      // reset kind
    g_haveRow = true; g_row = HeWorldPosRow{0x01, /*id*/ 999, /*suppress*/ 0}; // id mismatch
    HeWorldPosTable t;                        // no lookup? use lookup but no clock
    t.lookup = &RowLookup; t.clock = nullptr;
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(WasReset(r));
    CHECK(ClockMatches(r, gc));
}

// ---------------------------------------------------------------------------
// Inert table (no lookup) reports row absent -> stale -> RESET (headless exercise).
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, InertTableTreatsRowAsStale) {
    GameTime clk = MakeClock();
    Rec r;
    He_CityIndex(&r.h) = 5;
    He_CityId(&r.h) = 12345;
    HeBytes(&r.h)[0] = 0x05;                      // non-reset kind, but stale row forces reset
    HeWorldPosTable t;                        // lookup == nullptr (inert default)
    t.clock = &clk;
    He_UpdateHandlerWorldPos(&r.h, t);
    CHECK(WasReset(r));
    CHECK(ClockMatches(r, clk));
}

// ---------------------------------------------------------------------------
// Reset stamps state = -2 exactly (0xFFFFFFFE), not just "nonzero".
// ---------------------------------------------------------------------------
TEST(HeRecon3WorldPos, ResetStateValueIsMinusTwo) {
    GameTime clk = MakeClock();
    Rec r;
    RunKind(0x6F, clk, r);
    CHECK_EQ(He_State(&r.h), static_cast<i32>(-2));
}
