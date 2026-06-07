#pragma once
// =============================================================================
// guild::play — REAL MESH SOURCE (PLAYABLE_PLAN P2/P6 bridge).
//
// object_mesh_render.{h,cpp} draws each live object as its real geometry IF a
// MeshResolver hook hands it a render::MeshGeometry*; its inert default returns
// null (-> quad fallback). This module supplies that geometry from the REAL
// shipped assets: it opens a .bgf entry out of Resources/Objects.BIN through the
// reconstructed VFS / ArchiveMount (decompressing the DEFLATE member), parses the
// shipped AGF token-script format (render::LoadAgfModel), builds engine-stride
// geometry (render::BuildGeometry), and returns a render::MeshGeometry* the source
// owns and caches by name.
//
// It does NOT touch object_mesh_render.* — it matches that module's resolver hook
// shapes so a test can install it. Two installable hooks (with inert defaults
// defined in the .cpp, per the build model):
//   * a NAME resolver: EntityRef -> mesh name (so an entity maps to an asset),
//   * a process-global "active source" pointer the MeshResolver adapter reads.
// The default name resolver returns "" and the default active source is null, so
// the adapter resolver returns null (quad fallback) until a test installs a real
// source + name map.
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"
#include "render/agf_loader.h"
#include "render/bgf_loader.h"
#include "shim/IFileSystem.h"
#include "play/world_render.h"          // EntityRef
#include "play/object_mesh_render.h"    // MeshResolver signature

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace guild::render { struct MeshGeometry; }

namespace guild::play {

// ---------------------------------------------------------------------------
// RealMeshSource — opens .bgf members from a mounted PKZIP archive (Objects.BIN),
// decodes the AGF format, and hands back cached render::MeshGeometry* by name.
// ---------------------------------------------------------------------------
class RealMeshSource {
public:
    RealMeshSource() = default;

    // Mount an archive (e.g. "Resources/Objects.BIN") through `fs`. Returns true
    // on success. May be called once; opens lazily per member afterwards.
    bool MountArchive(shim::IFileSystem* fs, const char* archivePath,
                      bool caseInsensitive = false);

    // True once an archive is mounted.
    bool mounted() const { return mount_ && mount_->isMounted(); }
    std::size_t memberCount() const { return mount_ ? mount_->memberCount() : 0; }

    // Resolve a mesh by its archive member name (e.g. "_DYNAMIC/Buch/Buch.bgf").
    // Opens + decompresses + decodes + builds geometry on first use, caches the
    // result, and returns a pointer the source owns. Returns null if the member
    // is absent or the decode fails. `outModel` (optional) receives the parsed
    // BgfModel for inspection.
    render::MeshGeometry* Resolve(const char* memberName);

    // Like Resolve but also fills `outBounds` with the bounding extents and
    // recomputes vertex normals on the cached model. Returns the geometry or null.
    render::MeshGeometry* ResolveWithBounds(const char* memberName,
                                            render::BgfBounds* outBounds);

    // Direct access to the last parsed model for a member (or null if not cached).
    const render::BgfModel* ModelFor(const char* memberName) const;

    // Decode a flat decompressed AGF buffer directly into an owned geometry slot
    // keyed by `key` (no archive needed). Returns the geometry or null on failure.
    render::MeshGeometry* DecodeBuffer(const char* key, const u8* data, std::size_t size);

private:
    struct Cached {
        render::BgfModel    model;
        render::BgfGeometry geom;
        bool                valid = false;
    };
    std::unique_ptr<io::ArchiveMount>          mount_;
    std::map<std::string, std::unique_ptr<Cached>> cache_;

    Cached* loadFromArchive(const char* memberName);
};

// ---------------------------------------------------------------------------
// Installable hooks (inert defaults in the .cpp; tests install real behavior).
// ---------------------------------------------------------------------------

// Name resolver: EntityRef -> archive member name (.bgf). Inert default: "".
using MeshNameResolver = std::string (*)(const EntityRef& e);
std::string DefaultMeshNameResolver(const EntityRef& e);

// Install / read the process-global active source + name resolver the adapter
// (RealMeshResolver below) consults. Defaults: source=null, resolver=inert.
void InstallRealMeshSource(RealMeshSource* src);
void InstallMeshNameResolver(MeshNameResolver r);
RealMeshSource* ActiveRealMeshSource();
MeshNameResolver ActiveMeshNameResolver();

// A MeshResolver (object_mesh_render.h) adapter: maps the EntityRef through the
// installed name resolver, then through the installed source. Returns null when no
// source / no name / decode fails (-> the renderer's quad fallback). This is the
// function a test installs into ObjectMeshRenderer::Options::meshResolver.
const render::MeshGeometry* RealMeshResolver(const EntityRef& e);

} // namespace guild::play
