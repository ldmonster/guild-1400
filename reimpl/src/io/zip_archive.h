#pragma once
// gilde.exe — guild::io  (MODULE: PKZIP archive reader for .BIN/.BIN0-5 archives)
//
// A 1:1 reconstruction of the engine's vendored *minizip* reader (the VIBE_Zip_*
// cluster). The game's "archives" are plain PKZIP files (extensions .BIN and
// .BIN0..BIN5); this module parses the End-Of-Central-Directory record, walks the
// central directory, locates members by name, and extracts a member (stored or
// deflated) into memory. Deflate payloads are handed to compress::InflateRaw.
//
// Real archive bytes come through shim::IFileSystem; the original read through the
// buffered VIBE_File_* layer over a FILE*.
//
// Reconstructed entry points (gilde.exe addresses):
//   VIBE_Zip_ReadByte               @0x5ea68c
//   VIBE_Zip_ReadShort              @0x5ea6d0
//   VIBE_Zip_ReadLong               @0x5ea718
//   VIBE_Zip_CompareFileName        @0x5ea7d4
//   VIBE_Zip_FindEndOfCentralDir    @0x5ea7ec
//   VIBE_Zip_OpenArchive            @0x5ea908
//   VIBE_Zip_CloseArchive           @0x5eaaa8
//   VIBE_Zip_DecodeDosDateTime      @0x5eaaf0
//   VIBE_Zip_ReadCentralDirEntry    @0x5eab48
//   VIBE_Zip_GetCurrentFileInfo     @0x5eaec8
//   VIBE_Zip_GoToFirstFile          @0x5eaef0
//   VIBE_Zip_GoToNextFile           @0x5eaf3c
//   VIBE_Zip_LocateFileByName       @0x5eafbc
//   VIBE_Zip_ReadLocalFileHeader    @0x5eb090
//   VIBE_Zip_OpenCurrentFile        @0x5eb2d0
//   VIBE_Zip_ReadCurrentFile        @0x5eb418
//   VIBE_Zip_CloseCurrentFile       @0x5eb6c0
//   VIBE_Zip_GetCurrentFilePosition @0x5eb7b8
//   VIBE_Zip_SetCurrentFilePosition @0x5eb7e8
//
// The original unz_s state object is 0x80 bytes (byte_5EA660 == 0x20 dwords); the
// per-member info record (unz_file_info) is 0x50 bytes. Both are reproduced here
// field-for-field (offset comments) for fidelity, though the live host pointers
// (shim::IFile, byte buffer) are wider than the original 32-bit slots.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>
#include <string>
#include <vector>

