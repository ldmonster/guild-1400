#include "io/zip_archive.h"

#include "compress/inflate.h"
#include "compress/crc.h"

#include <cstring>

namespace guild::io {

using guild::compress::CrcCompute;
using guild::compress::InflateRaw;

namespace {

// VIBE_Util_StrCmpNoCaseInline @0x5ea788 — ASCII case-insensitive compare,
// returns 0 when equal (used by VIBE_Zip_CompareFileName when not case sensitive).
int StrCmpNoCase(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

// VIBE_Zip_CompareFileName @0x5ea7d4.
int CompareFileName(const char* inZip, const char* wanted, bool caseSensitive) {
    if (caseSensitive)
        return std::strcmp(wanted, inZip);
    return StrCmpNoCase(inZip, wanted);
}

} // namespace

// ---------------------------------------------------------------------------
// low-level byte readers over the in-memory archive (file_). These mirror
// VIBE_Zip_ReadByte/Short/Long, which read little-endian off the buffered FILE*.

bool ZipArchive::Seek(std::size_t absOff) {
    if (absOff > file_.size())
        return false;                       // VIBE_File_Seek failure
    cursor_ = absOff;
    return true;
}

// VIBE_Zip_ReadByte @0x5ea68c — read one byte; on EOF returns the byte 0 but the
// caller's loops treat a short read as an error via the FILE error flag. We model
// success/failure with the bool return and write 0 on failure (as the original
// wrote 0 to *a2 only via the Short/Long wrappers).
bool ZipArchive::ReadByte(guild::u32* out) {
    if (cursor_ >= file_.size()) {
        *out = 0;
        return false;
    }
    *out = file_[cursor_++];
    return true;
}

// VIBE_Zip_ReadShort @0x5ea6d0 — two bytes LE. *out = 0 on error.
bool ZipArchive::ReadShort(guild::u32* out) {
    guild::u32 b0 = 0, b1 = 0;
    bool ok = ReadByte(&b0);
    if (ok) ok = ReadByte(&b1);
    if (!ok) { *out = 0; return false; }
    *out = (b1 << 8) + b0;
    return true;
}

// VIBE_Zip_ReadLong @0x5ea718 — four bytes LE. *out = 0 on error.
bool ZipArchive::ReadLong(guild::u32* out) {
    guild::u32 b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    bool ok = ReadByte(&b0);
    if (ok) ok = ReadByte(&b1);
    if (ok) ok = ReadByte(&b2);
    if (ok) ok = ReadByte(&b3);
    if (!ok) { *out = 0; return false; }
    *out = (b3 << 24) + (b2 << 16) + (b1 << 8) + b0;
    return true;
}

// ---------------------------------------------------------------------------
// VIBE_Zip_FindEndOfCentralDir @0x5ea7ec — scan backwards from EOF for the
// signature "PK\5\6" (0x50 0x4B 0x05 0x06) inside a 0xFFFF-byte tail window.
guild::u32 ZipArchive::FindEndOfCentralDir() {
    constexpr guild::u32 kMaxBack = 0xFFFF;   // v8
    guild::u32 found = 0;                      // v9

    std::size_t fileSize = file_.size();       // VIBE_Memory_FreeBlock(a1)
    guild::u32 sizeFile = static_cast<guild::u32>(fileSize);
    guild::u32 maxBack = kMaxBack;
    if (sizeFile < kMaxBack)
        maxBack = sizeFile;

    // The original allocated a 0x404-byte scratch and read 1024+4 bytes per step,
    // scanning each window for the signature; we scan file_ directly with identical
    // window arithmetic so the *first* (lowest-offset within the highest matching
    // window) match the original would pick is reproduced.
    guild::u32 readPos = 4;                     // v3
    if (maxBack <= 4)
        return 0;
    do {
        readPos += 1024;
        if (readPos > maxBack)
            readPos = maxBack;
        guild::u32 readSize = readPos;          // v4
        if (readPos > 0x404)
            readSize = 1028;
        std::size_t base = sizeFile - readPos;   // VIBE_File_Seek(sizeFile - readPos)
        if (base + readSize > fileSize)
            break;
        // scan window [base, base+readSize) backwards for the 4-byte signature
        int i = static_cast<int>(readSize) - 3;  // v5
        const guild::u8* w = file_.data() + base;
        while (--i >= 0) {
            if (w[i] == 0x50 && w[i + 1] == 0x4B && w[i + 2] == 0x05 && w[i + 3] == 0x06) {
                found = static_cast<guild::u32>(base) + static_cast<guild::u32>(i);
                break;
            }
        }
    } while (!found && readPos < maxBack);

    return found;
}

// ---------------------------------------------------------------------------
// VIBE_Zip_DecodeDosDateTime @0x5eaaf0.
void ZipDecodeDosDateTime(guild::u32 ulDosDate, guild::u32* tmu) {
    guild::u32 uDate = ulDosDate >> 16;
    tmu[0] = 2 * (ulDosDate & 0x1F);                       // tm_sec
    tmu[1] = (ulDosDate & 0x7E0) >> 5;                     // tm_min
    tmu[2] = (ulDosDate & 0xF800) >> 11;                   // tm_hour
    tmu[3] = uDate & 0x1F;                                  // tm_mday
    tmu[4] = ((uDate & 0x1E0) >> 5) - 1;                   // tm_mon (0-based)
    tmu[5] = ((uDate & 0xFE00) >> 9) + 1980;              // tm_year
}

// ---------------------------------------------------------------------------
// VIBE_Zip_ReadCentralDirEntry @0x5eab48 — read the central dir file header at
// posInCentralDir_+byteBeforeZip_, fill curInfo_ + curOffsetLocalHeader_, and
// optionally copy the (truncated) filename. The extra/comment skip arithmetic of
// the original is preserved (we never request the extra/comment buffers here).
int ZipArchive::ReadCentralDirEntry(char* name, guild::u32 nameCap) {
    int err = kZipOk;                                       // v9
    ZipFileInfo info{};

    guild::u32 sig = 0;
    if (!Seek(posInCentralDir_ + byteBeforeZip_) || !ReadLong(&sig))
        err = kZipErrno;
    else if (sig != 0x02014b50)
        err = kZipBadZipFile;

    if (!ReadShort(&info.version))           err = kZipErrno;
    if (!ReadShort(&info.versionNeeded))     err = kZipErrno;
    if (!ReadShort(&info.flag))              err = kZipErrno;
    if (!ReadShort(&info.compressionMethod)) err = kZipErrno;
    if (!ReadLong(&info.dosDate))            err = kZipErrno;
    ZipDecodeDosDateTime(info.dosDate, &info.tmSec);
    if (!ReadLong(&info.crc))                err = kZipErrno;
    if (!ReadLong(&info.compressedSize))     err = kZipErrno;
    if (!ReadLong(&info.uncompressedSize))   err = kZipErrno;
    if (!ReadShort(&info.sizeFilename))      err = kZipErrno;
    if (!ReadShort(&info.sizeFileExtra))     err = kZipErrno;
    if (!ReadShort(&info.sizeFileComment))   err = kZipErrno;
    if (!ReadShort(&info.diskNumStart))      err = kZipErrno;
    if (!ReadShort(&info.internalFa))        err = kZipErrno;
    if (!ReadLong(&info.externalFa))         err = kZipErrno;
    guild::u32 offsetLocal = 0;
    if (!ReadLong(&offsetLocal))             err = kZipErrno;

    // filename copy (the original truncates to nameCap, NUL-terminating if there
    // is room, then accounts for the unread filename bytes in the skip).
    guild::u32 bytesToSkip = info.sizeFilename;             // v13
    if (err == kZipOk && name) {
        guild::u32 toCopy;
        if (nameCap <= info.sizeFilename) {
            toCopy = nameCap;
        } else {
            name[info.sizeFilename] = 0;
            toCopy = info.sizeFilename;
        }
        if (info.sizeFilename && nameCap) {
            if (cursor_ + toCopy > file_.size()) {
                err = kZipErrno;
            } else {
                std::memcpy(name, file_.data() + cursor_, toCopy);
                cursor_ += toCopy;
            }
        }
        bytesToSkip = info.sizeFilename - toCopy;
    }
    // (extra field and comment are skipped; we don't surface them — matches the
    //  null a6/a8 call sites used by the mount/locate paths.)

    if (err == kZipOk) {
        curInfo_ = info;
        curOffsetLocalHeader_ = offsetLocal;
    }
    (void)bytesToSkip;
    return err;
}

// ---------------------------------------------------------------------------
// VIBE_Zip_OpenArchive @0x5ea908.
bool ZipArchive::Open(guild::shim::IFileSystem* fs, const char* path) {
    Close();
    if (!fs || !path)
        return false;

    fs_ = fs;
    guild::shim::IFile* f = fs->open(path, "rb");   // VIBE_File_OpenStream(a1,"rb")
    if (!f)
        return false;
    std::int64_t sz = f->size();
    if (sz < 0) sz = 0;
    file_.resize(static_cast<std::size_t>(sz));
    std::size_t got = 0;
    if (sz > 0)
        got = f->read(file_.data(), static_cast<std::size_t>(sz));
    file_.resize(got);
    fs->close(f);
    cursor_ = 0;

    int err = kZipOk;                                // v2 (esi)
    guild::u32 eocd = FindEndOfCentralDir();          // v5 (edi)
    if (eocd == 0)
        err = kZipErrno;
    if (!Seek(eocd))
        err = kZipErrno;

    guild::u32 sig = 0, numberDisk = 0, diskWithCd = 0;
    guild::u32 numEntriesThisDisk = 0, numEntriesTotal = 0;
    guild::u32 sizeCentral = 0, offsetCentral = 0, commentLen = 0;
    if (!ReadLong(&sig))                err = kZipErrno;   // EOCD signature
    if (!ReadShort(&numberDisk))        err = kZipErrno;
    if (!ReadShort(&diskWithCd))        err = kZipErrno;
    if (!ReadShort(&numEntriesThisDisk)) err = kZipErrno;
    if (!ReadShort(&numEntriesTotal))   err = kZipErrno;
    if (numEntriesTotal != numEntriesThisDisk || diskWithCd || numberDisk)
        err = kZipBadZipFile;
    if (!ReadLong(&sizeCentral))        err = kZipErrno;
    if (!ReadLong(&offsetCentral))      err = kZipErrno;
    if (!ReadShort(&commentLen))        err = kZipErrno;

    // sanity: EOCD must lie at/after the central directory end. (`v5 < off+size`.)
    if ((eocd < offsetCentral + sizeCentral && err == kZipOk) || err != kZipOk) {
        Close();
        return false;
    }

    numberEntry_      = numEntriesTotal;       // unz_s[1]
    sizeComment_      = commentLen;            // unz_s[2]
    centralPos_       = eocd;                  // unz_s[7]
    sizeCentralDir_   = sizeCentral;           // unz_s[8]
    offsetCentralDir_ = offsetCentral;         // unz_s[9]
    // byte_before_the_zipfile = eocd - (offset + size)   (unz_s[3])
    byteBeforeZip_    = eocd - (offsetCentral + sizeCentral);
    open_ = true;

    GoToFirstFile();
    return true;
}

// VIBE_Zip_CloseArchive @0x5eaaa8.
void ZipArchive::Close() {
    file_.clear();
    cursor_ = 0;
    numberEntry_ = 0;
    sizeComment_ = 0;
    byteBeforeZip_ = 0;
    numFile_ = 0;
    posInCentralDir_ = 0;
    currentFileOk_ = false;
    centralPos_ = 0;
    sizeCentralDir_ = 0;
    offsetCentralDir_ = 0;
    curInfo_ = ZipFileInfo{};
    curOffsetLocalHeader_ = 0;
    open_ = false;
    fs_ = nullptr;
}

// VIBE_Zip_GoToFirstFile @0x5eaef0.
int ZipArchive::GoToFirstFile() {
    if (!open_)
        return kZipParamError;
    posInCentralDir_ = offsetCentralDir_;     // a1[5] = a1[9]
    numFile_ = 0;                              // a1[4] = 0
    int r = ReadCentralDirEntry(nullptr, 0);
    currentFileOk_ = (r == kZipOk);
    return r;
}

// VIBE_Zip_GoToNextFile @0x5eaf3c.
int ZipArchive::GoToNextFile() {
    if (!open_)
        return kZipParamError;
    if (!currentFileOk_ || numFile_ + 1 == numberEntry_)
        return kZipEndOfList;
    // pos += sizeComment + sizeExtra + sizeFilename + 46 (central header)
    posInCentralDir_ += curInfo_.sizeFileComment + curInfo_.sizeFileExtra +
                        curInfo_.sizeFilename + 46;
    ++numFile_;
    int r = ReadCentralDirEntry(nullptr, 0);
    currentFileOk_ = (r == kZipOk);
    return r;
}

// VIBE_Zip_GetCurrentFileInfo @0x5eaec8.
int ZipArchive::GetCurrentFileInfo(ZipFileInfo* info, char* name, guild::u32 nameCap) {
    if (!open_)
        return kZipParamError;
    int r = ReadCentralDirEntry(name, nameCap);
    if (r == kZipOk && info)
        *info = curInfo_;
    return r;
}

// VIBE_Zip_LocateFileByName @0x5eafbc.
int ZipArchive::LocateFileByName(const char* wanted, bool caseSensitive) {
    if (!open_ || !wanted || std::strlen(wanted) >= 0x100)
        return kZipParamError;
    if (!currentFileOk_)
        return kZipEndOfList;

    guild::u32 savedNum = numFile_;            // v7
    guild::u32 savedPos = posInCentralDir_;    // v8

    int r = GoToFirstFile();
    if (r == kZipOk) {
        for (;;) {
            char nameBuf[256];
            GetCurrentFileInfo(nullptr, nameBuf, sizeof(nameBuf));
            if (CompareFileName(nameBuf, wanted, caseSensitive) == 0)
                return kZipOk;
            r = GoToNextFile();
            if (r != kZipOk)
                break;
        }
    }
    // not found: restore previous position
    numFile_ = savedNum;
    posInCentralDir_ = savedPos;
    return r;
}

// VIBE_Zip_GetCurrentFilePosition @0x5eb7b8.
int ZipArchive::GetCurrentFilePosition(guild::u32* posInCentralDir, guild::u32* numFile) {
    *posInCentralDir = 0;
    *numFile = 0;
    if (!open_ || !currentFileOk_)
        return kZipParamError;
    *posInCentralDir = posInCentralDir_;
    *numFile = numFile_;
    return kZipOk;
}

// VIBE_Zip_SetCurrentFilePosition @0x5eb7e8.
int ZipArchive::SetCurrentFilePosition(guild::u32 posInCentralDir, guild::u32 numFile) {
    if (!open_ || posInCentralDir == 0)
        return kZipParamError;
    numFile_ = numFile;
    posInCentralDir_ = posInCentralDir;
    int r = ReadCentralDirEntry(nullptr, 0);
    currentFileOk_ = (r == kZipOk);
    return r;
}

// ---------------------------------------------------------------------------
// VIBE_Zip_ReadLocalFileHeader @0x5eb090 — read the local header for the current
// member to learn how many (filename + extra) bytes follow it, so the compressed
// data offset can be computed. We only need the total skip (`*sizeVar`).
int ZipArchive::ReadLocalFileHeader(guild::u32* sizeVar) {
    *sizeVar = 0;
    int err = kZipOk;

    std::size_t localPos = byteBeforeZip_ + curOffsetLocalHeader_;
    if (!Seek(localPos))
        return kZipErrno;

    guild::u32 sig = 0, tmp = 0, sizeFilename = 0, sizeExtra = 0;
    ReadLong(&sig);                    // local header signature (0x04034b50)
    ReadShort(&tmp);                   // version needed
    ReadShort(&tmp);                   // flag
    ReadShort(&tmp);                   // method
    ReadLong(&tmp);                    // dos date/time
    ReadLong(&tmp);                    // crc
    ReadLong(&tmp);                    // compressed size
    ReadLong(&tmp);                    // uncompressed size
    ReadShort(&sizeFilename);          // filename length
    if (!ReadShort(&sizeExtra))        // extra length
        err = kZipErrno;
    *sizeVar = sizeFilename + sizeExtra;
    return err;
}

// ---------------------------------------------------------------------------
// VIBE_Zip_OpenCurrentFile @0x5eb2d0 + ReadCurrentFile @0x5eb418 +
// CloseCurrentFile @0x5eb6c0 — extract the whole current member.
//
// Faithful behaviour: compute the compressed-data offset from the local header
// (pos_in_zipfile = byteBeforeZip + offset_local + 30 + sizeVar), then either copy
// (method 0) or inflate (method 8) `rest_read_compressed` bytes, folding the
// running CRC-32 over the decompressed output and comparing it to the central dir
// crc. (The original streamed in 0x4000-byte buffer chunks; reading the member in
// one shot is behaviour-identical for full extraction.)
bool ZipArchive::ExtractCurrentFile(std::vector<guild::u8>& out) {
    out.clear();
    if (!open_ || !currentFileOk_)
        return false;

    guild::u32 sizeVar = 0;                                  // iSizeVar
    if (ReadLocalFileHeader(&sizeVar) != kZipOk)
        return false;

    // pos_in_zipfile (file_in_zip_read_info_s +60)
    std::size_t dataOff = static_cast<std::size_t>(byteBeforeZip_) +
                          curOffsetLocalHeader_ + 30 + sizeVar;
    guild::u32 compSize = curInfo_.compressedSize;           // rest_read_compressed
    guild::u32 method   = curInfo_.compressionMethod;

    if (dataOff + compSize > file_.size())
        return false;
    const guild::u8* payload = file_.data() + dataOff;

    if (method == 0) {                                       // stored
        out.assign(payload, payload + compSize);
    } else if (method == 8) {                                // deflated
        out.reserve(curInfo_.uncompressedSize);
        // The original's VIBE_Zip_ReadCurrentFile loops VIBE_Inflate_Process until
        // rest_read_uncompressed reaches 0, then validates the CRC; it does NOT
        // gate success on Z_STREAM_END. InflateRaw here returns false when the raw
        // stream's FSM stops at Z_OK rather than Z_STREAM_END even though the full
        // payload was produced — so accept the output if it reached the expected
        // uncompressed length (the CRC check below is the real integrity gate).
        InflateRaw(payload, compSize, out);
        if (out.size() != curInfo_.uncompressedSize)
            return false;
    } else {
        return false;                                        // unsupported method
    }

    // CRC-32 verification (VIBE_Crc_Compute folds each decoded chunk; init 0).
    guild::u32 crc = CrcCompute(0, out.data(), static_cast<guild::u32>(out.size()));
    if (crc != curInfo_.crc)
        return false;
    return true;
}

bool ZipArchive::ExtractByName(const char* name, std::vector<guild::u8>& out,
                               bool caseSensitive) {
    if (LocateFileByName(name, caseSensitive) != kZipOk)
        return false;
    return ExtractCurrentFile(out);
}

} // namespace guild::io
