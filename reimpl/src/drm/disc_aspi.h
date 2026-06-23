#pragma once
#include <cstdint>

#include "guild/common/types.h"

// =============================================================================
// guild::drm — Disc ASPI (wnaspi32) SCSI command builders + response parsers
// =============================================================================
//
// WHAT THIS IS
//   gilde.exe's copy-protection cluster talks to the optical drive through the
//   Adaptec ASPI layer (wnaspi32.dll: GetASPI32SupportInfo / SendASPI32Command).
//   It builds standard SRB_ExecSCSICmd request blocks with hand-rolled SCSI CDBs
//   (READ TOC, READ CD, READ CD MSF, MODE SELECT speed control, TEST UNIT READY,
//   INQUIRY) and parses the replies (TOC bounds, MSF->LBA, an obfuscated
//   key-derivation over the returned LBA).
//
//   The SRB build, the CDB bytes, and the reply decode are PURE engine logic and
//   are reconstructed here 1:1 from the Hex-Rays decompile (provenance per fn).
//   They are byte-for-byte verifiable. The ONLY thing that touches hardware is
//   the actual SendASPI32Command / GetASPI32SupportInfo entry point — that is
//   routed through the DiscAspi hooks interface (see below) which defaults to an
//   inert backend, so the headless build links and the command logic is testable.
//
// THE ORIGINAL GLOBAL SRB
//   The original reuses one static SRB at byte_1460540 (0x50 = 80 bytes) for
//   every command, zeroing it each call. We mirror that exact layout in struct
//   SrbExecScsiCmd and reproduce every field write at the same offset. The
//   completion handshake in the original is:
//       dword_145BA0C = SendASPI32Command(&srb);   // dword_145A134
//       WaitForSingleObject(hEvent, -1);           // dword_145F360(dword_1459FB8)
//       return srb.status == SS_COMP (1);
//   We reproduce the SRB fill and the "status==1" success test; the send+wait is
//   the hardware boundary and is delegated to DiscAspi::sendAspiCommand().
//
// LP64 RECONCILIATION
//   The original is 32-bit: SRB_BufPointer (+0x10) and SRB_PostProc (+0x18) hold
//   a void* and a HANDLE respectively, each a 32-bit dword in the image. On a
//   64-bit host a real pointer does not fit in 32 bits. Following the established
//   reconstruction pattern, the byte-exact 0x50 image keeps those two fields as
//   placeholder u32 dwords (left 0 — the original's runtime pointer value is not
//   an engine constant and is never byte-tested), and the actual native-width
//   pointers live in dedicated slots (bufNative / postNative) OUTSIDE the 0x50
//   image. The hardware hook reads bufNative; pure logic and the golden byte
//   tests only ever touch the 0x50 image, where every count/flag/CDB byte sits
//   at its exact original offset.
//
// CLEAN-ROOM NOTE
//   Reconstructed for interoperability/preservation of a lawfully-owned copy from
//   the already-decompiled binary. Contains no circumvention keys and no disc
//   secrets — only the SCSI request/response shaping the engine itself performs.
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// SRB command codes / status / flags (Adaptec ASPI, as used by the original).
// -----------------------------------------------------------------------------
constexpr u8 kSC_EXEC_SCSI_CMD = 0x02; // byte_1460540 = 2
constexpr u8 kSC_GET_DEV_TYPE  = 0x01;
constexpr u8 kSS_PENDING       = 0x00;
constexpr u8 kSS_COMP          = 0x01; // success: srb.status == 1
constexpr u8 kSRB_DIR_IN       = 0x08; // data in  (read from device)
constexpr u8 kSRB_DIR_OUT      = 0x10; // data out (write to device)
constexpr u8 kSRB_EVENT_NOTIFY = 0x40; // signal hEvent on completion
constexpr u8 kSRB_POSTING      = 0x01;

