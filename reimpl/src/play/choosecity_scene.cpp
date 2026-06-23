// guild::play — native bridge for gui::Menu_RunChooseCity over the 3D scene. See header.
#include "play/choosecity_scene.h"

#include "io/archive_mount.h"
#include "shim/IFileSystem.h"

#include <cctype>

namespace guild::play {

namespace {
std::string Upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
// Case-insensitive test: does `name` end with `ext` (e.g. ".CTY")?
bool HasExt(const std::string& name, const char* ext) {
    const std::string e = Upper(ext);
    if (name.size() < e.size()) return false;
    return Upper(name.substr(name.size() - e.size())) == e;
}
} // namespace

SceneChooseCityHooks::~SceneChooseCityHooks() { delete objects_; }

bool SceneChooseCityHooks::Load(shim::IFileSystem& fs, int viewW, int viewH) {
    viewW_ = viewW; viewH_ = viewH;
    fs_ = &fs;
    delete objects_;
    objects_ = new io::ArchiveMount();

    // Resolve the gamedata cities dir (the on-disk casing is "Cities"; the engine's
    // VFS path "gamedata/cities" is case-insensitive — try a couple of variants).
    citiesDir_.clear();
    for (const char* cand : {"Resources/gamedata/Cities", "Resources/gamedata/cities",
                             "gamedata/Cities", "gamedata/cities"}) {
        shim::IDirListing* dl = fs.listDir(cand);
        if (dl) { citiesDir_ = cand; delete dl; break; }
    }

    io::ArchiveMount scenes;
    if (!scenes.Mount(&fs, "Resources/scenes.BIN", /*caseInsensitive=*/true)) return false;
    if (!objects_->Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true)) return false;

    std::vector<u8> ed3;
    if (!scenes.OpenMember("Menu/ChooseCity.ed3", ed3) || ed3.empty()) return false;
    scene_ = ParseSceneObjects(ed3.data(), ed3.size());
    if (scene_.size() < 5) return false;

    // The real pick camera (BuildSceneCamera): eye = dummy_A2 +92(+144), orientation
    // = the scene MegaCam look (PointToBoneLocalSpace world->view). Fall back to a
    // marker framing only if the cutscene dummies/header are absent.
    if (!BuildSceneCamera(ed3.data(), ed3.size(), scene_, "dummy_A2", cam_) &&
        !BuildSceneCamera(ed3.data(), ed3.size(), scene_, "dummy_A1", cam_)) {
        cam_.eye[0] = -90.0f; cam_.eye[1] = 78.0f; cam_.eye[2] = 172.0f;
        cam_.target[0] = -90.0f; cam_.target[1] = 40.0f; cam_.target[2] = 240.0f;
        cam_.up[0] = 0.0f; cam_.up[1] = 1.0f; cam_.up[2] = 0.0f;
        cam_.engineProjection = true;
    }
    return true;
}

int SceneChooseCityHooks::SpawnCityTower() {
    // VIBE_Object_AttachToUniverseNode("sp_STADTTURM") — the prototype tower (0x52ecb7).
    towerProto_ = 1;
    return towerProto_;
}

int SceneChooseCityHooks::EnumerateCityFiles(const char* dir, const char* ext,
                                             std::vector<std::string>& outFiles) {
    (void)dir;
    // VIBE_SaveBrowser_EnumerateSaveFiles("gamedata/cities", ext, ...) @0x569530 — list
    // the real gamedata/Cities directory and keep the files matching the extension
    // (".CTY" local / ".NET" network). Each yields one city.
    outFiles.clear();
    if (!fs_ || citiesDir_.empty()) return 0;
    shim::IDirListing* dl = fs_->listDir(citiesDir_.c_str());
    if (!dl) return 0;
    for (std::size_t i = 0; i < dl->count(); ++i) {
        const shim::DirEntry& e = dl->at(i);
        if (e.isDir || !e.name) continue;
        if (HasExt(e.name, ext)) outFiles.push_back(e.name);
    }
    delete dl;
    return static_cast<int>(outFiles.size());
}

bool SceneChooseCityHooks::ReadCityName(const std::string& file, std::string& outName) {
    // The "city name" is the file base (the engine reads it from the save header).
    std::size_t dot = file.rfind('.');
    outName = (dot == std::string::npos) ? file : file.substr(0, dot);
    return !outName.empty();
}

int SceneChooseCityHooks::SpawnCityMarker(const std::string& cityName) {
    // VIBE_Map_SpawnCityPointMarker(name) @0x52e2d0 — place a "stadt_<city>" tower at
    // the city's dummy marker. Returns the marker handle (sceneIndex+1; 0 == not found).
    const std::string dummy = "dummy_" + cityName;
    for (const auto& d : scene_) {
        if ((d.type == 2 || d.type == 3) && Upper(d.name) == Upper(dummy)) {
            SceneObjectInst t;
            t.name = "stadt_" + cityName;     // ChooseCity_IsCityObject prefix
            t.type = 4;
            t.mesh = "sp_STADTTURM";
            t.hasMesh = true;
            t.pos[0] = d.pos[0]; t.pos[1] = d.pos[1]; t.pos[2] = d.pos[2];
            scene_.push_back(std::move(t));
            const int handle = static_cast<int>(scene_.size());   // index+1
            markerHandles_.push_back(handle);
            return handle;
        }
    }
    return 0;
}

int SceneChooseCityHooks::FindTextIndex(const std::string& upperKey) {
    // No text DB in the headless bridge; yield a stable non-negative index per key so
    // the info row registers (the native front resolves the real Text_FindTextArrayIndex).
    return static_cast<int>(upperKey.size());
}

void SceneChooseCityHooks::RegisterStatusText(const std::string&, const std::string&) {}

int SceneChooseCityHooks::FormLoad(const char* name) {
    // Distinct non-(-1) handles so the cleanup path runs (header vs main form).
    if (name && std::string(name).find("HEADER") != std::string::npos) return 81;
    return 85;
}

int SceneChooseCityHooks::PickFor(int frame) {
    if (frame == cachedFrame_) return cachedPick_;
    cachedFrame_ = frame;
    float x = -1.0f, y = -1.0f;
    input_.cursor(frame, x, y);
    cachedPick_ = play::PickNearestObject(scene_, *objects_, cam_, viewW_, viewH_, x, y);
    return cachedPick_;
}

int SceneChooseCityHooks::PickNearestObject(int frame) {
    int idx = PickFor(frame);
    return (idx >= 0) ? (idx + 1) : 0;   // object handle (0 == none)
}

bool SceneChooseCityHooks::PickIsHover(int frame) {
    // dword_67221C — a selectable hit this frame (any picked mesh object).
    return PickFor(frame) >= 0;
}

std::string SceneChooseCityHooks::ObjectName(int object) {
    if (object >= 1 && object <= static_cast<int>(scene_.size()))
        return scene_[static_cast<std::size_t>(object - 1)].name;
    return std::string();
}

void SceneChooseCityHooks::RenderCityInfo(const std::string& cityName, int infoText) {
    lastInfoCity_ = cityName;
    lastInfoText_ = infoText;
}

} // namespace guild::play
