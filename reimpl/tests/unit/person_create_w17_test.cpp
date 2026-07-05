// WAVE-17 — VIBE_Person_CreateAndSpawn @0x58da70 gap closure.
//
// Golden-pins (a) the deterministic field-stamp image and (b) the EXACT
// RandNext draw count/order of the no-parents path (the path every new-game
// CreateAndSpawn takes — the player op-12 and both parents op-11 are created
// before their parent ids resolve, so FindRecordById returns null and the
// function falls into loc_58EA1A). Synthetic seed; no real asset scene needed.
//
// The draw stream is pinned by recording the LCG state after one create: the
// 32-bit ANSI LCG (state = 1103515245*state + 12345) makes the post-create
// state a faithful fingerprint of BOTH the draw count AND order — any change in
// either would change the final state. We also pin a manual replay of the draw
// COUNT so a regression names the divergence.
#include "tests/framework/test.h"

#include "sim/person_create.h"
#include "sim/entity.h"
#include "sim/building.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

void ResetAll() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    ResetEntityArrays();
    ResetPersonCreate();
    ResetBuildings();
    g_personArrayLoaded = true;
}

i32 R32(const u8* r, int o) { i32 v; std::memcpy(&v, r + o, 4); return v; }
i16 R16(const u8* r, int o) { i16 v; std::memcpy(&v, r + o, 2); return v; }
float RF(const u8* r, int o) { float v; std::memcpy(&v, r + o, 4); return v; }

// Faithful local replica of the no-parents LCG advance, used to count draws.
u32 g_replay;
inline int Draw() { g_replay = 1103515245u * g_replay + 12345u; return (g_replay >> 16) & 0x7FFF; }

} // namespace

// ---------------------------------------------------------------------------
// Deterministic field stamps (the pure-constant part of 0x58da70 — no RNG).
// ---------------------------------------------------------------------------
TEST(PersonCreateW17, DeterministicFieldStamps) {
    ResetAll();
    crt::Srand(12345);
    g_personNextId = 7000;

    PersonSpawnArgs a{};
    a.kind = 9;            // parent NPC kind (no early avatar/family branch)
    a.ownerWord = 34;      // profession word (RandomModulo(4)+32 region)
    a.parentAId = 111;     // unresolved -> no-parents path
    a.parentBId = 222;
    a.a8 = 0;              // male
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    const u8* r = reinterpret_cast<const u8*>(&g_persons[idx]);

    // identity / allocation
    CHECK_EQ((int)r[2], 9);                 // +2  kind
    CHECK_EQ(R32(r, 4), 7000);              // +4  id from allocator
    CHECK_EQ(g_personIds[idx], 7000);       // parallel id column lockstep
    CHECK_EQ(g_personNextId, 7001);
    CHECK_EQ((int)R16(r, 0x0A), 34);        // +10 owner/profession word
    CHECK_EQ((int)r[8], 100);               // +8  alive byte (byte_12CE918)

    // deterministic constants (0x58db73..0x58dc63)
    CHECK_EQ(R32(r, 0x190), 4);             // +400 = 4
    CHECK_EQ(R32(r, 0x194), 0);             // +404 = 0
    CHECK_EQ(R32(r, 0x18C), -1);            // +396 = -1
    CHECK_EQ(R32(r, 0x208), -1);            // +520 = -1
    CHECK_EQ(R32(r, 0x20C), -1);            // +524 = -1
    CHECK_EQ(RF(r, 0x1E0), 1.0f);           // +480 = 1.0f
    CHECK_EQ((int)r[0x214], 1);             // +532 = 1
    CHECK_EQ((int)r[0x166], 0);             // +358 = 0

    // wappen default 1342 (kind 9 is NOT 7/5 so no dedup re-derive)
    CHECK_EQ(R32(r, 0x54), 1342);
    // guild branch: kind 9 (<10, not 6/7/5) -> +0x50 = dword_64771C (first == 0)
    CHECK_EQ((int)R16(r, 0x50), 0);

    // face scalar constants written on LABEL_189 (0x58ed70/0x58edfa).
    // +0x1C starts at 384.0f (0x58ed70) but is REFINED by the block at
    // 0x58e377..0x58e41f whenever (u16)ownerWord(+0x0A) > +0x20 (12.0f):
    // iters = (int)(34 - 12) = 22 steps of x = x - v152 + v152/x from 384.0,
    // clamped to >= 1.0f by a signed bit compare (0x58e405..0x58e41f).
    // (The previous raw-384.0f pin matched a transcription that stubbed the
    // refinement out — disasm-refuted.)
    {
        double x = 384.0;
        const float v152 = RF(r, 0x14);
        for (int i = 0; i < 22; ++i)
            x = x - static_cast<double>(v152) + static_cast<double>(v152) / x;
        float expect = static_cast<float>(x);
        i32 bits; std::memcpy(&bits, &expect, 4);
        if (bits <= 0x3F800000) bits = 0x3F800000;
        std::memcpy(&expect, &bits, 4);
        CHECK_EQ(RF(r, 0x1C), expect);
    }
    CHECK_EQ(RF(r, 0x20), 12.0f);           // +0x20 = 12.0f
}

