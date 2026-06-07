#pragma once
// gilde.exe — guild::io  (MODULE: VFS archive-mount / in-memory member index)
//
// When VIBE_Vfs_ScanDirectory @0x450234 meets a file whose extension is .BIN or
// .BIN0..BIN5 it mounts that PKZIP archive as a virtual subtree via
// VIBE_Vfs_EnumerateMatchingFiles @0x4500cc: it opens the archive (ZipArchive),
// walks the central directory member by member, and for each *non-directory*
// member (uncompressed size != 0) records the member under the VFS tree with the
// archive's cached central-dir position so a later open can jump straight to it
// (VIBE_Zip_SetCurrentFilePosition) without re-scanning.
//
// This module reconstructs that mount as a standalone, testable operation:
//   * ArchiveMount holds the opened ZipArchive plus a flat, path-keyed member
//     index (the data VIBE_Vfs_AddFileByPath stored on the tree leaves:
//     pos_in_central_dir, num_file, dosDate, binExtIdx).
//   * MountArchive(fs, path, idx) builds the index — a 1:1 translation of the
//     EnumerateMatchingFiles loop (name uppercase per case mode, '\\'->'/').
//   * OpenMember(name, out) / OpenMemberByCachedPos(...) extract a member's bytes,
//     reusing ZipArchive::ExtractCurrentFile (stored + deflated via InflateRaw).
//
// Real archive bytes flow through shim::IFileSystem (no OS calls in game code).
#include "io/zip_archive.h"
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <string>
#include <vector>

namespace guild::io {

// One indexed member of a mounted archive. Mirrors the per-leaf data the original
// stored on the VFS tree (the args to VIBE_Vfs_AddFileByPath): the cached central
// directory position so a later open can VIBE_Zip_SetCurrentFilePosition straight
// to it.
struct ArchiveMember {
    std::string name;             // member path, normalized (uppercased per case
                                  // mode, backslashes -> forward slashes)
    guild::u32  posInCentralDir;  // VIBE_Zip_GetCurrentFilePosition arg2 (unz_s[5])
    guild::u32  numFile;          // VIBE_Zip_GetCurrentFilePosition arg3 (unz_s[4])
    guild::u32  dosDate;          // cur_file_info.dosDate (v19[4] in the original)
    guild::u32  uncompressedSize; // cur_file_info.uncompressedSize
    guild::u32  compressionMethod; // 0=stored, 8=deflated
};

// A mounted PKZIP archive plus its in-memory member index.
class ArchiveMount {
public:
    ArchiveMount() = default;

    // VIBE_Vfs_EnumerateMatchingFiles @0x4500cc — open `path` through `fs` and
    // build the member index. `caseInsensitive` mirrors byte_62EB84 (when set,
    // names are NOT uppercased — kept as stored; lookups compare case-insensitively).
    // Returns true on success.
    bool Mount(guild::shim::IFileSystem* fs, const char* path, bool caseInsensitive);

    bool isMounted() const { return archive_.isOpen(); }
    std::size_t memberCount() const { return members_.size(); }
    const std::vector<ArchiveMember>& members() const { return members_; }

    // Find an indexed member by (normalized) path. Returns nullptr if absent.
    const ArchiveMember* Find(const char* name) const;

    // Extract a member's bytes by name. Uses the cached central-dir position
    // (VIBE_Zip_SetCurrentFilePosition) then VIBE_Zip_OpenCurrentFile/ReadCurrentFile.
    bool OpenMember(const char* name, std::vector<guild::u8>& out);

    // Extract a member by its cached central-dir position (the path the engine's
    // VIBE_Vfs_OpenFile takes when a tree leaf carries a cached offset).
    bool OpenMemberByCachedPos(guild::u32 posInCentralDir, guild::u32 numFile,
                               std::vector<guild::u8>& out);

    // The underlying archive (for advanced use / testing).
    ZipArchive& archive() { return archive_; }

private:
    ZipArchive archive_;
    std::vector<ArchiveMember> members_;
    bool caseInsensitive_ = false;
};

} // namespace guild::io
