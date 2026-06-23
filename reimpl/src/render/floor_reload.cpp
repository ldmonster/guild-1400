#include "render/floor_reload.h"

#include "render/floorgfx_recon.h"   // kFloorMipSuffix / FloorTextureMipName (reuse, no ODR)

#include <string>

namespace guild::render {

namespace {
BmpLoadFn       g_load  = nullptr;
BuildBmpPathFn  g_build = nullptr;

bool DefaultLoad(const char* /*path*/, u8* /*dest*/, int /*flags*/) {
    return false;   // headless: no decode backend; iteration still observable.
}
std::string DefaultBuildPath(const std::string& name) {
    // VIBE_Texture_BuildBmpPath builds "*" + name + ".BMP" then VFS-resolves.
    // Headless default: hand back the un-resolved candidate so name-build is
    // testable; a real backend replaces this with the VFS resolver.
    return "*" + name + ".BMP";
}
} // namespace

void SetFloorBmpLoader(BmpLoadFn load)        { g_load = load; }
void SetFloorBmpPathBuilder(BuildBmpPathFn b) { g_build = b; }

// gilde.exe 0x5bd2d8 — VIBE_Floor_ReloadTextures.
int ReloadTextures(void* floor, const FloorTileAccess& acc) {
    if (!floor)                                   // if (!result) return result;
        return 0;

    if (acc.invalidate)
        acc.invalidate(floor);                    // VIBE_Floor_InvalidateTiles(floor)

    BmpLoadFn      load  = g_load  ? g_load  : &DefaultLoad;
    BuildBmpPathFn build = g_build ? g_build : &DefaultBuildPath;

    int attempted = 0;
    for (int slot = 0; slot < 8; ++slot) {        // v19 0..7 (+64 name row / +4 tile)
        const char* tmpl = acc.slotTemplateName
                               ? acc.slotTemplateName(floor, slot)
                               : nullptr;
        const std::string templateName = tmpl ? std::string(tmpl) : std::string();

        // 3 mip layers per slot: (end - tile)/32 == 96/32 == 3. The suffix table
        // advances 11 bytes per layer (kFloorMipSuffix "", "_high_1", "_high_2").
        for (int mip = 0; mip < 3; ++mip) {       // v1 != v21 (32-byte stride)
            // name = template + suffix  (FloorTextureMipName == the binary's
            // two interleaved-copy concatenations into the scratch buffer).
            const std::string name = FloorTextureMipName(templateName, mip);

            // path = Texture_BuildBmpPath(name); if (!path) skip.
            const std::string path = build(name);
            if (path.empty())
                continue;

            // if (*(v1+48)) Bmp_LoadBuffer(path, *(v1+48), 17, 0, ...)
            u8* dest = acc.tileSurfaceBuffer
                           ? acc.tileSurfaceBuffer(floor, slot, mip)
                           : nullptr;
            if (!dest)
                continue;

            load(path.c_str(), dest, 17);          // 0x11 == load-pixels | preserve-alpha
            ++attempted;
        }
    }
    return attempted;
}

} // namespace guild::render
