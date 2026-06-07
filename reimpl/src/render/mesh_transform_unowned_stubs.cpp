// Placeholder definitions for two render leaves that mesh_transform.cpp calls
// but that no module owns YET (only comment-refs exist in node_lod.h / light.h):
//
//   gilde.exe 0x5ad438 — VIBE_Mesh_TransformBoundingVolume (node_lod family)
//   gilde.exe 0x5c8218 — VIBE_Light_BuildObjectCache       (light module)
//
// They are declared in render/mesh_transform.h and invoked from the transform/
// AABB paths. Defining them ONCE here keeps the `guild` static library self-
// contained so every test executable links (the unit TU extern-references
// g_buildCacheCalls; the e2e TU does too). When the owning agent translates the
// real bodies into node_lod / light, DELETE this file — the real definitions
// will take over and the duplicate-symbol error will flag any leftover.
#include "render/mesh_transform.h"

namespace guild::render {

int  g_buildCacheCalls = 0;  // test-observable call counter (placeholder)

// gilde.exe 0x5ad438 — VIBE_Mesh_TransformBoundingVolume (UNTRANSLATED placeholder).
int TransformBoundingVolume(void* /*obj*/, float* /*pivot*/, u8 /*flag*/) { return 0; }

// gilde.exe 0x5c8218 — VIBE_Light_BuildObjectCache (UNTRANSLATED placeholder).
void BuildObjectCache(void* /*obj*/) { ++g_buildCacheCalls; }

} // namespace guild::render
