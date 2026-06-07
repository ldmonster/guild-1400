// Integration test: VIBE_Light_ApplyAmbient (0x42e0b0) wired against a REAL
// reconstructed sibling. ApplyAmbient brackets a VIBE_Object_DetachAndRelease
// (0x5b4258) between two universe-slot switches; that DetachAndRelease is itself a
// fully reconstructed function (guild::sim::ObjectDetachAndRelease). We forward
// render_leaves3's `detachAndRelease` hook into the genuine ObjectDetachAndRelease
// (NOT a mock) — exactly the live wiring — and assert the cross-module flow,
// including the SECOND-level effect that the real sibling performs through the
// object_lifecycle5 cluster (its unlink + dispose leaves).
#include "test.h"

#include "render/render_leaves3.h"
#include "sim/object_lifecycle5.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
// One real scene node the chain operates on. ObjectDetachAndRelease takes a Node5*;
// ApplyAmbient passes an int `obj`, so we use a fixed nonzero handle and resolve it
// back to this node in the forwarding hook (the binary's `obj` is a 32-bit ptr).
sim::Node5 g_node;
constexpr int kObjHandle = 0x1234;

struct Capture {
    int  detachArg = 0;       // the int ApplyAmbient forwarded
    char detachResult = -1;   // what the real sibling returned
    int  unlinkCalls = 0;     // second-level effect inside the real sibling
    int  disposeCalls = 0;
    sim::Node5* unlinkNode = nullptr;
};
Capture* g_cap = nullptr;

// The REAL sibling, bound exactly as the binary wires the hook
// (i8 detachAndRelease(int obj) == VIBE_Object_DetachAndRelease at 0x5b4258).
i8 realDetachAndRelease(int obj) {
    if (g_cap) g_cap->detachArg = obj;
    sim::Node5* node = (obj == kObjHandle) ? &g_node : nullptr;
    char r = sim::ObjectDetachAndRelease(node);   // genuine 0x5b4258 reimpl
    if (g_cap) g_cap->detachResult = r;
    return static_cast<i8>(r);
}

// object_lifecycle5's OWN hooks — observe the second-level effects the real
// sibling produces (it unlinks then disposes the node).
void capUnlink(sim::Node5* n) { if (g_cap) { g_cap->unlinkCalls++; g_cap->unlinkNode = n; } }
void capDispose(sim::Node5*)  { if (g_cap) g_cap->disposeCalls++; }

int g_switchCalls = 0;
void capSwitch(u32, int, int, int) { g_switchCalls++; }
}  // namespace

TEST(RenderLeaves3Itest, ApplyAmbientDetachViaRealObjectSibling) {
    Capture cap; g_cap = &cap;
    g_node = sim::Node5{};   // value-init (Node5 is non-trivial)
    g_switchCalls = 0;

    // Install render_leaves3's hook forwarding into the REAL ObjectDetachAndRelease.
    RenderLeaves3Hooks rh{};
    std::memset(&rh, 0, sizeof(rh));
    rh.detachAndRelease = realDetachAndRelease;
    rh.switchActiveSlot = capSwitch;
    InstallRenderLeaves3Hooks(rh);

    // Wire the real sibling's own cluster hooks so its unlink/dispose are observable.
    sim::ObjLife5Hooks oh{};
    oh.objUnlinkFromList = capUnlink;
    oh.objDispose = capDispose;
    sim::ObjLife5SetHooks(oh);

    // slotPtr != activeViewPtr(0) -> the index path runs the bracketing switches.
    // slotArrayBase chosen so (slotPtr - base)/0x3D8 lands in [0,0x40).
    const int slotArrayBase = 0x10000;
    const int slotPtr = slotArrayBase + 0x3D8 * 5;   // -> slot index 5
    i8 r = ApplyAmbient(kObjHandle, /*extra*/ 0, slotPtr, slotArrayBase,
                        /*activeSlot*/ 2);

    InstallRenderLeaves3Hooks(RenderLeaves3Hooks{});  // reset to defaults
    sim::ObjLife5ResetHooks();
    g_cap = nullptr;

    // ApplyAmbient forwarded the obj handle into the real sibling.
    CHECK_EQ(cap.detachArg, kObjHandle);
    // The real ObjectDetachAndRelease returned 1 for a non-null node.
    CHECK_EQ(static_cast<int>(cap.detachResult), 1);
    // ApplyAmbient took the slot-index path -> two SwitchActiveSlot brackets.
    CHECK_EQ(g_switchCalls, 2);
    // ApplyAmbient returns 1 on the index path (it re-switches before returning).
    CHECK_EQ(static_cast<int>(r), 1);

    // SECOND-level cross-module effect: the real sibling unlinked + disposed the
    // node through its own (object_lifecycle5) cluster, on OUR exact node.
    CHECK_EQ(cap.unlinkCalls, 1);
    CHECK_EQ(cap.disposeCalls, 1);
    if (cap.unlinkNode) CHECK(cap.unlinkNode == &g_node);
}

// Second assertion: when ApplyAmbient is already on the active view (slotPtr == 0),
// it skips both slot switches but STILL drives the real sibling once and returns
// that sibling's status byte verbatim.
TEST(RenderLeaves3Itest, ApplyAmbientActiveViewReturnsRealSiblingStatus) {
    Capture cap; g_cap = &cap;
    g_node = sim::Node5{};   // value-init (Node5 is non-trivial)
    g_switchCalls = 0;

    RenderLeaves3Hooks rh{};
    std::memset(&rh, 0, sizeof(rh));
    rh.detachAndRelease = realDetachAndRelease;
    rh.switchActiveSlot = capSwitch;
    InstallRenderLeaves3Hooks(rh);

    sim::ObjLife5Hooks oh{};
    oh.objUnlinkFromList = capUnlink;
    oh.objDispose = capDispose;
    sim::ObjLife5SetHooks(oh);

    // slotPtr == 0 == activeViewPtr -> no bracket switches; result is the sibling's.
    i8 r = ApplyAmbient(kObjHandle, 0, /*slotPtr*/ 0, /*slotArrayBase*/ 0,
                        /*activeSlot*/ 0);

    InstallRenderLeaves3Hooks(RenderLeaves3Hooks{});
    sim::ObjLife5ResetHooks();
    g_cap = nullptr;

    CHECK_EQ(g_switchCalls, 0);                       // active-view fast path
    CHECK_EQ(static_cast<int>(r), 1);                 // real sibling's status byte
    CHECK_EQ(static_cast<int>(cap.detachResult), 1);
    CHECK_EQ(cap.unlinkCalls, 1);
    CHECK_EQ(cap.disposeCalls, 1);
}
