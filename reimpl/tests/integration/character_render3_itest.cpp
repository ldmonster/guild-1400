// Integration test: VIBE_Character_AttachAni (0x404038) / PreloadAniSet (0x403c34)
// wired against a REAL reconstructed sibling. The attach/preload family builds a
// per-clip dialog name "%s_%s" and normalizes it with
// VIBE_Path_ConvertBackslashToSlash (0x44eb54). We forward character_render3's
// `convertBackslashToSlash` hook into the genuine guild::io::ConvertBackslashToSlash
// (NOT a mock) — exactly the live wiring — and assert the cross-module path
// normalization the binary would produce.
//
// The gait-clip loop-flag decision (IsLoopingGait) already delegates to the REAL
// guild::util::StrCmpNoCase sibling inside the module, so the loaded clip's loop
// flag is a second real cross-module result we observe here.
#include "test.h"

#include "sim/character_render3.h"
#include "io/path.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct Capture {
    std::vector<std::string> normalized;   // names passed through the real sibling
    std::vector<std::string> loadedPaths;  // paths handed to loadStreamToStock
    std::vector<int>         loadFlags;    // the loop flag the module computed
};
Capture* g_cap = nullptr;

// The REAL sibling, bound exactly as the binary wires the hook. The module calls
// convertBackslashToSlash(dlg) on the "%s_%s" dialog buffer (mutated in place); we
// forward straight into the reconstructed guild::io function and record the result.
void realConvert(char* s) {
    io::ConvertBackslashToSlash(s);   // genuine 0x44eb54 reimpl, in-place
    if (g_cap && s) g_cap->normalized.push_back(s);
}

void* capLoad(const char* path, int flag) {
    if (g_cap) {
        g_cap->loadedPaths.push_back(path ? path : "");
        g_cap->loadFlags.push_back(flag);
    }
    return nullptr;   // no cached slot -> AttachAni loads, then attaches
}
void* capAttach(void* /*base*/, int /*mask*/) {
    // Return a non-null channel so AttachAni records success (else it errors out).
    static int sentinel = 0;
    return &sentinel;
}
void noopGray(int, int) {}
void noopPrune(void*) {}
void noopReport(const char*) {}

// Fill every hook the attach/preload code path invokes (an installed struct's
// null members are NOT replaced by the inert defaults — only a nullptr struct is).
void wireCommon(CharRender3Hooks& h) {
    std::memset(&h, 0, sizeof(h));
    h.convertBackslashToSlash = realConvert;   // REAL guild::io sibling
    h.findFreeMeshSlot = []() -> void* { return nullptr; };  // force a load
    h.setGrayColorThunk = noopGray;
    h.loadStreamToStock = capLoad;
    h.attachToBone = capAttach;
    h.pruneExpiredAttachments = noopPrune;
    h.reportError = noopReport;
}
}  // namespace

TEST(CharacterRender3Itest, AttachAniDialogNameViaRealPathSibling) {
    Capture cap; g_cap = &cap;

    CharRender3Hooks h{};
    wireCommon(h);
    SetCharRender3Hooks(&h);

    CharActor3 a{};
    std::memset(&a, 0, sizeof(a));
    std::strcpy(a.baseName, "buerger");

    // A clip name carrying a backslash sub-path: the dialog name "%s_%s" becomes
    // "bewegung\\stehen_buerger", which the real sibling must turn into
    // "bewegung/stehen_buerger".
    void* ch = AttachAni(&a, "bewegung\\stehen", 1);

    SetCharRender3Hooks(nullptr);
    g_cap = nullptr;

    // The attach succeeded (channel recorded at a1+112) and flags byte set to -1.
    CHECK(ch != nullptr);
    CHECK(a.attachAnim == ch);
    CHECK_EQ(a.attachFlags, static_cast<u8>(0xFF));

    // The REAL sibling ran on the dialog buffer and converted the backslash.
    CHECK_EQ(static_cast<int>(cap.normalized.size()), 1);
    if (!cap.normalized.empty()) {
        CHECK(cap.normalized[0] == "bewegung/stehen_buerger");
        // No backslash survives the real conversion.
        CHECK(cap.normalized[0].find('\\') == std::string::npos);
    }

    // The .baf path fed to the loader is the forward-slash "character/%s/%s_%s.baf"
    // form (BuildAniPath uses printf, not the sibling, but proves the load fired).
    CHECK_EQ(static_cast<int>(cap.loadedPaths.size()), 1);
    if (!cap.loadedPaths.empty())
        CHECK(cap.loadedPaths[0] == "character/buerger/bewegung\\stehen_buerger.baf");
    // "bewegung\\stehen" is NOT a gait clip -> loop flag 0 (real StrCmpNoCase path).
    if (!cap.loadFlags.empty()) CHECK_EQ(cap.loadFlags[0], 0);
}

// Second cross-module assertion: PreloadAniSet runs the SAME real path sibling on
// each clip's dialog name, AND the gait clip "bewegung/gehen" trips the real
// StrCmpNoCase sibling so its load flag is 1 (loop) while a non-gait clip is 0.
TEST(CharacterRender3Itest, PreloadGaitLoopFlagViaRealStrCmpSibling) {
    Capture cap; g_cap = &cap;

    CharRender3Hooks h{};
    wireCommon(h);
    SetCharRender3Hooks(&h);

    CharActor3 a{};
    std::memset(&a, 0, sizeof(a));
    std::strcpy(a.baseName, "wache");

    const char* names[] = { "bewegung/gehen", "haus\\sitzen" };
    PreloadAniSet(&a, names, 2);

    SetCharRender3Hooks(nullptr);
    g_cap = nullptr;

    // Both dialog names went through the real path sibling.
    CHECK_EQ(static_cast<int>(cap.normalized.size()), 2);
    if (cap.normalized.size() == 2) {
        CHECK(cap.normalized[0] == "bewegung/gehen_wache");
        CHECK(cap.normalized[1] == "haus/sitzen_wache");   // backslash converted
    }
    // The gait clip loads with loop flag 1 (real StrCmpNoCase match); the other 0.
    CHECK_EQ(static_cast<int>(cap.loadFlags.size()), 2);
    if (cap.loadFlags.size() == 2) {
        CHECK_EQ(cap.loadFlags[0], 1);   // bewegung/gehen -> looping
        CHECK_EQ(cap.loadFlags[1], 0);   // haus/sitzen    -> not looping
    }
}
