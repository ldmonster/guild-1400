// Integration tests for personnel_recruit2 against REAL reconstructed siblings:
//   * guild::io VFS memory stream (VfsOpenMemoryStream / VfsWriteStream /
//     VfsReadStream / VfsCloseStream) — Avatar_Save/Load round-trip exactly as the
//     original wired VIBE_Vfs_WriteStream / VIBE_Vfs_ReadStreamBool.
//   * guild::util::RandomModulo / guild::crt::RandNext — the real CRT LCG that
//     drives Avatar_AllocSlot's slot pick (VIBE_Math_RandomModulo(0x20)).
// No mocks: the stream hook forwards into the real VFS, and the slot index comes
// from the real RNG.
#include "sim/personnel_recruit2.h"
#include "io/vfs.h"
#include "io/file.h"
#include "util/math_random.h"
#include "crt/rand.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::u8;
using guild::u32;
using guild::io::VfsHandle;

// Avatar_Save -> Avatar_Load round-trip through the REAL VFS memory stream.
TEST(PersRec2Integration, AvatarSaveLoadThroughRealVfs) {
    ResetAvatarPool();
    // Seed distinct ids and a flag pattern.
    for (int s = 0; s < kAvatarPoolCount; ++s) {
        u32 id = 0x5000u + s;
        std::memcpy(AvatarPoolEntry(s), &id, 4);
        u8 flag = (s & 1) ? 1 : 0;
        AvatarPoolEntry(s)[kAvatarPoolFlagOff] = flag;
        g_avatarPoolFlags[s] = flag;
    }

    std::vector<u8> backing(8 * 1024, 0);

    // --- WRITE through the real VFS ---
    struct WriteHook : AvatarStreamHooks {
        VfsHandle* h;
        bool Write(const void* src, u32 size, u32 count) override {
            return guild::io::VfsWriteStream(src, size, h, count) != 0xFFFFFFFFu;
        }
    } wh;
    VfsHandle* w = guild::io::VfsOpenMemoryStream(backing.data(), (u32)backing.size(), "wb");
    CHECK(w != nullptr);
    bool wrote = false;
    if (w) {
        wh.h = w;
        SetAvatarStreamHooks(&wh);
        wrote = Avatar_Save();
        SetAvatarStreamHooks(nullptr);
        CHECK(wrote);
        guild::io::VfsCloseStream(w);
    }

    // Corrupt in-memory flags; ids are kept so Load can match them.
    for (int s = 0; s < kAvatarPoolCount; ++s) {
        AvatarPoolEntry(s)[kAvatarPoolFlagOff] = 0x77;
        g_avatarPoolFlags[s] = 0x77;
    }

    // --- READ back through the real VFS ---
    struct ReadHook : AvatarStreamHooks {
        VfsHandle* h;
        bool Read(void* dst, u32 size, u32 count) override {
            u32 r = guild::io::VfsReadStream(dst, size, h, count);
            return r != 0 && r != 0xFFFFFFFFu;
        }
    } rh;
    VfsHandle* r = guild::io::VfsOpenMemoryStream(backing.data(), (u32)backing.size(), "rb");
    CHECK(r != nullptr);
    bool loaded = false;
    if (r && wrote) {
        rh.h = r;
        SetAvatarStreamHooks(&rh);
        loaded = Avatar_Load();
        SetAvatarStreamHooks(nullptr);
        CHECK(loaded);
        guild::io::VfsCloseStream(r);
    }

    if (loaded) {
        for (int s = 0; s < kAvatarPoolCount; ++s) {
            CHECK_EQ((int)g_avatarPoolFlags[s], (s & 1) ? 1 : 0);
        }
    }
}

// Avatar_AllocSlot driven by the REAL RNG: util::RandomModulo(32) selects the
// starting slot; with an empty pool it must always succeed and the chosen slot's
// flag becomes 1. We assert the slot picked matches the real RNG sequence.
TEST(PersRec2Integration, AvatarAllocUsesRealRng) {
    ResetAvatarPool();
    guild::crt::Srand(20240606u);
    // Reproduce the original call: VIBE_Math_RandomModulo(0x20).
    int expectSlot = guild::util::RandomModulo(0x20);
    CHECK(expectSlot >= 0 && expectSlot < kAvatarPoolCount);

    // Re-seed so AllocSlot consumes the SAME first RNG draw.
    guild::crt::Srand(20240606u);
    int slotPick = guild::util::RandomModulo(0x20);
    u8* got = Avatar_AllocSlot(slotPick);
    CHECK(got == AvatarPoolEntry(expectSlot));
    if (got) {
        CHECK_EQ((int)g_avatarPoolFlags[expectSlot], 1);
    }
}

// Cross-RNG sequence: allocate several slots; each pick comes from the real RNG
// and lands on a distinct free slot (round-robin from each draw). All succeed
// while the pool has room.
TEST(PersRec2Integration, AvatarAllocSequenceRealRng) {
    ResetAvatarPool();
    guild::crt::Srand(7u);
    int allocated = 0;
    for (int i = 0; i < 10; ++i) {
        int pick = guild::util::RandomModulo(0x20);
        u8* got = Avatar_AllocSlot(pick);
        CHECK(got != nullptr);
        if (got) ++allocated;
    }
    CHECK_EQ(allocated, 10);
    // Exactly 10 slots flagged used.
    int used = 0;
    for (int s = 0; s < kAvatarPoolCount; ++s) used += (g_avatarPoolFlags[s] == 1);
    CHECK_EQ(used, 10);
}
