// See wire_charstate.h. Binds the CharState render leaves + the wireable
// CharacterFactory leaves to their REAL reconstructed cross-module targets.
// GLUE only — no module logic lives here.
#include "sim/wire_charstate.h"

#include "sim/character_state.h"     // CharStateHooks / Set/GetCharStateHooks (fwd LiveActor)
#include "sim/character_factory.h"   // CharacterFactoryHooks / Set/GetCharacterFactoryHooks
#include "sim/character_render2.h"   // ApplyVisibilityState / VisibilityCtx (0x4019cc)
#include "sim/character_render3.h"   // PreloadAniSet / PreloadLowPolyAniSet / CharActor3 (0x403c34/0x403da0)
#include "sim/object_lifecycle3.h"   // ObjectSetPivotVector / SceneNode3 (0x5af490)
#include "config/errorlog.h"         // FormatMessage / ILogSink (0x438da8)

#include <cstring>

namespace guild::sim {

// NOTE: we do NOT include sim/character_query.h here — it transitively pulls
// sim/types.h, whose kPersonStride clashes with the same constant in
// character_render2.h (a pre-existing latent header duplication). We only need the
// Universe type (opaque, via pointer reinterpret) and the IndexFromUniverse leaf, so
// we forward-declare them. Every record field is read by raw byte offset (the
// originals address the record as *(rec+N)), so the full LiveActor / MeshHandle /
// Universe layouts are not needed in this TU.
struct Universe;   // sim/character_query.h — the +136 universe record (g_universes[])
struct LiveActor;  // sim/character_query.h — the 516-byte live-actor record
// gilde.exe 0x426724 — VIBE_Character_IndexFromPointer (character_query.cpp).
int IndexFromUniverse(const Universe* u);

namespace {

// ===========================================================================
// Raw-record byte access. CreateMesh / the flagged-local passes treat the live
// record as the verbatim 516-byte blob the binary addresses by *(rec+N); these
// helpers reproduce that addressing for the fields the adapters read. (The
// LiveActor / CharActor3 / VisibilityCtx structs are NATIVE views over the same
// record base — we read the touched offsets straight off the raw bytes so the
// adapters are byte-faithful, exactly as the originals index *(rec+N).)
// ===========================================================================
inline const unsigned char* RB(const void* p) {
    return static_cast<const unsigned char*>(p);
}
inline float RF(const void* base, int off) {
    float v;
    std::memcpy(&v, RB(base) + off, sizeof(float));
    return v;
}
inline void* RP(const void* base, int off) {
    void* v = nullptr;
    std::memcpy(&v, RB(base) + off, sizeof(void*));
    return v;
}

// Record offsets touched by the adapters (gilde.exe *(rec+N)).
//   +52  mesh / scene node   +76/+80/+84 mesh node world pos
//   +84/+88/+92 actor redraw-target vector
//   +136 owning universe ptr
//   +292 transport mesh ptr  +492 low-poly proxy ptr
//   +304 model base-name string
constexpr int kOffMesh      = 52;
constexpr int kOffTarget    = 84;
constexpr int kOffUniverse  = 136;   // unused directly here (universe via LiveActor)
constexpr int kOffTransport = 292;
constexpr int kOffLowPoly   = 492;
constexpr int kOffBaseName  = 304;
constexpr int kNodePos      = 76;    // mesh node world pos (node+76/+80/+84)

// ===========================================================================
// CharStateHooks adapters.
// ===========================================================================

// VIBE_Character_ProcessFlaggedLocal (0x40204c) leaf:
//   SetPivotVector(*(r+52) /*mesh node*/, r+84 /*pivot vec = {targetX,Y,Z}*/).
// Binds to the REAL VIBE_Object_SetPivotVector @0x5af490 (object_lifecycle3). The
// mesh handle at +52 IS the 0x21C scene-node block (== SceneNode3); the pivot vec
// is the actor's three contiguous redraw-target floats at +84/+88/+92.
void WcApplyPivot(LiveActor* a) {
    if (!a)
        return;
    void* mesh = RP(a, kOffMesh);          // *(r+52)
    if (!mesh)
        return;
    float pivot[3] = {
        RF(a, kOffTarget + 0),
        RF(a, kOffTarget + 4),
        RF(a, kOffTarget + 8),
    };
    ObjectSetPivotVector(reinterpret_cast<SceneNode3*>(mesh), pivot);  // 0x5af490
}

// VIBE_Character_RefreshFlaggedLocal (0x40208c) leaf:
//   v5[i] = *(r+84+4i) + *(*(r+52)+76+4i);     // actor target + mesh node world pos
//   ApplyVisibilityState(r, /*a2=*/0, v5);     // refreshLight == 0 on this path
// Binds to the REAL VIBE_Character_ApplyVisibilityState @0x4019cc (character_render2),
// which runs SetPosition(bodyMesh, v5) (the real record write); the deeper light /
// terrain / transport / low-poly leaves stay inert via CharRender2Hooks defaults
// (refreshLight false => only SetPosition runs). VisibilityCtx is built from the
// record's +52 / +292 / +492 pointers exactly as the original reads them.
void WcApplyVisibility(LiveActor* a) {
    if (!a)
        return;
    void* mesh = RP(a, kOffMesh);          // *(r+52)
    float pos[3] = {0.0f, 0.0f, 0.0f};
    if (mesh) {
        pos[0] = RF(a, kOffTarget + 0) + RF(mesh, kNodePos + 0);
        pos[1] = RF(a, kOffTarget + 4) + RF(mesh, kNodePos + 4);
        pos[2] = RF(a, kOffTarget + 8) + RF(mesh, kNodePos + 8);
    }
    void* transport = RP(a, kOffTransport);
    void* lowPoly   = RP(a, kOffLowPoly);
    VisibilityCtx v{};
    v.bodyMesh      = mesh;                         // *(a1+52)
    v.hasTransport  = (transport != nullptr);       // *(a1+292) != 0
    v.transportMesh = transport ? RP(transport, 0) : nullptr;  // **(a1+292)
    v.hasLowPoly    = (lowPoly != nullptr);         // *(a1+492) != 0
    ApplyVisibilityState(v, pos, /*refreshLight=*/false);  // 0x4019cc, a2 == 0
}

// ===========================================================================
// CharacterFactoryHooks adapters (the still-inert wireable leaves; the attach leaf
// is bound by object_attach_wiring.cpp and preserved by seed-from-defaults).
// ===========================================================================

// VIBE_Character_IndexFromPointer(universe) @0x426724 — universe ptr -> slot index
// (or 0xFFFFFFFF when out of range). Binds to the REAL IndexFromUniverse
// (character_query.cpp). The factory uses the raw value's truthiness; the -1
// sentinel (truthy) drives the "local universe" branch exactly as the original.
unsigned WcIndexFromPointer(void* universe) {
    return static_cast<unsigned>(
        IndexFromUniverse(reinterpret_cast<const Universe*>(universe)));
}

// VIBE_Character_PreloadAniSet(rec, count, names...) @0x403c34. Binds to the REAL
// PreloadAniSet (character_render3): build a CharActor3 view from the record's raw
// fields (+304 baseName, +52 bodyMesh, +492 lowPolyObj) and run the genuine
// path-formatting + per-clip preload loop. The .baf load/attach leaves stay inert
// via CharRender3Hooks defaults (no anim base headless), but the real control flow
// (BuildAniPath, the loop, the dlg-name format) runs end to end.
CharActor3 MakeActor3(LiveActor* rec) {
    CharActor3 a{};
    if (rec) {
        const char* base = reinterpret_cast<const char*>(RB(rec) + kOffBaseName);
        std::strncpy(a.baseName, base, sizeof(a.baseName) - 1);
        a.baseName[sizeof(a.baseName) - 1] = '\0';
        a.bodyMesh   = RP(rec, kOffMesh);
        a.lowPolyObj = RP(rec, kOffLowPoly);
        a.universe   = RP(rec, kOffUniverse);
    }
    return a;
}

void WcPreloadAniSet(LiveActor* rec, const char* const* names, int count) {
    CharActor3 a = MakeActor3(rec);
    PreloadAniSet(&a, names, count);                  // 0x403c34
}

// VIBE_Character_PreloadLowPolyAniSet(rec, 1, "gehen") @0x403da0 — REAL low-poly
// variant of the same preload (the lowpolycharacter/%s/%s_%s_LOW.baf path).
void WcPreloadLowPolyAniSet(LiveActor* rec, const char* const* names, int count) {
    CharActor3 a = MakeActor3(rec);
    PreloadLowPolyAniSet(&a, names, count);           // 0x403da0
}

// VIBE_ErrorLog_ReportMessage(msg) @0x438da8 — the model-load failure report. Binds
// to the REAL ErrorLog formatter (config/errorlog) through a process-lifetime sink
// (the original wrote to a .log file / OutputDebugString; the sink is the
// reconstructed Win32 substitute, so the genuine FormatMessage path runs).
class FactorySink : public config::ILogSink {
public:
    void file(const std::string& t) override { last_ = t; }
    void msgBox(const std::string& t) override { last_ = t; }
    void debug(const std::string& t) override { last_ = t; }
    void console(const std::string& t) override { last_ = t; }
private:
    std::string last_;
};
FactorySink g_factorySink;

void WcReportError(const char* msg) {
    config::FormatMessage(g_factorySink, config::kLogDebug, msg ? msg : "");  // 0x438da8
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
CharStateHooks g_charState{};

} // namespace

void InstallRealCharStateWiring() {
    // --- CharStateHooks (character_state.h) ----------------------------------
    // Seed from the module inert defaults (non-null stubs) so any field we do not
    // override keeps its safe stub; bind both render leaves to their reals.
    g_charState = GetCharStateHooks();
    g_charState.applyPivot      = &WcApplyPivot;        // 0x5af490 (REAL SetPivotVector)
    g_charState.applyVisibility = &WcApplyVisibility;   // 0x4019cc (REAL ApplyVisibilityState)
    SetCharStateHooks(&g_charState);

    // --- CharacterFactoryHooks (character_factory.h) -------------------------
    // SEED from the CURRENT factory table (NOT MakeDefaults) so the genuine attach
    // leaf bound by InstallRealObjectAttachWiring() is preserved, then bind the
    // remaining wireable leaves. findSubstring is ALREADY the real strstr default
    // (DefFindSubstring); seeding keeps it, so it is effectively real.
    CharacterFactoryHooks f = GetCharacterFactoryHooks();
    f.indexFromPointer     = &WcIndexFromPointer;       // 0x426724 (REAL IndexFromUniverse)
    f.preloadAniSet        = &WcPreloadAniSet;          // 0x403c34 (REAL PreloadAniSet)
    f.preloadLowPolyAniSet = &WcPreloadLowPolyAniSet;   // 0x403da0 (REAL PreloadLowPolyAniSet)
    f.reportError          = &WcReportError;            // 0x438da8 (REAL ErrorLog formatter)
    // INERT (no clean portable target / render-GPU leaves, rule 3 / rule 8):
    //   queryTerrainType   (0x404650) — heightmap probe + floor pick (live scene/Vulkan).
    //   buildObjectCache   (0x5c8218) — light cache rebuild (render).
    //   propagateDirtyFlag (0x5af2c0) — scene-graph dirty propagation (render).
    //   updateLowPolyMesh  (0x40244c) — low-poly mesh refresh (render).
    //   destroy            (0x402120) — the inert default ALREADY does the real
    //                                   g_live removal + freeDebug; the full Destroy
    //                                   (action-queue / mesh / morph teardown) is a
    //                                   large standalone target -> kept seeded default.
    //   attachToUniverseNode — preserved from object_attach_wiring (genuine 0x5b3e30).
    SetCharacterFactoryHooks(&f);

    // CharIntroRunHooks (gui/choosecharacter_intro_run.h): every field is a GUI/SDL/
    // input HOST boundary (FormLoad / RadioGroupCreate / RunFrameLoop / KeyCode /
    // ButtonId / ...) with no portable reconstructed leaf. It is driven only by the
    // live native runtime, so there is nothing to install here (fully inert by
    // design, like the sibling ChooseCity screen). See the report.
}

} // namespace guild::sim
