// gilde.exe — guild::io  (MODULE: savegames — GameState save/load orchestration)
//
// Faithful orchestration of the top-level save writer/loader spine around the
// reconstructed header, scalar block, and pointer<->id relink table. I/O goes
// through the VFS stream layer (io/vfs); the table phase is out of scope (see
// gamestate.h). See save.{h,cpp} for the byte-exact header/scalar/relink pieces.
#include "io/gamestate.h"

namespace guild::io {

// gilde.exe 0x5a348c — VIBE_Save_WriteGameFile (header + scalar + relink phase).
bool WriteGameState(const char* path, GameState& state,
                    const RelinkResolvers* save, bool partial) {
    if (!path)
        return false;

    // VIBE_Vfs_OpenFile(path, "wb", g_vfsRoot).
    VfsHandle* h = VfsOpenFile(path, "wb");
    if (!h)
        return false;

    // Pre-save: convert live pointers to ids in the relink table
    // (VIBE_Save_RelinkPersonObjects @0x5a3d8c — the slot-fixup + table pass).
    if (save && !state.relink.empty())
        RelinkTableIdsToPointers(state.relink.data(), *save);

    const guild::u8* thumb =
        state.thumbnail.size() == kThumbnailBytes ? state.thumbnail.data() : nullptr;

    bool ok = SaveWriteScenarioBlock(h, state.header, thumb)  // header + thumbnail
           && SaveWriteScalarBlock(h, state.scalar);          // fixed field block
    // (partial only gates the table phase in the original, which is out of scope.)
    (void)partial;

    VfsCloseStream(h);

    // Post-save: restore id->pointer (VIBE_Save_RelinkPersonRecords @0x5a3e4c).
    if (save && !state.relink.empty())
        RelinkTableRestore(state.relink.data(), *save);

    return ok;
}

// gilde.exe 0x5a7604 — VIBE_Save_LoadGameFile (header + scalar + relink phase).
bool LoadGameState(const char* path, GameState& state, const RelinkResolvers* load) {
    if (!path)
        return false;

    // VIBE_Vfs_OpenFile(path, "rb", g_vfsRoot).
    VfsHandle* h = VfsOpenFile(path, "rb");
    if (!h)
        return false;

    // (The original resets the world here: Building_ResetAllBuildings,
    //  CharAction_QueueFreeAll, Object_DestroySpawnedEntities — owned by other
    //  modules, out of scope for this slice.)

    guild::u8* thumbDst = nullptr;
    if (!state.thumbnail.empty()) {
        state.thumbnail.assign(kThumbnailBytes, 0);
        thumbDst = state.thumbnail.data();
    }

    bool ok = SaveLoadHeaderAndThumbnail(h, state.header, thumbDst);
    if (ok) {
        // Validate 0x10026 <= version <= 0x10045 (VIBE_Save_LoadGameFile @0x5a775a).
        guild::u32 v = SaveVersionGet();
        if (v > kSaveVersionLoadMax || v < kSaveVersionLoadMin)
            ok = false;
    }
    if (ok)
        ok = SaveLoadScalarBlock(h, state.scalar);

    VfsCloseStream(h);

    // Post-load: convert ids back to pointers, tag-dispatched
    // (VIBE_Save_RelinkLoadedPointers @0x5abb84 tail).
    if (ok && load && !state.relink.empty())
        RelinkTableIdsToPointers(state.relink.data(), *load);

    return ok;
}

} // namespace guild::io
