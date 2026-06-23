#include "test.h"

// Unit tier for the per-NPC animation CLIP SELECTION runtime
// (src/sim/npc_clip_select.*), the 1:1 distillation of the clip-decision core of
// VIBE_Character_Update @0x405148:
//   * the action-head gate (+296): an active walk action (type 45/58) selects the
//     gait "bewegung/gehen" (0x610170), its cart variant "bewegung/karren_ziehen"
//     (0x61024c) when pulling, a turn action (type 7) selects
//     "bewegung/dreh_90_rechts" (0x61070c), and a runtime-attach action (empty
//     catalog name byte_610134) selects nothing (rule 8);
//   * the no-action idle branch: the SIT flag (+140 & 0x10) selects
//     "sitzend/sitz_newnoise" (0x6103bc) else the stand "stehen/stehen_newnoise"
//     (0x610158), with the playback-speed jitter scale dbl_6103FC == 0.01;
//   * the session bridge: gait-vs-idle keyed on the live movement state (+0x74).
// All asset-free; golden clip strings are the get_bytes values.

#include "sim/npc_clip_select.h"

#include <cstring>

using namespace guild;
using guild::sim::ClipKind;
using guild::sim::ClipSelection;
using guild::sim::NpcClipState;

namespace {

bool streq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

// ---------------------------------------------------------------------------
// Golden clip-name constants — the exact get_bytes values from the binary.
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, GoldenClipStrings) {
    CHECK(streq(sim::kClipGait,      "bewegung/gehen"));          // 0x610170
    CHECK(streq(sim::kClipGaitCart,  "bewegung/karren_ziehen"));  // 0x61024c
    CHECK(streq(sim::kClipTurn90R,   "bewegung/dreh_90_rechts")); // 0x61070c
    CHECK(streq(sim::kClipIdleStand, "stehen/stehen_newnoise"));  // 0x610158
    CHECK(streq(sim::kClipIdleSit,   "sitzend/sitz_newnoise"));   // 0x6103bc
    // dbl_6103FC @0x6103FC == 0.01 (the idle playback-speed jitter scale).
    CHECK(sim::kIdlePlaybackJitterScale == 0.01f);
    CHECK(sim::kIdlePlaybackBase == 1.0f);
}

// ---------------------------------------------------------------------------
// ActionClipName — the action-type -> catalog clip lookup (RegisterHandlers
// @0x40be30): only types 7 / 45 / 58 are clip-bearing.
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, ActionCatalogLookup) {
    // Walk (45) and walk-on-path (58) -> the gait; cart variant when pulling.
    CHECK(streq(sim::ActionClipName(45, false), "bewegung/gehen"));
    CHECK(streq(sim::ActionClipName(58, false), "bewegung/gehen"));
    CHECK(streq(sim::ActionClipName(45, true),  "bewegung/karren_ziehen"));
    CHECK(streq(sim::ActionClipName(58, true),  "bewegung/karren_ziehen"));
    // Turn (7) -> the turn clip (cart flag irrelevant).
    CHECK(streq(sim::ActionClipName(7, false), "bewegung/dreh_90_rechts"));
    CHECK(streq(sim::ActionClipName(7, true),  "bewegung/dreh_90_rechts"));
    // Every other type carries the empty catalog name (runtime-attached clip).
    CHECK(streq(sim::ActionClipName(0,  false), "")); // RunActionOrFree
    CHECK(streq(sim::ActionClipName(49, false), "")); // take
    CHECK(streq(sim::ActionClipName(50, false), "")); // drop
    CHECK(streq(sim::ActionClipName(54, false), "")); // LoadAnim (runtime name)
    CHECK(streq(sim::ActionClipName(55, false), "")); // set-visible
}

// ---------------------------------------------------------------------------
// Gate 1 — active action selects the action's clip.
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, ActiveWalkActionSelectsGait) {
    NpcClipState st;
    st.hasAction  = true;
    st.actionType = 45; // WalkUpdate
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Gait);
    CHECK(streq(s.clip, "bewegung/gehen"));
    CHECK(!s.idle);

    st.actionType = 58; // Command_Dispatcher (full walk-on-path)
    s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Gait);
    CHECK(streq(s.clip, "bewegung/gehen"));

    // Cart morph-init variant.
    st.cart = true;
    s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Gait);
    CHECK(streq(s.clip, "bewegung/karren_ziehen"));
}

TEST(NpcClipSelect, ActiveTurnActionSelectsTurnClip) {
    NpcClipState st;
    st.hasAction  = true;
    st.actionType = 7; // TurnStepActionUpdate
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Action);
    CHECK(streq(s.clip, "bewegung/dreh_90_rechts"));
    CHECK(!s.idle);
}

TEST(NpcClipSelect, ActiveRuntimeAttachActionSelectsNothing) {
    // take/drop/etc. hold +296 but attach their own clip in-handler: the selector
    // chooses no clip (rule 8 — no invented clip).
    for (int t : {0, 49, 50, 54, 55, 23}) {
        NpcClipState st;
        st.hasAction  = true;
        st.actionType = t;
        ClipSelection s = sim::SelectNpcClip(st);
        CHECK(s.kind == ClipKind::None);
        CHECK(streq(s.clip, ""));
    }
}

