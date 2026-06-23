#include "test.h"

// GUARDED real-asset e2e: drive a REAL Objects.BIN mesh through the reconstructed
// ENGINE FRAME SPINE (render::RenderMainViewFrame @0x5B6074 -> RenderUniverseFrame
// -> BeginUniverseFrame's scene-graph walk -> ProcessSceneNodeAppend -> RadixSort
// -> RasterizeMeshList) into a software Surface, and dump the result to a BMP.
//
// This proves the live frame orchestration renders ACTUAL game geometry (a real
// .bgf member decoded by play::RealMeshSource), not a synthetic quad — the
// "stand up the live loop" milestone. Clean skip when the real game dir is absent.
#include "play/universe_render.h"
#include "play/real_mesh_source.h"
#include "io/archive_mount.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool ObjectsBinPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/Objects.BIN");
}

bool TexturesBinPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/Textures.BIN");
}

// Dump a 16bpp(565) surface as a 24bpp BMP via the public per-pixel reader.
bool DumpBmp(const char* path, render::Surface* s, int w, int h) {
    if (!s || w <= 0 || h <= 0)
        return false;
    const int rowBytes = (w * 3 + 3) & ~3;
    const int imgBytes = rowBytes * h;
    const int fileBytes = 54 + imgBytes;
    std::vector<std::uint8_t> buf(fileBytes, 0);
    auto put16 = [&](int o, std::uint16_t v) { buf[o] = v & 0xFF; buf[o + 1] = v >> 8; };
    auto put32 = [&](int o, std::uint32_t v) {
        buf[o] = v & 0xFF; buf[o + 1] = (v >> 8) & 0xFF;
        buf[o + 2] = (v >> 16) & 0xFF; buf[o + 3] = (v >> 24) & 0xFF;
    };
    buf[0] = 'B'; buf[1] = 'M';
    put32(2, fileBytes); put32(10, 54);
    put32(14, 40); put32(18, w); put32(22, h);
    put16(26, 1); put16(28, 24); put32(34, imgBytes);
    for (int y = 0; y < h; ++y) {
        std::uint8_t* dst = buf.data() + 54 + (std::size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, h - 1 - y, px);   // BMP bottom-up
            dst[x * 3 + 0] = px[2]; dst[x * 3 + 1] = px[1]; dst[x * 3 + 2] = px[0];
        }
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

TEST(UniverseRenderE2E, RealMeshThroughEngineFrameSpine) {
    if (!ObjectsBinPresent()) {
        std::printf("  [skip] UniverseRenderE2E.RealMeshThroughEngineFrameSpine: "
                    "Objects.BIN absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());

    // Mount the real .bgf archive (the mesh decode source) + enumerate member names.
    play::RealMeshSource src;
    CHECK(src.MountArchive(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));
    io::ArchiveMount names;
    CHECK(names.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/true));
    CHECK(names.memberCount() > 0);

    // Drive the FIRST member that decodes to real multi-tri geometry (>4 verts)
    // through the engine frame spine.
    play::UniverseFrameDriver drv;
    // Mount the REAL Textures.BIN so the mesh binds its actual per-material BMP.
    const bool haveTextures = TexturesBinPresent() &&
                              drv.InitTextures(&fs, "Resources/Textures.BIN");
    std::printf("[univ-e2e] textures mounted=%d\n", (int)haveTextures);
    play::UniverseFrameDriver::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.clearR = 0; opt.clearG = 0; opt.clearB = 64;

    play::UniverseFrameDriver::Result best{};
    std::string bestName;
    int tried = 0;
    for (const auto& m : names.members()) {
        if (tried >= 64)            // bound the scan; a real mesh is hit early
            break;
        if (!drv.LoadMember(src, m.name.c_str()))
            continue;
        ++tried;
        play::UniverseFrameDriver::Result r = drv.RenderFrame(opt);
        // Keep the richest frame (most rasterized triangles) for the artifact.
        if (r.real && r.rasterTris > best.rasterTris) {
            best = r;
            bestName = m.name;
        }
        if (best.real && best.rasterTris > 0 && best.nonClearPixels > 50)
            break;                  // good enough — a real mesh rendered
    }

    std::printf("[univ-e2e] member=\"%s\" verts=%d polys=%d appended=%d "
                "dispatched=%d rasterTris=%d nonClearPx=%d textured=%d "
                "boundMats=%d texturedPolys=%d distinctColors=%d\n",
                bestName.c_str(), best.meshVerts, best.meshPolys, best.appendedPolys,
                best.nodesDispatched, best.rasterTris, best.nonClearPixels,
                (int)best.textured, best.boundMaterials, best.texturedPolys,
                best.distinctColors);

    // Re-render the richest member so the surface artifact matches `best`.
    if (best.real && !bestName.empty() && drv.LoadMember(src, bestName.c_str()))
        drv.RenderFrame(opt);

    // The engine frame spine projected, dispatched, and rasterized REAL geometry.
    CHECK(best.real);                         // a real (>4-vert) mesh was driven
    CHECK(best.meshPolys > 2);                // genuine multi-tri geometry
    CHECK(best.nodesDispatched >= 1);         // ProcessSceneNodeAppend ran
    CHECK(best.appendedPolys >= 1);           // the scene walk appended draw entries
    CHECK(best.rasterTris >= 1);              // RasterizeMeshList flushed triangles
    CHECK(best.nonClearPixels >= 1);          // visible pixels in the frame

    // When Textures.BIN is present the mesh rendered TEXTURED through the real
    // RasterizeTexturedTriangleRgbz leaf: materials bound + many distinct texel
    // colours (a flat/untextured frame would have very few).
    if (haveTextures) {
        CHECK(best.textured);                 // per-material textures bound
        CHECK(best.boundMaterials >= 1);
        CHECK(best.texturedPolys >= 1);       // polys rasterized via the textured leaf
        CHECK(best.distinctColors >= 8);      // real texture variety, not flat fill
    }

    bool dumped = DumpBmp("/tmp/guild_universe_frame.bmp", drv.surface(),
                          opt.fbW, opt.fbH);
    CHECK(dumped);
    if (dumped)
        std::printf("[univ-e2e] dumped %dx%d frame to "
                    "/tmp/guild_universe_frame.bmp\n", opt.fbW, opt.fbH);

    // PRESENT path (rule 3/4 swap): blit the rendered frame into a graphics device
    // and present() it. Headless here via MemoryGraphicsDevice; the IDENTICAL call
    // drives an on-screen SDL window with VulkanGraphicsDevice (apps/guild_run.cpp).
    shim::MemoryGraphicsDevice gdev;
    CHECK(gdev.init(opt.fbW, opt.fbH, 16, /*fullscreen=*/false));
    bool presented = drv.PresentToDevice(gdev);
    CHECK(presented);
    if (presented) {
        // The device backbuffer received the rendered (non-blank) frame.
        shim::Surface* bb = gdev.backbuffer();
        CHECK(bb != nullptr);
        int bbNonZero = 0;
        if (bb && bb->pixels) {
            const u16* p = reinterpret_cast<const u16*>(bb->pixels);
            const int pitchPx = bb->pitch / 2;
            for (int y = 0; y < bb->height; ++y)
                for (int x = 0; x < bb->width; ++x)
                    if (p[(std::size_t)y * pitchPx + x] != 0) ++bbNonZero;
        }
        CHECK(bbNonZero >= 1);
        std::printf("[univ-e2e] presented frame to device (%d non-zero bb px)\n",
                    bbNonZero);
    }

    // PERSPECTIVE path: render the same member through the GENUINE universe-object
    // projection leaf VIBE_Render_ProjectObjectVertices @0x5ac970 (1/z divide) instead
    // of the affine stand-in, and assert it still projects + rasterizes real geometry.
    if (best.real && !bestName.empty()) {
        play::UniverseFrameDriver::Options popt = opt;
        popt.perspective = true;
        play::UniverseFrameDriver pdrv;
        if (haveTextures) pdrv.InitTextures(&fs, "Resources/Textures.BIN");
        CHECK(pdrv.LoadMember(src, bestName.c_str()));
        play::UniverseFrameDriver::Result pr = pdrv.RenderFrame(popt);
        std::printf("[univ-e2e] PERSPECTIVE: appended=%d rasterTris=%d nonClearPx=%d "
                    "texturedPolys=%d distinctColors=%d\n",
                    pr.appendedPolys, pr.rasterTris, pr.nonClearPixels,
                    pr.texturedPolys, pr.distinctColors);
        CHECK(pr.appendedPolys >= 1);       // the perspective projection appended draw entries
        CHECK(pr.rasterTris >= 1);          // rasterized through the engine spine
        CHECK(pr.nonClearPixels >= 1);      // visible perspective-projected pixels
        DumpBmp("/tmp/guild_universe_persp.bmp", pdrv.surface(), opt.fbW, opt.fbH);
    }
}
