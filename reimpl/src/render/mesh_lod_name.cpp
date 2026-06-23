#include "render/mesh_lod_name.h"

#include <cstdio>
#include <cstring>

// =============================================================================
// guild::render — stock-object LOD filename strategy + load-or-find orchestrator +
// object-node LOD attach. See mesh_lod_name.h for the original-function map.
//
// The string strategy (BuildLodFileName) and the path composition (BuildTexturePath)
// are reconstructed verbatim; the VFS-existence probe and the scene-graph draw-block
// writes are routed through MeshLodHooks (inert defaults below).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// LOD-mode byte (byte_64A098). A single process-global byte in the original.
// ---------------------------------------------------------------------------
u8& LodModeByteMut() {
    static u8 g = 0;  // mode 0, LOD disabled (high bit clear) by default
    return g;
}
u8 LodModeByte() { return LodModeByteMut(); }

// ---------------------------------------------------------------------------
// Hooks (inert defaults).
// ---------------------------------------------------------------------------
MeshLodHooks& MeshLodHooksMut() {
    static MeshLodHooks g;
    return g;
}
const MeshLodHooks& LodHooks() { return MeshLodHooksMut(); }

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1034 — VIBE_Mesh_BuildTexturePath.
// Original: strcpy(buf, "*"); strcat(buf, a1); strcat(buf, a2); then
//   VIBE_Vfs_ResolveAndBuildPath(buf, dword_1406110, &unk_1406114, dword_62EB78).
// The unk_628F14 constant is the single-char string "*". The resolve returns a
// truthy result only when the path resolves to an existing VFS entry.
// ---------------------------------------------------------------------------
bool BuildTexturePath(const char* name, const char* suffix, std::string* outPath) {
    // Compose "*" + name + suffix (the original's three strcat-chained copies).
    char buf[272];
    buf[0] = '*';
    buf[1] = '\0';
    if (name)
        std::strcat(buf, name);
    if (suffix)
        std::strcat(buf, suffix);

    if (outPath)
        *outPath = buf;

    // VIBE_Vfs_ResolveAndBuildPath: existence routed through the hook.
    if (LodHooks().textureExists)
        return LodHooks().textureExists(name ? name : "", suffix ? suffix : "");
    return false;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d15fc — VIBE_Mesh_BuildLodFileName (PURE string strategy).
// Constants: aS_8="_s", aSI_1="%s_%i", aBgf=".bgf".
// ---------------------------------------------------------------------------
u8 BuildLodFileName(const char* name, const char* secondName, char* out,
                    int lodIndex, char* outSecond) {
    const u8 mode = LodModeByte();

    if (lodIndex < 0) {
        // The "_s" suffix variant — only when LOD is ENABLED (high bit set, i.e.
        // (i8)byte_64A098 < 0). `byte_64A098 >= 0` (signed) -> disabled -> 0.
        if (static_cast<i8>(mode) >= 0)
            return 0;
        std::strcpy(out, name);
        std::strcat(out, "_s");
        if (secondName && outSecond) {
            std::strcpy(outSecond, secondName);
            std::strcat(outSecond, "_s");
        }
        return 1;
    }

    const u8 v6 = mode & 0x7F;

    if (lodIndex == 0) {
        if (v6 == 2) {
            // Switch-LOD base case: probe "%s_%i" downward (v7-1: 1, then 0) for an
            // existing .bgf; on the first hit return the matched name. If none of
            // the indexed names exist, fall back to the plain base name and check
            // THAT (return 1 if it exists, else 0 once v7 underflows).
            //
            // Faithful to the original's goto-LABEL_5 + while-condition shape: the
            // `sprintf` runs ONLY at LABEL_5; the existence test (the while head) is
            // re-evaluated on the current `out` after the plain-name copy WITHOUT a
            // re-sprintf.
            int v7 = 2;
            // LABEL_5 entry:
            std::sprintf(out, "%s_%i", name, v7 - 1);
            if (secondName && outSecond)
                std::sprintf(outSecond, "%s_%i", secondName, v7 - 1);
            while (!BuildTexturePath(out, ".bgf", nullptr)) {
                if (--v7 < 0)
                    return 0;
                if (v7 > 0) {
                    // re-enter LABEL_5 with the lower index
                    std::sprintf(out, "%s_%i", name, v7 - 1);
                    if (secondName && outSecond)
                        std::sprintf(outSecond, "%s_%i", secondName, v7 - 1);
                    continue;
                }
                // v7 == 0 : write the plain base name; the while head re-checks it.
                std::strcpy(out, name);
                if (secondName && outSecond)
                    std::strcpy(outSecond, secondName);
            }
            return 1;
        }
        // mode 0/1 base case: plain base name.
        std::strcpy(out, name);
        if (secondName && outSecond)
            std::strcpy(outSecond, secondName);
        return 1;
    }

    // lodIndex > 0 : explicit LOD frame. Switch-LOD flips the index (a4 = 2 - a4).
    int a4 = lodIndex;
    if (v6 == 2)
        a4 = 2 - a4;
    const int v42 = a4 - 1;
    std::sprintf(out, "%s_%i", name, v42);
    if (!secondName || !outSecond)
        return 1;
    std::sprintf(outSecond, "%s_%i", secondName, v42);
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1824 — VIBE_Mesh_AttachStockObjectLods.
//
// Object byte offsets (from the decompile):
//   object + 492        : draw-data block pointer (alloc via AllocDrawData if null)
//   drawData + 244      : base-LOD draw block         (offset 244 from drawData)
//   drawData + 1396     : the "_s" variant draw block (offset 1396)
//   drawData + 244 + 384*lod : LOD frame draw block   (384-byte LOD frame stride)
//   loop cap            : running 384*lod offset < 1152, lod < 3 (== 3 frames)
//
// The original looks the stock object up by name (VIBE_Mesh_FindStockObject) and
// attaches via VIBE_Mesh_AttachStockTextures(object, drawData+offset, lodArg). We
// route the alloc + attach through the hooks; the BuildLodFileName name strategy is
// reused verbatim so the SAME leaf names the original computes drive each attach.
// ---------------------------------------------------------------------------
u8 AttachStockObjectLods(void* object, bool attachExisting, const char* name,
                         int lodArg) {
    char buf[256];
    char* drawBlock = nullptr;  // models *(object+492); the hook owns the real block

    u8 result = 0;

    if (attachExisting) {
        // Fast path (original `if (a2)`): attach the directly-named stock object at
        // drawData+244. The original looks up by an already-prepared name; here the
        // caller-supplied `name` is that key.
        const MeshLodHooks& h = LodHooks();
        if (h.allocDrawData)
            h.allocDrawData(object);
        if (h.attachStockTextures)
            result = h.attachStockTextures(object, /*drawData+*/ 244, lodArg, name) ? 1 : 0;
        return result;
    }

    // Base LOD: BuildLodFileName(name, 0, buf, 0, 0) -> plain/probed base name.
    BuildLodFileName(name, nullptr, buf, 0, nullptr);
    const MeshLodHooks& h = LodHooks();
    if (h.allocDrawData)
        h.allocDrawData(object);
    if (h.attachStockTextures)
        result = h.attachStockTextures(object, 244, lodArg, buf) ? 1 : 0;

    // The "_s" variant -> drawData+1396 (only when BuildLodFileName produced it).
    if (BuildLodFileName(name, nullptr, buf, -1, nullptr)) {
        if (h.attachStockTextures)
            h.attachStockTextures(object, 1396, lodArg, buf);
    }

    // Multi-LOD mode: attach LOD frames 1..2 at drawData + 244 + 384*lod.
    result = LodModeByte() & 0x7F;
    if ((LodModeByte() & 0x7F) == 1) {
        int frameOff = 384;  // v8 (running 384*lod byte offset into the LOD frames)
        int lod = 1;         // v9
        do {
            result = BuildLodFileName(name, nullptr, buf, lod, nullptr);
            if (result) {
                // Stock object found+attached at drawData + 384*lod + 244.
                if (h.attachStockTextures &&
                    h.attachStockTextures(object, frameOff + 244, frameOff, buf)) {
                    frameOff += 384;
                    result = 1;
                }
            }
            ++lod;
        } while (lod < 3 && frameOff < 1152);
    }

    (void)drawBlock;
    return result;
}

} // namespace guild::render