// ---------------------------------------------------------------------------
// Gate 2 — no action + moving -> gait (the session "walking person").
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, NoActionMovingSelectsGait) {
    NpcClipState st;
    st.hasAction = false;
    st.moving    = true;
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Gait);
    CHECK(streq(s.clip, "bewegung/gehen"));
    CHECK(!s.idle);
    // The session bridge never pulls a cart -> always plain gait, even if `cart`.
    st.cart = true;
    s = sim::SelectNpcClip(st);
    CHECK(streq(s.clip, "bewegung/gehen"));
}

// ---------------------------------------------------------------------------
// Gate 3 — no action + not moving -> idle (stand, or sit on the +140 0x10 flag),
// with the playback-speed jitter seed.
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, IdleStandWhenNotMoving) {
    NpcClipState st; // all-default: no action, not moving, not sitting
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Idle);
    CHECK(s.idle);
    CHECK(!s.sit);
    CHECK(streq(s.clip, "stehen/stehen_newnoise"));
    CHECK(s.idlePlaybackScale == 0.01f);
    CHECK(s.idlePlaybackBase == 1.0f);
}

TEST(NpcClipSelect, IdleSitWhenSitFlagSet) {
    NpcClipState st;
    st.sit = true; // +140 & 0x10
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Idle);
    CHECK(s.idle);
    CHECK(s.sit);
    CHECK(streq(s.clip, "sitzend/sitz_newnoise"));
}

// ---------------------------------------------------------------------------
// Gate precedence — an active action overrides the movement/sit state (the
// engine's +296 gate is checked first; a walking actor's walk action holds +296
// so it never reaches the idle attach).
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, ActionOverridesMovementAndSit) {
    NpcClipState st;
    st.hasAction  = true;
    st.actionType = 7;     // turn action
    st.moving     = true;  // ignored: action gate wins
    st.sit        = true;  // ignored
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::Action);
    CHECK(streq(s.clip, "bewegung/dreh_90_rechts"));

    // A take action (runtime-attached) with moving=true still selects None (the
    // handler owns the clip), NOT the gait — the action gate is exclusive.
    st.actionType = 49;
    s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::None);
    CHECK(streq(s.clip, ""));
}

// ---------------------------------------------------------------------------
// The session bridge helper — SelectPersonClipFromMovement(moving, sit).
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, SessionBridgeMovementToClip) {
    // Moving -> gait.
    ClipSelection m = sim::SelectPersonClipFromMovement(true);
    CHECK(m.kind == ClipKind::Gait);
    CHECK(streq(m.clip, "bewegung/gehen"));

    // Idle -> stand.
    ClipSelection i = sim::SelectPersonClipFromMovement(false);
    CHECK(i.kind == ClipKind::Idle);
    CHECK(streq(i.clip, "stehen/stehen_newnoise"));
    CHECK(!i.sit);

    // Idle + sit -> sit.
    ClipSelection s = sim::SelectPersonClipFromMovement(false, /*sit=*/true);
    CHECK(s.kind == ClipKind::Idle);
    CHECK(s.sit);
    CHECK(streq(s.clip, "sitzend/sitz_newnoise"));

    // Moving overrides sit (a walking person plays the gait, not the sit idle).
    ClipSelection ms = sim::SelectPersonClipFromMovement(true, /*sit=*/true);
    CHECK(ms.kind == ClipKind::Gait);
    CHECK(streq(ms.clip, "bewegung/gehen"));
}

// ---------------------------------------------------------------------------
// W10-SIM hardening edges: invalid / out-of-range action types and the "active
// action but invalid type" combinations never deref a bad pointer — the catalog
// lookup is a pure switch defaulting to the empty name (a valid C-string), so the
// selector always returns a printable clip. (ASAN/UBSAN guard.)
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, InvalidActionTypeSelectsNothing) {
    // Negative, zero, and absurdly large action types all fall to the default
    // empty catalog name (no OOB, no null deref).
    for (int t : {-1, -1000, 0, 9999, 0x7fffffff}) {
        CHECK(streq(sim::ActionClipName(t, false), ""));
        CHECK(streq(sim::ActionClipName(t, true), ""));
        NpcClipState st;
        st.hasAction  = true;
        st.actionType = t;
        ClipSelection s = sim::SelectNpcClip(st);
        CHECK(s.kind == ClipKind::None);   // runtime-attach (handler owns the clip)
        CHECK(streq(s.clip, ""));           // never null
    }
}

// An invalid action type with moving/sit set still obeys the action gate (the
// +296 gate is exclusive): None, not the gait/idle — no fall-through indexing.
TEST(NpcClipSelect, InvalidActionTypeWithMotionStaysNone) {
    NpcClipState st;
    st.hasAction  = true;
    st.actionType = -42;
    st.moving     = true;
    st.sit        = true;
    ClipSelection s = sim::SelectNpcClip(st);
    CHECK(s.kind == ClipKind::None);
    CHECK(streq(s.clip, ""));
}

// ---------------------------------------------------------------------------
// Offset/flag constants match the engine record layout (VIBE_Character_Update).
// ---------------------------------------------------------------------------
TEST(NpcClipSelect, RecordOffsetsAndFlags) {
    CHECK_EQ((int)sim::kActionHeadOff, 0x128); // +296 action-queue head
    CHECK_EQ((int)sim::kFlagsAOff,     0x8C);  // +140 flagsA
    CHECK_EQ((int)sim::kFlagsBOff,     0x8D);  // +141 flagsB
    CHECK_EQ((int)sim::kFlagsASitBit,       0x10);
    CHECK_EQ((int)sim::kFlagsBIdlePending,  0x10);
    CHECK_EQ((int)sim::kFlagsBIdleAttached, 0x20);
}
