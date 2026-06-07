// e2e: exercise the world_history2 event-icon lifecycle end to end —
// create -> rearrange -> stale-sweep -> destroy-for-entity -> punishment gate,
// all through the installable hooks, driving the recovered 64-slot icon pool.
#include "test.h"

#include "world/world_history2.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// A tiny "live world": each entity handle maps to a current entity id. Lets the
// e2e simulate ids drifting (entity replaced) between create and sweep.
int g_live[16];
int LiveId(int parent) { return (parent >= 0 && parent < 16) ? g_live[parent] : parent; }

int g_meshBuilds;
int BuildMesh(HeIconSlot* s, int, int) { ++g_meshBuilds; s->node = 1000 + s->parent; return 0; }

int g_detaches;
void Detach(int) { ++g_detaches; }

} // namespace

TEST(WorldHistory2E2E, IconLifecycleFlow) {
    HeIconPoolReset();
    std::memset(g_live, 0, sizeof(g_live));
    g_meshBuilds = 0; g_detaches = 0;

    WorldHistory2Hooks h{};
    h.parentEntityId = LiveId;
    h.createIconMesh = BuildMesh;
    h.objectDetachAndRelease = Detach;
    // objectFindByHandle / iconsEnabled defaults: live + enabled.
    SetWorldHistory2Hooks(&h);

    // --- Create three event icons for entities 3, 4, 5.
    g_live[3] = 30; g_live[4] = 40; g_live[5] = 50;
    CHECK_EQ(He_CreateGfxInfo(3, 0, "Haus3"), 1);
    CHECK_EQ(He_CreateGfxInfo(4, 0, "Haus4"), 1);
    CHECK_EQ(He_CreateGfxInfo(5, 0, "Haus5"), 1);
    CHECK_EQ(g_meshBuilds, 3);

    HeIconSlot* pool = HeIconPool();
    CHECK_EQ(pool[0].parent, 3); CHECK_EQ(pool[0].entityId, 30); CHECK_EQ(pool[0].node, 1003);
    CHECK_EQ(pool[1].parent, 4); CHECK_EQ(pool[1].entityId, 40); CHECK_EQ(pool[1].node, 1004);
    CHECK_EQ(pool[2].parent, 5); CHECK_EQ(pool[2].entityId, 50); CHECK_EQ(pool[2].node, 1005);

    // --- Entity 4 gets replaced (its id changes); the others stay. A stale sweep
    // must destroy only entity 4's icon (and release its node).
    g_live[4] = 999;
    He_DestroyStaleIcons();
    CHECK_EQ(pool[0].entityId, 30);    // entity 3: fresh
    CHECK_EQ(pool[1].entityId, -1);    // entity 4: destroyed (drifted)
    CHECK_EQ(pool[1].parent, 0);
    CHECK_EQ(pool[2].entityId, 50);    // entity 5: fresh
    CHECK_EQ(g_detaches, 1);           // entity 4's node released

    // --- A new icon for entity 6 must reuse the freed slot 1 (first free parent).
    g_live[6] = 60;
    CHECK_EQ(He_CreateGfxInfo(6, 0, "Haus6"), 1);
    CHECK_EQ(pool[1].parent, 6);
    CHECK_EQ(pool[1].entityId, 60);

    // --- Destroy every icon owned by entity 5 (matched via the +8/mesh field,
    // which CreateGfxInfo left at 0; set it to mark ownership for this step).
    pool[2].mesh = 5;
    He_DestroyIconsForEntity(5);
    CHECK_EQ(pool[2].entityId, -1);    // destroyed
    CHECK_EQ(pool[0].entityId, 30);    // others untouched
    CHECK_EQ(pool[1].entityId, 60);

    // --- A punishment gate over a record built during the flow.
    u8 rec[600]; std::memset(rec, 0, sizeof(rec));
    CHECK_EQ(He_ValidatePunishmentType(rec, 5), -4);   // not eligible (+358 == 0)
    rec[358] = 1;
    CHECK_EQ(He_ValidatePunishmentType(rec, 5), 0);    // now eligible
    CHECK_EQ(He_ValidatePunishmentType(rec, 12), -3);  // out of range

    SetWorldHistory2Hooks(nullptr);
}