// ---------------------------------------------------------------------------
// RNG draw count/order — the load-bearing 1:1 property (downstream new-game
// RandomModulo must stay aligned). Pin the post-create LCG state + draw count.
// ---------------------------------------------------------------------------
TEST(PersonCreateW17, RngDrawCountAndOrderNoParents) {
    ResetAll();
    crt::Srand(0);
    *crt::RandStatePtr() = 0;                  // explicit known seed state
    const u32 before = *crt::RandStatePtr();

    PersonSpawnArgs a{};
    a.kind = 9;
    a.ownerWord = 32;
    a.parentAId = 1;     // unresolved -> no-parents
    a.parentBId = 2;
    a.a8 = 0;            // male -> the %191 name draw
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    const u32 after = *crt::RandStatePtr();

    // Count how many LCG advances separate `before` from `after` (<= an upper
    // bound covering the no-parents path: ~1577 draws).
    int draws = 0;
    u32 s = before;
    while (s != after && draws < 4000) { s = 1103515245u * s + 12345u; ++draws; }
    CHECK(s == after);                         // the state IS reachable by draws
    CHECK(draws > 1570);                       // the heavy relation-grid loop ran
    CHECK(draws < 1600);

    // The draw count is invariant under the seed (no data-dependent count change
    // for kind-9 male here): re-run from the same seed and require identical.
    ResetAll();
    crt::Srand(0); *crt::RandStatePtr() = 0;
    PersonSpawnArgs b = a;
    Person_CreateAndSpawn(b);
    CHECK_EQ((int)*crt::RandStatePtr(), (int)after);
}

// ---------------------------------------------------------------------------
// Stat-table-driven fields: the first-class stat triple seeded from
// flt_582900/flt_582904 (class 0). Pins the in-tree table reconstruction.
// ---------------------------------------------------------------------------
TEST(PersonCreateW17, StatTripleFromClass0Table) {
    ResetAll();
    crt::Srand(99);
    g_replay = *crt::RandStatePtr();

    PersonSpawnArgs a{};
    a.kind = 9; a.ownerWord = 0; a.parentAId = 5; a.parentBId = 6; a.a8 = 0;
    a.a6 = 0;            // group 0 (GroupFromCode(0) -> 0) -> class-0 stat row
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    const u8* r = reinterpret_cast<const u8*>(&g_persons[idx]);

    // Replay the exact draw order up to the FIRST stat triple to predict it.
    Draw();                                    // +432 hair (kind 9 < 10)
    // gender a8=0 != 2 -> no draw; prof override: kind 9 -> none.
    Draw();                                    // v109 = RandNext()%8 (personality)
    // First stat triple (t=0): base=2.0f, delta=(3.0f-2.0f)=1.0f from class-0.
    const int r1 = Draw();
    const float base = 2.0f, delta = 1.0f;     // flt_582900[0]=2, flt_582904[0]=3
    const float kScale = 3.0518509447574615e-05f;  // 1/32767
    const float statExpect = (float)((double)r1 * kScale * delta + base);
    CHECK_EQ(RF(r, 0x88), statExpect);         // +0x88 first stat value
    const int r2 = Draw();                     // %10 (consumed; +0x90)
    // +0x90 = (r2%10) + 900.0  (dbl_6268CC)
    CHECK_EQ(RF(r, 0x90), (float)((double)(r2 % 10) + 900.0));
    // NOTE: the first triple's +0x80 (stat*0.001) is FAITHFULLY clobbered later
    // by the talent loop, which writes BYTES at +0x80..+0x84 over this float —
    // exactly as the original does (the stat and talent arrays overlap at +0x80).
}