// -----------------------------------------------------------------------------
// gilde.exe SRB_ExecSCSICmd @ byte_1460540 — exact 80-byte (0x50) image layout.
// Field offsets verified against the global writes (byte_146054x..byte_146058x):
//   +0x00 cmd, +0x01 status, +0x02 haId, +0x03 flags, +0x0C bufLen,
//   +0x10 bufPointer, +0x14 senseLen, +0x15 cdbLen, +0x18 postProc,
//   +0x30 CDB[16] (byte_1460570..byte_146057F), +0x40 sense[16] (byte_1460580..).
// (byte_1460548 target / byte_1460549 lun confirmed at +0x08 / +0x09.)
// -----------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct GUILD_PACKED SrbImage {
    u8  cmd;            // +0x00  SRB_Cmd   (kSC_EXEC_SCSI_CMD)
    u8  status;         // +0x01  SRB_Status (byte_1460541; read back, ==1 means ok)
    u8  haId;           // +0x02  SRB_HaId  (byte_1460542)
    u8  flags;          // +0x03  SRB_Flags (byte_1460543)
    u32 hdrRsvd;        // +0x04  SRB_Hdr_Rsvd
    u8  target;         // +0x08  SRB_Target (byte_1460548)
    u8  lun;            // +0x09  SRB_Lun    (byte_1460549)
    u16 rsvd0A;         // +0x0A
    u32 bufLen;         // +0x0C  SRB_BufLen   (dword_146054C)
    u32 bufPointer;     // +0x10  SRB_BufPointer (dword_1460550) — image placeholder
    u8  senseLen;       // +0x14  SRB_SenseLen (byte_1460554 = 14)
    u8  cdbLen;         // +0x15  SRB_CDBLen   (byte_1460555)
    u8  haStat;         // +0x16  SRB_HaStat
    u8  targStat;       // +0x17  SRB_TargStat
    u32 postProc;       // +0x18  SRB_PostProc (dword_1460558 = hEvent) — image placeholder
    u8  rsvd1C[0x14];   // +0x1C..+0x2F  reserved (16 bytes hdr + workspace)
    u8  cdb[16];        // +0x30  SRB_CDBByte[16] (byte_1460570..byte_146057F)
    u8  sense[16];      // +0x40  SenseArea[16]   (byte_1460580..)
};
GUILD_PACKED_END
static_assert(sizeof(SrbImage) == 0x50, "SRB image must be 0x50 bytes");

// The SRB the builders operate on: the byte-exact 0x50 image plus native-width
// slots for the two pointer-bearing fields (LP64 reconciliation, see header).
struct SrbExecScsiCmd {
    SrbImage img{};      // the 0x50-byte image (all the tested counts/flags/CDB)
    void* bufNative = nullptr;   // native value the original stores at +0x10 (data buffer)
    void* postNative = nullptr;  // native value the original stores at +0x18 (hEvent)
};

// -----------------------------------------------------------------------------
// Hardware boundary — the wnaspi32.dll entry points, behind an inert default.
//
//   sendAspiCommand(srb): SendASPI32Command — submit the SRB, run the (real
//       hardware) command, write status / sense / data-buffer back. Returns the
//       value the original keeps in dword_145BA0C (never inspected; success is
//       read from srb.img.status).
//   supportInfo(): GetASPI32SupportInfo — high byte = comp code, low byte =
//       host-adapter count.
//
// The default backend does nothing (leaves status = kSS_PENDING), so no command
// "succeeds" on a machine with no drive — exactly the headless behavior we want
// while keeping the build logic intact and testable.
// -----------------------------------------------------------------------------
struct DiscAspi {
    virtual ~DiscAspi() = default;
    virtual u32 sendAspiCommand(SrbExecScsiCmd& srb) = 0;
    virtual u32 supportInfo() = 0;
};

// Inert default: never marks a command complete.
struct InertDiscAspi : DiscAspi {
    u32 sendAspiCommand(SrbExecScsiCmd&) override { return 0; }
    u32 supportInfo() override { return 0; }
};

