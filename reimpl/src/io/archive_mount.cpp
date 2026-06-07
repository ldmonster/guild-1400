#include "io/archive_mount.h"

#include <cstring>

namespace guild::io {

namespace {

// VIBE_Util_StrToUpper @0x5e9f50 — in-place ASCII upper-case (used by the scanner
// when byte_62EB84 == 0, i.e. case-folding is requested).
void StrToUpper(std::string& s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
}

// VIBE_Path_ConvertBackslashToSlash @0x44eb54 — '\\' -> '/' in place.
void ConvertBackslashToSlash(std::string& s) {
    for (char& c : s)
        if (c == '\\')
            c = '/';
}

int CompareNormalized(const std::string& a, const char* b, bool caseInsensitive) {
    // `a` is already normalized (slashes converted, optionally uppercased). Apply
    // the same normalization to `b` for the comparison.
    std::string bn(b);
    ConvertBackslashToSlash(bn);
    if (caseInsensitive) {
        // case-folded compare both sides
        std::size_t n = a.size() < bn.size() ? a.size() : bn.size();
        for (std::size_t i = 0; i < n; ++i) {
            char ca = a[i], cb = bn[i];
            if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 32);
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 32);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        if (a.size() != bn.size()) return a.size() < bn.size() ? -1 : 1;
        return 0;
    }
    StrToUpper(bn);   // names were uppercased at mount time
    return a.compare(bn);
}

} // namespace

// ---------------------------------------------------------------------------
// VIBE_Vfs_EnumerateMatchingFiles @0x4500cc — open the archive and index every
// member. The original additionally splices each member under a VFS tree dir
// (VIBE_Vfs_GetOrCreateSubDir + VIBE_Vfs_AddFileByPath); here we record the same
// per-member data into a flat path-keyed index (the data those tree leaves hold).
bool ArchiveMount::Mount(guild::shim::IFileSystem* fs, const char* path,
                         bool caseInsensitive) {
    members_.clear();
    caseInsensitive_ = caseInsensitive;

    if (!archive_.Open(fs, path))               // VIBE_Zip_OpenArchive
        return false;

    // Iterate the central directory member by member.
    do {
        char nameBuf[256];
        ZipFileInfo info{};
        if (archive_.GetCurrentFileInfo(&info, nameBuf, sizeof(nameBuf)) == kZipOk) {
            // Skip directory entries: the original skips members whose
            // uncompressed size (v19[7]) is zero.
            if (info.uncompressedSize != 0) {
                std::string name(nameBuf);
                if (!caseInsensitive)
                    StrToUpper(name);            // VIBE_Util_StrToUpper
                ConvertBackslashToSlash(name);   // VIBE_Path_ConvertBackslashToSlash

                guild::u32 pos = 0, num = 0;
                archive_.GetCurrentFilePosition(&pos, &num);  // cached offset

                ArchiveMember m;
                m.name = std::move(name);
                m.posInCentralDir = pos;
                m.numFile = num;
                m.dosDate = info.dosDate;        // v19[4]
                m.uncompressedSize = info.uncompressedSize;
                m.compressionMethod = info.compressionMethod;
                members_.push_back(std::move(m));
            }
        }
    } while (archive_.GoToNextFile() == kZipOk);

    return true;
}

const ArchiveMember* ArchiveMount::Find(const char* name) const {
    if (!name)
        return nullptr;
    for (const ArchiveMember& m : members_) {
        if (CompareNormalized(m.name, name, caseInsensitive_) == 0)
            return &m;
    }
    return nullptr;
}

bool ArchiveMount::OpenMember(const char* name, std::vector<guild::u8>& out) {
    const ArchiveMember* m = Find(name);
    if (!m)
        return false;
    return OpenMemberByCachedPos(m->posInCentralDir, m->numFile, out);
}

bool ArchiveMount::OpenMemberByCachedPos(guild::u32 posInCentralDir, guild::u32 numFile,
                                         std::vector<guild::u8>& out) {
    // VIBE_Zip_SetCurrentFilePosition jumps straight to the cached central-dir
    // entry, then VIBE_Zip_OpenCurrentFile/ReadCurrentFile extract the bytes.
    if (archive_.SetCurrentFilePosition(posInCentralDir, numFile) != kZipOk)
        return false;
    return archive_.ExtractCurrentFile(out);
}

} // namespace guild::io
