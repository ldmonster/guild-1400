#pragma once
#include "guild/common/types.h"

#include <array>
#include <cstddef>

// =============================================================================
// guild::drm — Disc SPTI (SCSI Pass-Through Interface) command builders
// =============================================================================
//
// WHAT THIS IS
//   The 0x140xxxx copy-protection cluster of gilde.exe fingerprints / verifies
//   the physical disc by issuing raw SCSI MMC commands to the optical drive
//   through Windows' SCSI Pass-Through Interface (SPTI). Each of these builders
//   fills a SCSI_PASS_THROUGH_DIRECT (SPTD) request — header + 6/10/12-byte CDB —
//   into one of two fixed global request buffers, then hands it to
//   DeviceIoControl(IOCTL_SCSI_PASS_THROUGH_DIRECT). This module reconstructs the
//   exact CDB byte layout and SPTD struct fill 1:1, and parses the SCSI reply
//   (the READ TOC bounds decode + the CHECK CONDITION sense capture).
//
// HARDWARE BOUNDARY
//   The DeviceIoControl syscall itself is the OS/hardware boundary (PLAN §4/§6).
//   It is routed through an inert-default DiscDevice hook so the pure
//   CDB-build + response-parse logic is reconstructed exactly and is testable:
//   a recording hook captures the bytes handed to the kernel and asserts them
//   byte-for-byte against the decompile, and synthetic SCSI replies drive the
//   parse paths. NEVER fakes the command-build logic.
//
//   The original calls, via the imported fn-ptr dword_145DB3C (DeviceIoControl):
//     dword_145DB3C(dword_1459FD0 /*hDevice*/, ioctl, &req, inSize,
//                   &req, outSize, &bytesReturned, 0);
//
//   Two SPTD request buffers exist in the original, at fixed globals:
//     * word_145B680 — the 6/12-byte-CDB request buffer (0x250 B),
//       submitted with IOCTL 315396 (0x4D004) by SptiSubmitRead (0x140e4f0).
//     * word_145EBC0 — the READ/READ-CD/READ-TOC request buffer (0x50 B),
//       submitted with IOCTL 315412 (0x4D014) by SptiSubmitSubchannel
//       (0x140e580).
//
//   The SPTD layout (32-bit, MSVC) is:
//     +0x00 USHORT Length; +0x02 UCHAR ScsiStatus; +0x03 UCHAR PathId;
//     +0x04 UCHAR TargetId; +0x05 UCHAR Lun; +0x06 UCHAR CdbLength;
//     +0x07 UCHAR SenseInfoLength; +0x08 UCHAR DataIn; +0x0c ULONG
//     DataTransferLength; +0x10 ULONG TimeOutValue; +0x14 PVOID DataBuffer;
//     +0x18 ULONG SenseInfoOffset; +0x1c UCHAR Cdb[16].
//   The original puts the sense buffer at +0x30 (SenseInfoOffset = 48); the
//   DataTransferLength field of the read buffer lives at +0x0c (dword_145B68C)
//   and of the subch buffer at +0x0c (dword_145EBCC).
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// SPTD request buffer (SCSI_PASS_THROUGH_DIRECT). Byte layout must match the
// original global request buffers (word_145B680 / word_145EBC0) exactly so the
// captured bytes can be asserted against the decompile.
//
// LP64 NOTE: the original DataBuffer field is a 32-bit pointer. We keep the
// struct byte layout faithful by storing a 32-bit cookie here (the original
// stored an int passed in from the caller — see ReadRawSector etc. which set
// dword_145EBD4 = a2). The actual buffer the device fills is delivered through
// the hook; we never cast a native pointer into this u32.
// -----------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct SptdRequest {
    u16 length;            // +0x00  always 44 (sizeof SPTD)
    u8  scsiStatus;        // +0x02  filled by device; 2 == CHECK CONDITION
    u8  pathId;            // +0x03
    u8  targetId;          // +0x04
    u8  lun;               // +0x05
    u8  cdbLength;         // +0x06  6 / 10 / 12
    u8  senseInfoLength;   // +0x07
    u8  dataIn;            // +0x08  0=OUT 1=IN 2=NONE
    u8  pad09;             // +0x09
    u8  pad0a;             // +0x0a
    u8  pad0b;             // +0x0b
    u32 dataTransferLength;// +0x0c
    u32 timeOutValue;      // +0x10
    u32 dataBuffer;        // +0x14  (32-bit pointer/cookie in original)
    u32 senseInfoOffset;   // +0x18  always 48 (sense at +0x30)
    u8  cdb[16];           // +0x1c  command descriptor block
    u8  pad2c;             // +0x2c  (Length=44 ends here; struct rounds to 48)
    u8  pad2d;             // +0x2d
    u8  pad2e;             // +0x2e
    u8  pad2f;             // +0x2f
    u8  sense[18];         // +0x30  sense buffer (SenseInfoOffset region)
    // The original read buffer (word_145B680) is memset for 0x250 bytes and the
    // subch buffer (word_145EBC0) for 0x50; both share this struct type, so the
    // backing storage spans 0x250 to keep those clears in-bounds (the original
    // globals reserve that much). Bytes past +0x42 are unused slack.
    u8  slack[0x250 - 0x42];  // +0x42 .. +0x250
} GUILD_PACKED;
GUILD_PACKED_END

static_assert(sizeof(SptdRequest) == 0x250, "SptdRequest must span the original buffer (0x250)");