namespace guild::io {

// minizip return codes (UNZ_*). Values match the originals seen at the call sites
// (e.g. -100 END_OF_LIST_OF_FILE, -102 PARAMERROR, -103 BADZIPFILE).
enum ZipReturn : int {
    kZipOk              = 0,
    kZipEndOfList       = -100,   // UNZ_END_OF_LIST_OF_FILE
    kZipParamError      = -102,   // UNZ_PARAMERROR
    kZipBadZipFile      = -103,   // UNZ_BADZIPFILE
    kZipInternalError   = -104,   // UNZ_INTERNALERROR
    kZipErrno           = -1,     // UNZ_ERRNO
};

// PKZIP record signatures.
constexpr guild::u32 kZipLocalSig   = 0x04034b50;  // "PK\3\4" local file header
constexpr guild::u32 kZipCentralSig = 0x02014b50;  // "PK\1\2" central dir header
constexpr guild::u32 kZipEocdSig    = 0x06054b50;  // "PK\5\6" end of central dir

// ---------------------------------------------------------------------------
// unz_file_info — the 0x50-byte per-member record qmemcpy'd by
// VIBE_Zip_ReadCentralDirEntry. Field indices recovered from that function (the
// v15[] array) and from the mount/open call sites. Every field is a 32-bit slot
// in the original (the dosDate-decoded tmu_date members included).
struct ZipFileInfo {
    guild::u32 version;            // +0x00  (#0) version made by
    guild::u32 versionNeeded;      // +0x04  (#1) version needed to extract
    guild::u32 flag;               // +0x08  (#2) general purpose bit flag
    guild::u32 compressionMethod;  // +0x0C  (#3) 0=stored, 8=deflated
    guild::u32 dosDate;            // +0x10  (#4) DOS-packed date/time
    guild::u32 crc;                // +0x14  (#5) crc-32
    guild::u32 compressedSize;     // +0x18  (#6) compressed size
    guild::u32 uncompressedSize;   // +0x1C  (#7) uncompressed size
    guild::u32 sizeFilename;       // +0x20  (#8) filename length
    guild::u32 sizeFileExtra;      // +0x24  (#9) extra field length
    guild::u32 sizeFileComment;    // +0x28  (#10) comment length
    guild::u32 diskNumStart;       // +0x2C  (#11) disk number start
    guild::u32 internalFa;         // +0x30  (#12) internal file attributes
    guild::u32 externalFa;         // +0x34  (#13) external file attributes
    // tmu_date written by VIBE_Zip_DecodeDosDateTime (a2[0..5]):
    guild::u32 tmSec;              // +0x38  (#14) seconds (2 * (dosDate & 0x1F))
    guild::u32 tmMin;              // +0x3C  (#15) minutes
    guild::u32 tmHour;            // +0x40  (#16) hours
    guild::u32 tmMday;            // +0x44  (#17) day of month
    guild::u32 tmMon;            // +0x48  (#18) month (0-based: nibble-1)
    guild::u32 tmYear;            // +0x4C  (#19) year (1980 + bits)
};
static_assert(sizeof(ZipFileInfo) == 0x50, "unz_file_info must be 0x50 bytes");

// VIBE_Zip_DecodeDosDateTime @0x5eaaf0 — decode a DOS-packed date/time into the
// six tmu_date dwords starting at &info.tmSec. (Exposed for testing.)
void ZipDecodeDosDateTime(guild::u32 dosDate, guild::u32* tmuOut6);

// ---------------------------------------------------------------------------
// ZipArchive — the live unz_s state. The original is a 0x80-byte heap block; we
// keep the *semantic* fields (with their original dword index in comments) plus
// the host backing store. One member is "current" at a time; the central
// directory is walked with GoToFirstFile / GoToNextFile, or a saved position is
// restored with SetCurrentFilePosition.
class ZipArchive {
public:
    ZipArchive() = default;

    // VIBE_Zip_OpenArchive @0x5ea908 — open the archive `path` through `fs`,
    // validate the EOCD record, and position on the first member. Returns true on
    // success. (The original returned the unz_s* or null.)
    bool Open(guild::shim::IFileSystem* fs, const char* path);

    // VIBE_Zip_CloseArchive @0x5eaaa8 — release the current file + backing store.
    void Close();

    bool isOpen() const { return open_; }

    // unz_global_info: total number of members (gi.number_entry, unz_s[1]).
    guild::u32 numberEntry() const { return numberEntry_; }

    // current_file_ok (unz_s[6]) — a member is positioned and valid.
    bool currentFileOk() const { return currentFileOk_; }

    // VIBE_Zip_GoToFirstFile @0x5eaef0 — position on the first member.
    int GoToFirstFile();
    // VIBE_Zip_GoToNextFile @0x5eaf3c — advance to the next member.
    int GoToNextFile();

    // VIBE_Zip_GetCurrentFileInfo @0x5eaec8 — read the current member's info into
    // `*info` (may be null) and its name into `name` (truncated to nameCap-1, NUL
    // terminated; may be null). Returns kZipOk or an error code.
    int GetCurrentFileInfo(ZipFileInfo* info, char* name, guild::u32 nameCap);