// Install / fetch the active backend. SetDiscAspi(nullptr) reverts to inert.
DiscAspi& ActiveDiscAspi();
void      SetDiscAspi(DiscAspi* backend);

// The default host-adapter / target / lun the original reads from
// dword_145A13C / dword_145A140 / dword_145A144 (all initialised to 0).
struct ScsiAddr { u8 haId = 0; u8 target = 0; u8 lun = 0; };
ScsiAddr& DefaultScsiAddr();

// dword_145A014 — running counter that perturbs the key/LBA obfuscation.
// (Initial value in the image is 0x113; the engine mutates it elsewhere.)
u32& ProtectionCounter();

// byte_145A790[9] — the 9-byte obfuscation table the key derivation indexes.
extern const u8 kObfTable[9];

// -----------------------------------------------------------------------------
// The reconstructed builders / parsers. Each fills the SRB exactly as the
// original, calls ActiveDiscAspi().sendAspiCommand(), and returns the same
// success value (srb.status == 1) or parses the reply identically.
// -----------------------------------------------------------------------------

// gilde.exe 0x140e610 — VIBE_Disc_AspiReadTocBounds (__stdcall)
// READ TOC (0x43), format 4 (raw), 28-byte buffer. On success, big-endian
// decode of the two returned LBAs into *outA/*outB (byteswapped). Returns 1/0.
int AspiReadTocBounds(int unused, u32* outA, u32* outB, int unused2);

// gilde.exe 0x1412460 — VIBE_Disc_AspiSetSpeed (__stdcall)
// 6-byte CDB 0x1B (START/STOP-unit-style). a2->bit0, a1->bit1 of CDB byte 4,
// dir-out. CDB[1] = 32*lun. Returns 1/0.
int AspiSetSpeed(int a1, int a2);

// gilde.exe 0x1412660 — VIBE_Disc_AspiSetReadSpeed (__stdcall)
// SET CD SPEED (0xBB). speed==256 -> 0xFFFF (max); else (1764*speed+9)/10 KB/s
// big-endian into CDB[2..3]; write-speed CDB[4..5] = 0xFFFF. Returns 1/0.
int AspiSetReadSpeed(int speed);

// gilde.exe 0x1412950 — VIBE_Disc_AspiReadTocAndDecode (__stdcall)
// READ SUB-CHANNEL/TOC MSF (0x42), 16-byte reply. Decodes MSF->LBA, subtracts
// an obf-table byte, optionally runs the nibble-shuffle key transform.
// Writes *outLba and *outDword. Returns 1/0.
int AspiReadTocAndDecode(u8 haId, u8 target, u8 lun, u8* buf,
                         u32* outLba, u32* outDword, int doTransform);

// gilde.exe 0x1412e10 — VIBE_Disc_AspiReadRawSector (__stdcall)
// READ CD-DA (0xD8) raw. mode 0->2352, 1->2368, 2->2448 byte sectors. LBA big-
// endian into CDB[2..5], subchannel select CDB[10] = {0,1,2}. Returns 1/0.
int AspiReadRawSector(u8 haId, u8 target, u8 lun, u8* buf, u32 lba, int mode);

// gilde.exe 0x1412fd0 — VIBE_Disc_AspiReadSectorEcc (__stdcall)
// READ CD (0xBE) with sync+all headers+EDC/ECC flags (CDB[9]=0xF8). On failure,
// clears 18 bytes at unk_145F380 then copies 14 bytes of sense out. Returns 1/0.
int AspiReadSectorEcc(u8 haId, u8 target, u8 lun, u8* buf, u32 lba, int mode);

// gilde.exe 0x1413430 — VIBE_Disc_AspiReadSector (__stdcall)
// READ(10) (0x28) cooked 2048 / "0" length. When mode==0, LBA is pre-perturbed
// by the obf table before the big-endian fill. Returns 1/0.
int AspiReadSector(u8 haId, u8 target, u8 lun, u8* buf, u32 lba, int mode);

