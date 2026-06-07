#pragma once
// Wave 25 PLAY — THE PLAYABLE SLICE: the end-to-end proof that the whole play
// layer runs as ONE loop (namespace guild::play).
//
// Every other play-layer module proves ONE link of the chain in isolation:
//   real_session.{h,cpp}  load + save round-trip a real city (P3/M3)
//   world_render.{h,cpp}  render the live world through the REAL pipeline (P2/M2)
//   input_command.{h,cpp} turn a world-view click into a real unit ORDER (P1/P5)
//   turn_economy.{h,cpp}  advance ONE game-day of the REAL economy passes (P4)
//   determinism / world_digest  hash the whole live world (P4/P7)
//
// This module is the INTEGRATOR: it wires those links into the single
// load -> render -> input -> command -> simulate -> render loop a real game frame
// runs, and proves two end-to-end invariants over it:
//
//   (1) the world genuinely CHANGED between the two rendered frames — the click
//       command + the game-day mutated live state, so HashFullWorld() differs
//       across frame1 and frame2; and
//   (2) the whole run is DETERMINISTIC — the same city + the same scripted inputs
//       + the same seed reproduce a byte-identical run (identical hashes, identical
//       rendered frames) on every rerun.
//
// It is ADDITIVE (new file): it CALLS the already-reconstructed siblings above
// (never redefines them) and installs no global state of its own beyond the
// per-run command-apply handler each module already exposes. Headless and GUARDED:
// the caller supplies the filesystem + graphics device, and the real-asset path is
// gated on the AUGSBURG.cty asset being present.
#include <cstdint>
#include <string>

#include "guild/common/types.h"
#include "play/input_command.h"   // CursorMode
#include "shim/IFileSystem.h"

namespace guild::shim { class IGraphicsDevice; }

namespace guild::play {

// ===========================================================================
// One scripted world-view click the slice replays as a unit ORDER. (sx,sy) is the
// cursor in framebuffer pixels; `mode` is the selected order tool. The slice picks
// the live scene object nearest the cursor (within pickRadius) and issues the
// classified order, mutating the picked object's live record.
// ===========================================================================
struct SliceClick {
    float      sx = 0.0f, sy = 0.0f;     // cursor position (framebuffer pixels)
    float      pickRadius = 64.0f;       // pick tolerance (pixels)
    CursorMode mode = CursorMode::kMove; // order tool (kMove -> a plain MOVE order)
    bool       attackAllowed = false;    // gate for the unit-attack branch
};

// ===========================================================================
// The result of one full playable-slice run.
// ===========================================================================
struct SliceResult {
    bool loaded = false;          // the city loaded into the live arrays
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;

    // --- frame 1 (after load) ---
    bool   frame1Rendered = false;
    int    frame1Objects  = 0;    // scene objects drawn in frame 1
    int    frame1NonClear = 0;    // non-background pixels in frame 1
    std::string frame1Path;       // dumped BMP path (empty if no device)

    // --- the scripted click -> command ---
    bool   commandIssued  = false;   // IssueWorldClick issued an order
    bool   commandEnqueued= false;   // the order hit the send ring (was applied)
    int    commandKind    = 0;       // the classified order kind (OrderKind)
    i32    commandTarget  = 0;       // the picked target entity id

    // --- the game-day ---
    int    economyPasses  = 0;       // Amt passes the day ran

    // --- frame 2 (after command + day) ---
    bool   frame2Rendered = false;
    int    frame2Objects  = 0;
    int    frame2NonClear = 0;
    std::string frame2Path;

    // --- determinism oracle (the four world hashes, fold order = loop order) ---
    std::uint64_t hashAfterLoad    = 0;  // HashFullWorld() right after load
    std::uint64_t hashAfterCommand = 0;  // ... after the click command applied
    std::uint64_t hashAfterDay     = 0;  // ... after the game-day (== frame2 world)

    // The world CHANGED across the two frames (the central proof).
    bool worldChanged() const { return hashAfterLoad != hashAfterDay; }
    // The command alone mutated the world.
    bool commandChangedWorld() const { return hashAfterLoad != hashAfterCommand; }

    bool ok() const {
        return loaded && frame1Rendered && frame2Rendered && worldChanged();
    }
};

// ===========================================================================
// RunPlayableSlice — the whole loop over a REAL city.
//
//   1. mount `gameDir` (app::MountRealGameAssets over `fs`) + io::LoadWorld the
//      city `<UPPER(cityName)>.cty` into the live sim arrays,
//   2. RENDER frame 1 from the live world through the REAL render pipeline
//      (play::WorldRenderer) into `dev1` (if non-null) — and dump it,
//   3. HashFullWorld() -> hashAfterLoad,
//   4. replay `click` as a unit ORDER via play::IssueWorldClick (the REAL
//      classifier+builder+queue), applying the opcode-80 mutation to the picked
//      live object -> HashFullWorld() -> hashAfterCommand,
//   5. advance ONE game-day via play::RunEconomyTurn (seeded by `econSeed`) ->
//      HashFullWorld() -> hashAfterDay,
//   6. RENDER frame 2 into `dev2` (if non-null) — and dump it,
//   7. assert the world changed (hashAfterLoad != hashAfterDay).
//
// `dev1`/`dev2` must already be init()'d to `fbW`x`fbH`x16bpp (the slice renders
// at that geometry). Either may be null to skip a dump (the hashes still compute).
// The VFS is bound for the duration and shut down on return; the live arrays are
// left populated with the final (post-day) world.
//
// Returns the run result (counts, hashes, frame stats). The SAME arguments
// reproduce a byte-identical result on every call (determinism).
SliceResult RunPlayableSlice(shim::IFileSystem* fs, const std::string& gameDir,
                             const std::string& cityName,
                             const SliceClick& click, std::uint32_t econSeed,
                             int fbW, int fbH,
                             shim::IGraphicsDevice* dev1,
                             shim::IGraphicsDevice* dev2);

// ===========================================================================
// SliceStep — the abstract step sequence the slice runs, exposed so the unit test
// can drive the SAME sequencing on a SYNTHETIC live world (no assets) and assert
// each step's pre/post HashFullWorld() behaves as the loop requires.
// ===========================================================================
enum class SliceStep {
    kLoad     = 0,   // world populated (synthetic seed or io::LoadWorld)
    kRender1  = 1,   // render frame 1 (no state mutation)
    kCommand  = 2,   // apply the click command (mutates a live object)
    kDay      = 3,   // run the economy day (mutates economy + RNG + witness)
    kRender2  = 4,   // render frame 2 (no state mutation)
};

// One measured step in a slice run: the world hash AFTER the step, and whether the
// step mutated the world relative to the previous step.
struct SliceStepHash {
    SliceStep     step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;   // hashAfter != previous step's hashAfter
};

// Drive the slice STEP SEQUENCE on a SYNTHETIC live world (seeded into the live sim
// arrays from `seed`, `persons` people + `objects` objects). No assets, no device:
// it measures HashFullWorld() before/after each of the five steps so the unit test
// can assert render steps are pure (no mutation) while command/day steps mutate.
// `out` receives one SliceStepHash per step in loop order; returns the step count.
int RunSliceStepsSynthetic(std::uint32_t seed, int persons, int objects,
                           const SliceClick& click, std::uint32_t econSeed,
                           int fbW, int fbH,
                           SliceStepHash* out, int cap);

} // namespace guild::play
