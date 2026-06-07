#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::drm — INTENTIONAL STUB of the copy-protection cluster
// =============================================================================
//
// WHAT THIS IS
//   gilde.exe ships wrapped in a SecuROM/SafeDisc-class protection layer living
//   high in the image (the `0x140xxxx` region) under the prefixes Drm_, Disc_,
//   DiscProtect_, DiscIo_ and CopyProtect_. At runtime that layer:
//     * verifies a physical disc signature / "genuine disc present" check,
//     * fingerprints the drive by measuring raw SCSI sector read timings,
//     * enumerates SCSI/ASPI devices and tries to detect virtual/emulated drives,
//     * installs a vectored-exception "opcode VM" (VIBE_Drm_ProtectionMain) whose
//       per-instruction trap handler re-derives and executes obfuscated code, and
//     * decrypts an on-disk "overlay" of the real game .text back into memory
//       before the game runs (VIBE_Drm_DecryptOverlay).
//
// WHY IT IS STUBBED (PLAN §1, §3, §8)
//   The protection layer talks directly to the OS / SCSI hardware and to a
//   physical disc that does not exist in this preservation/interop build of a
//   legally-owned game. None of it is game logic — it gates whether the game
//   proceeds. Per PLAN we replace the whole cluster with no-op stubs that report
//   the same "authorized / genuine disc present" results the callers expect, so
//   control flows straight into the game exactly as it would on an authentic disc.
//
//   This is a clean-room reimplementation for interoperability and preservation
//   of a lawfully-owned copy. It contains NO circumvention keys, NO disc layout
//   secrets, and NO copy of the protected payload — only stubs returning success.
//
// OVERLAY-DECRYPTION FINDING (resolves PLAN open-question #1)
//   PLAN §8.1 asked us to confirm the overlay decryptor does NOT decrypt real
//   game .text. FINDING: it DOES. VIBE_Drm_DecryptOverlay (@0x140bc10) rewrites
//   byte ranges that begin at 0x00401000 — the literal start of the game's real
//   .text section — and at 0x00432101 / 0x00442103, all inside the low game-code
//   region (imagebase 0x400000). Those ranges are delimited by the "NopStub"
//   functions, which are NOT inert: each is a 21-byte address-marker sled
//   (`mov edi, <lo>; mov esi, <hi>`) whose immediates carry the range bounds the
//   decryptor consumes (e.g. NopStub@0x140c380 -> 0x401000..0x4020AA).
//
//   This does NOT block stubbing, because the reconstruction is built from the
//   ALREADY-DECRYPTED image: every function in those ranges (e.g.
//   VIBE_Character_ResolveMesh @0x4013fc) decompiles as ordinary, plaintext code
//   and is being reimplemented directly as normal C++. There is therefore nothing
//   left to decrypt at our runtime — the decrypt step is a no-op for us, and the
//   stub below simply skips it. See the full write-up returned with this module.
//
// CONVENTIONS
//   Each entry point keeps its original address + symbol provenance comment and a
//   one-line note of what the original did. Stubs return the success / "genuine
//   disc present" sentinel the caller checks for. Namespace per CONVENTIONS:
//   guild::drm.
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// Success sentinels the original callers test for. Centralised so the meaning is
// explicit at each return site.
// -----------------------------------------------------------------------------

// VIBE_Drm_Main returns 0 on the success path (disc OK, game proceeds);
// it returns 11 only when no playback device is found.
constexpr int kDrmMainAuthorized = 0;

// The disc-verify routines (VIBE_Drm_VerifyDisc / VIBE_DiscProtect_VerifyDisc /
// VIBE_CopyProtect_*) report "genuine disc present" by setting the validity bit
// 0x40000 in the global status word off_145A018 and returning it non-zero; a
// zero return drives the failure path (VIBE_Crt_Exit). We return that bit value.
constexpr u32 kDiscGenuineFlag = 0x40000u;

// Generic "operation succeeded" return used by the many SCSI/ASPI/SPTI helpers,
// whose callers treat any non-zero as success.
constexpr int kDiscIoOk = 1;

// "Disc media is present / drive ready."
constexpr int kMediaPresent = 1;

// -----------------------------------------------------------------------------
// Top-level entry points (the only things the rest of the game reaches).
// -----------------------------------------------------------------------------

// gilde.exe 0x1414b20 — VIBE_Drm_Main  (__stdcall(int,int,int,int))
// Original: orchestrated the entire protection bring-up — loaded obfuscated
// libraries, resolved imports, opened the \\.\PLAYBACK device, decrypted the
// overlay, installed the opcode VM, ran disc-signature/checksum/timing
// verification, then fell through into the game. Called once from CRT startup
// (@0x1422615). Stub: report authorized so startup proceeds straight to the game.
int Main(int hInstance, int prevInstance, int cmdLine, int showCmd);

// gilde.exe 0x1411260 — VIBE_Drm_ProtectionMain  (__usercall, eax = result)
// Original: per-instruction trap handler of the SecuROM-style opcode VM; the
// game's overlay re-entered here via a vectored exception on each protected
// instruction to re-derive and dispatch the next operation. Stub: no VM exists in
// the decrypted reconstruction; this is never reached, so it is a harmless no-op.
int ProtectionMain();

