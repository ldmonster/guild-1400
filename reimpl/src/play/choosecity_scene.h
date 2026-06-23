#pragma once
// =============================================================================
// guild::play — the native bridge that drives the byte-faithful
// gui::Menu_RunChooseCity @0x52e6d8 over the REAL 3D city-select scene.
//
// gui/choosecity_run.* reconstructs RunChooseCity's control flow 1:1 but leaves
// every host leaf (scene setup, the .CTY enumeration + "stadt_<city>" marker
// spawn, the per-frame VIBE_Pick_FindNearestObjectAt, the info-form render, the
// frame loop + input edges) as installable ChooseCityHooks with inert defaults.
//
// This module supplies a concrete hooks implementation backed by:
//   * the parsed Menu/ChooseCity.ed3 scene (play::ParseSceneObjects) with a
//     "stadt_<city>" sp_STADTTURM tower placed at each city dummy marker — the
//     host analogue of VIBE_Map_SpawnCityPointMarker @0x52e2d0,
//   * play::PickNearestObject (the reconstruction of VIBE_Pick_FindNearestObjectAt
//     @0x5b5a38) for the per-frame cursor pick,
// so the REAL Menu_RunChooseCity loop runs the REAL pick -> hover -> info ->
// confirm flow.  All per-frame input (the frame-loop gate, the cursor position,
// and the confirm/cancel edges) comes from an injected Input source, so the same
// bridge runs headless under a scripted source and natively under SDL.
// =============================================================================
#include "gui/choosecity_run.h"
#include "play/scene_persp_render.h"
#include "play/scene_view.h"
#include "guild/common/types.h"

#include <functional>
#include <string>
#include <vector>

namespace guild::shim { class IFileSystem; }
namespace guild::io { class ArchiveMount; }

namespace guild::play {

// Per-frame host input the city loop needs. Each callback takes the 0-based frame
// index. In tests these are scripted; natively they read SDL (window pump, mouse).
struct ChooseCityInput {
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — return false to stop the loop.
    std::function<bool(int frame)> runFrame = [](int) { return false; };
    // Cursor position (device pixels) this frame.
    std::function<void(int frame, float& x, float& y)> cursor =
        [](int, float& x, float& y) { x = -1.0f; y = -1.0f; };
    // dword_75BF38 — the clicked widget id (1210 == confirm; 0 == none).
    std::function<int(int frame)> clickedWidgetId = [](int) { return 0; };
    // byte_67225C — key edge (28 Enter / 1 Esc / 0 none).
    std::function<int(int frame)> keyCode = [](int) { return 0; };
    // dword_672230 — a right-click (cancel) edge.
    std::function<bool(int frame)> rightClick = [](int) { return false; };
    // The chained confirm sub-screens (default: proceed).
    std::function<bool()> chooseCharacterIntro = [] { return true; };
    std::function<bool()> chooseHistory = [] { return true; };
};

class SceneChooseCityHooks : public gui::ChooseCityHooks {
public:
    SceneChooseCityHooks() = default;
    ~SceneChooseCityHooks() override;

    // Mount Resources/{scenes,Objects}.BIN through `fs`, parse Menu/ChooseCity.ed3
    // and place a "stadt_<CITY>" tower at each city dummy marker. `viewW/viewH` is
    // the render/pick viewport. Returns false if the assets are absent/unparseable.
    bool Load(shim::IFileSystem& fs, int viewW = 800, int viewH = 600);

    // The camera the pick projects through (defaults to a map-table framing so the
    // towers are visible). The native front sets the real flown cutscene camera.
    PerspCamera& camera() { return cam_; }
    const std::vector<SceneObjectInst>& scene() const { return scene_; }

    void SetInput(ChooseCityInput in) { input_ = std::move(in); }

    // ---- ChooseCityHooks overrides (the live leaves) ----
    int  SpawnCityTower() override;
    void RunIntroCutscene() override {}
    void SceneSetup() override {}

    int  EnumerateCityFiles(const char* dir, const char* ext,
                            std::vector<std::string>& outFiles) override;
    bool ReadCityName(const std::string& file, std::string& outName) override;
    int  SpawnCityMarker(const std::string& cityName) override;
    int  FindTextIndex(const std::string& upperKey) override;
    void RegisterStatusText(const std::string& key, const std::string& text) override;
    void DestroyMarker(int marker) override { (void)marker; }
    void DestroyCityTower(int tower) override { (void)tower; }

    int  FormLoad(const char* name) override;
    void FormPositionAndSelect(int, int) override {}
    void FormSetVisible(int, int) override {}
    void FormDestroy(int) override {}

    int  RunFrameLoop(int frame) override { return input_.runFrame(frame) ? 1 : 0; }
    int  PickNearestObject(int frame) override;
    bool PickIsHover(int frame) override;
    std::string ObjectName(int object) override;
    void RenderCityInfo(const std::string& cityName, int infoText) override;

    int  ClickedWidgetId(int frame) override { return input_.clickedWidgetId(frame); }
    int  KeyCode(int frame) override { return input_.keyCode(frame); }
    bool RightClick(int frame) override { return input_.rightClick(frame); }

    bool ChooseCharacterIntroVariant() override { return input_.chooseCharacterIntro(); }
    bool RunChooseHistory() override { return input_.chooseHistory(); }

    // Test/inspection: the last city info rendered (cityName + infoText).
    const std::string& lastInfoCity() const { return lastInfoCity_; }
    int lastInfoText() const { return lastInfoText_; }

private:
    // Resolve the pick: cache the per-frame result so PickNearestObject /
    // PickIsHover / ObjectName see one consistent pick (the original computes it
    // once at 0x52ea03 and reads it back).
    int PickFor(int frame);

    shim::IFileSystem* fs_ = nullptr;        // not owned (the real game-dir fs)
    std::string citiesDir_;                  // the resolved gamedata/Cities path
    io::ArchiveMount* objects_ = nullptr;   // owned (the mount of Objects.BIN)
    std::vector<SceneObjectInst> scene_;     // parsed ChooseCity.ed3 + stadt_ towers
    PerspCamera cam_;
    int viewW_ = 800, viewH_ = 600;
    ChooseCityInput input_;

    // marker handle == sceneIndex+1 (0 == none). One per "stadt_<city>" tower.
    std::vector<int> markerHandles_;
    int towerProto_ = 0;

    int cachedFrame_ = -1;
    int cachedPick_  = -1;   // scene index of the picked object (-1 == none)

    std::string lastInfoCity_;
    int lastInfoText_ = -2;
};

} // namespace guild::play