// gilde.exe 0x1413790 — VIBE_Disc_AspiTestUnitReady (__stdcall)
// TEST UNIT READY (0x00), dir-out, CDB[1] = 32*lun. Returns 1/0.
int AspiTestUnitReady(u8 haId, u8 target, u8 lun, int unused);

// gilde.exe 0x1413900 — VIBE_Disc_AspiInquiry (__stdcall)
// INQUIRY (0x12), 36-byte reply into buf. cdbLen=6, CDB[4] = 36 (alloc len).
// Returns 1/0.
int AspiInquiry(u8 haId, u8 target, u8 lun, u8* buf);

// gilde.exe 0x1412120 — VIBE_Disc_EnumScsiDevices
// SetupDi*-driven Windows device enumeration loop that walks the SCSI device
// interface list and, for each, opens it and tests a predicate. The body is
// entirely OS-API (SetupDiGetClassDevs / EnumDeviceInterfaces / ...), i.e. the
// hardware boundary. Its PURE shape (the enumeration loop + the descriptor it
// primes) is reconstructed; the per-device work is routed to a hook predicate.
//
// NOTE on the name: guild::drm::EnumScsiDevices() already exists in the live tree
// as the intentional success-returning stub of this same 0x1412120 entry point
// (src/drm/drm_stub.{h,cpp}). To avoid an ODR clash in the single linked library
// while still landing the faithful 1:1 reconstruction, this version is exposed as
// EnumScsiDevicesAspi(). It is the de-stubbed body for 0x1412120; the stub's
// caller can be repointed here when the DRM cluster is switched off the stubs.
struct ScsiEnumHooks {
    virtual ~ScsiEnumHooks() = default;
    // dword_145F064(): open the SCSI device-interface class enumerator. Returns
    //   an opaque handle, or null on failure.
    virtual void* openClassDevs() = 0;
    // dword_145B910(): fallback enumerator open used when lastError()==1008.
    virtual void* openClassDevsFallback() = 0;
    // dword_145E1E8(set, ifaceFlags, 0, &detail) / dword_145B908(set, flags, &detail):
    //   prime/first enumerate; returns nonzero on success.
    virtual bool  enumFirst(void* set, u32 ifaceFlags, void* outDetail) = 0;
    // dword_145D680(): GetLastError().
    virtual u32   lastError() = 0;
    // dword_145AD1C(detail, idx, buf, inLen, &outLen): get device-interface
    //   detail; returns 0 on success (mirrors the original's "!= 0 -> fail").
    virtual int   getDetail(void* detail, u32 idx, void* buf, u32 inLen, u32* outLen) = 0;
    // dword_145A5DC(...): create the file handle for the enumerated device.
    //   Returns nonzero on success; *outHandle receives the handle.
    virtual bool  openDevice(void* spec, u32 access, u32 share, u32 flags,
                             void** outHandle) = 0;
    // dword_14605CC(deviceId, fileHandle): the per-device probe predicate. Returns
    //   nonzero when this device is the disc the protection is looking for.
    virtual int   probeDevice(u32 deviceId, void* fileHandle) = 0;
};
struct InertScsiEnumHooks : ScsiEnumHooks {
    void* openClassDevs() override { return nullptr; }
    void* openClassDevsFallback() override { return nullptr; }
    bool  enumFirst(void*, u32, void*) override { return false; }
    u32   lastError() override { return 0; }
    int   getDetail(void*, u32, void*, u32, u32*) override { return 1; }
    bool  openDevice(void*, u32, u32, u32, void**) override { return false; }
    int   probeDevice(u32, void*) override { return 0; }
};
ScsiEnumHooks& ActiveScsiEnumHooks();
void           SetScsiEnumHooks(ScsiEnumHooks* hooks);

int EnumScsiDevicesAspi();

} // namespace guild::drm
