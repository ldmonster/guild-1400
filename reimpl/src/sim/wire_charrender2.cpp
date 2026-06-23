// See wire_charrender2.h. Binds the reconstructed VIBE_Path_ConvertBackslashToSlash
// (@0x44eb54) into CharRender3Hooks.convertBackslashToSlash — the single pure-logic leaf
// across the CharRender2/3/Mesh/Query bridges. Glue only — no module logic.
#include "sim/wire_charrender2.h"

#include "sim/character_render3.h"  // CharRender3Hooks / Set|GetCharRender3Hooks
#include "io/path.h"                // REAL guild::io::ConvertBackslashToSlash (0x44eb54)

namespace guild::sim {

namespace {

// VIBE_Path_ConvertBackslashToSlash @0x44eb54 — in-place '\\'(92) -> '/'(47), byte-faithful
// (verified 1:1: strlen-once scan). The original is __usercall returning the end pointer in
// eax, but the hook is `void (*)(char*)` and every call site uses it only for the side
// effect; we discard the return, matching the hook's contract exactly.
void WcConvertBackslashToSlash(char* s) {
    guild::io::ConvertBackslashToSlash(s);
}

// Process-lifetime wired hook table (the global hook ptr references this).
CharRender3Hooks g_render3{};

} // namespace

void InstallRealCharRender2Wiring() {
    // SEED-FROM-DEFAULTS: copy the module's inert defaults, then override only the one
    // reconstructed field. Required — character_render3.cpp's attach/preload family calls
    // h.convertBackslashToSlash(...) WITHOUT a null-check, and GetCharRender3Hooks() returns
    // the installed table verbatim (no per-field fallback), so every unbound field must keep
    // its safe inert default.
    g_render3 = GetCharRender3Hooks();
    g_render3.convertBackslashToSlash = &WcConvertBackslashToSlash;  // VIBE_Path_ConvertBackslashToSlash 0x44eb54
    // All other CharRender3 fields (findFreeMeshSlot / setGrayColorThunk / loadStreamToStock /
    // attachToBone / pruneExpiredAttachments / createMorphAnim / computeBoneDelta /
    // attachToUniverseNode / detachAndRelease / objectAttachToBone / buildLightCache /
    // switchUniverse / setPivotVector / setObjectPosition / soundPlaySample /
    // sound3dPlayOneShot / reportError): anim/path-loader/renderer/object/light/universe/
    // sound/script LEAVES (rule 3) — no reconstructed pure-logic target -> kept inert.
    SetCharRender3Hooks(&g_render3);

    // CharRender2Hooks / CharMeshHooks / CharQueryHooks: every field is a render/scene/object/
    // universe/light/heightmap/person-table/mesh-walk LEAF (rule 3) — zero reconstructed
    // pure-logic field to bind. They stay on their inert defaults; installing an empty table
    // would only risk shadowing those vetted defaults, so we deliberately install nothing.
}

} // namespace guild::sim
