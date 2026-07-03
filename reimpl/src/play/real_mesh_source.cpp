#include "play/real_mesh_source.h"

#include "render/geometry_types.h"

// guild::play — REAL MESH SOURCE bridge implementation.
//
// Integration glue, not a translation: it wires the reconstructed archive mount
// (io::ArchiveMount), the shipped AGF decoder (render::LoadAgfModel), and the
// geometry builder (render::BuildGeometry) into a name -> MeshGeometry* source the
// object_mesh_render MeshResolver hook can consume. All real reconstructed code
// over real bytes; the only OS boundary is shim::IFileSystem (passed in).
namespace guild::play {

bool RealMeshSource::MountArchive(shim::IFileSystem* fs, const char* archivePath,
                                  bool caseInsensitive) {
    if (!fs || !archivePath) return false;
    mount_ = std::make_unique<io::ArchiveMount>();
    if (!mount_->Mount(fs, archivePath, caseInsensitive)) {
        mount_.reset();
        return false;
    }
    return true;
}

RealMeshSource::Cached* RealMeshSource::loadFromArchive(const char* memberName) {
    if (!mount_ || !mount_->isMounted() || !memberName) return nullptr;

    std::vector<u8> bytes;
    if (!mount_->OpenMember(memberName, bytes) || bytes.empty()) return nullptr;

    auto cached = std::make_unique<Cached>();
    // The engine's loader order (VIBE_Mesh_LoadBgfFile @0x5D2348): the binary
    // FAST CHUNK first — the pre-baked (Y-up) cooked mesh — and only on failure
    // the AGF token-script fallback. The AGF stream is the RAW authoring data
    // (Z-up; the engine's fallback path bakes the up-conversion — the documented
    // morph-rotation-bake gap), so preferring the fast chunk both matches the
    // engine and yields upright geometry.
    if (!render::LoadFastChunk(bytes.data(), bytes.size(), cached->model) ||
        cached->model.polyCount == 0) {
        cached->model = render::BgfModel{};
        if (!render::LoadAgfModel(bytes.data(), bytes.size(), cached->model))
            return nullptr;
    }
    if (!render::BuildGeometry(cached->model, cached->geom))
        return nullptr;
    cached->valid = true;

    Cached* raw = cached.get();
    cache_[memberName] = std::move(cached);
    return raw;
}

render::MeshGeometry* RealMeshSource::Resolve(const char* memberName) {
    if (!memberName) return nullptr;
    auto it = cache_.find(memberName);
    Cached* c = (it != cache_.end()) ? it->second.get() : loadFromArchive(memberName);
    if (!c || !c->valid) return nullptr;
    return c->geom.View();
}

render::MeshGeometry* RealMeshSource::ResolveWithBounds(const char* memberName,
                                                        render::BgfBounds* outBounds) {
    if (!memberName) return nullptr;
    auto it = cache_.find(memberName);
    Cached* c = (it != cache_.end()) ? it->second.get() : loadFromArchive(memberName);
    if (!c || !c->valid) return nullptr;
    if (outBounds) *outBounds = render::ComputeBoundingExtents(c->model);
    render::ComputeVertexNormals(c->model);
    return c->geom.View();
}

const render::BgfModel* RealMeshSource::ModelFor(const char* memberName) const {
    if (!memberName) return nullptr;
    auto it = cache_.find(memberName);
    if (it == cache_.end() || !it->second->valid) return nullptr;
    return &it->second->model;
}

render::MeshGeometry* RealMeshSource::DecodeBuffer(const char* key, const u8* data,
                                                   std::size_t size) {
    if (!key || !data) return nullptr;
    auto cached = std::make_unique<Cached>();
    if (!render::LoadAgfModel(data, size, cached->model)) return nullptr;
    if (!render::BuildGeometry(cached->model, cached->geom)) return nullptr;
    cached->valid = true;
    Cached* raw = cached.get();
    cache_[key] = std::move(cached);
    return raw->geom.View();
}

// ---------------------------------------------------------------------------
// Installable hooks + inert defaults (defined here so the unified build links).
// ---------------------------------------------------------------------------
namespace {
RealMeshSource*   g_activeSource = nullptr;
MeshNameResolver  g_nameResolver = &DefaultMeshNameResolver;
}

std::string DefaultMeshNameResolver(const EntityRef&) { return std::string(); }

void InstallRealMeshSource(RealMeshSource* src) { g_activeSource = src; }
void InstallMeshNameResolver(MeshNameResolver r) {
    g_nameResolver = r ? r : &DefaultMeshNameResolver;
}
RealMeshSource* ActiveRealMeshSource() { return g_activeSource; }
MeshNameResolver ActiveMeshNameResolver() { return g_nameResolver; }

const render::MeshGeometry* RealMeshResolver(const EntityRef& e) {
    RealMeshSource* src = g_activeSource;
    if (!src) return nullptr;
    std::string name = g_nameResolver(e);
    if (name.empty()) return nullptr;
    return src->Resolve(name.c_str());
}

} // namespace guild::play