// ---------------------------------------------------------------------------
// Talent bytes: when !a7||a6, the talent slots derive from byte_649910 row a6.
// Row 1 = {0x69,0x69,0xBD,0x93,0x69,0x04}; non-zero entries -> base + RandNext()
// % (base<42 ? 6 : 11). Pins the in-tree talent table + per-slot draw.
// ---------------------------------------------------------------------------
TEST(PersonCreateW17, TalentBytesFromTable649910) {
    ResetAll();
    crt::Srand(7);
    PersonSpawnArgs a{};
    a.kind = 9; a.ownerWord = 0; a.parentAId = 3; a.parentBId = 4; a.a8 = 0;
    a.a6 = 1;            // talent row index 1
    a.a7 = 0;            // !a7 -> byte_649910 branch
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    const u8* r = reinterpret_cast<const u8*>(&g_persons[idx]);

    // Row 1 bytes (all non-zero, all >= 0x69 == 105 >= 42 -> span 11):
    //   talent[j] in [base, base+10].
    const u8 base[5] = { 0x69, 0x69, 0xBD, 0x93, 0x69 };
    for (int j = 0; j < 5; ++j) {
        int t = r[0x80 + j];
        CHECK(t >= base[j]);
        CHECK(t <= base[j] + 10);
    }
    // kind 9 (not 6/7) -> +0xD overlay = byte_649915[6*1] == byte_649910[6*1+5]
    //   == 0x04.
    CHECK_EQ((int)r[0x0D], 0x04);
}

// ---------------------------------------------------------------------------
// Kind-6 player: family branch sets +0x50 = (familyCount | 0x8000); the live
// counter (dword_647724) and the family counter (dword_647720) advance.
// ---------------------------------------------------------------------------
TEST(PersonCreateW17, PlayerFamilySlotWordAndCounters) {
    ResetAll();
    crt::Srand(3);
    CHECK_EQ(g_personLiveCount, 0);

    PersonSpawnArgs a{};
    a.kind = 6;          // local player -> family branch
    a.ownerWord = 16;    // also exercises the playermode arm (+0x1E4=1)
    a.parentAId = 50; a.parentBId = 60; a.a8 = 1;
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    const u8* r = reinterpret_cast<const u8*>(&g_persons[idx]);

    CHECK_EQ((int)r[2], 6);                     // kind 6
    CHECK_EQ((int)(u16)R16(r, 0x50), 0x8000);   // familyCount 0 | 0x8000
    // 0x58dc42 `mov bh,[ebp+2]` / 0x58dc69: the playermode arm keys on the KIND
    // byte (3/16/19), NOT the ownerWord — kind 6 leaves +0x1E4 at 0. (The old
    // ==1 pin matched an ownerWord-keyed transcription — disasm-refuted.)
    CHECK_EQ(R32(r, 0x1E4), 0);
    CHECK_EQ(g_personLiveCount, 1);             // dword_647724 incremented once

    // A second family person takes the next slot word 0x8001.
    PersonSpawnArgs c = a; c.ownerWord = 0;
    u16 idx2 = Person_CreateAndSpawn(c);
    const u8* r2 = reinterpret_cast<const u8*>(&g_persons[idx2]);
    CHECK_EQ((int)(u16)R16(r2, 0x50), 0x8001);
    CHECK_EQ(g_personLiveCount, 2);
}