// -----------------------------------------------------------------------------
// Disc / signature verification (the actual "is the genuine disc present?" gate).
// -----------------------------------------------------------------------------

// gilde.exe 0x140c3a0 — VIBE_Drm_VerifyDisc  (__stdcall(int,int,int,int))
// Original: read protected sectors, descrambled key tables and compared a disc
// signature to decide authenticity. Stub: always "genuine".
int VerifyDisc(int driveHandle, int outSig, int keyTable, int fileHandle);

// gilde.exe 0x1417dd0 — VIBE_DiscProtect_VerifyDisc  (__stdcall(int,unsigned,int,int*))
// Original: high-level disc verification driving sector-table build + timing scan.
// Stub: always succeeds.
int DiscProtectVerifyDisc(int a1, u32 a2, int a3, int* outResult);

// gilde.exe 0x141c720 — VIBE_CopyProtect_VerifyDiscSignature
// Original: read the disc's TOC/signature, compared it against an expected value
// and set/cleared the validity bit 0x40000 (Crt_Exit(7) on mismatch).
// Stub: report the disc signature as valid.
u32 VerifyDiscSignature();

// gilde.exe 0x141c980 — VIBE_CopyProtect_VerifySectorChecksum
// Original: summed a protected sector region and matched it against a checksum
// table (Crt_Exit(8) on mismatch). Stub: report the checksum as valid.
int VerifySectorChecksum();

// gilde.exe 0x1411eb0 — VIBE_Drm_CompareSignature
// Original: compared a freshly computed signature against the stored one.
// Stub: report a match (1 == equal/valid).
int CompareSignature();

// -----------------------------------------------------------------------------
// Authentication / overlay machinery (no-ops in the decrypted reconstruction).
// -----------------------------------------------------------------------------

// gilde.exe 0x1419eb0 — VIBE_DiscProtect_RunAuthentication
// Original: ran the full disc-authentication state machine (sector map, timing
// signature, key derivation). Stub: nothing to authenticate; no-op.
void RunAuthentication();

// gilde.exe 0x140bc10 — VIBE_Drm_DecryptOverlay
// Original: read encrypted .text ranges (start 0x401000) from the EXE, applied
// XOR/add transforms keyed off the disc, and wrote the plaintext back into the
// running image. See OVERLAY-DECRYPTION FINDING above. Stub: the reconstruction
// is already plaintext, so there is nothing to decrypt — no-op.
void DecryptOverlay();

// gilde.exe 0x140ec90 — VIBE_Drm_DecryptKeyTable
// Original: decrypted the per-title key table used by the overlay/VM.
// Stub: no key table needed; return success.
int DecryptKeyTable();

// gilde.exe 0x140fa60 — VIBE_Drm_InitProtection  (__stdcall(int,int))
// Original: initialised protection state for a VM dispatch. Stub: succeed.
int InitProtection(int a1, int a2);

// -----------------------------------------------------------------------------
// Hardware / drive probing (no real hardware in this build).
// -----------------------------------------------------------------------------

// gilde.exe 0x14139e0 — VIBE_Drm_MeasureSectorTiming  (__stdcall(int))
// Original: timed raw SCSI sector reads to fingerprint the physical medium.
// Stub: no timing fingerprint; report success.
int MeasureSectorTiming(int a1);

// gilde.exe 0x1411f20 — VIBE_Drm_VerifyPlaybackDevice  (__stdcall(int))
// Original: confirmed the \\.\PLAYBACK disc device was usable. Stub: device OK.
int VerifyPlaybackDevice(int a1);

// gilde.exe 0x1412120 — VIBE_Disc_EnumScsiDevices
// Original: enumerated SCSI devices to find the disc drive. Stub: pretend a
// suitable drive exists.
int EnumScsiDevices();

// gilde.exe 0x14136f0 — VIBE_Disc_CheckMediaPresent  (__stdcall(int))
// Original: TEST UNIT READY — is a disc loaded? Stub: media present.
int CheckMediaPresent(int driveHandle);

// gilde.exe 0x141e6a0 — VIBE_CopyProtect_DetectVirtualDrive
// Original: scanned for emulator/virtual-drive signatures (Daemon Tools etc.).
// Stub: report a clean, physical drive (no emulator detected).
int DetectVirtualDrive();

// gilde.exe 0x141c540 — VIBE_CopyProtect_ProbeDriveGeometry
// Original: probed drive geometry / capabilities. Stub: no-op.
void ProbeDriveGeometry();

// gilde.exe 0x141c3b0 — VIBE_CopyProtect_SetSpeedParams
// Original: configured drive read-speed parameters for timing measurement.
// Stub: no-op.
void SetSpeedParams();

// gilde.exe 0x141e0d0 — VIBE_CopyProtect_ScanScsiDevices
// Original: scanned/inventoried SCSI devices via ASPI. Stub: succeed.
int ScanScsiDevices();

} // namespace guild::drm