static_assert(offsetof(SptdRequest, scsiStatus) == 0x02, "");
static_assert(offsetof(SptdRequest, cdbLength) == 0x06, "");
static_assert(offsetof(SptdRequest, dataIn) == 0x08, "");
static_assert(offsetof(SptdRequest, dataTransferLength) == 0x0c, "");
static_assert(offsetof(SptdRequest, timeOutValue) == 0x10, "");
static_assert(offsetof(SptdRequest, dataBuffer) == 0x14, "");
static_assert(offsetof(SptdRequest, senseInfoOffset) == 0x18, "");
static_assert(offsetof(SptdRequest, cdb) == 0x1c, "");
static_assert(offsetof(SptdRequest, sense) == 0x30, "");

// IOCTL codes used by the two submit routines (decompile constants 315396/315412).
constexpr u32 kIoctlScsiPassThroughDirectRead = 315396u;   // 0x4D004  SptiSubmitRead
constexpr u32 kIoctlScsiPassThroughDirectSubch = 315412u;  // 0x4D014  SptiSubmitSubchannel

// SCSI status the original tests for (SCSISTAT_CHECK_CONDITION).
constexpr u8 kScsiStatusCheckCondition = 2;

// -----------------------------------------------------------------------------
// DiscDevice — hardware-boundary hook for the DeviceIoControl(SPTI) syscall.
// Inert default: returns 0 ("call failed") and leaves the request untouched,
// matching a system with no protected disc / no drive. A recording subclass is
// used by the unit tests to capture and assert the submitted bytes, and to feed
// synthetic SCSI replies (set req.scsiStatus / req.sense / the bound buffer).
// -----------------------------------------------------------------------------
struct DiscDevice {
    virtual ~DiscDevice() = default;

    // Mirrors DeviceIoControl(hDevice, ioctl, &req, inSize, &req, outSize,
    // &bytesReturned, NULL). Returns the non-zero success value the caller
    // stores; the inert default returns 0.
    //   ioctl   — 315396 (read) or 315412 (subchannel)
    //   req     — the SPTD request buffer (filled by the builder)
    //   inSize  — input buffer size (592 for read buf, 44 for subch buf)
    //   outSize — output buffer size (req.dataTransferLength + 80)
    virtual int submit(u32 ioctl, SptdRequest& req, u32 inSize, u32 outSize) {
        (void)ioctl; (void)req; (void)inSize; (void)outSize;
        return 0;
    }
};

// Active device hook (process-global, mirroring the single shared request
// buffers in the original). Defaults to an inert DiscDevice. Tests install a
// recording hook here, then restore.
DiscDevice& GetDiscDevice();
void SetDiscDevice(DiscDevice* dev);  // nullptr restores the inert default

// Access to the two shared request buffers (the reconstructed equivalents of
// word_145B680 / word_145EBC0), so tests can inspect/seed them.
SptdRequest& ReadRequestBuffer();      // word_145B680
SptdRequest& SubchRequestBuffer();     // word_145EBC0

// 18-byte scratch the original copies sense into on CHECK CONDITION
// (unk_145F380). Exposed for test inspection.
std::array<u8, 18>& SenseScratch();    // unk_145F380

// dword_1464CDC — shared big-endian byte-swap scratch global used by the DRM
// cluster (VerifyDisc/Aspi/Spti TOC decode). Exposed so the SptiReadTocBounds
// side effect is faithful and inspectable.
u32& TocSwapScratch();                 // dword_1464CDC

// dword_145A014 — rolling index into the READ(10) LBA-bias key table.
u32& BiasIndex();                      // dword_145A014

// -----------------------------------------------------------------------------
// Submit routines (issue the request via the hook + parse CHECK CONDITION sense).
// -----------------------------------------------------------------------------

// gilde.exe 0x140e4f0 — VIBE_Disc_SptiSubmitRead
int SptiSubmitRead();

// gilde.exe 0x140e580 — VIBE_Disc_SptiSubmitSubchannel
int SptiSubmitSubchannel();

// -----------------------------------------------------------------------------
// Command builders.
// -----------------------------------------------------------------------------

// gilde.exe 0x140e870 — VIBE_Disc_SptiReadTocBounds  (__stdcall)
// READ TOC/PMA/ATIP (op 0x43, format 4 = full TOC) → *a2/*a3 get the two 32-bit
// big-endian descriptor fields at reply+0x07 and reply+0x0B, byte-swapped.
int SptiReadTocBounds(int a1, int* a2, int* a3, int a4);

// gilde.exe 0x1412560 — VIBE_Disc_SptiSetSpeed  (__stdcall)
int SptiSetSpeed(int a1, int a2);

// gilde.exe 0x14127a0 — VIBE_Disc_SptiSetReadSpeed  (__stdcall)
int SptiSetReadSpeed(int a1);

// gilde.exe 0x1412ca0 — VIBE_Disc_SptiReadRawSector  (__stdcall)
// READ CD (op 0xD8) raw, with sub-channel selection by a4 (0/1/2).
int SptiReadRawSector(int a1, int a2, int a3, int a4);

// gilde.exe 0x14131b0 — VIBE_Disc_SptiReadSectorEcc  (__stdcall)
// READ CD (op 0xBE) with C2/ECC flags, sub-channel by a4 (note 1/2 swap).
int SptiReadSectorEcc(int a1, int a2, int a3, int a4);

// gilde.exe 0x14135b0 — VIBE_Disc_SptiReadSector  (__stdcall)
// READ(10) (op 0x28); when a4==0 the LBA is biased by a key-table byte.
int SptiReadSector(int a1, int a2, int a3, int a4);

// gilde.exe 0x1413860 — VIBE_Disc_SptiTestUnitReady  (__stdcall)
int SptiTestUnitReady(int a1, int a2);

} // namespace guild::drm
