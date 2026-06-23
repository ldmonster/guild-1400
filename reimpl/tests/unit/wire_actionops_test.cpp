// Verifies InstallRealActionOpsWiring() binds the one bindable field across the
// eight action-ops bridges this agent owns — MiscActionHooks::findNearby — to its
// real reconstructed leaf (VIBE_Character_FindNearbyInRadius @0x40507c). The other
// seven bridges (Kill/Reset/Run/Wait/Flow/Ops4/Op85) are zero-bindable and get no
// installer; they are not exercised here. Suite prefix: WireActionOps.
//
// Headless: no GPU/window/audio/native record path is touched — the wiring only
// rebinds an in-process sim hook table and the scan walks the (empty) global
// character array.
#include "tests/framework/test.h"

#include "sim/wire_actionops.h"
#include "sim/charaction_misc.h"     // MiscActionHooks / Set/GetMiscActionHooks
#include "sim/character.h"           // Character / SocialAvatar / g_characters
#include "sim/character_social.h"    // SocialAvatar fields

using namespace guild;
using namespace guild::sim;

// Re-inert the bridge so a clean baseline can be asserted before install.
static void InertMisc() { SetMiscActionHooks(nullptr); }

TEST(WireActionOps, BindsFindNearbyAndSeedsRestInert) {
    InertMisc();
    // Capture the inert-default table (non-null stubs) so we can prove the installer
    // SEEDS from it and overrides ONLY findNearby — the other fields must keep their
    // inert stubs (several CharAction step call sites invoke hooks without a null
    // check, so the seed must stay non-null), and findNearby must change.
    const MiscActionHooks before = GetMiscActionHooks();
    CHECK(before.findNearby != nullptr);          // module's inert DefFindNearby stub

    InstallRealActionOpsWiring();

    const MiscActionHooks& after = GetMiscActionHooks();
    // findNearby is now the real adapter (changed away from the inert stub).
    CHECK(after.findNearby != nullptr);
    CHECK(after.findNearby != before.findNearby);

    // Every other field stays at its inert module default (non-null stub, unchanged).
    CHECK(after.attachMovementAni == before.attachMovementAni);
    CHECK(after.attachAni         == before.attachAni);
    CHECK(after.stepMotionQueue   == before.stepMotionQueue);
    CHECK(after.checkQueueReady   == before.checkQueueReady);
    CHECK(after.stopSample        == before.stopSample);
    CHECK(after.sceneSlotIndex    == before.sceneSlotIndex);
    CHECK(after.worldToTile       == before.worldToTile);
    // all non-null (the call sites that don't null-check stay safe)
    CHECK(after.attachMovementAni != nullptr);
    CHECK(after.attachAni         != nullptr);
    CHECK(after.stepMotionQueue   != nullptr);
    CHECK(after.checkQueueReady   != nullptr);
    CHECK(after.stopSample        != nullptr);
    CHECK(after.sceneSlotIndex    != nullptr);
    CHECK(after.worldToTile       != nullptr);

    InertMisc();
}

// The wired findNearby adapter runs through the real proximity scan against the
// (empty) global character array without crashing, returning a defined result —
// i.e. the bound leaf actually executes. With no other live actor sharing the
// self's group/world, the scan finds nothing and the single-result adapter yields
// null (the original's "v19 empty -> falsy" branch).
TEST(WireActionOps, WiredFindNearbyExecutesOverEmptyArray) {
    // Ensure the global array is empty so the scan terminates with zero hits.
    for (int i = 0; i < kCharacterCapacity; ++i) g_characters[i] = nullptr;

    InstallRealActionOpsWiring();

    SocialAvatar avatar{};
    avatar.worldId = 7;
    avatar.groupId = 3;
    avatar.pos[0] = avatar.pos[1] = avatar.pos[2] = 0.0f;

    Character self{};
    self.social = &avatar;

    const MiscActionHooks& h = GetMiscActionHooks();
    Character* hit = h.findNearby(&self, 20.0f);
    CHECK(hit == nullptr);   // no candidates -> defined null result

    InertMisc();
}