    // VIBE_Zip_LocateFileByName @0x5eafbc — find a member by name. `caseSensitive`
    // selects strcmp (1) vs case-insensitive compare (0). Returns kZipOk if found
    // (leaving it current), kZipEndOfList if not (restoring the prior position).
    int LocateFileByName(const char* name, bool caseSensitive);

    // VIBE_Zip_GetCurrentFilePosition @0x5eb7b8 — cache the current member's
    // (pos_in_central_dir, num_file) for a later SetCurrentFilePosition.
    int GetCurrentFilePosition(guild::u32* posInCentralDir, guild::u32* numFile);
    // VIBE_Zip_SetCurrentFilePosition @0x5eb7e8 — restore a cached position and
    // re-read its central dir entry. `posInCentralDir` must be non-zero.
    int SetCurrentFilePosition(guild::u32 posInCentralDir, guild::u32 numFile);

    // VIBE_Zip_OpenCurrentFile @0x5eb2d0 + ReadCurrentFile @0x5eb418 +
    // CloseCurrentFile — extract the whole current member into `out`. Faithful to
    // the original: stored members are copied; deflated members are inflated via
    // InflateRaw; the running CRC-32 is verified against the central dir crc.
    // Returns true on success.
    bool ExtractCurrentFile(std::vector<guild::u8>& out);

    // Convenience: locate `name` then extract it.
    bool ExtractByName(const char* name, std::vector<guild::u8>& out,
                       bool caseSensitive = false);

private:
    // --- unz_s fields (original dword index in comments) --------------------
    guild::shim::IFileSystem* fs_ = nullptr;
    std::vector<guild::u8> file_;          // [0]  backing store (whole archive)
    std::size_t            cursor_ = 0;     //      read cursor into file_

    guild::u32 numberEntry_  = 0;          // [1]  gi.number_entry
    guild::u32 sizeComment_  = 0;          // [2]  gi.size_comment
    guild::u32 byteBeforeZip_ = 0;          // [3]  byte_before_the_zipfile
    guild::u32 numFile_      = 0;          // [4]  num_file (current index)
    guild::u32 posInCentralDir_ = 0;       // [5]  pos_in_central_dir
    bool       currentFileOk_ = false;      // [6]  current_file_ok
    guild::u32 centralPos_    = 0;          // [7]  central_pos (EOCD abs offset)
    guild::u32 sizeCentralDir_ = 0;         // [8]  size_central_dir
    guild::u32 offsetCentralDir_ = 0;       // [9]  offset_central_dir
    ZipFileInfo curInfo_{};                 // [10..] cur_file_info (0x50 bytes)
    guild::u32 curOffsetLocalHeader_ = 0;   // [30] cur_file_info_internal.offset
    bool       open_ = false;

    // --- low-level readers over file_ (VIBE_Zip_Read{Byte,Short,Long}) -------
    bool Seek(std::size_t absOff);          // VIBE_File_Seek(SEEK_SET)
    bool ReadByte(guild::u32* out);         // VIBE_Zip_ReadByte
    bool ReadShort(guild::u32* out);        // VIBE_Zip_ReadShort
    bool ReadLong(guild::u32* out);         // VIBE_Zip_ReadLong

    // VIBE_Zip_FindEndOfCentralDir @0x5ea7ec — scan backwards for "PK\5\6".
    // Returns the absolute EOCD offset, or 0 if not found.
    guild::u32 FindEndOfCentralDir();

    // VIBE_Zip_ReadCentralDirEntry @0x5eab48 — read the central dir header at
    // posInCentralDir_+byteBeforeZip_ into curInfo_ + curOffsetLocalHeader_, and
    // optionally copy the filename into `name`. Returns kZipOk / error.
    int ReadCentralDirEntry(char* name, guild::u32 nameCap);

    // VIBE_Zip_ReadLocalFileHeader @0x5eb090 — read the local header for the
    // current member; out: size of (filename+extra) to skip and local extra info.
    int ReadLocalFileHeader(guild::u32* sizeVar);
};

} // namespace guild::io
